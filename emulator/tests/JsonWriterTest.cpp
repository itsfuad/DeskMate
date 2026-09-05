#include "EmulatorPlatform.h"
#include "JsonScanner.h"
#include "JsonWriter.h"

#include <climits>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace {

class StringPrint : public Print {
 public:
  explicit StringPrint(size_t limit = static_cast<size_t>(-1)) : limit_(limit) {}

  size_t write(uint8_t value) override {
    if (text.size() >= limit_) return 0;
    text.push_back(static_cast<char>(value));
    return 1;
  }

  size_t write(const uint8_t* data, size_t size) override {
    const size_t available = limit_ - (text.size() < limit_ ? text.size() : limit_);
    const size_t count = size < available ? size : available;
    text.append(reinterpret_cast<const char*>(data), count);
    return count;
  }

  std::string text;

 private:
  size_t limit_;
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

void runDocument() {
  StringPrint output;
  JsonWriter writer(output);
  check(writer.beginObject(), "begin root object");
  check(writer.key("text"), "write text key");
  std::string controls = "quote\" slash\\";
  controls += '\n';
  controls += static_cast<char>(1);
  check(writer.value(controls.c_str()), "write escaped text");
  check(writer.key("values") && writer.beginArray(), "begin values array");
  check(writer.value(true), "write boolean");
  check(writer.value(-42), "write signed integer");
  check(writer.value(0xffffffffUL), "write unsigned integer");
  check(writer.value(LLONG_MIN), "write minimum signed integer");
  check(writer.value(1.5), "write finite double");
  check(writer.nullValue(), "write null");
  check(writer.beginObject() && writer.key("utf8") && writer.value("é") &&
            writer.endObject(),
        "write nested object");
  check(writer.endArray() && writer.endObject(), "close containers");
  check(writer.complete(), "closed document is complete");
  check(writer.error() == JsonWriter::Error::None, "successful writer has no error");

  checkText(output.text,
            "{\"text\":\"quote\\\" slash\\\\\\n\\u0001\","
            "\"values\":[true,-42,4294967295,-9223372036854775808,1.5,null,"
            "{\"utf8\":\"é\"}]}",
            "writer output is compact valid JSON");

  JsonScanner scanner(output.text.data(), output.text.size());
  check(scanner.walk(nullptr, nullptr), "writer output parses with strict scanner");
}

void runRootValues() {
  StringPrint stringOutput;
  JsonWriter stringWriter(stringOutput);
  check(stringWriter.value("root") && stringWriter.complete(),
        "root string is supported");
  checkText(stringOutput.text, "\"root\"", "root string output");

  StringPrint nullOutput;
  JsonWriter nullWriter(nullOutput);
  check(nullWriter.value(static_cast<const char*>(nullptr)) && nullWriter.complete(),
        "null char pointer writes null");
  checkText(nullOutput.text, "null", "null pointer output");

  StringPrint emptyOutput;
  JsonWriter emptyWriter(emptyOutput);
  check(emptyWriter.beginArray() && emptyWriter.endArray() && emptyWriter.complete(),
        "empty array completes");
  checkText(emptyOutput.text, "[]", "empty array output");
}

void runStateErrors() {
  {
    StringPrint output;
    JsonWriter writer(output);
    check(!writer.key("bad") && writer.error() == JsonWriter::Error::InvalidState,
          "key outside object is rejected");
  }
  {
    StringPrint output;
    JsonWriter writer(output);
    check(writer.beginObject() && writer.key("missing"), "prepare missing value");
    check(!writer.endObject() && writer.error() == JsonWriter::Error::InvalidState,
          "object cannot close while waiting for value");
  }
  {
    StringPrint output;
    JsonWriter writer(output);
    check(writer.beginArray(), "prepare mismatched close");
    check(!writer.endObject() && writer.error() == JsonWriter::Error::InvalidState,
          "mismatched close is rejected");
  }
  {
    StringPrint output;
    JsonWriter writer(output);
    check(writer.value(1), "write first root");
    check(!writer.value(2) && writer.error() == JsonWriter::Error::InvalidState,
          "second root value is rejected");
  }
  {
    StringPrint output;
    JsonWriter writer(output);
    check(writer.beginObject() && writer.key("a"), "prepare duplicate key call");
    check(!writer.key("b") && writer.error() == JsonWriter::Error::InvalidState,
          "key while waiting for value is rejected");
  }
}

void runLimitsAndOutputErrors() {
  {
    StringPrint output;
    JsonWriter writer(output);
    for (uint8_t i = 0; i < JsonWriter::MaxDepth; ++i)
      check(writer.beginArray(), "nest within writer depth");
    check(!writer.beginArray() && writer.error() == JsonWriter::Error::DepthLimit,
          "writer depth limit is enforced");
  }
  {
    StringPrint output;
    JsonWriter writer(output);
    check(!writer.value(std::numeric_limits<double>::infinity()) &&
              writer.error() == JsonWriter::Error::InvalidNumber,
          "infinity is rejected");
  }
  {
    StringPrint output(5);
    JsonWriter writer(output);
    check(writer.beginObject() && writer.key("a"), "write up to output limit");
    check(!writer.value("value") &&
              writer.error() == JsonWriter::Error::WriteFailure,
          "short Print write propagates WriteFailure");
    check(!writer.endObject(), "writer remains failed after output error");
  }
}

}  // namespace

int main() {
  emulatorConfigure(EmulatorBoard::Esp8266, EmulatorNetwork::Offline, -56, 640,
                    "/tmp", 0, "");
  runDocument();
  runRootValues();
  runStateErrors();
  runLimitsAndOutputErrors();

  if (failures) {
    std::fprintf(stderr, "%d JsonWriter check(s) failed\n", failures);
    return 1;
  }
  std::puts("JsonWriter tests passed");
  return 0;
}
