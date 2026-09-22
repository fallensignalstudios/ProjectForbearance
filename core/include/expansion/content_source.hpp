// Where the catalog's definition bytes come from.
//
// The core never touches a filesystem. A packaged Unreal build serves content
// from a pak file, a test serves it from memory, and the command-line host
// serves it from a directory; all three satisfy this one interface, so the
// catalog loader is identical in each.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace expansion {

// One definition file: a path relative to the content root, and its bytes.
struct ContentFile {
  std::string path;   // e.g. "facilities/facilities.json", always with '/'
  std::string bytes;
};

class ContentSource {
 public:
  virtual ~ContentSource() = default;
  // Every definition file the catalog should load. Order does not matter: the
  // loader sorts bytewise by path before hashing, so the catalog hash cannot
  // depend on how a host enumerates its files.
  virtual std::vector<ContentFile> read_all() const = 0;
  // A short description of where the content came from, for error messages.
  virtual std::string describe() const = 0;
};

// Content held in memory. Used by tests and by any host that has already
// unpacked its definitions.
class MemoryContentSource : public ContentSource {
 public:
  MemoryContentSource() = default;
  explicit MemoryContentSource(std::vector<ContentFile> files, std::string description = "memory")
      : files_(std::move(files)), description_(std::move(description)) {}

  void add(std::string path, std::string bytes) { files_.push_back({std::move(path), std::move(bytes)}); }
  std::vector<ContentFile> read_all() const override { return files_; }
  std::string describe() const override { return description_; }

 private:
  std::vector<ContentFile> files_;
  std::string description_ = "memory";
};

}  // namespace expansion
