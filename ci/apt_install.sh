#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
# apt-get update and install on a GitHub runner, robust to a slow Ubuntu mirror:
# each attempt may download for at most APT_DOWNLOAD_SECONDS (default 300), up to 3
# attempts; partly downloaded packages are kept, so a retry resumes. Unpacking runs
# only after every package is downloaded, without a time limit (killing dpkg could
# leave the system half-configured).
#
#   ci/apt_install.sh <package>...
set -euo pipefail
limit="${APT_DOWNLOAD_SECONDS:-300}"
apt=(apt-get -q -o Acquire::Retries=3 -o Acquire::http::Timeout=30)
for attempt in 1 2 3; do
  if sudo timeout "$limit" "${apt[@]}" update &&
     sudo DEBIAN_FRONTEND=noninteractive timeout "$limit" "${apt[@]}" install -y \
       --no-install-recommends --download-only "$@"; then
    sudo DEBIAN_FRONTEND=noninteractive "${apt[@]}" install -y --no-install-recommends "$@"
    exit 0
  fi
  echo "::warning::apt download attempt $attempt failed or took more than ${limit} s"
  sleep 10
done
echo "::error::apt download failed 3 times"
exit 1
