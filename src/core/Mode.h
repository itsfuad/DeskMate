// Mode.h — display feature + background polling contract.
//
// A feature owns its cached snapshot and renderer. The central PollScheduler
// owns timing, fairness, coalescing and retry policy so selected carousel
// screens stay fresh without every feature starting network work independently.
#pragma once
#include <Arduino.h>
#include "Settings.h"

enum class PollResult : uint8_t {
  Success,   // complete snapshot committed
  Failed,    // request finished/aborted without a new snapshot
  MoreWork,  // one bounded phase completed; schedule the next phase soon
  Skipped    // configuration/heap not ready; retry later without error escalation
};

class DisplayMode {
 public:
  virtual ~DisplayMode() {}

  virtual const char* id() const = 0;
  virtual uint8_t modeConst() const = 0;

  virtual void begin(const Settings& s) {}
  virtual void invalidate(const Settings& s) {}
  virtual void wake(const Settings& s) { invalidate(s); }

  // Polling is deliberately separate from rendering. Hidden selected carousel
  // features may poll and update their cache, while only the visible mode draws.
  virtual uint32_t pollIntervalMs(const Settings& s) const { return 0; }
  virtual uint16_t pollBudgetMs(const Settings& s) const {
    return static_cast<uint16_t>(min<uint32_t>(s.httpTimeout, 6000UL));
  }
  virtual uint8_t pollCost() const { return 1; }  // 1=light ... 5=TLS-heavy
  virtual PollResult poll(const Settings& s, uint16_t budgetMs) {
    (void)s; (void)budgetMs; return PollResult::Skipped;
  }

  // The scheduler calls this on the currently visible feature immediately
  // before and after a blocking network job. Modes with a status LED can paint
  // a solid blue "busy" state before the call begins, so a necessary pause is
  // communicated rather than looking like a frozen animation.
  virtual void pollActivityChanged(const Settings& s, bool busy) {
    (void)s; (void)busy;
  }

  // Called every loop for the visible feature only. It may update clock-driven
  // text and render a dirty cached snapshot, but must never start an API call.
  virtual void displayTick(const Settings& s) {}
};
