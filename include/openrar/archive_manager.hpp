#pragma once

#include "openrar/types.hpp"

#include <string>
#include <vector>

namespace openrar {

/// Trailer signature appended to self-extracting executables:
/// [sfx_stub binary][archive bytes][uint64 archive_offset][uint64 archive_size]["ORARSFX1"]
constexpr char kSfxMagic[8] = {'O', 'R', 'A', 'R', 'S', 'F', 'X', '1'};
constexpr size_t kSfxTrailerSize = 8 + 8 + 8;

/// High-level unified entry point for all OpenRAR operations.
///
/// Automatically detects archive formats (by magic bytes + extension) and routes calls
/// to the appropriate backend (`OrarArchive`, `ZipArchive`, `TarArchive`, or `ExtendedArchive`),
/// while also providing cross-format services (Convert, Search, SFX, Benchmark, Shell Integration).
class ArchiveManager {
public:
    /// Lists all entries and optional aggregate metadata for any supported archive.
    /// Returns false and populates `err` if the archive cannot be opened or needs a password.
    static bool list(const fs::path& archive_path, std::vector<EntryInfo>& entries,
                     const std::string& password = "",
                     ArchiveInfo* info = nullptr, std::string* err = nullptr);

    /// Creates a new archive in ZIP, OpenRAR (.orar), 7-Zip (.7z), TAR, or TAR.GZ format.
    static OperationResult create(const fs::path& archive_path,
                                  const std::vector<fs::path>& inputs,
                                  const CreateOptions& options);

    /// Extracts all (or selected) files from an archive to `options.destination`.
    /// Enforces Zip-Slip path safety and honors overwrite/update modes.
    static OperationResult extract(const fs::path& archive_path,
                                   const ExtractOptions& options);

    /// Extracts a single archive entry directly into an in-memory byte buffer (used by View & Find).
    static bool extract_entry_to_memory(const fs::path& archive_path,
                                        const std::string& entry_path,
                                        const std::string& password,
                                        std::vector<uint8_t>& out,
                                        std::string* err = nullptr);

    /// Verifies archive integrity (decompression, CRC32, SHA-256, and AES-GCM authentication tags).
    static OperationResult test(const fs::path& archive_path,
                                const std::string& password = "",
                                ProgressCallback progress = nullptr,
                                std::atomic<bool>* cancel_flag = nullptr);

    /// Appends or replaces files inside an existing ZIP or OpenRAR (.orar) archive.
    static OperationResult add_entries(const fs::path& archive_path,
                                       const std::vector<fs::path>& inputs,
                                       const CreateOptions& options);

    /// Removes specified file/folder entries from an existing ZIP or OpenRAR (.orar) archive.
    static OperationResult delete_entries(const fs::path& archive_path,
                                          const std::set<std::string>& entries_to_delete,
                                          const std::string& password = "");

    /// Updates the archive-level comment embedded inside a ZIP or OpenRAR (.orar) archive.
    static OperationResult set_comment(const fs::path& archive_path,
                                       const std::string& comment,
                                       const std::string& password = "");

    /// Repairs a damaged archive:
    /// - OpenRAR (.orar): bit-for-bit sector reconstruction using the embedded Parity Recovery Record.
    /// - ZIP (.zip): scans for intact Local File Headers (PK\x03\x04) and rebuilds Central Directory.
    static OperationResult repair(const fs::path& corrupted_path, const fs::path& repaired_path);

    /// Batch converts one or more archives into `target_options.format` inside `output_dir`.
    static OperationResult convert_archives(const std::vector<fs::path>& source_archives,
                                            const fs::path& output_dir,
                                            const CreateOptions& target_options,
                                            const std::string& source_password = "");

    /// Recursively searches disk directories AND inside archives by filename mask and/or text content.
    static std::vector<SearchMatch> find_files(const SearchOptions& options);

    /// Packages an archive + `openrar_sfx_stub` into a standalone Self-Extracting executable (.exe / ELF).
    static OperationResult create_sfx(const fs::path& archive_path,
                                      const fs::path& sfx_stub_path,
                                      const fs::path& output_sfx_path);

    /// Runs a multi-threaded Deflate compress+verify benchmark across `threads` cores (0 = auto).
    static BenchmarkResult run_benchmark(unsigned int threads = 0,
                                         double target_seconds = 2.0,
                                         std::atomic<bool>* cancel_flag = nullptr);

    /// Registers Windows Explorer right-click menu entries under HKCU (no admin privileges required).
    static bool register_shell_context_menu(const fs::path& gui_exe_path,
                                            const fs::path& cli_exe_path,
                                            std::string* err = nullptr);

    /// Removes all OpenRAR Windows Explorer right-click menu entries from HKCU.
    static bool unregister_shell_context_menu(std::string* err = nullptr);

    /// Returns true if OpenRAR's Windows Explorer context menu keys are currently installed.
    static bool is_shell_context_menu_registered();
};

} // namespace openrar
