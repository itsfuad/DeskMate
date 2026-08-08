#pragma once

#include <Arduino.h>
#include <memory>
#include "Platform.h"

struct HttpRequestOptions {
  const char* host = nullptr;
  uint16_t timeoutMs = 5000;
  uint32_t workingSetBytes = 6000;
  size_t responseLimitBytes = 49152;
  TlsSession* session = nullptr;
  uint16_t txBufferBytes = 512;
  bool cheapCiphers = false;
};

// One guarded, bounded HTTP(S) transaction. The request owns the client and
// the response stream wrapper, so every caller gets the same cleanup and heap
// admission policy.
class HttpRequest {
 public:
  HttpRequest();
  ~HttpRequest();

  bool begin(const String& url, const HttpRequestOptions& options);
  void end();

  HTTPClient& http() { return http_; }
  NetClient& client() { return *client_; }
  Stream& stream();

 private:
  class LimitedStream;
  std::unique_ptr<NetClient> client_;
  HTTPClient http_;
  std::unique_ptr<LimitedStream> limited_;
  bool started_ = false;
};

uint16_t networkTlsReceiveBuffer(const char* host);
bool networkMemoryReady(uint16_t tlsReceiveBytes, uint32_t workingSetBytes);
