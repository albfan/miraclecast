set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake
                      ${CMAKE_MODULE_PATH})

set(CMAKE_C_FLAGS "-std=gnu11")
add_definitions(-D_GNU_SOURCE)

project(Miraclecast)
cmake_minimum_required(VERSION 3.0)

add_subdirectory(src)
add_subdirectory(res)
add_subdirectory(test)

SET(BUILD_BINDIR "${CMAKE_INSTALL_PREFIX}/bin")
OPTION(BUILD_ENABLE_DEBUG "Enable Debug" ON )
OPTION(BUILD_TESTS "Enable TEST" ON )

SET(PACKAGE_NAME miraclecast)
SET(PACKAGE_VERSION 1)
SET(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")

find_package(PkgConfig)
pkg_check_modules (GLIB2 REQUIRED glib-2.0)
pkg_check_modules (UDEV REQUIRED libudev)

CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)

