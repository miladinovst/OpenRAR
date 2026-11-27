#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace openrar {

namespace fs = std::filesystem;

/// Supported archive container formats for reading, inspecting, and creating archives.
enum class ArchiveFormat {
    Unknown = 0,
    Zip,       ///< Standard PKWARE ZIP & ZIP64 (.zip, .jar, .apk, .cbz)
    Orar,      ///< OpenRAR native open-source format (.orar) with solid/AES-GCM/recovery
    Tar,       ///< Uncompressed POSIX USTAR archive (.tar)
    TarGz,     ///< GZip-compressed TAR archive (.tar.gz, .tgz)
    GZip,      ///< Single-file GZip stream (.gz)
    Rar,       ///< RAR4 / RAR5 archive (.rar, .cbr)
    SevenZip,  ///< 7-Zip LZMA/LZMA2 archive (.7z)
    TarBz2,    ///< BZip2-compressed TAR archive (.tar.bz2, .tbz2)
    TarXz,     ///< XZ-compressed TAR archive (.tar.xz, .txz)
    TarZst,    ///< Zstandard-compressed TAR archive (.tar.zst, .tzst)
    Cab,       ///< Microsoft Cabinet archive (.cab)
    Iso        ///< Optical disc image (.iso)
};

/// Compression speed vs. ratio presets matching classic archiver -m0 through -m5 levels.
enum class CompressionLevel : int {
    Store   = 0, ///< No compression (fastest, raw byte copy)
    Fastest = 1, ///< Shallow LZ77 hash chains, fixed/dynamic Huffman
    Fast    = 2, ///< Light LZ77 matching
    Normal  = 3, ///< Balanced speed and ratio with lazy matching (default)
    Good    = 4, ///< Deeper LZ77 chains + lazy matching
    Best    = 5  ///< Maximum search depth for smallest archive size
};

/// Strategy when an extracted file already exists on disk.
enum class OverwriteMode {
    Ask,          ///< Invoke ExtractOptions::overwrite_prompt callback
    OverwriteAll, ///< Silently replace existing destination files
    SkipExisting, ///< Keep existing destination files untouched
    RenameAuto    ///< Write extracted file as "name (1).ext", "name (2).ext", etc.
};

/// Controls which entries are extracted relative to existing files on disk.
enum class UpdateMode {
    ExtractAndReplace, ///< Extract all matching entries
    ExtractAndUpdate,  ///< Extract new files and overwrite older existing files
    FreshExistingOnly  ///< Only overwrite files that already exist and are older
};

/// Metadata describing a single file or directory entry inside an archive.
struct EntryInfo {
    std::string full_path;       ///< Normalized forward-slash path inside the archive
    std::string name;            ///< Basename (filename without parent folders)
    std::string directory;       ///< Parent folder path inside the archive (empty if root)
    uint64_t size = 0;           ///< Uncompressed byte size
    uint64_t compressed_size = 0;///< Packed byte size inside archive
    uint32_t crc32 = 0;          ///< IEEE 802.3 CRC32 checksum of uncompressed data
    std::string sha256_hex;      ///< Lowercase hex SHA-256 digest (populated for .orar)
    int64_t modified_time = 0;   ///< Last modification timestamp (Unix epoch seconds)
    bool is_directory = false;   ///< True if this entry represents a directory
    bool is_encrypted = false;   ///< True if entry payload is password-protected
    std::string method;          ///< Human-readable codec/encryption description
    std::string comment;         ///< Optional per-entry comment

    /// Returns packed-to-uncompressed percentage (e.g., 35.0 means 35% of original size).
    double ratio_percent() const {
        if (size == 0) return 0.0;
        double r = (static_cast<double>(compressed_size) / static_cast<double>(size)) * 100.0;
        return r > 999.9 ? 999.9 : r;
    }
};

/// Aggregate metadata and properties for an entire archive file.
struct ArchiveInfo {
    fs::path file_path;                  ///< Path to the archive (or first volume) on disk
    ArchiveFormat format = ArchiveFormat::Unknown;
    uint64_t archive_size = 0;           ///< Total bytes on disk across all volumes
    uint64_t total_uncompressed_size = 0;///< Sum of uncompressed file sizes
    uint64_t total_compressed_size = 0;  ///< Sum of compressed payload sizes
    size_t file_count = 0;               ///< Number of regular files in the archive
    size_t folder_count = 0;             ///< Number of directory entries in the archive
    bool is_solid = false;               ///< True if files share one continuous compressed stream
    bool is_encrypted = false;           ///< True if file payloads are encrypted
    bool has_encrypted_headers = false;  ///< True if filenames & metadata are also encrypted
    bool has_recovery_record = false;    ///< True if parity recovery data is embedded
    uint32_t recovery_percent = 0;       ///< Recovery overhead percentage (1..50)
    bool is_multi_volume = false;        ///< True if split across multiple .partN files
    size_t volume_count = 1;             ///< Total number of detected volumes
    std::string comment;                 ///< Archive-level text comment
    std::string compression_method;      ///< Summary of compression/container method

