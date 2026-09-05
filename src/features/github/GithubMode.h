#pragma once
#include "Mode.h"

// GitHub activity screen.
//
// The screen answers "is something waiting on me?" before it answers "how much
// have I done?". It auto-rotates three pages: an inbox of work addressed to the
// viewer, the viewer's own open pull requests with their review and CI state,
// and the contribution pulse.
//
// Which pages take part is configured, not fixed, and the screen's share of
// display time is divided between the selected ones: a ten-second carousel
// dwell with three pages selected shows each for a third of it, so one visit
// covers the whole rotation.
//
// Polling runs in two phases against one endpoint. The action lists are small
// and are refreshed every cycle; the three-month contribution calendar is the
// bulk of the payload and changes once a day, so it is refreshed hourly and
// requested as a scheduler continuation (PollResult::MoreWork).
class GithubMode : public DisplayMode {
 public:
  const char* id() const override { return "github"; }
  uint8_t modeConst() const override { return MODE_GITHUB; }
  void begin(const Settings&) override;
  void invalidate(const Settings&) override;
  void wake(const Settings&) override;
  uint32_t pollIntervalMs(const Settings&) const override;
  uint16_t pollBudgetMs(const Settings&) const override;
  uint8_t pollCost() const override { return 5; }
  bool requiresInternet() const override { return true; }
  PollResult poll(const Settings&, uint16_t budgetMs) override;
  void pollActivityChanged(const Settings&, bool busy) override;
  void pollResultChanged(const Settings&, PollResult result) override;
  void displayTick(const Settings&) override;

 private:
  enum class Phase : uint8_t { Lists, Calendar };

  bool dirty_ = true;
  Phase phase_ = Phase::Lists;
  uint32_t calendarAt_ = 0;      // millis() of the last calendar attempt
  bool calendarAttempted_ = false;
  uint8_t page_ = 0;      // a PAGE_* id, not a position in the rotation
  uint32_t pageAt_ = 0;
  bool pollBusy_ = false;
  uint32_t busySince_ = 0;

  bool calendarDue() const;
  // A page is in the rotation when it is selected and has data to show.
  bool pageEnabled(const Settings&, uint8_t page) const;
  uint8_t pageCount(const Settings&) const;
  uint8_t pageIndex(const Settings&, uint8_t page) const;
  uint8_t firstPage(const Settings&) const;
  uint8_t nextPage(const Settings&, uint8_t from) const;
  uint32_t pageDwellMs(const Settings&) const;
  void render(const Settings&);
};

extern GithubMode g_githubMode;
