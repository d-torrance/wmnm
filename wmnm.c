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
#include <stdlib.h>

#include "wmnm.h"
#include "wmnm-loop.h"
#include "wmnm-ui.h"
#include "wmnm-wifi.h"
#include "wmnm-connect.h"
#include "wmnm-agent.h"
#include "wmnm_mask.xbm"

static void drag_start(int x, int y, DARect rect, void *data);
static void show_ap_list(int x, int y, DARect rect, void *data);
static void grab_keyboard(gboolean grab);

Device *current_device;
View current_view = VIEW_DEVICE;

static gboolean pointer_inside;

static void set_view(View view)
{
	if (view == current_view)
		return;

	if (current_view == VIEW_APLIST) {
		wmnm_wifi_leave(current_device);
		wmnm_ui_stop_animations();
		grab_keyboard(FALSE);
	}

	current_view = view;

	if (view == VIEW_APLIST) {
		wmnm_wifi_enter(current_device);
		if (pointer_inside)
			grab_keyboard(TRUE);
	}

	wmnm_queue_render();
}

/* The access point list only means anything for a wifi device. */
static void show_ap_list(int x, int y, DARect rect, void *data)
{
	(void)x;
	(void)y;
	(void)rect;
	(void)data;

	if (current_device && current_device->wifi)
		set_view(VIEW_APLIST);
}

static void switch_devices(int x, int y, DARect rect, void *data)
{
	(void)x;
	(void)y;
	(void)rect;
	(void)data;

	set_view(VIEW_DEVICE);
	current_device = current_device->next;
	wmnm_queue_render();
}

static void scroll_up(int x, int y, DARect rect, void *data)
{
	(void)x;
	(void)y;
	(void)rect;
	(void)data;

	wmnm_wifi_scroll(current_device, -1);
}

static void scroll_down(int x, int y, DARect rect, void *data)
{
	(void)x;
	(void)y;
	(void)rect;
	(void)data;

	wmnm_wifi_scroll(current_device, 1);
}

/* First click moves the selection, a second click on the same row connects.
   There is no room for a separate connect button. */
static void select_row(int x, int y, DARect rect, void *data)
{
	guint row = (y - BODY_Y) / AP_ROW_HEIGHT;
	guint before;

	(void)x;
	(void)rect;
	(void)data;

	before = wmnm_wifi_cursor(current_device);
	wmnm_wifi_select_row(current_device, row);

	if (wmnm_wifi_cursor(current_device) == before)
		wmnm_connect_to(current_device,
				wmnm_wifi_selected(current_device));
}

static gboolean dragging;
static gboolean keyboard_grabbed;

/* A dockapp is not a normal window and does not get the keyboard focus, so
   arrow keys would never reach us.  Grab the keyboard while the pointer is
   over the icon and the network list is open -- a mode the user entered
   deliberately -- and release it the moment the pointer leaves. */
static void grab_keyboard(gboolean grab)
{
	if (grab == keyboard_grabbed)
		return;

	if (grab) {
		if (XGrabKeyboard(DADisplay, DAWindow, True, GrabModeAsync,
				  GrabModeAsync, CurrentTime) != GrabSuccess)
			return;
	} else {
		XUngrabKeyboard(DADisplay, CurrentTime);
	}

	keyboard_grabbed = grab;
}

static void pointer_entered(void)
{
	pointer_inside = TRUE;

	if (current_view == VIEW_APLIST)
		grab_keyboard(TRUE);
}

static void pointer_left(void)
{
	pointer_inside = FALSE;
	grab_keyboard(FALSE);
}

/* Map a pointer position in the track to a scroll offset, putting the middle
   of the thumb under the pointer. */
