#include "EmulatorPlatform.h"
#include "Platform.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
constexpr EmulatorBoardProfile profiles[] = {
    {"esp8266", "DeskMate ESP8266", 0x82661234, 160,
     48 * 1024, 24 * 1024, 4 * 1024 * 1024, 1536 * 1024, true, 1023,
     "deskmate-firmware.bin"},
    {"esp32c2", "DeskMate ESP32-C2", 0x32c21234, 80,
     220 * 1024, 160 * 1024, 4 * 1024 * 1024, 1408 * 1024, false, 4095,
     "deskmate-firmware-c2.bin"},
    {"esp32", "DeskMate ESP32", 0x32001234, 240,
     280 * 1024, 200 * 1024, 4 * 1024 * 1024, 1408 * 1024, false, 4095,
     "deskmate-firmware-esp32.bin"},
};

EmulatorBoard currentBoard = EmulatorBoard::Esp8266;
EmulatorNetwork currentNetwork = EmulatorNetwork::Sta;
int currentRssi = -56;
int currentLdr = 640;
uint16_t currentWebPort = 8080;
std::string currentStateDirectory = "emulator/.state/esp8266";
std::string currentResponseDirectory;
std::string currentHostname = "deskmate-emulator";
std::string updateError;
bool restartRequested = false;
void (*timeSyncCallback)() = nullptr;
unsigned radarFixtureIndex = 0;

const EmulatorBoardProfile& profile(EmulatorBoard board) {
  return profiles[static_cast<unsigned>(board)];
}

bool waitForSocket(int socket, short events, int timeoutMs) {
  pollfd descriptor{socket, events, 0};
  return poll(&descriptor, 1, timeoutMs) > 0 &&
         (descriptor.revents & events) != 0;
}

std::string responseFixtureName(const std::string& host,
                                const std::string& request) {
  const size_t firstSpace = request.find(' ');
  const size_t secondSpace = firstSpace == std::string::npos
      ? std::string::npos : request.find(' ', firstSpace + 1);
  std::string path = firstSpace == std::string::npos || secondSpace == std::string::npos
      ? "/" : request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
  path = path.substr(0, path.find('?'));
  if (host == "opendata.adsb.fi") {
    const unsigned candidate = radarFixtureIndex + 1;
    const std::string candidateName =
        "radar-" + std::to_string(candidate) + ".json";
    if (std::filesystem::exists(
            std::filesystem::path(emulatorResponseDirectory()) / candidateName)) {
      radarFixtureIndex = candidate;
      return candidateName;
    }
    return radarFixtureIndex
        ? "radar-" + std::to_string(radarFixtureIndex) + ".json"
        : "radar.http";
  }
  if (host == "api.openweathermap.org")
    return path.find("/forecast") != std::string::npos
        ? "weather-forecast.http" : "weather-current.http";
  if (host == "api.github.com") {
    if (path != "/graphql") return "release.http";
    // The GitHub screen posts two different documents to one endpoint. Pick the
    // fixture from the query body so each polling phase can be exercised.
    const size_t body = request.find("\r\n\r\n");
    const std::string document = body == std::string::npos
        ? std::string() : request.substr(body + 4);
    if (document.find("contributionCalendar") != std::string::npos)
      return "github-calendar.http";
    if (!document.empty()) return "github-lists.http";
    return "github.http";
  }

  std::string name = host + path;
  for (char& value : name)
    if (!std::isalnum(static_cast<unsigned char>(value))) value = '_';
  return name + ".http";
}
}

EmulatorWiFi WiFi;
EmulatorUpdate Update;

void emulatorConfigure(EmulatorBoard board, EmulatorNetwork network,
                       int rssi, int ldr, const std::string& stateDirectory,
                       uint16_t webPort, const std::string& responseDirectory) {
  currentBoard = board;
  currentNetwork = network;
  currentRssi = constrain(rssi, -100, 0);
  currentLdr = constrain(ldr, 0, static_cast<int>(profile(board).adcMax));
  currentWebPort = webPort;
  currentStateDirectory = stateDirectory;
  currentResponseDirectory = responseDirectory;
  restartRequested = false;
  radarFixtureIndex = 0;
  std::filesystem::create_directories(currentStateDirectory);
}

