// Cross-platform OpenRaR command-line interface (Windows, Linux, macOS).
// Parses single-letter commands (a, x, e, l, t, d, c, r, f, cv, sfx, b, info) and
// short switches (-p, -hp, -m0..5, -s, -rr, -v, -o+/-o-/-or, -df, -t, -y).

#include "openrar/archive_manager.hpp"
#include "openrar/i18n.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace openrar;

static std::string tr_cli(i18n::StrKey key) {
    std::string out;
    for (char c : std::string(i18n::tr(key))) {
        if (c != '&') out.push_back(c);
    }
    return out;
}

static std::string get_env_str(const char* name) {
#ifdef _MSC_VER
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) == 0 && buf) {
        std::string val(buf);
        std::free(buf);
        return val;
    }
    return {};
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string{};
#endif
}

static void print_usage() {
    std::cout <<
R"(OpenRaR 1.0 - Open-Source Cross-Platform Archiver (C++20)
Usage: openrar <command> [switches] <archive/args...>

Commands:
  a    Add files to archive (creates .zip, .orar, .7z, .tar, .tar.gz)
  x    Extract files with full paths
  e    Extract files without paths (flat)
  l    List contents of archive
  t    Test integrity of archive (CRC32 / SHA-256 / AES-GCM)
  d    Delete files from archive
  c    Set or display archive comment
  r    Repair corrupted archive (.orar via recovery record, .zip via header salvage)
  f    Find files across folders and inside archives
  cv   Convert archives to another format
  sfx  Create Self-Extracting Archive executable
  b    Benchmark multi-threaded compression speed
  info Display detailed archive properties
  lang List all 13 supported UI/CLI languages and active locale
  install-menu   Register Windows Explorer right-click OpenRaR submenu (HKCU, no admin)
  uninstall-menu Remove Windows Explorer right-click OpenRaR submenu

Switches:
  --lang=<code> Override display language (en, zh, es, hi, ar, pt, id, fr, ja, ru, sr, hr, bs)
  -p<pass>    Set password (AES-256)
  -hp<pass>   Set password & encrypt file headers/names (.orar)
  -zipcrypto  Use legacy ZipCrypto instead of AES-256 for .zip
  -m0..-m5    Compression level (0=Store, 1=Fastest, 3=Normal, 5=Best)
  -s          Create solid archive (.orar)
  -rr[N]      Add recovery record (default 5%, range 1..50, .orar)
  -v<size>    Split into volumes (e.g., -v10m, -v500k)
  -o+ / -o-   Overwrite existing files / Skip existing files
  -or         Auto-rename conflicting files
  -df         Delete source files after archiving
  -t          Test archive after creation
  -y          Assume Yes on all queries

Examples:
  openrar a -m5 -s -rr10 -hpMyPass backup.orar ./my_folder
  openrar a -pSecret docs.zip report.pdf notes.txt
  openrar x -pSecret docs.zip ./extracted
  openrar r damaged.orar fixed.orar
  openrar f ./projects "*.cpp" "AES-256"
  openrar b 4 2
  openrar lang --lang=sr
)";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    i18n::set_language(i18n::detect_system_language());
    if (std::string env_lang = get_env_str("OPENRAR_LANG"); !env_lang.empty()) {
        i18n::set_language_code(env_lang);
    }

    if (argc < 2) {
        print_usage();
        return 0;
    }

    std::string cmd = argv[1];
    if (cmd == "-h" || cmd == "--help" || cmd == "help") {
        print_usage();
        return 0;
    }

    CreateOptions co;
    ExtractOptions eo;
    std::vector<std::string> pos_args;
    std::string password;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a.starts_with("--lang=")) {
            if (!i18n::set_language_code(a.substr(7))) {
                std::cerr << "Unknown language code: " << a.substr(7) << "\n";
                return 1;
            }
        } else if (a.size() >= 2 && a[0] == '-') {
            if (a.starts_with("-hp")) {
                password = a.substr(3);
                co.password = password;
                eo.password = password;
                co.encrypt_headers = true;
            } else if (a.starts_with("-p")) {
                password = a.substr(2);
                co.password = password;
                eo.password = password;
            } else if (a == "-zipcrypto") {
                co.use_aes256 = false;
            } else if (a.size() == 3 && a[1] == 'm' && a[2] >= '0' && a[2] <= '5') {
                co.level = static_cast<CompressionLevel>(a[2] - '0');
            } else if (a == "-s") {
                co.solid = true;
            } else if (a.starts_with("-rr")) {
                co.add_recovery_record = true;
                if (a.size() > 3) {
                    try { co.recovery_percent = static_cast<uint32_t>(std::stoul(a.substr(3))); } catch (...) {}
                }
            } else if (a.starts_with("-v")) {
                co.split_volume_bytes = parse_size_string(a.substr(2));
            } else if (a == "-o+" || a == "-y") {
                eo.overwrite = OverwriteMode::OverwriteAll;
            } else if (a == "-o-") {
                eo.overwrite = OverwriteMode::SkipExisting;
            } else if (a == "-or") {
                eo.overwrite = OverwriteMode::RenameAuto;
            } else if (a == "-df") {
                co.delete_after = true;
            } else if (a == "-t") {
                co.test_after = true;
            }
        } else {
            pos_args.push_back(a);
        }
    }

    // -----------------------------------------------------------------------
    // a: Add / Create archive
    // -----------------------------------------------------------------------
    if (cmd == "a") {
        if (pos_args.size() < 2) {
            std::cerr << "Usage: openrar a [switches] <archive> <files...>\n";
            return 1;
        }
        fs::path archive = pos_args[0];
        co.format = detect_format_from_path(archive);
        if (co.format == ArchiveFormat::Unknown) co.format = ArchiveFormat::Zip;
        std::vector<fs::path> inputs;
        for (size_t i = 1; i < pos_args.size(); ++i) inputs.emplace_back(pos_args[i]);

        co.progress = [](const ProgressInfo& pi) {
            std::cout << "\r[" << std::fixed << std::setprecision(1) << pi.percent() << "%] "
                      << pi.current_file << "          " << std::flush;
        };

        auto res = fs::exists(archive)
            ? ArchiveManager::add_entries(archive, inputs, co)
            : ArchiveManager::create(archive, inputs, co);
        std::cout << "\n" << res.message << " (" << std::fixed << std::setprecision(2)
                  << res.duration_seconds << "s)\n";
        return res.success ? 0 : 1;
    }

    // -----------------------------------------------------------------------
    // x / e: Extract
    // -----------------------------------------------------------------------
    if (cmd == "x" || cmd == "e") {
        if (pos_args.empty()) {
            std::cerr << "Usage: openrar " << cmd << " [switches] <archive> [destination]\n";
            return 1;
        }
        fs::path archive = pos_args[0];
        eo.destination = pos_args.size() >= 2 ? fs::path(pos_args[1]) : fs::current_path();
        eo.preserve_paths = (cmd == "x");
        eo.progress = [](const ProgressInfo& pi) {
            std::cout << "\r[" << std::fixed << std::setprecision(1) << pi.percent() << "%] "
                      << pi.current_file << "          " << std::flush;
        };
        auto res = ArchiveManager::extract(archive, eo);
        std::cout << "\n" << res.message << " (" << std::fixed << std::setprecision(2)
                  << res.duration_seconds << "s)\n";
        for (const auto& e : res.errors) std::cerr << "  Error: " << e << "\n";
        return res.success ? 0 : 1;
    }

    // -----------------------------------------------------------------------
    // l: List
    // -----------------------------------------------------------------------
    if (cmd == "l") {
        if (pos_args.empty()) { std::cerr << "Usage: openrar l <archive>\n"; return 1; }
        std::vector<EntryInfo> entries;
        ArchiveInfo info;
        std::string err;
        if (!ArchiveManager::list(pos_args[0], entries, password, &info, &err)) {
            std::cerr << "Error: " << err << "\n";
            return 1;
        }
        std::cout << "Archive: " << path_to_utf8(info.file_path) << " [" << format_name(info.format) << "]\n";
        if (!info.comment.empty()) std::cout << "Comment: " << info.comment << "\n";
        std::cout << std::left << std::setw(12) << i18n::tr(i18n::StrKey::ColSize)
                  << std::setw(12) << i18n::tr(i18n::StrKey::ColPacked)
                  << std::setw(8) << "Ratio"
                  << std::setw(10) << i18n::tr(i18n::StrKey::ColCrc32)
                  << std::setw(22) << i18n::tr(i18n::StrKey::ColModified)
                  << i18n::tr(i18n::StrKey::ColName) << "\n";
        std::cout << std::string(80, '-') << "\n";
        for (const auto& e : entries) {
            std::ostringstream crc;
            if (!e.is_directory) crc << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << e.crc32;
            std::cout << std::left
                      << std::setw(12) << (e.is_directory ? "<DIR>" : std::to_string(e.size))
                      << std::setw(12) << (e.is_directory ? "" : std::to_string(e.compressed_size))
                      << std::setw(8)  << (e.is_directory ? "" : (std::to_string(static_cast<int>(e.ratio_percent())) + "%"))
                      << std::setw(10) << crc.str()
                      << std::setw(22) << format_timestamp(e.modified_time)
                      << e.full_path << (e.is_encrypted ? " *" : "") << "\n";
        }
        std::cout << std::string(80, '-') << "\n";
        std::cout << info.file_count << " file(s), " << info.folder_count << " folder(s), "
                  << format_bytes(info.total_uncompressed_size) << " -> " << format_bytes(info.archive_size)
                  << " (" << std::fixed << std::setprecision(1) << info.overall_ratio_percent() << "%)\n";
        return 0;
    }

    // -----------------------------------------------------------------------
    // t: Test
    // -----------------------------------------------------------------------
    if (cmd == "t") {
        if (pos_args.empty()) { std::cerr << "Usage: openrar t <archive>\n"; return 1; }
        auto res = ArchiveManager::test(pos_args[0], password);
        std::cout << res.message << "\n";
        for (const auto& e : res.errors) std::cerr << "  Error: " << e << "\n";
        return res.success ? 0 : 1;
    }

    // -----------------------------------------------------------------------
    // d: Delete entries
    // -----------------------------------------------------------------------
    if (cmd == "d") {
        if (pos_args.size() < 2) { std::cerr << "Usage: openrar d <archive> <entries...>\n"; return 1; }
        std::set<std::string> targets(pos_args.begin() + 1, pos_args.end());
        auto res = ArchiveManager::delete_entries(pos_args[0], targets, password);
        std::cout << res.message << "\n";
        return res.success ? 0 : 1;
    }

    // -----------------------------------------------------------------------
    // c: Comment
    // -----------------------------------------------------------------------
    if (cmd == "c") {
        if (pos_args.empty()) { std::cerr << "Usage: openrar c <archive> [new_comment]\n"; return 1; }
        if (pos_args.size() == 1) {
            std::vector<EntryInfo> entries;
            ArchiveInfo info;
            std::string err;
            if (!ArchiveManager::list(pos_args[0], entries, password, &info, &err)) {
                std::cerr << "Error: " << err << "\n";
                return 1;
            }
            std::cout << (info.comment.empty() ? "(No comment)" : info.comment) << "\n";
            return 0;
        }
        auto res = ArchiveManager::set_comment(pos_args[0], pos_args[1], password);
        std::cout << res.message << "\n";
        return res.success ? 0 : 1;
    }

    // -----------------------------------------------------------------------
    // r: Repair
    // -----------------------------------------------------------------------
    if (cmd == "r") {
        if (pos_args.empty()) { std::cerr << "Usage: openrar r <corrupted_archive> [repaired_output]\n"; return 1; }
        fs::path src = pos_args[0];
        fs::path dst = pos_args.size() >= 2
            ? fs::path(pos_args[1])
            : (src.parent_path() / path_from_utf8("rebuilt." + path_to_utf8(src.filename())));
        auto res = ArchiveManager::repair(src, dst);
        std::cout << res.message << "\n";
        if (res.success) std::cout << "Output: " << path_to_utf8(dst) << "\n";
        return res.success ? 0 : 1;
    }

    // -----------------------------------------------------------------------
    // f: Find files
    // -----------------------------------------------------------------------
    if (cmd == "f") {
        if (pos_args.size() < 2) { std::cerr << "Usage: openrar f <root_dir> <pattern> [text]\n"; return 1; }
        SearchOptions so;
        so.root_path = pos_args[0];
        so.filename_pattern = pos_args[1];
        if (pos_args.size() >= 3) so.text_to_find = pos_args[2];
        so.password = password;
        auto matches = ArchiveManager::find_files(so);
        for (const auto& m : matches) {
            if (m.is_in_archive()) {
                std::cout << "[ARCHIVE] " << path_to_utf8(m.archive_path) << " -> " << m.entry_path;
            } else {
                std::cout << "[FILE]    " << path_to_utf8(m.file_path);
            }
            if (!m.matched_line.empty()) std::cout << " | " << m.matched_line;
            std::cout << "\n";
        }
        std::cout << "Found " << matches.size() << " match(es).\n";
        return 0;
    }

    // -----------------------------------------------------------------------
    // cv: Convert archives
    // -----------------------------------------------------------------------
    if (cmd == "cv") {
        if (pos_args.size() < 3) {
            std::cerr << "Usage: openrar cv <zip|orar|tar|tar.gz> <output_dir> <archives...>\n";
            return 1;
        }
        std::string fmt_str = pos_args[0];
        co.format = detect_format_from_path("dummy." + fmt_str);
        if (co.format == ArchiveFormat::Unknown) {
            std::cerr << "Unknown target format: " << fmt_str << "\n";
            return 1;
        }
        fs::path out_dir = pos_args[1];
        std::vector<fs::path> sources(pos_args.begin() + 2, pos_args.end());
        auto res = ArchiveManager::convert_archives(sources, out_dir, co, password);
        std::cout << res.message << "\n";
        for (const auto& e : res.errors) std::cerr << "  Error: " << e << "\n";
        return res.success ? 0 : 1;
    }

    // -----------------------------------------------------------------------
    // sfx: Create Self-Extracting Archive
    // -----------------------------------------------------------------------
    if (cmd == "sfx") {
        if (pos_args.empty()) { std::cerr << "Usage: openrar sfx <archive> [output_sfx] [stub_path]\n"; return 1; }
        fs::path arc = pos_args[0];
#ifdef _WIN32
        fs::path out = pos_args.size() >= 2 ? fs::path(pos_args[1]) : arc.parent_path() / path_from_utf8(path_to_utf8(arc.stem()) + ".exe");
        fs::path default_stub = fs::path(argv[0]).parent_path() / "openrar_sfx_stub.exe";
#else
        fs::path out = pos_args.size() >= 2 ? fs::path(pos_args[1]) : arc.parent_path() / path_from_utf8(path_to_utf8(arc.stem()) + ".sfx");
        fs::path default_stub = fs::path(argv[0]).parent_path() / "openrar_sfx_stub";
#endif
        fs::path stub = pos_args.size() >= 3 ? fs::path(pos_args[2]) : default_stub;
        auto res = ArchiveManager::create_sfx(arc, stub, out);
        std::cout << res.message << "\n";
        return res.success ? 0 : 1;
    }

    // -----------------------------------------------------------------------
    // b: Benchmark
    // -----------------------------------------------------------------------
    if (cmd == "b") {
        unsigned int threads = pos_args.size() >= 1 ? static_cast<unsigned int>(std::stoul(pos_args[0])) : 0;
        double secs = pos_args.size() >= 2 ? std::stod(pos_args[1]) : 2.0;
        std::cout << "Running OpenRaR multi-threaded benchmark...\n";
        auto bm = ArchiveManager::run_benchmark(threads, secs);
        std::cout << "Threads:           " << bm.threads << "\n"
                  << "Duration:          " << std::fixed << std::setprecision(2) << bm.duration_seconds << " s\n"
                  << "Processed:         " << format_bytes(bm.bytes_processed) << "\n"
                  << "Resulting speed:   " << std::fixed << std::setprecision(1) << bm.speed_kbps << " KB/s ("
                  << std::setprecision(2) << bm.speed_mbps << " MB/s)\n"
                  << "Compression ratio: " << std::setprecision(1) << bm.compression_ratio << "%\n";
        return 0;
    }

    // -----------------------------------------------------------------------
    // info: Detailed Archive Info
    // -----------------------------------------------------------------------
    if (cmd == "info") {
        if (pos_args.empty()) { std::cerr << "Usage: openrar info <archive>\n"; return 1; }
        std::vector<EntryInfo> entries;
        ArchiveInfo info;
        std::string err;
        if (!ArchiveManager::list(pos_args[0], entries, password, &info, &err)) {
            std::cerr << "Error: " << err << "\n";
            return 1;
        }
        std::cout << "File:              " << path_to_utf8(info.file_path) << "\n"
                  << "Format:            " << format_name(info.format) << "\n"
                  << "Method:            " << info.compression_method << "\n"
                  << "Files / Folders:   " << info.file_count << " / " << info.folder_count << "\n"
                  << "Uncompressed Size: " << format_bytes(info.total_uncompressed_size) << "\n"
                  << "Archive Size:      " << format_bytes(info.archive_size) << "\n"
                  << "Ratio:             " << std::fixed << std::setprecision(1) << info.overall_ratio_percent() << "%\n"
                  << "Solid:             " << (info.is_solid ? "Yes" : "No") << "\n"
                  << "Encrypted:         " << (info.is_encrypted ? "Yes" : "No")
                  << (info.has_encrypted_headers ? " (Headers encrypted)" : "") << "\n"
                  << "Recovery Record:   " << (info.has_recovery_record ? ("Yes (" + std::to_string(info.recovery_percent) + "%)") : "No") << "\n"
                  << "Multi-Volume:      " << (info.is_multi_volume ? ("Yes (" + std::to_string(info.volume_count) + " volumes)") : "No") << "\n";
        if (!info.comment.empty()) std::cout << "Comment:           " << info.comment << "\n";
        return 0;
    }

    // -----------------------------------------------------------------------
    // lang: Show supported languages & active locale
    // -----------------------------------------------------------------------
    if (cmd == "lang") {
        i18n::Language cur = i18n::current_language();
        std::cout << tr_cli(i18n::StrKey::MenuLanguage) << " (" << i18n::language_code(cur) << "):\n";
        for (const auto& info : i18n::available_languages()) {
            std::cout << (info.id == cur ? "  * " : "    ")
                      << std::left << std::setw(5) << info.code
                      << info.native_name << "\n";
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // install-menu / uninstall-menu: Windows Explorer right-click submenu
    // -----------------------------------------------------------------------
    if (cmd == "install-menu") {
        fs::path self_dir = fs::absolute(fs::path(argv[0])).parent_path();
        fs::path gui = self_dir / "OpenRaR-GUI.exe";
        fs::path cli = self_dir / "openrar.exe";
        std::string err;
        if (!ArchiveManager::register_shell_context_menu(gui, cli, &err)) {
            std::cerr << "Error: " << err << "\n";
            return 1;
        }
        std::cout << i18n::tr(i18n::StrKey::MsgShellRegOk) << "\n";
        return 0;
    }
    if (cmd == "uninstall-menu") {
        ArchiveManager::unregister_shell_context_menu();
        std::cout << i18n::tr(i18n::StrKey::MsgShellUnregOk) << "\n";
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    print_usage();
    return 1;
}
