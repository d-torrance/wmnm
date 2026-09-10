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

/* Two 16x16 eyes for the reveal button: an open one offering to show the
   password, a shut one offering to hide it again. */
#define EYE_WIDTH 16

static char *EYE_OPEN_XPM[] = {
	"16 16 2 1",
	"  c None",
	". c #202020",
	"                ",
	"                ",
	"                ",
	"                ",
	"     ......     ",
	"   ..      ..   ",
	"  .   ....   .  ",
	" .    ....    . ",
	" .    ....    . ",
	"  .   ....   .  ",
	"   ..      ..   ",
	"     ......     ",
	"                ",
	"                ",
	"                ",
	"                "
};

static char *EYE_SHUT_XPM[] = {
	"16 16 2 1",
	"  c None",
	". c #202020",
	"                ",
	"                ",
	"                ",
	"                ",
	"                ",
	"                ",
	"                ",
	" .            . ",
	"  ..        ..  ",
	"    ...  ...    ",
	"       ..       ",
	"   .   ..   .   ",
	"                ",
	"                ",
	"                ",
	"                "
};

static void toggleSecure(WMWidget *self, void *data)
{
	WMInputPanel *panel = (WMInputPanel *)data;
	Bool show = WMGetButtonSelected((WMButton *)self) ? True : False;

	WMSetTextFieldSecure(panel->text, !show);

	/* Clicking the button takes the focus away from where the user was
	   typing; hand it straight back. */
	WMSetFocusToWidget(panel->text);
}

int main(int argc, char **argv)
{
	WMScreen *screen;
	WMInputPanel *panel;
	WMView *view;
	WMButton *reveal;
	WMPixmap *eyeOpen, *eyeShut;
	WMFont *font;
	const char *label = "Show";
	const char *altLabel = "Hide";
	const char *prompt = argc > 1 ? argv[1] : "Password:";
	char *secret;
	int px, py, bw, tw, room;

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

	/* The size the panel uses for its own labels and buttons. */
	font = WMSystemFontOfSize(screen, 12);

	/* A bordered button rather than a plain switch, so the shadows move
	   as it is pressed: without a bevel the bare eye looks like
	   decoration instead of something to click.  WBTToggle would be the
	   ready-made type, but it also carries WBBStatePushMask, which keeps
	   the button sunken for as long as the password is showing.  The
	   caption and the icon already say which way things stand, so let it
	   come back up on release like any other button. */
	reveal = WMCreateCustomButton(panel->win,
				      WBBPushInMask | WBBStateChangeMask);
	WMSetButtonText(reveal, label);
	WMSetButtonAltText(reveal, altLabel);
	WMSetButtonTextAlignment(reveal, WALeft);
	WMSetButtonImagePosition(reveal, WIPLeft);
	WMSetButtonFont(reveal, font);
	WMSetButtonAction(reveal, toggleSecure, panel);

	/* Icon and caption both say what a click would do, not what is
	   already true: an open eye and "Show" while the password is masked,
	   a shut one and "Hide" while it is not.  That is the way browsers
	   and password managers do it. */
	eyeShut = WMCreatePixmapFromXPMData(screen, EYE_SHUT_XPM);
	eyeOpen = WMCreatePixmapFromXPMData(screen, EYE_OPEN_XPM);
	if (eyeShut && eyeOpen) {
		WMSetButtonImage(reveal, eyeOpen);
		WMSetButtonAltImage(reveal, eyeShut);
	}
	if (eyeShut)
		WMReleasePixmap(eyeShut);
	if (eyeOpen)
		WMReleasePixmap(eyeOpen);

	/* Share the button row rather than taking a row of its own, which
	   left the panel looking like two unrelated halves.  That means
	   fitting in the space to the left of Cancel, so the caption is one
	   word.  The panel puts its buttons at 310 - 2 * (width + 10), so
	   that is where the room runs out. */
	tw = WMWidthOfString(font, label, strlen(label));
	bw = WMWidthOfString(font, altLabel, strlen(altLabel));
	bw = EYE_WIDTH + (tw > bw ? tw : bw) + 24;
	room = 310 - 2 * (WMWidgetWidth(panel->altBtn) + 10) - 20 - 10;
	if (bw > room)
		bw = room;
	WMMoveWidget(reveal, 20, 124);
	WMResizeWidget(reveal, bw, 24);

	WMRealizeWidget(reveal);
	WMMapWidget(reveal);

	view = WMWidgetView(panel->win);
	px = (WMScreenWidth(screen) - WMWidgetWidth(panel->win)) / 2;
	py = (WMScreenHeight(screen) - WMWidgetHeight(panel->win)) / 2;
	WMSetWindowInitialPosition(panel->win, px, py);

	WMMapWidget(panel->win);
	WMRunModalLoop(screen, view);

	secret = panel->result == WAPRDefault
		? WMGetTextFieldText(panel->text) : NULL;
	WMDestroyInputPanel(panel);
	WMReleaseFont(font);

	if (!secret)
		return 1;		/* cancelled */

	printf("%s\n", secret);
	fflush(stdout);

	/* Best effort: do not leave the secret sitting in freed memory. */
	memset(secret, 0, strlen(secret));
	wfree(secret);

	return 0;
}
