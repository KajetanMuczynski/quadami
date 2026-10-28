# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
#
# Qt-free build of the remeshing core and its command line, autoremesher-cli
# (the Blender add-on's worker). The Qt app is still built with qmake
# (autoremesher.pro).
#
#   cmake -S . -B build/cli -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/cli --config Release --parallel 2
#
# Linux and macOS link the installed TBB (libtbb-dev, brew install tbb).
# Windows builds oneTBB from source as a static library and links the C++
# runtime statically, so autoremesher-cli.exe needs only DLLs that ship with
# Windows. The macOS add-on zip does the same for oneTBB
# (-DAUTOREMESHER_BUNDLED_TBB=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=11.2).
cmake_minimum_required(VERSION 3.16)

if(WIN32)
    set(AUTOREMESHER_DEFAULT_BUNDLED_TBB ON)
else()
    set(AUTOREMESHER_DEFAULT_BUNDLED_TBB OFF)
endif()
option(AUTOREMESHER_BUNDLED_TBB "Build oneTBB from source and link it statically" ${AUTOREMESHER_DEFAULT_BUNDLED_TBB})
option(AUTOREMESHER_BUILD_TESTS "Build the core tests in tests/ (run with ctest)" OFF)
option(AUTOREMESHER_STATIC_RUNTIME "Link the MSVC C++ runtime statically (/MT)" ON)

