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

#include <libdockapp/dockapp.h>
#include <X11/Xft/Xft.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wmnm.h"
#include "wmnm-ui.h"
#include "wmnm-wifi.h"
#include "wmnm-connect.h"
#include "wmnm_master.xpm"

static char *led_on_xpm[] = {
	"4 4 4 1",
	" 	c None",
	".	c #181818",
	"+	c #00E500",
	"@	c #F7F7FF",
	".++.",
	"+@++",
	"++++",
	".++."};

static char *led_off_xpm[] = {
	"4 4 4 1",
	" 	c None",
	".	c #181818",
	"+	c #003900",
	"@	c #48484A",
	".++.",
	"+@++",
	"++++",
	".++."};

/* master is the untouched background; frame is what we draw a face into and
   hand to DASetPixmap().  Rendering always starts by restoring frame from
   master, so a view never has to undo what a previous view drew. */
static Pixmap master, frame, led_on, led_off;

static GC bg_gc, fg_gc, dim_gc;
static XftDraw *xft_draw;
static XftFont *xft_font;
static XftColor xft_fg, xft_bg;

static guint render_idle_id;

static void draw_string_clipped(XftColor *color, const char *str, int x, int y,
				int clip_x, int clip_width);

/* Marquee state for the selected row.  Only about seven characters fit, which
   is not enough to tell "KINETIC_7_9db161" from "KINETIC_LM_9db161", so the
   selected row scrolls its label back and forth. */
#define MARQUEE_INTERVAL_MSEC 300
#define MARQUEE_PAUSE_TICKS 3

/* One of these per scrolling label: the selected row in the list, and the
   network name on the device view, scroll independently. */
typedef struct {
	guint timer;
	int offset;
	int limit;
	int pause;
	int step;
	char *label;			/* what is scrolling, to spot changes */
} Marquee;

/* One per field whose text we do not control, which is most of them: an
   interface name, a network name, a device description and NetworkManager's
   error strings can all be wider than 54 pixels. */
enum {
	MARQUEE_IFACE,
	MARQUEE_SSID,
	MARQUEE_AP,
	MARQUEE_DESCRIPTION,
	MARQUEE_STATUS1,
	MARQUEE_STATUS2,
	MARQUEE_COUNT
};

static Marquee marquees[MARQUEE_COUNT];
static gboolean hovering;

static void draw_scrolling(int which, XftColor *color, const char *str, int x,
			   int y, int width);

static GC solid_gc(char *color)
{
	XGCValues values;

	values.foreground = DAGetColor(color);

	return XCreateGC(DADisplay, frame, GCForeground, &values);
}

void wmnm_ui_init(void)
{
	Colormap cmap;
	short unsigned int w, h;

	DAMakePixmapFromData(wmnm_master_xpm, &master, NULL, &w, &h);
	DAMakePixmapFromData(wmnm_master_xpm, &frame, NULL, &w, &h);
	DAMakePixmapFromData(led_on_xpm, &led_on, NULL, &w, &h);
	DAMakePixmapFromData(led_off_xpm, &led_off, NULL, &w, &h);

	bg_gc = solid_gc((char *)DEFAULT_BGCOLOR);
	fg_gc = solid_gc((char *)DEFAULT_FGCOLOR);
	dim_gc = solid_gc((char *)DEFAULT_DIMCOLOR);

	cmap = DefaultColormap(DADisplay, DefaultScreen(DADisplay));
	xft_font = XftFontOpenName(DADisplay, DefaultScreen(DADisplay),
				   "mono:pixelsize=9");
	XftColorAllocName(DADisplay, DAVisual, cmap, DEFAULT_FGCOLOR, &xft_fg);
	XftColorAllocName(DADisplay, DAVisual, cmap, DEFAULT_BGCOLOR, &xft_bg);
	xft_draw = XftDrawCreate(DADisplay, frame, DAVisual, cmap);

	wmnm_ui_stop_animations();	/* sets each marquee to a sane start */
}

