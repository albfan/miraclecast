gstreamer = dependency('gstreamer-1.0')
gstreamer_base = dependency('gstreamer-base-1.0')

inc = include_directories('../..', '../ctl',)
deps = [libsystemd, gstreamer, gstreamer_base, libmiracle_shared_dep]
if readline.found()
  deps += [readline]
endif

miracle_dispd_src = ['../ctl/ctl-cli.c',
  '../ctl/ctl-wifi.c',
  'wfd-sink.c',
  'wfd-dbus.c',
  'wfd-session.c',
  'wfd-out-session.c',
  'dispd.c',
  '../ctl/wfd.c',
  'wfd-arg.c'
]
executable('miracle-dispd',
  miracle_dispd_src,
  install: true,
  include_directories: inc,
  dependencies: deps
)
