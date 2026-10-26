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

"""Quad remeshing for Blender in a separate worker process.

The remesher never runs inside Blender: the operator writes the mesh to a
temporary folder, starts the headless AutoRemesher CLI (worker.py), shows its
progress, cancels it on Esc, and imports the result with its quality report.
The add-on's name and ids come from names.py.
"""

import json

import bpy
import numpy as np
from bpy.props import BoolProperty, EnumProperty, FloatProperty, IntProperty, PointerProperty, StringProperty

from . import report as report_rows
from . import worker
from .names import ID, NAME

REPORT_PROPERTY = ID + "_report"
# Name of the object a result was made from, for "Repair Folds and Re-run".
SOURCE_PROPERTY = ID + "_source"

# The one running job and the last result, shared by the operator and the panel.
_job = None
_cancel_from_panel = False
_last_status = ""
_last_message = ""
_last_report = None


def _preferences(context):
    addon = context.preferences.addons.get(__package__)
    return addon.preferences if addon else None


def _settings(context):
    return getattr(context.scene, ID)


class RemeshPreferences(bpy.types.AddonPreferences):
    bl_idname = __package__

    executable: StringProperty(
        name="Remesher executable",
        description="The remesher program (autoremesher-cli). Empty: "
                    "$AUTOREMESHER_EXECUTABLE, then the copy bundled with the add-on, "
                    "then autoremesher-cli on PATH",
        subtype="FILE_PATH",
    )

    def draw(self, context):
        self.layout.prop(self, "executable")
        found = worker.find_executable(bpy.path.abspath(self.executable))
        self.layout.label(text="Using: " + (found or "not found"),
                          icon="CHECKMARK" if found else "ERROR")
        _draw_build(self.layout, found, always_both=True)


def _draw_build(layout, executable, always_both=False):
    """The add-on's build id in grey, and the remesher's too when they differ
    (then in red: the add-on and its worker come from different builds)."""
    addon_id = worker.addon_build_id()
    worker_id = worker.worker_build_id(executable) if executable else None
    mismatch = executable is not None and worker_id != addon_id
    column = layout.column(align=True)
    row = column.row()
    row.enabled = mismatch
    row.alert = mismatch
    row.label(text="Add-on build " + addon_id, icon="ERROR" if mismatch else "NONE")
    if mismatch or (always_both and executable):
        row = column.row()
        row.enabled = mismatch
        row.alert = mismatch
        row.label(text="Remesher build " + (worker_id or "unknown"))


class RemeshSettings(bpy.types.PropertyGroup):
    # Exactly the parameters of the core's remesh() (docs/INTERFACE.md section 3),
    # with the Qt app's ranges. Tooltips state measured behaviour
    # (research/baseline-and-profiling.md section 4.5, code-to-math-spec.md).
    target_quads: IntProperty(
        name="Target Quads", default=5000, min=1, max=worker.MAXIMUM_TARGET_QUADS,
        soft_min=100, soft_max=worker.LARGE_TARGET_QUADS,
        description="Number of quads to aim for. Memory use grows with it, about 2 GB "
                    "per 100,000 quads; above %s quads the add-on asks first"
                    % format(worker.LARGE_TARGET_QUADS, ","))
    adaptivity: FloatProperty(
        name="Adaptivity", default=0.5, min=0.0, max=1.0,
        description="Smaller quads where the surface curves strongly, larger where it is "
                    "flat. 0: even quad size and the fewest flipped quads. "
                    "1: strongest variation, about twice the flipped quads of the default")
    sharp_edge_degrees: FloatProperty(
        name="Sharp Edge", default=90.0, min=30.0, max=180.0,
        description="Edges where the surface bends by more than this angle (degrees) "
                    "are kept as creases. Lower keeps more edges but makes more broken quads "
                    "(60: about twice the triangles and other non-quads)")
    anisotropy: FloatProperty(
        name="Anisotropy", default=1.0, min=0.0, max=1.0,
        description="Lets quads stretch along the direction in which the surface curves "
                    "least, as on a cylinder (aspect up to 2.3:1). 0: square quads, but the surface "
                    "is followed 25–35 % less closely and creases less well. "
                    "Almost no effect on quad count or on broken and flipped quads")
    edge_scaling: FloatProperty(
        name="Edge Scaling", default=1.0, min=1.0, max=4.0,
        description="Multiplies the quad edge length, so the quad count falls with its "
                    "square (2 gives about a quarter of the quads). Same effect as "
                    "lowering Target Quads, which is the clearer control")
    smooth_normal_degrees: FloatProperty(
        name="Smooth Normal", default=0.0, min=0.0, max=180.0,
        description="0: the result follows the input's flat triangles. Above 0: it follows "
                    "a smooth curved surface through them instead (helps on low-poly "
                    "input); faces turned more than this angle (degrees) from a vertex's "
                    "average normal are left out of its smoothing, so creases stay")
    resolution: EnumProperty(
        name="Resolution",
        items=(
            ("quality", "Quality", "Solves on a finer triangle mesh: cleaner quads and fewer "
                                   "flipped quads, about 1.5 times as long as Fast"),
            ("fast", "Fast", "Solves on a coarser triangle mesh: quicker, rougher quads"),
        ),
        default="quality",
        description="Triangle mesh the quads are solved on. The quad count is the same either way")

    # Not a remesh parameter: a choice next to Repair Folds and Re-run.
    flatten_spikes: BoolProperty(
        name="Also Flatten Needle Spikes", default=False,
        description="Where a fold sits on a needle-like spike and cannot be smoothed out, "
                    "lower the spike's tip to its base. This changes the shape: the panel "
                    "says how many tips moved and how far")

    def parameters(self):
        return {name: getattr(self, name) for name in worker.PARAMETER_FLAGS}


