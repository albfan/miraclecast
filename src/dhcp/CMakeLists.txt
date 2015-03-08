
########### next target ###############

set(miracle-dhcp_SRCS ${CMAKE_CURRENT_BINARY_DIR}/dhcp.c 
                      ${CMAKE_CURRENT_BINARY_DIR}/gdhcp.h 
                      ${CMAKE_CURRENT_BINARY_DIR}/unaligned.h 
                      ${CMAKE_CURRENT_BINARY_DIR}/common.h 
                      ${CMAKE_CURRENT_BINARY_DIR}/common.c 
                      ${CMAKE_CURRENT_BINARY_DIR}/ipv4ll.h 
                      ${CMAKE_CURRENT_BINARY_DIR}/ipv4ll.c 
                      ${CMAKE_CURRENT_BINARY_DIR}/client.c 
                      ${CMAKE_CURRENT_BINARY_DIR}/server.c)

add_executable(miracle-dhcp ${miracle-dhcp_SRCS})

target_link_libraries(miracle-dhcp ${KDE4_KDECORE_LIBS})

install(TARGETS miracle-dhcp DESTINATION bin)


########### install files ###############




#original Makefile.am contents follow:

#include $(top_srcdir)/common.am
#bin_PROGRAMS = miracle-dhcp
#
#miracle_dhcp_SOURCES = \
#	dhcp.c \
#	gdhcp.h \
#	unaligned.h \
#	common.h \
#	common.c \
#	ipv4ll.h \
#	ipv4ll.c \
#	client.c \
#	server.c
#miracle_dhcp_CPPFLAGS = \
#	$(AM_CPPFLAGS) \
#	$(DEPS_CFLAGS) \
#	$(GDHCP_CFLAGS)
#miracle_dhcp_LDADD = \
#	../shared/libmiracle-shared.la \
#	$(DEPS_LIBS) \
#	$(GDHCP_LIBS)
#
#
#
