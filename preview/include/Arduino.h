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

typedef uint8_t byte;
typedef bool boolean;

inline constexpr double radians(double degrees) {
  return degrees * PI / 180.0;
}

inline constexpr double degrees(double radiansValue) {
  return radiansValue * 180.0 / PI;
}

class __FlashStringHelper;

class String {
 public:
  String() = default;
  String(const char* value) : value_(value ? value : "") {}
  String(const std::string& value) : value_(value) {}
  String(char value) : value_(1, value) {}
  String(int value) : value_(std::to_string(value)) {}
  String(unsigned value) : value_(std::to_string(value)) {}
  String(long value) : value_(std::to_string(value)) {}
  String(unsigned long value) : value_(std::to_string(value)) {}
  String(float value, unsigned decimals = 2) { assignFloat(value, decimals); }
  String(double value, unsigned decimals = 2) { assignFloat(value, decimals); }

  size_t length() const { return value_.size(); }
  bool isEmpty() const { return value_.empty(); }
  const char* c_str() const { return value_.c_str(); }
  void reserve(size_t amount) { value_.reserve(amount); }
  void clear() { value_.clear(); }

  char operator[](size_t index) const { return value_[index]; }
  char& operator[](size_t index) { return value_[index]; }

  String& operator=(const char* value) {
    value_ = value ? value : "";
    return *this;
  }

  String& operator+=(const String& other) {
    value_ += other.value_;
    return *this;
  }

  String& operator+=(const char* other) {
    value_ += other ? other : "";
    return *this;
  }

  String& operator+=(char other) {
    value_ += other;
    return *this;
  }

  explicit operator bool() const { return !value_.empty(); }
  operator std::string() const { return value_; }

  friend String operator+(const String& lhs, const String& rhs) {
    return String(lhs.value_ + rhs.value_);
  }

  friend String operator+(const String& lhs, const char* rhs) {
    return String(lhs.value_ + (rhs ? rhs : ""));
  }

  friend String operator+(const char* lhs, const String& rhs) {
    return String((lhs ? lhs : "") + rhs.value_);
  }

 private:
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
  return static_cast<R>(a) < static_cast<R>(b) ? static_cast<R>(a)
                                               : static_cast<R>(b);
}

template <typename A, typename B>
constexpr auto max(A a, B b) -> typename std::common_type<A, B>::type {
  using R = typename std::common_type<A, B>::type;
  return static_cast<R>(a) > static_cast<R>(b) ? static_cast<R>(a)
                                               : static_cast<R>(b);
}

template <typename T, typename L, typename H>
constexpr T constrain(T value, L low, H high) {
  return value < static_cast<T>(low)
      ? static_cast<T>(low)
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

uint32_t millis();
void previewSetMillis(uint32_t value);
void delay(uint32_t milliseconds);
void yield();
