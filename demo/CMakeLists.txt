find_program(VALAC valac)
if(NOT VALAC)
	message(FATAL_ERROR "valac not found")
endif()
find_program(VALA_DBUS_BINDING_TOOL vala-dbus-binding-tool)
if(NOT VALA_DBUS_BINDING_TOOL)
	message(FATAL_ERROR "vala-dbus-binding-tool not found")
endif()
find_library(READLINE REQUIRED)

pkg_check_modules(GIO2 REQUIRED gio-2.0)

set(RES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/res)
set(DBUS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dbus)

add_custom_command(OUTPUT org-freedesktop-networkmanager.vala
						  org-freedesktop-miracle-wifi.vala
						  org-freedesktop-miracle-wfd.vala
				COMMAND ${VALA_DBUS_BINDING_TOOL}
						--gdbus
						--no-synced
						--rename-namespace=org:Org
						--rename-namespace=freedesktop:Freedesktop
						--rename-namespace=miracle:Miracle
						--rename-namespace=wifi:Wifi
						--rename-namespace=wfd:Wfd
						--api-path=${DBUS_DIR}
				WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(OUTPUT wfdctl.c
						  wfdctl.h
						  org-freedesktop-networkmanager.c
						  org-freedesktop-miracle-wifi.c
						  org-freedesktop-miracle-wfd.c
				COMMAND ${VALAC} --target-glib=2.50 -H wfdctl.h --use-header -C
								--pkg=gio-2.0
								${CMAKE_CURRENT_SOURCE_DIR}/wfdctl.vala
								org-freedesktop-networkmanager.vala
								org-freedesktop-miracle-wifi.vala
								org-freedesktop-miracle-wfd.vala
				DEPENDS wfdctl.vala
						${CMAKE_CURRENT_BINARY_DIR}/org-freedesktop-networkmanager.vala
						${CMAKE_CURRENT_BINARY_DIR}/org-freedesktop-miracle-wifi.vala
						${CMAKE_CURRENT_BINARY_DIR}/org-freedesktop-miracle-wfd.vala
				WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(OUTPUT wfdctl-res.c
				COMMAND ${GLIB_COMPILE_RESOURCES}
								--target=${CMAKE_CURRENT_BINARY_DIR}/wfdctl-res.c
								--generate-source ${RES_DIR}/wfdctl-res.xml
				DEPENDS ${RES_DIR}/wfdctl.ui
						${RES_DIR}/wfdctl-res.xml
				WORKING_DIRECTORY ${RES_DIR})

include_directories(${GIO2_INCLUDE_DIRS})

# silent C compiler warning about valac generated code, bad practice
set(CMAKE_C_FLAGS "-Wno-unused-label ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wno-incompatible-pointer-types ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wno-deprecated-declarations ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wno-unused-but-set-variable ${CMAKE_C_FLAGS}")

add_executable(miracle-wfdctl wfdctl
							  wfdctl-res.c
							  org-freedesktop-networkmanager.c
							  org-freedesktop-miracle-wifi.c
							  org-freedesktop-miracle-wfd.c)

target_link_libraries(miracle-wfdctl ${GIO2_LIBRARIES})

