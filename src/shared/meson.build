libmiracle_shared = static_library('miracle-shared',
  'rtsp.h',
  'rtsp.c',
  'shl_dlist.h',
  'shl_htable.h',
  'shl_htable.c',
  'shl_log.h',
  'shl_log.c',
  'shl_macro.h',
  'shl_ring.h',
  'shl_ring.c',
  'shl_util.h',
  'shl_util.c',
  'util.h',
  'wpas.h',
  'wpas.c',
  dependencies: [libsystemd]
)
libmiracle_shared_dep = declare_dependency(
  include_directories: include_directories('.'),
  link_with: libmiracle_shared
)
