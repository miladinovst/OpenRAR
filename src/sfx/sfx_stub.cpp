// Standalone Self-Extracting Archive (SFX) executable stub.
// Locates the 24-byte "ORARSFX1" trailer appended to its own executable binary,
// extracts the embedded archive payload to a temp file, and unpacks it to disk.

#include "openrar/archive_manager.hpp"

#include <chrono>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <vector>
#else
#include <unistd.h>
#endif

using namespace openrar;

static fs::path get_self_executable_path(const char* argv0) {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n > 0) return fs::path(buf);
#elif defined(__APPLE__)
    uint32_t sz = 0;
    _NSGetExecutablePath(nullptr, &sz);
    if (sz > 0) {
        std::vector<char> buf(sz + 1, '\0');
        if (_NSGetExecutablePath(buf.data(), &sz) == 0) return fs::path(buf.data());
    }
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return fs::path(buf);
    }
#endif
    return argv0 ? path_from_utf8(argv0) : fs::path{};
}

int main(int argc, char* argv[]) {
    fs::path self_path = get_self_executable_path(argc > 0 ? argv[0] : nullptr);
    auto self_bytes = read_file_bytes(self_path);
    if (self_bytes.size() < kSfxTrailerSize) {
        std::cerr << "OpenRAR SFX: Not a valid self-extracting archive.\n";
        return 1;
    }

    const uint8_t* trailer = self_bytes.data() + self_bytes.size() - kSfxTrailerSize;
    if (std::memcmp(trailer + 16, kSfxMagic, 8) != 0) {
        std::cerr << "OpenRAR SFX: No embedded archive payload found.\n";
        return 1;
    }

    uint64_t arc_offset = 0, arc_size = 0;
    for (int i = 0; i < 8; ++i) {
        arc_offset |= static_cast<uint64_t>(trailer[i]) << (i * 8);
        arc_size   |= static_cast<uint64_t>(trailer[8 + i]) << (i * 8);
    }
    if (arc_offset + arc_size > self_bytes.size() - kSfxTrailerSize) {
        std::cerr << "OpenRAR SFX: Embedded archive trailer is corrupted.\n";
        return 1;
    }

    fs::path dest = fs::current_path();
    std::string password;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.starts_with("-p")) {
            password = arg.substr(2);
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "OpenRAR Self-Extracting Archive\n"
                      << "Usage: " << path_to_utf8(self_path.filename()) << " [destination_dir] [-p<password>]\n";
            return 0;
        } else {
            dest = path_from_utf8(arg);
        }
    }

    std::error_code ec;
    fs::path tmp_arc = fs::temp_directory_path(ec) /
        ("openrar_sfx_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    if (!write_file_bytes(tmp_arc, self_bytes.data() + arc_offset, static_cast<size_t>(arc_size))) {
        std::cerr << "OpenRAR SFX: Failed creating temporary extraction buffer.\n";
        return 1;
    }

    ExtractOptions eo;
    eo.destination = dest;
    eo.password = password;
    eo.overwrite = OverwriteMode::OverwriteAll;
    auto res = ArchiveManager::extract(tmp_arc, eo);
    fs::remove(tmp_arc, ec);

    if (!res.success) {
        std::cerr << "OpenRAR SFX Error: " << res.message << "\n";
        for (const auto& e : res.errors) std::cerr << "  - " << e << "\n";
        return 1;
    }
    std::cout << "OpenRAR SFX: " << res.message << " -> " << path_to_utf8(dest) << "\n";
    return 0;
}
