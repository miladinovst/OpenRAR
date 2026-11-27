// POSIX USTAR + GNU LongName ('L') TAR and TAR.GZ archive handler.
// Produces and reads standard 512-byte block archives compatible with Linux tar and gzip.

#include "openrar/tar_archive.hpp"
#include "openrar/crypto.hpp"
#include "openrar/deflate.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace openrar {

namespace {

struct TarHeader {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};
static_assert(sizeof(TarHeader) == 512, "TAR header must be 512 bytes");

uint64_t parse_octal(const char* s, size_t len) {
    uint64_t val = 0;
    size_t i = 0;
    while (i < len && (s[i] == ' ' || s[i] == '\0')) ++i;
    for (; i < len && s[i] >= '0' && s[i] <= '7'; ++i) {
        val = (val << 3) | static_cast<uint64_t>(s[i] - '0');
    }
    return val;
}

void write_octal(char* dst, size_t len, uint64_t val) {
    std::memset(dst, '0', len - 1);
    dst[len - 1] = '\0';
    size_t pos = len - 2;
    while (val > 0 && pos < len) {
        dst[pos] = static_cast<char>('0' + (val & 7));
        val >>= 3;
        if (pos == 0) break;
        --pos;
    }
}

uint32_t compute_header_checksum(const TarHeader& h) {
    const auto* raw = reinterpret_cast<const uint8_t*>(&h);
    uint32_t sum = 0;
    for (size_t i = 0; i < 512; ++i) {
        if (i >= 148 && i < 156) sum += ' ';
        else sum += raw[i];
    }
    return sum;
}

bool is_zero_block(const uint8_t* p) {
    for (size_t i = 0; i < 512; ++i) {
        if (p[i] != 0) return false;
    }
    return true;
}

bool load_tar_bytes(const fs::path& path, bool gzipped, std::vector<uint8_t>& tar_bytes, std::string* err) {
    auto raw = read_file_bytes(path);
    if (raw.empty()) {
        if (err) *err = "Cannot read TAR archive: " + path_to_utf8(path);
        return false;
    }
    if (gzipped) {
        return codec::gzip_decompress(raw.data(), raw.size(), tar_bytes, nullptr, err);
    }
    tar_bytes = std::move(raw);
    return true;
}

struct TarParsedEntry {
    std::string path;
    bool is_directory = false;
    uint64_t size = 0;
    int64_t mtime = 0;
    size_t data_offset = 0;
};

bool parse_tar(const std::vector<uint8_t>& buf, std::vector<TarParsedEntry>& out, std::string* err) {
    out.clear();
    size_t pos = 0;
    std::string gnu_long_name;

    while (pos + 512 <= buf.size()) {
        if (is_zero_block(buf.data() + pos)) break;

        TarHeader h{};
        std::memcpy(&h, buf.data() + pos, 512);
        uint32_t expected = static_cast<uint32_t>(parse_octal(h.chksum, sizeof(h.chksum)));
        if (compute_header_checksum(h) != expected) {
            if (err) *err = "Invalid TAR header checksum";
            return false;
        }

        uint64_t size = parse_octal(h.size, sizeof(h.size));
        int64_t mtime = static_cast<int64_t>(parse_octal(h.mtime, sizeof(h.mtime)));
        size_t data_off = pos + 512;
        size_t blocks = static_cast<size_t>((size + 511) / 512);
        pos = data_off + blocks * 512;
        if (pos > buf.size()) {
            if (err) *err = "Truncated TAR entry payload";
            return false;
        }

        if (h.typeflag == 'L') { // GNU LongLink name
            size_t nlen = static_cast<size_t>(size);
            while (nlen > 0 && buf[data_off + nlen - 1] == '\0') --nlen;
            gnu_long_name.assign(reinterpret_cast<const char*>(buf.data() + data_off), nlen);
            continue;
        }

        std::string name;
        if (!gnu_long_name.empty()) {
            name = std::move(gnu_long_name);
            gnu_long_name.clear();
        } else {
            std::string n(h.name, strnlen(h.name, sizeof(h.name)));
            std::string pre(h.prefix, strnlen(h.prefix, sizeof(h.prefix)));
            name = pre.empty() ? n : (pre + "/" + n);
        }

        if (h.typeflag == '0' || h.typeflag == '\0' || h.typeflag == '5') {
            TarParsedEntry e;
            e.path = normalize_entry_path(name);
            e.is_directory = (h.typeflag == '5') || (!name.empty() && name.back() == '/');
            e.size = e.is_directory ? 0 : size;
            e.mtime = mtime;
            e.data_offset = data_off;
            if (!e.path.empty()) out.push_back(std::move(e));
        }
    }
    return true;
}

void append_tar_entry(std::vector<uint8_t>& tar, const std::string& rel_path,
                      bool is_dir, int64_t mtime, const uint8_t* data, size_t len) {
    std::string full = is_dir ? (rel_path + "/") : rel_path;
    if (full.size() > 100) {
        // Emit GNU LongName ('L') header
        TarHeader lh{};
        std::strncpy(lh.name, "././@LongLink", sizeof(lh.name) - 1);
        write_octal(lh.mode, sizeof(lh.mode), 0644);
        write_octal(lh.uid, sizeof(lh.uid), 0);
        write_octal(lh.gid, sizeof(lh.gid), 0);
        write_octal(lh.size, sizeof(lh.size), full.size() + 1);
        write_octal(lh.mtime, sizeof(lh.mtime), static_cast<uint64_t>(mtime > 0 ? mtime : 0));
        lh.typeflag = 'L';
        std::memcpy(lh.magic, "ustar ", 6);
        std::memcpy(lh.version, " ", 2);
        write_octal(lh.chksum, sizeof(lh.chksum), compute_header_checksum(lh));
        const auto* raw_lh = reinterpret_cast<const uint8_t*>(&lh);
        tar.insert(tar.end(), raw_lh, raw_lh + 512);
        tar.insert(tar.end(), full.begin(), full.end());
        tar.push_back(0);
        size_t rem = (full.size() + 1) % 512;
        if (rem != 0) tar.insert(tar.end(), 512 - rem, 0);
    }

    TarHeader h{};
    std::strncpy(h.name, full.c_str(), std::min<size_t>(full.size(), 99));
    write_octal(h.mode, sizeof(h.mode), is_dir ? 0755 : 0644);
    write_octal(h.uid, sizeof(h.uid), 0);
    write_octal(h.gid, sizeof(h.gid), 0);
    write_octal(h.size, sizeof(h.size), is_dir ? 0 : len);
    write_octal(h.mtime, sizeof(h.mtime), static_cast<uint64_t>(mtime > 0 ? mtime : 0));
    h.typeflag = is_dir ? '5' : '0';
    std::memcpy(h.magic, "ustar", 6);
    h.version[0] = '0'; h.version[1] = '0';
    write_octal(h.chksum, sizeof(h.chksum), compute_header_checksum(h));

    const auto* raw_h = reinterpret_cast<const uint8_t*>(&h);
    tar.insert(tar.end(), raw_h, raw_h + 512);
    if (!is_dir && len > 0) {
        tar.insert(tar.end(), data, data + len);
        size_t rem = len % 512;
        if (rem != 0) tar.insert(tar.end(), 512 - rem, 0);
    }
}

} // namespace

