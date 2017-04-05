project('Miraclecast',
  'c',
  version: '1',
  meson_version: '>=0.39',
  default_options: ['buildtype=debugoptimized',
    'c_std=gnu11',
    'b_lundef=true',
    'b_sanitize=address']
)

glib2 = dependency('glib-2.0')
udev = dependency('libudev')
libsystemd = dependency('libsystemd')
gstreamer = dependency('gstreamer-1.0')
gstreamer_base = dependency('gstreamer-base-1.0')

if get_option('build-enable-debug')
  add_project_arguments('-DBUILD_ENABLE_DEBUG', language: 'c')
endif

c_compiler = meson.get_compiler('c')
if c_compiler.has_argument('-std=gnu11')
  add_project_arguments('-std=gnu11', language: 'c')
endif

readline = c_compiler.find_library('readline', required: false)
if readline.found()
  add_project_arguments('-DHAVE_READLINE', language: 'c')
endif

add_project_arguments('-D_GNU_SOURCE', language: 'c')

r = c_compiler.compiles('''int main() { return _Generic(0, int: 0); }''',
  name: 'C11 generic selection')
if false == r
  error('C11 generic selection is required')
endif

subdir('src')
subdir('res')
subdir('test')

if get_option('build-demo')
  subdir('demo')
endif

conf_data = configuration_data()
conf_data.set('BUILD_BINDIR',
  '"' + join_paths(get_option('prefix'), get_option('bindir')) + '"'
)
conf_data.set('PACKAGE_STRING',
  '"@0@ @1@"'.format(meson.project_name(),
meson.project_version()))
configure_file(output: 'config.h',
  configuration: conf_data
)
