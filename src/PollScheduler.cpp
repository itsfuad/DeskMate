#include "PollScheduler.h"

namespace {
// At least this much foreground time is given back after every network job.
constexpr uint32_t kMinimumInterJobGapMs = 80;
constexpr uint32_t kMaximumInterJobGapMs = 900;
constexpr uint32_t kContinuationGapMs = 60;

// Network work may consume at most about 45% of long-run wall time. A short
// burst is allowed so cold start and the next carousel screen can prefetch
// quickly; slow providers then create credit debt and automatically shed load.
constexpr int32_t kDutyPercent = 45;
constexpr int32_t kCreditCapMs = 2600;
constexpr int32_t kPriorityDebtLimitMs = -3500;
constexpr int32_t kNormalDebtLimitMs = -300;
}

void PollScheduler::bind(DisplayMode** modes, uint8_t count) {
  count_ = count < MaxModes ? count : MaxModes;
  for (uint8_t i = 0; i < count_; ++i) modes_[i] = modes[i];
}

void PollScheduler::begin(const Settings&) {
  const uint32_t now = millis();
  lastCreditUpdate_ = now;
  networkCreditsMs_ = kCreditCapMs;
  for (uint8_t i = 0; i < count_; ++i) {
    runtime_[i] = Runtime();
    // Stagger cold-start work. Ten selected sources do not all become due on
    // the same loop, and no queue of obsolete one-second deadlines is created.
    runtime_[i].nextDue = now + 650UL + static_cast<uint32_t>(i) * 320UL;
  }
}

void PollScheduler::forceAll() {
  for (uint8_t i = 0; i < count_; ++i) runtime_[i].forced = true;
}

void PollScheduler::force(uint8_t modeConst) {
  for (uint8_t i = 0; i < count_; ++i) {
    if (modes_[i] && modes_[i]->modeConst() == modeConst) {
      runtime_[i].forced = true;
      return;
    }
  }
}

uint8_t PollScheduler::indexOf(DisplayMode* mode) const {
  if (!mode) return 0xFF;
  for (uint8_t i = 0; i < count_; ++i) {
    if (modes_[i] == mode) return i;
  }
  return 0xFF;
}

void PollScheduler::refillCredits(uint32_t now) {
  const uint32_t elapsed = now - lastCreditUpdate_;
  if (!elapsed) return;
  lastCreditUpdate_ = now;

  const int32_t refill = static_cast<int32_t>(
      (static_cast<uint64_t>(elapsed) * kDutyPercent) / 100ULL);
  const int64_t next = static_cast<int64_t>(networkCreditsMs_) + refill;
  networkCreditsMs_ = static_cast<int32_t>(
      next > kCreditCapMs ? kCreditCapMs : next);
}

uint32_t PollScheduler::jitteredInterval(uint32_t interval,
                                         uint8_t index) const {
  if (interval < 1000UL) return interval;
  // Deterministic +/-4% jitter prevents periodic sources from permanently
  // lining up while remaining reproducible and requiring no random state.
  const int8_t step = static_cast<int8_t>(
      (index * 37U + completedJobs_ * 13U) % 9U) - 4;
  const int32_t delta = static_cast<int32_t>(interval / 100UL) * step;
  const int32_t value = static_cast<int32_t>(interval) + delta;
  return static_cast<uint32_t>(value > 250 ? value : 250);
}

uint32_t PollScheduler::predictedDurationMs(uint8_t index,
                                             const Settings& settings) const {
  const Runtime& r = runtime_[index];
  if (r.averageDurationMs) return r.averageDurationMs;
  const DisplayMode* mode = modes_[index];
  const uint32_t byCost = 90UL + static_cast<uint32_t>(mode->pollCost()) * 170UL;
  const uint32_t budget = mode->pollBudgetMs(settings);
  return byCost < budget ? byCost : budget;
}

