
########### install files ###############

install(FILES  miracle-gst.sh DESTINATION bin)

INSTALL(
    FILES org.freedesktop.miracle.conf
    DESTINATION /etc/dbus-1/system.d 
    )
