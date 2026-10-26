# 2026-10-26 · cloud thread "Write release notes for 0.1" · release notes 0.1.0
- Repos/commits: master 988ec9e (after #87, #100, #101).
- Did: filled `docs/releases/0.1.0.md` (no placeholders left; its maintainer note no longer contains the word
  `ci/release.py` refuses, which the old draft did): thin-parts fixes (#78, #82), the Handles, Missing parts,
  collapsed-island and thin-part warnings (#98, #100), opt-in fold repair, build line; known limits restated.
  Added the thin-part warning row to USER-GUIDE §6 and fixed README's "panel does not warn yet".
- Checked: `release.version_notes('0.1.0', False)` passes; site builds offline.
- Not done (release PR, RELEASING step 3): CHANGELOGS section for 0.1.0, install docs pointing to Releases,
  gallery regeneration. Every number comes from merged session logs; nothing new measured.
