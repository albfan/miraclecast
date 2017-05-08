cmake_minimum_required(VERSION 3.0.2)

project(Miraclecast)

SET(PACKAGE_NAME miraclecast)
SET(PACKAGE_VERSION 1)
SET(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

SET(BUILD_BINDIR "${CMAKE_INSTALL_PREFIX}/bin")
OPTION(BUILD_ENABLE_DEBUG "Enable Debug" ON )
OPTION(RELY_UDEV "Rely in udev tag to select device" OFF )
OPTION(BUILD_TESTS "Enable TEST" ON )

if(BUILD_ENABLE_DEBUG)
    add_definitions(-DBUILD_ENABLE_DEBUG)
endif()

find_package(PkgConfig)
pkg_check_modules (GLIB2 REQUIRED glib-2.0)
pkg_check_modules (UDEV REQUIRED libudev)
pkg_check_modules (SYSTEMD REQUIRED libsystemd)
pkg_check_modules (GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules (GSTREAMER_BASE REQUIRED gstreamer-base-1.0)

include(CheckCCompilerFlag)
check_c_compiler_flag(-fstack-protector-strong HAS_STACK_PROTCTOR_STRONG)
check_c_compiler_flag(-fsanitize=undefined HAS_SANITIZE_UNDEFINED)
if(HAS_STACK_PROTCTOR_STRONG)
	set(CMAKE_C_FLAGS "-fstack-protector-strong ${CMAKE_C_FLAGS}")
endif()
if(HAS_SANITIZE_UNDEFINED)
	set(CMAKE_C_FLAGS "-fsanitize=undefined ${CMAKE_C_FLAGS}")
endif()
set(CMAKE_C_FLAGS "-std=gnu11 -Wall ${CMAKE_C_FLAGS}")
add_definitions(-D_GNU_SOURCE)

if(CMAKE_COMPILER_IS_GNUCC)
	execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion
					OUTPUT_VARIABLE GCC_VERSION)
	if(GCC_VERSION VERSION_LESS 4.9)
		message(FATAL_ERROR "gcc >= 4.9 is requred")
	endif()
endif()

CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/config.h.cmake ${CMAKE_BINARY_DIR}/config.h)

add_subdirectory(src)
add_subdirectory(res)
add_subdirectory(test)

