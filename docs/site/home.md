<!--
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) 2026 Quadami contributors
Home page of the website (tools/site/site.json). The marker below is replaced by the download
link of the newest release when the site is built.
-->

# Quadami

Quadami is an automatic quad remesher for Blender. Select a mesh (a sculpt, a scan, a dense
triangle model), set how many quads you want, press **Remesh**, and you get a new object made of
quads that follows the shape, with edge loops that run along it. It is free software
(GPL-3.0-or-later) and runs on Linux, Windows and macOS (Apple silicon; experimental and untested), in Blender 4.2 or newer.

<!-- site:latest-release -->

![Koala sculpt: input left, Quadami output right](../gallery/img/koala.webp)

## Status

**Version 0.2.0, a release with known limits.** It works on the sculpts we test with, but expect rough edges.
After every run the panel reports quads against the target, non-quads, holes, folded faces and the
time, so a bad result shows itself. Put the panel's build line in every bug report.

What it does not do well yet, measured on our test meshes ([results](../gallery/README.md)):

- **Thin parts** (leaves, vines, cloth, blade edges) thinner than one quad can lose their shape or
  pieces. The panel warns about thin and missing parts; raise Target Quads, or thicken the part.
- **Folded sculpt input** (Twist, Rotate, Pinch brushes) is where most remaining defects on our test
  sculpts sit. **Repair Folds and Re-run** repairs some of these spots, not all.
- **Hard-surface models**: sharp edges come out slightly rounded and cone tips are cut off; a few
  triangles or pentagons along sharp rims are normal. Mark edges Sharp to keep quad edges along
  them (**Follow Marks**). No mirror symmetry, guide curves or density painting yet.
- The macOS build is checked only by automatic tests, not yet on a real Mac. The programs are not
  code-signed.

The full list, with what helps: [user guide §7](../USER-GUIDE.md#7-sculpts-what-works-and-what-struggles).

## Where to go next

- [Install](../USER-GUIDE.md#2-install-and-update): get the zip, install it in Blender, update or remove it.
- [User guide](../USER-GUIDE.md): your first remesh, every setting, every warning.
- [Results](../gallery/README.md): before and after on public meshes, with the numbers and the limits.
- [Community](community.md): what other people made with it.
- [Contact](../../README.md#contact): bugs, ideas and questions go to GitHub Issues; security
  problems to a [private report](../../SECURITY.md).
- [Credits and license](../../README.md#origin-and-credits): Quadami started as a fork of
  AutoRemesher by Jeremy Hu.
