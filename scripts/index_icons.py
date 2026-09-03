#!/usr/bin/env python3
"""Refresh the committed icon catalogs under assets/icons/.

Run this only to pick up new upstream icons. The catalogs are committed, so
searching and resolving icon names works offline, and an ordinary build never
touches the network.

    python3 scripts/index_icons.py                  all packs
    python3 scripts/index_icons.py fontawesome      one pack
    python3 scripts/index_icons.py --from DIR       from a local release package

--from points at an unpacked official package (FontAwesome's "Free for Desktop"
or "Free for Web" download; their metadata and SVGs are byte-identical). The
location is remembered in assets/icons/sources.conf, so later runs -- including
vendoring new glyphs -- need no network access at all.
"""
import json
import os
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import icon_index as index


def fetch(url, description):
    request = urllib.request.Request(
        url, headers={"User-Agent": "deskmate-icon-indexer",
                      "Accept": "application/vnd.github+json"})
    try:
        with urllib.request.urlopen(request, timeout=45) as response:
            return response.read()
    except Exception as error:  # noqa: BLE001 - one clear message is enough
        sys.exit("%s: %s\n  %s" % (description, error, url))


def repo_tree(repo, branch="main"):
    """Every path in a repository, in one request."""
    data = json.loads(fetch(
        "https://api.github.com/repos/%s/git/trees/%s?recursive=1"
        % (repo, branch), "%s tree" % repo))
    if data.get("truncated"):
        sys.exit("%s: tree listing was truncated" % repo)
    return [node["path"] for node in data.get("tree", [])]


def clean_keywords(values, name):
    """Keep search terms short, unique, and not just a repeat of the name."""
    parts = set(name.split("-"))
    seen, kept = set(), []
    for value in values:
        for word in str(value).lower().replace("-", " ").split():
            if word in parts or word in seen or len(word) < 2:
                continue
            seen.add(word)
            kept.append(word)
        if len(kept) >= 8:
            break
    return " ".join(kept[:8])


def build_fontawesome(local=None):
    # One metadata file carries every icon, the styles it ships in Free, and
    # the upstream search terms. At nearly 5 MB it is far too large to vendor,
    # so only the three columns the generator needs are kept.
    relative = index.PACKS["fontawesome"]["local_meta"]
    if local and os.path.exists(os.path.join(local, relative)):
        with open(os.path.join(local, relative), "rb") as handle:
            meta = json.loads(handle.read())
    else:
        meta = json.loads(fetch(
            "https://raw.githubusercontent.com/FortAwesome/Font-Awesome"
            "/%s/metadata/icons.json" % index.PACKS["fontawesome"]["branch"],
            "FontAwesome metadata"))
    rows = []
    for name, entry in sorted(meta.items()):
        styles = [s for s in entry.get("free", [])
                  if s in index.FA_STYLE_ORDER]
        if not styles:
            continue  # Pro-only icon; the SVG is not downloadable
        terms = list(entry.get("search", {}).get("terms", []))
        label = entry.get("label", "")
        rows.append((name, ",".join(styles),
                     clean_keywords([label] + terms, name)))
    return rows


def build_octicons(local=None):
    paths = repo_tree(index.PACKS["octicons"]["repo"])
    sizes = {}
    for path in paths:
        if not path.startswith("icons/") or not path.endswith(".svg"):
            continue
        stem = os.path.basename(path)[:-4]
        base, _, size = stem.rpartition("-")
        if not base or not size.isdigit():
            continue
        sizes.setdefault(base, set()).add(int(size))

    keywords = {}
    try:
        keywords = json.loads(fetch(
            "https://raw.githubusercontent.com/primer/octicons"
            "/main/keywords.json", "Octicons keywords"))
    except SystemExit:
        pass  # keyword search degrades to name matching

    rows = []
    for name in sorted(sizes):
        rows.append((name,
                     ",".join(str(s) for s in sorted(sizes[name])),
                     clean_keywords(keywords.get(name, []), name)))
    return rows


def build_lucide(local=None):
    # Lucide keeps its tags in one JSON file per icon, which would be well over
    # a thousand requests. Names are descriptive enough to search on their own.
    paths = repo_tree(index.PACKS["lucide"]["repo"])
    names = sorted({os.path.basename(p)[:-4] for p in paths
                    if p.startswith("icons/") and p.endswith(".svg")})
    return [(name, "-", "") for name in names]


BUILDERS = {
    "fontawesome": build_fontawesome,
    "octicons": build_octicons,
    "lucide": build_lucide,
}


def main():
    args = sys.argv[1:]
    local = None
    if "--from" in args:
        position = args.index("--from")
        if position + 1 >= len(args):
            sys.exit("usage: index_icons.py --from DIR [pack...]")
        local = os.path.abspath(os.path.expanduser(args[position + 1]))
        del args[position:position + 2]
        if not os.path.isdir(local):
            sys.exit("%s is not a directory" % local)
        # Accept either the package root or its wrapper directory.
        if not os.path.exists(os.path.join(local, "metadata")):
            nested = [os.path.join(local, name) for name in os.listdir(local)]
            nested = [p for p in nested
                      if os.path.isdir(os.path.join(p, "metadata"))]
            if len(nested) == 1:
                local = nested[0]
            else:
                sys.exit("%s does not look like an icon release package "
                         "(no metadata/ directory)" % local)

    requested = args or list(index.PACK_ORDER)
    unknown = [p for p in requested if p not in BUILDERS]
    if unknown:
        sys.exit("unknown pack(s): %s (known: %s)"
                 % (", ".join(unknown), ", ".join(sorted(BUILDERS))))

    os.makedirs(index.ICON_DIR, exist_ok=True)
    total = 0
    for pack in requested:
        usable = local if local and index.PACKS[pack].get("local_meta") else None
        if usable:
            index.save_source(pack, usable)
        rows = BUILDERS[pack](usable)
        path = index.index_path(pack)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("# %s catalog -- %s\n"
                         "# generated by scripts/index_icons.py; do not edit\n"
                         "# <name>\\t<%s>\\t<keywords>\n"
                         % (pack, index.PACKS[pack]["license"],
                            "sizes" if pack == "octicons" else "styles"))
            for name, variants, keywords in rows:
                handle.write("%s\t%s\t%s\n" % (name, variants, keywords))
        print("%-12s %5d icons -> %s%s"
              % (pack, len(rows), os.path.relpath(path, index.ROOT),
                 "  (local package)" if usable else ""))
        total += len(rows)
    print("%d icons indexed and searchable offline" % total)


if __name__ == "__main__":
    main()
