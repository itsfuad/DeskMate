// Exercises JsonScanner against a GitHub-shaped GraphQL document.
//
// The properties tested here are the ones a flat "look for a quoted key"
// reader gets wrong, and gets wrong silently: the same field name appearing in
// several lists, a nested array reusing the outer array's name, and an
// errors[] entry that must not be confused with a payload field of the same
// name.
#include "EmulatorPlatform.h"
#include "JsonScanner.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

// A Stream over a std::string. contentLength is passed exactly, so the scanner
// terminates on the byte count and never consults the client.
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
  size_t write(const uint8_t*, size_t size) override { return size; }

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

void checkText(const char* actual, const char* expected, const char* what) {
  if (actual && !std::strcmp(actual, expected)) return;
  std::fprintf(stderr, "FAIL: %s -- expected \"%s\", got \"%s\"\n", what,
               expected, actual ? actual : "(null)");
  ++failures;
}

struct Collected {
  // Titles indexed by [list][row]: 0 = rev, 1 = own.
  std::string title[2][4];
  std::string repo[2][4];
  std::string checks[4];
  std::string review[4];
  std::string errorMessage;
  std::string payloadMessage;
  uint32_t issueCount[2] = {0, 0};
  int maxDepthSeen = 0;
  int scalars = 0;
};

int listIndexOf(const JsonScanner& scanner) {
  if (scanner.under("rev")) return 0;
  if (scanner.under("own")) return 1;
  return -1;
}

void onValue(void* context, const JsonScanner& scanner,
             JsonScanner::Value type, const char* text, uint32_t number) {
  Collected& out = *static_cast<Collected*>(context);
  ++out.scalars;
  const char* key = scanner.key();

  if (scanner.under("errors")) {
    if (!std::strcmp(key, "message")) out.errorMessage = text;
    return;
  }
  if (!std::strcmp(key, "message")) out.payloadMessage = text;

  const int list = listIndexOf(scanner);
  if (list < 0) return;

  if (!std::strcmp(key, "issueCount") &&
      type == JsonScanner::Value::Number) {
    out.issueCount[list] = number;
    return;
  }

  const int16_t row = scanner.indexUnder("nodes");
  if (row < 0 || row >= 4) return;
  if (!std::strcmp(key, "title")) {
    out.title[list][row] = text;
  } else if (!std::strcmp(key, "nameWithOwner")) {
    out.repo[list][row] = text;
  } else if (!std::strcmp(key, "review") &&
             type == JsonScanner::Value::String) {
    // A null reviewDecision arrives as Value::Other, so filtering on the type
    // is what keeps the literal "null" out of a display string.
    out.review[row] = text;
  } else if (!std::strcmp(key, "state") &&
             !std::strcmp(scanner.container(0), "checks")) {
    out.checks[row] = text;
  }
}

// Two lists share every field name, `own` nests a second "nodes" array inside
// each row, and a payload field is called "message" on purpose.
const char* kDocument = R"({"data":{
  "viewer":{"login":"itsfuad","message":"not an error"},
  "rev":{"issueCount":7,"nodes":[
    {"number":1,"title":"first review","repository":{"nameWithOwner":"a/one"}},
    {"number":2,"title":"second review","repository":{"nameWithOwner":"a/two"}}
  ]},
  "own":{"issueCount":3,"nodes":[
    {"number":10,"title":"my pr","isDraft":false,"review":"APPROVED",
     "repository":{"nameWithOwner":"b/repo"},
     "commits":{"nodes":[{"commit":{"checks":{"state":"FAILURE"}}}]}},
    {"number":11,"title":"draft pr","isDraft":true,"review":null,
     "repository":{"nameWithOwner":"b/other"},
     "commits":{"nodes":[{"commit":{"checks":{"state":"SUCCESS"}}}]}}
  ]}
},"errors":[{"message":"Bad credentials","type":"FORBIDDEN"}]})";

