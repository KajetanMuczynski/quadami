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

import collections
import json
import os

import blf
import bpy
import numpy as np
from bpy.props import BoolProperty, EnumProperty, FloatProperty, IntProperty, PointerProperty, StringProperty

from . import raise_hint
from . import report as report_rows
from . import transfer, worker
from .names import ID, NAME

REPORT_PROPERTY = ID + "_report"
# Object property (a pointer, so it survives renames): the object a result was
# made from. Remesh and "Repair Folds and Re-run" on a result run on it.
SOURCE_PROPERTY = ID + "_source"
# On a source object: its runs, for the Raise Target Quads hint (raise_hint.py).
HISTORY_PROPERTY = ID + "_history"
# Above this ratio of the largest to the smallest scale axis the panel and the
# report warn that the quads come out stretched (the core works in object space).
UNEVEN_SCALE_RATIO = 1.05

# The one running job and the last result, shared by the operator and the panel.
_job = None
_cancel_from_panel = False
_last_status = ""
_last_message = ""
_last_report = None
# The last run of this session, for Copy Report: the panel's settings, the
# input's triangle count, the worker's last lines and the result's name.
_last_run = None


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
                                   "flipped quads, about twice as long as Fast"),
            ("fast", "Fast", "Solves on a coarser triangle mesh: quicker, rougher quads"),
        ),
        default="quality",
        description="Triangle mesh the quads are solved on. The quad count is the same either way")
    quad_smoothing: EnumProperty(
        name="Quad Smoothing",
        items=(
            ("laplacian", "Standard", "Evens out the quads by moving them in 3D, which rounds off "
                                      "ridges, swirls and sharp edges"),
            ("tangential", "Keep Ridges", "Evens out the quads by sliding them along the surface: "
                                          "on test sculpts a third to a half less of the shape lost, "
                                          "but sometimes a few more flipped quads in deep grooves "
                                          "(experimental)"),
        ),
        default="laplacian",
        description="How the quads are evened out after they are found")

    # Not a CLI parameter: which marks go to the remesher as marked edges
    # (`l` lines of the input OBJ, RemeshRequest::featureEdges).
    follow_marks: EnumProperty(
        name="Follow Marks",
        items=(
            ("SHARP", "Sharp", "Edges marked Sharp"),
            ("CREASE", "Crease", "Edges with a subdivision crease"),
            ("SEAM", "Seam", "UV seams"),
        ),
        options={"ENUM_FLAG"},
        # On (Kajetan, 2026-10-27). An FBX or OBJ import often marks nearly
        # every edge Sharp (93 % of a sculpt's edges checked): such floods are
        # not sent (_flooded_marks) and the panel shows the counts.
        default={"SHARP"},
        description="Marked edges the result keeps a row of edges along, whatever their "
                    "angle (Sharp Edge decides only for unmarked edges)")

    # Not a remesh parameter: a choice next to Repair Folds and Re-run.
    flatten_spikes: BoolProperty(
        name="Also Flatten Needle Spikes", default=False,
        description="Where a fold sits on a needle-like spike and cannot be smoothed out, "
                    "lower the spike's tip to its base. This changes the shape: the panel "
                    "says how many tips moved and how far")

    # Not remesh parameters: what the result copies from the original
    # (transfer.py). The quads do not follow seams, so UVs are opt-in.
    keep_attributes: BoolProperty(
        name="Materials, Groups, Marks", default=True,
        description="Copy the original's materials, vertex groups, colour attributes and "
                    "sharp, crease and seam marks onto the result. Material borders zigzag "
                    "along the quads; marks follow the quads nearest to the marked line")
    keep_uvs: BoolProperty(
        name="UV Maps", default=False,
        description="Copy the original's UV maps. Faces that cross a UV seam reach a little "
                    "past the island's border (on test meshes with many seams up to 6 % of the "
                    "faces come out distorted). For a fresh map, keep the seams (above) and unwrap "
                    "the result again instead")

    def parameters(self):
        return {name: getattr(self, name) for name in worker.PARAMETER_FLAGS}


# How the panel shows the parameters: the main ones, then a collapsed
# "Advanced" section. Every parameter appears exactly once (blender_smoke.py).
MAIN_PARAMETERS = ("target_quads", "adaptivity", "sharp_edge_degrees")
ADVANCED_PARAMETERS = ("resolution", "anisotropy", "edge_scaling", "smooth_normal_degrees", "quad_smoothing")
KEEP_OPTIONS = ("keep_attributes", "keep_uvs")


