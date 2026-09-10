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

#include <string.h>

#include "wmnm-connect.h"
#include "wmnm-ui.h"

#define STATUS_SECONDS 5

static NMClient *nm_client;
static gboolean portal_opened;
static View status_next_view = VIEW_DEVICE;
static char *status_line1, *status_line2;
static guint status_timer;

static gboolean status_expired(gpointer user_data)
{
	(void)user_data;

	status_timer = 0;
	g_clear_pointer(&status_line1, g_free);
	g_clear_pointer(&status_line2, g_free);

	if (current_view == VIEW_STATUS)
		wmnm_set_view(status_next_view);
	wmnm_queue_render();

	return G_SOURCE_REMOVE;
}

/* next is where to go when the message times out: back to the list when the
   user still has a choice to make, otherwise to the normal device view. */
static void set_status_next(const char *line1, const char *line2, View next)
{
	g_free(status_line1);
	g_free(status_line2);
	status_line1 = g_strdup(line1);
	status_line2 = g_strdup(line2);
	status_next_view = next;

	wmnm_set_view(VIEW_STATUS);

	if (status_timer)
		g_source_remove(status_timer);
	status_timer = g_timeout_add_seconds(STATUS_SECONDS, status_expired,
					     NULL);

	wmnm_queue_render();
}

static void set_status(const char *line1, const char *line2)
{
	set_status_next(line1, line2, VIEW_DEVICE);
}

const char *wmnm_status_line1(void)
{
	return status_line1;
}

const char *wmnm_status_line2(void)
{
	return status_line2;
}

gboolean wmnm_portal_active(void)
{
	return nm_client &&
		nm_client_get_connectivity(nm_client) == NM_CONNECTIVITY_PORTAL;
}

void wmnm_portal_open(void)
{
	const char *uri;
	char *argv[3];

	if (!nm_client)
		return;

	/* This is the URI NetworkManager itself probes.  When a portal is
	   intercepting traffic, fetching it is what triggers the redirect to
	   the login page. */
	uri = nm_client_connectivity_check_get_uri(nm_client);
	if (!uri) {
		set_status("captive", "portal");
		return;
	}

	argv[0] = (char *)"xdg-open";
	argv[1] = (char *)uri;
	argv[2] = NULL;

	if (g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
			  NULL, NULL))
		set_status("captive", "portal");
	else
		set_status("portal", "open failed");
}

/* Connectivity can flap, and opening a browser tab on every transition would
   be intolerable, so this fires at most once per activation.  The badge stays
   clickable for a second look. */
static void on_connectivity(GObject *object, GParamSpec *pspec,
			    gpointer user_data)
{
	(void)object;
	(void)pspec;
	(void)user_data;

	if (wmnm_portal_active()) {
		if (!portal_opened) {
			portal_opened = TRUE;
			wmnm_portal_open();
		}
	} else if (nm_client_get_connectivity(nm_client) ==
		   NM_CONNECTIVITY_FULL) {
		portal_opened = FALSE;
	}

	wmnm_queue_render();
}

void wmnm_connect_init(NMClient *client)
{
	nm_client = client;

	g_signal_connect(client, "notify::" NM_CLIENT_CONNECTIVITY,
			 G_CALLBACK(on_connectivity), NULL);
}

/* Order matters here: an access point can advertise several key management
   options at once, and we want the strongest one we understand. */
ApSecurity wmnm_ap_security(const ApEntry *entry)
{
	guint32 security = entry->wpa | entry->rsn;

	if (security & (NM_802_11_AP_SEC_KEY_MGMT_802_1X |
			NM_802_11_AP_SEC_KEY_MGMT_EAP_SUITE_B_192))
		return AP_SEC_ENTERPRISE;
	if (security & NM_802_11_AP_SEC_KEY_MGMT_SAE)
		return AP_SEC_SAE;
	if (security & NM_802_11_AP_SEC_KEY_MGMT_PSK)
		return AP_SEC_PSK;
	if (security & (NM_802_11_AP_SEC_KEY_MGMT_OWE |
			NM_802_11_AP_SEC_KEY_MGMT_OWE_TM))
		return AP_SEC_OWE;
	if (entry->flags & NM_802_11_AP_FLAGS_PRIVACY)
		return AP_SEC_WEP;

	return AP_SEC_OPEN;
}

/* NetworkManager has no way to preselect an SSID in the editor, so the user
   has to retype it.  It is still better than leaving them stuck. */
static void launch_editor(const ApEntry *entry)
{
	char *argv_editor[] = {(char *)"nm-connection-editor", (char *)"--create",
			       (char *)"--type=802-11-wireless", NULL};
	char *argv_nmtui[] = {(char *)"x-terminal-emulator", (char *)"-e",
			      (char *)"nmtui", (char *)"connect", NULL};

	if (g_spawn_async(NULL, argv_editor, NULL, G_SPAWN_SEARCH_PATH, NULL,
			  NULL, NULL, NULL))
		set_status_next("use editor", entry->label, VIEW_APLIST);
	else if (g_spawn_async(NULL, argv_nmtui, NULL, G_SPAWN_SEARCH_PATH,
			       NULL, NULL, NULL, NULL))
		set_status_next("use nmtui", entry->label, VIEW_APLIST);
	else
		set_status_next("802.1x", "unsupported", VIEW_APLIST);
}

