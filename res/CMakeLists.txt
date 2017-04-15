
########### install files ###############

install(
    PROGRAMS miracle-gst gstplayer uibc-viewer
    DESTINATION bin
    )

INSTALL(
    FILES org.freedesktop.miracle.conf
    DESTINATION ${SYSCONFDIR}/dbus-1/system.d
    )
