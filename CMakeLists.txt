project(Miraclecast)
set(CMAKE_C_FLAGS "-std=c99") 
cmake_minimum_required(VERSION 3.0)

add_subdirectory(src)
add_subdirectory(res)
add_subdirectory(test)


########### install files ###############




#original Makefile.am contents follow:

#SUBDIRS = src res test
#EXTRA_DIST = README.md \
#	COPYING \
#	NEWS
#
#ACLOCAL_AMFLAGS = -I m4
