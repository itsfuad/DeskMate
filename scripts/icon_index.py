"""Icon pack catalogs: build, load, search and resolve.

An icon is never declared by hand. Firmware code writes `Icon::CodePullRequest`
and the generator resolves that name against these catalogs, so the set of
available icons is whatever the packs ship -- roughly four thousand across the
three of them -- not whatever someone remembered to add to a list.

A catalog is a committed TSV under assets/icons/, one line per icon:

    <base name>\t<styles or sizes>\t<keywords>

The middle column is what the pack varies per icon: FontAwesome varies the
style (solid/regular/brands) and Octicons varies the drawn size (16/24). It is
"-" when a pack varies neither.

`scripts/index_icons.py` refreshes these files; everything else here reads them,
so search and resolution work offline.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_DIR = os.path.join(ROOT, "assets", "icons")
OVERRIDES = os.path.join(ROOT, "assets", "icons.overrides")

DEFAULT_SIZE = 16
DEFAULT_THRESHOLD = 96

# Resolution order: which packs a bare Icon:: name may come from, in priority
# order. Lucide only -- one ISC-licensed set with no attribution obligation and
# one visual language across every screen. It is stroke-only, so status badges
# are rings rather than discs and need a larger box and a lower alpha threshold
# than a filled glyph would.
#
# Every indexed pack stays searchable regardless; this governs resolution only,
# and assets/icons.overrides can still pin an individual icon to another pack.
PACK_ORDER = ("lucide",)

# FontAwesome Free style preference. `github` exists only as a brand, so the
# fallback chain matters.
FA_STYLE_ORDER = ("solid", "regular", "brands")

PACKS = {
    "octicons": {
        "repo": "primer/octicons",
        "branch": "main",
        "rotation_offset": 0.0,
        "svg": "https://raw.githubusercontent.com/primer/octicons"
               "/main/icons/{path}.svg",
        "license": "MIT",
    },
    "fontawesome": {
        "repo": "FortAwesome/Font-Awesome",
        "branch": "7.x",
        # FontAwesome's plane points east.
        "rotation_offset": -90.0,
        # svgs-full, not svgs. FontAwesome's default svgs/ are tight-cropped,
        # so 1762 of its 2883 drawings have a non-square viewBox and would sit
        # letterboxed in a square icon box. svgs-full normalizes every drawing
        # to 640x640, which drops straight into a square box -- at the cost of
        # built-in padding, so the art fills about 83% of the box against
        # Octicons' 93%. Ask for a larger size to match: Icon::CodeMerge18.
        "svg": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome"
               "/7.x/svgs-full/{path}.svg",
        # Path to the same file inside an official FontAwesome release package,
        # so --from can vendor and index without network access.
        "local_svg": "svgs-full/{path}.svg",
        "local_meta": "metadata/icons.json",
        "license": "Icons CC BY 4.0 (attribution required)",
    },
    "lucide": {
        "repo": "lucide-icons/lucide",
        "branch": "main",
        # Lucide draws its vehicles flying northeast. Every rotating glyph from
        # this pack starts from here, so a pack whose art convention changes is
        # one number to correct rather than one line per icon.
        "rotation_offset": -45.0,
        "svg": "https://raw.githubusercontent.com/lucide-icons/lucide"
               "/main/icons/{path}.svg",
        "license": "ISC",
    },
}


SOURCES = os.path.join(ICON_DIR, "sources.conf")


def index_path(pack):
    return os.path.join(ICON_DIR, pack + ".index")


def load_sources():
    """Remembered local package directories, as {pack: path}."""
    sources = {}
    if not os.path.exists(SOURCES):
        return sources
    with open(SOURCES, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            pack, _, path = line.partition("=")
            pack, path = pack.strip(), os.path.expanduser(path.strip())
            if pack in PACKS and path:
                sources[pack] = path
    return sources


def save_source(pack, path):
    sources = load_sources()
    sources[pack] = os.path.abspath(os.path.expanduser(path))
    os.makedirs(ICON_DIR, exist_ok=True)
    with open(SOURCES, "w", encoding="utf-8") as handle:
        handle.write("# Local icon release packages, used instead of the\n"
                     "# network by scripts/index_icons.py and gen_icons.py.\n"
                     "# Written by --from; paths are machine-specific and a\n"
                     "# missing directory just falls back to downloading.\n")
        for name in sorted(sources):
            handle.write("%s = %s\n" % (name, sources[name]))


def local_svg(pack, path, sources):
    """Absolute path to a glyph inside a local package, if one is usable."""
    root = sources.get(pack)
    template = PACKS[pack].get("local_svg")
    if not root or not template:
        return None
    candidate = os.path.join(root, template.format(path=path))
    return candidate if os.path.exists(candidate) else None


class Entry:
    __slots__ = ("pack", "name", "variants", "keywords")

    def __init__(self, pack, name, variants, keywords):
        self.pack = pack
        self.name = name
        self.variants = variants     # style names, or integer sizes, or ()
        self.keywords = keywords


def load_indexes(packs=None):
    """Returns {pack: {name: Entry}} for every pack with a committed index.

    Every catalog is loaded, not just the packs in PACK_ORDER, so --search can
    show what a pack outside the default order would offer and an override can
    reach it.
    """
    catalogs = {}
    for pack in (packs or tuple(PACKS)):
        path = index_path(pack)
        if not os.path.exists(path):
            continue
        entries = {}
        with open(path, encoding="utf-8") as handle:
            for line in handle:
                line = line.rstrip("\n")
                if not line or line.startswith("#"):
                    continue
                fields = line.split("\t")
                if len(fields) != 3:
                    continue
                name, variants, keywords = fields
                parsed = () if variants == "-" else tuple(variants.split(","))
                if pack == "octicons":
                    parsed = tuple(int(v) for v in parsed)
                entries[name] = Entry(pack, name, parsed, keywords)
        catalogs[pack] = entries
    if not catalogs:
        sys.exit("no icon catalogs found under assets/icons; run "
                 "python3 scripts/index_icons.py")
    return catalogs


# --- name conversion -------------------------------------------------------
# Firmware code says Icon::CodePullRequest; packs ship code-pull-request.svg.
# A trailing number is the render size, not part of the name, so one drawing
# can be requested at several sizes without a declaration for each.
IDENT = re.compile(r"^([A-Z][A-Za-z0-9]*?)([0-9]{1,3})?$")
# Split before a capital that follows a lowercase or digit, and also
# between a run of capitals and a following word, so XCircleFill is
# x-circle-fill rather than xcircle-fill.
CAMEL_SPLIT = re.compile(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])")


def split_identifier(identifier):
    """`CheckCircleFill12` -> ("check-circle-fill", 12)."""
    match = IDENT.match(identifier)
    if not match:
        return None, None
    base, size = match.group(1), match.group(2)
    slug = "-".join(part.lower() for part in CAMEL_SPLIT.split(base))
    return slug, int(size) if size else DEFAULT_SIZE


def to_identifier(slug, size=None):
    """`code-pull-request`, 12 -> "CodePullRequest12"."""
    name = "".join(part.capitalize() for part in slug.split("-"))
    return name if size in (None, DEFAULT_SIZE) else "%s%d" % (name, size)


# --- overrides -------------------------------------------------------------
def load_overrides():
    """Optional per-icon pins: `<Identifier> <pack|-> [threshold] [rot=N]`.

    Needed when two packs share a name and the wrong one wins, when a glyph
    needs a different alpha cut, or when a glyph has to point somewhere: rot=N
    rasterizes N evenly spaced rotations of the same drawing. The rotation is
    applied to the vector before rasterizing, so every frame is as clean as the
    unrotated one -- rotating the finished bitmap would not be.

    base=D pre-rotates the drawing so that frame 0 points north, overriding the
    pack's own rotation_offset. The pack default covers the usual case -- a
    pack draws its vehicles at one house angle -- and base= handles the glyph
    that disagrees with its own pack. Either way the generator measures frame 0
    and fails the build if it does not end up pointing north.
    """
    overrides = {}
    if not os.path.exists(OVERRIDES):
        return overrides
    with open(OVERRIDES, encoding="utf-8") as handle:
        for lineno, raw in enumerate(handle, 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            fields = line.split()
            rotations = 1
            base = None          # None: inherit the pack's rotation_offset
            rest = []
            for field in fields[2:]:
                if field.startswith("rot="):
                    rotations = int(field[4:])
                elif field.startswith("base="):
                    base = float(field[5:])
                else:
                    rest.append(field)
            if len(fields) < 2 or len(rest) > 1:
                sys.exit("%s:%d: expected "
                         "`<Identifier> <pack|-> [threshold] [rot=N]`"
                         % (OVERRIDES, lineno))
            identifier, pack = fields[0], fields[1]
            threshold = int(rest[0]) if rest else None
            if not 1 <= rotations <= 64:
                sys.exit("%s:%d: rot must be 1..64" % (OVERRIDES, lineno))
            if base is not None and not -360.0 <= base <= 360.0:
                sys.exit("%s:%d: base must be -360..360" % (OVERRIDES, lineno))
            if pack != "-" and pack not in PACKS:
                sys.exit("%s:%d: unknown pack '%s'" % (OVERRIDES, lineno, pack))
            if threshold is not None and not 1 <= threshold <= 254:
                sys.exit("%s:%d: threshold must be 1..254" % (OVERRIDES, lineno))
            overrides[identifier] = (None if pack == "-" else pack, threshold,
                                     rotations, base)
    return overrides


# --- resolution ------------------------------------------------------------
class Resolved:
    def __init__(self, identifier, pack, path, slug, size, threshold,
                 rotations=1, base=0.0):
        self.identifier = identifier
        self.pack = pack
        self.path = path          # path inside the pack, without .svg
        self.slug = slug
        self.size = size
        self.threshold = threshold
        self.rotations = rotations
        self.base = base

    @property
    def svg(self):
        return os.path.join(ICON_DIR, self.pack, self.path + ".svg")

    @property
    def url(self):
        return PACKS[self.pack]["svg"].format(path=self.path)


def _pack_path(entry, size):
    """The path inside a pack for one entry at a requested render size."""
    if entry.pack == "fontawesome":
        for style in FA_STYLE_ORDER:
            if style in entry.variants:
                return "%s/%s" % (style, entry.name)
        return None
    if entry.pack == "octicons":
        if not entry.variants:
            return None
        # Octicons draws each icon per size. Take the smallest drawing that is
        # at least the render size so a glyph is never scaled up, falling back
        # to the largest available.
        larger = sorted(v for v in entry.variants if v >= size)
        chosen = larger[0] if larger else max(entry.variants)
        return "%s-%d" % (entry.name, chosen)
    return entry.name


def resolve(identifier, catalogs, overrides):
    """Map a C++ `Icon::` identifier onto a pack, path and render size."""
    slug, size = split_identifier(identifier)
    if not slug:
        return None, "'%s' is not a PascalCase identifier" % identifier

    pinned_pack, pinned_threshold, rotations, base = overrides.get(
        identifier, (None, None, 1, None))
    threshold = pinned_threshold or DEFAULT_THRESHOLD
    order = (pinned_pack,) if pinned_pack else PACK_ORDER

    for pack in order:
        entry = catalogs.get(pack, {}).get(slug)
        if not entry:
            continue
        path = _pack_path(entry, size)
        if not path:
            continue
        # A pack's rotation_offset exists to make frame 0 point north, so it
        # applies only to a glyph that actually stores headings. A static icon
        # has no direction to align and must be drawn exactly as it is drawn;
        # an explicit base= is still honoured either way.
        if base is not None:
            effective = base
        elif rotations > 1:
            effective = PACKS[pack].get("rotation_offset", 0.0)
        else:
            effective = 0.0
        return Resolved(identifier, pack, path, slug, size, threshold,
                        rotations, effective), None

    where = pinned_pack or "any pack"
    return None, ("no icon named '%s' in %s; try "
                  "python3 scripts/gen_icons.py --search %s"
                  % (slug, where, slug.split("-")[0]))


# --- search ----------------------------------------------------------------
def search(terms, catalogs, limit=40):
    """Rank catalog entries against whitespace-separated search terms.

    Packs outside PACK_ORDER are listed too, marked by the caller, since
    knowing an icon exists elsewhere is the first half of deciding to pin it.
    """
    needles = [t.lower() for t in terms if t]
    results = []
    for pack in sorted(catalogs, key=lambda p: (p not in PACK_ORDER, p)):
        for name, entry in catalogs.get(pack, {}).items():
            haystack = name.replace("-", " ") + " " + entry.keywords
            score = 0
            for needle in needles:
                if needle == name:
                    score += 100
                elif needle in name.split("-"):
                    score += 40
                elif needle in name:
                    score += 20
                elif needle in haystack:
                    score += 8
                else:
                    score = -1
                    break
            if score > 0:
                results.append((score, pack, entry))
    results.sort(key=lambda row: (-row[0], row[1], row[2].name))
    return results[:limit]
