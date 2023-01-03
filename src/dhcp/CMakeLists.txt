set(miracle-dhcp_SRCS dhcp.c 
                      gdhcp.h 
                      unaligned.h 
                      common.h 
                      common.c 
                      ipv4ll.h 
                      ipv4ll.c 
                      client.c 
                      server.c)

add_executable(miracle-dhcp ${miracle-dhcp_SRCS})

find_package(PkgConfig)
pkg_check_modules (GLIB2 REQUIRED glib-2.0)
pkg_check_modules (UDEV REQUIRED libudev)
target_link_libraries(miracle-dhcp miracle-shared)
link_directories( ${UDEV_LIBRARY_DIRS})
include_directories( ${UDEV_INCLUDE_DIRS})
target_link_libraries(miracle-dhcp ${UDEV_LIBRARIES})
target_link_libraries(miracle-dhcp m)
link_directories( ${GLIB2_LIBRARY_DIRS})
include_directories( ${GLIB2_INCLUDE_DIRS})
target_link_libraries(miracle-dhcp ${GLIB2_LIBRARIES})


install(TARGETS miracle-dhcp DESTINATION bin)
INCLUDE_DIRECTORIES(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/shared)
