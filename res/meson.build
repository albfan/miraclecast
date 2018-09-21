install_data(
  'org.freedesktop.miracle.conf',
  install_dir: join_paths(get_option('sysconfdir'), 'dbus-1', 'system.d')
)

install_data('miracle-gst', 'gstplayer', 'uibc-viewer',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')

install_data(
  'miracle-wifid', 'miracle-sinkctl', 'miracle-wifictl',
  install_dir: join_paths(get_option('datadir'), 'bash-completions', 'completions')
)