def _edge_vertices(mesh):
    edges = np.empty(len(mesh.edges) * 2, dtype=np.int32)
    mesh.edges.foreach_get("vertices", edges)
    return edges.reshape(-1, 2)


def _marked_pairs(mesh, kinds):
    """Vertex pairs (E, 2) of the mesh's edges that carry any of the marks
    in kinds (follow_marks values)."""
    marks = transfer.marked_edges(mesh)
    wanted = np.zeros(len(mesh.edges), dtype=bool)
    for kind in kinds:
        values = marks.get(kind.lower())
        if values is not None:
            wanted |= values > 0
    return _edge_vertices(mesh)[wanted]


MARK_NAMES = (("sharp", "Sharp"), ("crease", "Crease"), ("seam", "Seam"))


def _mark_counts(mesh):
    """{kind: number of edges} of mesh's marks, marked kinds only."""
    marks = transfer.marked_edges(mesh) if len(mesh.edges) else {}
    return {kind: int(np.count_nonzero(values > 0)) for kind, values in marks.items()}


def _flooded_marks(mesh, counts, kinds):
    """The followed kinds (follow_marks values) that flood the mesh
    (transfer.flooded)."""
    return [kind for kind in kinds if transfer.flooded(len(mesh.edges), counts.get(kind.lower(), 0))]


def _flood_warning(flooded):
    if not flooded:
        return None
    labels = dict((kind.upper(), label) for kind, label in MARK_NAMES)
    return ("Follow Marks left out %s: more than half of the edges carry it, as after an FBX or OBJ "
            "import, rather than the edges to keep; Keep from Original does not copy it either"
            % " and ".join(labels[kind] for kind in flooded))


def _mark_line(obj, kinds):
    """Panel line with how many edges of obj carry each mark, and whether
    a followed mark floods the mesh."""
    if obj is None or obj.type != "MESH":
        return None
    counts = _mark_counts(obj.data)
    if not counts:
        return None
    text = "Marked: " + ", ".join("%s %d" % (label, counts[kind]) for kind, label in MARK_NAMES if kind in counts)
    flood = bool(_flooded_marks(obj.data, counts, kinds))
    if flood:
        text += " (most edges: not followed)"
    return text, flood


def _mesh_arrays(context, obj, follow_marks=()):
    """Vertex positions and triangles of the evaluated mesh, in object space,
    whether most of its faces are smooth shaded, the vertex pairs of the
    edges carrying the marks in follow_marks, and the marks left out because
    they flood the mesh (_flooded_marks)."""
    depsgraph = context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    try:
        mesh.calc_loop_triangles()
        positions = np.empty(len(mesh.vertices) * 3, dtype=np.float32)
        mesh.vertices.foreach_get("co", positions)
        triangles = np.empty(len(mesh.loop_triangles) * 3, dtype=np.int32)
        mesh.loop_triangles.foreach_get("vertices", triangles)
        smooth_faces = np.empty(len(mesh.polygons), dtype=bool)
        mesh.polygons.foreach_get("use_smooth", smooth_faces)
        marked = np.empty((0, 2), dtype=np.int32)
        flooded = []
        if follow_marks:
            # Smooth by Angle (Shade Auto Smooth) marks every edge above its
            # angle Sharp on the evaluated mesh. Where the modifiers keep the
            # edges as they are, the user's own marks are the original's.
            base = obj.data
            same = len(base.vertices) == len(mesh.vertices) and np.array_equal(
                _edge_vertices(base), _edge_vertices(mesh))
            source = base if same else mesh
            flooded = _flooded_marks(source, _mark_counts(source), follow_marks)
            marked = _marked_pairs(source, [kind for kind in follow_marks if kind not in flooded])
    finally:
        evaluated.to_mesh_clear()
    smooth = bool(smooth_faces.size) and 2 * np.count_nonzero(smooth_faces) >= smooth_faces.size
    return positions.reshape(-1, 3), triangles.reshape(-1, 3), smooth, marked, flooded


def _source(obj):
    """The object a result was made from, or None if obj is not a result or
    its source was deleted."""
    source = getattr(obj, SOURCE_PROPERTY, None) if obj is not None else None
    return source if source is not obj else None


