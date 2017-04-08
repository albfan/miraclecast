inc = include_directories('../..')
miracle_wifid_src = ['wifid.h',
  'wifid.c',
  'wifid-dbus.c',
  'wifid-link.c',
  'wifid-peer.c',
  'wifid-supplicant.c'
]
executable('miracle-wifid', miracle_wifid_src,
  include_directories: inc,
  install: true,
  dependencies: [udev, glib2, libsystemd, libmiracle_shared_dep]
)

