NULL =

EGGSOURCES = \
	eggtreemultidnd.c \
        eggdesktopfile.c \
        $(NULL)

EGGHEADERS = \
	eggtreemultidnd.h \
        eggdesktopfile.h \
        $(NULL)

noinst_HEADERS = \
	$(EGGHEADERS) \
	$(NULL)

noinst_LTLIBRARIES = libegg.la

libegg_la_SOURCES = \
	$(EGGSOURCES) \
	$(EGGHEADERS)

libegg_la_CPPFLAGS = \
	-DG_LOG_DOMAIN=\""Egg"\"		\
	$(AM_CPPFLAGS)

libegg_la_CFLAGS = \
	$(DEPENDENCIES_CFLAGS)		\
	$(AM_CFLAGS)

