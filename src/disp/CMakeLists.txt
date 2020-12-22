find_package(PkgConfig)
pkg_check_modules (GLIB2 REQUIRED glib-2.0)
link_directories( ${GLIB2_LIBRARY_DIRS})
include_directories( ${GLIB2_INCLUDE_DIRS})
find_package(Readline REQUIRED)

set(miracle-dispd_SRCS
				../ctl/ctl-cli.c
				../ctl/ctl-wifi.c
				../ctl/wfd.c
				dispd-sink.c
				dispd-dbus.c
				dispd-session.c
				dispd-out-session.c
				dispd.c
				dispd-encoder.c
				dispd-arg.c
)

include_directories(${CMAKE_SOURCE_DIR}/src/ctl
					${CMAKE_BINARY_DIR}
					${CMAKE_SOURCE_DIR}/src
					${CMAKE_SOURCE_DIR}/src/shared
)

add_executable(miracle-dispd ${miracle-dispd_SRCS})
target_link_libraries(miracle-dispd ${GLIB2_LIBRARIES})

install(TARGETS miracle-dispd DESTINATION bin)

if(READLINE_FOUND)
	message(STATUS "Compiling with Readline support")
	set_property(TARGET miracle-dispd
						APPEND
						PROPERTY COMPILE_DEFINITIONS HAVE_READLINE
	)
	target_link_libraries(miracle-dispd ${READLINE_LIBRARY})
endif(READLINE_FOUND)

target_link_libraries(miracle-dispd
				miracle-shared
				${READLINE_LIBRARY}
)

