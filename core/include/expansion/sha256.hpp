// SHA-256 for the catalog hash and save payload checksum (TDD 16.1).
// A checksum detects accidental change; it is not authentication or anti-cheat
// (TDD 16.4).
#pragma once

#include <cstdint>
#include <string>

namespace expansion {

std::string sha256_hex(const std::string& bytes);

}  // namespace expansion
