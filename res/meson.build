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
  install_dir: join_paths('/', get_option('sysconfdir'), 'dbus-1', 'system.d')
)

install_data('miracle-gst', 'gstplayer', 'uibc-viewer',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
