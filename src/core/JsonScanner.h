// JsonScanner.h — streaming, allocation-free JSON structure walker.
//
// Feature clients on this hardware cannot buffer an API response before
// parsing it, so values are handled as they arrive off the socket. Unlike a
// flat "scan for a quoted key" reader, this scanner tracks container nesting,
// so a handler can tell `title` inside one list from `title` inside another and
// can read an `errors[].message` without mistaking a `message` field in the
// payload for a failure.
//
// Nothing here is GitHub-specific: the scanner reports structure, and the
// calling feature owns its own schema matching.
#pragma once
#include <Arduino.h>
#include "Platform.h"

class JsonScanner {
 public:
  // Deep enough for the responses this firmware requests, with headroom.
  // Structure below this depth is still traversed correctly; it is simply
  // reported as belonging to the deepest recorded container.
  static constexpr uint8_t MaxDepth = 12;
  // Keys are bounded because every query this firmware sends controls its own
  // field names — alias any longer GraphQL field to something shorter.
  static constexpr uint8_t MaxKey = 14;
  static constexpr uint8_t MaxValue = 56;

  enum class Value : uint8_t { String, Number, Other };

  // Called for every scalar in the document. `text` is always NUL-terminated
  // and truncated to MaxValue - 1 characters; `number` is meaningful only for
  // Value::Number. Use the path accessors to decide what the value is.
  using Handler = void (*)(void* context, const JsonScanner& scanner,
                           Value type, const char* text, uint32_t number);

  JsonScanner(Stream& stream, NetClient& client, int contentLength,
              uint32_t timeoutMs);

  // Returns false when the stream ended early, timed out, or was malformed.
  bool walk(Handler handler, void* context);

  bool timedOut() const { return timedOut_; }

  // ---- path accessors, valid only inside a Handler call -------------------

  // The key the current value was stored under. Empty inside an array.
  const char* key() const;

  // The key of the object holding the current value's container. `up` walks
  // outward: container(0) is the immediately enclosing container's own key.
  const char* container(uint8_t up = 0) const;

  // True when any enclosing container was opened under `name`.
  bool under(const char* name) const;

  // Element index within the outermost enclosing array named `name`, or -1.
  // Taking the outermost match keeps a row index stable when a row object
  // itself contains a nested array of the same name.
  int16_t indexUnder(const char* name) const;

 private:
  struct Frame {
    char key[MaxKey];         // key this container was opened under
    char pendingKey[MaxKey];  // key awaiting a value inside this container
    int16_t index;            // element index, arrays only
    bool isArray;
  };

  int read();
  void unread(int value);
  int nextNonSpace();
  bool readString(char* output, uint8_t outputSize);
  bool readNumber(int first, uint32_t& number, char* text, uint8_t textSize);
  void push(bool isArray);
  void pop();
  Frame* top();
  const Frame* top() const;

  Stream& stream_;
  NetClient& client_;
  int remaining_;
  uint32_t timeoutMs_;
  int pushed_ = -1;
  bool timedOut_ = false;

  Frame frames_[MaxDepth];
  uint8_t recorded_ = 0;   // frames actually stored
  uint16_t depth_ = 0;     // true nesting depth, may exceed MaxDepth
  bool completed_ = false; // the root container opened and closed cleanly
};
