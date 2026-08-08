#pragma once

#include "Platform.h"

// Validate response headers before a caller starts parsing the body. This does
// not own or configure the client, HTTP transaction, TLS buffers, or stream.
bool httpResponseReady(HTTPClient& http, int code, size_t maximumBytes,
                       int* contentLength = nullptr,
                       int expectedCode = HTTP_CODE_OK);
