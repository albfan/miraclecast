
########### install files ###############

install(
    PROGRAMS miracle-gst gstplayer uibc-viewer miracle-stop-network-manager miracle-utils.sh miracle-start-network-manager
    DESTINATION bin
    )

INSTALL(
    FILES org.freedesktop.miracle.conf
    DESTINATION /etc/dbus-1/system.d 
    )
