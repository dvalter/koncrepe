# SPDX-FileCopyrightText: 2026 Koncrepe Developers
# SPDX-License-Identifier: BSD-3-Clause
#
# Builds the shitty terminal engine's C embedding library (libshitty_vt.a)
# from the 3rdparty/shitty submodule and exposes it as the imported target
# Shitty::Vt.
#
# shitty has its own build runner and needs a C++26 clang, so it is driven
# as an external step rather than compiled by this project's toolchain. The
# facade it produces is plain C, so the archive links into the GCC- or
# clang-built Koncrepe libraries without any ABI coupling beyond the C++
# runtime both already share.

set(SHITTY_SOURCE_DIR "${CMAKE_SOURCE_DIR}/3rdparty/shitty" CACHE PATH "Path to the shitty source tree")
set(SHITTY_BUILD_DIR "${CMAKE_BINARY_DIR}/3rdparty/shitty")

if(NOT EXISTS "${SHITTY_SOURCE_DIR}/lib/embed/shitty_vt.h")
    message(FATAL_ERROR
        "The shitty terminal engine sources are missing from ${SHITTY_SOURCE_DIR}.\n"
        "Fetch the submodule with: git submodule update --init --recursive")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(SHITTY_CC NAMES clang clang-22 clang-21 clang-20 clang-19 DOC "C compiler used to build shitty")
find_program(SHITTY_CXX NAMES clang++ clang++-22 clang++-21 clang++-20 clang++-19 DOC "C++26 compiler used to build shitty")
find_program(SHITTY_RAGEL ragel DOC "Ragel state machine compiler (builds shitty's parser)")
if(NOT SHITTY_CC OR NOT SHITTY_CXX)
    message(FATAL_ERROR "Building the shitty terminal engine requires clang/clang++ with -std=c++26 support.")
endif()
if(NOT SHITTY_RAGEL)
    message(FATAL_ERROR "Building the shitty terminal engine requires Ragel 6 or 7.")
endif()

set(SHITTY_VT_ARCHIVE "${SHITTY_BUILD_DIR}/libshitty_vt.a")

# Rebuild whenever anything the engine is made of changes. shitty's own
# runner is content-addressed, so a spurious invocation is cheap.
file(GLOB_RECURSE SHITTY_VT_INPUTS CONFIGURE_DEPENDS
    "${SHITTY_SOURCE_DIR}/build"
    "${SHITTY_SOURCE_DIR}/build.py"
    "${SHITTY_SOURCE_DIR}/lib/vterm/*"
    "${SHITTY_SOURCE_DIR}/lib/embed/*"
    "${SHITTY_SOURCE_DIR}/ext/libstd/*.cpp"
    "${SHITTY_SOURCE_DIR}/ext/libstd/*.h"
    "${SHITTY_SOURCE_DIR}/ext/libstd/*.py"
    "${SHITTY_SOURCE_DIR}/ext/plt/*.cpp"
    "${SHITTY_SOURCE_DIR}/ext/plt/*.h"
    "${SHITTY_SOURCE_DIR}/ext/plt/*.py"
)

add_custom_command(
    OUTPUT "${SHITTY_VT_ARCHIVE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHITTY_BUILD_DIR}"
    COMMAND ${CMAKE_COMMAND} -E env "CC=${SHITTY_CC}" "CXX=${SHITTY_CXX}"
            "${Python3_EXECUTABLE}" "${SHITTY_SOURCE_DIR}/build" -B "${SHITTY_BUILD_DIR}" a
    WORKING_DIRECTORY "${SHITTY_SOURCE_DIR}"
    DEPENDS ${SHITTY_VT_INPUTS}
    COMMENT "Building the shitty terminal engine (libshitty_vt.a)"
    USES_TERMINAL
    VERBATIM
)
add_custom_target(shitty_vt_build DEPENDS "${SHITTY_VT_ARCHIVE}")

add_library(Shitty::Vt STATIC IMPORTED GLOBAL)
set_target_properties(Shitty::Vt PROPERTIES
    IMPORTED_LOCATION "${SHITTY_VT_ARCHIVE}"
    INTERFACE_INCLUDE_DIRECTORIES "${SHITTY_SOURCE_DIR}/lib/embed"
)
add_dependencies(Shitty::Vt shitty_vt_build)

# libstd, bundled into the archive, probes a few optional backends by
# header and leaves linking them to the importer. Mirror its probe.
include(CheckIncludeFileCXX)
set(SHITTY_VT_LINK_LIBRARIES Threads::Threads m)
find_package(Threads REQUIRED)
check_include_file_cxx("liburing.h" SHITTY_HAVE_LIBURING)
if(SHITTY_HAVE_LIBURING)
    list(APPEND SHITTY_VT_LINK_LIBRARIES uring)
endif()
check_include_file_cxx("rapidhash.h" SHITTY_HAVE_RAPIDHASH)
check_include_file_cxx("xxhash.h" SHITTY_HAVE_XXHASH)
if(NOT SHITTY_HAVE_RAPIDHASH AND SHITTY_HAVE_XXHASH)
    list(APPEND SHITTY_VT_LINK_LIBRARIES xxhash)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    list(APPEND SHITTY_VT_LINK_LIBRARIES atomic)
endif()
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SHITTY_SIMDUTF QUIET IMPORTED_TARGET "simdutf >= 6.5.0")
    if(SHITTY_SIMDUTF_FOUND)
        list(APPEND SHITTY_VT_LINK_LIBRARIES PkgConfig::SHITTY_SIMDUTF)
    endif()
endif()
set_property(TARGET Shitty::Vt PROPERTY INTERFACE_LINK_LIBRARIES ${SHITTY_VT_LINK_LIBRARIES})
# Keep the engine's internals (libstd, the VT core) out of the dynamic
# symbol table of whatever shared object links it: only shitty_vt_* is API.
set_property(TARGET Shitty::Vt APPEND PROPERTY INTERFACE_LINK_OPTIONS "LINKER:--exclude-libs,libshitty_vt.a")
