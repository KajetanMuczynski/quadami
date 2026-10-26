#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Quadami contributors
"""Build the Quadami website from a whitelist of Markdown files (tools/site/site.json).

    python tools/site/build.py --out build/site            # fetches releases from GitHub
    python tools/site/build.py --out build/site --offline  # preview without the network

Every page is one Markdown file of the repository (or some of its sections), rendered with
markdown-it-py into tools/site/template.html. Links are checked and rewritten: a link to another
published page goes to that page, a link to any other file of the repository goes to GitHub, and
a link that points nowhere (missing file or heading) fails the build. Images are copied only from
the folders listed under "media". The build also fails on links to upstream outside the credits
page, on raw HTML that carries a link (it would bypass the check), and on the forbidden strings.
"""

import argparse
import html
import json
import os
import posixpath
import re
import shutil
import sys
import urllib.error
import urllib.request

from markdown_it import MarkdownIt

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))

LATEST_MARK = "<!-- site:latest-release -->"
LIST_MARK = "<!-- site:release-list -->"
UPSTREAM = re.compile(r"github\.com/huxingyi/autoremesher", re.I)
RAW_LINK = re.compile(r"\b(?:href|src)\s*=", re.I)


class BuildError(Exception):
    pass


def slugify(text):
    """GitHub's heading anchor: lower case, punctuation dropped, spaces to hyphens."""
    return re.sub(r"[^\w\- ]", "", text.strip().lower()).replace(" ", "-")


def heading_text(inline):
    return "".join(c.content for c in inline.children or [] if c.type in ("text", "code_inline"))


def new_md():
    return MarkdownIt("commonmark", {"html": True}).enable(["table", "strikethrough"])


class Source:
    """One Markdown file: its text, tokens and GitHub-style heading anchors."""

    def __init__(self, md, rel, text):
        self.rel = rel
        self.text = text
        self.tokens = md.parse(text)
        self.anchors = []  # (line, level, title, slug) in document order
        seen = {}
        for i, tok in enumerate(self.tokens):
            if tok.type == "heading_open":
                title = heading_text(self.tokens[i + 1])
                base = slugify(title)
                n = seen.get(base, 0)
                seen[base] = n + 1
                slug = base if n == 0 else "%s-%d" % (base, n)
                self.anchors.append((tok.map[0], int(tok.tag[1]), title, slug))

    def slugs(self):
        return {a[3] for a in self.anchors}

    def line_ranges(self, titles):
        """Line ranges of the named sections: each heading down to the next of its level or higher."""
        out = []
        for title in titles:
            hits = [k for k, a in enumerate(self.anchors) if a[2] == title]
            if len(hits) != 1:
                raise BuildError("%s: section %r found %d times" % (self.rel, title, len(hits)))
            k = hits[0]
            start, level = self.anchors[k][0], self.anchors[k][1]
            end = len(self.text.splitlines())
            for line, lev, _, _ in self.anchors[k + 1:]:
                if lev <= level:
                    end = line
                    break
            out.append((start, end))
        return out


