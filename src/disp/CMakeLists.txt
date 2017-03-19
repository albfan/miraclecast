set(miracle-dispd_SRCS ../ctl/ctl-cli.c
						../ctl/ctl-wifi.c
						wfd-sink.c
						wfd-dbus.c
						wfd-session.c
						wfd-out-session.c
						dispd.c
						../ctl/wfd.c
						wfd-arg.c)

include_directories(${CMAKE_SOURCE_DIR}/src/ctl
					${CMAKE_BINARY_DIR}
					${GSTREAMER_INCLUDE_DIRS}
					${GSTREAMER_BASE_INCLUDE_DIRS}
					${CMAKE_SOURCE_DIR}/src
					${CMAKE_SOURCE_DIR}/src/shared)

add_executable(miracle-dispd ${miracle-dispd_SRCS})

install(TARGETS miracle-dispd DESTINATION bin)

if(READLINE_FOUND)
	message(STATUS "Compiling with Readline support")
	set_property(TARGET miracle-dispd
						APPEND
						PROPERTY COMPILE_DEFINITIONS HAVE_READLINE)
	target_link_libraries(miracle-dispd ${READLINE_LIBRARY})
endif(READLINE_FOUND)

target_link_libraries(miracle-dispd
				miracle-shared
				${GSTREAMER_LIBRARIES}
				${GSTREAMER_BASE_LIBRARIES}
				${READLINE_LIBRARY})