# How the panel shows the parameters: the main ones, then a collapsed
# "Advanced" section. Every parameter appears exactly once (blender_smoke.py).
MAIN_PARAMETERS = ("target_quads", "adaptivity", "sharp_edge_degrees")
ADVANCED_PARAMETERS = ("resolution", "anisotropy", "edge_scaling", "smooth_normal_degrees")


def _mesh_arrays(context, obj):
    """Vertex positions and triangles of the evaluated mesh, in object space."""
    depsgraph = context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    try:
        mesh.calc_loop_triangles()
        positions = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
        mesh.vertices.foreach_get("co", positions)
        triangles = np.empty(len(mesh.loop_triangles) * 3, dtype=np.int32)
        mesh.loop_triangles.foreach_get("vertices", triangles)
    finally:
        evaluated.to_mesh_clear()
    return positions.reshape(-1, 3), triangles.reshape(-1, 3)


def _build_object(context, source_name, matrix, collection_names, outcome):
    mesh = bpy.data.meshes.new(source_name + "_remesh")
    offsets = np.concatenate(([0], np.cumsum(outcome.face_sizes, dtype=np.int64)))
    indices = outcome.face_indices.tolist()
    faces = [indices[offsets[i]:offsets[i + 1]] for i in range(len(outcome.face_sizes))]
    mesh.from_pydata(outcome.positions.tolist(), [], faces)
    changed = mesh.validate()
    mesh.update()
    obj = bpy.data.objects.new(source_name + "_remesh", mesh)
    obj.matrix_world = matrix
    collections = [bpy.data.collections.get(name) for name in collection_names]
    collections = [c for c in collections if c is not None] or [context.scene.collection]
    for collection in collections:
        collection.objects.link(obj)
    report = dict(outcome.report)
    if changed:
        report.setdefault("warnings", []).append("Blender's mesh validation changed the result")
    obj[REPORT_PROPERTY] = json.dumps(report)
    obj[SOURCE_PROPERTY] = source_name
    return obj, report


def _redraw(context):
    screen = context.screen
    if screen is None:
        return
    for area in screen.areas:
        if area.type == "VIEW_3D":
            area.tag_redraw()


