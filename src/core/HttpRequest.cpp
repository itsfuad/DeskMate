#include "HttpRequest.h"

namespace {
bool readHeaderLine(NetClient& client, char* output, size_t outputSize,
                    uint32_t timeoutMs) {
  if (!outputSize) return false;
  size_t length = 0;
  const uint32_t started = millis();
  for (;;) {
    if (client.available()) {
      const int value = client.read();
      if (value < 0) continue;
      if (value == '\n') {
        if (length && output[length - 1] == '\r') --length;
        output[length < outputSize ? length : outputSize - 1] = 0;
        return true;
      }
      if (length + 1 < outputSize) output[length] = static_cast<char>(value);
      ++length;
      continue;
    }
    if (!client.connected() || millis() - started >= timeoutMs) return false;
    delay(1);
    yield();
  }
}

bool headerNameIs(const char* line, const char* name) {
  const size_t length = strlen(name);
  return strncasecmp(line, name, length) == 0 && line[length] == ':';
}
}

bool httpResponseReady(HTTPClient& http, int code, size_t maximumBytes,
                       int* contentLength, int expectedCode) {
  const int length = http.getSize();
  if (contentLength) *contentLength = length;
  return code == expectedCode && length >= 0 &&
         static_cast<size_t>(length) <= maximumBytes;
}

bool httpReadResponseHeaders(NetClient& client, uint32_t timeoutMs,
                             size_t maximumBytes, int* code,
                             int* contentLength, bool* chunked) {
  if (code) *code = 0;
  if (contentLength) *contentLength = -1;
  if (chunked) *chunked = false;

  client.setTimeout(timeoutMs);
  char line[128];
  if (!readHeaderLine(client, line, sizeof(line), timeoutMs) ||
      strncmp(line, "HTTP/", 5) != 0) {
    return false;
  }

  char* status = strchr(line, ' ');
  if (!status || !code) return false;
  *code = atoi(status + 1);

  for (;;) {
    if (!readHeaderLine(client, line, sizeof(line), timeoutMs)) return false;
    if (!line[0]) break;

    if (headerNameIs(line, "Content-Length")) {
      const unsigned long length = strtoul(strchr(line, ':') + 1, nullptr, 10);
      if (length > maximumBytes) return false;
      if (contentLength) *contentLength = static_cast<int>(length);
    } else if (headerNameIs(line, "Transfer-Encoding") &&
               strstr(line, "chunked")) {
      if (chunked) *chunked = true;
    }
  }
  return (contentLength && *contentLength >= 0) || (chunked && *chunked);
}
