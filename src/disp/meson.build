inc = include_directories('../..', '../ctl',)
gst1 = dependency('gstreamer-1.0')
gst1_base = dependency('gstreamer-base-1.0')
deps = [libsystemd, libmiracle_shared_dep, gst1, gst1_base]
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
