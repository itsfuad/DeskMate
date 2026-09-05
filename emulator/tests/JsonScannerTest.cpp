#include "EmulatorPlatform.h"
#include "JsonScanner.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

class StringStream : public Stream {
 public:
  explicit StringStream(std::string text) : text_(std::move(text)) {}
  int available() override {
    return static_cast<int>(text_.size() - position_);
  }
  int read() override {
    if (position_ >= text_.size()) return -1;
    return static_cast<unsigned char>(text_[position_++]);
  }
  int peek() override {
    if (position_ >= text_.size()) return -1;
    return static_cast<unsigned char>(text_[position_]);
  }
  size_t write(uint8_t) override { return 0; }

 private:
  std::string text_;
  size_t position_ = 0;
};

int failures = 0;

void check(bool condition, const char* what) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", what);
  ++failures;
}

void checkText(const std::string& actual, const char* expected,
               const char* what) {
  if (actual == expected) return;
  std::fprintf(stderr, "FAIL: %s -- expected \"%s\", got \"%s\"\n", what,
               expected, actual.c_str());
  ++failures;
}

struct Scalar {
  JsonScanner::Value type;
  std::string key;
  std::string text;
  uint32_t number;
  bool isUnsigned;
  bool truncated;
  size_t fullLength;
  int16_t row;
};

struct Collected {
  std::vector<Scalar> values;
  std::vector<std::string> events;
};

void onValue(void* context, const JsonScanner& scanner,
             JsonScanner::Value type, const char* text, uint32_t number) {
  Collected& out = *static_cast<Collected*>(context);
  out.values.push_back({type, scanner.key(), text, number,
                        scanner.numberIsUnsigned(), scanner.valueTruncated(),
                        scanner.valueLength(), scanner.indexUnder("rows")});
}

void onContainer(void* context, const JsonScanner& scanner,
                 JsonScanner::Container event) {
  Collected& out = *static_cast<Collected*>(context);
  const char* name = "";
  switch (event) {
    case JsonScanner::Container::ObjectStart: name = "object+"; break;
    case JsonScanner::Container::ObjectEnd: name = "object-"; break;
    case JsonScanner::Container::ArrayStart: name = "array+"; break;
    case JsonScanner::Container::ArrayEnd: name = "array-"; break;
  }
  out.events.push_back(std::string(name) + scanner.container());
}

JsonScanner::Error memoryError(const std::string& document) {
  JsonScanner scanner(document.data(), document.size());
  scanner.walk(nullptr, nullptr);
  return scanner.error();
}

void checkError(const char* document, JsonScanner::Error expected,
                const char* what) {
  const JsonScanner::Error actual = memoryError(document);
  if (actual == expected) return;
  std::fprintf(stderr, "FAIL: %s -- expected error %u, got %u\n", what,
               static_cast<unsigned>(expected), static_cast<unsigned>(actual));
  ++failures;
}

const Scalar* find(const Collected& out, const char* key) {
  for (const Scalar& value : out.values) {
    if (value.key == key) return &value;
  }
  return nullptr;
}

