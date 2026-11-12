# Local changes to the vendored Eigen

Eigen is MPL-2.0; changed files stay under MPL-2.0 and carry a dated modification note.

| # | File | Change | Why |
|---|---|---|---|
| 1 | `Eigen/src/OrderingMethods/Amd.h` | The supervariable hash of a node is summed in a `long long` and reduced modulo `n` into `h`; before, the sum was kept in `StorageIndex` (`int`). Same bucket whenever the old sum did not overflow. | The sum of a node's element and neighbour indices passed 2^31 on large systems with a few dense rows (head sculpt at 400,000 target quads: 1.7M unknowns, rows of up to 4,087 entries). It wrapped negative and indexed `hhead` out of bounds: about 25 minutes in the ordering, then a segfault. Upstream Eigen master still has the 32-bit sum (checked 2026-11-05). Test: `tests/amd_ordering_test.cpp`. |
