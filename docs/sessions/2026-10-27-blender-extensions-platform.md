# 2026-10-27 · Claude (cloud thread) · Blender Extensions platform readiness
- Repos/commits: core 01774c2 (master), release v0.1.0 (2a0ef4b)
- Did: read the extensions.blender.org terms (updated 10 August 2026), the moderation guidelines and
  canned responses, the manual's add-on and wheel pages, and Blender 5.2.2's manifest validator
  (`bl_pkg/cli/blender_ext.py`, from the `bpy` 5.2.2 wheel). Ran the validator on the three v0.1.0
  zips. Added `copyright` (every holder whose code is in the bundled program, terms 1.3) and a
  contact address to `maintainer` in the manifest; TODO G15.
- Found: the zips are valid extensions (manifest, `platforms` per zip, `files` permission, no
  `__pycache__`, no network use). The open question is the bundled executable: terms 3.6 want the
  extension's source "fully written in Python" and forbid "external, pre-compiled packages";
  compiled code on listed extensions comes as PyPI wheels (mmgpy). No rule or ruling on an
  extension that ships its own program was found. `worker._usable()` sets the executable bit inside
  the extension folder, which fails in a read-only System repository.
- Measured: the validator accepts all three v0.1.0 zips and a zip built from this branch, and
  rejects a copyright entry without a year (negative control). Nothing else measured.
- Next: Kajetan asks the moderators about the bundled program, or picks a wheel route (TODO G15).
