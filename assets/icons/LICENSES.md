# Icon sources

Screens draw from **Lucide only**. It is one ISC-licensed set with no
attribution obligation and a single visual language, so every glyph on the
device shares a stroke weight and a corner radius.

Two other packs stay indexed and searchable so an icon Lucide lacks can be
pinned in `assets/icons.overrides`, but nothing resolves to them by default and
no artwork from them ships today.

| Pack | Upstream | License | Resolves by default | Art shipped |
| --- | --- | --- | --- | --- |
| `lucide` | [lucide-icons/lucide](https://github.com/lucide-icons/lucide) | ISC | **yes** | yes |
| `octicons` | [primer/octicons](https://github.com/primer/octicons) | MIT | no | no |
| `fontawesome` | [FortAwesome/Font-Awesome](https://github.com/FortAwesome/Font-Awesome) 7.x | Icons CC BY 4.0, code MIT | no | no |

`*.index` files are catalogs of names and search terms built from each pack's
own metadata, which those projects license as code (MIT). Pinning a
FontAwesome icon would ship CC BY 4.0 artwork and require adding their
`LICENSE.txt` here plus a visible attribution notice; nothing does today.

## What Lucide costs

Lucide draws strokes, never fills, on a 24-unit grid with a 2-pixel stroke.
Three consequences shaped the current screens:

**Status badges are rings, not discs.** There is no filled check or dot, so a
passing check is an outlined circle. It reads well, but only at 14 px with the
alpha threshold cut to 64 — at the 12 px a filled glyph tolerated, the stroke
lands near one pixel and the ring breaks up. Those cuts live in
`assets/icons.overrides`.

**No brand marks.** Lucide ships no GitHub logo, so the loading screen uses
`git-graph`.

**No `code-review`.** A review request uses `message-square-code`, a code
comment, which is arguably the more literal metaphor anyway.

## Using an icon

Reference it in firmware code and regenerate. There is no list to edit.

```cpp
gfxDrawIcon(canvas, Icon::CodeMerge, x, y, color);
```

```bash
python3 scripts/gen_icons.py --search merge   # find the name, offline
python3 scripts/gen_icons.py                  # vendor + rasterize + emit
```

`Icon::Name` is the pack's own name in PascalCase, and a trailing number is the
render size (default 16), so `Icon::DotFill12` and `Icon::DotFill24` are one
drawing at two sizes. An icon that stops being referenced stops being compiled
in on the next run.

`assets/icons.overrides` exists only for the two cases the rules cannot infer:
pinning a pack when several ship the same name, and tuning the alpha threshold
for a glyph that comes out too heavy or too thin.

## Resolution order

`octicons`, then `fontawesome`, then `lucide` (`PACK_ORDER` in
`scripts/icon_index.py`). Octicons is first because this firmware's screens
mostly speak GitHub's vocabulary and Octicons ships size-specific drawings --
`git-commit-16.svg` is drawn for a 16-pixel grid, and at 1 bpp that shows.

FontAwesome covers the same ground (`code-pull-request`, `code-commit`,
`code-merge`, `at`, `inbox`) and is a line away in `icons.overrides`. Two
things differ in practice:

**Grid.** Octicons ships size-specific drawings, so a 12- or 16-pixel request
gets art hinted for that grid. FontAwesome draws once for higher-DPI rendering
and is scaled down, which softens fine detail. Raising an icon's threshold
thins a glyph that fills in; `--preview` shows the result before it reaches
hardware.

**Size in the box.** Covered above: `svgs-full` art fills ~83% of its box
against Octicons' ~93%. Both keep correct proportions -- the generator centers
any non-square glyph rather than stretching it -- but a FontAwesome icon reads
slightly smaller unless you ask for a larger size.

## Refreshing the catalogs

```bash
python3 scripts/index_icons.py                       # all packs, over the network
python3 scripts/index_icons.py --from DIR fontawesome  # from a local package
```

Needed only to pick up icons added upstream. Everything else -- including CI --
runs against the committed catalogs with no network access.

## Working from a local FontAwesome package

FontAwesome's official downloads can stand in for the network entirely. Point
`--from` at an unpacked package once:

```bash
python3 scripts/index_icons.py --from ~/Downloads/fontawesome-free-7.3.1-desktop fontawesome
```

The location is remembered in `assets/icons/sources.conf` (gitignored, since it
is an absolute machine-specific path), and later `gen_icons.py` runs vendor
glyphs straight out of it. A machine without the package silently falls back to
downloading, so nothing breaks for anyone else.

**Which download.** "Free for Desktop" and "Free for Web" carry byte-identical
`metadata/`, `svgs/` and `svgs-full/`; only their extras differ, and none of
those extras are usable here -- this firmware has no browser, no DOM and no
font renderer, so the web package's CSS, JS, SCSS, sprites and woff2 and the
desktop package's OTFs are all dead weight. Either download works; the desktop
one is the smaller of the two.

**svgs-full, not svgs.** The generator reads `svgs-full/`, where every drawing
is normalized to a square 640x640 box. FontAwesome's default `svgs/` are
tight-cropped, so 1762 of its 2883 drawings have a non-square viewBox and would
sit letterboxed in a square icon box. The trade is padding: `svgs-full` art
fills about 83% of its box against Octicons' 93%, so a FontAwesome glyph reads
roughly a tenth smaller at the same nominal size. Ask for a larger one to
match -- `Icon::CodeMerge18`.
