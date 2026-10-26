# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Tests of the website builder: python -m unittest discover -s tools/site/tests"""

import contextlib
import io
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import build  # noqa: E402

RELEASES = [
    {"tag_name": "v0.2.0", "name": "Quadami 0.2.0", "draft": True, "prerelease": False,
     "published_at": None, "html_url": "https://example.invalid/draft", "assets": [], "body": "secret"},
    {"tag_name": "v0.1.0", "name": "Quadami 0.1.0", "draft": False, "prerelease": False,
     "published_at": "2026-11-02T10:00:00Z", "html_url": "https://github.com/o/r/releases/tag/v0.1.0",
     "assets": [{"name": "quadami-0.1.0-linux-x64.zip", "size": 1400000,
                 "browser_download_url": "https://github.com/o/r/releases/download/v0.1.0/a.zip"}],
     "body": "Notes.\n\n## Download\n\n```\n## not a heading\n```\n"},
]


def write(root, rel, text):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


class Fixture(unittest.TestCase):
    """A tiny repository with two pages, built into a temporary folder."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = os.path.join(self.tmp.name, "repo")
        self.out = os.path.join(self.tmp.name, "out")
        write(self.root, "docs/home.md", "# Home\n\n<!-- site:latest-release -->\n\n"
              "[guide](GUIDE.md#second-part) [plan](PLAN.md) ![pic](img/a.webp)\n")
        write(self.root, "docs/GUIDE.md", "# Guide\n\n## First part\n\n[up](#first-part)\n\n"
              "## Second part\n\n### Inner\n\n## Third\n")
        write(self.root, "docs/PLAN.md", "# Internal plan\n")
        write(self.root, "docs/img/a.webp", "x")
        write(self.root, "docs/releases.md", "# Releases\n\n<!-- site:release-list -->\n")
        self.config = {
            "name": "Q", "url": "https://example.invalid/", "repo": "o/r", "branch": "master",
            "pages": [
                {"path": "", "nav": "Home", "source": "docs/home.md"},
                {"path": "part", "source": "docs/GUIDE.md", "sections": ["Second part"], "title": "Part"},
                {"path": "guide", "nav": "Guide", "source": "docs/GUIDE.md"},
                {"path": "releases", "nav": "Releases", "source": "docs/releases.md"},
            ],
            "links": {"test_builds": "/docs/GUIDE.md#first-part", "releases": "/docs/releases.md"},
            "media": ["docs/img/"],
            "upstream_links_only_on": ["credits"],
            "forbidden": ["private-sculpts"],
        }

    def tearDown(self):
        self.tmp.cleanup()

    def run_build(self, releases=None):
        cfg = os.path.join(self.tmp.name, "site.json")
        with open(cfg, "w") as f:
            json.dump(self.config, f)
        args = ["--out", self.out, "--config", cfg, "--root", self.root]
        if releases is None:
            args.append("--offline")
        else:
            rel = os.path.join(self.tmp.name, "releases.json")
            with open(rel, "w") as f:
                json.dump(releases, f)
            args += ["--releases-json", rel]
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            code = build.main(args)
        return code, err.getvalue()

    def read(self, page):
        with open(os.path.join(self.out, page, "index.html"), encoding="utf-8") as f:
            return f.read()


class TestBuild(Fixture):
    def test_links(self):
        code, err = self.run_build()
        self.assertEqual(code, 0, err)
        home = self.read("")
        # The heading lives on the section page, which comes first in the list.
        self.assertIn('href="part/#second-part"', home)
        self.assertIn('href="https://github.com/o/r/blob/master/docs/PLAN.md"', home)
        self.assertIn('src="media/img/a.webp"', home)
        self.assertTrue(os.path.exists(os.path.join(self.out, "media/img/a.webp")))
        self.assertFalse(os.path.exists(os.path.join(self.out, "plan")))
        # In-page anchor on the full page, and the anchor outside the section goes to the full page.
        self.assertIn('href="#first-part"', self.read("guide"))
        part = self.read("part")
        self.assertIn('<h2 id="second-part">', part)
        self.assertIn('<h3 id="inner">', part)
        self.assertNotIn("Third", part.split("<main>")[1])

    def test_missing_heading_fails(self):
        write(self.root, "docs/home.md", "# Home\n\n[x](GUIDE.md#nowhere)\n")
        code, err = self.run_build()
        self.assertEqual(code, 1)
        self.assertIn("no heading #nowhere", err)

    def test_missing_file_fails(self):
        write(self.root, "docs/home.md", "# Home\n\n[x](GONE.md)\n")
        self.assertEqual(self.run_build()[0], 1)

    def test_upstream_link_only_on_credits(self):
        write(self.root, "docs/home.md", "# Home\n\n[x](https://github.com/huxingyi/autoremesher)\n")
        code, err = self.run_build()
        self.assertEqual(code, 1)
        self.assertIn("upstream", err)

    def test_raw_html_link_fails(self):
        write(self.root, "docs/home.md", '# Home\n\n<img src="../../secret.png">\n')
        self.assertEqual(self.run_build()[0], 1)

    def test_image_outside_media_fails(self):
        write(self.root, "docs/other/b.png", "x")
        write(self.root, "docs/home.md", "# Home\n\n![b](other/b.png)\n")
        self.assertEqual(self.run_build()[0], 1)

    def test_forbidden_string_fails(self):
        write(self.root, "docs/home.md", "# Home\n\nsee private-sculpts/x\n")
        self.assertEqual(self.run_build()[0], 1)

    def test_coverage(self):
        self.config["pages"] = [p for p in self.config["pages"] if p["path"] != "guide"]
        self.config["coverage"] = {"docs/GUIDE.md": {"First part": "not for the site"}}
        code, err = self.run_build()
        self.assertEqual(code, 1)
        self.assertIn("'Third' is on no page", err)
        self.assertNotIn("'First part'", err)
        self.config["coverage"]["docs/GUIDE.md"]["Third"] = "internal"
        code, err = self.run_build()
        self.assertEqual(code, 0, err)
        self.config["coverage"]["docs/GUIDE.md"]["Gone"] = "stale"
        code, err = self.run_build()
        self.assertIn("'Gone' does not exist", err)

    def test_no_release(self):
        code, err = self.run_build(releases=[RELEASES[0]])
        self.assertEqual(code, 0, err)
        self.assertIn("There is no release yet", self.read(""))
        self.assertIn("No release has been published yet", self.read("releases"))
        self.assertNotIn("secret", self.read("releases"))

    def test_releases(self):
        code, err = self.run_build(releases=RELEASES)
        self.assertEqual(code, 0, err)
        home = self.read("")
        self.assertIn("Download Quadami 0.1.0", home)
        rel = self.read("releases")
        self.assertIn('<h2 id="quadami-010">Quadami 0.1.0</h2>', rel)
        self.assertIn('<h3 id="download">Download</h3>', rel)
        self.assertIn("## not a heading", rel)  # fenced code is left alone
        self.assertIn("1.4 MB", rel)
        self.assertNotIn("secret", rel)


class TestSlug(unittest.TestCase):
    def test_github_slugs(self):
        self.assertEqual(build.slugify("2. Install and update"), "2-install-and-update")
        self.assertEqual(build.slugify("Arming sword at 1,000 quads (thin parts)"),
                         "arming-sword-at-1000-quads-thin-parts")
        self.assertEqual(build.slugify("Limits and what the panel should say"),
                         "limits-and-what-the-panel-should-say")
        self.assertEqual(build.slugify("macOS: if the remesher is blocked"),
                         "macos-if-the-remesher-is-blocked")


class TestRealSite(unittest.TestCase):
    """The repository's own site builds without errors (no network)."""

    def test_repo(self):
        with tempfile.TemporaryDirectory() as out:
            err = io.StringIO()
            with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
                code = build.main(["--out", out, "--offline"])
            self.assertEqual(code, 0, err.getvalue())


if __name__ == "__main__":
    unittest.main()
