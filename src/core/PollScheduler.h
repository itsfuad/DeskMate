#pragma once
#include <Arduino.h>
#include "Mode.h"

// Cooperative background-data scheduler.
//
// It deliberately owns only one in-flight job because the ESP8266 cannot keep
// several TLS clients and JSON parsers resident safely. When demand exceeds the
// available network/CPU budget, deadlines are coalesced: every source retains a
// single "latest refresh" obligation instead of building an unbounded queue of
// obsolete polls.
class PollScheduler {
 public:
  static constexpr uint8_t MaxModes = 10;

  void bind(DisplayMode** modes, uint8_t count);
  void begin(const Settings& settings);
  void forceAll();
  void force(uint8_t modeConst);

  // enabled[i] describes the current runtime selection. active and upcoming are
  // priority hints only; every enabled source keeps its own independent cadence.
  void service(const Settings& settings, const bool* enabled,
               DisplayMode* active, DisplayMode* upcoming);

  uint32_t completedJobs() const { return completedJobs_; }
  uint32_t failedJobs() const { return failedJobs_; }
  uint32_t coalescedDeadlines() const { return coalescedDeadlines_; }
  uint32_t budgetDeferrals() const { return budgetDeferrals_; }
  uint32_t lastJobDurationMs() const { return lastJobDurationMs_; }
  uint32_t averageJobDurationMs() const { return averageJobDurationMs_; }
  int32_t networkCreditsMs() const { return networkCreditsMs_; }
  const char* currentJob() const { return currentJob_; }

 private:
  struct Runtime {
    uint32_t nextDue = 0;
    uint32_t lastAttempt = 0;
    uint32_t lastSuccess = 0;
    uint32_t lastDurationMs = 0;
    uint32_t averageDurationMs = 0;
    uint8_t failures = 0;
    bool forced = false;
    bool continuation = false;
    bool wasEnabled = false;
    bool budgetDeferred = false;
  };

  DisplayMode* modes_[MaxModes] = {};
  Runtime runtime_[MaxModes];
  uint8_t count_ = 0;

  uint32_t lastJobEnd_ = 0;
  uint32_t lastCreditUpdate_ = 0;
  int32_t networkCreditsMs_ = 0;
  uint32_t completedJobs_ = 0;
  uint32_t failedJobs_ = 0;
  uint32_t coalescedDeadlines_ = 0;
  uint32_t budgetDeferrals_ = 0;
  uint32_t lastJobDurationMs_ = 0;
  uint32_t averageJobDurationMs_ = 0;
  const char* currentJob_ = "idle";

  uint8_t indexOf(DisplayMode* mode) const;
  uint32_t jitteredInterval(uint32_t interval, uint8_t index) const;
  uint32_t predictedDurationMs(uint8_t index, const Settings& settings) const;
  void refillCredits(uint32_t now);
};
