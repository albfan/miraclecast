[Unit]
Description=Miraclecast WiFi Daemon
After=dbus.service
Requires=network.target

[Service]
BusName=org.freedesktop.miracle.wifi
Environment=PATH=/sbin:/usr/bin
ExecStart=@CMAKE_INSTALL_PREFIX@/bin/miracle-wifid \
	--use-dev \
	--log-level trace \
	--lazy-managed
CapabilityBoundingSet=CAP_NET_ADMIN \
	CAP_NET_BIND_SERVICE \
	CAP_NET_RAW

[Install]
WantedBy=multi-user.target
Alias=dbus-org.freedesktop.miracle.wifi.service
