find_package(PkgConfig)
pkg_check_modules (GLIB2 REQUIRED glib-2.0)
link_directories( ${GLIB2_LIBRARY_DIRS})
include_directories( ${GLIB2_INCLUDE_DIRS})
find_package(Readline REQUIRED)

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
target_link_libraries(miracle-wifictl ${GLIB2_LIBRARIES})

set(miracle-sinkctl_SRCS ctl.h 
                         ctl-cli.c 
                         ctl-sink.h
                         ctl-sink.c 
                         ctl-wifi.c 
                         sinkctl.c
                         wfd.c)

add_executable(miracle-sinkctl ${miracle-sinkctl_SRCS})
target_link_libraries(miracle-sinkctl ${GLIB2_LIBRARIES})

install(TARGETS miracle-sinkctl DESTINATION bin)

if(READLINE_FOUND)
	message(STATUS "Compiling with Readline support")
	set_property(TARGET miracle-sinkctl
		APPEND
		PROPERTY COMPILE_DEFINITIONS HAVE_READLINE)
	target_link_libraries(miracle-sinkctl ${READLINE_LIBRARY})
endif(READLINE_FOUND)

target_link_libraries(miracle-sinkctl miracle-shared)

include_directories(${CMAKE_BINARY_DIR}
         ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/shared)
