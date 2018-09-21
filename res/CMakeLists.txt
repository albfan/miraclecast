INSTALL(
    PROGRAMS miracle-gst gstplayer uibc-viewer
    DESTINATION bin
    )

INSTALL(
    FILES org.freedesktop.miracle.conf
    DESTINATION ${SYSCONFDIR}/dbus-1/system.d
    )

INSTALL(
    FILES miracle-wifid miracle-sinkctl miracle-wifictl
    DESTINATION ${DATADIR}/bash-completion/completions
    )
