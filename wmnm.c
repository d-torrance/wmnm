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
#include "wmnm_mask.xbm"

static void switch_devices(int x, int y, DARect rect, void *data);

/* globals */
DAActionRect action_rects[] = {
	{{5, 5, 54, 11}, switch_devices}
};
Device *current_device;
View current_view = VIEW_DEVICE;

static void switch_devices(int x, int y, DARect rect, void *data)
{
	(void)x;
	(void)y;
	(void)rect;
	(void)data;

	current_device = current_device->next;
	wmnm_queue_render();
}

static void button_press(int button, int state, int x, int y)
{
	(void)button;
	(void)state;

	DAProcessActionRects(x, y, action_rects, G_N_ELEMENTS(action_rects),
			     NULL);
}

/* libdockapp calls this when our window is destroyed.  It gives us a hook to
   shut down cleanly; without a non-NULL destroy callback libdockapp does not
   even select StructureNotifyMask. */
static void destroy(void)
{
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
				      NULL, NULL, NULL, NULL,
				      NULL};
	NMClient *client;
	GError *error = NULL;
	const GPtrArray *devices;
	Pixmap mask;

	DAParseArguments(argc, argv, NULL, 0,
			 "NetworkManager frontend as a Window Maker dockapp",
			 PACKAGE_STRING);
	DAInitialize(NULL, PACKAGE_NAME, DOCKAPP_WIDTH, DOCKAPP_HEIGHT,
		     argc, argv);
	DASetCallbacks(&eventCallbacks);

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

	g_object_unref(client);

	return EXIT_SUCCESS;
}
