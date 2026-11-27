# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""Where the worker keeps the prepared input of a mesh between runs.
No bpy in here.

The core writes the file (autoremesher-cli --input-cache, docs/PREVIEW.md
§4.2): the cleaned, fold-checked and split input plus the mesh itself, so a
re-run or a preview of the same mesh skips those steps, and the add-on need
not write the mesh out at all.  The core decides whether a file fits (same
mesh, fold settings and build); this module only names one file per mesh
and worker, and keeps the folder small.
"""

import hashlib
import os
import time

import numpy as np

# Files kept: the newest few, within a total size (a 1M-triangle sculpt
# takes about 60 MB).
KEEP_FILES = 4
KEEP_BYTES = 1536 * 1024 * 1024
SUFFIX = ".qic"


def mesh_digest(positions, triangles, marked):
    """A key (hex) for what the worker is sent of a mesh: positions,
    triangles and marked edges (guides included)."""
    digest = hashlib.blake2b(digest_size=16)
    for array in (positions, triangles, marked if marked is not None else np.zeros((0, 2), np.int32)):
        array = np.ascontiguousarray(array)
        digest.update(("%s %s;" % (array.dtype.str, array.shape)).encode())
        digest.update(array.tobytes())
    return digest.hexdigest()


def path_for(directory, positions, triangles, marked, worker_key, digest=None):
    """The file for this mesh and worker. `worker_key` names the worker
    exactly (its build id, path, size and time), so a rebuilt worker never
    gets a file another one wrote, even a development build whose id did not
    change.  `digest`: mesh_digest() of the mesh, if already known."""
    if digest is None:
        digest = mesh_digest(positions, triangles, marked)
    name = hashlib.blake2b(("quadami input cache 2;%s;%s" % (worker_key, digest)).encode("utf-8", "replace"),
                           digest_size=16)
    return os.path.join(directory, name.hexdigest() + SUFFIX)


def worker_key(executable, build_id):
    """What path_for() needs to know about the worker."""
    try:
        stat = os.stat(executable)
        return "%s;%s;%d;%d" % (build_id, os.path.abspath(executable), stat.st_size, stat.st_mtime_ns)
    except (OSError, TypeError):
        return "%s;%s" % (build_id, executable)


def touch(path):
    """Marks `path` as just used, so prune() keeps it longest."""
    try:
        os.utime(path, None)
    except OSError:
        pass


def prune(directory, keep, keep_files=KEEP_FILES, keep_bytes=KEEP_BYTES):
    """Removes the oldest cache files (and leftovers of interrupted writes)
    beyond `keep_files` files or `keep_bytes` bytes; never `keep`, the file
    about to be used. Returns how many files were removed."""
    try:
        names = os.listdir(directory)
    except OSError:
        return 0
    files = []
    removed = 0
    for name in names:
        path = os.path.join(directory, name)
        if ".qic.tmp-" in name:
            # A write the worker did not finish; a running one renames its
            # file within a fraction of a second, so only old ones go.
            try:
                if os.stat(path).st_mtime < time.time() - 3600:
                    os.remove(path)
                    removed += 1
            except OSError:
                pass
            continue
        if not name.endswith(SUFFIX) or os.path.normcase(path) == os.path.normcase(keep or ""):
            continue
        try:
            stat = os.stat(path)
        except OSError:
            continue
        files.append((stat.st_mtime, stat.st_size, path))
    files.sort(reverse=True)
    # The file about to be used counts as one of the kept ones.
    count = 1 if keep else 0
    total = 0
    for _, size, path in files:
        count += 1
        total += size
        if count > keep_files or total > keep_bytes:
            try:
                os.remove(path)
                removed += 1
            except OSError:
                pass
    return removed
