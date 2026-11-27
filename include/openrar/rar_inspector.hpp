#pragma once

#include "openrar/types.hpp"

#include <string>
#include <vector>

namespace openrar {

/// Native zero-dependency binary header parser for RAR4 and RAR5 archives.
/// Quickly extracts file listings, packed/unpacked sizes, CRC32 checksums, solid/recovery flags,
/// and encryption metadata directly from `.rar` container headers.
class RarInspector {
public:
    /// Inspects a RAR4 or RAR5 file and populates `entries` and optional `info`.
    static bool list(const fs::path& archive_path, std::vector<EntryInfo>& entries,
                     ArchiveInfo* info = nullptr, std::string* err = nullptr);
};

} // namespace openrar
