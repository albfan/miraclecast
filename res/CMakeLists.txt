
########### install files ###############

execute_process(
    COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=session_bus_services_dir dbus-1
    OUTPUT_VARIABLE DBUS_SESSION_SERVICES_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

configure_file(
    org.freedesktop.miracle.service.cmake
    org.freedesktop.miracle.service
    )

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.miracle.service
    DESTINATION ${DBUS_SESSION_SERVICES_DIR}/
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