def _source_problem(context, source):
    """Why the source object cannot be remeshed now, or None. Only an object
    the depsgraph evaluates can be read: hidden with H is fine, disabled in
    viewports or outside the view layer is not."""
    if source.type != "MESH":
        return "\"%s\" is not a mesh" % source.name
    if source.name not in context.view_layer.objects:
        return "\"%s\" is not in this view layer" % source.name
    if source.hide_viewport:
        return "\"%s\" is disabled in viewports (Outliner: monitor icon)" % source.name
    return None


def _scale_ratio(obj):
    """Largest over smallest stretch of the object's world matrix (singular
    values, so shear from a rotated parent counts too); 0 if degenerate."""
    stretch = np.linalg.svd(np.array(obj.matrix_world.to_3x3()), compute_uv=False)
    return float(stretch[0] / stretch[-1]) if stretch[-1] > 0 else 0.0


def _uneven_scale_warning(obj):
    ratio = _scale_ratio(obj)
    if ratio != 0.0 and ratio <= UNEVEN_SCALE_RATIO:
        return None
    return ("\"%s\" is scaled unevenly (%s), so the quads come out stretched. Apply the "
            "scale first: select it, Ctrl+A > Scale, then remesh."
            % (obj.name, "%.2f : 1" % ratio if ratio else "flattened to zero"))


def _place_like(obj, source):
    """Give obj the source's parent, local transform and collections, so it
    sits where the source is and moves with the source's parent."""
    obj.parent = source.parent
    if source.parent is not None:
        obj.parent_type = source.parent_type
        obj.parent_bone = source.parent_bone
        obj.parent_vertices = source.parent_vertices
        obj.matrix_parent_inverse = source.matrix_parent_inverse.copy()
    obj.rotation_mode = source.rotation_mode
    for name in ("location", "rotation_euler", "rotation_quaternion", "rotation_axis_angle", "scale",
                 "delta_location", "delta_rotation_euler", "delta_rotation_quaternion", "delta_scale"):
        setattr(obj, name, getattr(source, name))


def _keep(context, source, result, report, settings):
    """Copy what the settings ask for from the source onto the result; a
    failure costs the copy, never the result."""
    if not (settings.keep_attributes or settings.keep_uvs):
        return
    try:
        if settings.keep_attributes:
            report["kept"] = transfer.transfer(context, source, result, uvs=settings.keep_uvs)
        else:
            report["kept"] = transfer.transfer_uvs(context, source, result)
    except Exception as error:  # noqa: BLE001 - any failure here must not lose the result
        report.setdefault("warnings", []).append("Could not copy data from the original: %s" % error)
    result[REPORT_PROPERTY] = json.dumps(report)


def _track(source, key, result, report):
    """Add the run to the source's history and give the result's report the
    Raise Target Quads hint for it (report["raise"])."""
    history = raise_hint.record(raise_hint.load(source.get(HISTORY_PROPERTY)), key, report)
    if history is None:
        return
    source[HISTORY_PROPERTY] = json.dumps(history)
    hint = raise_hint.hint(history, worker.MAXIMUM_TARGET_QUADS)
    if hint:
        report["raise"] = hint
        result[REPORT_PROPERTY] = json.dumps(report)


def _build_object(context, source, outcome, smooth, warnings=(), addon_settings=None):
    """The result object, linked next to the source and placed like it,
    smooth shaded if the source mostly was. No edges are marked sharp: no
    angle threshold suits both sculpts and hard surfaces (at 30 degrees 7-21 %
    of the edges of 5k-quad sculpts, at 90 a few stray edges of a cube)."""
    mesh = bpy.data.meshes.new(source.name + "_remesh")
    offsets = np.concatenate(([0], np.cumsum(outcome.face_sizes, dtype=np.int64)))
    indices = outcome.face_indices.tolist()
    faces = [indices[offsets[i]:offsets[i + 1]] for i in range(len(outcome.face_sizes))]
    mesh.from_pydata(outcome.positions.tolist(), [], faces, shade_flat=not smooth)
    changed = mesh.validate()
    mesh.update()
    obj = bpy.data.objects.new(source.name + "_remesh", mesh)
    _place_like(obj, source)
    for collection in source.users_collection or [context.scene.collection]:
        collection.objects.link(obj)
    report = dict(outcome.report)
    report["warnings"] = list(report.get("warnings", [])) + list(warnings)
    if addon_settings:
        report["addon"] = dict(addon_settings)
    if changed:
        report["warnings"].append("Blender's mesh validation changed the result")
    obj[REPORT_PROPERTY] = json.dumps(report)
    setattr(obj, SOURCE_PROPERTY, source)
    return obj, report


