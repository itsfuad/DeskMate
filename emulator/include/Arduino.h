#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <type_traits>

#ifndef ARDUINO
#define ARDUINO 10819
#endif
#ifndef PI
#define PI 3.14159265358979323846
#endif

#define PROGMEM
#define PGM_P const char*
#define F(value) value
#define FPSTR(value) (value)
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))
#define pgm_read_ptr(addr) (*(const void* const*)(addr))
#define strlen_P std::strlen
#define memcpy_P std::memcpy

#define DEC 10
#define HEX 16
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define A0 0

using byte = uint8_t;
using boolean = bool;

inline constexpr double radians(double value) { return value * PI / 180.0; }
inline constexpr double degrees(double value) { return value * 180.0 / PI; }

class __FlashStringHelper;

class String {
 public:
  String() = default;
  String(const char* value) : value_(value ? value : "") {}
  String(const std::string& value) : value_(value) {}
  String(char value) : value_(1, value) {}
  String(int value, unsigned base = DEC) { assignInteger(value, base); }
  String(unsigned value, unsigned base = DEC) { assignInteger(value, base); }
  String(long value, unsigned base = DEC) { assignInteger(value, base); }
  String(unsigned long value, unsigned base = DEC) { assignInteger(value, base); }
  String(long long value, unsigned base = DEC) { assignInteger(value, base); }
  String(unsigned long long value, unsigned base = DEC) { assignInteger(value, base); }
  String(float value, unsigned decimals = 2) { assignFloat(value, decimals); }
  String(double value, unsigned decimals = 2) { assignFloat(value, decimals); }

  size_t length() const { return value_.size(); }
  bool isEmpty() const { return value_.empty(); }
  const char* c_str() const { return value_.c_str(); }
  void reserve(size_t amount) { value_.reserve(amount); }
  void clear() { value_.clear(); }

  char operator[](size_t index) const { return value_[index]; }
  char& operator[](size_t index) { return value_[index]; }
  bool operator==(const String& other) const { return value_ == other.value_; }
  bool operator==(const char* other) const { return value_ == (other ? other : ""); }
  bool operator!=(const String& other) const { return !(*this == other); }
  bool operator!=(const char* other) const { return !(*this == other); }

  String& operator=(const char* value) {
    value_ = value ? value : "";
    return *this;
  }
  String& operator+=(const String& other) { value_ += other.value_; return *this; }
  String& operator+=(const char* other) { value_ += other ? other : ""; return *this; }
  String& operator+=(char other) { value_ += other; return *this; }
  String& operator+=(int value) { value_ += std::to_string(value); return *this; }
  String& operator+=(unsigned value) { value_ += std::to_string(value); return *this; }
  String& operator+=(long value) { value_ += std::to_string(value); return *this; }
  String& operator+=(unsigned long value) { value_ += std::to_string(value); return *this; }

  int indexOf(char value) const {
    const size_t at = value_.find(value);
    return at == std::string::npos ? -1 : static_cast<int>(at);
  }
  int indexOf(const char* value) const {
    const size_t at = value_.find(value ? value : "");
    return at == std::string::npos ? -1 : static_cast<int>(at);
  }
  bool startsWith(const char* value) const {
    const std::string prefix = value ? value : "";
    return value_.rfind(prefix, 0) == 0;
  }
  bool endsWith(const char* value) const {
    const std::string suffix = value ? value : "";
    return suffix.size() <= value_.size() &&
           value_.compare(value_.size() - suffix.size(), suffix.size(), suffix) == 0;
  }
  bool equalsIgnoreCase(const String& other) const;
  String substring(size_t from, size_t to = std::string::npos) const {
    if (from >= value_.size()) return String();
    if (to == std::string::npos || to > value_.size()) to = value_.size();
    return String(value_.substr(from, to > from ? to - from : 0));
  }
  long toInt() const { return std::strtol(value_.c_str(), nullptr, 10); }
  void trim();
  bool concat(const char* value) {
    value_ += value ? value : "";
    return true;
  }
  bool concat(char value) { value_ += value; return true; }

  explicit operator bool() const { return !value_.empty(); }
  operator std::string() const { return value_; }

  friend String operator+(const String& a, const String& b) { return String(a.value_ + b.value_); }
  friend String operator+(const String& a, const char* b) { return String(a.value_ + (b ? b : "")); }
  friend String operator+(const char* a, const String& b) { return String((a ? a : "") + b.value_); }

 private:
  template <typename T>
  void assignInteger(T value, unsigned base) {
    char buffer[48];
    if (base == HEX) {
      std::snprintf(buffer, sizeof(buffer), "%llx",
                    static_cast<unsigned long long>(value));
    } else if constexpr (std::is_signed_v<T>) {
      std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
    } else {
      std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    }
    value_ = buffer;
  }

  template <typename T>
  void assignFloat(T value, unsigned decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", static_cast<int>(decimals),
                  static_cast<double>(value));
    value_ = buffer;
  }

  std::string value_;
};

template <typename A, typename B>
constexpr auto min(A a, B b) -> typename std::common_type<A, B>::type {
  using R = typename std::common_type<A, B>::type;
  return static_cast<R>(a) < static_cast<R>(b) ? static_cast<R>(a) : static_cast<R>(b);
}
template <typename A, typename B>
constexpr auto max(A a, B b) -> typename std::common_type<A, B>::type {
  using R = typename std::common_type<A, B>::type;
  return static_cast<R>(a) > static_cast<R>(b) ? static_cast<R>(a) : static_cast<R>(b);
}
template <typename T, typename L, typename H>
constexpr T constrain(T value, L low, H high) {
  return value < static_cast<T>(low) ? static_cast<T>(low)
       : value > static_cast<T>(high) ? static_cast<T>(high) : value;
}

inline size_t strlcpy(char* destination, const char* source, size_t size) {
  const char* safe = source ? source : "";
  const size_t length = std::strlen(safe);
  if (size) {
    const size_t copied = std::min(length, size - 1);
    std::memcpy(destination, safe, copied);
    destination[copied] = '\0';
  }
  return length;
}
inline size_t strlcat(char* destination, const char* source, size_t size) {
  const size_t current = std::strlen(destination);
  if (current >= size) return current + std::strlen(source ? source : "");
  return current + strlcpy(destination + current, source, size - current);
}

class Print;
class Stream;

class Printable {
 public:
  virtual ~Printable() = default;
  virtual size_t printTo(Print& output) const = 0;
};

class HardwareSerial {
 public:
  void begin(unsigned long) {}
  void print(const char* value);
  void print(const String& value);
  void print(unsigned long value);
  void println();
  template <typename T> void println(const T& value) { print(value); println(); }
};

class ESPClass {
 public:
  uint32_t getFreeHeap() const;
  uint32_t getFreeSketchSpace() const;
  String getResetInfo() const;
  void restart();
};

extern HardwareSerial Serial;
extern ESPClass ESP;

uint32_t millis();
void emulatorSetMillis(uint32_t value);
void emulatorUseRealtime(bool enabled);
void delay(uint32_t milliseconds);
void yield();
int analogRead(uint8_t pin);
void analogWrite(uint8_t pin, int value);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);

#include "Print.h"
