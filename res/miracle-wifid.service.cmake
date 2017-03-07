[Unit]
Description=Miraclecast WiFiD

[Service]
BusName=org.freedesktop.miracle.wifi
Environment=PATH=/sbin:/usr/bin
ExecStart=@CMAKE_INSTALL_PREFIX@/bin/miracle-wifid --use-dev --log-level trace --lazy-managed

[Install]
Alias=dbus-org.freedesktop.miracle.wifi.service
