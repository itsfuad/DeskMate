#include "JsonScanner.h"

#include <new>
#include <string.h>

alignas(JsonScanner) static uint8_t g_sharedScannerStorage[sizeof(JsonScanner)];

JsonScanner::JsonScanner(Stream& stream, NetClient& client, int contentLength,
                         uint32_t timeoutMs)
    : JsonScanner(stream, client, contentLength, timeoutMs,
                  contentLength >= 0 ? static_cast<size_t>(contentLength)
                                     : DefaultMaxBytes) {}

JsonScanner::JsonScanner(Stream& stream, NetClient& client, int contentLength,
                         uint32_t timeoutMs, size_t maxBytes)
    : source_(Source::Network), stream_(&stream), client_(&client),
      remaining_(contentLength), timeoutMs_(timeoutMs), maxBytes_(maxBytes) {}

JsonScanner::JsonScanner(Stream& stream, int contentLength, size_t maxBytes)
    : source_(Source::Stream), stream_(&stream), remaining_(contentLength),
      maxBytes_(maxBytes) {}

JsonScanner::JsonScanner(const char* data, size_t length)
    : source_(Source::Memory),
      memory_(reinterpret_cast<const uint8_t*>(data)), memoryLength_(length),
      remaining_(length > static_cast<size_t>(0x7fffffff) ? UnknownLength
                                                          : static_cast<int>(length)),
      maxBytes_(length) {}

JsonScanner& JsonScanner::shared(Stream& stream, NetClient& client,
                                 int contentLength, uint32_t timeoutMs) {
  return *new (g_sharedScannerStorage)
      JsonScanner(stream, client, contentLength, timeoutMs);
}

JsonScanner& JsonScanner::shared(Stream& stream, int contentLength,
                                 size_t maxBytes) {
  return *new (g_sharedScannerStorage)
      JsonScanner(stream, contentLength, maxBytes);
}

JsonScanner& JsonScanner::shared(const char* data, size_t length) {
  return *new (g_sharedScannerStorage) JsonScanner(data, length);
}

void JsonScanner::setValueBuffer(char* buffer, size_t size) {
  if (walked_) return;
  valueBuffer_ = buffer;
  valueBufferSize_ = size;
}

void JsonScanner::setContainerHandler(ContainerHandler handler, void* context) {
  containerHandler_ = handler;
  containerContext_ = context;
}

bool JsonScanner::fail(Error error) {
  if (error_ == Error::None) error_ = error;
  return false;
}

