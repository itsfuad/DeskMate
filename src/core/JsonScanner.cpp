#include "JsonScanner.h"
#include <ctype.h>

JsonScanner::JsonScanner(Stream& stream, NetClient& client, int contentLength,
                         uint32_t timeoutMs)
    : stream_(stream), client_(client), remaining_(contentLength),
      timeoutMs_(timeoutMs) {}

int JsonScanner::read() {
  if (pushed_ >= 0) {
    const int value = pushed_;
    pushed_ = -1;
    return value;
  }
  const uint32_t started = millis();
  for (;;) {
    if (stream_.available()) {
      const int value = stream_.read();
      if (value >= 0 && remaining_ > 0) --remaining_;
      return value;
    }
    if (remaining_ == 0) return -1;
    if (!client_.connected() && !stream_.available()) return -1;
    if (millis() - started >= timeoutMs_) {
      timedOut_ = true;
      return -1;
    }
    delay(1);
    yield();
  }
}

void JsonScanner::unread(int value) { pushed_ = value; }

int JsonScanner::nextNonSpace() {
  int value;
  do {
    value = read();
  } while (value >= 0 && isspace(value));
  return value;
}

bool JsonScanner::readString(char* output, uint8_t outputSize) {
  uint8_t length = 0;
  if (outputSize) output[0] = 0;
  for (;;) {
    int value = read();
    if (value < 0) return false;
    if (value == '"') return true;
    if (value == '\\') {
      value = read();
      if (value < 0) return false;
      switch (value) {
        case 'n': value = '\n'; break;
        case 'r': value = '\r'; break;
        case 't': value = '\t'; break;
        case 'b': value = '\b'; break;
        case 'f': value = '\f'; break;
        case 'u': {
          // Repository names and issue titles are displayed in a 6x8 ASCII
          // font. Consume the escape and substitute a placeholder rather than
          // carrying a UTF-8 conversion table the renderer could not draw.
          uint16_t code = 0;
          for (uint8_t i = 0; i < 4; ++i) {
            const int digit = read();
            if (digit < 0) return false;
            code <<= 4;
            if (digit >= '0' && digit <= '9') code |= digit - '0';
            else if (digit >= 'a' && digit <= 'f') code |= digit - 'a' + 10;
            else if (digit >= 'A' && digit <= 'F') code |= digit - 'A' + 10;
          }
          value = code < 0x80 ? static_cast<int>(code) : '?';
          break;
        }
        default: break;
      }
    }
    // Keep consuming past the buffer so the closing quote is still found and
    // the document stays in sync; only the retained prefix is truncated.
    if (outputSize && length + 1u < outputSize) {
      output[length++] = static_cast<char>(value);
      output[length] = 0;
    }
  }
}

bool JsonScanner::readNumber(int first, uint32_t& number, char* text,
                             uint8_t textSize) {
  uint8_t length = 0;
  bool digitsOnly = true;
  number = 0;
  int value = first;
  while (value >= 0 && value != ',' && value != '}' && value != ']' &&
         !isspace(value)) {
    if (value >= '0' && value <= '9') {
      if (number <= 429496728UL) number = number * 10UL + (value - '0');
    } else {
      digitsOnly = false;
    }
    if (textSize && length + 1u < textSize) {
      text[length++] = static_cast<char>(value);
      text[length] = 0;
    }
    value = read();
  }
  if (value >= 0) unread(value);
  if (!digitsOnly) number = 0;
  return digitsOnly && length > 0;
}

JsonScanner::Frame* JsonScanner::top() {
  return recorded_ ? &frames_[recorded_ - 1] : nullptr;
}

const JsonScanner::Frame* JsonScanner::top() const {
  return recorded_ ? &frames_[recorded_ - 1] : nullptr;
}

void JsonScanner::push(bool isArray) {
  // The key this container is opened under belongs to the parent's pending
  // slot; consume it so a sibling value cannot inherit it.
  char inherited[MaxKey] = "";
  if (Frame* parent = top()) {
    strlcpy(inherited, parent->pendingKey, MaxKey);
    parent->pendingKey[0] = 0;
  }
  ++depth_;
  if (recorded_ < MaxDepth) {
    Frame& frame = frames_[recorded_++];
    strlcpy(frame.key, inherited, MaxKey);
    frame.pendingKey[0] = 0;
    frame.index = isArray ? 0 : -1;
    frame.isArray = isArray;
  }
}

void JsonScanner::pop() {
  if (!depth_) return;
  // Frames are only recorded for the outermost MaxDepth levels, so a frame is
  // released exactly when the depth it was recorded at is left.
  if (recorded_ == depth_) --recorded_;
  --depth_;
  if (Frame* parent = top()) parent->pendingKey[0] = 0;
  if (!depth_) completed_ = true;
}

const char* JsonScanner::key() const {
  const Frame* frame = top();
  return frame ? frame->pendingKey : "";
}

const char* JsonScanner::container(uint8_t up) const {
  if (up + 1u > recorded_) return "";
  return frames_[recorded_ - 1 - up].key;
}

bool JsonScanner::under(const char* name) const {
  if (!name) return false;
  for (uint8_t i = 0; i < recorded_; ++i) {
    if (!strcmp(frames_[i].key, name)) return true;
  }
  return false;
}

int16_t JsonScanner::indexUnder(const char* name) const {
  if (!name) return -1;
  for (uint8_t i = 0; i < recorded_; ++i) {
    if (frames_[i].isArray && !strcmp(frames_[i].key, name)) {
      return frames_[i].index;
    }
  }
  return -1;
}

bool JsonScanner::walk(Handler handler, void* context) {
  char text[MaxValue];
  for (;;) {
    const int value = nextNonSpace();
    if (value < 0) break;
    switch (value) {
      case '{':
        push(false);
        break;
      case '[':
        push(true);
        break;
      case '}':
      case ']':
        pop();
        if (!depth_) return !timedOut_ && completed_;
        break;
      case ',': {
        Frame* frame = top();
        if (frame) {
          if (frame->isArray && frame->index >= 0) ++frame->index;
          frame->pendingKey[0] = 0;
        }
        break;
      }
      case ':':
        break;
      case '"': {
        Frame* frame = top();
        if (!frame) {  // a bare string document; nothing to report
          if (!readString(text, MaxValue)) return false;
          break;
        }
        // Inside an object the first string after a separator names the next
        // value; inside an array every string is itself a value.
        const bool isKey = !frame->isArray && frame->pendingKey[0] == 0;
        if (isKey) {
          if (!readString(frame->pendingKey, MaxKey)) return false;
          break;
        }
        if (!readString(text, MaxValue)) return false;
        if (handler) handler(context, *this, Value::String, text, 0);
        frame->pendingKey[0] = 0;
        break;
      }
      default: {
        Frame* frame = top();
        uint32_t number = 0;
        const bool isNumber = readNumber(value, number, text, MaxValue);
        if (handler) {
          handler(context, *this, isNumber ? Value::Number : Value::Other,
                  text, number);
        }
        if (frame) frame->pendingKey[0] = 0;
        break;
      }
    }
  }
  return !timedOut_ && completed_;
}
