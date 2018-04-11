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
#include <glib.h>
#include <NetworkManager.h>

#define DOCKAPP_WIDTH 64
#define DOCKAPP_HEIGHT 64

#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <NetworkManager.h>
#include <X11/Xft/Xft.h>

#include "wmnm_mask.xbm"
#include "wmnm_master.xpm"

#define DEFAULT_FGCOLOR "light sea green"
#define DEFAULT_BGCOLOR "#181818"

void switch_devices(int x, int y, DARect rect, void *data);


/* globals */
Pixmap pixmap;
const GPtrArray *devices;
DAActionRect action_rects[] = {
	{{5, 5, 54, 11}, switch_devices}
};

void clear_rectangle(int x, int y, unsigned int width, unsigned int height)
{
	XGCValues values;
	static GC gc = 0;

	if (!gc) {
		values.foreground = DAGetColor(DEFAULT_BGCOLOR);
		gc = XCreateGC(DADisplay, pixmap, GCForeground, &values);
	}
	XFillRectangle(DADisplay, pixmap, gc, x, y, width, height);
}

void draw_signal(const guint8 strength)
{
	int i, loop, offset;
	GC gc;
	static GC light_gc = 0, dark_gc = 0;
	XGCValues values;

	if (!light_gc) {
		values.foreground = DAGetColor(DEFAULT_FGCOLOR);
		light_gc = XCreateGC(DADisplay, pixmap, GCForeground, &values);
	}
	if (!dark_gc) {
		values.foreground = DAGetColor("#0c4744");
		dark_gc = XCreateGC(DADisplay, pixmap, GCForeground, &values);
	}

	gc = light_gc;
	loop = floor(0.26 * strength + 0.5); /* 100 strength = all 26 bars */

	for (i = 0, offset = 6; i < 26; i++, offset += 2) {
		if (i == loop)
			gc = dark_gc;
		XDrawLine(DADisplay, pixmap, gc, offset, 21, offset, 29);
	}
}

void draw_string(const char *str, int x, int y)
{
	Colormap cmap;
	static XftColor color = {0, 0};
	static XftDraw *draw = NULL;
	static XftFont *font = NULL;

	if (!font) {
		cmap = DefaultColormap(DADisplay, DefaultScreen(DADisplay));
		draw = XftDrawCreate(DADisplay, pixmap, DAVisual, cmap);
		XftColorAllocName(DADisplay, DAVisual, cmap, DEFAULT_FGCOLOR,
				  &color);
		font = XftFontOpenName(DADisplay, DefaultScreen(DADisplay),
				       "mono:pixelsize=9");
	}

	XftDrawString8(draw, &color, font, x, y, str, strlen(str));
}

void update_window_wifi(NMDevice *device)
{
	NMAccessPoint *active_ap = NULL;
	guint8 strength;
	guint32 speed;
	GBytes *active_ssid;
	char *active_ssid_str = NULL;
 	char speed_str[50];

	if ((active_ap =
	     nm_device_wifi_get_active_access_point(NM_DEVICE_WIFI(device)))) {
		active_ssid = nm_access_point_get_ssid(active_ap);
		if (active_ssid)
			active_ssid_str = nm_utils_ssid_to_utf8(
				g_bytes_get_data (active_ssid, NULL),
				g_bytes_get_size (active_ssid));
		else
			active_ssid_str = g_strdup ("--");

		strength = nm_access_point_get_strength(active_ap);
		draw_signal(strength);

		speed = nm_device_wifi_get_bitrate (NM_DEVICE_WIFI (device));
		speed /= 1000;
		snprintf(speed_str, sizeof(speed_str), "%d Mbps", speed);
		draw_string(speed_str, 6, 42);

		draw_string(active_ssid_str, 6, 56);
	}

}

void update_window(NMDevice *device)
{
	const char *iface;
	XGCValues values;
	GC gc;
	static Pixmap led_on = 0, led_off = 0;
	Pixmap led;
	short unsigned int w, h;

	static char * led_on_xpm[] = {
		"4 4 4 1",
		" 	c None",
		".	c #181818",
		"+	c #00E500",
		"@	c #F7F7FF",
		".++.",
		"+@++",
		"++++",
		".++."};

	static char * led_off_xpm[] = {
		"4 4 4 1",
		" 	c None",
		".	c #181818",
		"+	c #003900",
		"@	c #48484A",
		".++.",
		"+@++",
		"++++",
		".++."};

	/* only make the led pixmaps the first time we update window */
	if (!led_on)
		DAMakePixmapFromData(led_on_xpm, &led_on, NULL, &w, &h);
	if (!led_off)
		DAMakePixmapFromData(led_off_xpm, &led_off, NULL, &w, &h);

	/* print device interface name */
	iface = nm_device_get_iface(device);
	clear_rectangle(5, 5, 54, 11);
	draw_string(iface, 6, 13);

	/* draw led telling us whether device is activated */
	if (nm_device_get_state (device) == NM_DEVICE_STATE_ACTIVATED)
		led = led_on;
	else
		led = led_off;
	XCopyArea(DADisplay, led, pixmap, DAGC, 0, 0, 4, 4, 53, 8);

	clear_rectangle(5, 20, 54, 39);

	if (NM_IS_DEVICE_WIFI(device))
		update_window_wifi(device);

	DASetPixmap(pixmap);
}

void switch_devices(int x, int y, DARect rect, void *data)
{
	static int current_device = 0;
	NMDevice *device;

	current_device++;
	if (current_device >= devices->len)
		current_device = 0;

	device = g_ptr_array_index(devices, current_device);
	update_window(device);
}

void button_press(int button, int state, int x, int y)
{
	int *data = malloc(sizeof(int *));

	*data = button;

	DAProcessActionRects(x, y, action_rects, 1, (void *)data);

	free(data);
}

int main (int argc, char *argv[])
{
	XGCValues values;
	DACallbacks eventCallbacks = {NULL, button_press,
				      NULL, NULL, NULL, NULL,
				      NULL};

	NMClient *client;
	NMDevice *device;
	GError *error = NULL;
	int i;
	short unsigned int w, h;
	Pixmap mask;

	DAParseArguments(argc, argv, NULL, 0,
			 "NetworkManager frontend as a Window Maker dockapp",
			 PACKAGE_STRING);
	DAInitialize(NULL, PACKAGE_NAME, DOCKAPP_WIDTH, DOCKAPP_HEIGHT,
		     argc, argv);
	DASetCallbacks(&eventCallbacks);

	/* replace with DAMakeShapeFromData when next version of libdockapp
	   released */
	mask = XCreateBitmapFromData(DADisplay, DAWindow, wmnm_mask_bits,
				     wmnm_mask_width, wmnm_mask_height);
	DAMakePixmapFromData(wmnm_master_xpm, &pixmap, NULL, &w, &h);
	DASetPixmap(pixmap);
	DASetShape(mask);

	client = nm_client_new(NULL, &error);
	if (!client) {
		g_message("Error: Could not create NMClient: %s.", error->message);
		g_error_free(error);
		return EXIT_FAILURE;
	}

	devices = nm_client_get_devices(client);
	device = g_ptr_array_index(devices, 0);
	update_window(device);

	DASetTimeout(1000);
	DAShow();
	DAEventLoop();

	return EXIT_SUCCESS;
}
