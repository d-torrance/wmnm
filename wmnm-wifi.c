/* wmnm - NetworkManager frontend as a Window Maker dockapp
 * Copyright (C) 2018 Doug Torrance <dtorrance@piedmont.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include "wmnm-wifi.h"
#include "wmnm-ui.h"

/* NetworkManager throttles scans of its own accord; asking more often than
   this just generates errors. */
#define SCAN_INTERVAL_MSEC 20000
#define SCAN_TIMER_SECONDS 30

struct WifiView {
	NMDeviceWifi *device;
	GPtrArray *entries;		/* ApEntry *, sorted by ap_compare() */
	GBytes *cursor_ssid;		/* what the cursor is on, by name */
	guint cursor;
	guint scroll_top;
	gboolean visible;
	guint scan_timer;
	GCancellable *cancel;
	NMAccessPoint *watched_ap;	/* the one whose strength we follow */
	gulong strength_handler;
};

static void ap_entry_free(gpointer data)
{
	ApEntry *entry = data;

	g_bytes_unref(entry->ssid);
	g_free(entry->label);
	g_clear_object(&entry->best);
	g_free(entry);
}

gboolean wmnm_ap_is_secure(const ApEntry *entry)
{
	return (entry->flags & NM_802_11_AP_FLAGS_PRIVACY) ||
		entry->wpa || entry->rsn;
}

gboolean wmnm_ap_is_enterprise(const ApEntry *entry)
{
	guint32 security = entry->wpa | entry->rsn;

	return (security & (NM_802_11_AP_SEC_KEY_MGMT_802_1X |
			    NM_802_11_AP_SEC_KEY_MGMT_EAP_SUITE_B_192)) != 0;
}

/* Signal strength wobbles by a few percent from one scan to the next.  Sorting
   on the raw value makes the list reshuffle constantly, so compare coarse
   buckets and fall back to the name for a stable order within a bucket. */
static gint ap_compare(gconstpointer a, gconstpointer b)
{
	const ApEntry *x = *(ApEntry * const *)a;
	const ApEntry *y = *(ApEntry * const *)b;

	if (x->active != y->active)
		return x->active ? -1 : 1;

	if (x->strength / 20 != y->strength / 20)
		return (gint)(y->strength / 20) - (gint)(x->strength / 20);

	return g_strcmp0(x->label, y->label);
}

static void rebuild_entries(struct WifiView *wifi)
{
	const GPtrArray *aps;
	NMAccessPoint *active;
	GHashTable *seen;
	GHashTableIter iter;
	gpointer value;
	guint i;

	active = nm_device_wifi_get_active_access_point(wifi->device);
	aps = nm_device_wifi_get_access_points(wifi->device);

	/* Key on the raw SSID bytes.  nm_utils_ssid_to_utf8() is lossy, so two
	   genuinely different networks can share a display label. */
	seen = g_hash_table_new(g_bytes_hash, g_bytes_equal);

	for (i = 0; aps && i < aps->len; i++) {
		NMAccessPoint *ap = g_ptr_array_index(aps, i);
		GBytes *ssid = nm_access_point_get_ssid(ap);
		guint8 strength = nm_access_point_get_strength(ap);
		ApEntry *entry;

		if (!ssid || g_bytes_get_size(ssid) == 0)
			continue;	/* hidden network */

		entry = g_hash_table_lookup(seen, ssid);
		if (!entry) {
			entry = g_new0(ApEntry, 1);
			entry->ssid = g_bytes_ref(ssid);
			entry->label = nm_utils_ssid_to_utf8(
				g_bytes_get_data(ssid, NULL),
				g_bytes_get_size(ssid));
			g_hash_table_insert(seen, entry->ssid, entry);
		}

		/* Merge this BSSID into the network's entry. */
		entry->flags |= nm_access_point_get_flags(ap);
		entry->wpa |= nm_access_point_get_wpa_flags(ap);
		entry->rsn |= nm_access_point_get_rsn_flags(ap);
		if (ap == active)
			entry->active = TRUE;
		if (!entry->best || strength > entry->strength) {
			g_clear_object(&entry->best);
			entry->best = g_object_ref(ap);
			entry->strength = strength;
		}
	}

	g_ptr_array_set_size(wifi->entries, 0);
	g_hash_table_iter_init(&iter, seen);
	while (g_hash_table_iter_next(&iter, NULL, &value))
		g_ptr_array_add(wifi->entries, value);
	g_hash_table_destroy(seen);

	g_ptr_array_sort(wifi->entries, ap_compare);
}

/* Indices are meaningless across a rebuild, so the cursor is remembered by
   SSID and relocated afterwards. */