def _show_result(context, result, hidden):
    """Select the result and hide the objects it replaces, in the viewport
    and in renders (F12 would otherwise draw them on top of each other)."""
    for selected in list(context.view_layer.objects.selected):
        selected.select_set(False)
    for obj in hidden:
        if obj is not None and obj.name in context.view_layer.objects:
            obj.hide_set(True)
            obj.hide_render = True
    result.select_set(True)
    context.view_layer.objects.active = result


def _redraw(context):
    screen = context.screen
    if screen is None:
        return
    for area in screen.areas:
        if area.type == "VIEW_3D":
            area.tag_redraw()


class OBJECT_OT_remesh_quads(bpy.types.Operator):
    """Remesh the active mesh into quads in a separate process (Esc cancels). On a result, remesh its source again"""
    bl_idname = "object." + ID + "_remesh"
    bl_label = "Remesh"
    bl_options = {"REGISTER", "UNDO"}

    # Set by the "Repair Folds and Re-run" button: the fold repair on, which
    # needs a source to run on.
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
        global _job, _cancel_from_panel, _last_status, _last_message, _last_report, _last_run
        preferences = _preferences(context)
        configured = bpy.path.abspath(preferences.executable) if preferences else ""
        executable = worker.find_executable(configured)
        if executable is None:
            self.report({"ERROR"}, "Remesher executable not found; set it in the %s preferences" % NAME)
            return {"CANCELLED"}
        obj = context.active_object
        self._replaced = None
        # On a result, remesh its unchanged source and hide this result,
        # never the result itself (that would remesh a remesh).
        source = _source(obj)
        if source is not None:
            problem = _source_problem(context, source)
            if problem:
                self.report({"ERROR"}, "Cannot remesh the original: %s. Show it, or clear "
                            "Source in the %s panel to remesh this result itself" % (problem, NAME))
                return {"CANCELLED"}
            self._replaced = obj.name
            obj = source
        elif self.repair_folds:
            self.report({"ERROR"}, "The original object is gone; select it and remesh again")
            return {"CANCELLED"}
        settings = _settings(context)
        positions, triangles, self._smooth, marked, flooded = _mesh_arrays(context, obj, settings.follow_marks)
        if len(triangles) == 0:
            self.report({"ERROR"}, "The mesh has no faces")
            return {"CANCELLED"}
        parameters = settings.parameters()
        if self.repair_folds:
            parameters["repair_folds"] = True
            if self.flatten_spikes:
                parameters["flatten_spikes"] = True
        # After a run that lost parts, measure this one against that run's
        # quad width too, so the panel can say whether those parts came back.
        self._history_key = raise_hint.input_key(positions, triangles, marked, parameters)
        reference = raise_hint.reference_target(raise_hint.load(obj.get(HISTORY_PROPERTY)), self._history_key,
                                                parameters["target_quads"])
        if reference and (worker.worker_interface(executable) or (0, 0)) >= worker.LOST_PARTS_REFERENCE_INTERFACE:
            parameters["lost_parts_reference"] = reference
        job = worker.RemeshJob(executable, positions, triangles, parameters, marked)
        try:
            job.start()
        except (OSError, ValueError) as error:
            job.cleanup()
            self.report({"ERROR"}, "Could not start the remesher: %s" % error)
            return {"CANCELLED"}
        _job = job
        _cancel_from_panel = False
        _last_status, _last_message, _last_report = "running", "", None
        self._addon_settings = _addon_settings(settings)
        _last_run = {"settings": dict(parameters, **self._addon_settings), "input_triangles": len(triangles),
                     "log": None, "result": None}
        self._source_name = obj.name
        self._warnings = [w for w in (_uneven_scale_warning(obj), _flood_warning(flooded)) if w]
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
        _last_run["log"] = outcome.log
        if not outcome.ok:
            if outcome.log:
                print(NAME + " worker output:\n" + outcome.log)
            level = {"WARNING"} if outcome.status == "cancelled" else {"ERROR"}
            self.report(level, "%s %s: %s" % (
                NAME, report_rows.STATUS_TEXT.get(outcome.status, outcome.status), outcome.message))
            return {"CANCELLED"}
        source = bpy.data.objects.get(self._source_name)
        if source is None:
            # Deleted while the remesher ran.
            _last_status, _last_report = "failed", None
            _last_message = "\"%s\" was deleted during the remesh" % self._source_name
            self.report({"ERROR"}, "%s: %s" % (NAME, _last_message))
            return {"CANCELLED"}
        new_obj, _last_report = _build_object(context, source, outcome, self._smooth, self._warnings,
                                              self._addon_settings)
        _last_run["result"] = new_obj.name
        _keep(context, source, new_obj, _last_report, _settings(context))
        _track(source, self._history_key, new_obj, _last_report)
        _show_result(context, new_obj, (source, bpy.data.objects.get(self._replaced) if self._replaced else None))
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
            if _last_run is not None:
                _last_run["log"] = ""
            self._stop(context)


