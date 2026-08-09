#pragma once

#include "Arduino.h"
#include "Stream.h"
#include "EmulatorWebServer.h"

#include <cstdint>
#include <memory>
#include <string>

enum class EmulatorBoard { Esp8266, Esp32C2, Esp32 };
enum class EmulatorNetwork { Sta, Ap, Offline };

struct EmulatorBoardProfile {
  const char* id;
  const char* displayName;
  uint32_t chipId;
  uint32_t cpuMhz;
  uint32_t heapBytes;
  uint32_t maximumBlockBytes;
  uint32_t flashBytes;
  uint32_t otaSlotBytes;
  bool hasLdr;
  uint16_t adcMax;
  const char* updateAsset;
};

struct EmulatorResetInfoData {
  String reason;
  bool wasCrash = false;
  char epc[16] = "";
  char addr[16] = "";
};

void emulatorConfigure(EmulatorBoard board, EmulatorNetwork network,
                       int rssi, int ldr, const std::string& stateDirectory,
                       uint16_t webPort,
                       const std::string& responseDirectory = {});
const EmulatorBoardProfile& emulatorBoardProfile();
EmulatorNetwork emulatorNetworkMode();
uint16_t emulatorWebPort();
const std::string& emulatorStateDirectory();
const std::string& emulatorResponseDirectory();
bool emulatorRestartRequested();
void emulatorRequestRestart();
void emulatorSetHostname(const char* hostname);
void emulatorTimeBegin(const char* tz, const char*, const char*);
bool emulatorScanIsOpen(int index);
String emulatorUpdateError();
uint32_t emulatorChipId();
uint32_t emulatorCpuFreqMhz();
EmulatorResetInfoData emulatorResetInfo();
uint32_t emulatorMaxFreeBlock();
uint32_t emulatorFreeContStack();
bool emulatorTlsMemoryReady();
int emulatorLdrValue();
const char* emulatorUpdateAsset();

class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
  explicit IPAddress(const std::string& value) : value_(value) {}
  String toString() const { return String(value_); }
 private:
  std::string value_ = "0.0.0.0";
};

enum { WIFI_OFF, WIFI_STA, WIFI_AP };
enum { WL_DISCONNECTED, WL_CONNECTED = 3 };
enum { WIFI_AUTH_OPEN = 0, ENC_TYPE_NONE = 0 };

class EmulatorWiFi {
 public:
  void persistent(bool) {}
  void setAutoReconnect(bool) {}
  void mode(int mode) { mode_ = mode; }
  void setHostname(const char* value) { emulatorSetHostname(value); }
  void hostname(const char* value) { emulatorSetHostname(value); }
  void begin(const char*, const char*) {}
  void reconnect() {}
  void disconnect() {}
  int status() const;
  bool softAP(const char*, const char* = nullptr) { mode_ = WIFI_AP; return true; }
  bool softAPConfig(const IPAddress&, const IPAddress&, const IPAddress&) { return true; }
  IPAddress localIP() const;
  IPAddress softAPIP() const { return IPAddress("127.0.0.1"); }
  String SSID() const;
  String SSID(int) const { return String("Host network"); }
  int RSSI() const;
  int RSSI(int) const { return RSSI(); }
  int scanNetworks() const { return emulatorNetworkMode() == EmulatorNetwork::Offline ? 0 : 1; }
  void scanDelete() {}
  int encryptionType(int) const { return WIFI_AUTH_OPEN; }
  int hostByName(const char* host, IPAddress& result, uint32_t timeoutMs = 0) const;
 private:
  int mode_ = WIFI_STA;
};

extern EmulatorWiFi WiFi;

class EmulatorClient : public Stream {
 public:
  explicit EmulatorClient(bool secure = false);
  ~EmulatorClient() override;
  EmulatorClient(const EmulatorClient&) = delete;
  EmulatorClient& operator=(const EmulatorClient&) = delete;

  int connect(const char* host, uint16_t port);
  int available() override;
  int read() override;
  int peek() override;
  size_t write(uint8_t value) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  bool connected();
  void stop();
  void setTimeout(unsigned long timeout) override;

 protected:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class WiFiClient : public EmulatorClient {
 public:
  WiFiClient() : EmulatorClient(false) {}
};

class EmulatorSecureClient : public EmulatorClient {
 public:
  EmulatorSecureClient() : EmulatorClient(true) {}
  void setInsecure() {}
  void setBufferSizes(uint16_t, uint16_t) {}
  template <typename T> void setSession(T*) {}
  void setCiphersLessSecure() {}
};

struct EmulatorTlsSession {};

class EmulatorUpdate {
 public:
  bool begin(uint32_t maximumSize);
  size_t write(const uint8_t* data, size_t size);
  bool end(bool evenIfRemaining = false);
  bool hasError() const { return !error_.empty(); }
  String errorString() const { return String(error_); }
  String getErrorString() const { return String(error_); }
  void printError(HardwareSerial& serial) const;
 private:
  uint32_t maximumSize_ = 0;
  std::string error_;
  std::string temporaryPath_;
  size_t written_ = 0;
  uint8_t firstByte_ = 0;
  bool hasFirstByte_ = false;
};

extern EmulatorUpdate Update;

class WiFiUDP {
 public:
  static void stopAll() {}
};
