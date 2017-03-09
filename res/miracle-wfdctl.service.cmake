[Unit]
Description=Miraclecast WiFi Display Controller
After=dbus.service
Requires=miracle-wifid.service
After=miracle-wifid.service

[Service]
BusName=org.freedesktop.miracle.wfd
Environment=PATH=/sbin:/usr/bin
Environment=LOG_LEVEL=trace
ExecStart=@CMAKE_INSTALL_PREFIX@/bin/miracle-wfdctl

[Install]
WantedBy=multi-user.target
Alias=dbus-org.freedesktop.miracle.wfd.service
