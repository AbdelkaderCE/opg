#pragma once

#include <filesystem>
#include <string>

namespace opg {

// Lowercase hex SHA-256 of a file's contents. Throws std::runtime_error.
std::string sha256File(const std::filesystem::path& file);

// Lowercase hex SHA-256 of an in-memory string.
std::string sha256String(const std::string& data);

}  // namespace opg