    /// Overall packed archive size as a percentage of total uncompressed size.
    double overall_ratio_percent() const {
        if (total_uncompressed_size == 0) return 0.0;
        double r = (static_cast<double>(archive_size) / static_cast<double>(total_uncompressed_size)) * 100.0;
        return r > 999.9 ? 999.9 : r;
    }
};

/// Real-time progress snapshot emitted to ProgressCallback during long operations.
struct ProgressInfo {
    std::string operation;       ///< Current stage (e.g., "Compressing (OpenRAR)")
    std::string current_file;    ///< File currently being processed
    uint64_t bytes_processed = 0;///< Uncompressed bytes processed so far
    uint64_t total_bytes = 0;    ///< Total expected uncompressed bytes
    size_t files_processed = 0;  ///< Number of files completed so far
    size_t total_files = 0;      ///< Total number of files in batch
    double speed_bps = 0.0;      ///< Rolling throughput in bytes per second

    /// Computes completion percentage in [0.0, 100.0].
    double percent() const {
        if (total_bytes > 0) {
            double p = (static_cast<double>(bytes_processed) / static_cast<double>(total_bytes)) * 100.0;
            return p > 100.0 ? 100.0 : p;
        }
        if (total_files > 0) {
            double p = (static_cast<double>(files_processed) / static_cast<double>(total_files)) * 100.0;
            return p > 100.0 ? 100.0 : p;
        }
        return 0.0;
    }
};

/// Callback invoked periodically by core operations to report live progress to CLI/GUI.
using ProgressCallback = std::function<void(const ProgressInfo&)>;

/// Standardized outcome returned by archive creation, extraction, test, and repair calls.
struct OperationResult {
    bool success = false;                ///< True if operation finished without fatal errors
    std::string message;                 ///< Human-readable summary message
    size_t files_processed = 0;          ///< Count of files successfully processed
    uint64_t bytes_processed = 0;        ///< Total uncompressed bytes processed
    double duration_seconds = 0.0;       ///< Wall-clock duration in seconds
    std::vector<std::string> errors;     ///< Detailed error descriptions (if any)
    std::vector<std::string> warnings;   ///< Non-fatal warnings (e.g., skipped unsafe paths)
    std::vector<fs::path> output_files;  ///< Paths of created archive/volume/repaired files

    /// Helper factory for a successful OperationResult.
    static OperationResult ok(std::string msg, size_t files = 0, uint64_t bytes = 0, double secs = 0.0) {
        OperationResult r;
        r.success = true;
        r.message = std::move(msg);
        r.files_processed = files;
        r.bytes_processed = bytes;
        r.duration_seconds = secs;
        return r;
    }

    /// Helper factory for a failed OperationResult.
    static OperationResult fail(std::string msg) {
        OperationResult r;
        r.success = false;
        r.message = std::move(msg);
        return r;
    }
};

/// Configuration parameters for creating or adding files to an archive.
struct CreateOptions {
    ArchiveFormat format = ArchiveFormat::Zip;
    CompressionLevel level = CompressionLevel::Normal;
    std::string password;                ///< Optional encryption password (empty = unencrypted)
    bool encrypt_headers = false;        ///< Also encrypt file table/names (.orar -hp)
    bool use_aes256 = true;              ///< True = AES-256, False = legacy ZipCrypto (.zip)
    bool solid = false;                  ///< Compress all files as one solid stream (.orar -s)
    bool add_recovery_record = false;    ///< Append parity recovery record (.orar -rr)
    uint32_t recovery_percent = 5;       ///< Recovery overhead percentage (1..50)
    uint64_t split_volume_bytes = 0;     ///< Max volume size in bytes (0 = single archive)
    std::string comment;                 ///< Optional archive comment
    bool delete_after = false;           ///< Delete source files after successful archiving (-df)
    bool test_after = false;             ///< Run integrity verification immediately after creation (-t)
    fs::path base_dir;                   ///< Base directory used to compute relative entry paths
    std::atomic<bool>* cancel_flag = nullptr; ///< Optional cooperative cancellation token
    ProgressCallback progress;           ///< Optional progress listener
};

/// Configuration parameters for extracting files from an archive.
struct ExtractOptions {
    fs::path destination;                ///< Output directory on disk
    std::string password;                ///< Password for encrypted archives
    OverwriteMode overwrite = OverwriteMode::OverwriteAll;
    UpdateMode update_mode = UpdateMode::ExtractAndReplace;
    bool preserve_paths = true;          ///< True = recreate subdirectories, False = flat extract
    bool keep_broken = false;            ///< Keep files even if CRC/auth check fails
    std::set<std::string> selected_entries; ///< Empty = extract all; non-empty = extract subset
    std::function<OverwriteMode(const fs::path&)> overwrite_prompt; ///< Used when overwrite == Ask
    std::atomic<bool>* cancel_flag = nullptr; ///< Optional cooperative cancellation token
    ProgressCallback progress;           ///< Optional progress listener
};