# Before project(): the runtime choice must reach oneTBB's targets too.
if(AUTOREMESHER_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
endif()

project(autoremesher VERSION 1.2.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

if(AUTOREMESHER_BUNDLED_TBB)
    include(FetchContent)
    # oneTBB v2023.1.0 (Apache-2.0), pinned by commit.
    FetchContent_Declare(onetbb
        GIT_REPOSITORY https://github.com/uxlfoundation/oneTBB.git
        GIT_TAG 3046c8b0c29df995980003ea24f4d78c80ec0c8d)
    set(BUILD_SHARED_LIBS OFF)
    set(TBB_TEST OFF CACHE BOOL "" FORCE)
    set(TBB_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(TBB_STRICT OFF CACHE BOOL "" FORCE)
    set(TBB_INSTALL OFF CACHE BOOL "" FORCE)
    set(TBBMALLOC_BUILD OFF CACHE BOOL "" FORCE)
    set(TBBMALLOC_PROXY_BUILD OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(onetbb)
else()
    find_package(TBB REQUIRED)
endif()

set(THIRDPARTY "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")

# The same core sources autoremesher.pro compiles into the Qt app.
add_library(autoremesher_core STATIC
    src/AutoRemesher/autoremesher.cpp
    src/AutoRemesher/constrainedleastsquares.cpp
    src/AutoRemesher/diagnostics.cpp
    src/AutoRemesher/featureedges.cpp
    src/AutoRemesher/foldrepair.cpp
    src/AutoRemesher/framefield.cpp
    src/AutoRemesher/isotropicremesher.cpp
    src/AutoRemesher/mapcheck.cpp
    src/AutoRemesher/meshseparator.cpp
    src/AutoRemesher/mixedintegerleastsquares.cpp
    src/AutoRemesher/parameterizer.cpp
    src/AutoRemesher/positionkey.cpp
    src/AutoRemesher/predicates.cpp
    src/AutoRemesher/qexextraction.cpp
    src/AutoRemesher/quadextractor.cpp
    src/AutoRemesher/quadparameterizer.cpp
    src/AutoRemesher/remesh.cpp
    src/AutoRemesher/singularitysimplifier.cpp
    src/AutoRemesher/surfacemesh.cpp
    src/AutoRemesher/surfaceprojector.cpp
    src/AutoRemesher/thinparts.cpp
    src/AutoRemesher/inputsanitizer.cpp
    src/AutoRemesher/untangler.cpp
    "${THIRDPARTY}/isotropicremesher/isotropicremesher.cpp"
    "${THIRDPARTY}/isotropicremesher/isotropichalfedgemesh.cpp"
    "${THIRDPARTY}/isotropicremesher/axisalignedboundingboxtree.cpp"
    "${THIRDPARTY}/meshoptimizer/src/simplifier.cpp"
    "${THIRDPARTY}/meshoptimizer/src/indexgenerator.cpp")
target_compile_features(autoremesher_core PUBLIC cxx_std_14)
target_include_directories(autoremesher_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${THIRDPARTY}/eigen"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${THIRDPARTY}/isotropicremesher"
        "${THIRDPARTY}/meshoptimizer/src")
# _USE_MATH_DEFINES: M_PI on MSVC. NOMINMAX: <windows.h> must not define min/max.
target_compile_definitions(autoremesher_core PUBLIC _USE_MATH_DEFINES NOMINMAX)
target_link_libraries(autoremesher_core PUBLIC TBB::tbb Threads::Threads)

# libQEx (Ebke et al. 2013, GPL-3.0-or-later) extracts the quads, with the fork's termination and
# memory guards (thirdparty/libQEx/PATCHES.md); it needs OpenMesh's core (BSD-3-Clause). Both vendored.
file(GLOB_RECURSE OPENMESH_CORE_SOURCES "${THIRDPARTY}/OpenMesh/src/OpenMesh/Core/*.cc")
add_library(autoremesher_qex STATIC ${OPENMESH_CORE_SOURCES}
    "${THIRDPARTY}/libQEx/interfaces/c/qex.cc"
    "${THIRDPARTY}/libQEx/src/MeshExtractor.cc"
    "${THIRDPARTY}/libQEx/src/predicates.c")
target_compile_features(autoremesher_qex PUBLIC cxx_std_14)
target_include_directories(autoremesher_qex PUBLIC "${THIRDPARTY}/OpenMesh/src" "${THIRDPARTY}/libQEx/interfaces/c")
# qex.h marks its functions dllimport on Windows unless DLLEXPORT is set; this is a static library.
target_compile_definitions(autoremesher_qex PUBLIC _USE_MATH_DEFINES OM_STATIC_BUILD DLLEXPORT=)
if(NOT MSVC)
    # Third-party warnings are not ours to fix. predicates.c uses K&R definitions, which C23 rejects.
    target_compile_options(autoremesher_qex PRIVATE -w $<$<COMPILE_LANGUAGE:C>:-std=gnu99>)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        # Shewchuk's predicates need SSE arithmetic, not x87 (libQEx's CMakeLists does the same).
        target_compile_options(autoremesher_qex PRIVATE -msse2 -mfpmath=sse)
    endif()
else()
    target_compile_options(autoremesher_qex PRIVATE /bigobj /utf-8 /W0)
endif()
target_link_libraries(autoremesher_core PRIVATE autoremesher_qex)

if(MSVC)
    # /bigobj: Eigen-heavy translation units exceed the default section count.
    # /utf-8: sources are UTF-8. Third-party code is noisy at /W3.
    target_compile_options(autoremesher_core PRIVATE /bigobj /utf-8 /W1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    # As autoremesher.pro (unix:!macx): x86-64-v2 keeps old CPUs supported.
    target_compile_options(autoremesher_core PRIVATE $<$<CONFIG:Release>:-O3 -funroll-loops -march=x86-64-v2>)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT AUTOREMESHER_IPO OUTPUT AUTOREMESHER_IPO_ERROR LANGUAGES CXX)

add_executable(autoremesher-cli
    src/cli/main.cpp
    src/cli/objfile.cpp
    src/cli/stdincontrol.cpp)
# C++17 for std::filesystem::u8path (UTF-8 paths on Windows).
target_compile_features(autoremesher-cli PRIVATE cxx_std_17)
target_compile_definitions(autoremesher-cli PRIVATE AUTOREMESHER_VERSION="${PROJECT_VERSION}")
# Build id (commit and date, cmake/BuildId.cmake), worked out on every build so
# it follows new commits without reconfiguring; empty AUTOREMESHER_BUILD_ID
# means "from git".
set(AUTOREMESHER_BUILD_ID "" CACHE STRING "Build id shown by --version and the add-on (empty: from git)")
set(AUTOREMESHER_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${AUTOREMESHER_GENERATED}")
add_custom_target(autoremesher_build_id
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}" "-DBUILD_ID=${AUTOREMESHER_BUILD_ID}"
        "-DOUTPUT_DIR=${AUTOREMESHER_GENERATED}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildId.cmake"
    BYPRODUCTS "${AUTOREMESHER_GENERATED}/build_id.h" "${AUTOREMESHER_GENERATED}/build-id.txt"
    COMMENT "Build id"
    VERBATIM)
add_dependencies(autoremesher-cli autoremesher_build_id)
target_include_directories(autoremesher-cli PRIVATE "${AUTOREMESHER_GENERATED}")
target_link_libraries(autoremesher-cli PRIVATE autoremesher_core)
if(MSVC)
    target_compile_options(autoremesher-cli PRIVATE /utf-8)
endif()
if(AUTOREMESHER_IPO)
    set_property(TARGET autoremesher_core autoremesher-cli PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if(AUTOREMESHER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# `cmake --install` lays out what the Blender add-on bundles (addon/package.py):
# the program in bin/, its build id in build-id.txt and the license of
# everything compiled into it in licenses/. A new license here also needs its
# copyright holder in addon/quadami/blender_manifest.toml.
install(TARGETS autoremesher-cli RUNTIME DESTINATION bin)
install(FILES "${AUTOREMESHER_GENERATED}/build-id.txt" DESTINATION .)
install(FILES LICENSE LICENSE-MIT-upstream DESTINATION licenses)
install(FILES "${THIRDPARTY}/isotropicremesher/LICENSE" DESTINATION licenses RENAME isotropicremesher-LICENSE)
install(FILES "${THIRDPARTY}/meshoptimizer/LICENSE.md" DESTINATION licenses RENAME meshoptimizer-LICENSE.md)
install(FILES "${THIRDPARTY}/OpenMesh/LICENSE" DESTINATION licenses RENAME OpenMesh-LICENSE)
# libQEx is GPL-3.0-or-later like the fork (licenses/LICENSE); its notice and the list of our changes.
install(FILES "${THIRDPARTY}/libQEx/README.md" DESTINATION licenses RENAME libQEx-README.md)
install(FILES "${THIRDPARTY}/libQEx/PATCHES.md" DESTINATION licenses RENAME libQEx-PATCHES.md)
foreach(eigen_license COPYING.MPL2 COPYING.BSD COPYING.APACHE COPYING.README)
    install(FILES "${THIRDPARTY}/eigen/${eigen_license}" DESTINATION licenses RENAME eigen-${eigen_license})
endforeach()
# tinyobjloader keeps its MIT license in the header's first comment.
file(READ src/tiny_obj_loader.h TINYOBJ_HEAD LIMIT 2000)
string(REGEX MATCH "/\\*\n([^*]*)\\*/" TINYOBJ_MATCH "${TINYOBJ_HEAD}")
set(TINYOBJ_LICENSE "${CMAKE_MATCH_1}")
if(NOT TINYOBJ_LICENSE MATCHES "MIT License")
    message(FATAL_ERROR "src/tiny_obj_loader.h no longer starts with its license")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/tinyobjloader-LICENSE.txt" "${TINYOBJ_LICENSE}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/tinyobjloader-LICENSE.txt" DESTINATION licenses)
if(AUTOREMESHER_BUNDLED_TBB)
    install(FILES "${onetbb_SOURCE_DIR}/LICENSE.txt" DESTINATION licenses RENAME oneTBB-LICENSE.txt)
    install(FILES "${onetbb_SOURCE_DIR}/third-party-programs.txt" DESTINATION licenses
        RENAME oneTBB-third-party-programs.txt)
endif()
