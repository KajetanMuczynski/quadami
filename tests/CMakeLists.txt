# SPDX-License-Identifier: GPL-3.0-or-later
# Qt-free tests of the remeshing core. They link the top-level autoremesher_core
# target, so the core is compiled from one source list:
#
#   cmake -S . -B build-tests -DAUTOREMESHER_BUILD_TESTS=ON
#   cmake --build build-tests --parallel 2
#   ctest --test-dir build-tests --output-on-failure
add_executable(quad_count_test quad_count_test.cpp)
target_link_libraries(quad_count_test PRIVATE autoremesher_core)
add_test(NAME quad_count COMMAND quad_count_test)

add_executable(diagnostics_test diagnostics_test.cpp)
target_link_libraries(diagnostics_test PRIVATE autoremesher_core)
add_test(NAME diagnostics COMMAND diagnostics_test)

add_executable(open_boundary_test open_boundary_test.cpp)
target_link_libraries(open_boundary_test PRIVATE autoremesher_core)
add_test(NAME open_boundary COMMAND open_boundary_test)

add_executable(quality_report_test quality_report_test.cpp)
target_link_libraries(quality_report_test PRIVATE autoremesher_core)
add_test(NAME quality_report COMMAND quality_report_test)

add_executable(small_island_test small_island_test.cpp)
target_link_libraries(small_island_test PRIVATE autoremesher_core)
add_test(NAME small_island COMMAND small_island_test)

add_executable(hole_filling_test hole_filling_test.cpp)
target_link_libraries(hole_filling_test PRIVATE autoremesher_core)
add_test(NAME hole_filling COMMAND hole_filling_test)

add_executable(rounding_test rounding_test.cpp)
target_link_libraries(rounding_test PRIVATE autoremesher_core)
add_test(NAME rounding COMMAND rounding_test)

add_executable(position_key_test position_key_test.cpp)
target_link_libraries(position_key_test PRIVATE autoremesher_core)
add_test(NAME position_key COMMAND position_key_test)

add_executable(untangle_test untangle_test.cpp)
target_link_libraries(untangle_test PRIVATE autoremesher_core)
add_test(NAME untangle COMMAND untangle_test)

add_executable(decimation_topology_test decimation_topology_test.cpp)
target_link_libraries(decimation_topology_test PRIVATE autoremesher_core)
add_test(NAME decimation_topology COMMAND decimation_topology_test)

add_executable(target_limit_test target_limit_test.cpp)
target_link_libraries(target_limit_test PRIVATE autoremesher_core)
add_test(NAME target_limit COMMAND target_limit_test)

add_executable(surface_projection_test surface_projection_test.cpp)
target_link_libraries(surface_projection_test PRIVATE autoremesher_core)
add_test(NAME surface_projection COMMAND surface_projection_test)

add_executable(map_check_test map_check_test.cpp)
target_link_libraries(map_check_test PRIVATE autoremesher_core)
add_test(NAME map_check COMMAND map_check_test)

add_executable(input_cleanup_test input_cleanup_test.cpp)
target_link_libraries(input_cleanup_test PRIVATE autoremesher_core)
add_test(NAME input_cleanup COMMAND input_cleanup_test)

add_executable(input_sanitizer_test input_sanitizer_test.cpp)
target_link_libraries(input_sanitizer_test PRIVATE autoremesher_core)
add_test(NAME input_sanitizer COMMAND input_sanitizer_test)

add_executable(qex_guard_test qex_guard_test.cpp)
target_link_libraries(qex_guard_test PRIVATE autoremesher_core)
add_test(NAME qex_guard COMMAND qex_guard_test "${CMAKE_CURRENT_SOURCE_DIR}/data/qex-budget-ellipsoid.obj"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/qex-oneway-sculpt-parts.obj"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/qex-faceloop-sculpt-parts.obj")
# A guard that stops working shows as a hang; unguarded, this map never ends.
set_tests_properties(qex_guard PROPERTIES TIMEOUT 120)

add_executable(needle_tip_test needle_tip_test.cpp)
target_link_libraries(needle_tip_test PRIVATE autoremesher_core)
add_test(NAME needle_tip COMMAND needle_tip_test)
add_executable(fold_repair_test fold_repair_test.cpp)
target_link_libraries(fold_repair_test PRIVATE autoremesher_core)
add_test(NAME fold_repair COMMAND fold_repair_test)
add_executable(thin_parts_test thin_parts_test.cpp)
target_link_libraries(thin_parts_test PRIVATE autoremesher_core)
add_test(NAME thin_parts COMMAND thin_parts_test)
add_executable(quad_smoothing_test quad_smoothing_test.cpp)
target_link_libraries(quad_smoothing_test PRIVATE autoremesher_core)
add_test(NAME quad_smoothing COMMAND quad_smoothing_test)
