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

#ifndef WMNM_H
#define WMNM_H

#include <NetworkManager.h>
#include <glib.h>

#define DOCKAPP_WIDTH 64
#define DOCKAPP_HEIGHT 64

#define DEFAULT_FGCOLOR "light sea green"
#define DEFAULT_DIMCOLOR "#0c4744"
#define DEFAULT_BGCOLOR "#181818"

/* The inset panel below the interface name: everything a view may draw in. */
#define BODY_X 5
#define BODY_Y 20
#define BODY_WIDTH 54
#define BODY_HEIGHT 39

/* The access point list: four 10px rows, with the rightmost 5px of the body
   reserved as a scroll gutter. */
#define WMNM_AP_ROWS 4
#define AP_ROW_HEIGHT 10
#define GUTTER_X 55
#define GUTTER_WIDTH 5
#define GUTTER_ZONE_HEIGHT 8
#define AP_LABEL_WIDTH 41

struct WifiView;

typedef struct Device {
	NMDevice *device;
	struct WifiView *wifi;		/* access point list, wifi only */
	struct Device *previous;
	struct Device *next;
} Device;

typedef enum {
	VIEW_DEVICE,			/* interface name and link statistics */
	VIEW_APLIST,			/* scrollable list of nearby networks */
	VIEW_STATUS			/* transient result of a connection */
} View;

extern Device *current_device;
extern View current_view;

#endif /* WMNM_H */
