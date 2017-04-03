meson.add_install_script('meson-install-scripts.sh', get_option('bindir'))

install_data(
  'org.freedesktop.miracle.conf',
  install_dir: join_paths('/', get_option('sysconfdir'), 'dbus-1', 'system.d')
)
