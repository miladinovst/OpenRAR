// Unified ArchiveManager facade.
// Routes format operations to OrarArchive, ZipArchive, TarArchive, or ExtendedArchive,
// and implements cross-format services: batch convert, recursive find-in-archives,
// standalone SFX builder, multi-threaded benchmark, and HKCU Explorer context menu.

#include "openrar/archive_manager.hpp"
#include "openrar/crypto.hpp"
#include "openrar/deflate.hpp"
#include "openrar/extended_archive.hpp"
#include "openrar/orar_archive.hpp"
#include "openrar/rar_inspector.hpp"
#include "openrar/tar_archive.hpp"
#include "openrar/zip_archive.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace openrar {

namespace {

bool contains_text(const uint8_t* data, size_t len, const std::string& needle,
                   bool case_sensitive, std::string& matched_line) {
    if (needle.empty()) return true;
    std::string text(reinterpret_cast<const char*>(data), len);
    std::string search_text = text;
    std::string search_needle = needle;
    if (!case_sensitive) {
        for (char& c : search_text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : search_needle) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    auto pos = search_text.find(search_needle);
    if (pos == std::string::npos) return false;

    size_t line_start = text.rfind('\n', pos);
    line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
    size_t line_end = text.find('\n', pos);
    if (line_end == std::string::npos) line_end = text.size();
    matched_line = text.substr(line_start, std::min<size_t>(line_end - line_start, 160));
    while (!matched_line.empty() && (matched_line.back() == '\r' || matched_line.back() == '\n')) {
        matched_line.pop_back();
    }
    return true;
}

} // namespace

bool ArchiveManager::list(const fs::path& archive_path, std::vector<EntryInfo>& entries,
                          const std::string& password, ArchiveInfo* info, std::string* err) {
    auto fmt = detect_format_from_file(archive_path);
    switch (fmt) {
        case ArchiveFormat::Zip:
            return ZipArchive::list(archive_path, entries, info, err);
        case ArchiveFormat::Orar:
            return OrarArchive::list(archive_path, entries, password, info, err);
        case ArchiveFormat::Tar:
            return TarArchive::list(archive_path, false, entries, info, err);
        case ArchiveFormat::TarGz:
            return TarArchive::list(archive_path, true, entries, info, err);
        case ArchiveFormat::GZip: {
            auto raw = read_file_bytes(archive_path);
            std::vector<uint8_t> out;
            std::string orig_name;
            if (!codec::gzip_decompress(raw.data(), raw.size(), out, &orig_name, err)) return false;
            if (orig_name.empty()) orig_name = path_to_utf8(archive_path.stem());
            EntryInfo e;
            e.full_path = orig_name;
            e.name = orig_name;
            e.size = out.size();
            e.compressed_size = raw.size();
            e.crc32 = crypto::crc32(out);
            e.method = "GZip";
            entries = {e};
            if (info) {
                info->file_path = archive_path;
                info->format = ArchiveFormat::GZip;
                info->archive_size = raw.size();
                info->total_uncompressed_size = out.size();
                info->total_compressed_size = raw.size();
                info->file_count = 1;
                info->compression_method = "GZip";
            }
            return true;
        }
        case ArchiveFormat::Rar:
        case ArchiveFormat::SevenZip:
        case ArchiveFormat::TarBz2:
        case ArchiveFormat::TarXz:
        case ArchiveFormat::TarZst:
        case ArchiveFormat::Cab:
        case ArchiveFormat::Iso:
            return ExtendedArchive::list(archive_path, fmt, entries, info, err);
        default:
            if (err) *err = "Unsupported or unknown archive format: " + path_to_utf8(archive_path);
            return false;
    }
}

OperationResult ArchiveManager::create(const fs::path& archive_path,
                                       const std::vector<fs::path>& inputs,
                                       const CreateOptions& options) {
    auto fmt = options.format;
    if (fmt == ArchiveFormat::Unknown) fmt = detect_format_from_path(archive_path);
    if (fmt == ArchiveFormat::Unknown) fmt = ArchiveFormat::Zip;

    switch (fmt) {
        case ArchiveFormat::Zip:
            return ZipArchive::create(archive_path, inputs, options);
        case ArchiveFormat::Orar:
            return OrarArchive::create(archive_path, inputs, options);
        case ArchiveFormat::Tar:
            return TarArchive::create(archive_path, false, inputs, options);
        case ArchiveFormat::TarGz:
            return TarArchive::create(archive_path, true, inputs, options);
        case ArchiveFormat::SevenZip:
            return ExtendedArchive::create_7z(archive_path, inputs, options);
        case ArchiveFormat::Rar:
            return ExtendedArchive::create_rar(archive_path, inputs, options);
        default:
            return OperationResult::fail("Creating archives in format '" + format_name(fmt) + "' is not supported; use OpenRAR (.orar), ZIP, 7z, TAR, or TAR.GZ");
    }
}

OperationResult ArchiveManager::extract(const fs::path& archive_path, const ExtractOptions& options) {
    auto fmt = detect_format_from_file(archive_path);
    switch (fmt) {
        case ArchiveFormat::Zip:
            return ZipArchive::extract(archive_path, options);
        case ArchiveFormat::Orar:
            return OrarArchive::extract(archive_path, options);
        case ArchiveFormat::Tar:
            return TarArchive::extract(archive_path, false, options);
        case ArchiveFormat::TarGz:
            return TarArchive::extract(archive_path, true, options);
        case ArchiveFormat::GZip: {
            auto raw = read_file_bytes(archive_path);
            std::vector<uint8_t> out;
            std::string orig_name, err;
            if (!codec::gzip_decompress(raw.data(), raw.size(), out, &orig_name, &err)) {
                return OperationResult::fail(err);
            }
            if (orig_name.empty()) orig_name = path_to_utf8(archive_path.stem());
            fs::path target = options.destination / path_from_utf8(orig_name).filename();
            if (!write_file_bytes(target, out.data(), out.size())) {
                return OperationResult::fail("Failed writing extracted GZip file");
            }
            return OperationResult::ok("Extracted 1 file", 1, out.size());
        }
        case ArchiveFormat::Rar:
        case ArchiveFormat::SevenZip:
        case ArchiveFormat::TarBz2:
        case ArchiveFormat::TarXz:
        case ArchiveFormat::TarZst:
        case ArchiveFormat::Cab:
        case ArchiveFormat::Iso:
            return ExtendedArchive::extract(archive_path, fmt, options);
        default:
            return OperationResult::fail("Unsupported archive format: " + path_to_utf8(archive_path));
    }
}

bool ArchiveManager::extract_entry_to_memory(const fs::path& archive_path,
                                             const std::string& entry_path,
                                             const std::string& password,
                                             std::vector<uint8_t>& out,
                                             std::string* err) {
    auto fmt = detect_format_from_file(archive_path);
    switch (fmt) {
        case ArchiveFormat::Zip:
            return ZipArchive::extract_entry_to_memory(archive_path, entry_path, password, out, err);
        case ArchiveFormat::Orar:
            return OrarArchive::extract_entry_to_memory(archive_path, entry_path, password, out, err);
        case ArchiveFormat::Tar:
            return TarArchive::extract_entry_to_memory(archive_path, false, entry_path, out, err);
        case ArchiveFormat::TarGz:
            return TarArchive::extract_entry_to_memory(archive_path, true, entry_path, out, err);
        case ArchiveFormat::Rar:
        case ArchiveFormat::SevenZip:
        case ArchiveFormat::TarBz2:
        case ArchiveFormat::TarXz:
        case ArchiveFormat::TarZst:
        case ArchiveFormat::Cab:
        case ArchiveFormat::Iso:
            return ExtendedArchive::extract_entry_to_memory(archive_path, fmt, entry_path, password, out, err);
        default:
            if (err) *err = "Cannot extract entry from this format";
            return false;
    }
}

OperationResult ArchiveManager::test(const fs::path& archive_path, const std::string& password,
                                     ProgressCallback progress, std::atomic<bool>* cancel_flag) {
    auto fmt = detect_format_from_file(archive_path);
    switch (fmt) {
        case ArchiveFormat::Zip:
            return ZipArchive::test(archive_path, password, std::move(progress), cancel_flag);
        case ArchiveFormat::Orar:
            return OrarArchive::test(archive_path, password, std::move(progress), cancel_flag);
        case ArchiveFormat::Tar:
            return TarArchive::test(archive_path, false, std::move(progress));
        case ArchiveFormat::TarGz:
            return TarArchive::test(archive_path, true, std::move(progress));
        case ArchiveFormat::Rar:
        case ArchiveFormat::SevenZip:
        case ArchiveFormat::TarBz2:
        case ArchiveFormat::TarXz:
        case ArchiveFormat::TarZst:
        case ArchiveFormat::Cab:
        case ArchiveFormat::Iso:
            return ExtendedArchive::test(archive_path, fmt, password);
        default:
            return OperationResult::fail("Testing not supported for this format");
    }
}

OperationResult ArchiveManager::add_entries(const fs::path& archive_path,
                                            const std::vector<fs::path>& inputs,
                                            const CreateOptions& options) {
    auto fmt = detect_format_from_file(archive_path);
    if (fmt == ArchiveFormat::Zip) return ZipArchive::add_entries(archive_path, inputs, options);
    if (fmt == ArchiveFormat::Orar) return OrarArchive::add_entries(archive_path, inputs, options);
    if (fmt == ArchiveFormat::Rar) return ExtendedArchive::create_rar(archive_path, inputs, options);
    return OperationResult::fail("Adding entries to existing archives is supported for ZIP, OpenRAR (.orar), and RAR (when rar CLI is installed)");
}

OperationResult ArchiveManager::delete_entries(const fs::path& archive_path,
                                               const std::set<std::string>& entries_to_delete,
                                               const std::string& password) {
    auto fmt = detect_format_from_file(archive_path);
    if (fmt == ArchiveFormat::Zip) return ZipArchive::delete_entries(archive_path, entries_to_delete);
    if (fmt == ArchiveFormat::Orar) return OrarArchive::delete_entries(archive_path, entries_to_delete, password);
    return OperationResult::fail("Deleting entries is supported for ZIP and OpenRAR (.orar)");
}

OperationResult ArchiveManager::set_comment(const fs::path& archive_path,
                                            const std::string& comment,
                                            const std::string& password) {
    auto fmt = detect_format_from_file(archive_path);
    if (fmt == ArchiveFormat::Zip) return ZipArchive::set_comment(archive_path, comment);
    if (fmt == ArchiveFormat::Orar) return OrarArchive::set_comment(archive_path, comment, password);
    return OperationResult::fail("Archive comments are supported for ZIP and OpenRAR (.orar)");
}

OperationResult ArchiveManager::repair(const fs::path& corrupted_path, const fs::path& repaired_path) {
    auto fmt = detect_format_from_file(corrupted_path);
    if (fmt == ArchiveFormat::Orar) return OrarArchive::repair(corrupted_path, repaired_path);
    if (fmt == ArchiveFormat::Zip) return ZipArchive::repair(corrupted_path, repaired_path);
    return OperationResult::fail("Repair is supported for OpenRAR (.orar) and ZIP archives");
}

OperationResult ArchiveManager::convert_archives(const std::vector<fs::path>& source_archives,
                                                 const fs::path& output_dir,
                                                 const CreateOptions& target_options,
                                                 const std::string& source_password) {
    auto t0 = std::chrono::steady_clock::now();
    std::error_code ec;
    fs::create_directories(output_dir, ec);

    OperationResult res;
    for (const auto& src : source_archives) {
        fs::path temp_dir = fs::temp_directory_path(ec) /
            ("openrar_conv_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(temp_dir, ec);

        ExtractOptions eo;
        eo.destination = temp_dir;
        eo.password = source_password;
        auto er = extract(src, eo);
        if (!er.success) {
            res.errors.push_back(path_to_utf8(src.filename()) + ": " + er.message);
            fs::remove_all(temp_dir, ec);
            continue;
        }

        std::vector<fs::path> extracted_items;
        for (const auto& it : fs::directory_iterator(temp_dir, ec)) {
            extracted_items.push_back(it.path());
        }

        std::string stem = path_to_utf8(src.stem());
        if (stem.ends_with(".tar")) stem = stem.substr(0, stem.size() - 4);
        fs::path out_path = output_dir / path_from_utf8(stem + default_extension(target_options.format));

        CreateOptions co = target_options;
        co.base_dir = temp_dir;
        auto cr = create(out_path, extracted_items, co);
        fs::remove_all(temp_dir, ec);

        if (!cr.success) {
            res.errors.push_back(path_to_utf8(src.filename()) + ": " + cr.message);
        } else {
            res.files_processed++;
            res.output_files.push_back(out_path);
        }
    }

    res.duration_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    res.success = res.errors.empty();
    res.message = "Converted " + std::to_string(res.files_processed) + " archive(s)";
    return res;
}

std::vector<SearchMatch> ArchiveManager::find_files(const SearchOptions& options) {
    std::vector<SearchMatch> matches;
    std::error_code ec;
    if (!fs::exists(options.root_path, ec)) return matches;

    auto inspect_file = [&](const fs::path& fp) {
        if (options.cancel_flag && options.cancel_flag->load()) return;

        // 1. Check filename itself
        std::string fname = path_to_utf8(fp.filename());
        if (wildcard_match(fname, options.filename_pattern, options.case_sensitive)) {
            bool text_ok = true;
            std::string matched_line;
            if (!options.text_to_find.empty()) {
                auto bytes = read_file_bytes(fp);
                text_ok = contains_text(bytes.data(), bytes.size(), options.text_to_find,
                                        options.case_sensitive, matched_line);
            }
            if (text_ok) {
                SearchMatch sm;
                sm.file_path = fp;
                sm.size = fs::file_size(fp, ec);
                sm.modified_time = file_time_to_unix(fs::last_write_time(fp, ec));
                sm.matched_line = matched_line;
                matches.push_back(std::move(sm));
            }
        }

        // 2. If archive and search_inside_archives is enabled, inspect entries!
        if (options.search_inside_archives && is_archive_path(fp)) {
            std::vector<EntryInfo> entries;
            if (list(fp, entries, options.password)) {
                for (const auto& e : entries) {
                    if (options.cancel_flag && options.cancel_flag->load()) return;
                    if (e.is_directory) continue;
                    if (!wildcard_match(e.name, options.filename_pattern, options.case_sensitive)) continue;

                    std::string matched_line;
                    if (!options.text_to_find.empty()) {
                        std::vector<uint8_t> data;
                        if (!extract_entry_to_memory(fp, e.full_path, options.password, data)) continue;
                        if (!contains_text(data.data(), data.size(), options.text_to_find,
                                           options.case_sensitive, matched_line)) continue;
                    }
                    SearchMatch sm;
                    sm.file_path = fp;
                    sm.archive_path = fp;
                    sm.entry_path = e.full_path;
                    sm.size = e.size;
                    sm.modified_time = e.modified_time;
                    sm.matched_line = matched_line;
                    matches.push_back(std::move(sm));
                }
            }
        }
    };

    if (fs::is_regular_file(options.root_path, ec)) {
        inspect_file(options.root_path);
        return matches;
    }

    if (options.recursive) {
        for (const auto& it : fs::recursive_directory_iterator(options.root_path, fs::directory_options::skip_permission_denied, ec)) {
            if (options.cancel_flag && options.cancel_flag->load()) break;
            if (it.is_regular_file(ec)) inspect_file(it.path());
        }
    } else {
        for (const auto& it : fs::directory_iterator(options.root_path, fs::directory_options::skip_permission_denied, ec)) {
            if (options.cancel_flag && options.cancel_flag->load()) break;
            if (it.is_regular_file(ec)) inspect_file(it.path());
        }
    }
    return matches;
}

OperationResult ArchiveManager::create_sfx(const fs::path& archive_path,
                                           const fs::path& sfx_stub_path,
                                           const fs::path& output_sfx_path) {
    auto stub = read_file_bytes(sfx_stub_path);
    if (stub.empty()) {
        return OperationResult::fail("Cannot read SFX stub binary: " + path_to_utf8(sfx_stub_path));
    }
    auto arc = read_file_bytes(archive_path);
    if (arc.empty()) {
        return OperationResult::fail("Cannot read source archive: " + path_to_utf8(archive_path));
    }

    uint64_t arc_offset = stub.size();
    uint64_t arc_size = arc.size();

    std::vector<uint8_t> out;
    out.reserve(stub.size() + arc.size() + kSfxTrailerSize);
    out.insert(out.end(), stub.begin(), stub.end());
    out.insert(out.end(), arc.begin(), arc.end());
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((arc_offset >> (i * 8)) & 0xFF));
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((arc_size >> (i * 8)) & 0xFF));
    out.insert(out.end(), kSfxMagic, kSfxMagic + 8);

