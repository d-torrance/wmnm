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

#ifndef WMNM_WIFI_H
#define WMNM_WIFI_H

#include "wmnm.h"

/* One visible network.  A network can be reachable through several BSSIDs;
   they are merged into a single entry keyed on the raw SSID. */
typedef struct {
	GBytes *ssid;
	char *label;			/* SSID as UTF-8, for display */
	NMAccessPoint *best;		/* strongest BSSID seen */
	guint8 strength;
	guint32 flags, wpa, rsn;
	gboolean active;		/* the AP we are associated with */
} ApEntry;

/* Attach an access point list to a wifi device.  Does nothing for other
   device types. */
void wmnm_wifi_attach(Device *d);

/* Called when the access point view becomes visible or is hidden.  Scanning
   only happens while it is visible: a dockapp should not be waking the radio
   when nobody is looking at it. */
void wmnm_wifi_enter(Device *d);
void wmnm_wifi_leave(Device *d);

void wmnm_wifi_scroll(Device *d, int delta);
void wmnm_wifi_select_row(Device *d, guint row);

const GPtrArray *wmnm_wifi_entries(Device *d);
guint wmnm_wifi_cursor(Device *d);
guint wmnm_wifi_scroll_top(Device *d);
ApEntry *wmnm_wifi_selected(Device *d);

gboolean wmnm_ap_is_secure(const ApEntry *entry);
gboolean wmnm_ap_is_enterprise(const ApEntry *entry);

#endif /* WMNM_WIFI_H */