int JsonScanner::read() {
  if (pushed_ >= 0) {
    const int value = pushed_;
    pushed_ = -1;
    return value;
  }
  if (remaining_ == 0) return -1;

  if (source_ == Source::Memory) {
    if (memoryPosition_ >= memoryLength_) return -1;
    if (bytesRead_ >= maxBytes_) {
      fail(Error::SizeLimit);
      return -1;
    }
    ++bytesRead_;
    if (remaining_ > 0) --remaining_;
    return memory_[memoryPosition_++];
  }

  if (source_ == Source::Stream) {
    if (!stream_->available()) return -1;
    if (bytesRead_ >= maxBytes_) {
      fail(Error::SizeLimit);
      return -1;
    }
    const int value = stream_->read();
    if (value < 0) return -1;
    ++bytesRead_;
    if (remaining_ > 0) --remaining_;
    return value;
  }

  const uint32_t started = millis();
  for (;;) {
    if (stream_->available()) {
      if (bytesRead_ >= maxBytes_) {
        fail(Error::SizeLimit);
        return -1;
      }
      const int value = stream_->read();
      if (value < 0) continue;
      ++bytesRead_;
      if (remaining_ > 0) --remaining_;
      return value;
    }
    if (remaining_ == 0) return -1;
    if (!client_->connected() && !stream_->available()) return -1;
    if (millis() - started >= timeoutMs_) {
      fail(Error::Timeout);
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
  } while (value >= 0 && (value == ' ' || value == '\t' || value == '\n' ||
                          value == '\r'));
  return value;
}

bool JsonScanner::emitByte(char*& output, size_t& length, size_t outputSize,
                           uint8_t value, Error overflowError) {
  if (!output || outputSize == 0) return fail(overflowError);
  if (length + 1 >= outputSize) {
    if (overflowError != Error::ValueTooLong) return fail(overflowError);
    valueTruncated_ = true;
    ++length;
    return true;
  }
  output[length++] = static_cast<char>(value);
  output[length] = 0;
  return true;
}

bool JsonScanner::parseString(char* output, size_t outputSize,
                              Error overflowError) {
  size_t length = 0;
  if (!output || outputSize == 0) return fail(overflowError);
  output[0] = 0;

  for (;;) {
    int value = read();
    if (value < 0) {
      return error_ == Error::None ? fail(Error::UnexpectedEnd) : false;
    }
    if (value == '"') {
      valueLength_ = length;
      return true;
    }
    if (value >= 0 && value < 0x20) return fail(Error::InvalidSyntax);
    if (value != '\\') {
      if (!emitByte(output, length, outputSize, static_cast<uint8_t>(value),
                    overflowError)) return false;
      continue;
    }

    value = read();
    if (value < 0) {
      return error_ == Error::None ? fail(Error::UnexpectedEnd) : false;
    }
    switch (value) {
      case '"': value = '"'; break;
      case '\\': value = '\\'; break;
      case '/': value = '/'; break;
      case 'b': value = '\b'; break;
      case 'f': value = '\f'; break;
      case 'n': value = '\n'; break;
      case 'r': value = '\r'; break;
      case 't': value = '\t'; break;
      case 'u': {
        uint16_t code = 0;
        for (uint8_t i = 0; i < 4; ++i) {
          const int digit = read();
          if (digit < 0) {
            return error_ == Error::None ? fail(Error::UnexpectedEnd) : false;
          }
          code <<= 4;
          if (digit >= '0' && digit <= '9') code |= digit - '0';
          else if (digit >= 'a' && digit <= 'f') code |= digit - 'a' + 10;
          else if (digit >= 'A' && digit <= 'F') code |= digit - 'A' + 10;
          else return fail(Error::InvalidEscape);
        }

        uint32_t scalar = code;
        if (code >= 0xd800 && code <= 0xdbff) {
          if (read() != '\\' || read() != 'u') return fail(Error::InvalidEscape);
          uint16_t low = 0;
          for (uint8_t i = 0; i < 4; ++i) {
            const int digit = read();
            if (digit < 0) {
              return error_ == Error::None ? fail(Error::UnexpectedEnd) : false;
            }
            low <<= 4;
            if (digit >= '0' && digit <= '9') low |= digit - '0';
            else if (digit >= 'a' && digit <= 'f') low |= digit - 'a' + 10;
            else if (digit >= 'A' && digit <= 'F') low |= digit - 'A' + 10;
            else return fail(Error::InvalidEscape);
          }
          if (low < 0xdc00 || low > 0xdfff) return fail(Error::InvalidEscape);
          scalar = 0x10000UL + ((code - 0xd800UL) << 10) + (low - 0xdc00UL);
        } else if (code >= 0xdc00 && code <= 0xdfff) {
          return fail(Error::InvalidEscape);
        }

        uint8_t encoded[4];
        uint8_t count;
        if (scalar < 0x80) {
          encoded[0] = static_cast<uint8_t>(scalar);
          count = 1;
        } else if (scalar < 0x800) {
          encoded[0] = 0xc0 | static_cast<uint8_t>(scalar >> 6);
          encoded[1] = 0x80 | static_cast<uint8_t>(scalar & 0x3f);
          count = 2;
        } else if (scalar < 0x10000) {
          encoded[0] = 0xe0 | static_cast<uint8_t>(scalar >> 12);
          encoded[1] = 0x80 | static_cast<uint8_t>((scalar >> 6) & 0x3f);
          encoded[2] = 0x80 | static_cast<uint8_t>(scalar & 0x3f);
          count = 3;
        } else {
          encoded[0] = 0xf0 | static_cast<uint8_t>(scalar >> 18);
          encoded[1] = 0x80 | static_cast<uint8_t>((scalar >> 12) & 0x3f);
          encoded[2] = 0x80 | static_cast<uint8_t>((scalar >> 6) & 0x3f);
          encoded[3] = 0x80 | static_cast<uint8_t>(scalar & 0x3f);
          count = 4;
        }
        for (uint8_t i = 0; i < count; ++i) {
          if (!emitByte(output, length, outputSize, encoded[i], overflowError))
            return false;
        }
        continue;
      }
      default: return fail(Error::InvalidEscape);
    }
    if (!emitByte(output, length, outputSize, static_cast<uint8_t>(value),
                  overflowError)) return false;
  }
}

bool JsonScanner::parseNumber(int first) {
  size_t length = 0;
  char* output = valueBuffer_;
  const size_t size = valueBufferSize_;
  uint32_t number = 0;
  bool unsignedInteger = true;
  bool overflow = false;
  int value = first;

  auto append = [&](int character) {
    return emitByte(output, length, size, static_cast<uint8_t>(character),
                    Error::ValueTooLong);
  };
  auto digit = [](int character) { return character >= '0' && character <= '9'; };

  if (value == '-') {
    unsignedInteger = false;
    if (!append(value)) return false;
    value = read();
    if (value < 0) return fail(Error::InvalidNumber);
  }

  if (value == '0') {
    if (!append(value)) return false;
    value = read();
    if (digit(value)) return fail(Error::InvalidNumber);
  } else if (value >= '1' && value <= '9') {
    do {
      if (!append(value)) return false;
      if (!overflow) {
        const uint8_t d = static_cast<uint8_t>(value - '0');
        if (number > (0xffffffffUL - d) / 10UL) overflow = true;
        else number = number * 10UL + d;
      }
      value = read();
    } while (digit(value));
  } else {
    return fail(Error::InvalidNumber);
  }

  if (value == '.') {
    unsignedInteger = false;
    if (!append(value)) return false;
    value = read();
    if (!digit(value)) return fail(Error::InvalidNumber);
    do {
      if (!append(value)) return false;
      value = read();
    } while (digit(value));
  }

  if (value == 'e' || value == 'E') {
    unsignedInteger = false;
    if (!append(value)) return false;
    value = read();
    if (value == '+' || value == '-') {
      if (!append(value)) return false;
      value = read();
    }
    if (!digit(value)) return fail(Error::InvalidNumber);
    do {
      if (!append(value)) return false;
      value = read();
    } while (digit(value));
  }

  if (value >= 0) {
    const bool delimiter = value == ',' || value == ']' || value == '}' ||
                           value == ' ' || value == '\t' || value == '\n' ||
                           value == '\r';
    if (!delimiter) return fail(Error::InvalidNumber);
    unread(value);
  }

  valueLength_ = length;
  if (valueTruncated_) return fail(Error::ValueTooLong);
  numberIsUnsigned_ = unsignedInteger && !overflow;
  if (handler_) handler_(handlerContext_, *this, Value::Number, valueBuffer_,
                         numberIsUnsigned_ ? number : 0);
  if (Frame* frame = top()) frame->pendingKey[0] = 0;
  return true;
}

bool JsonScanner::parseLiteral(int first, const char* suffix, Value type) {
  for (const char* cursor = suffix; *cursor; ++cursor) {
    if (read() != *cursor) return fail(Error::InvalidSyntax);
  }
  const char* text = type == Value::Boolean ? (first == 't' ? "true" : "false")
                                             : "null";
  if (strlen(text) + 1 > valueBufferSize_) return fail(Error::ValueTooLong);
  strlcpy(valueBuffer_, text, valueBufferSize_);
  numberIsUnsigned_ = false;
  if (handler_) handler_(handlerContext_, *this, type, valueBuffer_, 0);
  if (Frame* frame = top()) frame->pendingKey[0] = 0;
  return true;
}

JsonScanner::Frame* JsonScanner::top() {
  return depth_ ? &frames_[depth_ - 1] : nullptr;
}

const JsonScanner::Frame* JsonScanner::top() const {
  return depth_ ? &frames_[depth_ - 1] : nullptr;
}

bool JsonScanner::push(bool isArray) {
  if (depth_ >= MaxDepth) return fail(Error::DepthLimit);
  char inherited[MaxKey] = "";
  if (Frame* parent = top()) {
    strlcpy(inherited, parent->pendingKey, sizeof(inherited));
    parent->pendingKey[0] = 0;
  }
  Frame& frame = frames_[depth_++];
  strlcpy(frame.key, inherited, sizeof(frame.key));
  frame.pendingKey[0] = 0;
  frame.index = -1;
  frame.isArray = isArray;
  return true;
}

void JsonScanner::pop() {
  if (depth_) --depth_;
  if (Frame* parent = top()) parent->pendingKey[0] = 0;
}

bool JsonScanner::parseObject() {
  if (!push(false)) return false;
  if (containerHandler_)
    containerHandler_(containerContext_, *this, Container::ObjectStart);

  int value = nextNonSpace();
  if (value < 0)
    return error_ == Error::None ? fail(Error::UnexpectedEnd) : false;
  if (value == '}') {
    if (containerHandler_)
      containerHandler_(containerContext_, *this, Container::ObjectEnd);
    pop();
    return true;
  }

  for (;;) {
    if (value != '"') return fail(Error::InvalidSyntax);
    Frame* frame = top();
    if (!parseString(frame->pendingKey, sizeof(frame->pendingKey),
                     Error::KeyTooLong)) return false;
    if (nextNonSpace() != ':') return fail(Error::InvalidSyntax);
    value = nextNonSpace();
    if (value < 0)
      return error_ == Error::None ? fail(Error::UnexpectedEnd) : false;
    if (!parseValue(value)) return false;

    value = nextNonSpace();
    if (value < 0)
      return error_ == Error::None ? fail(Error::UnexpectedEnd) : false;
    if (value == '}') {
      if (containerHandler_)
        containerHandler_(containerContext_, *this, Container::ObjectEnd);
      pop();
      return true;
    }
    if (value != ',') return fail(Error::InvalidSyntax);
    value = nextNonSpace();
    if (value < 0)
      return error_ == Error::None ? fail(Error::UnexpectedEnd) : false;
  }
}

bool JsonScanner::parseArray() {
  if (!push(true)) return false;
  if (containerHandler_)
    containerHandler_(containerContext_, *this, Container::ArrayStart);

  int value = nextNonSpace();
  if (value < 0)
    return error_ == Error::None ? fail(Error::UnexpectedEnd) : false;
  if (value == ']') {
    if (containerHandler_)
      containerHandler_(containerContext_, *this, Container::ArrayEnd);
    pop();
    return true;
  }

  for (;;) {
    if (top()->index < 0x7fff) ++top()->index;
    if (!parseValue(value)) return false;
    value = nextNonSpace();
    if (value < 0)
      return error_ == Error::None ? fail(Error::UnexpectedEnd) : false;
    if (value == ']') {
      if (containerHandler_)
        containerHandler_(containerContext_, *this, Container::ArrayEnd);
      pop();
      return true;
    }
    if (value != ',') return fail(Error::InvalidSyntax);
    value = nextNonSpace();
    if (value < 0)
      return error_ == Error::None ? fail(Error::UnexpectedEnd) : false;
  }
}

bool JsonScanner::parseValue(int first) {
  numberIsUnsigned_ = false;
  valueTruncated_ = false;
  valueLength_ = 0;
  switch (first) {
    case '{': return parseObject();
    case '[': return parseArray();
    case '"':
      if (!parseString(valueBuffer_, valueBufferSize_, Error::ValueTooLong))
        return false;
      if (handler_)
        handler_(handlerContext_, *this, Value::String, valueBuffer_, 0);
      if (Frame* frame = top()) frame->pendingKey[0] = 0;
      return true;
    case 't': return parseLiteral(first, "rue", Value::Boolean);
    case 'f': return parseLiteral(first, "alse", Value::Boolean);
    case 'n': return parseLiteral(first, "ull", Value::Null);
    default:
      if (first == '-' || (first >= '0' && first <= '9')) return parseNumber(first);
      return fail(Error::InvalidSyntax);
  }
}

bool JsonScanner::finishDocument() {
  const int value = nextNonSpace();
  if (value >= 0) return fail(Error::TrailingData);
  if (error_ != Error::None) return false;
  if (remaining_ > 0) return fail(Error::UnexpectedEnd);
  return true;
}

bool JsonScanner::walk(Handler handler, void* context) {
  if (walked_) return fail(Error::InvalidSyntax);
  walked_ = true;
  handler_ = handler;
  handlerContext_ = context;
  if (!valueBuffer_ || valueBufferSize_ == 0) return fail(Error::ValueTooLong);
  if (remaining_ >= 0 && static_cast<size_t>(remaining_) > maxBytes_)
    return fail(Error::SizeLimit);
  if (source_ == Source::Memory && !memory_ && memoryLength_)
    return fail(Error::UnexpectedEnd);

  const int first = nextNonSpace();
  if (first < 0)
    return error_ == Error::None ? fail(Error::UnexpectedEnd) : false;
  return parseValue(first) && finishDocument();
}

const char* JsonScanner::key() const {
  const Frame* frame = top();
  return frame ? frame->pendingKey : "";
}

const char* JsonScanner::container(uint8_t up) const {
  if (up + 1u > depth_) return "";
  return frames_[depth_ - 1 - up].key;
}

bool JsonScanner::under(const char* name) const {
  if (!name) return false;
  for (uint8_t i = 0; i < depth_; ++i) {
    if (!strcmp(frames_[i].key, name)) return true;
  }
  return false;
}

int16_t JsonScanner::indexUnder(const char* name) const {
  if (!name) return -1;
  for (uint8_t i = 0; i < depth_; ++i) {
    if (frames_[i].isArray && !strcmp(frames_[i].key, name))
      return frames_[i].index;
  }
  return -1;
}
