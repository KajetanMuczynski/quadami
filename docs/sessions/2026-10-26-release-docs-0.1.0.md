# 2026-10-26 · Claude (cloud thread "Finish release PR for 0.1") · Release docs for 0.1.0
- Repos/commits: quadami master `aaa9b4e`
- Did: CHANGELOGS 0.1.0 section (adds the changes merged after `0e9bec8`); README, user guide §2,
  addon/README and the website home point to the Releases page, CI artifacts moved to "Test builds
  between releases"; gallery intro and limits note that thin/missing-part warnings exist since
  `0e9bec8`; placeholder (TBD) for the folds/flips statement in docs/releases/0.1.0.md.
- Found: the gallery was generated at `0e9bec8`. Not regenerated: its folded-face numbers would
  change with whatever the folds/flips bisect decides, so it belongs on the final release commit
  (RELEASING step 3).
- Measured: nothing measured.
- Next: replace the TBD line after the bisect (the tag is refused until then); regenerate the
  gallery at the release commit.
