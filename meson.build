project('Miraclecast',
  'c',
  version: '1',
  meson_version: '>=0.39',
  default_options: ['buildtype=debugoptimized', 'c_std=gnu11']
)

add_project_arguments('-D_GNU_SOURCE', language: 'c')

conf_data = configuration_data()
conf_data.set_quoted('BUILD_BINDIR',
  join_paths(get_option('prefix'), get_option('bindir'))
)
conf_data.set_quoted('PACKAGE_STRING',
  '@0@ @1@'.format(meson.project_name(), meson.project_version())
)
configure_file(output: 'config.h',
  configuration: conf_data
)

c_compiler = meson.get_compiler('c')
readline = c_compiler.find_library('readline', required: false)
if readline.found()
  add_project_arguments('-DHAVE_READLINE', language: 'c')
endif

if get_option('build-enable-debug')
  add_project_arguments('-DBUILD_ENABLE_DEBUG', language: 'c')
endif

if get_option('rely-udev')
  add_project_arguments('-DRELY_UDEV', language: 'c')
endif

glib2 = dependency('glib-2.0')
udev = dependency('libudev')
libsystemd = dependency('libsystemd')

subdir('src')
subdir('res')

if get_option('build-tests')
  subdir('test')
endif

