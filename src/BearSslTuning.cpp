// BearSslTuning.cpp — pin the ESP8266's BearSSL elliptic-curve support to P-256.
//
// DeskMate talks to modern HTTPS APIs that may negotiate ECDHE. Full BearSSL
// advertises several curves, including x25519, which is expensive on this chip.
// Defining br_ec_get_default() here overrides the library default at link time
// and pins negotiation to the prebuilt P-256 implementation. That avoids pulling
// unused curve code and makes OpenWeather, GitHub, ADS-B, and OTA handshakes more
// predictable on the memory-constrained ESP8266.
//
// Under BEARSSL_SSL_BASIC there is no EC engine to pin, so this is skipped.
#if defined(ESP8266) && !defined(BEARSSL_SSL_BASIC)
#include <bearssl/bearssl_ec.h>

extern "C" const br_ec_impl *br_ec_get_default(void) {
  return &br_ec_p256_m15;
}
#endif
