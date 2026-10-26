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

"""Run one remesh in a separate process and collect its result.

No bpy in here, so the same code runs inside Blender and in plain Python
(tests, scripts). The worker process is the Qt-free AutoRemesher CLI
(src/cli), bundled with the add-on in bin/ when it was packaged with one:

    autoremesher-cli -i in.obj -o out.obj --report-json report.json --stdin-control ...

The Qt app's headless mode takes the same arguments and also works.

- progress: the CLI prints "42% done. <step>" lines on stdout;
- cancel: write "cancel" to its stdin; the core stops at its next cancel point,
  which can take a few seconds inside one solve, so the process is only killed
  after CANCEL_GRACE_SECONDS;
- result: the OBJ it writes plus the JSON report (schema
  autoremesher.remesh-result, docs/INTERFACE.md section 3.1).
"""

import collections
import json
import os
import queue
import re
import shutil
import subprocess
import tempfile
import threading
import time

import numpy as np

CANCEL_GRACE_SECONDS = 5.0
SUPPORTED_INTERFACE_MAJOR = 1

# The core's MaximumTargetQuadCount and ApproximatePeakBytesPerQuad
# (src/AutoRemesher/remesh.h; test_worker.py checks they agree), and the count
# above which autoremesher-cli warns and the panel asks first.
MAXIMUM_TARGET_QUADS = 2_000_000
PEAK_BYTES_PER_QUAD = 22e3
LARGE_TARGET_QUADS = 200_000

# Parameter name in the report JSON -> CLI flag.
PARAMETER_FLAGS = {
    "target_quads": "--target-quads",
    "adaptivity": "--adaptivity",
    "anisotropy": "--anisotropy",
    "edge_scaling": "--edge-scaling",
    "sharp_edge_degrees": "--sharp-edge",
    "smooth_normal_degrees": "--smooth-normal",
    "resolution": "--resolution",
}

# Options of one run that are not panel settings -> CLI flag.  repair_folds
# is offered by the panel after a run that found folded input, flatten_spikes
# (only with repair_folds) next to it.
RUN_FLAGS = {
    "repair_folds": "--fold-repair",
    "flatten_spikes": "--flatten-spikes",
}

_PROGRESS_LINE = re.compile(r"^(\d+)% done\.(?: (.*))?$")


class Outcome:
    """What a finished job hands back.

    status is the report's status ("ok", "invalid_input", "cancelled",
    "failed"); when the process left no report it is "cancelled" after a
    cancel and "failed" otherwise. The mesh arrays are set only for "ok".
    """

    def __init__(self, status, message="", report=None, positions=None,
                 face_sizes=None, face_indices=None, log=""):
        self.status = status
        self.message = message
        self.report = report
        self.positions = positions        # float64 (V, 3)
        self.face_sizes = face_sizes      # uint32 (F,)
        self.face_indices = face_indices  # uint32 (sum of face_sizes,)
        self.log = log                    # last lines the worker printed

    @property
    def ok(self):
        return self.status == "ok"


def write_obj(path, positions, triangles):
    """positions (V, 3) float, triangles (F, 3) int, 0-based."""
    positions = np.asarray(positions, dtype=np.float64).reshape(-1, 3)
    triangles = np.asarray(triangles, dtype=np.int64).reshape(-1, 3)
    with open(path, "w", encoding="ascii", newline="\n") as f:
        # 9 significant digits round-trip Blender's float32 coordinates.
        np.savetxt(f, positions, fmt="v %.9g %.9g %.9g")
        np.savetxt(f, triangles + 1, fmt="f %d %d %d")


def read_obj(path):
    """Read the CLI's polygon OBJ: returns positions, face_sizes, face_indices."""
    coords = []
    sizes = []
    indices = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("v "):
                coords.extend(float(x) for x in line.split()[1:4])
            elif line.startswith("f "):
                corners = line.split()[1:]
                sizes.append(len(corners))
                indices.extend(int(c.split("/")[0]) - 1 for c in corners)
    return (np.array(coords, dtype=np.float64).reshape(-1, 3),
            np.array(sizes, dtype=np.uint32),
            np.array(indices, dtype=np.uint32))


CLI_NAME = "autoremesher-cli"


def bundled_executable():
    """Where a packaged add-on keeps its CLI (addon/package.py)."""
    name = CLI_NAME + (".exe" if os.name == "nt" else "")
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), "bin", name)


