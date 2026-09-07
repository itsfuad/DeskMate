#include "HttpRequest.h"
#include "CrashBreadcrumbs.h"

namespace {
bool readHeaderLine(NetClient& client, char* output, size_t outputSize,
                    uint32_t timeoutMs) {
  if (!outputSize) return false;
  size_t length = 0;
  size_t received = 0;
  const uint32_t started = millis();
  for (;;) {
    if (millis() - started >= timeoutMs) return false;
    if (client.available()) {
      const int value = client.read();
      if (value < 0) continue;
      if (value == '\n') {
        if (length && output[length - 1] == '\r') --length;
        output[length < outputSize ? length : outputSize - 1] = 0;
        return true;
      }
      if (++received > 4096) return false;
      if (length + 1 < outputSize) output[length++] = static_cast<char>(value);
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

bool httpReadResponseHeaders(NetClient& client, uint32_t timeoutMs,
                             size_t maximumBytes, int* code,
                             int* contentLength, bool* chunked,
                             bool allowUnknownLength) {
  crashMark(CrashOperation::HttpHeaders);
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

  const uint32_t headerStarted = millis();
  uint16_t headerCount = 0;
  for (;;) {
    if (++headerCount > 64 || millis() - headerStarted >= timeoutMs ||
        !readHeaderLine(client, line, sizeof(line), timeoutMs)) return false;
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
  if (contentLength && *contentLength >= 0) return true;
  if (chunked && *chunked) return true;
  // Nothing framed the body. That is a hard error for a caller that needs the
  // size in advance, and ordinary HTTP/1.0 close-delimiting for one that
  // streams until the peer disconnects.
  return allowUnknownLength;
}

bool httpGet(NetClient& client, const char* url, const char* userAgent,
             const char* accept, uint32_t timeoutMs, size_t maximumBytes,
             int* code, int* contentLength, bool* chunked,
             bool allowUnknownLength) {
  if (!url || !url[0]) return false;
  const char* hostStart = nullptr;
  uint16_t port = 0;
  if (!strncmp(url, "https://", 8)) {
    hostStart = url + 8;
    port = 443;
  } else if (!strncmp(url, "http://", 7)) {
    hostStart = url + 7;
    port = 80;
  } else {
    return false;
  }

  const bool https = port == 443;
  const char* path = strpbrk(hostStart, "/?#");
  const size_t hostLength = path ? static_cast<size_t>(path - hostStart) : strlen(hostStart);
  if (!path) path = "";
  char host[128];
  if (!hostLength || hostLength >= sizeof(host)) return false;
  memcpy(host, hostStart, hostLength);
  host[hostLength] = 0;
  for (const char* p = host; *p; ++p)
    if (static_cast<unsigned char>(*p) <= 32 || *p == '@' || *p == '[' || *p == ']') return false;
  char* explicitPort = strchr(host, ':');
  if (explicitPort) {
    *explicitPort++ = 0;
    uint32_t parsedPort = 0;
    if (!host[0] || !*explicitPort) return false;
    for (const char* p = explicitPort; *p; ++p) {
      if (*p < '0' || *p > '9') return false;
      parsedPort = parsedPort * 10 + (*p - '0');
      if (parsedPort > 65535) return false;
    }
    if (!parsedPort) return false;
    port = static_cast<uint16_t>(parsedPort);
  }
  const size_t pathLength = strcspn(path, "#");
  for (size_t i = 0; i < pathLength; ++i)
    if (static_cast<unsigned char>(path[i]) <= 32) return false;

  client.setTimeout(timeoutMs);
  if (https && !platformTlsConnectMemoryReady()) {
    crashMark(CrashOperation::TlsAdmissionRejected, port);
    return false;
  }
  crashMark(CrashOperation::HttpConnect, port);
  const bool connected = client.connect(host, port);
  crashMark(CrashOperation::HttpConnected, connected ? port : 0);
  if (!connected) return false;

  crashMark(CrashOperation::HttpWriteBegin, port);
  const char* accepted = accept && accept[0] ? accept : "*/*";
  const char* agent = userAgent && userAgent[0] ? userAgent : "DeskMate";
  bool sent = client.print(F("GET ")) == 4 &&
      (path[0] == '/' || client.print('/') == 1) &&
      client.write(reinterpret_cast<const uint8_t*>(path), pathLength) == pathLength &&
      client.print(F(" HTTP/1.0\r\nHost: ")) == 17 &&
      client.write(reinterpret_cast<const uint8_t*>(hostStart), hostLength) == hostLength &&
      client.print(F("\r\nAccept: ")) == 10 && client.print(accepted) == strlen(accepted) &&
      client.print(F("\r\nUser-Agent: ")) == 14 && client.print(agent) == strlen(agent) &&
      client.print(F("\r\nConnection: close\r\n\r\n")) == 23;
  if (!sent) { client.stop(); return false; }
  crashMark(CrashOperation::HttpWriteEnd, port);

  return httpReadResponseHeaders(client, timeoutMs, maximumBytes, code,
                                 contentLength, chunked, allowUnknownLength);
}