void runDocument() {
  StringStream stream(kDocument);
  NetClient client(false);
  Collected out;
  JsonScanner scanner(stream, client,
                      static_cast<int>(std::strlen(kDocument)), 2000);
  check(scanner.walk(onValue, &out), "document walks to completion");
  check(!scanner.timedOut(), "document does not time out");

  checkText(out.title[0][0].c_str(), "first review", "rev row 0 title");
  checkText(out.title[0][1].c_str(), "second review", "rev row 1 title");
  checkText(out.repo[0][1].c_str(), "a/two", "rev row 1 repository");

  // Same field names, different list: a depth-blind reader overwrites these.
  checkText(out.title[1][0].c_str(), "my pr", "own row 0 title");
  checkText(out.title[1][1].c_str(), "draft pr", "own row 1 title");
  checkText(out.repo[1][0].c_str(), "b/repo", "own row 0 repository");
  check(out.issueCount[0] == 7, "rev issueCount");
  check(out.issueCount[1] == 3, "own issueCount");

  // The rollup state lives under a second "nodes" array inside each row. The
  // row index must still come from the outer one.
  checkText(out.checks[0].c_str(), "FAILURE", "own row 0 check state");
  checkText(out.checks[1].c_str(), "SUCCESS", "own row 1 check state");
  checkText(out.review[0].c_str(), "APPROVED", "own row 0 review decision");
  check(out.review[1].empty(),
        "a null review decision is not reported as a string");

  checkText(out.errorMessage.c_str(), "Bad credentials", "errors[].message");
  checkText(out.payloadMessage.c_str(), "not an error",
            "a payload field named message stays in the payload");
}

// Structure deeper than MaxDepth must not corrupt the frames it does record.
void runOverflow() {
  std::string document = R"({"data":{"own":{"nodes":[{"title":"deep",)";
  const int extra = JsonScanner::MaxDepth + 6;
  for (int i = 0; i < extra; ++i) document += "\"w\":{";
  document += "\"leaf\":1";
  for (int i = 0; i < extra; ++i) document += "}";
  document += "}]}}}";

  StringStream stream(document);
  NetClient client(false);
  Collected out;
  JsonScanner scanner(stream, client,
                      static_cast<int>(document.size()), 2000);
  check(scanner.walk(onValue, &out), "over-deep document still completes");
  checkText(out.title[1][0].c_str(), "deep",
            "a field recorded before the overflow survives it");
}

// A body that stops mid-object is a failure, not a partial commit.
void runTruncated() {
  const std::string document = R"({"data":{"own":{"nodes":[{"title":"cut off")";
  StringStream stream(document);
  NetClient client(false);
  Collected out;
  JsonScanner scanner(stream, client,
                      static_cast<int>(document.size()), 2000);
  check(!scanner.walk(onValue, &out), "a truncated document fails the walk");
}

// Escapes must not desynchronize the key/value alternation.
void runEscapes() {
  const std::string document =
      R"({"data":{"rev":{"nodes":[{"title":"a \"quoted\" \\ title!",)"
      R"("repository":{"nameWithOwner":"a/one"}}]}}})";
  StringStream stream(document);
  NetClient client(false);
  Collected out;
  JsonScanner scanner(stream, client,
                      static_cast<int>(document.size()), 2000);
  check(scanner.walk(onValue, &out), "escaped document walks to completion");
  checkText(out.title[0][0].c_str(), "a \"quoted\" \\ title!",
            "escapes decode without losing the string boundary");
  checkText(out.repo[0][0].c_str(), "a/one",
            "the field after an escaped string is still parsed");
}

}  // namespace

int main() {
  emulatorConfigure(EmulatorBoard::Esp8266, EmulatorNetwork::Offline, -56, 640,
                    "/tmp", 0, "");
  emulatorSetMillis(1000);

  runDocument();
  runOverflow();
  runTruncated();
  runEscapes();

  if (failures) {
    std::fprintf(stderr, "%d JsonScanner check(s) failed\n", failures);
    return 1;
  }
  std::puts("JsonScanner tests passed: list isolation, nested arrays, "
            "error scoping, depth overflow, truncation, escapes.");
  return 0;
}
