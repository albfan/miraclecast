set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake
                      ${CMAKE_MODULE_PATH})
set(CMAKE_C_FLAGS "-std=gnu11") 

add_definitions(-DBUILD_ENABLE_DEBUG)
add_definitions(-DBUILD_BINDIR="$(bindir)")

#set(CMAKE_CPP_FLAGS "-include $(top_builddir)/config.h \
	#-I $(top_srcdir)/src \
	#-I $(top_srcdir)/src/shared \
FILE(GLOB _ALL_FILES $(top_srcdir)/src $(top_srcdir)/src/shared)
FOREACH(_FILE ${_ALL_FILES})
   IF(IS_DIRECTORY ${_FILE})
      INCLUDE_DIRECTORIES(${_FILE})
   ENDIF()
ENDFOREACH()


project(Miraclecast)
cmake_minimum_required(VERSION 3.0)

add_subdirectory(src)
add_subdirectory(res)
add_subdirectory(test)

OPTION( WITH_FOO "Enable FOO support" ON )
OPTION( WITH_BAR "Enable BAR component" OFF )
SET( BAZ 18 )
SET(PACKAGE_NAME miraclecast)
SET(PACKAGE_VERSION 1)
SET(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
CONFIGURE_FILE( ${CMAKE_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h )
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})


########### install files ###############




#original Makefile.am contents follow:

#SUBDIRS = src res test
#EXTRA_DIST = README.md \
#	COPYING \
#	NEWS
#
#ACLOCAL_AMFLAGS = -I m4
