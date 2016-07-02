cmake_minimum_required(VERSION 3.0)
project(Miraclecast)

SET(PACKAGE_NAME miraclecast)
SET(PACKAGE_VERSION 1)
SET(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake
                      ${CMAKE_MODULE_PATH})

set(CMAKE_C_FLAGS "-std=gnu11 ${CMAKE_C_FLAGS}")
add_definitions(-D_GNU_SOURCE)

OPTION(BUILD_ENABLE_DEBUG "Enable Debug" ON )
OPTION(RELY_UDEV "Rely in udev tag to select device" OFF )
OPTION(BUILD_TESTS "Enable TEST" ON )

find_package(PkgConfig)
pkg_check_modules (GLIB2 REQUIRED glib-2.0)
pkg_check_modules (UDEV REQUIRED libudev)

CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/config.h.cmake ${CMAKE_BINARY_DIR}/config.h)

add_subdirectory(src)
add_subdirectory(res)
add_subdirectory(test)

