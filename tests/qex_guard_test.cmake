# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
# Included by tests/CMakeLists.txt in place of the default add_test for qex_guard_test.cpp.
add_test(NAME qex_guard COMMAND qex_guard_test "${CMAKE_CURRENT_SOURCE_DIR}/data/qex-budget-ellipsoid.obj"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/qex-oneway-sculpt-parts.obj"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/qex-faceloop-sculpt-parts.obj")
# A guard that stops working shows as a hang; unguarded, this map never ends.
set_tests_properties(qex_guard PROPERTIES TIMEOUT 120)
