##
## $Id: Makefile.am,v 1.22 2003/11/18 18:15:10 cco Exp $
##
## Process this file with automake to produce Makefile.in
##

## Other intersting options: nostdinc
## 
##

@SET_MAKE@

SUBDIRS = diameter_msg diameter_api transport mem client server

INCLUDES = @INCLTDL@

bin_PROGRAMS = disc
common =			aaa_lock.c dprint.c hash_table.c \
					timer.c aaa_module.c aaa_core.c route.c \
					cfg_init.c cfg_parser.c aaa_parse_uri.c \
					server.c client.c \
					aaa_lock.h dprint.h hash.h lock_alloc.h sem.h \
					timer.h config.h fastlock.h hash_table.h lock_ops.h \
					utils.h counter.h globals.h msg_queue.h msg_queue.c \
					list.h locking.h str.h aaa_module.h aaa_core.h \
					cfg_init.h cfg_parser.h parser_f.h aaa_parse_uri.h \
					route.h server.h client.h

disc_SOURCES = $(common)
# hash.c only for tests
				
EXTRA_DIST=aaa_client.cfg aaa_server.cfg

disc_LDADD =	 mem/libmem.la transport/libtransport.la \
			diameter_api/libdiameter_api.la diameter_msg/libdiameter_msg.la \
			@LIBLTDL@
# @LIBADD_DL@ # not needed for newer libtool version (>=1.4)
disc_LDFLAGS = -export-dynamic -pthread


## install config

sysconfdir = @sysconfdir@/@PACKAGE@
sysconf_DATA = aaa_client.cfg aaa_server.cfg 


