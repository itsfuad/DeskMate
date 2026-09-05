// JsonScanner.h — strict, fixed-memory streaming JSON parser.
#pragma once
#include <Arduino.h>
#include "Platform.h"

class JsonScanner {
 public:
  static constexpr uint8_t MaxDepth = 12;
  static constexpr size_t MaxKey = 24;
  static constexpr size_t MaxValue = 96;
  static constexpr size_t DefaultMaxBytes = 64 * 1024;
  static constexpr int UnknownLength = -1;

  enum class Value : uint8_t {
    String,
    Number,
    Boolean,
    Null,
    Other = Null  // Source compatibility with callers of the old API.
  };

  enum class Container : uint8_t { ObjectStart, ObjectEnd, ArrayStart, ArrayEnd };

  enum class Error : uint8_t {
    None,
    UnexpectedEnd,
    Timeout,
    SizeLimit,
    DepthLimit,
    KeyTooLong,
    ValueTooLong,
    OutOfMemory,
    InvalidSyntax,
    InvalidEscape,
    InvalidNumber,
    TrailingData
  };

  // `number` is exact when type is Number and numberIsUnsigned() is true.
  using Handler = void (*)(void* context, const JsonScanner& scanner,
                           Value type, const char* text, uint32_t number);
  using ContainerHandler = void (*)(void* context, const JsonScanner& scanner,
                                    Container event);

  // Existing network constructor. A non-negative content length is exact.
  JsonScanner(Stream& stream, NetClient& client, int contentLength,
              uint32_t timeoutMs);
  JsonScanner(Stream& stream, NetClient& client, int contentLength,
              uint32_t timeoutMs, size_t maxBytes);

  // Stream-only input. UnknownLength consumes currently readable bytes to EOF.
  explicit JsonScanner(Stream& stream, int contentLength = UnknownLength,
                       size_t maxBytes = DefaultMaxBytes);

  // In-memory input. The complete buffer must contain exactly one JSON value.
  JsonScanner(const char* data, size_t length);

  // One reusable scanner for serialized low-stack firmware paths. The next call
  // resets it, so a shared scan must complete before another begins.
  static JsonScanner& shared(Stream& stream, NetClient& client,
                             int contentLength, uint32_t timeoutMs);
  static JsonScanner& shared(Stream& stream, int contentLength,
                             size_t maxBytes = DefaultMaxBytes);
  static JsonScanner& shared(const char* data, size_t length);

  // The supplied storage is used for decoded scalar text. It must remain valid
  // through walk(); strings up to 512 bytes fit in the default buffer.
  void setValueBuffer(char* buffer, size_t size);
  void setContainerHandler(ContainerHandler handler, void* context = nullptr);

  bool walk(Handler handler, void* context);

  Error error() const { return error_; }
  bool timedOut() const { return error_ == Error::Timeout; }
  size_t bytesRead() const { return bytesRead_; }
  bool numberIsUnsigned() const { return numberIsUnsigned_; }
  bool valueTruncated() const { return valueTruncated_; }
  size_t valueLength() const { return valueLength_; }

  // Path accessors are valid only during scalar or container callbacks.
  const char* key() const;
  const char* container(uint8_t up = 0) const;
  bool under(const char* name) const;
  int16_t indexUnder(const char* name) const;

 private:
  struct Frame {
    char key[MaxKey];
    char pendingKey[MaxKey];
    int16_t index;
    bool isArray;
  };

  enum class Source : uint8_t { Network, Stream, Memory };

  int read();
  void unread(int value);
  int nextNonSpace();
  bool parseValue(int first);
  bool parseObject();
  bool parseArray();
  bool parseString(char* output, size_t outputSize, Error overflowError);
  bool parseNumber(int first);
  bool parseLiteral(int first, const char* suffix, Value type);
  bool finishDocument();
  bool push(bool isArray);
  void pop();
  bool emitByte(char*& output, size_t& length, size_t outputSize,
                uint8_t value, Error overflowError);
  bool fail(Error error);
  Frame* top();
  const Frame* top() const;

  Source source_;
  Stream* stream_ = nullptr;
  NetClient* client_ = nullptr;
  const uint8_t* memory_ = nullptr;
  size_t memoryLength_ = 0;
  size_t memoryPosition_ = 0;
  int remaining_ = UnknownLength;
  uint32_t timeoutMs_ = 0;
  size_t maxBytes_ = DefaultMaxBytes;
  size_t bytesRead_ = 0;
  int pushed_ = -1;

  char defaultValue_[MaxValue];
  char* valueBuffer_ = defaultValue_;
  size_t valueBufferSize_ = sizeof(defaultValue_);
  Handler handler_ = nullptr;
  void* handlerContext_ = nullptr;
  ContainerHandler containerHandler_ = nullptr;
  void* containerContext_ = nullptr;
  Error error_ = Error::None;
  bool numberIsUnsigned_ = false;
  bool valueTruncated_ = false;
  size_t valueLength_ = 0;
  bool walked_ = false;

  Frame frames_[MaxDepth];
  uint8_t depth_ = 0;
};
