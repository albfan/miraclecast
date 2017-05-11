inc = include_directories('../..', '../ctl',)
gst1 = dependency('gstreamer-1.0')
gst1_base = dependency('gstreamer-base-1.0')
deps = [libsystemd, libmiracle_shared_dep, gst1, gst1_base]
if readline.found()
  deps += [readline]
endif

miracle_dispd_src = ['../ctl/ctl-cli.c',
  '../ctl/ctl-wifi.c',
  '../ctl/wfd.c',
  'dispd.c',
  'dispd-sink.c',
  'dispd-dbus.c',
  'dispd-session.c',
  'dispd-out-session.c',
  'dispd-arg.c',
  'dispd-encoder.c'
]
executable('miracle-dispd',
  miracle_dispd_src,
  install: true,
  include_directories: inc,
  dependencies: deps
)