void PollScheduler::service(const Settings& settings, const bool* enabled,
                            DisplayMode* active, DisplayMode* upcoming) {
  if (!count_ || !enabled) return;
  const uint32_t now = millis();
  refillCredits(now);

  const uint32_t dynamicGap = lastJobDurationMs_ == 0
      ? kMinimumInterJobGapMs
      : min<uint32_t>(kMaximumInterJobGapMs,
                      max<uint32_t>(kMinimumInterJobGapMs,
                                    lastJobDurationMs_ / 3UL));
  if (static_cast<uint32_t>(now - lastJobEnd_) < dynamicGap) return;

  const uint8_t activeIndex = indexOf(active);
  const uint8_t upcomingIndex = indexOf(upcoming);
  int best = -1;
  int32_t bestScore = -1;

  for (uint8_t i = 0; i < count_; ++i) {
    Runtime& r = runtime_[i];
    DisplayMode* mode = modes_[i];
    if (!mode) continue;
    const uint32_t interval = mode->pollIntervalMs(settings);
    if (!interval) continue;

    if (!enabled[i]) {
      r.wasEnabled = false;
      r.continuation = false;
      continue;
    }
    if (!r.wasEnabled) {
      r.wasEnabled = true;
      r.forced = true;  // newly selected carousel screen gets a fresh snapshot
    }

    // If a source is many periods late, count and discard historical deadlines.
    // There remains exactly one pending obligation: fetch the newest snapshot.
    if (!r.forced && !r.continuation &&
        static_cast<int32_t>(now - r.nextDue) >= 0) {
      const uint32_t overdue = now - r.nextDue;
      if (overdue >= interval) {
        const uint32_t missed = overdue / interval;
        coalescedDeadlines_ += missed;
        r.nextDue += missed * interval;
      }
    }

    const bool due = r.forced || r.continuation ||
                     static_cast<int32_t>(now - r.nextDue) >= 0;
    if (!due) continue;

    const bool priority = r.continuation || r.forced ||
                          i == activeIndex || i == upcomingIndex;
    const int32_t debtLimit = priority ? kPriorityDebtLimitMs
                                       : kNormalDebtLimitMs;
    const uint32_t predicted = predictedDurationMs(i, settings);
    if (networkCreditsMs_ - static_cast<int32_t>(predicted) < debtLimit) {
      ++budgetDeferrals_;
      continue;
    }

    int32_t score = 0;
    if (r.continuation) score += 1200;
    if (r.forced) score += 850;
    if (i == upcomingIndex) score += 540;
    if (i == activeIndex) score += 390;
    if (static_cast<int32_t>(now - r.nextDue) > 0) {
      score += min<int32_t>(260,
          static_cast<int32_t>((now - r.nextDue) / 1000UL));
    }
    const uint8_t cost = mode->pollCost() > 5 ? 5 : mode->pollCost();
    score += 20 - static_cast<int32_t>(cost) * 3;

    if (score > bestScore) {
      bestScore = score;
      best = i;
    }
  }

  if (best < 0) return;
  Runtime& r = runtime_[best];
  DisplayMode* mode = modes_[best];
  r.forced = false;
  r.lastAttempt = now;
  currentJob_ = mode->id();

  uint16_t budget = mode->pollBudgetMs(settings);
  if (budget < 250) budget = 250;
  const uint32_t started = millis();
  const PollResult result = mode->poll(settings, budget);
  const uint32_t finished = millis();
  const uint32_t duration = finished - started;

  r.lastDurationMs = duration;
  r.averageDurationMs = r.averageDurationMs
      ? (r.averageDurationMs * 3UL + duration) / 4UL
      : duration;
  lastJobDurationMs_ = duration;
  averageJobDurationMs_ = averageJobDurationMs_
      ? (averageJobDurationMs_ * 7UL + duration) / 8UL
      : duration;
  networkCreditsMs_ -= static_cast<int32_t>(
      duration > 60000UL ? 60000UL : duration);
  if (networkCreditsMs_ < kPriorityDebtLimitMs - 60000) {
    networkCreditsMs_ = kPriorityDebtLimitMs - 60000;
  }

  lastJobEnd_ = finished;
  currentJob_ = "idle";

  switch (result) {
    case PollResult::Success:
      ++completedJobs_;
      r.failures = 0;
      r.continuation = false;
      r.lastSuccess = finished;
      r.nextDue = finished + jitteredInterval(
          mode->pollIntervalMs(settings), static_cast<uint8_t>(best));
      break;

    case PollResult::MoreWork:
      r.continuation = true;
      r.nextDue = finished + kContinuationGapMs;
      break;

    case PollResult::Failed: {
      ++failedJobs_;
      r.continuation = false;
      const uint8_t nextFailures = static_cast<uint8_t>(r.failures + 1U);
      r.failures = nextFailures > 8 ? 8 : nextFailures;
      const uint32_t normal = mode->pollIntervalMs(settings);
      const uint8_t shift = r.failures > 8 ? 8 : r.failures;
      const uint32_t exponential = min<uint32_t>(
          300000UL, 1000UL << shift);
      // Never accumulate missed calls. One failed source owns one future retry,
      // bounded by its normal cadence, not N queued historical deadlines.
      r.nextDue = finished + min<uint32_t>(normal, exponential);
      break;
    }

    case PollResult::Skipped:
      r.continuation = false;
      // Missing credentials/location and temporary low-heap conditions must not
      // turn into a tight three-second spin when many screens are selected.
      r.nextDue = finished + min<uint32_t>(
          mode->pollIntervalMs(settings), 60000UL);
      break;
  }
}