class Site:
    def __init__(self, config, out, releases, root=ROOT):
        self.root = root
        self.config = config
        self.out = out
        self.releases = releases
        self.md = new_md()
        self.sources = {}
        self.pages = config["pages"]
        self.copied = {}
        self.github = "https://github.com/%s" % config["repo"]
        self.errors = []

    # -- sources -------------------------------------------------------------------------

    def source(self, rel, text=None):
        if rel not in self.sources:
            if text is None:
                with open(os.path.join(self.root, rel), encoding="utf-8") as f:
                    text = f.read()
            self.sources[rel] = Source(self.md, rel, text)
        return self.sources[rel]

    def page_text(self, page):
        src = self.source(page["source"])
        if "sections" not in page:
            return src, None
        return src, src.line_ranges(page["sections"])

    def page_slugs(self, page):
        src, ranges = self.page_text(page)
        if ranges is None:
            return src.slugs()
        return {a[3] for a in src.anchors if any(s <= a[0] < e for s, e in ranges)}

    # -- links ---------------------------------------------------------------------------

    def url_of(self, page, from_page):
        depth = len([p for p in from_page["path"].split("/") if p])
        target = page["path"] + "/" if page["path"] else ""
        return ("../" * depth) + target or "./"

    def media_url(self, repo_path, from_page):
        rel = repo_path[len("docs/"):] if repo_path.startswith("docs/") else repo_path
        dest = "media/" + rel
        self.copied[repo_path] = dest
        depth = len([p for p in from_page["path"].split("/") if p])
        return ("../" * depth) + dest

    def rewrite(self, href, page, is_image):
        where = "%s (page /%s)" % (page["source"], page["path"])
        if re.match(r"^[a-z][a-z0-9+.-]*:", href, re.I):
            if UPSTREAM.search(href) and page["path"] not in self.config["upstream_links_only_on"]:
                self.errors.append("%s: link to upstream outside the credits page: %s" % (where, href))
            return href
        if href.startswith("//"):
            self.errors.append("%s: protocol-relative link %s" % (where, href))
            return href
        path, _, frag = href.partition("#")
        base = posixpath.dirname(page["source"])
        if path.startswith("/"):  # relative to the repository root
            base, path = "", path.lstrip("/")
        target = posixpath.normpath(posixpath.join(base, path)) if path else page["source"]
        if target.startswith("../") or target == "..":
            self.errors.append("%s: link leaves the repository: %s" % (where, href))
            return href
        full = os.path.join(self.root, target)
        if not os.path.exists(full):
            self.errors.append("%s: link to a missing file: %s" % (where, href))
            return href
        if os.path.isdir(full):
            readme = posixpath.join(target, "README.md")
            if os.path.exists(os.path.join(self.root, readme)) and not is_image:
                target = readme
            else:
                return "%s/tree/%s/%s" % (self.github, self.config["branch"], target)

        if is_image:
            if not any(target.startswith(m) for m in self.config["media"]):
                self.errors.append("%s: image outside the media folders: %s" % (where, href))
                return href
            return self.media_url(target, page)

        # A published page that has this file (and this heading), this page first.
        candidates = [page] + [p for p in self.pages if p is not page]
        if target.endswith(".md"):
            if frag and frag not in self.source(target).slugs():
                self.errors.append("%s: no heading #%s in %s" % (where, frag, target))
            for cand in candidates:
                if cand["source"] != target:
                    continue
                if frag and frag not in self.page_slugs(cand):
                    continue
                if cand is page and frag:
                    return "#" + frag
                return self.url_of(cand, page) + ("#" + frag if frag else "")
        if any(target.startswith(m) for m in self.config["media"]):
            return self.media_url(target, page)
        url = "%s/blob/%s/%s" % (self.github, self.config["branch"], target)
        return url + ("#" + frag if frag else "")

    # -- rendering -----------------------------------------------------------------------

    def render(self, page):
        src, ranges = self.page_text(page)
        lines = src.text.splitlines(keepends=True)
        chunks = [(0, len(lines))] if ranges is None else ranges
        slug_at = {a[0]: a[3] for a in src.anchors}
        out = []
        for start, end in chunks:
            tokens = self.md.parse("".join(lines[start:end]))
            for i, tok in enumerate(tokens):
                if tok.type == "heading_open":
                    tok.attrSet("id", slug_at[start + tok.map[0]])
                if tok.type in ("html_block", "html_inline") and RAW_LINK.search(tok.content):
                    self.errors.append("%s: raw HTML with a link (write it in Markdown): %s"
                                       % (page["source"], tok.content.strip()[:80]))
                for child in tok.children or []:
                    if child.type == "html_inline" and RAW_LINK.search(child.content):
                        self.errors.append("%s: raw HTML with a link (write it in Markdown): %s"
                                           % (page["source"], child.content.strip()[:80]))
                    elif child.type == "link_open":
                        child.attrSet("href", self.rewrite(child.attrGet("href"), page, False))
                    elif child.type == "image":
                        child.attrSet("src", self.rewrite(child.attrGet("src"), page, True))
                        child.attrSet("loading", "lazy")
            out.append(self.md.renderer.render(tokens, self.md.options, {}))
        body = "".join(out)
        body = re.sub(r"<table>", '<div class="table"><table>', body)
        body = re.sub(r"</table>", "</table></div>", body)
        title = page.get("title")
        if not title:
            h1 = [a for a in src.anchors if a[1] == 1]
            title = h1[0][2] if h1 else self.config["name"]
        return title, body

    def nav(self, current):
        items = []
        for p in self.pages:
            if "nav" not in p:
                continue
            attr = ' aria-current="page"' if p is current else ""
            items.append('<a href="%s"%s>%s</a>' % (html.escape(self.url_of(p, current)), attr,
                                                     html.escape(p["nav"])))
        return "\n      ".join(items)

    def build(self, template, commit):
        os.makedirs(self.out, exist_ok=True)
        self.inject_releases()
        for page in self.pages:
            title, body = self.render(page)
            depth = len([p for p in page["path"].split("/") if p])
            full_title = self.config["name"] if not page["path"] else "%s · %s" % (title, self.config["name"])
            canonical = self.config["url"] + (page["path"] + "/" if page["path"] else "")
            doc = (template
                   .replace("{{title}}", html.escape(full_title))
                   .replace("{{canonical}}", html.escape(canonical))
                   .replace("{{root}}", "../" * depth or "./")
                   .replace("{{nav}}", self.nav(page))
                   .replace("{{content}}", body)
                   .replace("{{repo}}", self.github)
                   .replace("{{source}}", "%s/blob/%s/%s" % (self.github, self.config["branch"], page["source"]))
                   .replace("{{commit}}", html.escape(commit)))
            for bad in self.config["forbidden"]:
                if bad in doc:
                    self.errors.append("%s: forbidden string %r in the output" % (page["source"], bad))
            dest = os.path.join(self.out, page["path"], "index.html")
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "w", encoding="utf-8") as f:
                f.write(doc)
        shutil.copyfile(os.path.join(HERE, "style.css"), os.path.join(self.out, "style.css"))
        for repo_path, dest in sorted(self.copied.items()):
            target = os.path.join(self.out, dest)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(os.path.join(self.root, repo_path), target)

    # -- releases ------------------------------------------------------------------------

    def inject_releases(self):
        """Fill the release markers of the home and releases pages from the GitHub releases."""
        published = [r for r in self.releases if not r.get("draft")] if self.releases is not None else None
        for page in self.pages:
            src = self.source(page["source"])
            text = src.text
            if LATEST_MARK not in text and LIST_MARK not in text:
                continue
            text = text.replace(LATEST_MARK, latest_markdown(published, self.config["links"]))
            text = text.replace(LIST_MARK, list_markdown(published, self.config["links"]))
            self.sources[page["source"]] = Source(self.md, page["source"], text)


