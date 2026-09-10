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

#ifndef WMNM_LOOP_H
#define WMNM_LOOP_H

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <glib.h>

/* Attach the X connection to the default GMainContext so that X events and
   GLib/D-Bus sources are serviced by a single poll(), then run that loop.
   libdockapp's own DAEventLoop() is never entered. */
/* libdockapp's callback table has no entry for key presses, so KeyPress
   events are picked out of the event stream here and handed to this. */
typedef void (*WmnmKeyFunc)(KeySym keysym, unsigned int state);
void wmnm_loop_set_key_handler(WmnmKeyFunc handler);

void wmnm_loop_attach_x_source(Display *dpy);
void wmnm_loop_run(void);
void wmnm_loop_quit(void);

#endif /* WMNM_LOOP_H */
