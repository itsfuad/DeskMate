#include "EmulatorWebServer.h"
#include "EmulatorPlatform.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace {
std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

const char* reasonFor(int code) {
  switch (code) {
    case 200: return "OK";
    case 302: return "Found";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 422: return "Unprocessable Entity";
    case 500: return "Internal Server Error";
    default: return "Response";
  }
}

bool sendAll(int socket, const std::string& value) {
  size_t sent = 0;
  while (sent < value.size()) {
    const ssize_t count = send(socket, value.data() + sent, value.size() - sent,
                               MSG_NOSIGNAL);
    if (count <= 0) return false;
    sent += static_cast<size_t>(count);
  }
  return true;
}
}

EmulatorWebServer::~EmulatorWebServer() {
  if (socket_ >= 0) close(socket_);
}

void EmulatorWebServer::on(const char* path, HTTPMethod method,
                           std::function<void()> handler) {
  routes_.push_back({path ? path : "/", method, std::move(handler), {}});
}
void EmulatorWebServer::on(const char* path, std::function<void()> handler) {
  on(path, HTTP_ANY, std::move(handler));
}
void EmulatorWebServer::on(const char* path, HTTPMethod method,
                           std::function<void()> handler,
                           std::function<void()> uploadHandler) {
  routes_.push_back({path ? path : "/", method, std::move(handler),
                     std::move(uploadHandler)});
}

void EmulatorWebServer::begin() {
  socket_ = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_ < 0) return;
  int yes = 1;
  setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(emulatorWebPort());
  if (bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      listen(socket_, 8) != 0) {
    close(socket_);
    socket_ = -1;
    return;
  }
  fcntl(socket_, F_SETFL, fcntl(socket_, F_GETFL, 0) | O_NONBLOCK);
}

void EmulatorWebServer::handleClient() {
  if (socket_ < 0) return;
  const int client = accept(socket_, nullptr, nullptr);
  if (client < 0) return;
  timeval timeout{2, 0};
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  handleConnection(client);
  close(client);
}

const EmulatorWebServer::Route* EmulatorWebServer::findRoute(
    const std::string& path, HTTPMethod method) const {
  for (const Route& route : routes_) {
    if (route.path == path && (route.method == HTTP_ANY || route.method == method))
      return &route;
  }
  return nullptr;
}

