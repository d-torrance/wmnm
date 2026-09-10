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

#ifndef WMNM_CONNECT_H
#define WMNM_CONNECT_H

#include "wmnm.h"
#include "wmnm-wifi.h"

typedef enum {
	AP_SEC_OPEN,
	AP_SEC_WEP,
	AP_SEC_PSK,
	AP_SEC_SAE,
	AP_SEC_OWE,
	AP_SEC_ENTERPRISE
} ApSecurity;

void wmnm_connect_init(NMClient *client);

/* Report activation progress and failures for this device. */
void wmnm_connect_watch_device(NMDevice *device);
ApSecurity wmnm_ap_security(const ApEntry *entry);

/* Activate the given network, reusing a saved profile when NetworkManager
   already has one that matches. */
void wmnm_connect_to(Device *d, ApEntry *entry);

/* TRUE while NetworkManager reports the connection is behind a captive
   portal. */
gboolean wmnm_portal_active(void);

/* Open the captive portal in the default browser. */
void wmnm_portal_open(void);

/* Two short lines for VIEW_STATUS, or NULL when there is nothing to say. */
const char *wmnm_status_line1(void);
const char *wmnm_status_line2(void);

#endif /* WMNM_CONNECT_H */
