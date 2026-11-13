# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
# Included by tests/CMakeLists.txt in place of the default add_test for amd_ordering_test.cpp.
add_test(NAME amd_ordering COMMAND amd_ordering_test)
# Unpatched, Eigen's AMD ordering of this matrix runs for minutes: a hang.
set_tests_properties(amd_ordering PROPERTIES TIMEOUT 60)