static void drag_to(int y)
{
	const GPtrArray *entries = wmnm_wifi_entries(current_device);
	guint max_top, top;
	int thumb_y, thumb_h, span, offset;

	if (!entries || entries->len <= WMNM_AP_ROWS)
		return;

	max_top = entries->len - WMNM_AP_ROWS;
	wmnm_ui_thumb_geometry(entries->len, 0, &thumb_y, &thumb_h);

	span = TRACK_HEIGHT - thumb_h;
	if (span <= 0)
		return;

	offset = y - TRACK_Y - thumb_h / 2;
	if (offset < 0)
		offset = 0;
	if (offset > span)
		offset = span;

	top = (guint)((offset * (int)max_top + span / 2) / span);
	wmnm_wifi_set_scroll_top(current_device, top);
}

static void drag_start(int x, int y, DARect rect, void *data)
{
	(void)x;
	(void)rect;
	(void)data;

	dragging = TRUE;
	drag_to(y);
}

static void motion(int x, int y)
{
	(void)x;

	if (dragging)
		drag_to(y);
}

static void key_press(KeySym keysym, unsigned int state)
{
	(void)state;

	if (current_view != VIEW_APLIST) {
		if (keysym == XK_Up || keysym == XK_Down)
			show_ap_list(0, 0, DANoRect, NULL);
		else
			return;
	}

	switch (keysym) {
	case XK_Up:
	case XK_KP_Up:
		wmnm_wifi_scroll(current_device, -1);
		break;
	case XK_Down:
	case XK_KP_Down:
		wmnm_wifi_scroll(current_device, 1);
		break;
	case XK_Page_Up:
	case XK_KP_Page_Up:
		wmnm_wifi_scroll(current_device, -WMNM_AP_ROWS);
		break;
	case XK_Page_Down:
	case XK_KP_Page_Down:
		wmnm_wifi_scroll(current_device, WMNM_AP_ROWS);
		break;
	case XK_Home:
	case XK_KP_Home:
		wmnm_wifi_scroll(current_device, -G_MAXINT);
		break;
	case XK_End:
	case XK_KP_End:
		wmnm_wifi_scroll(current_device, G_MAXINT);
		break;
	case XK_Return:
	case XK_KP_Enter:
	case XK_space:
		wmnm_connect_to(current_device,
				wmnm_wifi_selected(current_device));
		break;
	case XK_Escape:
		set_view(VIEW_DEVICE);
		break;
	default:
		break;
	}
}

static void button_release(int button, int state, int x, int y)
{
	(void)state;
	(void)x;
	(void)y;

	/* Each wheel notch also sends a release; ignore those. */
	if (button == Button1)
		dragging = FALSE;
}

/* globals */
static DAActionRect device_rects[] = {
	{{5, 5, 54, 11}, switch_devices},
	{{BODY_X, BODY_Y, BODY_WIDTH, BODY_HEIGHT}, show_ap_list}
};

static DAActionRect aplist_rects[] = {
	{{5, 5, 54, 11}, switch_devices},
	{{GUTTER_X, BODY_Y, GUTTER_WIDTH, GUTTER_BUTTON_HEIGHT}, scroll_up},
	{{GUTTER_X, BODY_Y + BODY_HEIGHT - GUTTER_BUTTON_HEIGHT, GUTTER_WIDTH,
	  GUTTER_BUTTON_HEIGHT}, scroll_down},
	{{GUTTER_X, TRACK_Y, GUTTER_WIDTH, TRACK_HEIGHT}, drag_start},
	{{BODY_X, BODY_Y, GUTTER_X - BODY_X, BODY_HEIGHT}, select_row}
};

static void button_press(int button, int state, int x, int y)
{
	(void)state;

	/* The X server synthesises buttons 4 and 5 from the scroll axes of a
	   libinput touchpad, so two-finger scrolling arrives here too.  Only
	   the press is handled: each notch also generates a release. */
	switch (button) {
	case Button4:
		show_ap_list(x, y, DANoRect, NULL);
		wmnm_wifi_scroll(current_device, -1);
		return;
	case Button5:
		show_ap_list(x, y, DANoRect, NULL);
		wmnm_wifi_scroll(current_device, 1);
		return;
	case Button2:
		if (wmnm_portal_active())
			wmnm_portal_open();
		return;
	case Button3:
		set_view(VIEW_DEVICE);
		return;
	case Button1:
		break;
	default:
		return;		/* ignore the horizontal wheel, 6 and 7 */
	}

	if (current_view == VIEW_APLIST)
		DAProcessActionRects(x, y, aplist_rects,
				     G_N_ELEMENTS(aplist_rects), NULL);
	else
		DAProcessActionRects(x, y, device_rects,
				     G_N_ELEMENTS(device_rects), NULL);
}

