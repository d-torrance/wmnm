# wmnm

A [NetworkManager](https://networkmanager.dev/) frontend as a
[Window Maker](https://www.windowmaker.org/) dockapp.

`wmnm` exists so that a laptop running Window Maker can join wireless networks
without pulling in nm-applet, GNOME or KDE.  It is a 64x64 icon that lists
nearby networks, connects to them, prompts for passphrases, and brings up
captive portal login pages.

## Features

* Cycle through NetworkManager's devices; signal strength, link speed and
  current network for wifi, type and MAC address for everything else.
* Scroll through nearby networks, sorted by signal strength, deduplicated
  across access points.  Mouse wheel and two-finger touchpad scrolling both
  work, and there are click zones for pointing devices without a scroll axis.
* Connect with two clicks.  Known networks reconnect without prompting.
* Passphrase prompts through `wmnm-askpass`, a WINGs dialog.
* Captive portals open automatically in the default browser.
* 802.1x networks are handed to `nm-connection-editor`.

## Building

    ./autogen.sh      # or: autoreconf -i
    ./configure
    make
    make install

Requires `libnm`, `libdockapp`, `glib`, `libxft` and `libx11`.

`wmnm-askpass` additionally needs WINGs (`libwings-dev` on Debian and
derivatives).  It is optional: `configure` warns and skips it if WINGs is
missing, and `wmnm` then falls back to `$SSH_ASKPASS`.  Build without it
explicitly with `./configure --without-wings`.

## Usage

See `wmnm(1)`.  Briefly:

| Action | Effect |
| --- | --- |
| Click the interface name | Cycle devices |
| Scroll, or click the body | Open the network list |
| Click a network | Select it |
| Click it again | Connect |
| Middle click | Reopen the captive portal page |
| Right click | Back to the device view |

## Secret agents

NetworkManager only asks *registered secret agents* for passphrases, and it
asks them in sequence, taking the first answer.  `wmnm` registers itself as
one.  If nm-applet, gnome-shell or plasma-nm is also running you may get its
dialog instead, unpredictably, so run only one of them.

## wmnm-askpass on its own

`wmnm-askpass` follows the askpass convention shared by ssh, git and sudo:
prompt in `argv[1]`, secret on stdout, non-zero exit to cancel.  It is
therefore useful outside `wmnm`:

    SSH_ASKPASS=wmnm-askpass SSH_ASKPASS_REQUIRE=prefer ssh-add
    git config --global core.askPass wmnm-askpass
    SUDO_ASKPASS=/usr/bin/wmnm-askpass sudo -A command

Note that it is *not* a pinentry replacement; GnuPG speaks the Assuan protocol
instead.

See `wmnm-askpass(1)`.

## License

GPL-2.0-or-later.  See the header of any source file.
