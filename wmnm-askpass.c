/* wmnm-askpass - password prompt for wmnm, and a general purpose askpass
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

/* Implements the askpass convention shared by ssh, git and sudo: take the
 * prompt as the first argument, write the secret to standard output, and exit
 * zero.  A non-zero exit means the user cancelled.  That makes this usable
 * both by wmnm and as $SSH_ASKPASS, $GIT_ASKPASS or $SUDO_ASKPASS.
 */

#include <WINGs/WINGs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	WMScreen *screen;
	WMInputPanel *panel;
	WMView *view;
	const char *prompt = argc > 1 ? argv[1] : "Password:";
	char *secret;
	int px, py;

	WMInitializeApplication("wmnm-askpass", &argc, argv);

	screen = WMOpenScreen(NULL);
	if (!screen) {
		fprintf(stderr, "wmnm-askpass: cannot open display\n");
		return 1;
	}

	/* WMRunInputPanel() would be the obvious call, but it creates the
	   panel internally, so there is no opportunity to mark the field
	   secure before it is mapped.  Do what it does, with that one extra
	   step. */
	panel = WMCreateInputPanel(screen, NULL, "wmnm", prompt, "",
				   "OK", "Cancel");
	WMSetTextFieldSecure(panel->text, True);

	view = WMWidgetView(panel->win);
	px = (WMScreenWidth(screen) - WMWidgetWidth(panel->win)) / 2;
	py = (WMScreenHeight(screen) - WMWidgetHeight(panel->win)) / 2;
	WMSetWindowInitialPosition(panel->win, px, py);

	WMMapWidget(panel->win);
	WMRunModalLoop(screen, view);

	secret = panel->result == WAPRDefault
		? WMGetTextFieldText(panel->text) : NULL;
	WMDestroyInputPanel(panel);

	if (!secret)
		return 1;		/* cancelled */

	printf("%s\n", secret);
	fflush(stdout);

	/* Best effort: do not leave the secret sitting in freed memory. */
	memset(secret, 0, strlen(secret));
	wfree(secret);

	return 0;
}