static NMRemoteConnection *find_connection(NMDevice *device, NMAccessPoint *ap)
{
	const GPtrArray *all = nm_client_get_connections(nm_client);
	GPtrArray *usable;
	NMRemoteConnection *found = NULL;
	guint i;

	usable = nm_access_point_filter_connections(ap, all);
	for (i = 0; i < usable->len; i++) {
		NMConnection *connection = g_ptr_array_index(usable, i);

		if (nm_device_connection_valid(device, connection)) {
			found = NM_REMOTE_CONNECTION(connection);
			break;
		}
	}
	g_ptr_array_unref(usable);

	return found;
}

static void on_activated(GObject *object, GAsyncResult *result,
			 gpointer user_data)
{
	GError *error = NULL;
	NMActiveConnection *active;
	char *label = user_data;

	active = nm_client_activate_connection_finish(NM_CLIENT(object), result,
						      &error);
	if (!active) {
		set_status_next("failed", error->message, VIEW_APLIST);
		g_clear_error(&error);
	} else {
		g_object_unref(active);
	}

	g_free(label);
}

static void on_added_activated(GObject *object, GAsyncResult *result,
			       gpointer user_data)
{
	GError *error = NULL;
	NMActiveConnection *active;
	char *label = user_data;

	active = nm_client_add_and_activate_connection2_finish(
		NM_CLIENT(object), result, NULL, &error);
	if (!active) {
		set_status_next("failed", error->message, VIEW_APLIST);
		g_clear_error(&error);
	} else {
		g_object_unref(active);
	}

	g_free(label);
}

/* Build just enough of a connection for NetworkManager to fill in the rest.
   The passphrase is deliberately left out: its absence is what makes NM ask
   a secret agent for it. */
static NMConnection *build_connection(const ApEntry *entry,
				      ApSecurity security)
{
	NMConnection *connection = nm_simple_connection_new();
	NMSettingConnection *s_con;
	NMSettingWireless *s_wifi;
	NMSettingWirelessSecurity *s_sec;

	s_con = (NMSettingConnection *)nm_setting_connection_new();
	g_object_set(s_con,
		     NM_SETTING_CONNECTION_ID, entry->label,
		     NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIRELESS_SETTING_NAME,
		     NULL);
	nm_connection_add_setting(connection, NM_SETTING(s_con));

	s_wifi = (NMSettingWireless *)nm_setting_wireless_new();
	g_object_set(s_wifi, NM_SETTING_WIRELESS_SSID, entry->ssid, NULL);
	nm_connection_add_setting(connection, NM_SETTING(s_wifi));

	if (security == AP_SEC_PSK || security == AP_SEC_SAE ||
	    security == AP_SEC_OWE) {
		s_sec = (NMSettingWirelessSecurity *)
			nm_setting_wireless_security_new();
		g_object_set(s_sec, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT,
			     security == AP_SEC_SAE ? "sae" :
			     security == AP_SEC_OWE ? "owe" : "wpa-psk",
			     NULL);
		nm_connection_add_setting(connection, NM_SETTING(s_sec));
	}

	return connection;
}

/* The reason code is where the useful detail lives; new_state alone cannot
   distinguish a wrong password from the network going away. */
static void on_state_changed(NMDevice *device, guint new_state, guint old_state,
			     guint reason, gpointer user_data)
{
	(void)device;
	(void)old_state;
	(void)user_data;

	switch (new_state) {
	case NM_DEVICE_STATE_ACTIVATED:
		set_status("connected", NULL);
		break;
	case NM_DEVICE_STATE_PREPARE:
		portal_opened = FALSE;
		break;
	case NM_DEVICE_STATE_FAILED:
		if (reason == NM_DEVICE_STATE_REASON_NO_SECRETS)
			set_status_next("wrong", "password", VIEW_APLIST);
		else
			set_status_next("failed", NULL, VIEW_APLIST);
		break;
	case NM_DEVICE_STATE_NEED_AUTH:
		set_status("authenticating", NULL);
		break;
	default:
		/* Redraw anyway: the activation LED tracks device state. */
		wmnm_queue_render();
		return;
	}
}

void wmnm_connect_watch_device(NMDevice *device)
{
	g_signal_connect(device, "state-changed",
			 G_CALLBACK(on_state_changed), NULL);
}

void wmnm_connect_to(Device *d, ApEntry *entry)
{
	ApSecurity security;
	NMRemoteConnection *existing;
	const char *ap_path;

	if (!entry || !entry->best)
		return;

	if (entry->active) {
		set_status("connected", entry->label);
		return;
	}

	security = wmnm_ap_security(entry);
	if (security == AP_SEC_ENTERPRISE) {
		launch_editor(entry);
		return;
	}
	if (security == AP_SEC_WEP) {
		set_status_next("WEP not", "supported", VIEW_APLIST);
		return;
	}

	ap_path = nm_object_get_path(NM_OBJECT(entry->best));

	/* Pinning the access point keeps NetworkManager on the BSSID the user
	   actually picked. */
	existing = find_connection(d->device, entry->best);
	if (existing) {
		set_status("connecting", entry->label);
		nm_client_activate_connection_async(
			nm_client, NM_CONNECTION(existing), d->device, ap_path,
			NULL, on_activated, g_strdup(entry->label));
	} else {
		NMConnection *partial = build_connection(entry, security);

		set_status("connecting", entry->label);
		nm_client_add_and_activate_connection2(
			nm_client, partial, d->device, ap_path, NULL, NULL,
			on_added_activated, g_strdup(entry->label));
		g_object_unref(partial);
	}
}