static void restore_cursor(struct WifiView *wifi)
{
	guint i;

	if (wifi->entries->len == 0) {
		wifi->cursor = 0;
		wifi->scroll_top = 0;
		return;
	}

	if (wifi->cursor_ssid) {
		for (i = 0; i < wifi->entries->len; i++) {
			ApEntry *entry = g_ptr_array_index(wifi->entries, i);

			if (g_bytes_equal(entry->ssid, wifi->cursor_ssid)) {
				wifi->cursor = i;
				goto clamp;
			}
		}
	}

	/* The network the cursor was on is gone; stay near where we were. */
	if (wifi->cursor >= wifi->entries->len)
		wifi->cursor = wifi->entries->len - 1;

clamp:
	if (wifi->cursor < wifi->scroll_top)
		wifi->scroll_top = wifi->cursor;
	else if (wifi->cursor >= wifi->scroll_top + WMNM_AP_ROWS)
		wifi->scroll_top = wifi->cursor - WMNM_AP_ROWS + 1;

	if (wifi->entries->len <= WMNM_AP_ROWS)
		wifi->scroll_top = 0;
	else if (wifi->scroll_top > wifi->entries->len - WMNM_AP_ROWS)
		wifi->scroll_top = wifi->entries->len - WMNM_AP_ROWS;

	g_clear_pointer(&wifi->cursor_ssid, g_bytes_unref);
	if (wifi->cursor < wifi->entries->len) {
		ApEntry *entry = g_ptr_array_index(wifi->entries, wifi->cursor);

		wifi->cursor_ssid = g_bytes_ref(entry->ssid);
	}
}

static void on_strength_changed(GObject *object, GParamSpec *pspec,
				gpointer user_data)
{
	(void)object;
	(void)pspec;
	(void)user_data;

	wmnm_queue_render();
}

/* Follow the signal strength of the access point we are associated with, so
   the bars on the device view stay live.  Only this one access point is
   watched: subscribing to every access point in range would mean a redraw
   storm for readings that are not on screen. */
static void watch_active_ap(struct WifiView *wifi)
{
	NMAccessPoint *active;

	active = nm_device_wifi_get_active_access_point(wifi->device);
	if (active == wifi->watched_ap)
		return;

	if (wifi->watched_ap) {
		g_signal_handler_disconnect(wifi->watched_ap,
					    wifi->strength_handler);
		g_clear_object(&wifi->watched_ap);
		wifi->strength_handler = 0;
	}

	if (active) {
		wifi->watched_ap = g_object_ref(active);
		wifi->strength_handler = g_signal_connect(
			active, "notify::" NM_ACCESS_POINT_STRENGTH,
			G_CALLBACK(on_strength_changed), wifi);
	}
}

static void refresh(struct WifiView *wifi)
{
	rebuild_entries(wifi);
	restore_cursor(wifi);
	watch_active_ap(wifi);
	wmnm_queue_render();
}

/* access-point-added and -removed pass the access point itself, so the
   handler takes three arguments even though we only need to know that the
   list changed. */
static void on_aps_changed(NMDeviceWifi *device, NMAccessPoint *ap,
			   gpointer user_data)
{
	(void)device;
	(void)ap;

	refresh(user_data);
}

static void on_notify(GObject *object, GParamSpec *pspec, gpointer user_data)
{
	(void)object;
	(void)pspec;

	refresh(user_data);
}

static void on_scan_done(GObject *object, GAsyncResult *result,
			 gpointer user_data)
{
	GError *error = NULL;

	(void)user_data;

	/* NetworkManager reports an error when it declines a scan because one
	   happened recently.  That is not interesting to us. */
	if (!nm_device_wifi_request_scan_finish(NM_DEVICE_WIFI(object), result,
						&error))
		g_clear_error(&error);
}

static void maybe_scan(struct WifiView *wifi)
{
	gint64 last;

	last = nm_device_wifi_get_last_scan(wifi->device);
	if (last != -1 && nm_utils_get_timestamp_msec() - last <
	    SCAN_INTERVAL_MSEC)
		return;

	nm_device_wifi_request_scan_async(wifi->device, wifi->cancel,
					  on_scan_done, wifi);
}

static gboolean scan_timer(gpointer user_data)
{
	maybe_scan(user_data);

	return G_SOURCE_CONTINUE;
}

