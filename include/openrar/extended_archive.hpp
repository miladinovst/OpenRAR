#pragma once

#include "openrar/types.hpp"

#include <string>
#include <vector>

namespace openrar {

/// Cross-platform extended archive backend supporting RAR4, RAR5, 7-Zip (.7z),
/// TAR.XZ, TAR.BZ2, TAR.ZST, CAB, and ISO.
///
/// Uses Windows 11's built-in libarchive (`tar.exe` / `bsdtar`, invoked silently with
/// `CREATE_NO_WINDOW`) and Linux's `bsdtar`/`tar`, with automatic fallback to `unrar`/`7z`
/// when extracting password-protected `.rar` or `.7z` archives.
class ExtendedArchive {
public:
    /// Returns true if a working `bsdtar`/`tar` binary is available on this system.
    static bool is_available();

    /// Lists entries and metadata (uses native RarInspector first for rich RAR4/RAR5 metadata).
    static bool list(const fs::path& archive_path, ArchiveFormat fmt,
                     std::vector<EntryInfo>& entries,
                     ArchiveInfo* info = nullptr, std::string* err = nullptr);

    /// Extracts all or selected entries from an extended-format archive to `options.destination`.
    static OperationResult extract(const fs::path& archive_path, ArchiveFormat fmt,
                                   const ExtractOptions& options);

    /// Streams a single archive entry to memory via stdout (used by View & Find-in-Archives).
    static bool extract_entry_to_memory(const fs::path& archive_path, ArchiveFormat fmt,
                                        const std::string& entry_path,
                                        const std::string& password,
                                        std::vector<uint8_t>& out,
                                        std::string* err = nullptr);

    /// Verifies archive integrity by streaming decompression end-to-end without writing files.
    static OperationResult test(const fs::path& archive_path, ArchiveFormat fmt,
                                const std::string& password = "");

    /// Creates a new `.7z` archive from `inputs` using libarchive's 7-Zip writer.
    static OperationResult create_7z(const fs::path& archive_path,
                                     const std::vector<fs::path>& inputs,
                                     const CreateOptions& options);

    /// Creates or adds to a `.rar` archive if RARLAB's `rar` CLI is installed on the user's system.
    /// Returns a clear message guiding users to `.orar`, `.zip`, or `.7z` if `rar` is not installed.
    static OperationResult create_rar(const fs::path& archive_path,
                                      const std::vector<fs::path>& inputs,
                                      const CreateOptions& options);
};

} // namespace openrar
