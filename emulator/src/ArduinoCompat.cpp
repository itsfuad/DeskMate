#include "Arduino.h"
#include "EmulatorPlatform.h"

#include <atomic>
#include <cctype>
#include <iostream>

namespace {
const auto startedAt = std::chrono::steady_clock::now();
std::atomic<bool> realtime{true};
std::atomic<uint32_t> syntheticMillis{0};
}

namespace {
int nothrowCountdown = -1;
int stringCountdown = -1;
bool permitAllocation(int& countdown) {
  if (countdown < 0) return true;
  if (countdown-- > 0) return true;
  countdown = -1;
  return false;
}
}

void emulatorFailNothrowAfter(int count) { nothrowCountdown = count; }
void emulatorFailStringGrowthAfter(int count) { stringCountdown = count; }
bool emulatorStringGrowthAllowed() { return permitAllocation(stringCountdown); }

// Only explicit checked allocations are intercepted; stdlib/OpenSSL and ordinary
// new retain their native host allocation behavior and sanitizer instrumentation.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  if (!permitAllocation(nothrowCountdown)) return nullptr;
  try { return ::operator new(size); }
  catch (const std::bad_alloc&) { return nullptr; }
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept {
  ::operator delete(pointer);
}

HardwareSerial Serial;
ESPClass ESP;

bool String::equalsIgnoreCase(const String& other) const {
  if (length() != other.length()) return false;
  for (size_t i = 0; i < length(); ++i) {
    if (std::tolower(static_cast<unsigned char>((*this)[i])) !=
        std::tolower(static_cast<unsigned char>(other[i]))) return false;
  }
  return true;
}

void String::trim() {
  const size_t first = value_.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) { value_.clear(); return; }
  const size_t last = value_.find_last_not_of(" \t\r\n");
  value_ = value_.substr(first, last - first + 1);
}

void HardwareSerial::print(const char* value) { std::cout << (value ? value : ""); }
void HardwareSerial::print(const String& value) { std::cout << value.c_str(); }
void HardwareSerial::print(unsigned long value) { std::cout << value; }
void HardwareSerial::println() { std::cout << '\n' << std::flush; }

uint32_t ESPClass::getFreeHeap() const { return emulatorFreeHeap(); }
uint32_t ESPClass::getFreeSketchSpace() const { return emulatorBoardProfile().otaSlotBytes; }
String ESPClass::getResetInfo() const { return emulatorResetInfo().reason; }
void ESPClass::restart() { emulatorRequestRestart(); }

uint32_t millis() {
  if (!realtime.load()) return syntheticMillis.load();
  const uint64_t elapsed = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - startedAt).count());
  return static_cast<uint32_t>(elapsed * emulatorTimeScale());
}

void emulatorSetMillis(uint32_t value) {
  realtime.store(false);
  syntheticMillis.store(value);
}

void emulatorUseRealtime(bool enabled) { realtime.store(enabled); }

void delay(uint32_t milliseconds) {
  if (realtime.load()) {
    const uint32_t scale = emulatorTimeScale();
    const uint32_t wallMs = std::max<uint32_t>(1, milliseconds / scale);
    std::this_thread::sleep_for(std::chrono::milliseconds(wallMs));
  } else syntheticMillis.fetch_add(milliseconds);
}

void yield() { std::this_thread::yield(); }
int analogRead(uint8_t) { return emulatorLdrValue(); }
void analogWrite(uint8_t, int) {}
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
