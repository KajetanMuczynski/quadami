# 2026-11-05 · Claude (cloud) · Fix crash on faces with repeated vertices
- Repos/commits: core on master b6da17f, branch fix/repeated-vertex-faces
- Did: traced the "free(): invalid size" abort the speed thread's fuzzing found (closed UV sphere plus
  `f 1 1 2`, `f 5 6 5`, `f 7 8 9 7`; targets 1k and 3k, default extractor). Faces that use a vertex
  twice are now removed first in the input cleanup (`removeRepeatedCornerTriangles`, report key
  `input_repeated_corner_triangles`, add-on Input cleanup row), and `extractWithQex` rejects such a
  triangle so the island falls back instead of corrupting the heap. Tests in `input_sanitizer_test`
  and `qex_guard_test`.
- Found:
  - Cause (checked with ASan): OpenMesh `add_face(a, a, b)` in `extractWithQex` links halfedges through
    an invalid handle and writes 8 bytes before the edge array (`ArrayKernel::set_next_halfedge_handle`).
  - How the face got there (checked): `cleanInput` drops repeated corners only in parts where it merged
    an edge, so an input triangle (a, a, b) passed every cleanup step, and the isotropic remesh kept it
    (the extractor received a triangle with input-numbering corners 3, 3, 5).
  - Add-on (checked with bpy 5.2.3): `Mesh.from_pydata` without `validate()` keeps such faces, and the
    add-on's export (`evaluated.to_mesh()` + `loop_triangles`) passes them on (3 of 964 triangles).
    BMesh refuses them (`faces.new` raises) and `Mesh.validate()` removes them. Blender importers and
    Edit Mode round trips were not checked.
- Measured: with the fix, degen.obj gives 1026 quads at 1k and 3014 at 3k, 0 non-quads, "removed 3
  faces". Extractor guard alone (sanitizer off): 1046 / 2932 quads, no crash. Without both, both new
  tests hit the ASan heap overflow.
- Next: none.
