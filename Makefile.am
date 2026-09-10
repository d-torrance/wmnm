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
	wmnm_mask.xbm \
	wmnm_master.xpm

wmnm_CFLAGS = $(x11_CFLAGS) $(dockapp_CFLAGS) $(glib_CFLAGS) $(libnm_CFLAGS) \
	$(xft_CFLAGS)
wmnm_LDADD = $(x11_LIBS) $(dockapp_LIBS) $(glib_LIBS) $(libnm_LIBS) \
	$(xft_LIBS) -lm

#dist_man_MANS = wmnm.1
#EXTRA_DIST = ChangeLog.md README.md
