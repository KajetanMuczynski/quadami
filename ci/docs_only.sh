#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
# Whether the change from <base> to <head> touches documentation only, for the "changes"
# job in ci.yml: prints the changed paths, then "macos=false" (documentation only) or
# "macos=true", and appends that line to $GITHUB_OUTPUT when it is set.
#
#   ci/docs_only.sh <base> <head>
#
# Documentation is docs/ and the Markdown files at the top level or directly in addon/,
# nothing else (Markdown in thirdparty/ or addon/quadami/ can end up in the zip).
# --no-renames: a rename is listed as its old and its new path, so moving a file out of
# addon/ into docs/ is not documentation only (with rename detection only the new path
# shows). Tested by ci/tests/test_docs_only.py.
set -euo pipefail
base="${1:?usage: docs_only.sh <base> <head>}"
head="${2:?usage: docs_only.sh <base> <head>}"
changed=$(git diff --no-renames --name-only "$base" "$head")
printf '%s\n' "$changed"
if [ -n "$changed" ] && ! printf '%s\n' "$changed" | grep -v -E '^docs/|^[^/]+\.md$|^addon/[^/]+\.md$'; then
  echo "documentation only: the macOS job is skipped"
  line=macos=false
else
  echo "^ not documentation (or no change): the macOS job runs"
  line=macos=true
fi
echo "$line"
if [ -n "${GITHUB_OUTPUT:-}" ]; then echo "$line" >> "$GITHUB_OUTPUT"; fi
