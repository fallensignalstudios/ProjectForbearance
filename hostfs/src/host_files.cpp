#include "expansion/host_files.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace expansion::host {

std::vector<ContentFile> DirectoryContentSource::read_all() const {
  namespace fs = std::filesystem;
  if (!fs::exists(root_)) throw SimError("content directory not found: " + root_);
  std::vector<ContentFile> out;
  for (const auto& entry : fs::recursive_directory_iterator(root_)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".json") continue;
    out.push_back({fs::relative(entry.path(), root_).generic_string(), read_file(entry.path().string())});
  }
  return out;
}

Catalog load_catalog_from_directory(const std::string& content_dir) {
  return Catalog::load(DirectoryContentSource(content_dir));
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw SimError("cannot open file for reading: " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  if (in.bad()) throw SimError("read error on file: " + path);
  return ss.str();
}

void write_file_atomic(const std::string& path, const std::string& bytes) {
  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) throw SimError("cannot open temporary file for writing: " + tmp);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) throw SimError("write error on temporary file: " + tmp);
  }
#if !defined(_WIN32)
  int fd = ::open(tmp.c_str(), O_RDONLY);
  if (fd >= 0) {
    ::fsync(fd);
    ::close(fd);
  }
#endif
  std::remove(path.c_str());
  if (std::rename(tmp.c_str(), path.c_str()) != 0) throw SimError("cannot replace file: " + path);
}

void write_save_slot(const std::string& path, const std::string& bytes) {
  namespace fs = std::filesystem;
  if (fs::exists(path)) {
    std::error_code ec;
    fs::copy_file(path, path + ".bak", fs::copy_options::overwrite_existing, ec);
    if (ec) throw SimError("save: cannot write the backup slot: " + ec.message());
  }
  write_file_atomic(path, bytes);
}

}  // namespace expansion::host