const EmulatorBoardProfile& emulatorBoardProfile() { return profile(currentBoard); }
EmulatorNetwork emulatorNetworkMode() { return currentNetwork; }
uint16_t emulatorWebPort() { return currentWebPort; }
const std::string& emulatorStateDirectory() { return currentStateDirectory; }
const std::string& emulatorResponseDirectory() { return currentResponseDirectory; }
bool emulatorRestartRequested() { return restartRequested; }
void emulatorRequestRestart() { restartRequested = true; }
void emulatorSetHostname(const char* value) {
  currentHostname = value && value[0] ? value : "deskmate-emulator";
}
void emulatorTimeBegin(const char* tz, const char*, const char*) {
  const char* zone = tz && tz[0] ? tz : "UTC0";
  setenv("TZ", zone, 1);
  tzset();
  if (timeSyncCallback) timeSyncCallback();
}
bool emulatorScanIsOpen(int) { return true; }
String emulatorUpdateError() { return String(updateError); }
uint32_t emulatorChipId() { return emulatorBoardProfile().chipId; }
uint32_t emulatorCpuFreqMhz() { return emulatorBoardProfile().cpuMhz; }
EmulatorResetInfoData emulatorResetInfo() {
  EmulatorResetInfoData result;
  const std::filesystem::path path =
      std::filesystem::path(currentStateDirectory) / "reset.reason";
  std::ifstream input(path);
  std::string reason;
  if (input && std::getline(input, reason) && !reason.empty()) result.reason = reason;
  else result.reason = "Emulator power on";
  std::ofstream(path, std::ios::trunc) << "Software restart\n";
  return result;
}
uint32_t emulatorMaxFreeBlock() { return emulatorBoardProfile().maximumBlockBytes; }
uint32_t emulatorFreeContStack() {
  return currentBoard == EmulatorBoard::Esp8266 ? 4096 : 0;
}
bool emulatorTlsMemoryReady() {
  if (currentNetwork == EmulatorNetwork::Offline) return false;
  if (currentBoard != EmulatorBoard::Esp8266) return true;
  return emulatorBoardProfile().heapBytes >=
      PLATFORM_TLS_RX_BYTES + PLATFORM_TLS_TX_BYTES +
      PLATFORM_TLS_HEAP_OVERHEAD_BYTES;
}
int emulatorLdrValue() { return emulatorBoardProfile().hasLdr ? currentLdr : 0; }
const char* emulatorUpdateAsset() { return emulatorBoardProfile().updateAsset; }

uint32_t emulatorFsTotalBytes() {
  return currentBoard == EmulatorBoard::Esp8266 ? 1024UL * 1024UL : 0xF0000UL;
}

namespace {
std::vector<std::filesystem::path> emulatorFsFiles() {
  std::vector<std::filesystem::path> files;
  const auto root = std::filesystem::path(emulatorStateDirectory()) / "littlefs";
  std::error_code error;
  if (!std::filesystem::exists(root, error)) return files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error)) {
    if (!error && entry.is_regular_file()) files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  return files;
}
}

uint32_t emulatorFsUsedBytes() {
  uint64_t used = 0;
  for (const auto& file : emulatorFsFiles()) {
    std::error_code error;
    const uintmax_t size = std::filesystem::file_size(file, error);
    if (!error) used += size;
  }
  return static_cast<uint32_t>(std::min<uint64_t>(used, UINT32_MAX));
}

size_t emulatorFsFileCount() { return emulatorFsFiles().size(); }

bool emulatorFsFileAt(size_t index, String& path, size_t& size) {
  const auto files = emulatorFsFiles();
  if (index >= files.size()) return false;
  const auto root = std::filesystem::path(emulatorStateDirectory()) / "littlefs";
  std::error_code error;
  const auto relative = std::filesystem::relative(files[index], root, error);
  if (error) return false;
  path = String("/") + relative.generic_string();
  size = std::filesystem::file_size(files[index], error);
  return !error;
}

void platformOnTimeSync(void (*callback)()) {
  timeSyncCallback = callback;
  if (callback) callback();
}

IPAddress::IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  char buffer[20];
  std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", a, b, c, d);
  value_ = buffer;
}

int EmulatorWiFi::status() const {
  return emulatorNetworkMode() == EmulatorNetwork::Sta ? WL_CONNECTED
                                                        : WL_DISCONNECTED;
}
IPAddress EmulatorWiFi::localIP() const {
  return emulatorNetworkMode() == EmulatorNetwork::Sta
      ? IPAddress("127.0.0.1") : IPAddress("0.0.0.0");
}
String EmulatorWiFi::SSID() const {
  return emulatorNetworkMode() == EmulatorNetwork::Ap
      ? String("DeskMate-Setup") : String("Host network");
}
int EmulatorWiFi::RSSI() const {
  return emulatorNetworkMode() == EmulatorNetwork::Sta ? currentRssi : 0;
}
int EmulatorWiFi::hostByName(const char* host, IPAddress& result, uint32_t) const {
  if (emulatorNetworkMode() != EmulatorNetwork::Sta || !host || !host[0]) return 0;
  addrinfo hints{};
  hints.ai_family = AF_INET;
  addrinfo* addresses = nullptr;
  if (getaddrinfo(host, nullptr, &hints, &addresses) != 0 || !addresses) return 0;
  char text[INET_ADDRSTRLEN] = "";
  const auto* address = reinterpret_cast<sockaddr_in*>(addresses->ai_addr);
  inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text));
  result = IPAddress(text);
  freeaddrinfo(addresses);
  return 1;
}

