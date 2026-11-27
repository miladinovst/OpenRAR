#pragma once

#include "openrar/types.hpp"

#include <cstdint>
#include <regex>
#include <string>
#include <vector>

namespace openrar::bin {

// Little-endian readers
inline uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t read_u64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<uint64_t>(p[i]) << (i * 8);
    }
    return v;
}

// Little-endian writers
inline void write_u16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

inline void write_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

inline void write_u64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
}

// Reassembles multi-volume archives named `<base>.part1.<ext>`, `<base>.part2.<ext>`, ...
// Falls back to reading `archive_path` as a single volume if not multi-volume.
inline std::vector<uint8_t> load_split_volumes(const fs::path& archive_path,
                                               const std::string& ext_without_dot,
                                               size_t* volume_count_out = nullptr) {
    std::string fname = archive_path.filename().string();
    std::regex part_re("^(.+)\\.part(\\d+)\\." + ext_without_dot + "$", std::regex::icase);
    std::smatch m;
    if (std::regex_match(fname, m, part_re)) {
        std::string base = m[1].str();
        fs::path dir = archive_path.parent_path();
        std::vector<uint8_t> combined;
        size_t vols = 0;
        for (size_t idx = 1;; ++idx) {
            fs::path vp = dir / (base + ".part" + std::to_string(idx) + "." + ext_without_dot);
            std::error_code ec;
            if (!fs::exists(vp, ec)) break;
            auto chunk = read_file_bytes(vp);
            combined.insert(combined.end(), chunk.begin(), chunk.end());
            vols++;
        }
        if (volume_count_out) *volume_count_out = vols;
        if (!combined.empty()) return combined;
    }
    if (volume_count_out) *volume_count_out = 1;
    return read_file_bytes(archive_path);
}

} // namespace openrar::bin
