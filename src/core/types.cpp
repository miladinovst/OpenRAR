#include "openrar/types.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace openrar {

static std::string to_lower_ascii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

ArchiveFormat detect_format_from_path(const fs::path& path) {
    std::string lower = to_lower_ascii(path_to_utf8(path.filename()));
    if (lower.ends_with(".tar.gz") || lower.ends_with(".tgz")) return ArchiveFormat::TarGz;
    if (lower.ends_with(".tar.bz2") || lower.ends_with(".tbz2")) return ArchiveFormat::TarBz2;
    if (lower.ends_with(".tar.xz") || lower.ends_with(".txz")) return ArchiveFormat::TarXz;
    if (lower.ends_with(".tar.zst") || lower.ends_with(".tzst")) return ArchiveFormat::TarZst;
    if (lower.ends_with(".orar")) return ArchiveFormat::Orar;
    if (lower.ends_with(".zip") || lower.ends_with(".jar") || lower.ends_with(".apk") || lower.ends_with(".cbz"))
        return ArchiveFormat::Zip;
    if (lower.ends_with(".tar")) return ArchiveFormat::Tar;
    if (lower.ends_with(".gz")) return ArchiveFormat::GZip;
    if (lower.ends_with(".rar") || lower.ends_with(".cbr")) return ArchiveFormat::Rar;
    if (lower.ends_with(".7z")) return ArchiveFormat::SevenZip;
    if (lower.ends_with(".cab")) return ArchiveFormat::Cab;
    if (lower.ends_with(".iso")) return ArchiveFormat::Iso;
    return ArchiveFormat::Unknown;
}

ArchiveFormat detect_format_from_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (f) {
        uint8_t magic[8] = {};
        f.read(reinterpret_cast<char*>(magic), sizeof(magic));
        auto n = f.gcount();
        if (n >= 6 && magic[0] == 'O' && magic[1] == 'R' && magic[2] == 'A' && magic[3] == 'R') {
            return ArchiveFormat::Orar;
        }
        if (n >= 4 && magic[0] == 'P' && magic[1] == 'K' &&
            ((magic[2] == 0x03 && magic[3] == 0x04) || (magic[2] == 0x05 && magic[3] == 0x06))) {
            return ArchiveFormat::Zip;
        }
        if (n >= 7 && magic[0] == 'R' && magic[1] == 'a' && magic[2] == 'r' && magic[3] == '!' &&
            magic[4] == 0x1A && magic[5] == 0x07) {
            return ArchiveFormat::Rar;
        }
        if (n >= 6 && magic[0] == '7' && magic[1] == 'z' && magic[2] == 0xBC && magic[3] == 0xAF &&
            magic[4] == 0x27 && magic[5] == 0x1C) {
            return ArchiveFormat::SevenZip;
        }
        if (n >= 4 && magic[0] == 'M' && magic[1] == 'S' && magic[2] == 'C' && magic[3] == 'F') {
            return ArchiveFormat::Cab;
        }
        if (n >= 2 && magic[0] == 0x1F && magic[1] == 0x8B) {
            auto by_ext = detect_format_from_path(path);
            return (by_ext == ArchiveFormat::TarGz) ? ArchiveFormat::TarGz : ArchiveFormat::GZip;
        }
    }
    return detect_format_from_path(path);
}

bool is_archive_path(const fs::path& path) {
    return detect_format_from_path(path) != ArchiveFormat::Unknown;
}

std::string format_name(ArchiveFormat fmt) {
    switch (fmt) {
        case ArchiveFormat::Zip:      return "ZIP";
        case ArchiveFormat::Orar:  return "OpenRAR (.orar)";
        case ArchiveFormat::Tar:      return "TAR";
        case ArchiveFormat::TarGz:    return "TAR.GZ";
        case ArchiveFormat::GZip:     return "GZIP";
        case ArchiveFormat::Rar:      return "RAR";
        case ArchiveFormat::SevenZip: return "7-Zip (.7z)";
        case ArchiveFormat::TarBz2:   return "TAR.BZ2";
        case ArchiveFormat::TarXz:    return "TAR.XZ";
        case ArchiveFormat::TarZst:   return "TAR.ZST";
        case ArchiveFormat::Cab:      return "CAB";
        case ArchiveFormat::Iso:      return "ISO";
        default:                      return "Unknown";
    }
}