void runTypedValuesAndPaths() {
  const char* document =
      R"({"rows":[{"s":"line\n\u00e9 \ud83d\ude00","u":4294967295,"big":4294967296,"neg":-2,"real":1.25e+2,"yes":true,"no":false,"nil":null}]})";
  Collected out;
  JsonScanner scanner(document, std::strlen(document));
  scanner.setContainerHandler(onContainer, &out);
  check(scanner.walk(onValue, &out), "typed memory document parses");
  check(scanner.error() == JsonScanner::Error::None, "successful parse has no error");
  check(scanner.bytesRead() == std::strlen(document), "memory byte count is exact");

  const Scalar* string = find(out, "s");
  check(string && string->type == JsonScanner::Value::String,
        "string has String type");
  if (string) {
    checkText(string->text, "line\né \xf0\x9f\x98\x80", "unicode escapes decode to UTF-8");
    check(string->row == 0, "array path index is available in handler");
  }
  const Scalar* maximum = find(out, "u");
  check(maximum && maximum->type == JsonScanner::Value::Number &&
            maximum->isUnsigned && maximum->number == 0xffffffffUL,
        "maximum uint32 remains exact");
  const Scalar* large = find(out, "big");
  check(large && !large->isUnsigned && large->number == 0 &&
            large->text == "4294967296",
        "overflowing integer remains available as number text");
  const Scalar* negative = find(out, "neg");
  check(negative && !negative->isUnsigned && negative->text == "-2",
        "negative number is typed but not unsigned");
  const Scalar* real = find(out, "real");
  check(real && real->type == JsonScanner::Value::Number &&
            real->text == "1.25e+2",
        "fraction and exponent retain exact text");
  check(find(out, "yes") && find(out, "yes")->type == JsonScanner::Value::Boolean &&
            find(out, "yes")->text == "true",
        "true has Boolean type and text");
  check(find(out, "no") && find(out, "no")->type == JsonScanner::Value::Boolean,
        "false has Boolean type");
  check(find(out, "nil") && find(out, "nil")->type == JsonScanner::Value::Null,
        "null has Null type");

  const std::vector<std::string> expected = {
      "object+", "array+rows", "object+", "object-", "array-rows", "object-"};
  check(out.events == expected, "container callbacks are ordered and scoped");
}

void runInputModes() {
  const std::string document = R"({"answer":42})";

  StringStream exactStream(document);
  JsonScanner exact(exactStream, static_cast<int>(document.size()));
  check(exact.walk(nullptr, nullptr), "stream-only exact length parses");

  StringStream unknownStream(document);
  JsonScanner unknown(unknownStream);
  check(unknown.walk(nullptr, nullptr), "stream-only unknown length parses");

  StringStream networkStream(document);
  NetClient client(false);
  JsonScanner legacy(networkStream, client, static_cast<int>(document.size()), 50);
  check(legacy.walk(nullptr, nullptr), "legacy network constructor parses");

  StringStream boundedStream(document);
  JsonScanner bounded(boundedStream, client, static_cast<int>(document.size()),
                      50, document.size() - 1);
  check(!bounded.walk(nullptr, nullptr) &&
            bounded.error() == JsonScanner::Error::SizeLimit,
        "network max byte limit is enforced before reading");

  StringStream shortStream("{}");
  JsonScanner shortBody(shortStream, 3);
  check(!shortBody.walk(nullptr, nullptr) &&
            shortBody.error() == JsonScanner::Error::UnexpectedEnd,
        "exact stream length detects an early EOF");

  emulatorConfigure(EmulatorBoard::Esp8266, EmulatorNetwork::Sta, -56, 640,
                    "/tmp", 0, ".");
  NetClient connected(false);
  check(connected.connect("fixture.invalid", 80) == 1,
        "fixture client connects for timeout test");
  StringStream emptyStream("");
  JsonScanner waiting(emptyStream, connected, JsonScanner::UnknownLength, 3, 32);
  check(!waiting.walk(nullptr, nullptr) && waiting.timedOut() &&
            waiting.error() == JsonScanner::Error::Timeout,
        "connected network input reports timeout");
  connected.stop();
  emulatorConfigure(EmulatorBoard::Esp8266, EmulatorNetwork::Offline, -56, 640,
                    "/tmp", 0, "");
}

