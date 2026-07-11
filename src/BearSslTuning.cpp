// BearSslTuning.cpp — pin the ESP8266's BearSSL elliptic-curve support to P-256.
//
// The firmware builds with full BearSSL so cash.ch's ECDHE-only CDN is
// reachable. Full BearSSL by default advertises x25519 + P-256/384/521 and lets
// the server choose; x25519 in particular is heavy on this chip. Defining
// br_ec_get_default() here overrides the library's version at link time (our
// object satisfies the reference before ec_default.o is pulled from the
// archive), forcing P-256 only (the prebuilt lib ships br_ec_p256_m15). This
// avoids the much heavier x25519 the server could otherwise pick, and drops the
// unused curve code from flash. Stack cost of the EC step is minor next to the
// RSA cert-chain verify; the real savings come from session resumption + small
// buffers + the contiguous-heap guard in StockClient.
#if defined(ESP8266)
#include <bearssl/bearssl_ec.h>

extern "C" const br_ec_impl *br_ec_get_default(void) {
  return &br_ec_p256_m15;
}
#endif