def _addon_settings(settings):
    """The panel's settings that are not remesher parameters."""
    return {"follow_marks": sorted(settings.follow_marks), "keep_attributes": settings.keep_attributes,
            "keep_uvs": settings.keep_uvs}


def _shown_report(context):
    """(status, message, report, from_last_run) of the result box: a failed
    or cancelled last run stays on show until the next run; otherwise the
    active object's own report, if it is a result."""
    status, message, report = _last_status, _last_message, _last_report
    obj = context.active_object
    if status in ("", "ok") and obj is not None and REPORT_PROPERTY in obj:
        try:
            report = json.loads(obj[REPORT_PROPERTY])
            status, message = report.get("status", ""), report.get("message", "")
        except ValueError:
            return status, message, report, status != ""
        return status, message, report, _last_run is not None and obj.name == _last_run["result"]
    return status, message, report, status != ""


def copy_report_text(context):
    """The Copy Report text for what the result box shows."""
    status, message, report, last = _shown_report(context)
    preferences = _preferences(context)
    executable = worker.find_executable(bpy.path.abspath(preferences.executable) if preferences else "")
    if report and "parameters" in report:
        settings = dict(report["parameters"], **report.get("addon", {}))
    else:
        settings = _last_run["settings"] if last and _last_run else {}
    info = {
        "addon_build": worker.addon_build_id(),
        "worker_build": worker.worker_build_id(executable) if executable else None,
        "blender": bpy.app.version_string,
        "system": report_rows.system_text(),
        "settings": settings,
        "home": os.path.expanduser("~"),
    }
    if last and _last_run:
        info["input_triangles"] = _last_run["input_triangles"]
        info["log"] = _last_run["log"]
    return report_rows.copy_text(status, message, report, info)


class OBJECT_OT_copy_report(bpy.types.Operator):
    """Copy the result box, settings, build, Blender and system versions and the remesher's last output to the clipboard, for a bug report. No mesh data and no user names"""
    bl_idname = "object." + ID + "_copy_report"
    bl_label = "Copy Report"

    @classmethod
    def poll(cls, context):
        return _shown_report(context)[0] not in ("", "running")

    def execute(self, context):
        context.window_manager.clipboard = copy_report_text(context)
        self.report({"INFO"}, "%s report copied: paste it into the bug report form or an e-mail" % NAME)
        return {"FINISHED"}


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


# Room for text in the sidebar, in pixels: `panel` is a row of the panel,
# `box` a row inside the report's box, `icon` what an icon in front of a label
# takes, `pad` the margin Blender keeps in front of a label's text and `space`
# the gap between the columns of a split row.  Characters (measure len) where
# Blender gives no region.
TextRoom = collections.namedtuple("TextRoom", "panel box icon pad space measure")


class OBJECT_OT_set_target_quads(bpy.types.Operator):
    """Set Target Quads to this count; press Remesh to try it"""
    bl_idname = "object." + ID + "_set_target"
    bl_label = "Set Target Quads"
    bl_options = {"INTERNAL"}

    target: IntProperty(min=1, max=worker.MAXIMUM_TARGET_QUADS, options={"HIDDEN", "SKIP_SAVE"})

    @classmethod
    def poll(cls, context):
        return _job is None

    def execute(self, context):
        _settings(context).target_quads = self.target
        self.report({"INFO"}, "Target Quads set to %s: press Remesh to try it" % format(self.target, ","))
        return {"FINISHED"}