class OBJECT_OT_remesh_quads(bpy.types.Operator):
    """Remesh the active mesh into quads in a separate process (Esc cancels)"""
    bl_idname = "object." + ID + "_remesh"
    bl_label = "Remesh"
    bl_options = {"REGISTER", "UNDO"}

    # Set by the "Repair Folds and Re-run" button: remesh this object (the
    # one the shown result was made from) instead of the active one, with
    # the fold repair on, and hide the old result.
    source: StringProperty(default="", options={"HIDDEN", "SKIP_SAVE"})
    repair_folds: BoolProperty(default=False, options={"HIDDEN", "SKIP_SAVE"})
    flatten_spikes: BoolProperty(default=False, options={"HIDDEN", "SKIP_SAVE"})

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return _job is None and obj is not None and obj.type == "MESH" and context.mode == "OBJECT"

    def invoke(self, context, event):
        # A stray extra zero in Target Quads can exhaust the machine's memory.
        target = _settings(context).target_quads
        if target > worker.LARGE_TARGET_QUADS:
            return context.window_manager.invoke_confirm(
                self, event, title="Remesh to %s quads?" % format(target, ","),
                message="This needs about %.1f GB of memory and can take a long time."
                        % (target * worker.PEAK_BYTES_PER_QUAD / 1e9),
                confirm_text="Remesh", icon="WARNING")
        return self.execute(context)

    def execute(self, context):
        global _job, _cancel_from_panel, _last_status, _last_message, _last_report
        preferences = _preferences(context)
        configured = bpy.path.abspath(preferences.executable) if preferences else ""
        executable = worker.find_executable(configured)
        if executable is None:
            self.report({"ERROR"}, "Remesher executable not found; set it in the %s preferences" % NAME)
            return {"CANCELLED"}
        obj = context.active_object
        self._replaced = None
        if self.source:
            self._replaced = obj.name
            obj = bpy.data.objects.get(self.source)
            if obj is None or obj.type != "MESH":
                self.report({"ERROR"}, "The original object \"%s\" is gone; select it and remesh again" % self.source)
                return {"CANCELLED"}
        # A hidden object (the source of a shown result) still evaluates.
        positions, triangles = _mesh_arrays(context, obj)
        if len(triangles) == 0:
            self.report({"ERROR"}, "The mesh has no faces")
            return {"CANCELLED"}
        parameters = _settings(context).parameters()
        if self.repair_folds:
            parameters["repair_folds"] = True
            if self.flatten_spikes:
                parameters["flatten_spikes"] = True
        job = worker.RemeshJob(executable, positions, triangles, parameters)
        try:
            job.start()
        except (OSError, ValueError) as error:
            job.cleanup()
            self.report({"ERROR"}, "Could not start the remesher: %s" % error)
            return {"CANCELLED"}
        _job = job
        _cancel_from_panel = False
        _last_status, _last_message, _last_report = "running", "", None
        self._source_name = obj.name
        self._matrix = obj.matrix_world.copy()
        self._collections = [c.name for c in obj.users_collection]
        self._timer = context.window_manager.event_timer_add(0.1, window=context.window)
        context.window_manager.modal_handler_add(self)
        self._status_text(context)
        return {"RUNNING_MODAL"}

    def _status_text(self, context):
        if _job.cancel_requested:
            text = NAME + ": cancelling..."
        else:
            text = "%s: %d%% %s   (Esc to cancel)" % (NAME, round(_job.fraction * 100), _job.step)
        context.workspace.status_text_set(text)

    def modal(self, context, event):
        if event.type == "ESC" and event.value == "PRESS":
            _job.cancel()
            self._status_text(context)
            return {"RUNNING_MODAL"}
        if event.type != "TIMER":
            return {"PASS_THROUGH"}
        if _cancel_from_panel:
            _job.cancel()
        _job.poll()
        if not _job.done:
            self._status_text(context)
            _redraw(context)
            return {"PASS_THROUGH"}
        return self._finish(context)

    def _finish(self, context):
        global _job, _last_status, _last_message, _last_report
        outcome = _job.outcome()
        self._stop(context)
        _last_status, _last_message, _last_report = outcome.status, outcome.message, outcome.report
        if not outcome.ok:
            if outcome.log:
                print(NAME + " worker output:\n" + outcome.log)
            level = {"WARNING"} if outcome.status == "cancelled" else {"ERROR"}
            self.report(level, "%s %s: %s" % (
                NAME, report_rows.STATUS_TEXT.get(outcome.status, outcome.status), outcome.message))
            return {"CANCELLED"}
        new_obj, _last_report = _build_object(
            context, self._source_name, self._matrix, self._collections, outcome)
        source = bpy.data.objects.get(self._source_name)
        for selected in list(context.view_layer.objects.selected):
            selected.select_set(False)
        for hidden in (source, bpy.data.objects.get(self._replaced) if self._replaced else None):
            if hidden is not None and hidden.name in context.view_layer.objects:
                hidden.hide_set(True)
        new_obj.select_set(True)
        context.view_layer.objects.active = new_obj
        output = outcome.report.get("output", {})
        self.report({"INFO"}, "%s: %d quads, %d non-quads" % (
            NAME, output.get("quads", 0), output.get("non_quads", 0)))
        return {"FINISHED"}

    def _stop(self, context):
        global _job
        context.window_manager.event_timer_remove(self._timer)
        context.workspace.status_text_set(None)
        _redraw(context)
        _job = None

    def cancel(self, context):
        # Blender removes the handler, e.g. when another file is loaded.
        global _last_status, _last_message
        if _job is not None:
            _job.cleanup()
            _last_status, _last_message = "cancelled", "stopped by Blender"
            self._stop(context)


