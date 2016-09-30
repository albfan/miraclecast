dbus1 = dependency('dbus-1')
systemd = dependency('systemd')

dbus_system_services_dir = dbus1.get_pkgconfig_variable('system_bus_services_dir')
systemd_system_unit_dir = systemd.get_pkgconfig_variable('systemdsystemunitdir')

conf_data = configuration_data()
dbus_name_prefix = 'org.freedesktop.miracle'
conf_data.set('dbus_name_prefix', dbus_name_prefix)
conf_data.set('exec_path',
  join_paths(get_option('prefix'), get_option('bindir'))
)

configure_file(input: 'miracle-wifid.service.in',
  output: 'miracle-wifid.service',
  configuration: conf_data,
  install_dir: systemd_system_unit_dir
)
configure_file(input: 'miracle-dispd.service.in',
  output: 'miracle-dispd.service',
  configuration: conf_data,
  install_dir: systemd_system_unit_dir
)
# create symlinks for DBus activiated services
meson.add_install_script('create-symlinks')

configure_file(input: 'org.freedesktop.miracle.wifi.service.in',
  output: 'org.freedesktop.miracle.wifi.service',
  configuration: conf_data,
  install_dir: dbus_system_services_dir
)
configure_file(input: 'org.freedesktop.miracle.wfd.service.in',
  output: 'org.freedesktop.miracle.wfd.service',
  configuration: conf_data,
  install_dir: dbus_system_services_dir
)
install_data(
  'org.freedesktop.miracle.conf',
  install_dir: join_paths(get_option('sysconfdir'), 'dbus-1', 'system.d')
)

install_data('miracle-gst', 'gstplayer', 'uibc-viewer',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')

gio2 = dependency('gio-2.0')
gdk3 = dependency('gdk-3.0')
gst1 = dependency('gstreamer-1.0')
gst1_base = dependency('gstreamer-base-1.0')

add_languages('vala')
valac = meson.get_compiler('vala')
valac_extra_args = []
if valac.version().version_compare('>=0.34') and gdk3.version().version_compare('>=3.22')
  valac_extra_args += ['-D', 'GDK3_HAS_MONITOR_CLASS']
endif

executable('gstencoder', 'gstencoder.vala',
  dependencies: [gst1, gst1_base, gio2],
  install: true,
  vala_args: ['--pkg=posix'])

miracle_dispctl_src = ['dispctl.vala',
  'sigint.vapi',
  'sigint.c',
  'networkmanager.vala',
  'miracle-wifi.vala',
  'miracle-wfd.vala'
]
executable('miracle-dispctl', miracle_dispctl_src,
  dependencies: [gio2, gdk3],
  vala_args: valac_extra_args,
  install: true
)

install_data(
  'miracle-wifid', 'miracle-sinkctl', 'miracle-wifictl',
  install_dir: join_paths(get_option('datadir'), 'bash-completions', 'completions')
)
