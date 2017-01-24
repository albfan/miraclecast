find_package(Readline)
########### next target ###############

set(miracle-wifictl_SRCS ctl.h 
                         ctl-cli.c 
                         ctl-wifi.c 
                         wifictl.c)

add_executable(miracle-wifictl ${miracle-wifictl_SRCS})

install(TARGETS miracle-wifictl DESTINATION bin)

if(READLINE_FOUND)
	message(STATUS "Compiling with Readline support")
	set_property(TARGET miracle-wifictl
		APPEND
		PROPERTY COMPILE_DEFINITIONS HAVE_READLINE)
	target_link_libraries(miracle-wifictl ${READLINE_LIBRARY})
endif(READLINE_FOUND)

target_link_libraries(miracle-wifictl miracle-shared)
########### next target ###############

set(miracle-sinkctl_SRCS ctl.h 
                         ctl-cli.c 
                         ctl-sink.h
                         ctl-sink.c 
                         ctl-wifi.c 
                         sinkctl.c
                         wfd.c)

add_executable(miracle-sinkctl ${miracle-sinkctl_SRCS})

install(TARGETS miracle-sinkctl DESTINATION bin)

if(READLINE_FOUND)
	message(STATUS "Compiling with Readline support")
	set_property(TARGET miracle-sinkctl
		APPEND
		PROPERTY COMPILE_DEFINITIONS HAVE_READLINE)
	target_link_libraries(miracle-sinkctl ${READLINE_LIBRARY})
endif(READLINE_FOUND)

target_link_libraries(miracle-sinkctl miracle-shared)

include_directories(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/shared)

########### next target ###############
set(miracle-wfdctl_SRCS ctl-cli.c
						ctl-wifi.c
						wfd-src.c
						wfd-sink.c
						wfd-dbus.c
						wfd-session.c
						wfdctl.c
						wfd.c)

include_directories(${CMAKE_BINARY_DIR}
                    ${CMAKE_SOURCE_DIR}/src
					${CMAKE_SOURCE_DIR}/src/shared)

add_executable(miracle-wfdctl ${miracle-wfdctl_SRCS})

install(TARGETS miracle-wfdctl DESTINATION bin)

if(READLINE_FOUND)
	message(STATUS "Compiling with Readline support")
	set_property(TARGET miracle-wfdctl
						APPEND
						PROPERTY COMPILE_DEFINITIONS HAVE_READLINE)
	target_link_libraries(miracle-wfdctl ${READLINE_LIBRARY})
endif(READLINE_FOUND)

target_link_libraries(miracle-wfdctl miracle-shared)

########### install files ###############




#original Makefile.am contents follow:

#include $(top_srcdir)/common.am
#bin_PROGRAMS = miracle-wifictl miracle-sinkctl
#
#miracle_wifictl_SOURCES = \
#	ctl.h \
#	ctl-cli.c \
#	ctl-wifi.c \
#	wifictl.c
#miracle_wifictl_CPPFLAGS = \
#	$(AM_CPPFLAGS) \
#	$(DEPS_CFLAGS)
#miracle_wifictl_LDADD = \
#	../shared/libmiracle-shared.la \
#	-lreadline \
#	$(DEPS_LIBS)
#
#miracle_sinkctl_SOURCES = \
#	ctl.h \
#	ctl-cli.c \
#	ctl-sink.c \
#	ctl-wifi.c \
#	sinkctl.c
#miracle_sinkctl_CPPFLAGS = \
#	$(AM_CPPFLAGS) \
#	$(DEPS_CFLAGS)
#miracle_sinkctl_LDADD = \
#	../shared/libmiracle-shared.la \
#	-lreadline \
#	$(DEPS_LIBS)
#
#

