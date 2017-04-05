find_program(VALAC valac)
if(NOT VALAC)
	message(FATAL_ERROR "valac not found")
endif()

find_library(READLINE REQUIRED)
pkg_check_modules(GIO2 REQUIRED gio-2.0)
pkg_check_modules(GDK3 REQUIRED gdk-3.0)

execute_process(COMMAND ${VALAC} --version
				OUTPUT_VARIABLE VALAC_VERSION
				OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REPLACE "Vala " "" VALAC_VERSION "${VALAC_VERSION}")
if(VALAC_VERSION VERSION_GREATER 0.34.0)
	list(APPEND VALA_EXTRA_OPTIONS -D VALA_VERSION_NEWER_THEN_0_34)
endif()

execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --atleast-version 3.22 gdk-3.0
				RESULT_VARIABLE GDK_VERSION_NEWER_THEN_3_22)
if(NOT GDK_VERSION_NEWER_THEN_3_22)
	list(APPEND VALA_EXTRA_OPTIONS -D GDK_VERSION_NEWER_THEN_3_22)
endif()

set(DBUS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dbus)

add_custom_command(OUTPUT wfdctl.c
						  wfdctl.h
						  org-freedesktop-networkmanager.c
						  org-freedesktop-miracle-wifi.c
						  org-freedesktop-miracle-wfd.c
				COMMAND ${VALAC} --target-glib=2.50 -H wfdctl.h --use-header -C
								--pkg=gio-2.0
								--pkg=gdk-3.0
								${CMAKE_CURRENT_SOURCE_DIR}/wfdctl.vala
								${CMAKE_CURRENT_SOURCE_DIR}/sigint.vapi
								${CMAKE_CURRENT_SOURCE_DIR}/org-freedesktop-networkmanager.vala
								${CMAKE_CURRENT_SOURCE_DIR}/org-freedesktop-miracle-wifi.vala
								${CMAKE_CURRENT_SOURCE_DIR}/org-freedesktop-miracle-wfd.vala
								${VALA_EXTRA_OPTIONS}
				DEPENDS wfdctl.vala
								${CMAKE_CURRENT_SOURCE_DIR}/org-freedesktop-networkmanager.vala
								${CMAKE_CURRENT_SOURCE_DIR}/org-freedesktop-miracle-wifi.vala
								${CMAKE_CURRENT_SOURCE_DIR}/org-freedesktop-miracle-wfd.vala
				WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

include_directories(${GIO2_INCLUDE_DIRS}
					${GDK3_INCLUDE_DIRS}
					${CMAKE_CURRENT_SOURCE_DIR})

# silent C compiler warning about valac generated code, bad practice
set(CMAKE_C_FLAGS "-Wno-unused-label ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wno-incompatible-pointer-types ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wno-deprecated-declarations ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wno-unused-but-set-variable ${CMAKE_C_FLAGS}")

add_executable(miracle-wfdctl wfdctl
							  sigint.c
							  org-freedesktop-networkmanager.c
							  org-freedesktop-miracle-wifi.c
							  org-freedesktop-miracle-wfd.c)

target_link_libraries(miracle-wfdctl ${GIO2_LIBRARIES}
									 ${GDK3_LIBRARIES})

install(TARGETS miracle-wfdctl DESTINATION bin)
