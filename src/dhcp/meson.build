miracle_dhcp_srcs = ['dhcp.c',
  'common.c',
  'ipv4ll.c',
  'client.c',
  'server.c'
]
executable('miracle-dhcp', miracle_dhcp_srcs,
  install: true,
  include_directories: include_directories('../..'),
  dependencies: [glib2, udev, libmiracle_shared_dep]
)
