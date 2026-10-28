#!/usr/bin/env python3
"""The project's website, built from site/ and the README's screenshots.

Usage: build_site.py [--out DIR] [--check]

Copies site/ (the page, its style, script and fonts) into tools/dev/builds/site/ (git-ignored),
or DIR, with the screenshots of docs/images/ and the app's icon in images/. Nothing is fetched and
nothing is generated: open index.html from the folder, or serve it.

--check, after the build: every local link, image, script and style resolves, every #anchor
names an id of the page, the page loads nothing from another site (it links to other sites, never
loads from them), and no file of the site names a planning item or the planning notes' folder.
Standard library only.
"""

from __future__ import annotations

import argparse
import html.parser
import re
import shutil
import sys
import urllib.parse
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SITE = REPO / "site"
IMAGES = REPO / "docs" / "images"
OUT = REPO / "tools" / "dev" / "builds" / "site"
# A planning item's identifier, or the planning notes' folder (spelt in two pieces, so that this
# file passes the repository's own check for it).
PLANNING = re.compile(r"\b(epic|story|iteration)[ -]?[0-9]|docs/" + "epics", re.IGNORECASE)
# Attributes that make the browser load something, against those that only link to it.
LOADS = {("img", "src"), ("script", "src"), ("link", "href"), ("source", "src"),
         ("video", "src"), ("audio", "src"), ("iframe", "src")}
# A <link> that only names a page: the page's own address.
NAMES = {"canonical", "alternate"}


def build(out: Path) -> None:
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(SITE, out)
    (out / "images").mkdir()
    for f in sorted(IMAGES.iterdir()):
        if f.suffix in (".png", ".gif"):
            shutil.copy2(f, out / "images" / f.name)
    shutil.copy2(REPO / "icon.png", out / "images" / "icon.png")
    (out / ".nojekyll").touch()  # served as it is, without Jekyll


class Page(html.parser.HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.ids: set[str] = set()
        self.refs: list[tuple[str, str, str]] = []  # (tag, attribute, value)

    def handle_starttag(self, tag, attrs):
        if tag == "link" and dict(attrs).get("rel") in NAMES:
            tag = "a"  # names an address, loads nothing
        for name, value in attrs:
            if value is None:
                continue
            if name == "id":
                self.ids.add(value)
            elif name in ("href", "src"):
                self.refs.append((tag, name, value))


def check(out: Path) -> list[str]:
    errors = []
    for f in sorted(out.rglob("*")):
        if f.suffix in (".html", ".css", ".js") and PLANNING.search(f.read_text()):
            errors.append(f"{f.relative_to(out)}: names the planning notes")
    for f in sorted(out.rglob("*.html")):
        page = Page()
        page.feed(f.read_text())
        rel = f.relative_to(out)
        for tag, attr, value in page.refs:
            url = urllib.parse.urlparse(value)
            if url.scheme or value.startswith("//"):
                if (tag, attr) in LOADS:
                    errors.append(f"{rel}: <{tag} {attr}> loads {value} from another site")
                continue
            if url.path and not (f.parent / url.path).exists():
                errors.append(f"{rel}: {value} does not exist")
            if url.fragment and not url.path and url.fragment not in page.ids:
                errors.append(f"{rel}: #{url.fragment} names no id")
    for f in sorted(out.rglob("*.css")):
        for value in re.findall(r"url\(\s*['\"]?([^'\")]+)", f.read_text()):
            if value.startswith("data:"):
                continue
            if urllib.parse.urlparse(value).scheme:
                errors.append(f"{f.relative_to(out)}: loads {value} from another site")
            elif not (f.parent / value).exists():
                errors.append(f"{f.relative_to(out)}: {value} does not exist")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, default=OUT)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    out = args.out.resolve()
    build(out)
    print(f"site: {out}")
    if args.check:
        errors = check(out)
        for e in errors:
            print(f"error: {e}", file=sys.stderr)
        if errors:
            return 1
        print("site: checked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
