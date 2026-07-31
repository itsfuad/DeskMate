#pragma once

#include "Arduino.h"

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t value) = 0;

  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t written = 0;
    while (written < size) written += write(buffer[written]);
    return written;
  }

  size_t write(const char* text) {
    if (!text) return 0;
    return write(reinterpret_cast<const uint8_t*>(text), std::strlen(text));
  }

  size_t print(const String& value) { return write(value.c_str()); }
  size_t print(const char* value) { return write(value ? value : ""); }
  size_t print(char value) { return write(static_cast<uint8_t>(value)); }
  size_t print(unsigned char value, int = 10) { return printNumber(value); }
  size_t print(int value, int = 10) { return printNumber(value); }
  size_t print(unsigned int value, int = 10) { return printNumber(value); }
  size_t print(long value, int = 10) { return printNumber(value); }
  size_t print(unsigned long value, int = 10) { return printNumber(value); }
  size_t print(long long value, int = 10) { return printNumber(value); }
  size_t print(unsigned long long value, int = 10) { return printNumber(value); }

  size_t print(double value, int digits = 2) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    return write(buffer);
  }

  size_t println() { return write("\r\n"); }

  template <typename T>
  size_t println(const T& value) {
    return print(value) + println();
  }

 protected:
  template <typename T>
  size_t printNumber(T value) {
    char buffer[48];
    if constexpr (std::is_signed_v<T>) {
      std::snprintf(buffer, sizeof(buffer), "%lld",
                    static_cast<long long>(value));
    } else {
      std::snprintf(buffer, sizeof(buffer), "%llu",
                    static_cast<unsigned long long>(value));
    }
    return write(buffer);
  }
};
