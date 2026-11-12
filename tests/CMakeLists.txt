# SPDX-License-Identifier: GPL-3.0-or-later
# Qt-free tests of the remeshing core. They link the top-level autoremesher_core
# target, so the core is compiled from one source list:
#
#   cmake -S . -B build-tests -DAUTOREMESHER_BUILD_TESTS=ON
#   cmake --build build-tests --parallel 2
#   ctest --test-dir build-tests --output-on-failure
#
# Every tests/<name>_test.cpp becomes the executable <name>_test and the ctest test
# <name>, so adding a test does not touch this file (parallel PRs used to collide on
# its last lines). A test that needs arguments or properties gets a
# tests/<name>_test.cmake next to it, which is included instead of the default
# add_test (see qex_guard_test.cmake).
file(GLOB test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
foreach(source IN LISTS test_sources)
    get_filename_component(target "${source}" NAME_WE)
    string(REGEX REPLACE "_test$" "" name "${target}")
    add_executable(${target} "${source}")
    target_link_libraries(${target} PRIVATE autoremesher_core)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${target}.cmake")
        include("${CMAKE_CURRENT_SOURCE_DIR}/${target}.cmake")
    else()
        add_test(NAME ${name} COMMAND ${target})
    endif()
endforeach()
