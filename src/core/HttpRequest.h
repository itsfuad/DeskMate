#pragma once

#include "Platform.h"

// Read a bounded HTTP response header without constructing HTTPClient's URL
// and header String state. Unknown-length bodies are rejected unless they are
// explicitly chunked; chunked responses are reported separately to the caller.
bool httpReadResponseHeaders(NetClient& client, uint32_t timeoutMs,
                             size_t maximumBytes, int* code,
                             int* contentLength, bool* chunked);

// Connect and send one bounded HTTP/1.0 GET. The caller owns the client type
// (plain or TLS) and reads the response body directly from it after this
// function returns.
bool httpGet(NetClient& client, const char* url, const char* userAgent,
             const char* accept, uint32_t timeoutMs, size_t maximumBytes,
             int* code, int* contentLength, bool* chunked);
