check = dependency('check', required: false)
deps = [udev, glib2, check, libsystemd, libmiracle_shared_dep]

if check.found()
  test_rtsp = executable('test_rtsp', 'test_rtsp.c', dependencies: deps)

  test_wpas = executable('test_wpas', 'test_wpas.c', dependencies: deps)

  test_valgrind = executable('test_valgrind',
    'test_valgrind.c',
    dependencies: deps
  )

  test('rtsp test', test_rtsp)
  test('wpas test', test_wpas)
  test('valgrind test', test_valgrind)

#  set(VALGRIND CK_FORK=no valgrind --tool=memcheck --leak-check=yes --show-reachable=yes --leak-resolution=high --error-exitcode=1 --suppressions=${CMAKE_SOURCE_DIR}/test.supp)
#
#  add_custom_target(memcheck-verify
#    DEPENDS test_rtsp test_wpas test_valgrind
#    COMMAND ${VALGRIND} --log-file=/dev/null ./test_valgrind >/dev/null |
#      test 1 = $$?
#    COMMENT "verify memcheck")
#
#  add_custom_target(memcheck
#    DEPENDS memcheck-verify
#    COMMAND for i in $(MEMTESTS) |
#      do |
#      ${VALGRIND} --log-file=${CMAKE_SOURCE_DIR}/$$i.memlog |
#        ${CMAKE_SOURCE_DIR}/$$i >/dev/null || (echo "memcheck failed on: $$i" ; exit 1) ; |
#      done
#    SOURCES test_rtsp test_valgrind test_wpas
#    COMMENT "verify memcheck")
endif
