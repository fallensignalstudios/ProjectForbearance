#include "expansion/save_file.hpp"

#include "expansion/json.hpp"
#include "expansion/sha256.hpp"
#include "expansion/state_codec.hpp"

namespace expansion {

std::string encode_save(const SessionState& state, const Catalog& catalog, const std::string& build_id) {
  json::Value payload = encode_state(state, catalog);
  const std::string canonical = json::serialize_canonical(payload);

  json::Value root = json::Value::object({});
  root.set("magic", json::Value::string(kSaveMagic));
  root.set("format_version", json::Value::integer(kSaveFormatVersion));
  root.set("simulation_version", json::Value::string(state.simulation_version));
  root.set("catalog_hash", json::Value::string(state.catalog_hash));
  root.set("scenario_id", json::Value::string(state.scenario_id));
  root.set("day", json::dec(state.day));
  root.set("revision", json::dec_u(state.revision));
  root.set("payload_length", json::Value::integer(static_cast<std::int64_t>(canonical.size())));
  root.set("payload_sha256", json::Value::string(sha256_hex(canonical)));
  root.set("build_id", json::Value::string(build_id));
  root.set("payload", payload);
  return json::serialize_canonical(root);
}

SaveHeader read_save_header(const std::string& bytes) {
  json::Value root = json::parse(bytes);
  const std::string ctx = "save envelope";
  SaveHeader h;
  h.magic = root.require_string("magic", ctx);
  if (h.magic != kSaveMagic) throw SimError(ErrorCode::CorruptSave, "save: this file is not a Sovereign Call: Expansion save");
  h.format_version = static_cast<int>(root.require_int("format_version", ctx));
  h.simulation_version = root.require_string("simulation_version", ctx);
  h.catalog_hash = root.require_string("catalog_hash", ctx);
  h.scenario_id = root.require_string("scenario_id", ctx);
  h.day = root.require_decimal("day", ctx);
  h.revision = root.require_decimal_u("revision", ctx);
  h.payload_length = static_cast<std::size_t>(root.require_int("payload_length", ctx));
  h.payload_sha256 = root.require_string("payload_sha256", ctx);
  h.build_id = root.string_or("build_id", "");
  return h;
}

SessionState decode_save(const std::string& bytes, const Catalog& catalog, SaveHeader* header_out) {
  json::Value root = json::parse(bytes);
  SaveHeader h = read_save_header(bytes);
  if (h.format_version != kSaveFormatVersion) {
    throw SimError(ErrorCode::IncompatibleSave,
                   "save: unsupported save format version " + std::to_string(h.format_version) +
                       "; this build reads version " + std::to_string(kSaveFormatVersion));
  }
  if (h.simulation_version != catalog.simulation_version()) {
    throw SimError(ErrorCode::IncompatibleSave, "save: written by simulation version " + h.simulation_version +
                                                    "; this build is " + catalog.simulation_version());
  }
  // An exact catalog match is required in P1, even when the difference looks
  // cosmetic (TDD 16.3).
  if (h.catalog_hash != catalog.hash()) {
    throw SimError(ErrorCode::IncompatibleSave,
                   "save: catalog mismatch. The save expects catalog " + h.catalog_hash +
                       "; this content set is " + catalog.hash() + ". No state was changed.");
  }
  const json::Value& payload = root.require("payload", "save envelope");
  const std::string canonical = json::serialize_canonical(payload);
  if (canonical.size() != h.payload_length) {
    throw SimError(ErrorCode::CorruptSave, "save: payload length does not match the envelope");
  }
  if (sha256_hex(canonical) != h.payload_sha256) {
    throw SimError(ErrorCode::CorruptSave,
                   "save: payload checksum does not match the envelope; the file has been changed or truncated");
  }
  SessionState state = decode_state(payload, catalog);
  if (state.day != h.day || state.revision != h.revision) {
    throw SimError("save: the envelope day and revision disagree with the payload");
  }
  if (header_out != nullptr) *header_out = h;
  return state;
}

std::string SaveSlots::autosave_path(int index) const {
  return directory + "/autosave_" + std::to_string(index) + ".scexp.json";
}
std::string SaveSlots::checkpoint_path() const { return directory + "/checkpoint.scexp.json"; }
std::string SaveSlots::manual_path(const std::string& name) const {
  return directory + "/manual_" + name + ".scexp.json";
}

}  // namespace expansion
