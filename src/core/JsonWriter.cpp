#include "JsonWriter.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

bool JsonWriter::fail(Error error) {
  if (error_ == Error::None) error_ = error;
  return false;
}

bool JsonWriter::writeByte(char value) {
  if (error_ != Error::None) return false;
  if (output_.write(static_cast<uint8_t>(value)) != 1)
    return fail(Error::WriteFailure);
  return true;
}

bool JsonWriter::writeText(const char* text) {
  if (error_ != Error::None) return false;
  const size_t length = strlen(text);
  if (output_.write(reinterpret_cast<const uint8_t*>(text), length) != length)
    return fail(Error::WriteFailure);
  return true;
}

bool JsonWriter::writeEscaped(const char* text) {
  if (!writeByte('"')) return false;
  static const char hex[] = "0123456789abcdef";
  for (const uint8_t* cursor = reinterpret_cast<const uint8_t*>(text); *cursor;
       ++cursor) {
    const uint8_t value = *cursor;
    switch (value) {
      case '"': if (!writeText("\\\"")) return false; break;
      case '\\': if (!writeText("\\\\")) return false; break;
      case '\b': if (!writeText("\\b")) return false; break;
      case '\f': if (!writeText("\\f")) return false; break;
      case '\n': if (!writeText("\\n")) return false; break;
      case '\r': if (!writeText("\\r")) return false; break;
      case '\t': if (!writeText("\\t")) return false; break;
      default:
        if (value < 0x20) {
          char escape[] = {'\\', 'u', '0', '0', hex[value >> 4],
                           hex[value & 0x0f], 0};
          if (!writeText(escape)) return false;
        } else if (!writeByte(static_cast<char>(value))) {
          return false;
        }
    }
  }
  return writeByte('"');
}

bool JsonWriter::beforeValue() {
  if (error_ != Error::None) return false;
  if (!depth_) {
    if (rootWritten_) return fail(Error::InvalidState);
    rootWritten_ = true;
    return true;
  }

  Frame& frame = frames_[depth_ - 1];
  if (frame.object) {
    if (!frame.waitingForValue) return fail(Error::InvalidState);
    frame.waitingForValue = false;
    ++frame.count;
    return true;
  }
  if (frame.count && !writeByte(',')) return false;
  ++frame.count;
  return true;
}

bool JsonWriter::beginContainer(bool object) {
  if (depth_ >= MaxDepth) return fail(Error::DepthLimit);
  if (!beforeValue()) return false;
  if (!writeByte(object ? '{' : '[')) return false;
  frames_[depth_++] = Frame{0, object, false};
  return true;
}

bool JsonWriter::endContainer(bool object) {
  if (error_ != Error::None) return false;
  if (!depth_) return fail(Error::InvalidState);
  const Frame& frame = frames_[depth_ - 1];
  if (frame.object != object || frame.waitingForValue)
    return fail(Error::InvalidState);
  if (!writeByte(object ? '}' : ']')) return false;
  --depth_;
  return true;
}

bool JsonWriter::beginObject() { return beginContainer(true); }
bool JsonWriter::endObject() { return endContainer(true); }
bool JsonWriter::beginArray() { return beginContainer(false); }
bool JsonWriter::endArray() { return endContainer(false); }

bool JsonWriter::key(const char* name) {
  if (error_ != Error::None) return false;
  if (!name || !depth_) return fail(Error::InvalidState);
  Frame& frame = frames_[depth_ - 1];
  if (!frame.object || frame.waitingForValue) return fail(Error::InvalidState);
  if (frame.count && !writeByte(',')) return false;
  if (!writeEscaped(name) || !writeByte(':')) return false;
  frame.waitingForValue = true;
  return true;
}

bool JsonWriter::value(const char* text) {
  if (!text) return nullValue();
  return beforeValue() && writeEscaped(text);
}

bool JsonWriter::value(bool value) {
  return beforeValue() && writeText(value ? "true" : "false");
}

bool JsonWriter::writeUnsigned(unsigned long long value) {
  char buffer[21];
  char* cursor = buffer + sizeof(buffer);
  *--cursor = 0;
  do {
    *--cursor = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value);
  return writeText(cursor);
}

bool JsonWriter::value(int value) {
  return this->value(static_cast<long long>(value));
}
bool JsonWriter::value(unsigned int value) {
  return this->value(static_cast<unsigned long long>(value));
}
bool JsonWriter::value(long value) {
  return this->value(static_cast<long long>(value));
}
bool JsonWriter::value(unsigned long value) {
  return this->value(static_cast<unsigned long long>(value));
}

bool JsonWriter::value(long long value) {
  if (!beforeValue()) return false;
  if (value < 0) {
    if (!writeByte('-')) return false;
    // Avoid negating LLONG_MIN in signed arithmetic.
    const unsigned long long magnitude =
        static_cast<unsigned long long>(-(value + 1)) + 1;
    return writeUnsigned(magnitude);
  }
  return writeUnsigned(static_cast<unsigned long long>(value));
}

bool JsonWriter::value(unsigned long long value) {
  return beforeValue() && writeUnsigned(value);
}

bool JsonWriter::value(double value) {
  if (!isfinite(value)) return fail(Error::InvalidNumber);
  char buffer[32];
  const int length = snprintf(buffer, sizeof(buffer), "%.17g", value);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(buffer))
    return fail(Error::InvalidNumber);
  return beforeValue() && writeText(buffer);
}

bool JsonWriter::nullValue() { return beforeValue() && writeText("null"); }