void wmnm_wifi_attach(Device *d)
{
	struct WifiView *wifi;

	if (!NM_IS_DEVICE_WIFI(d->device))
		return;

	wifi = g_new0(struct WifiView, 1);
	wifi->device = NM_DEVICE_WIFI(d->device);
	wifi->entries = g_ptr_array_new_with_free_func(ap_entry_free);
	wifi->cancel = g_cancellable_new();
	d->wifi = wifi;

	g_signal_connect(d->device, "access-point-added",
			 G_CALLBACK(on_aps_changed), wifi);
	g_signal_connect(d->device, "access-point-removed",
			 G_CALLBACK(on_aps_changed), wifi);
	g_signal_connect(d->device, "notify::" NM_DEVICE_WIFI_ACTIVE_ACCESS_POINT,
			 G_CALLBACK(on_notify), wifi);
	g_signal_connect(d->device, "notify::" NM_DEVICE_WIFI_LAST_SCAN,
			 G_CALLBACK(on_notify), wifi);

	rebuild_entries(wifi);
	restore_cursor(wifi);
	watch_active_ap(wifi);
}

void wmnm_wifi_enter(Device *d)
{
	struct WifiView *wifi = d ? d->wifi : NULL;

	if (!wifi || wifi->visible)
		return;

	wifi->visible = TRUE;
	refresh(wifi);
	maybe_scan(wifi);
	wifi->scan_timer = g_timeout_add_seconds(SCAN_TIMER_SECONDS,
						 scan_timer, wifi);
}

void wmnm_wifi_leave(Device *d)
{
	struct WifiView *wifi = d ? d->wifi : NULL;

	if (!wifi || !wifi->visible)
		return;

	wifi->visible = FALSE;
	if (wifi->scan_timer) {
		g_source_remove(wifi->scan_timer);
		wifi->scan_timer = 0;
	}
}

void wmnm_wifi_scroll(Device *d, int delta)
{
	struct WifiView *wifi = d ? d->wifi : NULL;
	gint cursor;

	if (!wifi || wifi->entries->len == 0)
		return;

	cursor = (gint)wifi->cursor + delta;
	if (cursor < 0)
		cursor = 0;
	if ((guint)cursor >= wifi->entries->len)
		cursor = wifi->entries->len - 1;
	wifi->cursor = cursor;

	g_clear_pointer(&wifi->cursor_ssid, g_bytes_unref);
	wifi->cursor_ssid = g_bytes_ref(
		((ApEntry *)g_ptr_array_index(wifi->entries,
					      wifi->cursor))->ssid);

	if (wifi->cursor < wifi->scroll_top)
		wifi->scroll_top = wifi->cursor;
	else if (wifi->cursor >= wifi->scroll_top + WMNM_AP_ROWS)
		wifi->scroll_top = wifi->cursor - WMNM_AP_ROWS + 1;

	wmnm_queue_render();
}

void wmnm_wifi_select_row(Device *d, guint row)
{
	struct WifiView *wifi = d ? d->wifi : NULL;

	if (!wifi || wifi->scroll_top + row >= wifi->entries->len)
		return;

	wmnm_wifi_scroll(d, (gint)(wifi->scroll_top + row) - (gint)wifi->cursor);
}

/* Dragging the scrollbar moves the view, not the selection, but the cursor
   must stay on a visible row or the next rebuild would scroll us back. */
void wmnm_wifi_set_scroll_top(Device *d, guint top)
{
	struct WifiView *wifi = d ? d->wifi : NULL;
	guint max_top;

	if (!wifi || wifi->entries->len <= WMNM_AP_ROWS)
		return;

	max_top = wifi->entries->len - WMNM_AP_ROWS;
	if (top > max_top)
		top = max_top;

	if (top == wifi->scroll_top)
		return;

	wifi->scroll_top = top;

	if (wifi->cursor < top)
		wifi->cursor = top;
	else if (wifi->cursor >= top + WMNM_AP_ROWS)
		wifi->cursor = top + WMNM_AP_ROWS - 1;

	g_clear_pointer(&wifi->cursor_ssid, g_bytes_unref);
	wifi->cursor_ssid = g_bytes_ref(
		((ApEntry *)g_ptr_array_index(wifi->entries,
					      wifi->cursor))->ssid);

	wmnm_queue_render();
}

const GPtrArray *wmnm_wifi_entries(Device *d)
{
	struct WifiView *wifi = d ? d->wifi : NULL;

	return wifi ? wifi->entries : NULL;
}

guint wmnm_wifi_cursor(Device *d)
{
	struct WifiView *wifi = d ? d->wifi : NULL;

	return wifi ? wifi->cursor : 0;
}

guint wmnm_wifi_scroll_top(Device *d)
{
	struct WifiView *wifi = d ? d->wifi : NULL;

	return wifi ? wifi->scroll_top : 0;
}

ApEntry *wmnm_wifi_selected(Device *d)
{
	struct WifiView *wifi = d ? d->wifi : NULL;

	if (!wifi || wifi->cursor >= wifi->entries->len)
		return NULL;

	return g_ptr_array_index(wifi->entries, wifi->cursor);
}
