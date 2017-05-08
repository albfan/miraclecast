find_library(READLINE REQUIRED)
pkg_check_modules(GIO2 REQUIRED gio-2.0)
pkg_check_modules(GDK3 REQUIRED gdk-3.0)
include(ValaPrecompile)

include_directories(${GIO2_INCLUDE_DIRS}
					${GDK3_INCLUDE_DIRS}
					${CMAKE_CURRENT_SOURCE_DIR}
)

set(CMAKE_C_FLAGS "-Wno-deprecated-declarations ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wno-unused-but-set-variable ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wno-missing-braces ${CMAKE_C_FLAGS}")

if(VALA_VERSION VERSION_GREATER 0.34.0 AND NOT GDK_VERSION_NEWER_THEN_3_22)
	list(APPEND VALA_EXTRA_OPTIONS -D GDK3_HAS_MONITOR_CLASS)
endif()

vala_precompile(WFDCTL_SRC wfdctl
					wfdctl.vala
					org-freedesktop-networkmanager.vala
					org-freedesktop-miracle-wifi.vala
					org-freedesktop-miracle-wfd.vala
				GENERATE_HEADER
					wfdctl.h
				CUSTOM_VAPIS
					sigint.vapi
				OPTIONS
					 --target-glib=2.50
					 ${VALA_EXTRA_OPTIONS}
				PACKAGES
					gio-2.0
					gdk-3.0
)

add_executable(miracle-wfdctl ${WFDCTL_SRC} sigint.c)
target_link_libraries(miracle-wfdctl ${GIO2_LIBRARIES} ${GDK3_LIBRARIES})

install(TARGETS miracle-wfdctl DESTINATION bin)
