// Save envelope, slot safety and compatibility checks (TDD 16).
//
// The checksum detects accidental change. It is not authentication and not
// anti-cheat, and nothing in a save is executed or used as a file path.
#pragma once

#include <string>
#include <vector>

#include "expansion/catalog.hpp"
#include "expansion/session.hpp"

namespace expansion {

inline constexpr const char* kSaveMagic = "SCEXP";
inline constexpr int kSaveFormatVersion = 1;

struct SaveHeader {
  std::string magic;
  int format_version = 0;
  std::string simulation_version;
  std::string catalog_hash;
  std::string scenario_id;
  Day day = 0;
  Revision revision = 0;
  std::string payload_sha256;
  std::size_t payload_length = 0;
  std::string build_id;
};

// EncodeSave(snapshot) -> Bytes | Errors. Encodes at a committed boundary only.
std::string encode_save(const SessionState& state, const Catalog& catalog, const std::string& build_id);

// DecodeSave(bytes, catalog) -> ValidatedState | Errors. Validates the candidate
// in isolation; the caller replaces the active session only on success.
SessionState decode_save(const std::string& bytes, const Catalog& catalog, SaveHeader* header_out = nullptr);

// Reads only the envelope, for listing slots without loading a campaign.
SaveHeader read_save_header(const std::string& bytes);

// Writes through a temporary file in the target directory and keeps the previous
// valid slot as `<path>.bak`. A newer snapshot is never overwritten by a late
// completion from an older write.
void write_save_slot(const std::string& path, const std::string& bytes);

// Rolling autosave slots plus one pre-major-decision checkpoint (TDD 16.2).
struct SaveSlots {
  std::string directory;
  int autosave_count = 3;
  std::string autosave_path(int index) const;
  std::string checkpoint_path() const;
  std::string manual_path(const std::string& name) const;
};

}  // namespace expansion