/* libdockapp calls this when our window is destroyed.  It gives us a hook to
   shut down cleanly; without a non-NULL destroy callback libdockapp does not
   even select StructureNotifyMask. */
static void destroy(void)
{
	grab_keyboard(FALSE);
	wmnm_loop_quit();
}

static void device_changed(GObject *object, GParamSpec *pspec,
			   gpointer user_data)
{
	(void)object;
	(void)pspec;
	(void)user_data;

	wmnm_queue_render();
}

/* Link the devices into a ring so that switch_devices() can walk it, and
   start on whichever device is already activated. */
static Device *build_device_ring(const GPtrArray *devices)
{
	Device *first = NULL, *previous = NULL, *d;
	guint i;

	for (i = 0; i < devices->len; i++) {
		d = g_new0(Device, 1);
		d->device = g_ptr_array_index(devices, i);

		if (!first)
			first = d;
		else {
			d->previous = previous;
			previous->next = d;
		}
		previous = d;

		if (nm_device_get_state(d->device) == NM_DEVICE_STATE_ACTIVATED
		    && !current_device)
			current_device = d;

		wmnm_wifi_attach(d);
		wmnm_connect_watch_device(d->device);

		if (NM_IS_DEVICE_WIFI(d->device))
			g_signal_connect(d->device,
					 "notify::" NM_DEVICE_WIFI_BITRATE,
					 G_CALLBACK(device_changed), d);
	}

	previous->next = first;
	first->previous = previous;

	if (!current_device)
		current_device = first;

	return first;
}

int main(int argc, char *argv[])
{
	DACallbacks eventCallbacks = {destroy, button_press,
				      button_release, motion, pointer_entered,
				      pointer_left, NULL};
	NMClient *client;
	GError *error = NULL;
	const GPtrArray *devices;
	XWindowAttributes attributes;
	Pixmap mask;

	DAParseArguments(argc, argv, NULL, 0,
			 "NetworkManager frontend as a Window Maker dockapp",
			 PACKAGE_STRING);
	DAInitialize(NULL, PACKAGE_NAME, DOCKAPP_WIDTH, DOCKAPP_HEIGHT,
		     argc, argv);
	DASetCallbacks(&eventCallbacks);

	/* libdockapp derives its event mask from the callback table, which has
	   no entry for key presses, so add that to whatever it selected. */
	XGetWindowAttributes(DADisplay, DAWindow, &attributes);
	XSelectInput(DADisplay, DAWindow,
		     attributes.your_event_mask | KeyPressMask);
	wmnm_loop_set_key_handler(key_press);

	client = nm_client_new(NULL, &error);
	if (!client) {
		g_message("Error: Could not create NMClient: %s.",
			  error->message);
		g_error_free(error);
		return EXIT_FAILURE;
	}

	devices = nm_client_get_devices(client);
	if (!devices || devices->len == 0) {
		g_message("Error: NetworkManager reports no devices.");
		g_object_unref(client);
		return EXIT_FAILURE;
	}

	wmnm_ui_init();
	wmnm_connect_init(client);
	wmnm_agent_start();
	build_device_ring(devices);

	mask = XCreateBitmapFromData(DADisplay, DAWindow,
				     (const char *)wmnm_mask_bits,
				     wmnm_mask_width, wmnm_mask_height);
	DASetShape(mask);

	wmnm_render();
	DAShow();

	/* DASetCallbacks() above is what calls XSelectInput(), so it is still
	   required even though we no longer use libdockapp's event loop. */
	wmnm_loop_attach_x_source(DADisplay);
	wmnm_loop_run();

	wmnm_agent_stop();
	g_object_unref(client);

	return EXIT_SUCCESS;
}