struct EmulatorClient::Impl {
  explicit Impl(bool secureValue) : secure(secureValue) {}
  bool secure = false;
  int socket = -1;
  SSL_CTX* context = nullptr;
  SSL* ssl = nullptr;
  int peeked = -1;
  unsigned long timeoutMs = 10000;
  bool fixture = false;
  bool responseLoaded = false;
  std::string host;
  std::string request;
  std::string response;
  size_t responsePosition = 0;

  // The fixture is chosen the first time the caller reads, by which point the
  // whole request -- headers and body -- has been written.
  void ensureResponse() {
    if (responseLoaded || request.empty()) return;
    responseLoaded = true;
    const std::filesystem::path path =
        std::filesystem::path(emulatorResponseDirectory()) /
        responseFixtureName(host, request);
    std::ifstream input(path, std::ios::binary);
    response.assign(std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>());
    if (path.extension() == ".json" && !response.empty()) {
      const std::string body = response;
      response = "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n"
          "Content-Length: " + std::to_string(body.size()) +
          "\r\nConnection: close\r\n\r\n" + body;
    }
  }
};

EmulatorClient::EmulatorClient(bool secure) : impl_(new Impl(secure)) {}
EmulatorClient::~EmulatorClient() { stop(); }

int EmulatorClient::connect(const char* host, uint16_t port) {
  stop();
  if (emulatorNetworkMode() != EmulatorNetwork::Sta || !host || !host[0]) return 0;

  if (!emulatorResponseDirectory().empty()) {
    impl_->fixture = true;
    impl_->host = host;
    return 1;
  }

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* addresses = nullptr;
  char service[8];
  std::snprintf(service, sizeof(service), "%u", port);
  if (getaddrinfo(host, service, &hints, &addresses) != 0) return 0;

  for (addrinfo* address = addresses; address; address = address->ai_next) {
    const int socketFd = socket(address->ai_family, address->ai_socktype,
                                address->ai_protocol);
    if (socketFd < 0) continue;
    timeval timeout{static_cast<time_t>(impl_->timeoutMs / 1000),
                    static_cast<suseconds_t>((impl_->timeoutMs % 1000) * 1000)};
    setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (::connect(socketFd, address->ai_addr, address->ai_addrlen) == 0) {
      impl_->socket = socketFd;
      break;
    }
    close(socketFd);
  }
  freeaddrinfo(addresses);
  if (impl_->socket < 0) return 0;

  if (impl_->secure) {
    impl_->context = SSL_CTX_new(TLS_client_method());
    if (!impl_->context) { stop(); return 0; }
    SSL_CTX_set_verify(impl_->context, SSL_VERIFY_NONE, nullptr);
    impl_->ssl = SSL_new(impl_->context);
    if (!impl_->ssl) { stop(); return 0; }
    SSL_set_tlsext_host_name(impl_->ssl, host);
    SSL_set_fd(impl_->ssl, impl_->socket);
    if (SSL_connect(impl_->ssl) != 1) { stop(); return 0; }
  }
  return 1;
}

int EmulatorClient::available() {
  if (impl_->fixture) impl_->ensureResponse();
  if (!connected()) return 0;
  if (impl_->peeked >= 0) return 1;
  if (impl_->fixture)
    return static_cast<int>(impl_->response.size() - impl_->responsePosition);
  if (impl_->secure && SSL_pending(impl_->ssl) > 0) return SSL_pending(impl_->ssl);
  return waitForSocket(impl_->socket, POLLIN, 0) ? 1 : 0;
}

int EmulatorClient::read() {
  if (impl_->peeked >= 0) {
    const int value = impl_->peeked;
    impl_->peeked = -1;
    return value;
  }
  if (impl_->fixture) impl_->ensureResponse();
  if (!connected()) return -1;
  if (impl_->fixture) {
    if (impl_->responsePosition >= impl_->response.size()) return -1;
    return static_cast<unsigned char>(impl_->response[impl_->responsePosition++]);
  }
  unsigned char byte = 0;
  const int count = impl_->secure
      ? SSL_read(impl_->ssl, &byte, 1)
      : static_cast<int>(recv(impl_->socket, &byte, 1, 0));
  if (count == 1) return byte;
  if (count == 0) stop();
  return -1;
}

