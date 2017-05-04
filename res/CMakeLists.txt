
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

find_program(VALAC valac)
if(NOT VALAC)
	message(FATAL_ERROR "valac not found")
endif()

add_custom_command(OUTPUT gstencoder.c
				COMMAND ${VALAC} --target-glib=2.50 -C
								--pkg=gstreamer-1.0
								--pkg=gio-2.0
								--pkg=posix
								${CMAKE_CURRENT_SOURCE_DIR}/gstencoder.vala
				DEPENDS gstencoder.vala
				WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

include_directories(${GIO2_INCLUDE_DIRS}
					${GDK3_INCLUDE_DIRS}
					${CMAKE_CURRENT_SOURCE_DIR})

pkg_check_modules(GST1 REQUIRED gstreamer-1.0)
pkg_check_modules(GIO2 REQUIRED gio-2.0)

include_directories(${GST1_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS})

add_executable(gstencoder gstencoder)

target_link_libraries(gstencoder ${GST1_LIBRARIES} ${GIO2_LIBRARIES})

install(TARGETS gstencoder DESTINATION bin)