static void clear_rectangle(int x, int y, unsigned int width,
			    unsigned int height)
{
	XFillRectangle(DADisplay, frame, bg_gc, x, y, width, height);
}

static void draw_string_in(XftColor *color, const char *str, int x, int y)
{
	XftDrawString8(xft_draw, color, xft_font, x, y,
		       (const FcChar8 *)str, strlen(str));
}

static void draw_string(const char *str, int x, int y)
{
	draw_string_in(&xft_fg, str, x, y);
}

static void draw_signal(guint8 strength)
{
	int lit;
	GC gc = fg_gc;

	lit = floor(0.26 * strength + 0.5);	/* 100 strength = all 26 bars */

	for (int j = 0, offset = 6; j < 26; j++, offset += 2) {
		if (j == lit)
			gc = dim_gc;
		XDrawLine(DADisplay, frame, gc, offset, 21, offset, 29);
	}
}

/* Short enough to fit the panel: about nine characters. */
static const char *device_state_label(NMDeviceState state)
{
	switch (state) {
	case NM_DEVICE_STATE_UNMANAGED:	   return "unmanaged";
	case NM_DEVICE_STATE_UNAVAILABLE:  return "no radio";
	case NM_DEVICE_STATE_DISCONNECTED: return "offline";
	case NM_DEVICE_STATE_PREPARE:
	case NM_DEVICE_STATE_CONFIG:
	case NM_DEVICE_STATE_SECONDARIES:  return "connecting";
	case NM_DEVICE_STATE_NEED_AUTH:	   return "password";
	case NM_DEVICE_STATE_IP_CONFIG:
	case NM_DEVICE_STATE_IP_CHECK:	   return "getting IP";
	case NM_DEVICE_STATE_DEACTIVATING: return "stopping";
	case NM_DEVICE_STATE_FAILED:	   return "failed";
	default:			   return NULL;
	}
}

static void render_wifi_body(NMDevice *device)
{
	NMDeviceState state = nm_device_get_state(device);
	NMAccessPoint *active_ap;
	GBytes *ssid;
	char *ssid_str;
	char speed_str[50];
	guint32 speed;

	active_ap = nm_device_wifi_get_active_access_point(
		NM_DEVICE_WIFI(device));
	if (!active_ap) {
		const char *label = device_state_label(state);

		if (label)
			draw_string_clipped(&xft_fg, label, 6, 42, BODY_X,
					    BODY_WIDTH);
		return;
	}

	ssid = nm_access_point_get_ssid(active_ap);
	if (ssid)
		ssid_str = nm_utils_ssid_to_utf8(g_bytes_get_data(ssid, NULL),
						 g_bytes_get_size(ssid));
	else
		ssid_str = g_strdup("--");
	/* Neighbouring networks often share a long prefix, so the tail is the
	   only thing that tells them apart.  Scroll it if it does not fit. */
	draw_scrolling(MARQUEE_SSID, &xft_fg, ssid_str, 6, 56,
		       SSID_LABEL_WIDTH);
	g_free(ssid_str);

	draw_signal(nm_access_point_get_strength(active_ap));

	/* NetworkManager publishes the access point as soon as it starts
	   associating, long before there is a link.  Reporting the bitrate
	   then just prints "0 Mbps", which reads as a connection that is up
	   and idle rather than one still being made. */
	if (state == NM_DEVICE_STATE_ACTIVATED) {
		speed = nm_device_wifi_get_bitrate(NM_DEVICE_WIFI(device));
		speed = (speed + 500) / 1000;
		snprintf(speed_str, sizeof(speed_str), "%d Mbps", speed);
		draw_string(speed_str, 6, 42);
	} else {
		const char *label = device_state_label(state);

		if (label)
			draw_string_clipped(&xft_fg, label, 6, 42, BODY_X,
					    BODY_WIDTH);
	}
}

