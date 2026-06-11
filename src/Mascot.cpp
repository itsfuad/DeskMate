#include "Mascot.h"
#include "mascot_frames.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Burn-rate tracker (ported from clawdmeter-win's usage_rate.cpp).
//
// We watch how fast the 5-hour session utilization climbs (%/min) and bucket it
// into four moods. A full 300-min session ÷ 100% = 0.33 %/min is "pace-matching"
// (you'll hit the cap right at the reset), so that's the Heavy threshold.
//   < 0.10 %/min -> idle    (basically dormant)
//   < 0.20       -> normal  (slow steady use)
//   < 0.33       -> active  (heavy but not pace-matching)
//   >= 0.33      -> heavy   (matching or beating the session reset)
// ---------------------------------------------------------------------------
#define RATE_THRESH_NORMAL  0.10f
#define RATE_THRESH_ACTIVE  0.20f
#define RATE_THRESH_HEAVY   0.33f

// Need a few minutes of accumulated history before the rate is trustworthy —
// otherwise a single +1% poll-to-poll bump reads as 1 %/min (Heavy). Side-effect:
// the creature stays idle for the first ~4 min after boot.
#define MIN_WINDOW_MS       240000UL
#define RING_SIZE           6

// Auto-cycle to another animation in the current mood group this often.
#define ROTATE_INTERVAL_MS  20000UL

struct Sample { uint32_t ms; float pct; };
static Sample   s_ring[RING_SIZE];
static uint8_t  s_count = 0;
static uint8_t  s_head  = 0;            // next write slot

static uint16_t s_curAnim   = 0;
static uint16_t s_curFrame  = 0;
static uint32_t s_frameStartMs = 0;
static uint32_t s_lastPickMs   = 0;
static uint8_t  s_groupRotation[4] = {0};

static inline uint8_t oldestIdx() { return (s_head + RING_SIZE - s_count) % RING_SIZE; }

static void rateReset() { s_count = 0; s_head = 0; }

static int rateGroup() {
  if (s_count < 2) return 0;
  uint8_t o = oldestIdx();
  uint8_t l = (s_head + RING_SIZE - 1) % RING_SIZE;
  uint32_t dt = s_ring[l].ms - s_ring[o].ms;
  if (dt < MIN_WINDOW_MS) return 0;

  float dp = s_ring[l].pct - s_ring[o].pct;
  if (dp < 0.0f) dp = 0.0f;
  float rate = dp * 60000.0f / (float)dt;

  if (rate < RATE_THRESH_NORMAL) return 0;
  if (rate < RATE_THRESH_ACTIVE) return 1;
  if (rate < RATE_THRESH_HEAVY)  return 2;
  return 3;
}

// Pick the next animation belonging to the current mood group, rotating through
// the (possibly several) animations tagged with that group. Falls back to group 0.
static void pickForRate() {
  int g = rateGroup();
  if (g < 0 || g > 3) g = 0;

  // Count animations in group g.
  uint8_t inGroup = 0;
  for (uint8_t i = 0; i < MASCOT_ANIM_COUNT; i++)
    if (mascot_anims[i].group == g) inGroup++;
  if (inGroup == 0) { g = 0; for (uint8_t i = 0; i < MASCOT_ANIM_COUNT; i++) if (mascot_anims[i].group == 0) inGroup++; }
  if (inGroup == 0) return;

  uint8_t slot = s_groupRotation[g] % inGroup;
  s_groupRotation[g]++;

  uint8_t seen = 0;
  for (uint8_t i = 0; i < MASCOT_ANIM_COUNT; i++) {
    if (mascot_anims[i].group != g) continue;
    if (seen == slot) { s_curAnim = i; break; }
    seen++;
  }
  s_curFrame = 0;
  s_frameStartMs = millis();
  s_lastPickMs = s_frameStartMs;
}

// ---------------------------------------------------------------------------
void mascotInit() {
  rateReset();
  s_curAnim = 0;
  s_curFrame = 0;
  s_frameStartMs = millis();
  s_lastPickMs = s_frameStartMs;
}

void mascotReset() {
  s_curFrame = 0;
  s_frameStartMs = millis();
  s_lastPickMs = s_frameStartMs;
}

void mascotSample(float sessionPct) {
  uint32_t now = millis();
  if (s_count > 0) {
    uint8_t latest = (s_head + RING_SIZE - 1) % RING_SIZE;
    if (sessionPct + 5.0f < s_ring[latest].pct) rateReset();  // session reset -> restart
  }
  s_ring[s_head] = { now, sessionPct };
  s_head = (s_head + 1) % RING_SIZE;
  if (s_count < RING_SIZE) s_count++;
}

bool mascotTick() {
  bool changed = false;

  if (millis() - s_lastPickMs >= ROTATE_INTERVAL_MS) {
    pickForRate();
    changed = true;
  }

  const MascotAnim& a = mascot_anims[s_curAnim];
  if (a.frame_count == 0) return changed;

  uint16_t hold = pgm_read_word(&a.holds[s_curFrame]);
  if (millis() - s_frameStartMs >= hold) {
    s_curFrame = (s_curFrame + 1) % a.frame_count;
    s_frameStartMs = millis();
    changed = true;
  }
  return changed;
}

const uint8_t*  mascotCells()   { return mascot_anims[s_curAnim].frames[s_curFrame]; }
const uint16_t* mascotPalette() { return mascot_anims[s_curAnim].palette; }
const char*     mascotName()    { return mascot_anims[s_curAnim].name; }

const uint8_t*  mascotIdleCells()   { return mascot_anims[0].frames[0]; }
const uint16_t* mascotIdlePalette() { return mascot_anims[0].palette; }
