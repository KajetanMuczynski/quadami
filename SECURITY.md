<!--
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) 2026 Quadami contributors
-->

# Security policy

## Reporting a vulnerability

Please do **not** open a public issue for a security problem. Report it privately on GitHub:
[Security > Report a vulnerability](https://github.com/KajetanMuczynski/quadami/security/advisories/new)
(you need a free GitHub account). Only the maintainer sees the report until a fix is published.
Without a GitHub account, e-mail [quadami.contact@gmail.com](mailto:quadami.contact@gmail.com) with "Security" in the subject;
e-mail is not encrypted, so send the details of a serious problem only once we have replied.

Include what you can: the build line from the bottom of the Quadami panel, your system and Blender
version, the steps or the file that triggers the problem, and what an attacker could gain. If the
trigger is a model, attach a minimal one that you are allowed to share.

Quadami is maintained by one person in their spare time. You will get a reply in the report; a fix
goes into the next release, and the advisory is published with it, crediting you unless you ask
otherwise.

## Supported versions

Only the newest release and the current `master` branch get security fixes. Test builds from pull
requests and older releases do not: update first and check that the problem is still there.

## What is in scope

- The Blender add-on (`addon/`) and the remesher program it starts (`autoremesher-cli`), for
  example a crafted mesh that makes the remesher write outside its temporary folder, run code, or
  corrupt memory in a way that goes beyond a crash.
- The release zips and the website built from this repository (`tools/site/`, GitHub Pages).
- The CI workflows in `.github/workflows/`, where they could leak a token or let a pull request
  publish something.

A mesh that only makes the remesher fail, crash or produce a bad result is a normal bug: use the
[bug report form](https://github.com/KajetanMuczynski/quadami/issues/new?template=bug-report.yml).
Problems in Blender itself go to Blender; problems in a bundled library go to that library (we
update it here once it is fixed there).
