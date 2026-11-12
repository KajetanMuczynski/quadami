#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
#
# Download a Blender release file into blender-download/ and check its SHA-256.
#
#   ci/fetch_blender.sh <version> <file> <sha256>
#
# download.blender.org answers 403 to some runners, so official mirrors
# (blender.org/download/mirrors) are tried after it; the pinned SHA-256 decides
# whether a file is used, wherever it came from. A file already there (from
# the cache) is only checked.
set -euo pipefail
version=$1 file=$2 sum=$3
series=$(echo "$version" | cut -d. -f1-2)
mkdir -p blender-download
check() {
    if command -v sha256sum > /dev/null; then echo "$sum  blender-download/$file" | sha256sum -c -
    else echo "$sum  blender-download/$file" | shasum -a 256 -c -; fi
}
if [ -f "blender-download/$file" ] && check; then exit 0; fi
for base in https://download.blender.org/release \
            https://mirrors.ocf.berkeley.edu/blender/release \
            https://ftp.halifax.rwth-aachen.de/blender/release \
            https://mirror.clarkson.edu/blender/release; do
    url="$base/Blender$series/$file"
    echo "fetch_blender: $url"
    if curl -fsSL --retry 3 -o "blender-download/$file" "$url" && check; then exit 0; fi
    rm -f "blender-download/$file"
done
echo "fetch_blender: no source gave $file with SHA-256 $sum" >&2
exit 1
