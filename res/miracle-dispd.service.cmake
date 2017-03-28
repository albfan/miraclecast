[Unit]
Description=Miraclecast WiFi Display Service
After=dbus.service
Requires=miracle-wifid.service
After=miracle-wifid.service

[Service]
BusName=org.freedesktop.miracle.wfd
Environment=LOG_LEVEL=trace
ExecStart=@CMAKE_INSTALL_PREFIX@/bin/miracle-dispd
KillSignal=SIGKILL

[Install]
WantedBy=multi-user.target
Alias=dbus-org.freedesktop.miracle.wfd.service
