# 2026-11-27 · Claude (cloud thread) · Guide loops: the kink falsifier fired; decimation is what breaks rings
- Repos/commits: exp/guide-ring-cycles (experiment, no PR), fd27a1c..this commit. Arms of ad78219, both sides the
  same commit, ab.py, plain + 5 float32 replicates. Plan and the prediction written before its test:
  `2026-11-27-guide-kinks-handoff.md`. Private head in the cloud workspace only; `ext_knight.obj` (public, hand-
  downloaded, from Kajetan) and `ext_washington` (external set) are not committed either.
- Did:
  - Metric: the core prints the chain's turn in the surface (`guide chain turns`, angle sums of T's triangles on one
    side of the chain); `ring_events.py` counts kinks with it and splits loops by kink count.
  - Stub report (`guide stubs`): the short hard edges the remesh leaves, by what refuses their collapse.
  - One arm, `guide_stubs=collapse`, default off, output unchanged when off (192 of 192 runs byte-identical to the
    earlier commit, disprover): hard-line edges under 0.4 of the target collapse also in a face with the line's next
    edge and where a neighbouring edge would grow past the limit (up to twice it), plus one pass after the last iteration.
- Measured, endpoint fixed before the runs (closes by `loop_gate.py`, 6k and 10k):
  - Kinks go down: on the first solve's chains of all paired runs 188 -> 131 (cut rings), 157 -> 96 (staircase),
    27 -> 13 (head) [disprover's count].
  - Closing does not go up: cut rings 66 -> 61 of 96, staircase twins 67 -> 68 of 96, head 25 -> 23 of 36; 158 -> 152
    of 228. 12 cases up, 16 down, 10 equal (sign test p = 0.57); bootstrap interval of the difference -23 to +11.
    Loops whose kinks were removed close 34 -> 34 of 42; loops still kinked 27 -> 17 of 47 (p = 0.04, unadjusted).
  - So the falsifier fired: removing kinks this way does not raise closing at 6k/10k. Kink count is a property of
    the ring (70 of 76 arm-ring-target strata are all kink-free or all kinked); it marks hard rings and is not
    shown to cause their gaps. Baseline split with the surface metric: none 72 of 75, 1-4 47 of 71, 5 or more
    10 of 18 on the single rings; no split on the head (13 of 17, 11 of 13).
- Measured, not the endpoint (3k was a readout where I expected nothing):
  - Head inputs at 3k, where the 139k-triangle input is decimated before the remesh: 36 -> 67 of 114 (cut 12 -> 23,
    staircase 19 -> 33, head 5 -> 11); 13 cases up, 2 down, 4 equal (sign test p = 0.007). Runs in which T has no
    closed smooth guide chain: 30 -> 3. Post hoc.
  - Prediction written and pushed before the test (9c771c1): the same on other decimated inputs.
    Knight torso (65k triangles, 6 cut rings + 6 staircase twins, 750 and 1400 quads): 20 -> 88 of 144; 20 cases up,
    1 down, 3 equal. Confirmed. Washington bust (150k, 2 + 2 rings, 1500 and 3000): 0 -> 0 of 48. Not confirmed,
    and not informative: no run of either arm has a closed smooth guide chain in T at all.
  - Decimation off (`decimate=off`), the repeat Kajetan asked for: knight torso 20 -> 100 of 144 (cut 8 -> 62,
    staircase 12 -> 38); bust 0 -> 6 of 48. On the head at 3k it was 12 -> 29 (cut), 19 -> 16 (staircase).
  - loopcheck at 1.0 h_loc agrees with the gate in every run of these arms (0 disagreements).
- Why (stub report, [M] on 5 traced runs, [I] as an explanation): the decimation keeps the guide's edges, so the
  guide stays at input resolution inside a coarser mesh; the remesh then refuses to collapse its edges because a
  neighbouring edge would become too long, or because the edge shares a face with the line's next edge.
- Disprover (fresh worker, own scripts): endpoint numbers hold with both checks; no bug in the turn code (read,
  and surface turn equals the 3D turn on smooth rings to 0.5-0.7 degrees); `ring_events.py` also reads 3k folders
  when given a whole set (fixed by passing targets apart); a debug line cut by progress text broke the parser on
  the bust logs (fixed: skipped). It did not check 3k, the knight or the bust.
- Not done: no fix proposed for master. ring26 is unchanged (kept corners). ctest and lint: thirdparty and
  tools only, `ci/lint.sh` not applicable; the default path is byte-identical. Folds, quad counts and run time
  of the arms not compared (`--no-metrics`).
- What would falsify: the stub arm failing on further decimated sculpts with a guide that survives into T; a
  gain at 6k/10k above the +11 bound in a larger run; the knight result not holding under a fresh disprover.
- Not re-checked: why the bust loses its guides; whether the loss on still-kinked loops at 6k/10k is real;
  cost of `decimate=off` in time and memory; one sculpt confirms the prediction, one is silent.
