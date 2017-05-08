
########### install files ###############

execute_process(
    COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=system_bus_services_dir dbus-1
    OUTPUT_VARIABLE DBUS_SYSTEM_SERVICES_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=systemdsystemunitdir systemd
    OUTPUT_VARIABLE SYSTEMD_SYSTEM_UNIT_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )

configure_file(
    miracle-wifid.service.cmake
    miracle-wifid.service
    )
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/miracle-wifid.service
    DESTINATION ${SYSTEMD_SYSTEM_UNIT_DIR}/
    )
install(
	CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink miracle-wifid.service \$ENV{DESTDIR}/${SYSTEMD_SYSTEM_UNIT_DIR}/dbus-org.freedesktop.miracle.wifi.service)"
    )

configure_file(
    miracle-dispd.service.cmake
    miracle-dispd.service
    )
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/miracle-dispd.service
    DESTINATION ${SYSTEMD_SYSTEM_UNIT_DIR}/
    )
install(
	CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink miracle-dispd.service \$ENV{DESTDIR}/${SYSTEMD_SYSTEM_UNIT_DIR}/dbus-org.freedesktop.miracle.wfd.service)"
    )

configure_file(
    org.freedesktop.miracle.wifi.service.cmake
    org.freedesktop.miracle.wifi.service
    )
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.miracle.wifi.service
    DESTINATION ${DBUS_SYSTEM_SERVICES_DIR}/
    )

configure_file(
    org.freedesktop.miracle.wfd.service.cmake
    org.freedesktop.miracle.wfd.service
    )
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.miracle.wfd.service
    DESTINATION ${DBUS_SYSTEM_SERVICES_DIR}/
    )

install(
    PROGRAMS miracle-gst gstplayer uibc-viewer
    DESTINATION bin
    )

install(
    FILES org.freedesktop.miracle.conf
    DESTINATION ${SYSCONFDIR}/dbus-1/system.d
    )

install(
    FILES miracle-wifid miracle-sinkctl miracle-wifictl
    DESTINATION ${DATADIR}/bash-completion/completions
    )

include(ValaPrecompile)
pkg_check_modules(GIO2 REQUIRED gio-2.0)
pkg_check_modules(GDK3 REQUIRED gdk-3.0)
pkg_check_modules(GST1 REQUIRED gstreamer-1.0)

include_directories(
	${GST1_INCLUDE_DIRS}
	${GDK3_INCLUDE_DIRS}
	${GIO_INCLUDE_DIRS}
	${CMAKE_CURRENT_SOURCE_DIR}
	)

set(CMAKE_C_FLAGS "-Wno-deprecated-declarations ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wno-unused-but-set-variable ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS "-Wno-missing-braces ${CMAKE_C_FLAGS}")
if(VALA_VERSION VERSION_GREATER 0.34.0 AND NOT GDK_VERSION_NEWER_THEN_3_22)
	list(APPEND VALA_EXTRA_OPTIONS -D GDK3_HAS_MONITOR_CLASS)
endif()

vala_precompile(
	GSTENCODER_SRC gstencoder
		gstencoder.vala
	OPTIONS
		--target-glib=2.50
	PACKAGES
		gstreamer-1.0
		gio-2.0
		posix
	)
add_executable(gstencoder ${GSTENCODER_SRC})
target_link_libraries(
	gstencoder
	${GST1_LIBRARIES}
	${GDK3_LIBRARIES}
	${GIO2_LIBRARIES}
	)

vala_precompile(
	WFDCTL_SRC wfdctl
		wfdctl.vala
		networkmanager.vala
		miracle-wifi.vala
		miracle-wfd.vala
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

########### install files ###############

install(TARGETS gstencoder DESTINATION bin)

install(TARGETS miracle-wfdctl DESTINATION bin)

execute_process(
	COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=system_bus_services_dir dbus-1
	OUTPUT_VARIABLE DBUS_SYSTEM_SERVICES_DIR
	OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
	COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=systemdsystemunitdir systemd
	OUTPUT_VARIABLE SYSTEMD_SYSTEM_UNIT_DIR
	OUTPUT_STRIP_TRAILING_WHITESPACE
	)

configure_file(
	miracle-wifid.service.cmake
	miracle-wifid.service
	)
install(
	FILES ${CMAKE_CURRENT_BINARY_DIR}/miracle-wifid.service
	DESTINATION ${SYSTEMD_SYSTEM_UNIT_DIR}/
	)
install(
	CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink miracle-wifid.service \$ENV{DESTDIR}${SYSTEMD_SYSTEM_UNIT_DIR}/dbus-org.freedesktop.miracle.wifi.service)"
	)

configure_file(
	miracle-dispd.service.cmake
	miracle-dispd.service
	)
install(
	FILES ${CMAKE_CURRENT_BINARY_DIR}/miracle-dispd.service
	DESTINATION ${SYSTEMD_SYSTEM_UNIT_DIR}/
	)
install(
	CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink miracle-dispd.service \$ENV{DESTDIR}${SYSTEMD_SYSTEM_UNIT_DIR}/dbus-org.freedesktop.miracle.wfd.service)"
	)

configure_file(
	org.freedesktop.miracle.wifi.service.cmake
	org.freedesktop.miracle.wifi.service
	)
install(
	FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.miracle.wifi.service
	DESTINATION $ENV{DESTDIR}${DBUS_SYSTEM_SERVICES_DIR}/
	)

configure_file(
	org.freedesktop.miracle.wfd.service.cmake
	org.freedesktop.miracle.wfd.service
	)
install(
	FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.miracle.wfd.service
	DESTINATION $ENV{DESTDIR}${DBUS_SYSTEM_SERVICES_DIR}/
	)

install(
	PROGRAMS miracle-gst gstplayer uibc-viewer
	DESTINATION bin
	)

install(
	FILES org.freedesktop.miracle.conf
	DESTINATION $ENV{DESTDIR}/etc/dbus-1/system.d
	)
