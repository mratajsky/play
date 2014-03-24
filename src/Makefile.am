AM_CPPFLAGS =						\
		-Wall						\
		-DDISABLE_DEPRECATED		\
		$(GLIB_CFLAGS) 				\
		$(LIBXML2_CFLAGS) 			\
		$(GSTREAMER_CFLAGS)

BUILT_SOURCES = 					\
		marshal.c 					\
		marshal.h

bin_PROGRAMS = play
play_SOURCES = 						\
		$(BUILT_SOURCES)			\
		play-common.h 				\
		play-downloader.c 			\
		play-downloader.h 			\
		play-gstreamer.c 			\
		play-gstreamer.h 			\
		play-playlist.c 			\
		play-playlist.h 			\
		play-queue.c 				\
		play-queue.h 				\
		play-queue-item.c 			\
		play-queue-item.h 			\
		play-simple-queue.c 		\
		play-simple-queue.h 		\
		play-terminal.c				\
		play-terminal.h 			\
		play.c

play_LDADD =						\
		$(GLIB_LIBS)				\
		$(LIBXML2_LIBS)				\
		$(GSTREAMER_LIBS)

marshal.c: Makefile marshal.list
	@GLIB_GENMARSHAL@ --prefix=play_marshal $(srcdir)/marshal.list --header --body >> $@.tmp
	mv $@.tmp $@

marshal.h: Makefile marshal.list
	@GLIB_GENMARSHAL@ --prefix=play_marshal $(srcdir)/marshal.list --header > marshal.h

DISTCLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = marshal.list
