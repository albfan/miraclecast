find_package(PkgConfig)
pkg_check_modules (CHECK check)
    
if(CHECK_FOUND)
    set(test_rtsp_SOURCES test_common.h test_rtsp.c)
    add_executable(test_rtsp ${test_rtsp_SOURCES})
    target_link_libraries(test_rtsp miracle-shared)
    target_link_libraries(test_rtsp ${UDEV_LIBRARIES})
    target_link_libraries(test_rtsp ${GLIB2_LIBRARIES})
    target_link_libraries(test_rtsp ${CHECK_LIBRARIES})
    target_link_libraries(test_rtsp ${CHECK_CFLAGS})
    
    set(test_wpas_SOURCES test_common.h test_wpas.c)
    add_executable(test_wpas ${test_wpas_SOURCES})
    target_link_libraries(test_wpas miracle-shared)
    target_link_libraries(test_wpas ${UDEV_LIBRARIES})
    target_link_libraries(test_wpas ${GLIB2_LIBRARIES})
    target_link_libraries(test_wpas ${CHECK_LIBRARIES})
    target_link_libraries(test_wpas ${CHECK_CFLAGS})
    target_link_libraries(test_wpas m)

    set(test_valgrind_SOURCES test_common.h test_valgrind.c)
    add_executable(test_valgrind ${test_valgrind_SOURCES})
    target_link_libraries(test_valgrind miracle-shared)
    target_link_libraries(test_valgrind ${UDEV_LIBRARIES})
    target_link_libraries(test_valgrind ${GLIB2_LIBRARIES})
    target_link_libraries(test_valgrind ${CHECK_LIBRARIES})
    target_link_libraries(test_valgrind ${CHECK_CFLAGS})

    INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/src/shared)

    set(VALGRIND CK_FORK=no valgrind --tool=memcheck --leak-check=yes --show-reachable=yes --leak-resolution=high --error-exitcode=1 --suppressions=${CMAKE_SOURCE_DIR}/test.supp)

    add_custom_target(memcheck-verify
                    DEPENDS test_rtsp test_wpas test_valgrind
                    COMMAND ${VALGRIND} --log-file=/dev/null ./test_valgrind >/dev/null |
                            test 1 = $$?
                    COMMENT "verify memcheck")

    add_custom_target(memcheck
                    DEPENDS memcheck-verify
                    COMMAND for i in $$(MEMTESTS) |
                            do |
                            ${VALGRIND} --log-file=${CMAKE_SOURCE_DIR}/$$i.memlog |
                            	${CMAKE_SOURCE_DIR}/$$i >/dev/null || (echo "memcheck failed on: $$i" ; exit 1) ; |
                            done
                    SOURCES test_rtsp test_valgrind test_wpas
                    COMMENT "verify memcheck")

endif(CHECK_FOUND)
