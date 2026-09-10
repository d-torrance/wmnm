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
static XftColor xft_fg;

static guint render_idle_id;

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
	xft_draw = XftDrawCreate(DADisplay, frame, DAVisual, cmap);
}

static void clear_rectangle(int x, int y, unsigned int width,
			    unsigned int height)
{
	XFillRectangle(DADisplay, frame, bg_gc, x, y, width, height);
}

static void draw_string(const char *str, int x, int y)
{
	XftDrawString8(xft_draw, &xft_fg, xft_font, x, y,
		       (const FcChar8 *)str, strlen(str));
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

static void render_wifi_body(NMDevice *device)
{
	NMAccessPoint *active_ap;
	GBytes *ssid;
	char *ssid_str;
	char speed_str[50];
	guint32 speed;

	active_ap = nm_device_wifi_get_active_access_point(
		NM_DEVICE_WIFI(device));
	if (!active_ap)
		return;

	ssid = nm_access_point_get_ssid(active_ap);
	if (ssid)
		ssid_str = nm_utils_ssid_to_utf8(g_bytes_get_data(ssid, NULL),
						 g_bytes_get_size(ssid));
	else
		ssid_str = g_strdup("--");
	draw_string(ssid_str, 6, 56);
	g_free(ssid_str);

	draw_signal(nm_access_point_get_strength(active_ap));

	speed = nm_device_wifi_get_bitrate(NM_DEVICE_WIFI(device));
	speed = (speed + 500) / 1000;
	snprintf(speed_str, sizeof(speed_str), "%d Mbps", speed);
	draw_string(speed_str, 6, 42);
}

static void render_generic_body(NMDevice *device)
{
	const char *description, *address;
	char *first, *second;

	description = nm_device_get_type_description(device);
	if (description)
		draw_string(description, 6, 30);

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

static void render_device_view(Device *d)
{
	const char *iface;

	iface = nm_device_get_iface(d->device);
	if (iface)
		draw_string(iface, 6, 13);

	XCopyArea(DADisplay,
		  nm_device_get_state(d->device) == NM_DEVICE_STATE_ACTIVATED
		  ? led_on : led_off,
		  frame, DAGC, 0, 0, 4, 4, 53, 8);

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
