[Unit]
Description=Miraclecast WiFi Display Service
After=dbus.service
Requires=miracle-wifid.service
After=miracle-wifid.service

[Service]
BusName=org.freedesktop.miracle.wfd
Environment=LOG_LEVEL=trace
ExecStart=@CMAKE_INSTALL_PREFIX@/bin/miracle-dispd
CapabilityBoundingSet=CAP_NET_BIND_SERVICE \
	CAP_SETGID \
	CAP_SETUID \
	CAP_SETPCAP

[Install]
WantedBy=multi-user.target
Alias=dbus-org.freedesktop.miracle.wfd.service
