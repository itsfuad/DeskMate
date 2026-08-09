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

uint32_t ESPClass::getFreeHeap() const { return emulatorBoardProfile().heapBytes; }
uint32_t ESPClass::getFreeSketchSpace() const { return emulatorBoardProfile().otaSlotBytes; }
String ESPClass::getResetInfo() const { return emulatorResetInfo().reason; }
void ESPClass::restart() { emulatorRequestRestart(); }

uint32_t millis() {
  if (!realtime.load()) return syntheticMillis.load();
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startedAt).count());
}

void emulatorSetMillis(uint32_t value) {
  realtime.store(false);
  syntheticMillis.store(value);
}

void emulatorUseRealtime(bool enabled) { realtime.store(enabled); }

void delay(uint32_t milliseconds) {
  if (realtime.load()) std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
  else syntheticMillis.fetch_add(milliseconds);
}

void yield() { std::this_thread::yield(); }
int analogRead(uint8_t) { return emulatorLdrValue(); }
void analogWrite(uint8_t, int) {}
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