    if (!write_file_bytes(output_sfx_path, out.data(), out.size())) {
        return OperationResult::fail("Failed writing SFX executable: " + path_to_utf8(output_sfx_path));
    }

#ifndef _WIN32
    std::error_code ec;
    fs::permissions(output_sfx_path,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
#endif

    auto r = OperationResult::ok("Created Self-Extracting Archive: " + path_to_utf8(output_sfx_path), 1, out.size());
    r.output_files.push_back(output_sfx_path);
    return r;
}

BenchmarkResult ArchiveManager::run_benchmark(unsigned int threads, double target_seconds,
                                              std::atomic<bool>* cancel_flag) {
    if (threads == 0) {
        threads = std::thread::hardware_concurrency();
        if (threads == 0) threads = 1;
    }
    if (target_seconds < 0.2) target_seconds = 0.2;

    // Generate deterministic compressible benchmark buffer (256 KB of mixed structured text & patterns)
    constexpr size_t kBufSize = 256 * 1024;
    std::vector<uint8_t> sample(kBufSize);
    const char* phrases[] = {
        "OpenRAR high-performance cross-platform compression benchmark data block. ",
        "The quick brown fox jumps over the lazy dog 0123456789! ",
        "AES-256-GCM + RFC-1951 Deflate + Reed-Solomon/Parity Recovery Record. ",
        "C++20 Native Portable Engine for Windows and Linux. \n"
    };
    size_t pos = 0;
    uint32_t lcg = 0x12345678;
    while (pos < kBufSize) {
        lcg = lcg * 1664525U + 1013904223U;
        const char* ph = phrases[(lcg >> 16) & 3];
        size_t n = std::min(std::strlen(ph), kBufSize - pos);
        std::memcpy(sample.data() + pos, ph, n);
        pos += n;
        if (pos < kBufSize && (lcg & 7) == 0) {
            sample[pos++] = static_cast<uint8_t>((lcg >> 8) & 0x7F);
        }
    }

    std::atomic<uint64_t> total_in{0};
    std::atomic<uint64_t> total_out{0};
    auto t0 = std::chrono::steady_clock::now();
    auto deadline = t0 + std::chrono::duration<double>(target_seconds);

    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (unsigned int t = 0; t < threads; ++t) {
        workers.emplace_back([&] {
            std::vector<uint8_t> decomp;
            while (std::chrono::steady_clock::now() < deadline) {
                if (cancel_flag && cancel_flag->load()) break;
                auto comp = codec::deflate_compress(sample, CompressionLevel::Normal);
                codec::deflate_decompress(comp, decomp);
                total_in.fetch_add(sample.size(), std::memory_order_relaxed);
                total_out.fetch_add(comp.size(), std::memory_order_relaxed);
            }
        });
    }
    for (auto& w : workers) w.join();

    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (elapsed <= 0.0) elapsed = 0.001;

    BenchmarkResult res;
    res.threads = threads;
    res.duration_seconds = elapsed;
    res.bytes_processed = total_in.load();
    res.speed_kbps = (static_cast<double>(res.bytes_processed) / 1024.0) / elapsed;
    res.speed_mbps = res.speed_kbps / 1024.0;
    res.compression_ratio = res.bytes_processed > 0
        ? (static_cast<double>(total_out.load()) / static_cast<double>(res.bytes_processed)) * 100.0
        : 0.0;
    return res;
}

// ============================================================================
// Windows Explorer Right-Click Shell Context Menu (HKCU - No Admin Needed)
// ============================================================================
#ifdef _WIN32
static bool set_reg_str(HKEY root, const std::wstring& subkey, const std::wstring& name, const std::wstring& value) {
    HKEY hkey = nullptr;
    if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hkey, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LONG rc = RegSetValueExW(hkey, name.empty() ? nullptr : name.c_str(), 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(value.c_str()),
                             static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hkey);
    return rc == ERROR_SUCCESS;
}

bool ArchiveManager::register_shell_context_menu(const fs::path& gui_exe_path,
                                                 const fs::path&,
                                                 std::string* err) {
    std::wstring gui = gui_exe_path.wstring();
    bool ok = true;

    // Helper: creates a cascading "OpenRAR >" submenu entry with icon and ordered sub-actions
    auto add_sub_item = [&](const std::wstring& menu_root, const std::wstring& key_name,
                            const std::wstring& label, const std::wstring& args) {
        std::wstring item_key = menu_root + L"\\shell\\" + key_name;
        ok &= set_reg_str(HKEY_CURRENT_USER, item_key, L"", label);
        ok &= set_reg_str(HKEY_CURRENT_USER, item_key, L"Icon", gui);
        ok &= set_reg_str(HKEY_CURRENT_USER, item_key + L"\\command", L"", L"\"" + gui + L"\" " + args);
    };

    // 1. Cascading "OpenRAR >" submenu on ANY file or folder
    for (const wchar_t* target_root : {L"Software\\Classes\\*\\shell\\OpenRAR",
                                       L"Software\\Classes\\Directory\\shell\\OpenRAR"}) {
        std::wstring root(target_root);
        ok &= set_reg_str(HKEY_CURRENT_USER, root, L"MUIVerb", L"OpenRAR");
        ok &= set_reg_str(HKEY_CURRENT_USER, root, L"Icon", gui);
        ok &= set_reg_str(HKEY_CURRENT_USER, root, L"SubCommands", L"");
        add_sub_item(root, L"01_AddArchive", L"Add to archive...",       L"--add \"%1\"");
        add_sub_item(root, L"02_QuickOrar",  L"Add to .orar (instant)",  L"--quick-orar \"%1\"");
        add_sub_item(root, L"03_QuickZip",   L"Add to .zip (instant)",   L"--quick-zip \"%1\"");
        add_sub_item(root, L"04_Quick7z",    L"Add to .7z (instant)",    L"--quick-7z \"%1\"");
    }

    // 2. Cascading "OpenRAR >" archive menu on supported archive extensions
    for (const wchar_t* ext : {L".rar", L".7z", L".zip", L".orar", L".tar", L".gz", L".tgz", L".xz", L".bz2", L".zst", L".cab", L".iso"}) {
        std::wstring root = std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + ext + L"\\shell\\OpenRAR";
        ok &= set_reg_str(HKEY_CURRENT_USER, root, L"MUIVerb", L"OpenRAR");
        ok &= set_reg_str(HKEY_CURRENT_USER, root, L"Icon", gui);
        ok &= set_reg_str(HKEY_CURRENT_USER, root, L"SubCommands", L"");
        add_sub_item(root, L"01_Open",          L"Open with OpenRAR",     L"\"%1\"");
        add_sub_item(root, L"02_ExtractDialog", L"Extract files...",      L"--extract \"%1\"");
        add_sub_item(root, L"03_ExtractHere",   L"Extract Here",          L"--extract-here \"%1\"");
        add_sub_item(root, L"04_ExtractFolder", L"Extract to folder\\",   L"--extract-folder \"%1\"");
        add_sub_item(root, L"05_Test",          L"Test archive",          L"--test \"%1\"");
    }

    // Clean up any legacy flat keys from earlier versions
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\OpenRAR_Add");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shell\\OpenRAR_Add");

