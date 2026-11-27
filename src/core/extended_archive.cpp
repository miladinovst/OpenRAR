// Extended archive backend for RAR4/RAR5, 7z, TAR.XZ, TAR.BZ2, TAR.ZST, CAB, and ISO.
// Invokes Windows 11 built-in libarchive (tar.exe, CREATE_NO_WINDOW) or Linux bsdtar/tar,
// falling back to unrar/7z when extracting password-protected .rar or .7z archives.

#include "openrar/extended_archive.hpp"
#include "openrar/rar_inspector.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace openrar {

namespace {

struct ProcResult {
    int exit_code = -1;
    std::vector<uint8_t> stdout_bytes;
    std::string stderr_text;
};

#ifdef _WIN32
std::wstring utf8_to_wide_ext(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring quote_win_arg(const std::wstring& a) {
    if (a.empty()) return L"\"\"";
    if (a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    for (wchar_t c : a) {
        if (c == L'"') out += L"\\\"";
        else out.push_back(c);
    }
    out.push_back(L'"');
    return out;
}

ProcResult run_process(const fs::path& exe, const std::vector<std::string>& args,
                       const fs::path& cwd = {}) {
    ProcResult res;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out_rd = nullptr, out_wr = nullptr;
    HANDLE err_rd = nullptr, err_wr = nullptr;
    if (!CreatePipe(&out_rd, &out_wr, &sa, 0)) return res;
    if (!CreatePipe(&err_rd, &err_wr, &sa, 0)) {
        CloseHandle(out_rd); CloseHandle(out_wr);
        return res;
    }
    SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_rd, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmdline = quote_win_arg(exe.wstring());
    for (const auto& a : args) {
        cmdline += L" " + quote_win_arg(utf8_to_wide_ext(a));
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = out_wr;
    si.hStdError = err_wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring cwd_w = cwd.empty() ? L"" : cwd.wstring();
    BOOL ok = CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr,
                             cwd_w.empty() ? nullptr : cwd_w.c_str(), &si, &pi);
    CloseHandle(out_wr);
    CloseHandle(err_wr);

    if (!ok) {
        CloseHandle(out_rd);
        CloseHandle(err_rd);
        return res;
    }

    // Drain stdout and stderr
    uint8_t buf[8192];
    DWORD got = 0;
    while (ReadFile(out_rd, buf, sizeof(buf), &got, nullptr) && got > 0) {
        res.stdout_bytes.insert(res.stdout_bytes.end(), buf, buf + got);
    }
    while (ReadFile(err_rd, buf, sizeof(buf), &got, nullptr) && got > 0) {
        res.stderr_text.append(reinterpret_cast<const char*>(buf), got);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    res.exit_code = static_cast<int>(code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(out_rd);
    CloseHandle(err_rd);
    return res;
}

fs::path find_bsdtar() {
    fs::path sys_tar = "C:\\Windows\\System32\\tar.exe";
    std::error_code ec;
    if (fs::exists(sys_tar, ec)) return sys_tar;
    return "tar.exe";
}
#else
ProcResult run_process(const fs::path& exe, const std::vector<std::string>& args,
                       const fs::path& cwd = {}) {
    ProcResult res;
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0) return res;
    if (pipe(err_pipe) != 0) { close(out_pipe[0]); close(out_pipe[1]); return res; }

    pid_t pid = fork();
    if (pid == 0) {
        if (!cwd.empty()) { (void)chdir(cwd.c_str()); }
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);

        std::vector<char*> argv;
        std::string exe_s = exe.string();
        argv.push_back(exe_s.data());
        std::vector<std::string> copy = args;
        for (auto& a : copy) argv.push_back(a.data());
        argv.push_back(nullptr);
        execvp(exe_s.c_str(), argv.data());
        _exit(127);
    }
    close(out_pipe[1]);
    close(err_pipe[1]);
    uint8_t buf[8192];
    ssize_t n;
    while ((n = read(out_pipe[0], buf, sizeof(buf))) > 0) {
        res.stdout_bytes.insert(res.stdout_bytes.end(), buf, buf + n);
    }
    while ((n = read(err_pipe[0], buf, sizeof(buf))) > 0) {
        res.stderr_text.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
    }
    close(out_pipe[0]);
    close(err_pipe[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    res.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return res;
}

fs::path find_bsdtar() {
    if (fs::exists("/usr/bin/bsdtar")) return "/usr/bin/bsdtar";
    if (fs::exists("/usr/local/bin/bsdtar")) return "/usr/local/bin/bsdtar";
    if (fs::exists("/bin/bsdtar")) return "/bin/bsdtar";
#ifdef __APPLE__
    if (fs::exists("/usr/bin/tar")) return "/usr/bin/tar";
#endif
    return "bsdtar";
}
#endif

fs::path find_optional_tool(const std::string& name) {
#ifdef _WIN32
    std::vector<fs::path> candidates = {
        fs::path("C:\\Program Files\\7-Zip\\7z.exe"),
        fs::path("C:\\Program Files (x86)\\7-Zip\\7z.exe"),
        fs::path("C:\\Program Files\\WinRAR\\UnRAR.exe"),
        fs::path("C:\\Program Files\\WinRAR\\Rar.exe")
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (name == "7z" && c.filename() == "7z.exe" && fs::exists(c, ec)) return c;
        if (name == "unrar" && c.filename() == "UnRAR.exe" && fs::exists(c, ec)) return c;
        if (name == "rar" && c.filename() == "Rar.exe" && fs::exists(c, ec)) return c;
    }
    return name + ".exe";
#else
    for (const char* dir : {"/usr/bin/", "/usr/local/bin/", "/bin/"}) {
        fs::path p = std::string(dir) + name;
        std::error_code ec;
        if (fs::exists(p, ec)) return p;
    }
    return name;
#endif
}

// Parse `bsdtar -tvf` output lines:
// e.g. "-rw-r--r--  0 0      0        1234 Nov 27 12:00 path/to/file.txt"
void parse_bsdtar_listing(const std::string& text, ArchiveFormat fmt,
                          std::vector<EntryInfo>& entries, ArchiveInfo& ai) {
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.size() < 20) continue;

        // Tokenize first 8 fields: perms, links, owner, group, size, mon, day, time/year, rest=name
        std::istringstream ls(line);
        std::string perms, links, owner, group, sz_str, mon, day, tm_or_yr;
        if (!(ls >> perms >> links >> owner >> group >> sz_str >> mon >> day >> tm_or_yr)) continue;
        std::string rest;
        std::getline(ls, rest);
        size_t first_ns = rest.find_first_not_of(" \t");
        if (first_ns == std::string::npos) continue;
        std::string path = normalize_entry_path(rest.substr(first_ns));
        if (path.empty()) continue;

        EntryInfo e;
        e.is_directory = (!perms.empty() && perms[0] == 'd');
        e.full_path = path;
        auto slash = path.rfind('/');
        if (slash != std::string::npos) {
            e.directory = path.substr(0, slash);
            e.name = path.substr(slash + 1);
        } else {
            e.name = path;
        }
        try { e.size = std::stoull(sz_str); } catch (...) { e.size = 0; }
        e.compressed_size = e.size;
        e.method = format_name(fmt);

        if (e.is_directory) ai.folder_count++;
        else {
            ai.file_count++;
            ai.total_uncompressed_size += e.size;
        }
        entries.push_back(std::move(e));
    }
}

} // namespace

bool ExtendedArchive::is_available() {
    auto r = run_process(find_bsdtar(), {"--version"});
    if (r.exit_code != 0) return false;
    std::string out(reinterpret_cast<const char*>(r.stdout_bytes.data()), r.stdout_bytes.size());
    return out.find("libarchive") != std::string::npos || out.find("bsdtar") != std::string::npos;
}

bool ExtendedArchive::list(const fs::path& archive_path, ArchiveFormat fmt,
                           std::vector<EntryInfo>& entries,
                           ArchiveInfo* info, std::string* err) {
    entries.clear();

    // For RAR, try our fast native RAR4/RAR5 binary header parser first (gives CRC32, solid, recovery, encryption!)
    if (fmt == ArchiveFormat::Rar) {
        if (RarInspector::list(archive_path, entries, info, nullptr) && !entries.empty()) {
            return true;
        }
        entries.clear();
    }

    auto r = run_process(find_bsdtar(), {"-tvf", archive_path.string()});
    if (r.exit_code != 0) {
        if (err) *err = r.stderr_text.empty() ? ("Failed to list " + format_name(fmt) + " archive") : r.stderr_text;
        return false;
    }

    std::error_code ec;
    ArchiveInfo ai;
    ai.file_path = archive_path;
    ai.format = fmt;
    ai.archive_size = fs::file_size(archive_path, ec);
    ai.total_compressed_size = ai.archive_size;
    ai.compression_method = format_name(fmt) + " (libarchive)";

    std::string out_txt(reinterpret_cast<const char*>(r.stdout_bytes.data()), r.stdout_bytes.size());
    parse_bsdtar_listing(out_txt, fmt, entries, ai);
    if (info) *info = std::move(ai);
    return true;
}

OperationResult ExtendedArchive::extract(const fs::path& archive_path, ArchiveFormat fmt,
                                         const ExtractOptions& options) {
    auto t0 = std::chrono::steady_clock::now();
    std::error_code ec;
    fs::create_directories(options.destination, ec);

    // If a password is provided for .rar or .7z, try unrar / 7z first (bsdtar doesn't accept passwords)
    if (!options.password.empty() && (fmt == ArchiveFormat::Rar || fmt == ArchiveFormat::SevenZip)) {
        if (fmt == ArchiveFormat::Rar) {
            auto ur = run_process(find_optional_tool("unrar"),
                                  {"x", "-p" + options.password,
                                   options.overwrite == OverwriteMode::SkipExisting ? "-o-" : "-o+",
                                   archive_path.string(), options.destination.string() + "/"});
            if (ur.exit_code == 0) {
                double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                return OperationResult::ok("Extracted encrypted RAR archive", 1, 0, secs);
            }
        }
        auto z7 = run_process(find_optional_tool("7z"),
                              {"x", "-p" + options.password,
                               options.overwrite == OverwriteMode::SkipExisting ? "-aos" : "-aoa",
                               "-o" + options.destination.string(), archive_path.string()});
        if (z7.exit_code == 0) {
            double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            return OperationResult::ok("Extracted encrypted " + format_name(fmt) + " archive", 1, 0, secs);
        }
    }

    std::vector<std::string> args = {"-xf", archive_path.string(), "-C", options.destination.string()};
    if (options.overwrite == OverwriteMode::SkipExisting) {
        args.push_back("-k"); // keep old files
    }
    if (!options.selected_entries.empty()) {
        for (const auto& e : options.selected_entries) {
            args.push_back(e);
        }
    }

    auto r = run_process(find_bsdtar(), args);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (r.exit_code != 0) {
        std::string msg = r.stderr_text.empty()
            ? ("Failed extracting " + format_name(fmt) + " archive")
            : r.stderr_text;
        return OperationResult::fail(msg);
    }

    // Count extracted files for reporting
    size_t count = 0;
    if (!options.selected_entries.empty()) {
        count = options.selected_entries.size();
    } else {
        std::vector<EntryInfo> entries;
        if (list(archive_path, fmt, entries)) {
            for (const auto& e : entries) if (!e.is_directory) count++;
        }
    }
    return OperationResult::ok("Extracted " + std::to_string(count) + " file(s) from " + format_name(fmt),
                               count, 0, secs);
}

bool ExtendedArchive::extract_entry_to_memory(const fs::path& archive_path, ArchiveFormat,
                                              const std::string& entry_path,
                                              const std::string&,
                                              std::vector<uint8_t>& out,
                                              std::string* err) {
    auto r = run_process(find_bsdtar(), {"-xOf", archive_path.string(), entry_path});
    if (r.exit_code != 0) {
        if (err) *err = r.stderr_text.empty() ? ("Failed extracting entry: " + entry_path) : r.stderr_text;
        return false;
    }
    out = std::move(r.stdout_bytes);
    return true;
}

OperationResult ExtendedArchive::test(const fs::path& archive_path, ArchiveFormat fmt, const std::string&) {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<EntryInfo> entries;
    std::string err;
    if (!list(archive_path, fmt, entries, nullptr, &err)) {
        return OperationResult::fail(err);
    }
    // Verify full decompression by streaming archive contents to stdout via -xOf
    auto r = run_process(find_bsdtar(), {"-xOf", archive_path.string()});
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (r.exit_code != 0) {
        return OperationResult::fail(r.stderr_text.empty() ? "Archive integrity test failed" : r.stderr_text);
    }
    size_t fcnt = 0;
    for (const auto& e : entries) if (!e.is_directory) fcnt++;
    return OperationResult::ok("All " + std::to_string(fcnt) + " file(s) passed " + format_name(fmt) + " integrity test (OK)",
                               fcnt, r.stdout_bytes.size(), secs);
}

OperationResult ExtendedArchive::create_7z(const fs::path& archive_path,
                                           const std::vector<fs::path>& inputs,
                                           const CreateOptions& options) {
    auto t0 = std::chrono::steady_clock::now();
    if (inputs.empty()) return OperationResult::fail("No inputs to archive");

    std::error_code ec;
    fs::path base = options.base_dir.empty() ? fs::absolute(inputs[0], ec).parent_path() : fs::absolute(options.base_dir, ec);
    fs::path abs_out = fs::absolute(archive_path, ec);

    std::vector<std::string> args = {"-a", "-cf", abs_out.string()};
    for (const auto& inp : inputs) {
        fs::path rel = fs::relative(fs::absolute(inp, ec), base, ec);
        args.push_back(rel.generic_string());
    }

    auto r = run_process(find_bsdtar(), args, base);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (r.exit_code != 0) {
        return OperationResult::fail(r.stderr_text.empty() ? "Failed creating 7z archive" : r.stderr_text);
    }
    auto res = OperationResult::ok("Created 7-Zip (.7z) archive", inputs.size(), 0, secs);
    res.output_files.push_back(abs_out);
    return res;
}

OperationResult ExtendedArchive::create_rar(const fs::path& archive_path,
                                            const std::vector<fs::path>& inputs,
                                            const CreateOptions& options) {
    auto t0 = std::chrono::steady_clock::now();
    if (inputs.empty()) return OperationResult::fail("No inputs to archive");

    fs::path rar_exe = find_optional_tool("rar");
    std::error_code ec;
    fs::path base = options.base_dir.empty() ? fs::absolute(inputs[0], ec).parent_path() : fs::absolute(options.base_dir, ec);
    fs::path abs_out = fs::absolute(archive_path, ec);

    std::vector<std::string> args = {"a", "-r", "-m" + std::to_string(static_cast<int>(options.level))};
    if (options.solid) args.push_back("-s");
    if (options.add_recovery_record) args.push_back("-rr" + std::to_string(options.recovery_percent) + "p");
    if (!options.password.empty()) {
        args.push_back((options.encrypt_headers ? "-hp" : "-p") + options.password);
    }
    if (options.split_volume_bytes > 0) {
        args.push_back("-v" + std::to_string(options.split_volume_bytes) + "b");
    }
    args.push_back(abs_out.string());
    for (const auto& inp : inputs) {
        fs::path rel = fs::relative(fs::absolute(inp, ec), base, ec);
        args.push_back(rel.generic_string());
    }

    auto r = run_process(rar_exe, args, base);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (r.exit_code != 0) {
        return OperationResult::fail(
            "Creating .rar archives requires the proprietary RAR command-line tool (rar.exe / rar) to be installed on your system. "
            "OpenRaR natively creates .orar (with solid, AES-256-GCM, recovery records, and split volumes), .zip, .7z, and .tar.gz without extra tools.");
    }
    auto res = OperationResult::ok("Created RAR archive", inputs.size(), 0, secs);
    res.output_files.push_back(abs_out);
    return res;
}

} // namespace openrar