static void render_generic_body(NMDevice *device)
{
	const char *description, *address;
	char *first, *second;

	description = nm_device_get_type_description(device);
	if (description)
		draw_scrolling(MARQUEE_DESCRIPTION, &xft_fg, description, 6, 30,
			       SSID_LABEL_WIDTH);

	address = nm_device_get_hw_address(device);
	if (address && strlen(address) >= 17) {
		first = g_strndup(address, 9);
		second = g_strndup(address + 9, 8);
		draw_string(first, 6, 42);
		draw_string(second, 6, 54);
		g_free(first);
		g_free(second);
	}
}

/* Draw str clipped to a box, so a long SSID stops at the panel edge instead
   of running over the border. */
static void draw_string_clipped(XftColor *color, const char *str, int x, int y,
				int clip_x, int clip_width)
{
	XRectangle clip;

	clip.x = clip_x;
	clip.y = 0;
	clip.width = clip_width;
	clip.height = DOCKAPP_HEIGHT;

	XftDrawSetClipRectangles(xft_draw, 0, 0, &clip, 1);
	draw_string_in(color, str, x, y);
	XftDrawSetClip(xft_draw, NULL);
}

/* A padlock, three pixels wide: shackle on top, body beneath.  Enterprise
   networks get a broken shackle so it is obvious up front that they will be
   handed off to nm-connection-editor rather than prompting here. */
static void draw_lock(int x, int y, gboolean enterprise, GC gc)
{
	if (enterprise)
		XDrawPoint(DADisplay, frame, gc, x, y);
	else
		XDrawLine(DADisplay, frame, gc, x, y, x + 2, y);
	XDrawPoint(DADisplay, frame, gc, x, y + 1);
	XDrawPoint(DADisplay, frame, gc, x + 2, y + 1);
	XFillRectangle(DADisplay, frame, gc, x, y + 2, 3, 3);
}

/* The thumb spans the whole track: at the top of the list it touches the top
   of the track, and at the bottom it touches the bottom.  Deriving both ends
   from the same expression is what keeps it from stopping short. */
void wmnm_ui_thumb_geometry(guint count, guint top, int *y, int *height)
{
	guint max_top = count > WMNM_AP_ROWS ? count - WMNM_AP_ROWS : 0;
	int thumb;

	thumb = count ? (int)((TRACK_HEIGHT * WMNM_AP_ROWS) / count)
		      : TRACK_HEIGHT;
	if (thumb < THUMB_MIN_HEIGHT)
		thumb = THUMB_MIN_HEIGHT;
	if (thumb > TRACK_HEIGHT)
		thumb = TRACK_HEIGHT;

	*height = thumb;
	*y = TRACK_Y;
	if (max_top > 0)
		*y += (int)(((TRACK_HEIGHT - thumb) * top) / max_top);
}

/* A 3px triangle, pointing up or down, so the scroll buttons read as buttons
   rather than as two stray pixels. */
static void draw_arrow(int x, int y, gboolean up, GC gc)
{
	if (up) {
		XDrawPoint(DADisplay, frame, gc, x + 1, y);
		XDrawLine(DADisplay, frame, gc, x, y + 1, x + 2, y + 1);
	} else {
		XDrawLine(DADisplay, frame, gc, x, y, x + 2, y);
		XDrawPoint(DADisplay, frame, gc, x + 1, y + 1);
	}
}

static int text_width(const char *str)
{
	XGlyphInfo extents;

	XftTextExtents8(DADisplay, xft_font, (const FcChar8 *)str,
			strlen(str), &extents);

	return extents.xOff;
}

static gboolean marquee_tick(gpointer user_data)
{
	Marquee *marquee = user_data;

	if (marquee->pause > 0) {
		marquee->pause--;
		return G_SOURCE_CONTINUE;
	}

	marquee->offset += marquee->step;
	if (marquee->offset >= marquee->limit) {
		marquee->offset = marquee->limit;
		marquee->step = -1;
		marquee->pause = MARQUEE_PAUSE_TICKS;
	} else if (marquee->offset <= 0) {
		marquee->offset = 0;
		marquee->step = 1;
		marquee->pause = MARQUEE_PAUSE_TICKS;
	}

	wmnm_queue_render();

	return G_SOURCE_CONTINUE;
}