void EmulatorWebServer::handleConnection(int client) {
  std::string request;
  char buffer[4096];
  size_t headerEnd = std::string::npos;
  size_t contentLength = 0;
  while (request.size() < 2 * 1024 * 1024) {
    const ssize_t count = recv(client, buffer, sizeof(buffer), 0);
    if (count <= 0) break;
    request.append(buffer, static_cast<size_t>(count));
    if (headerEnd == std::string::npos) {
      headerEnd = request.find("\r\n\r\n");
      if (headerEnd != std::string::npos) {
        const std::string headerText = lower(request.substr(0, headerEnd));
        const size_t marker = headerText.find("content-length:");
        if (marker != std::string::npos)
          contentLength = std::strtoul(headerText.c_str() + marker + 15, nullptr, 10);
      }
    }
    if (headerEnd != std::string::npos &&
        request.size() >= headerEnd + 4 + contentLength) break;
  }
  if (headerEnd == std::string::npos) return;

  std::istringstream firstLine(request.substr(0, request.find("\r\n")));
  std::string methodText;
  std::string target;
  firstLine >> methodText >> target;
  const HTTPMethod method = methodText == "POST" ? HTTP_POST : HTTP_GET;
  const size_t queryAt = target.find('?');
  const std::string path = target.substr(0, queryAt);
  const std::string headers = request.substr(0, headerEnd);
  const std::string loweredHeaders = lower(headers);
  const std::string body = request.substr(headerEnd + 4, contentLength);

  args_.clear();
  responseHeaders_.clear();
  responseReady_ = false;
  responseBody_.clear();
  activeClient_ = client;
  const Route* route = findRoute(path, method);

  if (route && route->uploadHandler &&
      loweredHeaders.find("multipart/form-data") != std::string::npos) {
    const size_t boundaryAt = loweredHeaders.find("boundary=");
    const std::string boundary = boundaryAt == std::string::npos
        ? std::string() : headers.substr(boundaryAt + 9,
            headers.find("\r\n", boundaryAt) - boundaryAt - 9);
    const size_t filenameAt = body.find("filename=\"");
    const size_t filenameStart = filenameAt == std::string::npos
        ? std::string::npos : filenameAt + 10;
    const size_t filenameEnd = filenameStart == std::string::npos
        ? std::string::npos : body.find('"', filenameStart);
    const size_t dataStartMarker = body.find("\r\n\r\n", filenameEnd);
    const size_t dataStart = dataStartMarker == std::string::npos
        ? std::string::npos : dataStartMarker + 4;
    const std::string terminator = "\r\n--" + boundary;
    const size_t dataEnd = dataStart == std::string::npos
        ? std::string::npos : body.find(terminator, dataStart);

    if (dataStart != std::string::npos && dataEnd != std::string::npos) {
      upload_.filename = filenameEnd == std::string::npos
          ? String("firmware.bin")
          : String(body.substr(filenameStart, filenameEnd - filenameStart));
      upload_.totalSize = dataEnd - dataStart;
      args_["size"] = String(static_cast<unsigned long>(upload_.totalSize));
      upload_.status = UPLOAD_FILE_START;
      upload_.buf = nullptr;
      upload_.currentSize = 0;
      route->uploadHandler();
      for (size_t offset = dataStart; offset < dataEnd; offset += 4096) {
        upload_.status = UPLOAD_FILE_WRITE;
        upload_.currentSize = std::min<size_t>(4096, dataEnd - offset);
        upload_.buf = reinterpret_cast<uint8_t*>(
            const_cast<char*>(body.data() + offset));
        route->uploadHandler();
      }
      upload_.status = UPLOAD_FILE_END;
      upload_.currentSize = 0;
      upload_.buf = nullptr;
      route->uploadHandler();
    } else {
      upload_.status = UPLOAD_FILE_ABORTED;
      route->uploadHandler();
    }
  } else if (!body.empty()) {
    args_["plain"] = String(body);
  }

  if (route) route->handler();
  else if (notFound_) notFound_();
  else send(404, "text/plain", "Not found");
  if (!responseReady_) send(204, "text/plain", "");
  writeResponse();
  activeClient_ = -1;
}

bool EmulatorWebServer::hasArg(const char* name) const {
  return args_.find(name ? name : "") != args_.end();
}
String EmulatorWebServer::arg(const char* name) const {
  const auto found = args_.find(name ? name : "");
  return found == args_.end() ? String() : found->second;
}
void EmulatorWebServer::sendHeader(const char* name, const String& value, bool first) {
  const auto header = std::make_pair(std::string(name ? name : ""), value);
  if (first) responseHeaders_.insert(responseHeaders_.begin(), header);
  else responseHeaders_.push_back(header);
}
void EmulatorWebServer::send(int code, const char* contentType, const String& content) {
  responseCode_ = code;
  responseType_ = contentType ? contentType : "text/plain";
  responseBody_ = static_cast<std::string>(content);
  responseReady_ = true;
}
void EmulatorWebServer::writeResponse() {
  if (activeClient_ < 0) return;
  std::ostringstream output;
  output << "HTTP/1.1 " << responseCode_ << ' ' << reasonFor(responseCode_) << "\r\n"
         << "Content-Type: " << responseType_ << "\r\n"
         << "Content-Length: " << responseBody_.size() << "\r\n";
  for (const auto& header : responseHeaders_)
    output << header.first << ": " << header.second.c_str() << "\r\n";
  output << "Connection: close\r\n\r\n" << responseBody_;
  sendAll(activeClient_, output.str());
}
