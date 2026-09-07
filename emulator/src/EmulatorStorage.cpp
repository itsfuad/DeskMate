#include "LittleFS.h"
#include "EmulatorPlatform.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>

struct EmulatorFileData {
  std::FILE* file = nullptr;
  bool writable = false;

  ~EmulatorFileData() {
    if (file) std::fclose(file);
  }
};

EmulatorLittleFS LittleFS;

namespace {
std::filesystem::path resolvePath(const char* path) {
  std::string relative = path ? path : "";
  while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
  return std::filesystem::path(emulatorStateDirectory()) / "littlefs" / relative;
}
}

File::~File() = default;
File::operator bool() const { return data_ && data_->file; }
int File::available() {
  if (!*this || data_->writable) return 0;
  const long position = std::ftell(data_->file);
  const size_t length = size();
  return position >= 0 && static_cast<size_t>(position) < length
      ? static_cast<int>(std::min<size_t>(length - position, INT_MAX)) : 0;
}
int File::read() {
  return *this && !data_->writable ? std::fgetc(data_->file) : -1;
}
int File::peek() {
  const int value = read();
  if (value != EOF) std::ungetc(value, data_->file);
  return value;
}
size_t File::write(uint8_t value) { return write(&value, 1); }
size_t File::write(const uint8_t* buffer, size_t size) {
  if (!*this || !data_->writable || !buffer) return 0;
  const size_t total = emulatorFsTotalBytes();
  const size_t used = emulatorFsUsedBytes();
  size = std::min(size, used < total ? total - used : 0);
  return size ? std::fwrite(buffer, 1, size, data_->file) : 0;
}
String File::readString() {
  std::string value;
  int next;
  while ((next = read()) != EOF) value.push_back(static_cast<char>(next));
  return String(value);
}
size_t File::size() const {
  struct stat status {};
  return *this && ::fstat(::fileno(data_->file), &status) == 0
      ? static_cast<size_t>(status.st_size) : 0;
}
void File::close() {
  if (!*this) return;
  std::fclose(data_->file);
  data_->file = nullptr;
}

bool EmulatorLittleFS::begin() {
  std::error_code error;
  std::filesystem::create_directories(
      std::filesystem::path(emulatorStateDirectory()) / "littlefs", error);
  return !error;
}
bool EmulatorLittleFS::format() {
  std::error_code error;
  const auto root = std::filesystem::path(emulatorStateDirectory()) / "littlefs";
  std::filesystem::remove_all(root, error);
  if (error) return false;
  std::filesystem::create_directories(root, error);
  return !error;
}
bool EmulatorLittleFS::exists(const char* path) const {
  std::error_code error;
  return std::filesystem::exists(resolvePath(path), error) && !error;
}
bool EmulatorLittleFS::remove(const char* path) const {
  std::error_code error;
  return std::filesystem::remove(resolvePath(path), error) && !error;
}
bool EmulatorLittleFS::rename(const char* from, const char* to) const {
  std::error_code error;
  std::filesystem::rename(resolvePath(from), resolvePath(to), error);
  return !error;
}
File EmulatorLittleFS::open(const char* path, const char* mode) const {
  if (!path || !mode) return File();
  const bool append = std::strcmp(mode, "a") == 0;
  const bool writable = append || std::strcmp(mode, "w") == 0;
  if (!writable && std::strcmp(mode, "r") != 0) return File();
  const auto resolved = resolvePath(path);
  if (writable) {
    std::error_code error;
    std::filesystem::create_directories(resolved.parent_path(), error);
    if (error) return File();
  }
  auto data = std::make_shared<EmulatorFileData>();
  data->file = std::fopen(resolved.c_str(), writable ? (append ? "ab" : "wb") : "rb");
  if (!data->file) return File();
  // Keep disk usage current so simultaneous open writers share the same budget.
  std::setvbuf(data->file, nullptr, _IONBF, 0);
  data->writable = writable;
  return File(data);
}