    if (!ok && err) *err = "Failed writing one or more HKCU registry keys";
    return ok;
}

bool ArchiveManager::unregister_shell_context_menu(std::string*) {
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\OpenRAR");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shell\\OpenRAR");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\OpenRAR_Add");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shell\\OpenRAR_Add");
    for (const wchar_t* ext : {L".rar", L".7z", L".zip", L".orar", L".tar", L".gz", L".tgz", L".xz", L".bz2", L".zst", L".cab", L".iso"}) {
        std::wstring base = std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + ext + L"\\shell\\";
        RegDeleteTreeW(HKEY_CURRENT_USER, (base + L"OpenRAR").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (base + L"OpenRAR_Open").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (base + L"OpenRAR_ExtractHere").c_str());
    }
    return true;
}

bool ArchiveManager::is_shell_context_menu_registered() {
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\OpenRAR", 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        RegCloseKey(hkey);
        return true;
    }
    return false;
}
#else
bool ArchiveManager::register_shell_context_menu(const fs::path&, const fs::path&, std::string* err) {
    if (err) *err = "Windows Explorer context menu integration is only available on Windows";
    return false;
}
bool ArchiveManager::unregister_shell_context_menu(std::string*) { return false; }
bool ArchiveManager::is_shell_context_menu_registered() { return false; }
#endif

} // namespace openrar