class OBJECT_OT_keep_quads(bpy.types.Operator):
    """Hide the Raise Target Quads hint for this original until its mesh or the settings change"""
    bl_idname = "object." + ID + "_keep_quads"
    bl_label = "Keep These Quads"
    bl_options = {"INTERNAL", "UNDO"}

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return obj is not None and REPORT_PROPERTY in obj

    def execute(self, context):
        obj = context.active_object
        try:
            report = json.loads(obj[REPORT_PROPERTY])
        except ValueError:
            return {"CANCELLED"}
        if "raise" in report:
            report["raise"]["dismissed"] = True
            obj[REPORT_PROPERTY] = json.dumps(report)
        source = _source(obj)
        history = raise_hint.load(source.get(HISTORY_PROPERTY)) if source is not None else None
        if history is not None:
            history["dismissed"] = True
            source[HISTORY_PROPERTY] = json.dumps(history)
        _redraw(context)
        return {"FINISHED"}


def _draw_raise_hint(box, hint, room):
    """The Raise Target Quads hint under the warnings, with Set and dismiss."""
    text = raise_hint.text(hint)
    if not text:
        return
    column = box.column(align=True)
    for i, line in enumerate(report_rows.wrap(text, room.box - room.icon - room.pad, room.measure)):
        column.label(text=line, icon="INFO" if i == 0 else "BLANK1")
    if hint["state"] == "back":
        return
    # Beside Set the dismiss is an icon (its tooltip says what it does): two
    # labels side by side would be cut off in a narrow sidebar.
    row = box.row(align=True)
    row.enabled = _job is None
    if hint.get("next"):
        operator = row.operator(OBJECT_OT_set_target_quads.bl_idname, text="Set %s" % format(hint["next"], ","))
        operator.target = hint["next"]
        row.operator(OBJECT_OT_keep_quads.bl_idname, text="", icon="X")
    else:
        row.operator(OBJECT_OT_keep_quads.bl_idname, text="Keep These Quads", icon="X")


def _text_room(context):
    """Blender 4.2 and 5.2 lay a sidebar panel out from the widget unit: tabs
    1 unit (1.4 with 5.x's compact tabs), the panel's background 0.4 unit on
    both sides, panelspace 8 px and a box's boxspace 5 px on both sides, an
    icon 16 + 2 px and a label's text margin 0.4 unit, all times the UI scale.
    Half a unit more is slack for rounding and font hinting.  A label wider
    than its room is cut off with an ellipsis."""
    region = context.region
    system = context.preferences.system
    scale = system.ui_scale
    if region is None or region.width <= 0 or scale <= 0:
        return TextRoom(report_rows.WARNING_WIDTH + 4, report_rows.WARNING_WIDTH + 2, 2, 0, 1, len)
    unit = round(18 * scale) + 2 * system.pixel_size
    tabs = 1.4 if getattr(system, "show_panel_tabs_compact", False) else 1.0
    panel = region.width - (tabs + 0.8 + 0.5) * unit - 2 * int(8 * scale)
    size = context.preferences.ui_styles[0].widget.points * scale

    # Font 0 is Blender's interface font.
    def measure(text):
        blf.size(0, size)
        return blf.dimensions(0, text)[0]

    return TextRoom(panel=max(panel, 10 * size), box=max(panel - 2 * int(5 * scale), 10 * size),
                    icon=round(18 * scale), pad=round(0.4 * unit), space=int(8 * scale), measure=measure)


