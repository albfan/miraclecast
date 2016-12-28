
########### install files ###############

execute_process(
    COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=session_bus_services_dir dbus-1
    OUTPUT_VARIABLE DBUS_SESSION_SERVICES_DIR
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
    CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink miracle-wifid.service ${SYSTEMD_SYSTEM_UNIT_DIR}/dbus-org.freedesktop.miracle.wifi.service)"
    )

configure_file(
    org.freedesktop.miracle.wifi.service.cmake
    org.freedesktop.miracle.wifi.service
    )

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.miracle.wifi.service
    DESTINATION ${DBUS_SYSTEM_SERVICES_DIR}/
    )

install(
    PROGRAMS miracle-gst gstplayer uibc-viewer
    DESTINATION bin
    )

install(
    FILES org.freedesktop.miracle.conf
    DESTINATION /etc/dbus-1/system.d 
    )
