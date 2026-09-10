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

#include "wmnm-loop.h"

typedef struct {
	GSource source;
	GPollFD pfd;
	Display *dpy;
} XSource;

static GMainLoop *main_loop;
static WmnmKeyFunc key_handler;

/* Xlib buffers events in a userspace queue, so there can be events waiting
   for us while the socket itself has nothing left to read.  Reporting that
   here is what keeps us from sleeping in a poll() that would never wake. */
static gboolean x_prepare(GSource *source, gint *timeout)
{
	XSource *xs = (XSource *)source;

	*timeout = -1;
	XFlush(xs->dpy);		/* push our drawing before we sleep */

	return XPending(xs->dpy) > 0;
}

static gboolean x_check(GSource *source)
{
	XSource *xs = (XSource *)source;

	if (xs->pfd.revents & (G_IO_IN | G_IO_HUP | G_IO_ERR))
		return TRUE;

	return XPending(xs->dpy) > 0;
}

static gboolean x_dispatch(GSource *source, GSourceFunc callback,
			   gpointer user_data)
{
	XSource *xs = (XSource *)source;
	XEvent event;

	(void)callback;
	(void)user_data;

	while (XPending(xs->dpy)) {
		XNextEvent(xs->dpy, &event);

		if (event.type == KeyPress && key_handler) {
			key_handler(XLookupKeysym(&event.xkey, 0),
				    event.xkey.state);
			continue;
		}

		DAProcessEvent(&event);
	}

	return G_SOURCE_CONTINUE;
}

static GSourceFuncs x_source_funcs = {
	x_prepare, x_check, x_dispatch, NULL, NULL, NULL
};

/* If the X server goes away, Xlib's default handler exits without unwinding.
   Quit the loop instead so that main() can deregister our secret agent. */
static int x_io_error(Display *dpy)
{
	(void)dpy;

	wmnm_loop_quit();

	return 0;
}

void wmnm_loop_set_key_handler(WmnmKeyFunc handler)
{
	key_handler = handler;
}

void wmnm_loop_attach_x_source(Display *dpy)
{
	XSource *xs;

	xs = (XSource *)g_source_new(&x_source_funcs, sizeof(XSource));
	xs->dpy = dpy;
	xs->pfd.fd = ConnectionNumber(dpy);
	xs->pfd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;

	g_source_add_poll(&xs->source, &xs->pfd);
	g_source_set_can_recurse(&xs->source, FALSE);
	g_source_attach(&xs->source, NULL);
	g_source_unref(&xs->source);

	XSetIOErrorHandler(x_io_error);
}

void wmnm_loop_run(void)
{
	main_loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(main_loop);
}

void wmnm_loop_quit(void)
{
	if (main_loop)
		g_main_loop_quit(main_loop);
}
