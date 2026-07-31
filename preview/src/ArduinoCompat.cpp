#include "Arduino.h"

#include <atomic>

namespace {
std::atomic<uint32_t> previewMillis{0};
}

uint32_t millis() { return previewMillis.load(); }

void previewSetMillis(uint32_t value) { previewMillis.store(value); }

void delay(uint32_t milliseconds) {
  previewMillis.fetch_add(milliseconds);
}

void yield() {}
