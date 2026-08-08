#include "HttpRequest.h"

bool httpResponseReady(HTTPClient& http, int code, size_t maximumBytes,
                       int* contentLength, int expectedCode) {
  const int length = http.getSize();
  if (contentLength) *contentLength = length;
  return code == expectedCode && length >= 0 &&
         static_cast<size_t>(length) <= maximumBytes;
}
