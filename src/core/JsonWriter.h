// JsonWriter.h — allocation-free JSON serialization over Arduino Print.
#pragma once
#include <Arduino.h>

class JsonWriter {
 public:
  static constexpr uint8_t MaxDepth = 12;

  enum class Error : uint8_t {
    None,
    InvalidState,
    DepthLimit,
    InvalidNumber,
    WriteFailure
  };

  explicit JsonWriter(Print& output) : output_(output) {}

  bool beginObject();
  bool endObject();
  bool beginArray();
  bool endArray();
  bool key(const char* name);

  bool value(const char* text);
  bool value(const String& text) { return value(text.c_str()); }
  bool value(bool value);
  bool value(int value);
  bool value(unsigned int value);
  bool value(long value);
  bool value(unsigned long value);
  bool value(long long value);
  bool value(unsigned long long value);
  bool value(double value);
  bool nullValue();

  bool complete() const { return error_ == Error::None && rootWritten_ && depth_ == 0; }
  Error error() const { return error_; }

 private:
  struct Frame {
    uint16_t count;
    bool object;
    bool waitingForValue;
  };

  bool beginContainer(bool object);
  bool endContainer(bool object);
  bool beforeValue();
  bool writeByte(char value);
  bool writeText(const char* text);
  bool writeEscaped(const char* text);
  bool writeUnsigned(unsigned long long value);
  bool fail(Error error);

  Print& output_;
  Frame frames_[MaxDepth];
  uint8_t depth_ = 0;
  bool rootWritten_ = false;
  Error error_ = Error::None;
};
