[Unit]
Description=Miraclecast WiFi Display Control

[Service]
BusName=org.freedesktop.miracle.wfd
Environment=PATH=/sbin:/usr/bin
Environment=LOG_LEVEL=trace
ExecStart=@CMAKE_INSTALL_PREFIX@/bin/miracle-wfdctl

[Install]
Allias=dbus-org.freedesktop.miracle.wfd.service