int EmulatorClient::peek() {
  if (impl_->peeked < 0) impl_->peeked = read();
  return impl_->peeked;
}

size_t EmulatorClient::write(uint8_t value) { return write(&value, 1); }
size_t EmulatorClient::write(const uint8_t* buffer, size_t size) {
  if (!connected() || !buffer) return 0;
  if (impl_->fixture) {
    impl_->request.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }
  size_t written = 0;
  while (written < size) {
    const int count = impl_->secure
        ? SSL_write(impl_->ssl, buffer + written, static_cast<int>(size - written))
        : static_cast<int>(send(impl_->socket, buffer + written, size - written, MSG_NOSIGNAL));
    if (count <= 0) break;
    written += static_cast<size_t>(count);
  }
  return written;
}

bool EmulatorClient::connected() {
  if (impl_->fixture)
    return !impl_->responseLoaded || impl_->responsePosition < impl_->response.size();

  if (impl_->socket < 0) return false;
  pollfd descriptor{impl_->socket, static_cast<short>(POLLIN | POLLHUP | POLLERR), 0};
  if (poll(&descriptor, 1, 0) <= 0) return true;
  return (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)) == 0 ||
         (descriptor.revents & POLLIN) != 0;
}

void EmulatorClient::stop() {
  if (impl_->ssl) { SSL_shutdown(impl_->ssl); SSL_free(impl_->ssl); impl_->ssl = nullptr; }
  if (impl_->context) { SSL_CTX_free(impl_->context); impl_->context = nullptr; }
  if (impl_->socket >= 0) { close(impl_->socket); impl_->socket = -1; }
  impl_->peeked = -1;
  impl_->fixture = false;
  impl_->responseLoaded = false;
  impl_->host.clear();
  impl_->request.clear();
  impl_->response.clear();
  impl_->responsePosition = 0;
}

void EmulatorClient::setTimeout(unsigned long timeout) {
  Stream::setTimeout(timeout);
  impl_->timeoutMs = timeout;
}

bool EmulatorUpdate::begin(uint32_t maximumSize) {
  maximumSize_ = maximumSize;
  written_ = 0;
  firstByte_ = 0;
  hasFirstByte_ = false;
  error_.clear();
  temporaryPath_ = (std::filesystem::path(emulatorStateDirectory()) /
                    "virtual-flash.pending").string();
  std::ofstream output(temporaryPath_, std::ios::binary | std::ios::trunc);
  if (!output) error_ = "could not open virtual flash";
  updateError = error_;
  return error_.empty();
}

size_t EmulatorUpdate::write(const uint8_t* data, size_t size) {
  if (!error_.empty() || !data) return 0;
  if (written_ + size > maximumSize_) {
    error_ = "image exceeds virtual OTA slot";
    updateError = error_;
    return 0;
  }
  if (!hasFirstByte_ && size > 0) {
    firstByte_ = data[0];
    hasFirstByte_ = true;
  }
  std::ofstream output(temporaryPath_, std::ios::binary | std::ios::app);
  output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
  if (!output) {
    error_ = "virtual flash write failed";
    updateError = error_;
    return 0;
  }
  written_ += size;
  return size;
}

bool EmulatorUpdate::end(bool evenIfRemaining) {
  if (error_.empty() && written_ == 0) error_ = "empty firmware image";
  if (error_.empty() && (!hasFirstByte_ || firstByte_ != 0xE9))
    error_ = "invalid ESP firmware image";
  if (error_.empty() && !evenIfRemaining && written_ != maximumSize_)
    error_ = "incomplete virtual firmware image";
  if (!error_.empty()) { updateError = error_; return false; }
  const std::filesystem::path installed =
      std::filesystem::path(emulatorStateDirectory()) / "virtual-flash.bin";
  std::error_code errorCode;
  std::filesystem::rename(temporaryPath_, installed, errorCode);
  if (errorCode) {
    std::filesystem::copy_file(temporaryPath_, installed,
                               std::filesystem::copy_options::overwrite_existing,
                               errorCode);
  }
  if (errorCode) error_ = "could not commit virtual flash";
  updateError = error_;
  return error_.empty();
}

void EmulatorUpdate::printError(HardwareSerial& serial) const {
  if (!error_.empty()) serial.println(error_.c_str());
}