class OBJECT_OT_remesh_quads_cancel(bpy.types.Operator):
    """Cancel the running remesh"""
    bl_idname = "object." + ID + "_cancel"
    bl_label = "Cancel"

    @classmethod
    def poll(cls, context):
        return _job is not None and not _job.cancel_requested

    def execute(self, context):
        global _cancel_from_panel
        _cancel_from_panel = True
        return {"FINISHED"}


def _draw_fold_offer(box, settings, source_name):
    column = box.column(align=True)
    for line in report_rows.fold_offer_lines():
        column.label(text=line)
    box.prop(settings, "flatten_spikes")
    operator = box.operator(OBJECT_OT_remesh_quads.bl_idname, text="Repair Folds and Re-run", icon="MOD_SMOOTH")
    operator.source = source_name
    operator.repair_folds = True
    operator.flatten_spikes = settings.flatten_spikes


class VIEW3D_PT_remesh_quads(bpy.types.Panel):
    bl_label = NAME
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = NAME

    def draw(self, context):
        layout = self.layout
        settings = _settings(context)
        column = layout.column(align=True)
        column.enabled = _job is None
        for name in MAIN_PARAMETERS:
            column.prop(settings, name)
        header, body = layout.panel(ID + "_advanced", default_closed=True)
        header.label(text="Advanced")
        if body is not None:
            body.enabled = _job is None
            for name in ADVANCED_PARAMETERS:
                body.prop(settings, name)

        if _job is not None:
            text = "Cancelling..." if _job.cancel_requested else \
                "%d%% %s" % (round(_job.fraction * 100), _job.step)
            layout.progress(factor=_job.fraction, type="BAR", text=text)
            layout.operator(OBJECT_OT_remesh_quads_cancel.bl_idname, icon="CANCEL")
        else:
            layout.operator(OBJECT_OT_remesh_quads.bl_idname, icon="MOD_REMESH")

        # A failed or cancelled last run stays on show until the next run;
        # otherwise the active object's own report, if it is a result.
        status, message, report = _last_status, _last_message, _last_report
        obj = context.active_object
        if status in ("", "ok") and obj is not None and REPORT_PROPERTY in obj:
            try:
                report = json.loads(obj[REPORT_PROPERTY])
                status, message = report.get("status", ""), report.get("message", "")
            except ValueError:
                pass
        if status not in ("", "running"):
            box = layout.box()
            box.label(text=report_rows.STATUS_TEXT.get(status, status),
                      icon="CHECKMARK" if status == "ok" else "ERROR")
            if message and status != "ok":
                box.label(text=message)
            if report:
                for label, value, problem in report_rows.summary_rows(report):
                    row = box.row()
                    row.alert = problem
                    row.label(text=label)
                    row.label(text=value)
                # Full width, so a sentence is not cut off at the value column.
                column = box.column(align=True)
                for text, first, problem in report_rows.warning_lines(report):
                    row = column.row()
                    row.alert = problem
                    icon = ("ERROR" if problem else "INFO") if first else "BLANK1"
                    row.label(text=text, icon=icon)
                source_name = obj.get(SOURCE_PROPERTY, "") if obj is not None else ""
                if report_rows.offers_fold_repair(report) and source_name:
                    _draw_fold_offer(box, settings, source_name)

        preferences = _preferences(context)
        _draw_build(layout, worker.find_executable(bpy.path.abspath(preferences.executable) if preferences else ""))


classes = (
    RemeshPreferences,
    RemeshSettings,
    OBJECT_OT_remesh_quads,
    OBJECT_OT_remesh_quads_cancel,
    VIEW3D_PT_remesh_quads,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    setattr(bpy.types.Scene, ID, PointerProperty(type=RemeshSettings))


def unregister():
    if _job is not None:
        _job.cleanup()
    delattr(bpy.types.Scene, ID)
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