def size_text(n):
    return "%.1f MB" % (n / 1e6) if n >= 1e6 else "%d kB" % max(1, round(n / 1e3))


def latest_markdown(published, links):
    if published is None:
        return "*Preview build: the release list was not fetched.*"
    stable = [r for r in published if not r.get("prerelease")]
    if not stable:
        text = ("**There is no release yet.** Test builds of the add-on are available for Linux, "
                "Windows and macOS; [Install](%s) says where to get them and how good they are."
                % links["test_builds"])
        pre = [r for r in published if r.get("prerelease")]
        if pre:
            text += " A pre-release, %s, is on the [releases page](%s)." % (pre[0]["tag_name"],
                                                                           links["releases"])
        return text
    r = stable[0]
    return ("**[Download Quadami %s](%s)** (released %s): one zip for Linux, Windows or macOS. "
            "[All releases](%s)." % (r["tag_name"].lstrip("v"), r["html_url"],
                                     r["published_at"][:10], links["releases"]))


def shift_headings(text, by):
    """Push ATX headings down `by` levels (outside fenced code), so a release body nests."""
    out, fence = [], None
    for line in text.splitlines():
        m = re.match(r"^(`{3,}|~{3,})", line)
        if m and (fence is None or m.group(1)[0] == fence[0]):
            fence = None if fence else m.group(1)
        if fence is None and re.match(r"^#{1,6} ", line):
            line = "#" * by + line
        out.append(line)
    return "\n".join(out)


