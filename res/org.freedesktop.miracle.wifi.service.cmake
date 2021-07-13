[D-BUS Service]
Name=org.freedesktop.miracle.wifi
Exec=/bin/sh -c 'PATH=/sbin:/usr/bin @CMAKE_INSTALL_PREFIX@/bin/miracle-wifid --use-dev --log-level trace'
User=root
SystemdService=dbus-org.freedesktop.miracle.wifi.service
