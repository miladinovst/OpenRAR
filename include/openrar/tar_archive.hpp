#pragma once

#include "openrar/types.hpp"

#include <string>
#include <vector>

namespace openrar {

/// POSIX USTAR + GNU LongName handler for `.tar` and GZip-compressed `.tar.gz` / `.tgz` archives.
/// 100% interoperable with Linux `tar` and `gzip`.
class TarArchive {
public:
    /// Lists all file/directory entries inside a `.tar` or `.tar.gz` archive.
    static bool list(const fs::path& archive_path, bool gzipped,
                     std::vector<EntryInfo>& entries,
                     ArchiveInfo* info = nullptr, std::string* err = nullptr);

    /// Creates a new `.tar` (or `.tar.gz` when `gzipped == true`) archive from `inputs`.
    static OperationResult create(const fs::path& archive_path, bool gzipped,
                                  const std::vector<fs::path>& inputs,
                                  const CreateOptions& options);

    /// Extracts all or selected entries to `options.destination` with path-safety validation.
    static OperationResult extract(const fs::path& archive_path, bool gzipped,
                                   const ExtractOptions& options);

    /// Extracts a single entry into memory for quick preview or content search.
    static bool extract_entry_to_memory(const fs::path& archive_path, bool gzipped,
                                        const std::string& entry_path,
                                        std::vector<uint8_t>& out,
                                        std::string* err = nullptr);

    /// Validates GZip checksums (if gzipped) and every 512-byte TAR header checksum.
    static OperationResult test(const fs::path& archive_path, bool gzipped,
                                ProgressCallback progress = nullptr);
};

} // namespace openrar
