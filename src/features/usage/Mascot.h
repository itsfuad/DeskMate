// Mascot.h — the pixel-art "Claude usage" creature animation.
//
// Holds the animation state machine: which of the curated claudepix animations
// is playing, frame timing, and a burn-rate tracker that switches the creature
// between idle / normal / active / heavy moods based on how fast the 5-hour
// session window is filling up. Frame *data* lives PROGMEM in mascot_frames.h;
// the renderer (Display) reads cells/palette back via pgm_read_*.
#pragma once
#include <stdint.h>

// Mascot canvas geometry (also defined, guarded, in the generated mascot_frames.h).
#ifndef MASCOT_GRID
#define MASCOT_GRID          20
#endif
#ifndef MASCOT_PALETTE_SIZE
#define MASCOT_PALETTE_SIZE  10
#endif

void mascotInit();                      // one-time setup
void mascotReset();                     // restart from frame 0 (on entering the screen)
void mascotSample(float sessionPct);    // feed the burn-rate tracker (per usage update)
bool mascotTick();                      // advance frames/anim; true if something changed

// Current frame, for the renderer.
const uint8_t*  mascotCells();          // 400 cells (20x20) in PROGMEM — read with pgm_read_byte
const uint16_t* mascotPalette();        // MASCOT_PALETTE_SIZE RGB565 entries (RAM, index directly)
const char*     mascotName();           // current animation name (debug)

// A fixed calm idle pose, for the small header logo on the stats screen. Lets
// Display reuse the frame data without re-including (and re-instantiating) it.
const uint8_t*  mascotIdleCells();
const uint16_t* mascotIdlePalette();
