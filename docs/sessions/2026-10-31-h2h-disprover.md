# 2026-10-31 · cloud thread · Disprover pass on the upstream head-to-head
- Repos/commits: v0.2.0 (57c19f9) and upstream tag 1.2.0 (d9ef96b), both rebuilt from source; Blender QuadriFlow via bpy 5.2.2.
- Did: re-ran ours vs upstream (defaults, count-matched, five other settings) and ours vs Blender QuadriFlow with an own driver (no ab.py, other replicate seeds, per-run count matching) and own metric code (non-quads, duplicates, non-manifold, three fold tests).
- Found: count, non-quad and non-manifold claims reproduce; "no duplicated faces" is false (tiny 2-quad pillow islands in 52/240 external runs); upstream at adaptivity 0 has fewer self-crossing quads per model than ours (6 replicates); the open-source fold factor is 2.5-9x against Blender QuadriFlow depending on the fold test, not 7-25x.
- Report: research/h2h-disprover-2026-10-31.md (project files). Next: wording changes for the website claims; QuadriFlow/Instant Meshes re-check needs a build approval.
