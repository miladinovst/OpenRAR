#pragma once

#include "openrar/types.hpp"

#include <string>
#include <vector>

namespace openrar {

/// Native OpenRAR (.orar) archive handler implementing classic archiver flagship features in open source:
/// - Solid compression (`-s`) and multi-threaded non-solid compression
/// - Authenticated AES-256-GCM encryption (PBKDF2-HMAC-SHA256)
/// - Encrypted filenames & file table (`-hp`)
/// - Interleaved Parity Recovery Records (`-rr`) for bit-for-bit corruption repair
/// - Automatic multi-volume split & reassembly (`.part1.orar`, `.part2.orar`, ...)
/// - Per-entry CRC32 + SHA-256 verification and archive comments
class OrarArchive {
public:
    /// Reads the .orar header & file table (decrypting headers if `-hp` was used).
    static bool list(const fs::path& archive_path, std::vector<EntryInfo>& entries,
                     const std::string& password = "",
                     ArchiveInfo* info = nullptr, std::string* err = nullptr);

    /// Compresses `inputs` into a new .orar archive (or split `.partN.orar` volumes).
    static OperationResult create(const fs::path& archive_path,
                                  const std::vector<fs::path>& inputs,
                                  const CreateOptions& options);

    /// Extracts all or selected entries, verifying both CRC32 and SHA-256 for every file.
    static OperationResult extract(const fs::path& archive_path,
                                   const ExtractOptions& options);

    /// Extracts a single entry directly into memory (used for instant View and Search-in-Archives).
    static bool extract_entry_to_memory(const fs::path& archive_path,
                                        const std::string& entry_path,
                                        const std::string& password,
                                        std::vector<uint8_t>& out,
                                        std::string* err = nullptr);

    /// Verifies AES-GCM tags, decompression, CRC32, and SHA-256 of every entry without writing to disk.
    static OperationResult test(const fs::path& archive_path,
                                const std::string& password,
                                ProgressCallback progress = nullptr,
                                std::atomic<bool>* cancel_flag = nullptr);

    /// Adds or replaces entries inside an existing .orar archive while preserving existing files.
    static OperationResult add_entries(const fs::path& archive_path,
                                       const std::vector<fs::path>& inputs,
                                       const CreateOptions& options);

    /// Removes specified file or folder paths and rewrites the .orar archive cleanly.
    static OperationResult delete_entries(const fs::path& archive_path,
                                          const std::set<std::string>& entries_to_delete,
                                          const std::string& password = "");

    /// Updates the archive comment embedded inside the .orar file table.
    static OperationResult set_comment(const fs::path& archive_path,
                                       const std::string& comment,
                                       const std::string& password = "");

    /// Reconstructs corrupted bytes/bursts in `corrupted_path` using the embedded Recovery Record
    /// and writes the bit-for-bit repaired archive to `repaired_path`.
    static OperationResult repair(const fs::path& corrupted_path, const fs::path& repaired_path);
};

} // namespace openrar
