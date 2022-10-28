set(miracle-wifid_SRCS wifid.h 
                       wifid.c 
                       wifid-dbus.c 
                       wifid-link.c 
                       wifid-peer.c 
                       wifid-supplicant.c)

add_executable(miracle-wifid ${miracle-wifid_SRCS})

target_link_libraries(miracle-wifid ${KDE4_KDECORE_LIBS})

cmake_policy(SET CMP0015 NEW)
include_directories(shared)
link_directories(shared)
target_link_libraries(miracle-wifid miracle-shared)

find_package(PkgConfig)
pkg_check_modules (GLIB2 REQUIRED glib-2.0)
pkg_check_modules (UDEV REQUIRED libudev)
target_link_libraries(miracle-wifid ${UDEV_LIBRARIES})
target_link_libraries(miracle-wifid m)
link_directories( ${GLIB2_LIBRARY_DIRS})
include_directories( ${GLIB2_INCLUDE_DIRS})
target_link_libraries(miracle-wifid ${GLIB2_LIBRARIES})

install(TARGETS miracle-wifid DESTINATION bin)
INCLUDE_DIRECTORIES(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/shared)
