#pragma once

#include "Stream.h"

#include <memory>

class File : public Stream {
 public:
  File() = default;
  explicit File(std::shared_ptr<struct EmulatorFileData> data) : data_(std::move(data)) {}
  ~File() override;

  explicit operator bool() const { return static_cast<bool>(data_); }
  int available() override;
  int read() override;
  int peek() override;
  size_t write(uint8_t value) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  String readString();
  size_t size() const;
  void close();

 private:
  std::shared_ptr<struct EmulatorFileData> data_;
};

class EmulatorLittleFS {
 public:
  bool begin();
  bool format();
  bool exists(const char* path) const;
  bool remove(const char* path) const;
  File open(const char* path, const char* mode) const;
};

extern EmulatorLittleFS LittleFS;
