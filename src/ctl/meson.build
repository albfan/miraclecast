inc = include_directories('../..')
deps = [libsystemd, libmiracle_shared_dep, glib2]
if readline.found()
  deps += readline
endif

miracle_wifictl_srcs = ['ctl-cli.c', 'ctl-wifi.c', 'wifictl.c']
executable('miracle-wifictl', miracle_wifictl_srcs,
  install: true,
  include_directories: inc,
  dependencies: deps
)

miracle_sinkctl_srcs = ['ctl-cli.c',
  'ctl-sink.c',
  'ctl-wifi.c',
  'sinkctl.c',
  'wfd.c'
]
executable('miracle-sinkctl', miracle_sinkctl_srcs,
  install: true,
  include_directories: inc,
  dependencies: deps
)
