// Zero-dependency RAR4 and RAR5 binary header parser.
// Reads archive flags (solid, multi-volume, recovery, encrypted headers) and file table
// entries directly from container blocks without invoking external tools.

#include "openrar/rar_inspector.hpp"
#include "openrar/binary_io.hpp"

#include <cstring>

namespace openrar {

namespace {

constexpr uint8_t kRar4Magic[7] = {'R', 'a', 'r', '!', 0x1A, 0x07, 0x00};
constexpr uint8_t kRar5Magic[8] = {'R', 'a', 'r', '!', 0x1A, 0x07, 0x01, 0x00};

inline uint16_t rd16(const uint8_t* p) { return bin::read_u16(p); }
inline uint32_t rd32(const uint8_t* p) { return bin::read_u32(p); }

bool read_vint(const std::vector<uint8_t>& buf, size_t& pos, uint64_t& out) {
    out = 0;
    int shift = 0;
    while (pos < buf.size() && shift < 64) {
        uint8_t b = buf[pos++];
        out |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) return true;
        shift += 7;
    }
    return false;
}

} // namespace

bool RarInspector::list(const fs::path& archive_path, std::vector<EntryInfo>& entries,
                        ArchiveInfo* info, std::string* err) {
    entries.clear();
    auto buf = read_file_bytes(archive_path);
    if (buf.size() < 8) {
        if (err) *err = "File too small to be a RAR archive";
        return false;
    }

    ArchiveInfo ai;
    ai.file_path = archive_path;
    ai.format = ArchiveFormat::Rar;
    ai.archive_size = buf.size();

    if (std::memcmp(buf.data(), kRar5Magic, 8) == 0) {
        ai.compression_method = "RAR5";
        size_t pos = 8;
        while (pos + 4 < buf.size()) {
            pos += 4; // header CRC32
            uint64_t hdr_size = 0;
            if (!read_vint(buf, pos, hdr_size)) break;
            size_t hdr_start = pos;
            size_t hdr_end = hdr_start + static_cast<size_t>(hdr_size);
            if (hdr_end > buf.size()) break;

            uint64_t hdr_type = 0, hdr_flags = 0;
            if (!read_vint(buf, pos, hdr_type) || !read_vint(buf, pos, hdr_flags)) break;

            uint64_t extra_size = 0, data_size = 0;
            if (hdr_flags & 0x0001) read_vint(buf, pos, extra_size);
            if (hdr_flags & 0x0002) read_vint(buf, pos, data_size);

            if (hdr_type == 1) { // Main archive header
                uint64_t arc_flags = 0;
                if (read_vint(buf, pos, arc_flags)) {
                    ai.is_multi_volume = (arc_flags & 0x0001) != 0;
                    ai.is_solid = (arc_flags & 0x0004) != 0;
                    ai.has_recovery_record = (arc_flags & 0x0008) != 0;
                }
            } else if (hdr_type == 2) { // File header
                uint64_t fflags = 0, unp_size = 0, attrs = 0, mtime = 0, crc = 0, comp_info = 0, host_os = 0, name_len = 0;
                read_vint(buf, pos, fflags);
                read_vint(buf, pos, unp_size);
                read_vint(buf, pos, attrs);
                if (fflags & 0x0002) {
                    if (pos + 4 <= hdr_end) { mtime = rd32(buf.data() + pos); pos += 4; }
                }
                if (fflags & 0x0004) {
                    if (pos + 4 <= hdr_end) { crc = rd32(buf.data() + pos); pos += 4; }
                }
                read_vint(buf, pos, comp_info);
                read_vint(buf, pos, host_os);
                read_vint(buf, pos, name_len);
                if (pos + name_len <= hdr_end) {
                    std::string name(reinterpret_cast<const char*>(buf.data() + pos), static_cast<size_t>(name_len));
                    EntryInfo e;
                    e.full_path = normalize_entry_path(name);
                    auto slash = e.full_path.rfind('/');
                    e.name = (slash != std::string::npos) ? e.full_path.substr(slash + 1) : e.full_path;
                    e.directory = (slash != std::string::npos) ? e.full_path.substr(0, slash) : "";
                    e.is_directory = (fflags & 0x0001) != 0;
                    e.size = unp_size;
                    e.compressed_size = data_size;
                    e.crc32 = static_cast<uint32_t>(crc);
                    e.modified_time = static_cast<int64_t>(mtime);
                    e.method = "RAR5";
                    if (e.is_directory) ai.folder_count++;
                    else { ai.file_count++; ai.total_uncompressed_size += e.size; ai.total_compressed_size += e.compressed_size; }
                    entries.push_back(std::move(e));
                }
            } else if (hdr_type == 4) {
                ai.is_encrypted = true;
                ai.has_encrypted_headers = true;
                break;
            } else if (hdr_type == 5) {
                break; // End of archive
            }

            pos = hdr_end + static_cast<size_t>(data_size);
        }
        if (info) *info = std::move(ai);
        return true;
    }

    if (std::memcmp(buf.data(), kRar4Magic, 7) == 0) {
        ai.compression_method = "RAR4";
        size_t pos = 7;
        while (pos + 7 <= buf.size()) {
            uint8_t htype = buf[pos + 2];
            uint16_t hflags = rd16(buf.data() + pos + 3);
            uint16_t hsize = rd16(buf.data() + pos + 5);
            if (hsize < 7 || pos + hsize > buf.size()) break;

            uint32_t add_size = 0;
            if ((hflags & 0x8000) && pos + 11 <= buf.size()) {
                add_size = rd32(buf.data() + pos + 7);
            }

            if (htype == 0x73) { // Main header
                ai.is_multi_volume = (hflags & 0x0001) != 0;
                ai.is_solid = (hflags & 0x0008) != 0;
                ai.has_recovery_record = (hflags & 0x0040) != 0;
                if (hflags & 0x0080) { ai.is_encrypted = true; ai.has_encrypted_headers = true; break; }
            } else if (htype == 0x74 && pos + 32 <= buf.size()) { // File header
                uint32_t pack_sz = rd32(buf.data() + pos + 7);
                uint32_t unp_sz  = rd32(buf.data() + pos + 11);
                uint32_t crc     = rd32(buf.data() + pos + 16);
                uint16_t nlen    = rd16(buf.data() + pos + 26);
                if (pos + 32 + nlen <= buf.size()) {
                    std::string name(reinterpret_cast<const char*>(buf.data() + pos + 32), nlen);
                    EntryInfo e;
                    e.full_path = normalize_entry_path(name);
                    auto slash = e.full_path.rfind('/');
                    e.name = (slash != std::string::npos) ? e.full_path.substr(slash + 1) : e.full_path;
                    e.directory = (slash != std::string::npos) ? e.full_path.substr(0, slash) : "";
                    e.is_directory = ((hflags & 0x00E0) == 0x00E0);
                    e.is_encrypted = (hflags & 0x0004) != 0;
                    e.size = unp_sz;
                    e.compressed_size = pack_sz;
                    e.crc32 = crc;
                    e.method = "RAR4";
                    if (e.is_encrypted) ai.is_encrypted = true;
                    if (e.is_directory) ai.folder_count++;
                    else { ai.file_count++; ai.total_uncompressed_size += e.size; ai.total_compressed_size += e.compressed_size; }
                    entries.push_back(std::move(e));
                }
                add_size = pack_sz;
            } else if (htype == 0x7B) {
                break; // End of archive
            }
            pos += hsize + add_size;
        }
        if (info) *info = std::move(ai);
        return true;
    }

    if (err) *err = "Not a valid RAR4/RAR5 archive";
    return false;
}

} // namespace openrar
