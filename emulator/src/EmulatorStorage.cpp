#include "LittleFS.h"
#include "EmulatorPlatform.h"

#include <filesystem>
#include <fstream>
#include <vector>

struct EmulatorFileData {
  std::filesystem::path path;
  std::vector<uint8_t> bytes;
  size_t position = 0;
  bool writable = false;
  bool closed = false;
};

EmulatorLittleFS LittleFS;

namespace {
std::filesystem::path resolvePath(const char* path) {
  std::string relative = path ? path : "";
  while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
  return std::filesystem::path(emulatorStateDirectory()) / "littlefs" / relative;
}

void flushFile(const std::shared_ptr<EmulatorFileData>& data) {
  if (!data || !data->writable || data->closed) return;
  std::filesystem::create_directories(data->path.parent_path());
  std::ofstream output(data->path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(data->bytes.data()),
               static_cast<std::streamsize>(data->bytes.size()));
  data->closed = true;
}
}

File::~File() {
  if (data_ && data_.use_count() == 1) flushFile(data_);
}
int File::available() {
  return data_ && data_->position < data_->bytes.size()
      ? static_cast<int>(data_->bytes.size() - data_->position) : 0;
}
int File::read() {
  if (!available()) return -1;
  return data_->bytes[data_->position++];
}
int File::peek() {
  if (!available()) return -1;
  return data_->bytes[data_->position];
}
size_t File::write(uint8_t value) { return write(&value, 1); }
size_t File::write(const uint8_t* buffer, size_t size) {
  if (!data_ || !data_->writable || !buffer) return 0;
  data_->bytes.insert(data_->bytes.end(), buffer, buffer + size);
  data_->position = data_->bytes.size();
  return size;
}
String File::readString() {
  if (!data_) return String();
  std::string value(data_->bytes.begin() + static_cast<ptrdiff_t>(data_->position),
                    data_->bytes.end());
  data_->position = data_->bytes.size();
  return String(value);
}
size_t File::size() const { return data_ ? data_->bytes.size() : 0; }
void File::close() { flushFile(data_); }

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
  return std::filesystem::exists(resolvePath(path));
}
bool EmulatorLittleFS::remove(const char* path) const {
  std::error_code error;
  return std::filesystem::remove(resolvePath(path), error) && !error;
}
File EmulatorLittleFS::open(const char* path, const char* mode) const {
  const bool writable = mode && (std::strchr(mode, 'w') || std::strchr(mode, 'a'));
  const bool append = mode && std::strchr(mode, 'a');
  const auto resolved = resolvePath(path);
  auto data = std::make_shared<EmulatorFileData>();
  data->path = resolved;
  data->writable = writable;

  if ((!writable || append) && std::filesystem::exists(resolved)) {
    std::ifstream input(resolved, std::ios::binary);
    data->bytes.assign(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
  } else if (!writable) {
    return File();
  }
  data->position = append ? data->bytes.size() : 0;
  return File(data);
}
