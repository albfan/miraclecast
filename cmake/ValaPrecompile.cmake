##
# Copyright 2009-2010 Jakob Westhoff. All rights reserved.
# Copyright 2012 elementary.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
#    1. Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
# 
#    2. Redistributions in binary form must reproduce the above copyright notice,
#       this list of conditions and the following disclaimer in the documentation
#       and/or other materials provided with the distribution.
# 
# THIS SOFTWARE IS PROVIDED BY JAKOB WESTHOFF ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
# EVENT SHALL JAKOB WESTHOFF OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# 
# The views and conclusions contained in the software and documentation are those
# of the authors and should not be interpreted as representing official policies,
# either expressed or implied, of Jakob Westhoff
##

include(ParseArguments)
find_package(Vala REQUIRED)

##
# Compile vala files to their c equivalents for further processing. 
#
# The "vala_precompile" macro takes care of calling the valac executable on the
# given source to produce c files which can then be processed further using
# default cmake functions.
# 
# The first parameter provided is a variable, which will be filled with a list
# of c files outputted by the vala compiler. This list can than be used in
# conjunction with functions like "add_executable" or others to create the
# necessary compile rules with CMake.
# 
# The initial variable is followed by a list of .vala files to be compiled.
# Please take care to add every vala file belonging to the currently compiled
# project or library as Vala will otherwise not be able to resolve all
# dependencies.
# 
# The following sections may be specified afterwards to provide certain options
# to the vala compiler:
#
# LIBRARY
#   Indicates that this is to be compiled as a library.
# 
# PACKAGES
#   A list of vala packages/libraries to be used during the compile cycle. The
#   package names are exactly the same, as they would be passed to the valac
#   "--pkg=" option.
# 
# OPTIONS
#   A list of optional options to be passed to the valac executable. This can be
#   used to pass "--thread" for example to enable multi-threading support.
#
# CUSTOM_VAPIS
#   A list of custom vapi files to be included for compilation. This can be
#   useful to include freshly created vala libraries without having to install
#   them in the system.
#
# DEPENDS
#   Additional files that may change the results of the outputed C files.
#
# GENERATE_VAPI [INTERNAL]
#   Pass all the needed flags to the compiler to create a vapi for
#   the compiled library. The provided name will be used for this and a
#   <provided_name>.vapi file will be created. If INTERNAL is specified,
#   an internal vapi <provided_name>_internal.vapi will be created as well.
#   This option implies GENERATE_HEADER, so there is not need use GENERATE_HEADER
#   in addition to GENERATE_VAPI unless they require different names. Requires
#   that LIBRARY is set.
#
# GENERATE_HEADER [INTERNAL]
#   Let the compiler generate a header file for the compiled code. There will
#   be a header file being generated called <provided_name>.h. If INTERNAL
#   is specified, an internal header <provided_name>_internal.h will be created
#   as well.
#
# GENERATE_GIR [TYPELIB]
#   Have the compiler generate a GObject-Introspection repository file with
#   name: <provided_name>.gir. If TYPELIB is specified, the compiler will also
#   create a binary typelib using the GI compiler. Requires that LIBRARY is set.
#
# TYPELIB_OPTIONS
#   Additional options to pass to the GI compiler. Requires that GENERATE_GIR
#   TYPELIB is set.
#
# GENERATE_SYMBOLS
#   Output a <provided_name>.symbols file containing all the exported symbols.
# 
# The following call is a simple example to the vala_precompile macro showing
# an example to every of the optional sections:
#
#   vala_precompile(VALA_C mytargetname
#   LIBRARY
#       source1.vala
#       source2.vala
#       source3.vala
#   PACKAGES
#       gtk+-2.0
#       gio-1.0
#       posix
#   DIRECTORY
#       gen
#   OPTIONS
#       --thread
#   CUSTOM_VAPIS
#       some_vapi.vapi
#   GENERATE_VAPI
#       myvapi
#   GENERATE_HEADER
#       myheader
#   GENERATE_GIR TYPELIB
#       mygir
#   TYPELIB_OPTIONS
#       --includedir=some/dir
#   GENERATE_SYMBOLS
#       mysymbols
#   )
#
# Most important is the variable VALA_C which will contain all the generated c
# file names after the call.
##