bool TarArchive::list(const fs::path& archive_path, bool gzipped,
                      std::vector<EntryInfo>& entries, ArchiveInfo* info, std::string* err) {
    entries.clear();
    std::vector<uint8_t> tar_bytes;
    if (!load_tar_bytes(archive_path, gzipped, tar_bytes, err)) return false;

    std::vector<TarParsedEntry> parsed;
    if (!parse_tar(tar_bytes, parsed, err)) return false;

    std::error_code ec;
    ArchiveInfo ai;
    ai.file_path = archive_path;
    ai.format = gzipped ? ArchiveFormat::TarGz : ArchiveFormat::Tar;
    ai.archive_size = fs::file_size(archive_path, ec);
    ai.compression_method = gzipped ? "GZip + USTAR" : "USTAR (Store)";

    for (const auto& p : parsed) {
        EntryInfo e;
        e.full_path = p.path;
        auto slash = p.path.rfind('/');
        if (slash != std::string::npos) {
            e.directory = p.path.substr(0, slash);
            e.name = p.path.substr(slash + 1);
        } else {
            e.name = p.path;
        }
        e.is_directory = p.is_directory;
        e.size = p.size;
        e.compressed_size = p.size;
        e.modified_time = p.mtime;
        e.method = gzipped ? "GZip" : "TAR";
        if (!e.is_directory && p.size > 0) {
            e.crc32 = crypto::crc32(tar_bytes.data() + p.data_offset, static_cast<size_t>(p.size));
        }
        if (e.is_directory) ai.folder_count++;
        else {
            ai.file_count++;
            ai.total_uncompressed_size += e.size;
            ai.total_compressed_size += e.compressed_size;
        }
        entries.push_back(std::move(e));
    }
    if (gzipped) ai.total_compressed_size = ai.archive_size;
    if (info) *info = std::move(ai);
    return true;
}