def _usable(path):
    if not path or not os.path.isfile(path):
        return False
    if os.access(path, os.X_OK):
        return True
    if os.name == "nt" or path != bundled_executable():
        return False
    # Blender unpacks extensions with Python's zipfile, which drops the
    # executable bit on Linux and macOS.
    try:
        os.chmod(path, os.stat(path).st_mode | 0o111)
    except OSError:
        return False
    return os.access(path, os.X_OK)


def find_executable(configured=""):
    """The CLI from the add-on preferences, $AUTOREMESHER_EXECUTABLE, the copy
    bundled with the add-on, or autoremesher-cli on PATH, as an absolute path
    (the worker runs in its own temporary folder)."""
    for candidate in (configured, os.environ.get("AUTOREMESHER_EXECUTABLE", ""), bundled_executable()):
        if _usable(candidate):
            return os.path.abspath(candidate)
    found = shutil.which(CLI_NAME)
    return os.path.abspath(found) if found else None


BUILD_ID_FILE = "build-id.txt"
_worker_build_ids = {}


def addon_build_id():
    """The build id addon/package.py stored with the add-on (commit and date,
    cmake/BuildId.cmake); "dev" when it runs from the source tree."""
    try:
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), BUILD_ID_FILE),
                  "r", encoding="utf-8") as f:
            return f.read().strip() or "dev"
    except OSError:
        return "dev"


def worker_build_id(executable):
    """The build id `executable --version` reports, or None (a build from
    before build ids, the Qt app, or a program that does not run). Cached per
    file, so the UI can ask on every redraw and a rebuilt program is asked again."""
    try:
        stat = os.stat(executable)
    except (OSError, TypeError):
        return None
    key = (executable, stat.st_mtime_ns, stat.st_size)
    if key not in _worker_build_ids:
        creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
        try:
            output = subprocess.run([executable, "--version"], capture_output=True, timeout=5,
                                    env=worker_environment(os.environ), creationflags=creationflags,
                                    text=True, encoding="utf-8", errors="replace").stdout
        except (OSError, subprocess.SubprocessError):
            output = ""
        match = re.search(r"\(build ([^,)]+)", output)
        _worker_build_ids[key] = match.group(1).strip() if match else None
    return _worker_build_ids[key]