def _draw_fold_offer(box, settings, room):
    column = box.column(align=True)
    for line in report_rows.fold_offer_lines(room.box - room.pad, room.measure):
        column.label(text=line)
    box.prop(settings, "flatten_spikes")
    operator = box.operator(OBJECT_OT_remesh_quads.bl_idname, text="Repair Folds and Re-run", icon="MOD_SMOOTH")
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
        # The label on its own line: beside the three toggles the sidebar
        # cut it off with an ellipsis.
        column = layout.column(align=True)
        column.enabled = _job is None
        column.label(text="Follow Marks")
        column.row(align=True).prop(settings, "follow_marks")
        line = _mark_line(context.active_object, settings.follow_marks)
        if line:
            room = _text_room(context)
            column = layout.column(align=True)
            column.enabled = _job is None
            text, flood = line
            icon = room.icon if flood else 0
            for i, part in enumerate(report_rows.wrap(text, room.panel - icon - room.pad, room.measure)):
                column.label(text=part, icon=("ERROR" if 0 == i else "BLANK1") if flood else "NONE")
        header, body = layout.panel(ID + "_advanced", default_closed=True)
        header.label(text="Advanced")
        if body is not None:
            body.enabled = _job is None
            for name in ADVANCED_PARAMETERS:
                body.prop(settings, name)
        header, body = layout.panel(ID + "_keep", default_closed=True)
        header.label(text="Keep from Original")
        if body is not None:
            body.enabled = _job is None
            for name in KEEP_OPTIONS:
                body.prop(settings, name)

        if _job is not None:
            text = "Cancelling..." if _job.cancel_requested else \
                "%d%% %s" % (round(_job.fraction * 100), _job.step)
            layout.progress(factor=_job.fraction, type="BAR", text=text)
            layout.operator(OBJECT_OT_remesh_quads_cancel.bl_idname, icon="CANCEL")
        else:
            layout.operator(OBJECT_OT_remesh_quads.bl_idname, icon="MOD_REMESH")

        room = _text_room(context)
        obj = context.active_object
        if obj is not None and obj.type == "MESH":
            # What Remesh will run on: a result shows its source, which the
            # user can clear to remesh the result itself. A result's own
            # report already carries the scale warning.
            is_result = _source(obj) is not None or REPORT_PROPERTY in obj
            if is_result:
                layout.prop(obj, SOURCE_PROPERTY, text="Source")
            warning = None if is_result else _uneven_scale_warning(obj)
            if warning:
                column = layout.column(align=True)
                column.alert = True
                for i, line in enumerate(report_rows.wrap(warning, room.panel - room.icon - room.pad, room.measure)):
                    column.label(text=line, icon="ERROR" if i == 0 else "BLANK1")

        status, message, report, _ = _shown_report(context)
        if status not in ("", "running"):
            box = layout.box()
            box.label(text=report_rows.STATUS_TEXT.get(status, status),
                      icon="CHECKMARK" if status == "ok" else "ERROR")
            if message and status != "ok":
                box.label(text=message)
            if report:
                factor, rows = report_rows.summary_columns(report_rows.summary_rows(report), room.box, room.space,
                                                           room.pad, room.measure)
                for labels, values, problem in rows:
                    split = box.split(factor=factor)
                    split.alert = problem
                    for column, lines in ((split.column(align=True), labels), (split.column(align=True), values)):
                        for line in lines:
                            column.label(text=line)
                # Full width, so a sentence is not cut off at the value column.
                column = box.column(align=True)
                for text, first, problem in report_rows.warning_lines(report, room.box - room.icon - room.pad,
                                                                      room.measure):
                    row = column.row()
                    row.alert = problem
                    icon = ("ERROR" if problem else "INFO") if first else "BLANK1"
                    row.label(text=text, icon=icon)
                if report.get("raise") and status == "ok":
                    _draw_raise_hint(box, report["raise"], room)
                if report_rows.offers_fold_repair(report) and _source(obj) is not None:
                    _draw_fold_offer(box, settings, room)
            box.operator(OBJECT_OT_copy_report.bl_idname, icon="COPYDOWN")

        preferences = _preferences(context)
        _draw_build(layout, worker.find_executable(bpy.path.abspath(preferences.executable) if preferences else ""))


classes = (
    RemeshPreferences,
    RemeshSettings,
    OBJECT_OT_remesh_quads,
    OBJECT_OT_remesh_quads_cancel,
    OBJECT_OT_set_target_quads,
    OBJECT_OT_keep_quads,
    OBJECT_OT_copy_report,
    VIEW3D_PT_remesh_quads,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    setattr(bpy.types.Scene, ID, PointerProperty(type=RemeshSettings))
    setattr(bpy.types.Object, SOURCE_PROPERTY, PointerProperty(
        type=bpy.types.Object, name="Source",
        description="The object this result was made from. Remesh and Repair Folds run on it "
                    "and replace this result. Clear it to remesh this result itself"))


def unregister():
    if _job is not None:
        _job.cleanup()
    delattr(bpy.types.Object, SOURCE_PROPERTY)
    delattr(bpy.types.Scene, ID)
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
