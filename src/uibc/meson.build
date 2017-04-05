m = c_compiler.find_library('m', required: false)
executable('miracle-uibcctl', 'miracle-uibcctl.h', 'miracle-uibcctl.c',
  install: true,
  dependencies: [m, libmiracle_shared_dep]
)