OperationResult TarArchive::create(const fs::path& archive_path, bool gzipped,
                                   const std::vector<fs::path>& inputs,
                                   const CreateOptions& options) {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> tar;
    size_t files_done = 0;
    uint64_t bytes_done = 0;

    for (const auto& inp : inputs) {
        std::error_code ec;
        if (!fs::exists(inp, ec)) continue;
        fs::path abs_inp = fs::absolute(inp, ec);
        fs::path root = options.base_dir.empty() ? abs_inp.parent_path() : fs::absolute(options.base_dir, ec);

        if (fs::is_directory(abs_inp, ec)) {
            std::string drel = normalize_entry_path(path_to_utf8(fs::relative(abs_inp, root, ec)));
            if (!drel.empty() && drel != ".") {
                append_tar_entry(tar, drel, true, file_time_to_unix(fs::last_write_time(abs_inp, ec)), nullptr, 0);
            }
            for (const auto& it : fs::recursive_directory_iterator(abs_inp, ec)) {
                if (options.cancel_flag && options.cancel_flag->load()) return OperationResult::fail("Cancelled");
                std::string rel = normalize_entry_path(path_to_utf8(fs::relative(it.path(), root, ec)));
                if (rel.empty() || rel == ".") continue;
                bool is_dir = it.is_directory(ec);
                int64_t mt = file_time_to_unix(it.last_write_time(ec));
                if (is_dir) {
                    append_tar_entry(tar, rel, true, mt, nullptr, 0);
                } else {
                    auto bytes = read_file_bytes(it.path());
                    append_tar_entry(tar, rel, false, mt, bytes.data(), bytes.size());
                    bytes_done += bytes.size();
                }
                files_done++;
            }
        } else {
            std::string rel = normalize_entry_path(path_to_utf8(fs::relative(abs_inp, root, ec)));
            if (rel.empty() || rel.find("..") == 0) rel = path_to_utf8(abs_inp.filename());
            auto bytes = read_file_bytes(abs_inp);
            append_tar_entry(tar, rel, false, file_time_to_unix(fs::last_write_time(abs_inp, ec)),
                             bytes.data(), bytes.size());
            bytes_done += bytes.size();
            files_done++;
        }
    }

    if (files_done == 0) return OperationResult::fail("No files found to archive");

    // End-of-archive marker: two 512-byte zero blocks
    tar.insert(tar.end(), 1024, 0);

    std::vector<uint8_t> final_bytes;
    if (gzipped) {
        final_bytes = codec::gzip_compress(tar.data(), tar.size(), options.level,
                                           path_to_utf8(archive_path.stem()));
    } else {
        final_bytes = std::move(tar);
    }

    if (!write_file_bytes(archive_path, final_bytes.data(), final_bytes.size())) {
        return OperationResult::fail("Failed writing TAR archive");
    }

    auto r = OperationResult::ok(
        std::string("Created ") + (gzipped ? "TAR.GZ" : "TAR") + " archive with " + std::to_string(files_done) + " item(s)",
        files_done, bytes_done,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    r.output_files.push_back(archive_path);
    return r;
}

OperationResult TarArchive::extract(const fs::path& archive_path, bool gzipped,
                                    const ExtractOptions& options) {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> tar_bytes;
    std::string err;
    if (!load_tar_bytes(archive_path, gzipped, tar_bytes, &err)) return OperationResult::fail(err);

    std::vector<TarParsedEntry> parsed;
    if (!parse_tar(tar_bytes, parsed, &err)) return OperationResult::fail(err);

    std::error_code ec;
    fs::create_directories(options.destination, ec);

    OperationResult res;
    size_t files_done = 0;
    uint64_t bytes_done = 0;

    for (const auto& e : parsed) {
        if (options.cancel_flag && options.cancel_flag->load()) return OperationResult::fail("Cancelled");
        if (!options.selected_entries.empty() && !options.selected_entries.count(e.path)) continue;
        if (!is_safe_relative_path(e.path)) { res.warnings.push_back("Skipped unsafe path: " + e.path); continue; }

        fs::path norm_fs = path_from_utf8(e.path);
        fs::path rel = options.preserve_paths ? norm_fs : norm_fs.filename();
        fs::path target = options.destination / rel;
        if (e.is_directory) {
            if (options.preserve_paths) fs::create_directories(target, ec);
            continue;
        }
        if (options.overwrite == OverwriteMode::SkipExisting && fs::exists(target, ec)) continue;
        if (!write_file_bytes(target, tar_bytes.data() + e.data_offset, static_cast<size_t>(e.size))) {
            res.errors.push_back("Failed writing: " + path_to_utf8(target));
            continue;
        }
        if (e.mtime > 0) fs::last_write_time(target, unix_to_file_time(e.mtime), ec);
        files_done++;
        bytes_done += e.size;
    }

    res.files_processed = files_done;
    res.bytes_processed = bytes_done;
    res.duration_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    res.success = res.errors.empty();
    res.message = res.success ? ("Extracted " + std::to_string(files_done) + " file(s)") : "Extraction had errors";
    return res;
}

bool TarArchive::extract_entry_to_memory(const fs::path& archive_path, bool gzipped,
                                         const std::string& entry_path,
                                         std::vector<uint8_t>& out, std::string* err) {
    std::vector<uint8_t> tar_bytes;
    if (!load_tar_bytes(archive_path, gzipped, tar_bytes, err)) return false;
    std::vector<TarParsedEntry> parsed;
    if (!parse_tar(tar_bytes, parsed, err)) return false;
    std::string target = normalize_entry_path(entry_path);
    for (const auto& e : parsed) {
        if (e.path == target) {
            out.assign(tar_bytes.begin() + static_cast<ptrdiff_t>(e.data_offset),
                       tar_bytes.begin() + static_cast<ptrdiff_t>(e.data_offset + e.size));
            return true;
        }
    }
    if (err) *err = "Entry not found: " + entry_path;
    return false;
}

OperationResult TarArchive::test(const fs::path& archive_path, bool gzipped, ProgressCallback) {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<EntryInfo> entries;
    std::string err;
    if (!list(archive_path, gzipped, entries, nullptr, &err)) {
        return OperationResult::fail(err);
    }
    return OperationResult::ok("TAR archive integrity OK (" + std::to_string(entries.size()) + " entries)",
                               entries.size(), 0,
                               std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
}

} // namespace openrar