static void marquee_reset(Marquee *marquee)
{
	if (marquee->timer) {
		g_source_remove(marquee->timer);
		marquee->timer = 0;
	}

	g_clear_pointer(&marquee->label, g_free);
	marquee->offset = 0;
	marquee->limit = 0;
	marquee->step = 1;
	marquee->pause = 0;
}

/* How far left to shift a label that does not fit, starting the animation on
   demand and stopping it when there is nothing to scroll. */
static int marquee_offset_for(Marquee *marquee, const char *label,
			      int available)
{
	int overflow;

	/* Only scroll while someone is looking.  Parked at offset zero the
	   label reads from its start, which is the sensible resting state. */
	if (!hovering) {
		marquee_reset(marquee);
		return 0;
	}

	overflow = text_width(label) - available;
	if (overflow <= 0) {
		marquee_reset(marquee);
		return 0;
	}

	/* Compare the text rather than the overflow: two different networks
	   can be the same number of pixels too wide. */
	if (g_strcmp0(marquee->label, label) != 0) {
		marquee_reset(marquee);
		marquee->label = g_strdup(label);
		marquee->pause = MARQUEE_PAUSE_TICKS;
	}

	marquee->limit = overflow;

	if (!marquee->timer)
		marquee->timer = g_timeout_add(MARQUEE_INTERVAL_MSEC,
					       marquee_tick, marquee);

	return marquee->offset;
}

void wmnm_ui_stop_animations(void)
{
	int i;

	for (i = 0; i < MARQUEE_COUNT; i++)
		marquee_reset(&marquees[i]);
}

void wmnm_ui_set_hover(gboolean is_hovering)
{
	if (is_hovering == hovering)
		return;

	hovering = is_hovering;

	if (!hovering)
		wmnm_ui_stop_animations();

	wmnm_queue_render();
}

/* Draw str in a field of the given width, scrolling it if it does not fit. */
static void draw_scrolling(int which, XftColor *color, const char *str, int x,
			   int y, int width)
{
	int offset = marquee_offset_for(&marquees[which], str, width);

	draw_string_clipped(color, str, x - offset, y, x, width);
}

/* The buttons are always drawn, dimmed when they would do nothing, so that
   they stay findable.  Two lit pixels that vanish at the ends of the list are
   not a discoverable control. */
static void render_scrollbar(guint count, guint top)
{
	gboolean can_up = top > 0;
	gboolean can_down = count > top + WMNM_AP_ROWS;
	int thumb_y, thumb_h;

	XFillRectangle(DADisplay, frame, dim_gc, GUTTER_X + 1, TRACK_Y, 3,
		       TRACK_HEIGHT);

	draw_arrow(GUTTER_X + 1, BODY_Y + 3, TRUE, can_up ? fg_gc : dim_gc);
	draw_arrow(GUTTER_X + 1, BODY_Y + BODY_HEIGHT - 5, FALSE,
		   can_down ? fg_gc : dim_gc);

	if (count > WMNM_AP_ROWS) {
		wmnm_ui_thumb_geometry(count, top, &thumb_y, &thumb_h);
		XFillRectangle(DADisplay, frame, fg_gc, GUTTER_X + 1, thumb_y,
			       3, thumb_h);
	}
}

