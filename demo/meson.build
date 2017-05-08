add_languages('vala')

gio2 = dependency('gio-2.0')
gdk3 = dependency('gdk-3.0')

valac = meson.get_compiler('vala')
valac_extra_args = []
if valac.version().version_compare('>=0.34') and gdk3.version().version_compare('>=3.22')
  valac_extra_args += ['-D', 'GDK3_HAS_MONITOR_CLASS']
endif

miracle_wfdctl_src = ['wfdctl.vala',
  'sigint.vapi',
  'sigint.c',
  'networkmanager.vala',
  'miracle-wifi.vala',
  'miracle-wfd.vala'
]

executable('miracle-wfdctl', miracle_wfdctl_src,
  dependencies: [gio2, gdk3],
  vala_args: valac_extra_args,
  install: true
)
