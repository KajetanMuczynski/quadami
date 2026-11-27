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

"""The density plans of this session, and the one run that waits after its
plan so Remesh can go on with it (docs/PREVIEW.md §4.8).  No bpy in here.

A plan is kept per object, mesh content and plan settings, so hiding and
showing the preview again, or coming back to an object, needs no new plan;
the content key (what the worker is sent: positions, triangles, marks and
guides) means an edited mesh can never get a stale plan, and an undo finds
its plan again.

The preview's worker is a real run that stopped after its plan
(worker.RemeshJob with wait_after_plan).  It waits here until Remesh asks
for exactly its mesh and parameters, which then lets it go on instead of
starting over; anything else (another preview, a file load, WAIT_SECONDS
without use) stops it, so at most one worker ever waits.
"""

import collections
import time

# Plans kept, the most recently used first out of the way last.
KEEP_PLANS = 4
# A waiting worker holds the run's memory (about worker.PEAK_BYTES_PER_QUAD
# per quad at most), so it is stopped after this long unused.
WAIT_SECONDS = 15 * 60


def run_key(object_name, digest, worker_key, parameters):
    """What a waiting run and the Remesh that would take it over must share:
    the object, the mesh, the worker and every parameter of the run."""
    return (object_name, digest, worker_key, tuple(sorted(parameters.items())))


def plan_key(object_name, digest, worker_key, settings_key):
    return (object_name, digest, worker_key, settings_key)


class Store:
    def __init__(self, clock=time.monotonic):
        self._clock = clock
        self._plans = collections.OrderedDict()
        self._waiting = None  # (run key, job, since)

    # Plans.
    def add_plan(self, key, preview):
        self._plans[key] = preview
        self._plans.move_to_end(key)
        while len(self._plans) > KEEP_PLANS:
            self._plans.popitem(last=False)

    def plan(self, key):
        preview = self._plans.get(key)
        if preview is not None:
            self._plans.move_to_end(key)
        return preview

    def plans(self):
        return list(self._plans.values())

    # The waiting run.
    def park(self, key, job):
        """Keeps `job` (waiting after its plan) for a Remesh with `key`;
        stops the one waiting before."""
        self.stop_waiting()
        self._waiting = (key, job, self._clock())

    def waiting_key(self):
        """The key of the run that waits, or None."""
        self._drop_ended()
        return self._waiting[0] if self._waiting else None

    def take(self, key):
        """The waiting run if it is for `key` (it is then no longer kept
        here), else None and the waiting run is stopped: a Remesh of
        anything else would leave it waiting for nothing."""
        self._drop_ended()
        if self._waiting is None:
            return None
        waiting_key, job, _since = self._waiting
        self._waiting = None
        if waiting_key == key:
            return job
        _stop(job)
        return None

    def stop_waiting(self):
        if self._waiting is not None:
            _stop(self._waiting[1])
            self._waiting = None

    def expire(self):
        """Stops the waiting run after WAIT_SECONDS unused. Returns True if
        one still waits (so the caller checks again later)."""
        self._drop_ended()
        if self._waiting is not None and self._clock() - self._waiting[2] > WAIT_SECONDS:
            self.stop_waiting()
        return self._waiting is not None

    def clear(self):
        self.stop_waiting()
        self._plans.clear()

    def _drop_ended(self):
        if self._waiting is None:
            return
        job = self._waiting[1]
        job.poll()
        if job.done or not job.waiting:
            job.cleanup()
            self._waiting = None


def _stop(job):
    job.cancel()
    job.cleanup()
