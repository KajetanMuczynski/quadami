# 2026-10-22 · cloud thread "Wrong genus after libQEx" · genus re-measured and its cause by intervention
- Repos/commits: master ae27301 (libQEx default, #78 fan cuts); core change: per-island handle count in
  the report and a panel warning (TODO 2.22).
- Did: `ab.py --replicates 5` on corpus, closed, sculpt, brush, Kajetan's sculpts, stress jitter/folds and
  six external meshes with handles; the same with `--extractor trace` (our tracer on the same map); a
  probe build (not merged; patch in project files) that counts 2g after every extraction step, and one
  that dumps T with its continuous and rounded map for the holonomy script of the seam-lattice thread.
  New `tools/bench/genus_report.py`. Core: `RemeshedIsland` carries input and output 2g
  (`twiceHandleCount`, the libQEx handle check's count), the report gets `islands.handles_lost /
  handles_added / handles_changed` (interface 1.7) and a warning; the panel a **Handles** row.
- Found: extra handles are gone with libQEx (0 of 900 corpus + closed; the tracer on the same maps
  adds 53). Every remaining wrong genus is a lost handle, and the map loses it: 96 % are already
  missing from libQEx's raw output, 4 % go in `repairTopology`, none in `fixHoles` or the cleanup
  passes, and the tracer loses the same ones. demosthenes (the "undetermined" 11 of #50) is map-bound:
  T has 0.7–1.5 quad edges around a tunnel the input has 2.8 around at 40k, and the continuous map
  already rounds it to 0 or 2. Crash: Happy Buddha u1 at 10k aborts inside libQEx (heap corruption);
  handed to the libQEx thread. ENVELOPE §3.3's closed-only χ rule would miss demosthenes (open).
- Measured (6 replicates): wrong genus corpus 24/432 (was 67), closed 2/468 (was 8), brush 1/144,
  stress_jitter 10/48 (both only on tracer-fallback islands), sculpt 36/252 and private sword 72/96 (all
  non-manifold input), head + cube 0/36, external 56/72 (Happy Buddha −92…−99 of 104). The core's count
  equals `metrics.py`'s on every replicate-run checked; outputs byte-identical to master.
  Details: research/genus-libqex-2026-10-22.md (project files).
- Next: TODO 2.22 (a) local sizing at short handles, first on demosthenes 10k–40k and the boot at 10k;
  then (b)–(d). 1.14 stays open (lost plus added on one island).
