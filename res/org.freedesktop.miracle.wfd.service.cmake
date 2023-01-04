[D-BUS Service]
Name=org.freedesktop.miracle.wfd
Exec=/bin/sh -c 'LOG_LEVEL=trace @CMAKE_INSTALL_PREFIX@/bin/miracle-dispd'
User=root
SystemdService=dbus-org.freedesktop.miracle.wfd.service
