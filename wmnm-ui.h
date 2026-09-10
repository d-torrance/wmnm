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

#ifndef WMNM_UI_H
#define WMNM_UI_H

#include <glib.h>

/* Build the pristine background and the pixmap we render frames into. */
void wmnm_ui_init(void);

/* Ask for a redraw.  Many NetworkManager properties can change at once, so
   this coalesces to a single render on the next main loop idle. */
void wmnm_queue_render(void);

/* Render immediately.  wmnm_queue_render() is nearly always what you want. */
void wmnm_render(void);

/* Stop the selected-row marquee, e.g. when leaving the access point view. */
void wmnm_ui_stop_animations(void);

#endif /* WMNM_UI_H */