std::string default_extension(ArchiveFormat fmt) {
    switch (fmt) {
        case ArchiveFormat::Zip:      return ".zip";
        case ArchiveFormat::Orar:  return ".orar";
        case ArchiveFormat::Tar:      return ".tar";
        case ArchiveFormat::TarGz:    return ".tar.gz";
        case ArchiveFormat::GZip:     return ".gz";
        case ArchiveFormat::Rar:      return ".rar";
        case ArchiveFormat::SevenZip: return ".7z";
        case ArchiveFormat::TarBz2:   return ".tar.bz2";
        case ArchiveFormat::TarXz:    return ".tar.xz";
        case ArchiveFormat::TarZst:   return ".tar.zst";
        case ArchiveFormat::Cab:      return ".cab";
        case ArchiveFormat::Iso:      return ".iso";
        default:                      return ".zip";
    }
}

std::string format_bytes(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    std::ostringstream oss;
    if (u == 0) {
        oss << bytes << " B";
    } else {
        oss << std::fixed << std::setprecision(2) << v << " " << units[u];
    }
    return oss.str();
}

std::string format_timestamp(int64_t unix_time) {
    if (unix_time <= 0) return "";
    std::time_t t = static_cast<std::time_t>(unix_time);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

std::string normalize_entry_path(const std::string& path) {
    std::string out = path;
    std::replace(out.begin(), out.end(), '\\', '/');
    while (!out.empty() && out.front() == '/') out.erase(out.begin());
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

bool is_safe_relative_path(const std::string& path) {
    if (path.empty()) return false;
    if (path[0] == '/' || path[0] == '\\') return false;
    if (path.size() >= 2 && path[1] == ':') return false;
    std::string norm = path;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    std::stringstream ss(norm);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (part == "..") return false;
    }
    return true;
}

bool wildcard_match(const std::string& text, const std::string& pattern, bool case_sensitive) {
    std::string t = case_sensitive ? text : to_lower_ascii(text);
    std::string p = case_sensitive ? pattern : to_lower_ascii(pattern);
    size_t ti = 0, pi = 0, star = std::string::npos, match = 0;
    while (ti < t.size()) {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == t[ti])) {
            ++ti; ++pi;
        } else if (pi < p.size() && p[pi] == '*') {
            star = pi++;
            match = ti;
        } else if (star != std::string::npos) {
            pi = star + 1;
            ti = ++match;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '*') ++pi;
    return pi == p.size();
}

int64_t file_time_to_unix(const fs::file_time_type& ft) {
    using namespace std::chrono;
    auto sys_now = system_clock::now();
    auto file_now = fs::file_time_type::clock::now();
    auto sctp = time_point_cast<system_clock::duration>(ft - file_now + sys_now);
    return duration_cast<seconds>(sctp.time_since_epoch()).count();
}

fs::file_time_type unix_to_file_time(int64_t unix_time) {
    using namespace std::chrono;
    auto sys_now = system_clock::now();
    auto file_now = fs::file_time_type::clock::now();
    auto target_sys = system_clock::time_point(seconds(unix_time));
    return time_point_cast<fs::file_time_type::duration>(target_sys - sys_now + file_now);
}

std::string path_to_utf8(const fs::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

fs::path path_from_utf8(const std::string& utf8) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::vector<uint8_t> read_file_bytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = f.tellg();
    if (sz <= 0) return {};
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

bool write_file_bytes(const fs::path& p, const uint8_t* data, size_t len) {
    std::error_code ec;
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path(), ec);
    }
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (len > 0) {
        f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
    }
    return f.good();
}

uint64_t parse_size_string(const std::string& s) {
    if (s.empty()) return 0;
    std::string t = to_lower_ascii(s);
    size_t idx = 0;
    double val = 0.0;
    try {
        val = std::stod(t, &idx);
    } catch (...) {
        return 0;
    }
    if (val <= 0) return 0;
    std::string suffix = t.substr(idx);
    while (!suffix.empty() && std::isspace(static_cast<unsigned char>(suffix.front()))) {
        suffix.erase(suffix.begin());
    }
    uint64_t mult = 1;
    if (suffix == "k" || suffix == "kb") mult = 1024ULL;
    else if (suffix == "m" || suffix == "mb") mult = 1024ULL * 1024ULL;
    else if (suffix == "g" || suffix == "gb") mult = 1024ULL * 1024ULL * 1024ULL;
    return static_cast<uint64_t>(val * static_cast<double>(mult));
}

} // namespace openrar
