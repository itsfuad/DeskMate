#include "HttpRequest.h"

namespace {
constexpr uint32_t kBaseFreeHeapBytes = 12000;
constexpr uint32_t kTlsBlockMarginBytes = 7000;
constexpr uint8_t kTlsHostCacheSize = 6;

struct TlsHostCacheEntry {
  char host[48];
  uint16_t receiveBytes;
};

TlsHostCacheEntry g_tlsHostCache[kTlsHostCacheSize] = {};

class LimitedStreamImpl : public Stream {
 public:
  LimitedStreamImpl(Stream& source, size_t limit)
      : source_(source), remaining_(limit) {}

  int available() override {
    const int available = source_.available();
    return available < 0 ? 0 : min<size_t>(static_cast<size_t>(available), remaining_);
  }

  int read() override {
    if (!remaining_) return -1;
    const int value = source_.read();
    if (value >= 0) --remaining_;
    return value;
  }

  int peek() override { return remaining_ ? source_.peek() : -1; }
  void flush() override { source_.flush(); }
  size_t write(uint8_t value) override { return source_.write(value); }
  using Print::write;

 private:
  Stream& source_;
  size_t remaining_;
};
}

bool networkMemoryReady(uint16_t tlsReceiveBytes, uint32_t workingSetBytes);

uint16_t networkTlsReceiveBuffer(const char* host) {
#if defined(DESKMATE_ESP8266)
  if (!host || !host[0]) return 4096;
  for (TlsHostCacheEntry& entry : g_tlsHostCache) {
    if (strcmp(entry.host, host) == 0) return entry.receiveBytes;
  }

  uint16_t receiveBytes = 4096;
  if (networkMemoryReady(receiveBytes, 0)) {
    if (BearSSL::WiFiClientSecure::probeMaxFragmentLength(host, 443, 512)) {
      receiveBytes = 512;
    } else if (BearSSL::WiFiClientSecure::probeMaxFragmentLength(host, 443, 1024)) {
      receiveBytes = 1024;
    }
  }

  static uint8_t nextEntry = 0;
  TlsHostCacheEntry& entry = g_tlsHostCache[nextEntry++ % kTlsHostCacheSize];
  strlcpy(entry.host, host, sizeof(entry.host));
  entry.receiveBytes = receiveBytes;
  return receiveBytes;
#else
  (void)host;
#endif
  return 4096;
}

bool networkMemoryReady(uint16_t tlsReceiveBytes, uint32_t workingSetBytes) {
#if defined(DESKMATE_ESP8266)
  const uint32_t minimumFree = kBaseFreeHeapBytes + workingSetBytes + tlsReceiveBytes;
  const uint32_t minimumBlock = kTlsBlockMarginBytes + tlsReceiveBytes;
  return ESP.getFreeHeap() >= minimumFree &&
         platformMaxFreeBlock() >= minimumBlock;
#else
  (void)tlsReceiveBytes;
  (void)workingSetBytes;
  return true;
#endif
}

class HttpRequest::LimitedStream : public LimitedStreamImpl {
 public:
  LimitedStream(Stream& source, size_t limit) : LimitedStreamImpl(source, limit) {}
};

HttpRequest::~HttpRequest() { end(); }

bool HttpRequest::begin(const String& url, const HttpRequestOptions& options) {
  end();

  const bool https = url.startsWith("https://");
  uint16_t tlsReceiveBytes = 0;
  if (https) {
    tlsReceiveBytes = networkTlsReceiveBuffer(options.host);
    if (!networkMemoryReady(tlsReceiveBytes, options.workingSetBytes)) return false;
    client_.reset(platformMakeSecureClient(tlsReceiveBytes, options.session,
                                            options.txBufferBytes,
                                            options.cheapCiphers));
  } else {
    client_.reset(new WiFiClient());
  }
  if (!client_) return false;

  http_.setTimeout(options.timeoutMs);
  http_.setReuse(false);
  http_.useHTTP10(true);
  if (!http_.begin(*client_, url)) {
    end();
    return false;
  }

  limited_.reset(new LimitedStream(http_.getStream(), options.responseLimitBytes));
  if (!limited_) {
    end();
    return false;
  }
  started_ = true;
  return true;
}

void HttpRequest::end() {
  if (started_) http_.end();
  limited_.reset();
  client_.reset();
  started_ = false;
}

Stream& HttpRequest::stream() { return *limited_; }