/// Query parameters for ArchiveManager::find_files (searches disk folders AND inside archives).
struct SearchOptions {
    fs::path root_path;                  ///< Starting directory or single archive file
    std::string filename_pattern = "*";  ///< Wildcard pattern supporting '*' and '?'
    std::string text_to_find;            ///< Optional substring to search inside file contents
    bool case_sensitive = false;         ///< Case sensitivity for both filename and text matching
    bool recursive = true;               ///< Descend into subdirectories
    bool search_inside_archives = true;  ///< Inspect and search inside encountered archives
    std::string password;                ///< Optional password for encrypted archives
    std::atomic<bool>* cancel_flag = nullptr;
};

/// Single search hit returned by ArchiveManager::find_files.
struct SearchMatch {
    fs::path file_path;                  ///< Matched file on disk (or host archive path)
    fs::path archive_path;               ///< Non-empty if the match was found inside an archive
    std::string entry_path;              ///< Entry path inside archive_path
    uint64_t size = 0;
    int64_t modified_time = 0;
    std::string matched_line;            ///< Snippet of matching line when text_to_find is set

    bool is_in_archive() const { return !archive_path.empty(); }
};

/// Summary metrics produced by ArchiveManager::run_benchmark.
struct BenchmarkResult {
    unsigned int threads = 1;            ///< Number of worker threads executed
    double duration_seconds = 0.0;       ///< Actual elapsed benchmark time
    uint64_t bytes_processed = 0;        ///< Total uncompressed bytes compressed & verified
    double speed_kbps = 0.0;             ///< Throughput in KiB/s
    double speed_mbps = 0.0;             ///< Throughput in MiB/s
    double compression_ratio = 0.0;      ///< Resulting compressed/original ratio (%)
};

// ============================================================================
// Format Detection, Formatting, Path Safety, & File I/O Utilities
// ============================================================================

/// Guesses archive format purely from the filename extension(s) (e.g., ".tar.gz", ".orar").
ArchiveFormat detect_format_from_path(const fs::path& path);

/// Peeks the file's magic signature bytes first, falling back to extension detection.
ArchiveFormat detect_format_from_file(const fs::path& path);

/// Returns true if the filename has a recognized archive extension.
bool is_archive_path(const fs::path& path);

/// Returns a human-readable display name for an ArchiveFormat (e.g., "OpenRAR (.orar)").
std::string format_name(ArchiveFormat fmt);

/// Returns the canonical file extension for an ArchiveFormat (e.g., ".zip", ".tar.gz").
std::string default_extension(ArchiveFormat fmt);

/// Formats a raw byte count into human-readable units (e.g., "12.40 MB").
std::string format_bytes(uint64_t bytes);

/// Formats a Unix epoch timestamp as "YYYY-MM-DD HH:MM:SS" in local time.
std::string format_timestamp(int64_t unix_time);

/// Converts backslashes to forward slashes and strips leading/trailing slashes.
std::string normalize_entry_path(const std::string& path);

/// Guards against Zip-Slip path traversal (rejects absolute paths, drive letters, and "..").
bool is_safe_relative_path(const std::string& path);

/// Glob-style wildcard matching supporting '*' (zero or more) and '?' (single char).
bool wildcard_match(const std::string& text, const std::string& pattern, bool case_sensitive = false);

/// Cross-platform conversion from std::filesystem::file_time_type to Unix epoch seconds.
int64_t file_time_to_unix(const fs::file_time_type& ft);

/// Cross-platform conversion from Unix epoch seconds to std::filesystem::file_time_type.
fs::file_time_type unix_to_file_time(int64_t unix_time);

/// Converts a filesystem path to a UTF-8 std::string safely on Windows and Linux.
std::string path_to_utf8(const fs::path& p);

/// Constructs a filesystem path from a UTF-8 std::string safely on Windows and Linux.
fs::path path_from_utf8(const std::string& utf8);

/// Reads an entire binary file into memory (returns empty vector if unreadable or 0 bytes).
std::vector<uint8_t> read_file_bytes(const fs::path& p);

/// Creates parent directories if needed and atomically writes `len` bytes to `p`.
bool write_file_bytes(const fs::path& p, const uint8_t* data, size_t len);

/// Parses human-friendly volume sizes like "10m", "500kb", "2g", or raw byte numbers.
uint64_t parse_size_string(const std::string& s);

} // namespace openrar