static void render_ap_list(Device *d)
{
	const GPtrArray *entries = wmnm_wifi_entries(d);
	guint top = wmnm_wifi_scroll_top(d);
	guint cursor = wmnm_wifi_cursor(d);
	guint row;

	if (!entries || entries->len == 0) {
		marquee_reset(&marquees[MARQUEE_AP]);
		draw_string("scanning", 8, 42);
		return;
	}

	for (row = 0; row < WMNM_AP_ROWS; row++) {
		guint index = top + row;
		int y = BODY_Y + row * AP_ROW_HEIGHT;
		int baseline = y + 8;
		gboolean selected = (index == cursor);
		XftColor *color = selected ? &xft_bg : &xft_fg;
		GC gc = selected ? bg_gc : fg_gc;
		ApEntry *entry;
		int bar;

		if (index >= entries->len)
			break;

		entry = g_ptr_array_index(entries, index);

		/* The selected row is a filled bar with the text knocked out
		   of it. */
		if (selected)
			XFillRectangle(DADisplay, frame, fg_gc, BODY_X, y,
				       GUTTER_X - BODY_X, AP_ROW_HEIGHT);

		if (wmnm_ap_is_secure(entry))
			draw_lock(BODY_X + 1, y + 2,
				  wmnm_ap_is_enterprise(entry), gc);

		/* Only the selected row scrolls; four moving rows would be unreadable. */
		if (selected)
			draw_scrolling(MARQUEE_AP, color, entry->label,
				       BODY_X + 5, baseline, AP_LABEL_WIDTH);
		else
			draw_string_clipped(color, entry->label, BODY_X + 5,
					    baseline, BODY_X + 5,
					    AP_LABEL_WIDTH);

		/* Strength as a short vertical tick rather than a bar graph;
		   there is no room for anything wider. */
		bar = (entry->strength * 7) / 100;
		if (bar > 0)
			XFillRectangle(DADisplay, frame, gc, 51,
				       y + 8 - bar, 3, bar);
	}

	render_scrollbar(entries->len, top);
}

static void render_status(void)
{
	const char *line1 = wmnm_status_line1();
	const char *line2 = wmnm_status_line2();

	/* line2 is often a NetworkManager error string, which can be a whole
	   sentence. */
	if (line1)
		draw_scrolling(MARQUEE_STATUS1, &xft_fg, line1, BODY_X + 1, 34,
			       SSID_LABEL_WIDTH);
	if (line2)
		draw_scrolling(MARQUEE_STATUS2, &xft_fg, line2, BODY_X + 1, 46,
			       SSID_LABEL_WIDTH);
}

static void render_iface_strip(Device *d)
{
	const char *iface;

	iface = nm_device_get_iface(d->device);
	if (iface)
		draw_scrolling(MARQUEE_IFACE, &xft_fg, iface, 6, 13,
			       STRIP_LABEL_WIDTH);

	XCopyArea(DADisplay,
		  nm_device_get_state(d->device) == NM_DEVICE_STATE_ACTIVATED
		  ? led_on : led_off,
		  frame, DAGC, 0, 0, 4, 4, 53, 8);

	/* A portal means the link is up but traffic is being intercepted, so
	   the activation LED alone would be misleading. */
	if (wmnm_portal_active())
		draw_string_clipped(&xft_fg, "!", 47, 13, 47, 5);
}

static void render_device_view(Device *d)
{
	render_iface_strip(d);

	if (NM_IS_DEVICE_WIFI(d->device))
		render_wifi_body(d->device);
	else
		render_generic_body(d->device);
}

void wmnm_render(void)
{
	if (render_idle_id) {
		g_source_remove(render_idle_id);
		render_idle_id = 0;
	}

	if (!current_device)
		return;

	XCopyArea(DADisplay, master, frame, DAGC, 0, 0,
		  DOCKAPP_WIDTH, DOCKAPP_HEIGHT, 0, 0);
	clear_rectangle(BODY_X, BODY_Y, BODY_WIDTH, BODY_HEIGHT);
	clear_rectangle(5, 5, 54, 11);

	switch (current_view) {
	case VIEW_DEVICE:
		render_device_view(current_device);
		break;
	case VIEW_APLIST:
		render_iface_strip(current_device);
		render_ap_list(current_device);
		break;
	case VIEW_STATUS:
		render_iface_strip(current_device);
		render_status();
		break;
	}

	/* Drawing into the pixmap is not enough on its own: DASetPixmap() is
	   what installs it as the window background and clears the window. */
	DASetPixmap(frame);
}

static gboolean render_idle(gpointer user_data)
{
	(void)user_data;

	render_idle_id = 0;
	wmnm_render();

	return G_SOURCE_REMOVE;
}

void wmnm_queue_render(void)
{
	if (!render_idle_id)
		render_idle_id = g_idle_add(render_idle, NULL);
}
