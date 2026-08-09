#pragma once

#include "Print.h"

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() {}
  virtual void setTimeout(unsigned long timeout) { timeoutMs_ = timeout; }

  size_t readBytes(char* buffer, size_t length) {
    size_t readCount = 0;
    const uint32_t started = millis();
    while (readCount < length && millis() - started < timeoutMs_) {
      const int value = read();
      if (value >= 0) buffer[readCount++] = static_cast<char>(value);
      else delay(1);
    }
    return readCount;
  }

 protected:
  unsigned long timeoutMs_ = 1000;
};
