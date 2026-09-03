#pragma once

#include "Platform.h"

// Read a bounded HTTP response header without constructing HTTPClient's URL
// and header String state. Chunked responses are reported separately to the
// caller.
//
// By default a body of unknown length is rejected, which is what a caller
// needs when it must know the size up front -- a firmware image, say. Callers
// that stream and stop at end-of-stream can pass allowUnknownLength: an
// HTTP/1.0 server may legally delimit a body by closing the connection, and
// GitHub's GraphQL endpoint does exactly that, sending neither Content-Length
// nor Transfer-Encoding. Such a response reports contentLength -1.
bool httpReadResponseHeaders(NetClient& client, uint32_t timeoutMs,
                             size_t maximumBytes, int* code,
                             int* contentLength, bool* chunked,
                             bool allowUnknownLength = false);

// Connect and send one bounded HTTP/1.0 GET. The caller owns the client type
// (plain or TLS) and reads the response body directly from it after this
// function returns. allowUnknownLength has the same meaning as above: pass it
// when the caller parses as it reads, which is what a CDN answering HTTP/1.0
// with a close-delimited body requires.
bool httpGet(NetClient& client, const char* url, const char* userAgent,
             const char* accept, uint32_t timeoutMs, size_t maximumBytes,
             int* code, int* contentLength, bool* chunked,
             bool allowUnknownLength = false);
