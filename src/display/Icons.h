// Icons.h — vector-sourced UI glyphs, rasterized at build time.
//
// Feature screens ask for an icon by name instead of drawing one from circles
// and lines. Nothing declares the icon set: name one here and
// scripts/gen_icons.py finds the reference, resolves it against the Octicons /
// FontAwesome / Lucide catalogs in assets/icons/, and rasterizes it into the
// generated IconData.{h,cpp}. `gen_icons.py --search TERM` lists what is
// available offline.
//
// An identifier is the pack's own name in PascalCase, and a trailing number is
// the render size (default 16): Icon::DotFill12 and Icon::DotFill24 are one
// drawing at two sizes. Referencing a name that has not been generated yet is
// a compile error, which is the cue to re-run the generator.
//
// Glyphs are 1 bpp masks: set bits are painted in the requested color and clear
// bits are left untouched, so an icon composites over whatever is behind it.
#pragma once
#include <Arduino.h>
#include "IconData.h"

class Adafruit_GFX;

uint8_t gfxIconW(Icon id);
uint8_t gfxIconH(Icon id);

// Top-left placement, matching Adafruit_GFX bitmap conventions.
void gfxDrawIcon(Adafruit_GFX& g, Icon id, int16_t x, int16_t y,
                 uint16_t color);

// Places the glyph box's center on (cx, cy). Row layouts line icons up with
// text baselines more readably this way than by juggling per-icon offsets.
void gfxDrawIconCentered(Adafruit_GFX& g, Icon id, int16_t cx, int16_t cy,
                         uint16_t color);

// How many rotation frames an icon stores. 1 for a glyph that does not point
// anywhere, which is nearly all of them.
uint8_t gfxIconFrames(Icon id);

// Draws the frame nearest `degrees` clockwise from north, centred on (cx, cy).
// Rotations are rasterized from the vector at build time, so each frame is as
// clean as the unrotated glyph. An icon with a single frame is simply drawn
// unrotated, which keeps call sites free of special cases.
void gfxDrawIconRotated(Adafruit_GFX& g, Icon id, int16_t cx, int16_t cy,
                        float degrees, uint16_t color);
