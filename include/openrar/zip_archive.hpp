#pragma once

#include "openrar/types.hpp"

#include <string>
#include <vector>

namespace openrar {

/// Full PKWARE ZIP & ZIP64 archive handler (100% interoperable with standard ZIP tools on Windows, macOS, and Linux).
/// Supports Dynamic/Fixed Huffman Deflate, Store, AES-256 & ZipCrypto encryption, UTF-8 filenames,
/// multi-threaded compression, archive comments, split volumes (.partN.zip), and header-scan repair.
class ZipArchive {
public:
    /// Parses the Central Directory (including ZIP64 EOCD/extra fields) and populates `entries` and `info`.
    static bool list(const fs::path& archive_path, std::vector<EntryInfo>& entries,
                     ArchiveInfo* info = nullptr, std::string* err = nullptr);

    /// Creates a new ZIP (auto-upgrading to ZIP64 when >4 GB or >=65,535 entries) from `inputs`.
    static OperationResult create(const fs::path& archive_path,
                                  const std::vector<fs::path>& inputs,
                                  const CreateOptions& options);

    /// Extracts all or selected entries to `options.destination`, verifying CRC32/MAC on every file.
    static OperationResult extract(const fs::path& archive_path,
                                   const ExtractOptions& options);

    /// Extracts a single named entry directly into an in-memory buffer `out`.
    static bool extract_entry_to_memory(const fs::path& archive_path,
                                        const std::string& entry_path,
                                        const std::string& password,
                                        std::vector<uint8_t>& out,
                                        std::string* err = nullptr);

    /// Decompresses and verifies CRC32/authentication tags for every entry without writing to disk.
    static OperationResult test(const fs::path& archive_path,
                                const std::string& password,
                                ProgressCallback progress = nullptr,
                                std::atomic<bool>* cancel_flag = nullptr);

    /// Merges `inputs` into an existing ZIP archive (replacing duplicate paths while preserving others).
    static OperationResult add_entries(const fs::path& archive_path,
                                       const std::vector<fs::path>& inputs,
                                       const CreateOptions& options);

    /// Deletes matching file or directory entries without recompressing untouched entries.
    static OperationResult delete_entries(const fs::path& archive_path,
                                          const std::set<std::string>& entries_to_delete);

    /// Updates the EOCD archive-level comment inside an existing ZIP file.
    static OperationResult set_comment(const fs::path& archive_path, const std::string& comment);

    /// Salvages intact entries from a truncated/corrupted ZIP by scanning for Local File Headers
    /// (`PK\x03\x04`) and reconstructing a fresh Central Directory and EOCD.
    static OperationResult repair(const fs::path& corrupted_path, const fs::path& repaired_path);
};

} // namespace openrar
