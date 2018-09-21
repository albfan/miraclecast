cmake_minimum_required(VERSION 2.8)
project(Miraclecast)

SET(PACKAGE_NAME miraclecast)
SET(PACKAGE_VERSION 1)
SET(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake
                      ${CMAKE_MODULE_PATH})

set(CMAKE_C_FLAGS "-std=gnu11 ${CMAKE_C_FLAGS}")
add_definitions(-D_GNU_SOURCE)

SET(BUILD_BINDIR "${CMAKE_INSTALL_PREFIX}/bin")
OPTION(BUILD_ENABLE_DEBUG "Enable Debug" ON )
OPTION(RELY_UDEV "Rely in udev tag to select device" OFF )
OPTION(BUILD_TESTS "Enable TEST" ON )

if(BUILD_ENABLE_DEBUG)
    add_definitions(-DBUILD_ENABLE_DEBUG)
endif()

set(SYSCONFDIR "/etc" CACHE STRING "system config dir")
set(DATADIR "${CMAKE_INSTALL_PREFIX}/share" CACHE STRING "shared data dir")

find_package(PkgConfig)
pkg_check_modules (GLIB2 REQUIRED glib-2.0)
pkg_check_modules (UDEV REQUIRED libudev)
pkg_check_modules (SYSTEMD REQUIRED libsystemd)

CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/config.h.cmake ${CMAKE_BINARY_DIR}/config.h)

add_subdirectory(src)
add_subdirectory(res)
add_subdirectory(test)

