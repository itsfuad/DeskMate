#pragma once

#include "Platform.h"

// Validate response headers before a caller starts parsing the body. This does
// not own or configure the client, HTTP transaction, TLS buffers, or stream.
bool httpResponseReady(HTTPClient& http, int code, size_t maximumBytes,
                       int* contentLength = nullptr,
                       int expectedCode = HTTP_CODE_OK);

// Read a bounded HTTP response header without constructing HTTPClient's URL
// and header String state. Unknown-length bodies are rejected unless they are
// explicitly chunked; chunked responses are reported separately to the caller.
bool httpReadResponseHeaders(NetClient& client, uint32_t timeoutMs,
                             size_t maximumBytes, int* code,
                             int* contentLength, bool* chunked);