def list_markdown(published, links):
    if published is None:
        return "*Preview build: the release list was not fetched.*"
    if not published:
        return ("No release has been published yet. Until then, test builds are listed under "
                "[Install](%s)." % links["test_builds"])
    parts = []
    for r in published:
        kind = " (pre-release)" if r.get("prerelease") else ""
        parts.append("## %s%s\n" % (r.get("name") or r["tag_name"], kind))
        parts.append("Published %s · tag `%s` · [on GitHub](%s)\n" % (
            (r.get("published_at") or "")[:10], r["tag_name"], r["html_url"]))
        assets = r.get("assets") or []
        if assets:
            parts.append("| File | Size |\n|---|---:|")
            for a in assets:
                parts.append("| [%s](%s) | %s |" % (a["name"], a["browser_download_url"], size_text(a["size"])))
            parts.append("")
        parts.append(shift_headings(r.get("body") or "", 1) + "\n")
    return "\n".join(parts)


def fetch_releases(repo):
    """Every release, newest first. Uses $GITHUB_TOKEN when set (CI); fails loudly otherwise."""
    releases, url = [], "https://api.github.com/repos/%s/releases?per_page=100" % repo
    while url:
        req = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json",
                                                   "User-Agent": "quadami-site"})
        token = os.environ.get("GITHUB_TOKEN")
        if token:
            req.add_header("Authorization", "Bearer " + token)
        with urllib.request.urlopen(req, timeout=30) as resp:
            releases += json.load(resp)
            m = re.search(r'<([^>]+)>;\s*rel="next"', resp.headers.get("Link") or "")
            url = m.group(1) if m else None
    releases.sort(key=lambda r: r.get("published_at") or r.get("created_at") or "", reverse=True)
    return releases


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True, help="output folder (emptied first)")
    ap.add_argument("--config", default=os.path.join(HERE, "site.json"))
    ap.add_argument("--root", default=ROOT, help="repository the page sources are in (tests)")
    group = ap.add_mutually_exclusive_group()
    group.add_argument("--offline", action="store_true", help="do not fetch releases (preview)")
    group.add_argument("--releases-json", help="read releases from this file (tests)")
    ap.add_argument("--commit", default=os.environ.get("GITHUB_SHA", "local")[:7])
    args = ap.parse_args(argv)

    with open(args.config, encoding="utf-8") as f:
        config = json.load(f)
    if args.offline:
        releases = None
    elif args.releases_json:
        with open(args.releases_json, encoding="utf-8") as f:
            releases = json.load(f)
    else:
        try:
            releases = fetch_releases(config["repo"])
        except (urllib.error.URLError, OSError) as e:
            print("error: could not fetch the releases (%s); use --offline for a preview" % e,
                  file=sys.stderr)
            return 1

    if os.path.isdir(args.out):
        shutil.rmtree(args.out)
    with open(os.path.join(HERE, "template.html"), encoding="utf-8") as f:
        template = f.read()
    site = Site(config, args.out, releases, args.root)
    try:
        site.build(template, args.commit)
    except BuildError as e:
        site.errors.append(str(e))
    if site.errors:
        for e in site.errors:
            print("error: " + e, file=sys.stderr)
        return 1
    print("built %d pages and %d images into %s (releases: %s)" % (
        len(site.pages), len(site.copied), args.out,
        "not fetched" if releases is None else len([r for r in releases if not r.get("draft")])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
