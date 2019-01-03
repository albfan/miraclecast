set(CMAKE_C_FLAGS "-std=gnu11") 

set(miracle-shared_SOURCES rtsp.h
                             rtsp.c 
                             shl_dlist.h 
                             shl_htable.h 
                             shl_htable.c 
                             shl_log.h 
                             shl_log.c 
                             shl_macro.h
                             shl_ring.h 
                             shl_ring.c 
                             shl_util.h 
                             shl_util.c 
                             util.h 
                             wpas.h 
                             wpas.c)
add_library(miracle-shared STATIC ${miracle-shared_SOURCES})
target_link_libraries (miracle-shared ${SESSION_LIBRARIES})


########### install files ###############




#original Makefile.am contents follow:

#include $(top_srcdir)/common.am
#noinst_LTLIBRARIES = libmiracle-shared.la
#
#libmiracle_shared_la_SOURCES = \
#	rtsp.h \
#	rtsp.c \
#	shl_dlist.h \
#	shl_htable.h \
#	shl_htable.c \
#	shl_log.h \
#	shl_log.c \
#	shl_macro.h \
#	shl_ring.h \
#	shl_ring.c \
#	shl_util.h \
#	shl_util.c \
#	util.h \
#	wpas.h \
#	wpas.c
#libmiracle_shared_la_LIBADD = -lsystemd
#
#
#