macro(vala_precompile output target_name)
    parse_arguments(ARGS
        "TARGET;PACKAGES;OPTIONS;TYPELIB_OPTIONS;DIRECTORY;GENERATE_GIR;GENERATE_SYMBOLS;GENERATE_HEADER;GENERATE_VAPI;CUSTOM_VAPIS;DEPENDS"
        "LIBRARY" ${ARGN})

    if(ARGS_DIRECTORY)
        set(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${ARGS_DIRECTORY})
    else(ARGS_DIRECTORY)
        set(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    endif(ARGS_DIRECTORY)
    include_directories(${DIRECTORY})
    set(vala_pkg_opts "")
    foreach(pkg ${ARGS_PACKAGES})
        list(APPEND vala_pkg_opts "--pkg=${pkg}")
    endforeach(pkg ${ARGS_PACKAGES})
    set(in_files "")
    set(out_files "")
    set(out_files_display "")
    set(${output} "")

    foreach(src ${ARGS_DEFAULT_ARGS})
        # this string(REPLACE ...) is a workaround for a strange behavior when
        # the cmake binary directory is a subdirectory of the source directory
        # and you include a vala source file from the cmake binary directory.
        # For a yet to be determined reason, cmake deletes the generated c file
        # before it is compiled, resulting in an error. We fix this by making
        # any absolute path that is in the source directory a relative path.
        string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/" "" src ${src})

        string(REGEX MATCH "^/" IS_MATCHED ${src})
        if(${IS_MATCHED} MATCHES "/")
            set(src_file_path ${src})
        else()
            set(src_file_path ${CMAKE_CURRENT_SOURCE_DIR}/${src})
        endif()
        list(APPEND in_files ${src_file_path})
        string(REPLACE ".vala" ".c" src ${src})
        string(REPLACE ".gs" ".c" src ${src})
        if(${IS_MATCHED} MATCHES "/")
            get_filename_component(VALA_FILE_NAME ${src} NAME)
            set(out_file "${CMAKE_CURRENT_BINARY_DIR}/${VALA_FILE_NAME}")
            list(APPEND out_files "${CMAKE_CURRENT_BINARY_DIR}/${VALA_FILE_NAME}")
        else()
            set(out_file "${DIRECTORY}/${src}")
            list(APPEND out_files "${DIRECTORY}/${src}")
        endif()
        list(APPEND ${output} ${out_file})
        list(APPEND out_files_display "${src}")
    endforeach(src ${ARGS_DEFAULT_ARGS})

    set(custom_vapi_arguments "")
    if(ARGS_CUSTOM_VAPIS)
        foreach(vapi ${ARGS_CUSTOM_VAPIS})
            if(${vapi} MATCHES ${CMAKE_SOURCE_DIR} OR ${vapi} MATCHES ${CMAKE_BINARY_DIR})
                list(APPEND custom_vapi_arguments ${vapi})
            else (${vapi} MATCHES ${CMAKE_SOURCE_DIR} OR ${vapi} MATCHES ${CMAKE_BINARY_DIR})
                list(APPEND custom_vapi_arguments ${CMAKE_CURRENT_SOURCE_DIR}/${vapi})
            endif(${vapi} MATCHES ${CMAKE_SOURCE_DIR} OR ${vapi} MATCHES ${CMAKE_BINARY_DIR})
        endforeach(vapi ${ARGS_CUSTOM_VAPIS})
    endif(ARGS_CUSTOM_VAPIS)

    set(library_arguments "")
    if(ARGS_LIBRARY)
        list(APPEND library_arguments "--library=${target_name}")
    endif(ARGS_LIBRARY)

    set(vapi_arguments "")
    if(ARGS_GENERATE_VAPI)
        parse_arguments(ARGS_GENERATE_VAPI "" "INTERNAL" ${ARGS_GENERATE_VAPI})
        list(APPEND out_files "${DIRECTORY}/${ARGS_GENERATE_VAPI_DEFAULT_ARGS}.vapi")
        list(APPEND out_files_display "${ARGS_GENERATE_VAPI_DEFAULT_ARGS}.vapi")
        list(APPEND vapi_arguments "--vapi=${ARGS_GENERATE_VAPI_DEFAULT_ARGS}.vapi")
        list(APPEND vapi_arguments "--vapi-comments")

        # Header and internal header is needed to generate internal vapi
        if (NOT ARGS_GENERATE_HEADER)
            set(ARGS_GENERATE_HEADER ${ARGS_GENERATE_VAPI_DEFAULT_ARGS})
        endif(NOT ARGS_GENERATE_HEADER)

        if(ARGS_GENERATE_VAPI_INTERNAL)
            list(APPEND out_files "${DIRECTORY}/${ARGS_GENERATE_VAPI_DEFAULT_ARGS}_internal.vapi")
            list(APPEND out_files_display "${ARGS_GENERATE_VAPI_DEFAULT_ARGS}_internal.vapi")
            list(APPEND vapi_arguments "--internal-vapi=${ARGS_GENERATE_VAPI_DEFAULT_ARGS}_internal.vapi")
            list(APPEND ARGS_GENERATE_HEADER "INTERNAL")
        endif(ARGS_GENERATE_VAPI_INTERNAL)
    endif(ARGS_GENERATE_VAPI)

    set(header_arguments "")
    if(ARGS_GENERATE_HEADER)
        parse_arguments(ARGS_GENERATE_HEADER "" "INTERNAL" ${ARGS_GENERATE_HEADER})
        list(APPEND out_files "${DIRECTORY}/${ARGS_GENERATE_HEADER_DEFAULT_ARGS}.h")
        list(APPEND out_files_display "${ARGS_GENERATE_HEADER_DEFAULT_ARGS}.h")
        list(APPEND header_arguments "--header=${ARGS_GENERATE_HEADER_DEFAULT_ARGS}.h")
        if(ARGS_GENERATE_HEADER_INTERNAL)
        list(APPEND out_files "${DIRECTORY}/${ARGS_GENERATE_HEADER_DEFAULT_ARGS}_internal.h")
        list(APPEND out_files_display "${ARGS_GENERATE_HEADER_DEFAULT_ARGS}_internal.h")
            list(APPEND header_arguments "--internal-header=${ARGS_GENERATE_HEADER_DEFAULT_ARGS}_internal.h")
        endif(ARGS_GENERATE_HEADER_INTERNAL)
    endif(ARGS_GENERATE_HEADER)

    set(gir_arguments "")
    set(gircomp_command "")
    if(ARGS_GENERATE_GIR)
        parse_arguments(ARGS_GENERATE_GIR "" "TYPELIB" ${ARGS_GENERATE_GIR})
        list(APPEND out_files "${DIRECTORY}/${ARGS_GENERATE_GIR_DEFAULT_ARGS}.gir")
        list(APPEND out_files_display "${ARGS_GENERATE_GIR_DEFAULT_ARGS}.gir")
        list(APPEND gir_arguments "--gir=${ARGS_GENERATE_GIR_DEFAULT_ARGS}.gir")

        if(ARGS_GENERATE_GIR_TYPELIB)
            include (FindGirCompiler)
            find_package(GirCompiler REQUIRED)

            add_custom_command(
                OUTPUT
                    "${DIRECTORY}/${ARGS_GENERATE_GIR_DEFAULT_ARGS}.typelib"
                COMMAND
                    ${G_IR_COMPILER_EXECUTABLE}
                ARGS
                    "${DIRECTORY}/${ARGS_GENERATE_GIR_DEFAULT_ARGS}.gir"
                    "--shared-library=$<TARGET_SONAME_FILE_NAME:${target_name}>"
                    "--output=${DIRECTORY}/${ARGS_GENERATE_GIR_DEFAULT_ARGS}.typelib"
                    ${ARGS_TYPELIB_OPTIONS}
                DEPENDS
                    "${DIRECTORY}/${ARGS_GENERATE_GIR_DEFAULT_ARGS}.gir"
                COMMENT
                    "Genterating typelib.")

            add_custom_target("${target_name}-typelib"
                ALL
                DEPENDS
                    "${DIRECTORY}/${ARGS_GENERATE_GIR_DEFAULT_ARGS}.typelib")
        endif(ARGS_GENERATE_GIR_TYPELIB)
    endif(ARGS_GENERATE_GIR)

    set(symbols_arguments "")
    if(ARGS_GENERATE_SYMBOLS)
        list(APPEND out_files "${DIRECTORY}/${ARGS_GENERATE_SYMBOLS}.symbols")
        list(APPEND out_files_display "${ARGS_GENERATE_SYMBOLS}.symbols")
        set(symbols_arguments "--symbols=${ARGS_GENERATE_SYMBOLS}.symbols")
    endif(ARGS_GENERATE_SYMBOLS)

    # Workaround for a bug that would make valac run twice. This file is written
    # after the vala compiler generates C source code.
    set(OUTPUT_STAMP ${CMAKE_CURRENT_BINARY_DIR}/${target_name}_valac.stamp)

    add_custom_command(
    OUTPUT
        ${OUTPUT_STAMP}
    COMMAND 
        ${VALA_EXECUTABLE} 
    ARGS 
        "-C" 
        ${header_arguments} 
        ${library_arguments}
        ${vapi_arguments} 
        ${gir_arguments} 
        ${symbols_arguments} 
        "-b" ${CMAKE_CURRENT_SOURCE_DIR} 
        "-d" ${DIRECTORY} 
        ${vala_pkg_opts} 
        ${ARGS_OPTIONS} 
        "--debug"
        ${in_files} 
        ${custom_vapi_arguments}
    COMMAND
        touch
    ARGS
        ${OUTPUT_STAMP}
    DEPENDS 
        ${in_files} 
        ${ARGS_CUSTOM_VAPIS}
        ${ARGS_DEPENDS}
    COMMENT
        "Generating ${out_files_display}"
    )

    # This command will be run twice for some reason (pass a non-empty string to COMMENT
    # in order to see it). Since valac is not executed from here, this won't be a problem.
    add_custom_command(OUTPUT ${out_files} DEPENDS ${OUTPUT_STAMP} COMMENT "")
endmacro(vala_precompile)
