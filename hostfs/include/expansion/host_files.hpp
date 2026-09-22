// Host-side file services.
//
// Deliberately separate from the core and from persistence. The core reads its
// content through a ContentSource and knows nothing about paths; persistence
// owns the save format but not the disk. Everything that actually opens a file
// lives here, so a packaged Unreal build can leave this module out entirely and
// serve content from its own pak files and saves through its own platform layer.
#pragma once

#include <string>

#include "expansion/catalog.hpp"
#include "expansion/content_source.hpp"

namespace expansion::host {

// Every .json file under a directory, recursively, keyed by a path relative to
// that directory with '/' separators on every platform.
class DirectoryContentSource : public ContentSource {
 public:
  explicit DirectoryContentSource(std::string root) : root_(std::move(root)) {}
  std::vector<ContentFile> read_all() const override;
  std::string describe() const override { return "directory '" + root_ + "'"; }

 private:
  std::string root_;
};

// Convenience for the command-line host and the tests.
Catalog load_catalog_from_directory(const std::string& content_dir);

std::string read_file(const std::string& path);

// Writes a temporary file in the target directory, flushes it, and replaces the
// target through a rename (TDD 16.2). Atomicity and durability still have to be
// tested on the real target operating system; neither is claimed here.
void write_file_atomic(const std::string& path, const std::string& bytes);

// Keeps the previous valid slot as `<path>.bak` before replacing it.
void write_save_slot(const std::string& path, const std::string& bytes);

}  // namespace expansion::host
