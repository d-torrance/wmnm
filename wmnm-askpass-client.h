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

#ifndef WMNM_ASKPASS_CLIENT_H
#define WMNM_ASKPASS_CLIENT_H

#include <glib.h>

typedef struct WmnmAskpass WmnmAskpass;

/* secret is NULL if the user cancelled or the helper could not be run. */
typedef void (*WmnmAskpassFunc)(const char *secret, gpointer user_data);

/* Ask the user for a secret.  Returns NULL if no helper could be spawned, in
   which case the callback is never invoked. */
WmnmAskpass *wmnm_askpass_run(const char *prompt, WmnmAskpassFunc callback,
			      gpointer user_data);

/* Abandon a running prompt.  The callback will not be invoked afterwards. */
void wmnm_askpass_cancel(WmnmAskpass *askpass);

#endif /* WMNM_ASKPASS_CLIENT_H */
