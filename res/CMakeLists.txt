
########### install files ###############

install(
    PROGRAMS miracle-gst gstplayer uibc-viewer
    DESTINATION bin
    )

INSTALL(
    FILES org.freedesktop.miracle.conf
    DESTINATION /etc/dbus-1/system.d 
    )
