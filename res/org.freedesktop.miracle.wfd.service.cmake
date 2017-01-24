[D-BUS Service]
Name=org.freedesktop.miracle.wfd
Exec=/bin/sh -c 'PATH=/sbin:/usr/bin LOG_LEVEL=trace @CMAKE_INSTALL_PREFIX@/bin/miracle-wfdctl'
User=root
SystemdService=dbus-org.freedesktop.miracle.wfd.service
