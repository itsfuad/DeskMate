#pragma once

#include "Arduino.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST };
enum UploadStatus {
  UPLOAD_FILE_START,
  UPLOAD_FILE_WRITE,
  UPLOAD_FILE_END,
  UPLOAD_FILE_ABORTED,
};

struct HTTPUpload {
  UploadStatus status = UPLOAD_FILE_START;
  String filename;
  uint8_t* buf = nullptr;
  size_t currentSize = 0;
  size_t totalSize = 0;
};

class EmulatorWebServer {
 public:
  explicit EmulatorWebServer(uint16_t) {}
  ~EmulatorWebServer();

  void on(const char* path, HTTPMethod method, std::function<void()> handler);
  void on(const char* path, std::function<void()> handler);
  void on(const char* path, HTTPMethod method, std::function<void()> handler,
          std::function<void()> uploadHandler);
  void onNotFound(std::function<void()> handler) { notFound_ = std::move(handler); }
  void begin();
  void handleClient();

  bool hasArg(const char* name) const;
  String arg(const char* name) const;
  HTTPUpload& upload() { return upload_; }

  void sendHeader(const char* name, const String& value, bool first = false);
  void sendHeader(const char* name, const char* value, bool first = false) {
    sendHeader(name, String(value), first);
  }
  void send(int code, const char* contentType, const String& content);
  void send(int code, const char* contentType, const char* content) {
    send(code, contentType, String(content));
  }
  void setContentLength(size_t length) {
    responseLength_ = length;
    responseLengthSet_ = true;
  }
  void sendContent(const char* content, size_t length);
  void sendContent(const String& content) {
    sendContent(content.c_str(), content.length());
  }
  void send_P(int code, const char* contentType, const char* content) {
    send(code, contentType, content);
  }

  template <typename FileLike>
  size_t streamFile(FileLike& file, const char* contentType) {
    const String content = file.readString();
    send(200, contentType, content);
    return content.length();
  }

 private:
  struct Route {
    std::string path;
    HTTPMethod method = HTTP_ANY;
    std::function<void()> handler;
    std::function<void()> uploadHandler;
  };

  void handleConnection(int client);
  const Route* findRoute(const std::string& path, HTTPMethod method) const;
  void writeResponse();

  int socket_ = -1;
  int activeClient_ = -1;
  std::vector<Route> routes_;
  std::function<void()> notFound_;
  std::map<std::string, String> args_;
  std::vector<std::pair<std::string, String>> responseHeaders_;
  HTTPUpload upload_;
  int responseCode_ = 200;
  std::string responseType_ = "text/plain";
  std::string responseBody_;
  size_t responseLength_ = 0;
  bool responseLengthSet_ = false;
  bool responseReady_ = false;
};