void runLargeAndCallerBuffers() {
  check(sizeof(JsonScanner) <= 1024,
        "scanner object stays within the ESP8266 stack budget");
  const std::string payload(512, 'x');
  const std::string document = std::string("{\"value\":\"") + payload + "\"}";
  Collected out;
  JsonScanner scanner(document.data(), document.size());
  check(scanner.walk(onValue, &out), "default buffer skips oversized string tail");
  check(out.values.size() == 1 && out.values[0].truncated &&
            out.values[0].fullLength == payload.size() &&
            out.values[0].text.size() == JsonScanner::MaxValue - 1,
        "oversized value reports truncation and full decoded length");

  char supplied[600];
  Collected suppliedOut;
  JsonScanner suppliedScanner(document.data(), document.size());
  suppliedScanner.setValueBuffer(supplied, sizeof(supplied));
  check(suppliedScanner.walk(onValue, &suppliedOut), "caller value buffer is used");
  check(suppliedOut.values[0].text == payload, "caller buffer preserves value");

  char tiny[5];
  JsonScanner tinyScanner(document.data(), document.size());
  tinyScanner.setValueBuffer(tiny, sizeof(tiny));
  check(tinyScanner.walk(nullptr, nullptr),
        "ignored oversized value remains valid JSON");

  std::string longKey(JsonScanner::MaxKey, 'k');
  const std::string keyDocument = std::string("{\"") + longKey + "\":1}";
  check(memoryError(keyDocument) == JsonScanner::Error::KeyTooLong,
        "oversized key reports KeyTooLong");
}

void runStrictGrammar() {
  checkError("", JsonScanner::Error::UnexpectedEnd, "empty document");
  checkError("{", JsonScanner::Error::UnexpectedEnd, "truncated object");
  checkError("[1", JsonScanner::Error::UnexpectedEnd, "truncated array");
  checkError("{\"a\":1]", JsonScanner::Error::InvalidSyntax,
             "mismatched delimiter");
  checkError("{\"a\":1,}", JsonScanner::Error::InvalidSyntax,
             "object trailing comma");
  checkError("[1,]", JsonScanner::Error::InvalidSyntax, "array trailing comma");
  checkError("{\"a\" 1}", JsonScanner::Error::InvalidSyntax, "missing colon");
  checkError("{\"a\":1 \"b\":2}", JsonScanner::Error::InvalidSyntax,
             "missing comma");
  checkError("true false", JsonScanner::Error::TrailingData, "trailing value");
  checkError("nullx", JsonScanner::Error::TrailingData, "trailing literal data");
  checkError("\"bad\\q\"", JsonScanner::Error::InvalidEscape,
             "unknown string escape");
  checkError("\"bad\\u12xz\"", JsonScanner::Error::InvalidEscape,
             "invalid unicode escape");
  checkError("\"\\ud800\"", JsonScanner::Error::InvalidEscape,
             "lone high surrogate");
  checkError("\"\\udc00\"", JsonScanner::Error::InvalidEscape,
             "lone low surrogate");
  checkError("\"control\n\"", JsonScanner::Error::InvalidSyntax,
             "unescaped control character");
  checkError("tru", JsonScanner::Error::InvalidSyntax, "truncated literal");

  const char* invalidNumbers[] = {"+1", "01", "-01", "1.", "1e", "1e+", "--1", "1x"};
  for (const char* number : invalidNumbers) {
    checkError(number,
               number[0] == '+' ? JsonScanner::Error::InvalidSyntax
                                : JsonScanner::Error::InvalidNumber,
               number);
  }
}

void runLimits() {
  std::string deep;
  for (uint8_t i = 0; i < JsonScanner::MaxDepth + 1; ++i) deep += '[';
  for (uint8_t i = 0; i < JsonScanner::MaxDepth + 1; ++i) deep += ']';
  check(memoryError(deep) == JsonScanner::Error::DepthLimit,
        "excess nesting reports DepthLimit");

  StringStream stream("{} ");
  JsonScanner scanner(stream, JsonScanner::UnknownLength, 2);
  check(!scanner.walk(nullptr, nullptr) &&
            scanner.error() == JsonScanner::Error::SizeLimit,
        "unknown stream counts trailing whitespace toward max bytes");
}

}  // namespace

int main() {
  emulatorConfigure(EmulatorBoard::Esp8266, EmulatorNetwork::Offline, -56, 640,
                    "/tmp", 0, "");
  emulatorSetMillis(1000);

  runTypedValuesAndPaths();
  runInputModes();
  runLargeAndCallerBuffers();
  runStrictGrammar();
  runLimits();

  if (failures) {
    std::fprintf(stderr, "%d JsonScanner check(s) failed\n", failures);
    return 1;
  }
  std::puts("JsonScanner tests passed");
  return 0;
}