def worker_environment(environ):
    """The worker's environment: Blender's, minus what would confuse the CLI.

    The configured executable may be the Qt app, so Qt always gets the
    offscreen platform. Inside a Snap (Blender's is one), library paths into
    the snap are dropped, so the worker loads the system TBB (and Qt) it was
    built against. LD_LIBRARY_PATH is colon-separated on every platform that
    reads it, so it is split on ":" and not os.pathsep (";" on Windows).
    """
    env = dict(environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    snap = env.get("SNAP")
    if snap and "LD_LIBRARY_PATH" in env:
        kept = [p for p in env["LD_LIBRARY_PATH"].split(":")
                if p and not p.startswith(snap.rstrip("/") + "/") and p != snap]
        if kept:
            env["LD_LIBRARY_PATH"] = ":".join(kept)
        else:
            del env["LD_LIBRARY_PATH"]
    return env


class RemeshJob:
    """One remesh in a child process. Not thread-safe: drive it from one thread.

    start(), then call poll() regularly (it never blocks) until done is true,
    then outcome(). cancel() may be called at any time.
    """

    def __init__(self, executable, positions, triangles, parameters):
        self.executable = executable
        self.positions = positions
        self.triangles = triangles
        self.parameters = dict(parameters)
        self.fraction = 0.0
        self.step = ""
        self.cancel_requested = False
        self.killed = False
        self._cancel_time = None
        self._process = None
        self._reader = None
        self._lines = queue.Queue()
        self._log = collections.deque(maxlen=40)
        self._workdir = None
        self._outcome = None

    def _path(self, name):
        return os.path.join(self._workdir, name)

    def start(self):
        flags = dict(PARAMETER_FLAGS, **RUN_FLAGS)
        unknown = set(self.parameters) - set(flags)
        if unknown:
            raise ValueError("unknown parameters: " + ", ".join(sorted(unknown)))
        self._workdir = tempfile.mkdtemp(prefix="quadami-")
        write_obj(self._path("input.obj"), self.positions, self.triangles)
        args = [self.executable,
                "-i", self._path("input.obj"),
                "-o", self._path("output.obj"),
                "--report-json", self._path("report.json"),
                "--stdin-control"]
        for name, value in self.parameters.items():
            if name == "target_quads":
                value = int(value)
            if isinstance(value, bool):
                value = "on" if value else "off"
            args += [flags[name], repr(value) if isinstance(value, float) else str(value)]
        env = worker_environment(os.environ)
        creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
        self._process = subprocess.Popen(
            args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            env=env, cwd=self._workdir, creationflags=creationflags,
            text=True, encoding="utf-8", errors="replace", bufsize=1)
        self._reader = threading.Thread(target=self._read_output, daemon=True)
        self._reader.start()

    def _read_output(self):
        try:
            for line in self._process.stdout:
                self._lines.put(line.rstrip("\r\n"))
        except (OSError, ValueError):
            pass  # stdout closed by cleanup()
        self._lines.put(None)

    def poll(self):
        """Take in what the worker printed; kill it if a cancel is overdue."""
        while True:
            try:
                line = self._lines.get_nowait()
            except queue.Empty:
                break
            if line is None:
                continue
            match = _PROGRESS_LINE.match(line)
            if match:
                self.fraction = max(self.fraction, min(int(match.group(1)), 100) / 100.0)
                if match.group(2):
                    self.step = match.group(2)
            elif line:
                self._log.append(line)
        if (self._cancel_time is not None and self._process.poll() is None
                and time.monotonic() - self._cancel_time > CANCEL_GRACE_SECONDS):
            self._process.kill()
            self.killed = True

    def cancel(self):
        if self.cancel_requested or self._process is None:
            return
        self.cancel_requested = True
        self._cancel_time = time.monotonic()
        try:
            self._process.stdin.write("cancel\n")
            self._process.stdin.flush()
        except (BrokenPipeError, OSError, ValueError):
            pass  # already exiting

    @property
    def done(self):
        return (self._process is not None and self._process.poll() is not None
                and not self._reader.is_alive())

    def outcome(self):
        """Read the result once the job is done; removes the temporary folder."""
        if self._outcome is None:
            self.poll()
            try:
                self._outcome = self._collect()
            finally:
                self.cleanup()
        return self._outcome

    def _collect(self):
        code = self._process.returncode
        log = "\n".join(self._log)
        try:
            with open(self._path("report.json"), "r", encoding="utf-8") as f:
                report = json.load(f)
        except (OSError, ValueError):
            report = None
        if report is None:
            if self.cancel_requested:
                message = "killed after the cancel took longer than %g s" % CANCEL_GRACE_SECONDS \
                    if self.killed else "cancelled"
                return Outcome("cancelled", message, log=log)
            last = self._log[-1] if self._log else "no output"
            return Outcome("failed", "the remesher exited with code %s: %s" % (code, last), log=log)
        major = str(report.get("interface_version", "")).split(".")[0]
        if major != str(SUPPORTED_INTERFACE_MAJOR):
            return Outcome("failed", "the remesher speaks interface %s, this add-on %d.x"
                           % (report.get("interface_version"), SUPPORTED_INTERFACE_MAJOR),
                           report=report, log=log)
        status = report.get("status", "failed")
        message = report.get("message", "")
        if status != "ok":
            return Outcome(status, message, report=report, log=log)
        try:
            positions, face_sizes, face_indices = read_obj(self._path("output.obj"))
        except OSError as error:
            return Outcome("failed", "no output mesh: %s" % error, report=report, log=log)
        if len(face_sizes) == 0:
            return Outcome("failed", "the output mesh has no faces", report=report, log=log)
        return Outcome("ok", message, report, positions, face_sizes, face_indices, log)

    def cleanup(self):
        """Stop the process if it still runs and remove the temporary folder."""
        if self._process is not None and self._process.poll() is None:
            self._process.kill()
            self._process.wait()
        if self._process is not None:
            for stream in (self._process.stdin, self._process.stdout):
                try:
                    stream.close()
                except (OSError, ValueError):
                    pass
        if self._workdir is not None:
            shutil.rmtree(self._workdir, ignore_errors=True)
            self._workdir = None


def remesh(executable, positions, triangles, parameters, on_progress=None, interval=0.05):
    """Blocking convenience wrapper: run a job to the end and return its Outcome.

    on_progress(fraction, step) may return True to request a cancel.
    """
    job = RemeshJob(executable, positions, triangles, parameters)
    job.start()
    try:
        while not job.done:
            job.poll()
            if on_progress is not None and on_progress(job.fraction, job.step):
                job.cancel()
            time.sleep(interval)
        return job.outcome()
    finally:
        job.cleanup()
