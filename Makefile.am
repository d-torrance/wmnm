AM_CPPFLAGS = -DWMNM_ASKPASS_PATH='"$(bindir)/wmnm-askpass"'

bin_PROGRAMS = wmnm

wmnm_SOURCES = \
	wmnm.c \
	wmnm.h \
	wmnm-loop.c \
	wmnm-loop.h \
	wmnm-ui.c \
	wmnm-ui.h \
	wmnm-wifi.c \
	wmnm-wifi.h \
	wmnm-connect.c \
	wmnm-connect.h \
	wmnm-agent.c \
	wmnm-agent.h \
	wmnm-askpass-client.c \
	wmnm-askpass-client.h \
	wmnm_mask.xbm \
	wmnm_master.xpm

wmnm_CFLAGS = $(x11_CFLAGS) $(dockapp_CFLAGS) $(glib_CFLAGS) $(libnm_CFLAGS) \
	$(xft_CFLAGS)
wmnm_LDADD = $(x11_LIBS) $(dockapp_LIBS) $(glib_LIBS) $(libnm_LIBS) \
	$(xft_LIBS) -lm

if BUILD_ASKPASS
bin_PROGRAMS += wmnm-askpass
wmnm_askpass_SOURCES = wmnm-askpass.c
wmnm_askpass_CFLAGS = $(WINGS_CFLAGS)
wmnm_askpass_LDADD = $(WINGS_LIBS)
endif

dist_man1_MANS = wmnm.1
if BUILD_ASKPASS
dist_man1_MANS += wmnm-askpass.1
endif

# Ship the helper's sources and manual page even when WINGs is missing here,
# so the tarball can still build it elsewhere.
EXTRA_DIST = README.md wmnm-askpass.c wmnm-askpass.1
