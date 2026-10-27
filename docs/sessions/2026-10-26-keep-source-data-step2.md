# 2026-10-26 · cloud thread · Result keeps materials, groups, colours, marks, UVs (step 2)
- Repos/commits: master b0a7efe (after step 1, #110)
- Did: add-on `transfer.py` and a collapsed *Keep from Original* panel section (TODO G9). On by default:
  materials (nearest source face per result face), vertex groups and colour attributes (Data Transfer
  modifier, nearest face interpolated, baked with `new_from_object`), sharp/crease/seam marks (each marked
  line becomes a shortest edge path through waypoints two result edges apart, off-line edges dearer).
  Opt-in: UV maps (each face takes the UV island under its centre; corners from the nearest point of that
  island, or the centre triangle's map extended where that point is beyond the border or across a seam
  inside the island). A failure becomes a report warning, never a lost result. Headless test covers all.
- Found: per-edge copies of marks (Data Transfer edge mappings, or "edge lies along a marked edge") break
  lines into pieces: precision 22-86 % on a sphere equator and cube edges, and a broken seam does not cut UV
  islands. Paths keep every island count (sphere 2, cube 6, at 1k/4k/16k quads), cube marks within 0.01-0.06
  of an edge length of the crease. Data Transfer's islands_precision changed no UV at all (bpy 5.2.2); its
  island handling comes from the source's seam marks, and is slow (7-27 s at 5k quads).
- Measured (bpy 5.2.2, ad-hoc scripts, core output unchanged): UV faces distorted > 4x in area against the
  source face under them, 5000 quads, Smart UV Project on goathead/lion/boot/leeperrysmith/scifihelmet plus
  cylinder and UV sphere own maps: ours 0/0/0/0/1.4/0.8/5.6 %; Data Transfer with source seams
  5.8/1.9/3.1/4.1/12.2/4.2/14.0 %, without seams up to 56 %. Time at 50k quads: 1.5 s from a 330k-triangle
  source, 4 s from 1.3M; UVs another 3 s / 10 s. Materials, groups, colours exact on a test sphere.
- Next: merge only after the v0.1.0 tag. Later: quads that follow material borders and marked lines
  (TODO 5.4) make all of this exact; face sets and shape keys are not copied.
