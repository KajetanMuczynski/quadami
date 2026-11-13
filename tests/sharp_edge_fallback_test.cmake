# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
# Included by tests/CMakeLists.txt in place of the default add_test for sharp_edge_fallback_test.cpp.
add_test(NAME sharp_edge_fallback COMMAND sharp_edge_fallback_test
    "${CMAKE_CURRENT_SOURCE_DIR}/data/cad-csg-bracket-rot.obj")
