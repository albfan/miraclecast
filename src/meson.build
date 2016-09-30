inc_shared = include_directories('shared')

subdir('shared')
subdir('wifi')
subdir('dhcp')
subdir('ctl')
subdir('uibc')
subdir('disp')

executable('miracled', 'miracled.c',
  dependencies: libmiracle_shared_dep,
  include_directories: include_directories('..'),
  install: true
)
