# 2026-11-01 · Claude (cloud thread) · Make edge loops close instead of spiral
- Repos/commits: research commits on feat/tube-ring-soft (no PR), the 10-31 prototype rebased on master 2b858a3
- Did: checked the field twist on Blender-style cylinders (none); listed the continuous shift of detected tube
  rings; A/B (ab.py, 5 replicates) of "constrain only rings under 1 row" (V1) and of a soft ring penalty
  (`--ring-soft`, 0.1 and 0.3) against master.
- Found: near-closed rings mostly round to 0 already, so V1 keeps few of the prototype's gains; the soft penalty
  removes the prototype's needle quads but the gain stays modest (external closed-edge share 0.131 -> 0.15,
  longest loop 0.077 -> 0.072) for +15 % time and +3-4 % folds.
- Decision proposed: no default or option; reuse the ring constraint for closed guides (TODO 5.7).
- Next: research/handoff-tube-rings-2026-11-01.md (shared folder).
