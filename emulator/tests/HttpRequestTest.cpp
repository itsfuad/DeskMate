#include "HttpRequest.h"
#include <cstdio>
#include <cstdlib>
#include <string>

class Client : public EmulatorClient {
 public:
  std::string response = "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\n{}";
  std::string sent;
  size_t at = 0;
  size_t writeLimit = SIZE_MAX;
  int available() override { return static_cast<int>(response.size() - at); }
  int read() override { return at < response.size() ? static_cast<unsigned char>(response[at++]) : -1; }
  int peek() override { return at < response.size() ? static_cast<unsigned char>(response[at]) : -1; }
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    const size_t count = std::min(length, writeLimit);
    sent.append(reinterpret_cast<const char*>(data), count);
    return count;
  }
};

void check(bool value, const char* message) {
  if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
bool get(Client& client, const char* url) {
  int code = 0, length = -1;
  bool chunked = false;
  return httpGet(client, url, "test", "application/json", 1000, 1024,
                 &code, &length, &chunked) && code == 200 && length == 2;
}
int main() {
  emulatorConfigure(EmulatorBoard::Esp8266, EmulatorNetwork::Sta, -56, 640,
                    "/tmp/deskmate-http-test", 0, ".");
  emulatorSetMillis(0);
  Client root;
  check(get(root, "http://fixture.example"), "host-only URL failed");
  check(root.sent.find("GET / HTTP/1.0\r\nHost: fixture.example\r\n") == 0,
        "host-only URL request incorrect");
  Client query;
  check(get(query, "http://fixture.example:8080?q=1#ignored"), "query/port URL failed");
  check(query.sent.find("GET /?q=1 HTTP/1.0\r\nHost: fixture.example:8080\r\n") == 0,
        "query/port request incorrect");
  Client shortWrite;
  shortWrite.writeLimit = 1;
  check(!get(shortWrite, "http://fixture.example/"), "short write accepted");
  check(shortWrite.at == 0, "response parsed after failed request write");
  Client longHeader;
  longHeader.response = "HTTP/1.0 200 OK\r\nX-Long: " + std::string(512, 'x') +
      "\r\nContent-Length: 2\r\n\r\n{}";
  check(get(longHeader, "http://fixture.example/"), "long optional header failed");
  Client unbounded;
  unbounded.response = "HTTP/1.0 200 OK\r\nX: " + std::string(5000, 'x') + "\r\n\r\n";
  check(!get(unbounded, "http://fixture.example/"), "unbounded header accepted");
  for (const char* url : {"http://", "http://fixture:0/", "http://fixture:65536/",
                          "http://fixture:bad/", "http://fixture/\r\ninjected"}) {
    Client invalid;
    check(!get(invalid, url) && invalid.sent.empty(), "invalid URL accepted");
  }
  std::puts("HTTP URL, short-write and long-header regressions passed");
}
