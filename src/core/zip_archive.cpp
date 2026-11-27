// PKWARE ZIP and ZIP64 archive implementation.
// Supports Store/Deflate, standard AE-2 AES-256 (PBKDF2-HMAC-SHA1 + HMAC-SHA1-80),
// legacy ZipCrypto, UTF-8 names, parallel per-file compression, split volumes,
// EOCD comments, and Local File Header (PK\x03\x04) salvage repair.

#include "openrar/zip_archive.hpp"
#include "openrar/binary_io.hpp"
#include "openrar/crypto.hpp"
#include "openrar/deflate.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <thread>

namespace openrar {

namespace {

constexpr uint32_t kSigLocalHeader   = 0x04034B50;
constexpr uint32_t kSigCentralDir    = 0x02014B50;
constexpr uint32_t kSigEocd          = 0x06054B50;
constexpr uint32_t kSigZip64Eocd     = 0x06064B50;
constexpr uint32_t kSigZip64Locator  = 0x07064B50;
constexpr uint16_t kExtraZip64Id     = 0x0001;
constexpr uint16_t kExtraAesId       = 0x9901;

inline uint16_t rd16(const uint8_t* p) { return bin::read_u16(p); }
inline uint32_t rd32(const uint8_t* p) { return bin::read_u32(p); }
inline uint64_t rd64(const uint8_t* p) { return bin::read_u64(p); }
inline void wr16(std::vector<uint8_t>& b, uint16_t v) { bin::write_u16(b, v); }
inline void wr32(std::vector<uint8_t>& b, uint32_t v) { bin::write_u32(b, v); }
inline void wr64(std::vector<uint8_t>& b, uint64_t v) { bin::write_u64(b, v); }

std::vector<uint8_t> load_zip_volumes(const fs::path& archive_path, size_t* vol_count_out = nullptr) {
    return bin::load_split_volumes(archive_path, "zip", vol_count_out);
}

void unix_to_dos_datetime(int64_t unix_time, uint16_t& dos_time, uint16_t& dos_date) {
    std::time_t t = static_cast<std::time_t>(unix_time > 0 ? unix_time : std::time(nullptr));
    std::tm tm_val{};
#ifdef _WIN32
    localtime_s(&tm_val, &t);
#else
    localtime_r(&t, &tm_val);
#endif
    int year = std::clamp(tm_val.tm_year + 1900, 1980, 2107);
    dos_date = static_cast<uint16_t>(((year - 1980) << 9) | ((tm_val.tm_mon + 1) << 5) | tm_val.tm_mday);
    dos_time = static_cast<uint16_t>((tm_val.tm_hour << 11) | (tm_val.tm_min << 5) | (tm_val.tm_sec / 2));
}

int64_t dos_datetime_to_unix(uint16_t dos_time, uint16_t dos_date) {
    std::tm tm_val{};
    tm_val.tm_year = ((dos_date >> 9) & 0x7F) + 80;
    tm_val.tm_mon  = ((dos_date >> 5) & 0x0F) - 1;
    tm_val.tm_mday = (dos_date & 0x1F);
    tm_val.tm_hour = (dos_time >> 11) & 0x1F;
    tm_val.tm_min  = (dos_time >> 5) & 0x3F;
    tm_val.tm_sec  = (dos_time & 0x1F) * 2;
    tm_val.tm_isdst = -1;
    return static_cast<int64_t>(std::mktime(&tm_val));
}

struct ZipRecord {
    std::string name;
    uint16_t flags = 0;
    uint16_t method = 0;
    uint16_t actual_method = 0;
    uint16_t dos_time = 0;
    uint16_t dos_date = 0;
    uint32_t crc32 = 0;
    uint64_t comp_size = 0;
    uint64_t uncomp_size = 0;
    uint64_t local_header_offset = 0;
    uint32_t external_attr = 0;
    bool is_aes = false;
    uint8_t aes_strength = 3;
    std::string comment;
};

bool find_eocd(const std::vector<uint8_t>& buf, size_t& eocd_pos) {
    if (buf.size() < 22) return false;
    size_t min_pos = buf.size() > 65557 ? buf.size() - 65557 : 0;
    for (size_t i = buf.size() - 22;; --i) {
        if (rd32(buf.data() + i) == kSigEocd) {
            uint16_t comment_len = rd16(buf.data() + i + 20);
            if (i + 22 + comment_len == buf.size()) {
                eocd_pos = i;
                return true;
            }
        }
        if (i == min_pos) break;
    }
    return false;
}

void parse_extra_fields(const uint8_t* extra, size_t len, ZipRecord& rec) {
    size_t pos = 0;
    while (pos + 4 <= len) {
        uint16_t id = rd16(extra + pos);
        uint16_t sz = rd16(extra + pos + 2);
        pos += 4;
        if (pos + sz > len) break;
        if (id == kExtraAesId && sz >= 7) {
            rec.is_aes = true;
            rec.aes_strength = extra[pos + 4];
            rec.actual_method = rd16(extra + pos + 5);
        } else if (id == kExtraZip64Id) {
            size_t zp = pos;
            size_t zend = pos + sz;
            if (rec.uncomp_size == 0xFFFFFFFFULL && zp + 8 <= zend) { rec.uncomp_size = rd64(extra + zp); zp += 8; }
            if (rec.comp_size == 0xFFFFFFFFULL && zp + 8 <= zend) { rec.comp_size = rd64(extra + zp); zp += 8; }
            if (rec.local_header_offset == 0xFFFFFFFFULL && zp + 8 <= zend) { rec.local_header_offset = rd64(extra + zp); zp += 8; }
        }
        pos += sz;
    }
}

bool parse_zip(const std::vector<uint8_t>& buf, std::vector<ZipRecord>& records,
               std::string& archive_comment, std::string* err) {
    size_t eocd_pos = 0;
    if (!find_eocd(buf, eocd_pos)) {
        if (err) *err = "Not a valid ZIP archive (End of Central Directory not found)";
        return false;
    }

    uint64_t entry_count = rd16(buf.data() + eocd_pos + 10);
    uint64_t cd_size     = rd32(buf.data() + eocd_pos + 12);
    uint64_t cd_offset   = rd32(buf.data() + eocd_pos + 16);
    uint16_t comment_len = rd16(buf.data() + eocd_pos + 20);
    archive_comment.assign(reinterpret_cast<const char*>(buf.data() + eocd_pos + 22), comment_len);

    // Check for ZIP64 EOCD Locator immediately preceding EOCD
    if ((entry_count == 0xFFFF || cd_size == 0xFFFFFFFFULL || cd_offset == 0xFFFFFFFFULL) &&
        eocd_pos >= 20 && rd32(buf.data() + eocd_pos - 20) == kSigZip64Locator) {
        uint64_t z64_off = rd64(buf.data() + eocd_pos - 12);
        if (z64_off + 56 <= buf.size() && rd32(buf.data() + z64_off) == kSigZip64Eocd) {
            entry_count = rd64(buf.data() + z64_off + 32);
            cd_size     = rd64(buf.data() + z64_off + 40);
            cd_offset   = rd64(buf.data() + z64_off + 48);
        }
    }

    if (cd_offset + cd_size > buf.size()) {
        if (err) *err = "Corrupted ZIP central directory bounds";
        return false;
    }

    size_t pos = static_cast<size_t>(cd_offset);
    records.reserve(static_cast<size_t>(std::min<uint64_t>(entry_count, 1000000ULL)));
    for (uint64_t i = 0; i < entry_count; ++i) {
        if (pos + 46 > buf.size() || rd32(buf.data() + pos) != kSigCentralDir) {
            if (err) *err = "Invalid Central Directory header";
            return false;
        }
        ZipRecord r;
        r.flags       = rd16(buf.data() + pos + 8);
        r.method      = rd16(buf.data() + pos + 10);
        r.actual_method = r.method;
        r.dos_time    = rd16(buf.data() + pos + 12);
        r.dos_date    = rd16(buf.data() + pos + 14);
        r.crc32       = rd32(buf.data() + pos + 16);
        r.comp_size   = rd32(buf.data() + pos + 20);
        r.uncomp_size = rd32(buf.data() + pos + 24);
        uint16_t name_len    = rd16(buf.data() + pos + 28);
        uint16_t extra_len   = rd16(buf.data() + pos + 30);
        uint16_t cmt_len     = rd16(buf.data() + pos + 32);
        r.external_attr      = rd32(buf.data() + pos + 38);
        r.local_header_offset = rd32(buf.data() + pos + 42);

        pos += 46;
        if (pos + name_len + extra_len + cmt_len > buf.size()) {
            if (err) *err = "Truncated Central Directory entry";
            return false;
        }
        r.name.assign(reinterpret_cast<const char*>(buf.data() + pos), name_len);
        if (extra_len > 0) {
            parse_extra_fields(buf.data() + pos + name_len, extra_len, r);
        }
        if (cmt_len > 0) {
            r.comment.assign(reinterpret_cast<const char*>(buf.data() + pos + name_len + extra_len), cmt_len);
        }
        pos += name_len + extra_len + cmt_len;
        records.push_back(std::move(r));
    }
    return true;
}

bool extract_record_data(const std::vector<uint8_t>& buf, const ZipRecord& r,
                         const std::string& password, std::vector<uint8_t>& out, std::string* err) {
    if (r.local_header_offset + 30 > buf.size() ||
        rd32(buf.data() + r.local_header_offset) != kSigLocalHeader) {
        if (err) *err = "Invalid local file header for " + r.name;
        return false;
    }
    uint16_t l_name_len  = rd16(buf.data() + r.local_header_offset + 26);
    uint16_t l_extra_len = rd16(buf.data() + r.local_header_offset + 28);
    size_t data_pos = static_cast<size_t>(r.local_header_offset) + 30 + l_name_len + l_extra_len;
    if (data_pos + r.comp_size > buf.size()) {
        if (err) *err = "Truncated entry payload for " + r.name;
        return false;
    }

    const uint8_t* raw = buf.data() + data_pos;
    size_t raw_len = r.comp_size;
    std::vector<uint8_t> decrypted;
    uint16_t comp_method = r.method;

    bool encrypted = (r.flags & 0x0001) != 0;
    if (encrypted) {
        if (password.empty()) {
            if (err) *err = "Password required for encrypted entry: " + r.name;
            return false;
        }
        if (r.is_aes && r.method == 99) {
            // Standard AE-2 AES-256: [16-byte salt][2-byte pw verify][ciphertext][10-byte HMAC-SHA1-80]
            if (raw_len < 16 + 2 + 10) {
                if (err) *err = "Truncated AES encrypted entry: " + r.name;
                return false;
            }
            const uint8_t* salt = raw;
            const uint8_t* pw_ver = raw + 16;
            size_t ct_len = raw_len - 16 - 2 - 10;
            const uint8_t* ct = raw + 18;
            const uint8_t* mac = raw + 18 + ct_len;

            auto dk = crypto::pbkdf2_hmac_sha1(password, salt, 16, 1000, 66);
            const uint8_t* aes_key = dk.data();
            const uint8_t* mac_key = dk.data() + 32;
            const uint8_t* expected_ver = dk.data() + 64;
            if (!crypto::constant_time_equal(pw_ver, expected_ver, 2)) {
                if (err) *err = "Wrong password for: " + r.name;
                return false;
            }
            auto calc_mac = crypto::hmac_sha1(mac_key, 32, ct, ct_len);
            if (!crypto::constant_time_equal(calc_mac.data(), mac, 10)) {
                if (err) *err = "Authentication failed (corrupted or wrong password) for: " + r.name;
                return false;
            }
            decrypted.assign(ct, ct + ct_len);
            crypto::aes256_ctr_crypt(aes_key, decrypted.data(), decrypted.size(), 1);
            raw = decrypted.data();
            raw_len = decrypted.size();
            comp_method = r.actual_method;
        } else {
            // Legacy PKWARE ZipCrypto
            if (raw_len < 12) {
                if (err) *err = "Truncated ZipCrypto header for: " + r.name;
                return false;
            }
            crypto::ZipCrypto zc(password);
            uint8_t hdr[12];
            for (int i = 0; i < 12; ++i) hdr[i] = zc.decrypt_byte(raw[i]);
            uint8_t check_byte = (r.flags & 0x0008)
                ? static_cast<uint8_t>((r.dos_time >> 8) & 0xFF)
                : static_cast<uint8_t>((r.crc32 >> 24) & 0xFF);
            if (hdr[11] != check_byte) {
                if (err) *err = "Wrong password for: " + r.name;
                return false;
            }
            decrypted.resize(raw_len - 12);
            for (size_t i = 0; i < decrypted.size(); ++i) {
                decrypted[i] = zc.decrypt_byte(raw[12 + i]);
            }
            raw = decrypted.data();
            raw_len = decrypted.size();
        }
    }

    if (comp_method == 0) {
        out.assign(raw, raw + raw_len);
    } else if (comp_method == 8) {
        if (!codec::deflate_decompress(raw, raw_len, out, err)) {
            return false;
        }
    } else {
        if (err) *err = "Unsupported ZIP compression method (" + std::to_string(comp_method) + ") for " + r.name;
        return false;
    }

    if (!r.is_aes || r.crc32 != 0) {
        uint32_t actual_crc = crypto::crc32(out);
        if (actual_crc != r.crc32) {
            if (err) *err = "CRC32 mismatch for " + r.name;
            return false;
        }
    }
    return true;
}

struct PendingEntry {
    std::string archive_name;
    bool is_directory = false;
    std::vector<uint8_t> raw_data;
    int64_t mtime = 0;
};

void collect_inputs(const std::vector<fs::path>& inputs, const fs::path& base_dir,
                    std::vector<PendingEntry>& out) {
    for (const auto& inp : inputs) {
        std::error_code ec;
        if (!fs::exists(inp, ec)) continue;
        fs::path abs_inp = fs::absolute(inp, ec);
        fs::path root = base_dir.empty() ? abs_inp.parent_path() : fs::absolute(base_dir, ec);

        if (fs::is_directory(abs_inp, ec)) {
            std::string dir_rel = normalize_entry_path(path_to_utf8(fs::relative(abs_inp, root, ec)));
            if (!dir_rel.empty() && dir_rel != ".") {
                PendingEntry de;
                de.archive_name = dir_rel + "/";
                de.is_directory = true;
                de.mtime = file_time_to_unix(fs::last_write_time(abs_inp, ec));
                out.push_back(std::move(de));
            }
            for (const auto& it : fs::recursive_directory_iterator(abs_inp, ec)) {
                std::string rel = normalize_entry_path(path_to_utf8(fs::relative(it.path(), root, ec)));
                if (rel.empty() || rel == ".") continue;
                PendingEntry pe;
                pe.is_directory = it.is_directory(ec);
                pe.archive_name = pe.is_directory ? (rel + "/") : rel;
                pe.mtime = file_time_to_unix(it.last_write_time(ec));
                if (!pe.is_directory) {
                    pe.raw_data = read_file_bytes(it.path());
                }
                out.push_back(std::move(pe));
            }
        } else {
            std::string rel = normalize_entry_path(path_to_utf8(fs::relative(abs_inp, root, ec)));
            if (rel.empty() || rel.find("..") == 0) rel = path_to_utf8(abs_inp.filename());
            PendingEntry pe;
            pe.archive_name = rel;
            pe.is_directory = false;
            pe.mtime = file_time_to_unix(fs::last_write_time(abs_inp, ec));
            pe.raw_data = read_file_bytes(abs_inp);
            out.push_back(std::move(pe));
        }
    }
}

struct WrittenRecord {
    std::string name;
    uint16_t flags = 0;
    uint16_t method = 0;
    uint16_t dos_time = 0;
    uint16_t dos_date = 0;
    uint32_t crc32 = 0;
    uint64_t comp_size = 0;
    uint64_t uncomp_size = 0;
    uint64_t local_offset = 0;
    uint32_t ext_attr = 0;
    std::vector<uint8_t> extra;
    std::string comment;
};

std::vector<uint8_t> build_zip_bytes(const std::vector<PendingEntry>& entries,
                                     const CreateOptions& options,
                                     size_t& files_done,
                                     uint64_t& bytes_done) {
    std::vector<uint8_t> out;
    std::vector<WrittenRecord> records;
    records.reserve(entries.size());

    uint64_t total_bytes = 0;
    for (const auto& e : entries) total_bytes += e.raw_data.size();

    auto t0 = std::chrono::steady_clock::now();
    struct PreparedZipEntry {
        WrittenRecord wr;
        std::vector<uint8_t> payload;
        uint64_t raw_size = 0;
    };
    std::vector<PreparedZipEntry> prepared(entries.size());

    unsigned int num_threads = std::min<unsigned int>(
        std::max<unsigned int>(1, std::thread::hardware_concurrency()),
        static_cast<unsigned int>(std::max<size_t>(1, entries.size())));
    std::atomic<size_t> next_idx{0};

    auto worker = [&] {
        while (true) {
            if (options.cancel_flag && options.cancel_flag->load()) return;
            size_t i = next_idx.fetch_add(1, std::memory_order_relaxed);
            if (i >= entries.size()) return;
            const auto& e = entries[i];

            WrittenRecord wr;
            wr.name = e.archive_name;
            wr.flags = 0x0800; // UTF-8
            unix_to_dos_datetime(e.mtime, wr.dos_time, wr.dos_date);
            wr.uncomp_size = static_cast<uint32_t>(e.raw_data.size());
            wr.crc32 = e.is_directory ? 0 : crypto::crc32(e.raw_data);
            wr.ext_attr = e.is_directory ? 0x10U : 0x20U;

            std::vector<uint8_t> payload;
            uint16_t actual_method = 0;
            if (!e.is_directory) {
                if (options.level == CompressionLevel::Store || e.raw_data.empty()) {
                    actual_method = 0;
                    payload = e.raw_data;
                } else {
                    auto deflated = codec::deflate_compress(e.raw_data, options.level);
                    if (deflated.size() < e.raw_data.size()) {
                        actual_method = 8;
                        payload = std::move(deflated);
                    } else {
                        actual_method = 0;
                        payload = e.raw_data;
                    }
                }
            }

            if (!options.password.empty() && !e.is_directory) {
                wr.flags |= 0x0001;
                if (options.use_aes256) {
                    wr.method = 99;
                    wr.crc32 = 0; // Per AE-2 spec, CRC32 field is 0 (HMAC-SHA1-80 authenticates data)
                    wr16(wr.extra, kExtraAesId);
                    wr16(wr.extra, 7);
                    wr16(wr.extra, 2); // AE-2
                    wr.extra.push_back('A');
                    wr.extra.push_back('E');
                    wr.extra.push_back(3); // AES-256
                    wr16(wr.extra, actual_method);

                    auto salt = crypto::random_bytes(16);
                    auto dk = crypto::pbkdf2_hmac_sha1(options.password, salt.data(), 16, 1000, 66);
                    const uint8_t* aes_key = dk.data();
                    const uint8_t* mac_key = dk.data() + 32;
                    const uint8_t* pw_ver  = dk.data() + 64;

                    std::vector<uint8_t> ct = payload;
                    crypto::aes256_ctr_crypt(aes_key, ct.data(), ct.size(), 1);
                    auto mac = crypto::hmac_sha1(mac_key, 32, ct.data(), ct.size());

                    payload.clear();
                    payload.reserve(16 + 2 + ct.size() + 10);
                    payload.insert(payload.end(), salt.begin(), salt.end());
                    payload.push_back(pw_ver[0]);
                    payload.push_back(pw_ver[1]);
                    payload.insert(payload.end(), ct.begin(), ct.end());
                    payload.insert(payload.end(), mac.begin(), mac.begin() + 10);
                } else {
                    wr.method = actual_method;
                    crypto::ZipCrypto zc(options.password);
                    auto hdr = crypto::random_bytes(12);
                    hdr[11] = static_cast<uint8_t>((wr.crc32 >> 24) & 0xFF);
                    std::vector<uint8_t> enc;
                    enc.reserve(12 + payload.size());
                    for (uint8_t b : hdr) enc.push_back(zc.encrypt_byte(b));
                    for (uint8_t b : payload) enc.push_back(zc.encrypt_byte(b));
                    payload = std::move(enc);
                }
            } else {
                wr.method = actual_method;
            }
            wr.comp_size = static_cast<uint32_t>(payload.size());
            prepared[i] = {std::move(wr), std::move(payload), e.raw_data.size()};
        }
    };

    if (num_threads <= 1 || entries.size() < 4) {
        worker();
    } else {
        std::vector<std::thread> pool;
        pool.reserve(num_threads);
        for (unsigned int t = 0; t < num_threads; ++t) pool.emplace_back(worker);
        for (auto& th : pool) th.join();
    }
    if (options.cancel_flag && options.cancel_flag->load()) return {};

    for (size_t i = 0; i < prepared.size(); ++i) {
        auto& wr = prepared[i].wr;
        auto& payload = prepared[i].payload;
        wr.local_offset = out.size();

        bool need_z64_local = (wr.comp_size >= 0xFFFFFFFFULL || wr.uncomp_size >= 0xFFFFFFFFULL);
        std::vector<uint8_t> local_extra = wr.extra;
        if (need_z64_local) {
            wr16(local_extra, kExtraZip64Id);
            wr16(local_extra, 16);
            wr64(local_extra, wr.uncomp_size);
            wr64(local_extra, wr.comp_size);
        }

        // Local File Header
        wr32(out, kSigLocalHeader);
        wr16(out, wr.method == 99 ? 51 : (need_z64_local ? 45 : 20));
        wr16(out, wr.flags);
        wr16(out, wr.method);
        wr16(out, wr.dos_time);
        wr16(out, wr.dos_date);
        wr32(out, wr.crc32);
        wr32(out, need_z64_local ? 0xFFFFFFFFU : static_cast<uint32_t>(wr.comp_size));
        wr32(out, need_z64_local ? 0xFFFFFFFFU : static_cast<uint32_t>(wr.uncomp_size));
        wr16(out, static_cast<uint16_t>(wr.name.size()));
        wr16(out, static_cast<uint16_t>(local_extra.size()));
        out.insert(out.end(), wr.name.begin(), wr.name.end());
        out.insert(out.end(), local_extra.begin(), local_extra.end());
        out.insert(out.end(), payload.begin(), payload.end());

        files_done++;
        bytes_done += prepared[i].raw_size;
        records.push_back(std::move(wr));

        if (options.progress) {
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            ProgressInfo pi;
            pi.operation = "Compressing (ZIP)";
            pi.current_file = records.back().name;
            pi.bytes_processed = bytes_done;
            pi.total_bytes = total_bytes;
            pi.files_processed = files_done;
            pi.total_files = entries.size();
            pi.speed_bps = elapsed > 0.001 ? static_cast<double>(bytes_done) / elapsed : 0.0;
            options.progress(pi);
        }
    }

    // Central Directory
    uint64_t cd_offset = out.size();
    bool need_zip64_eocd = (records.size() >= 0xFFFF || cd_offset >= 0xFFFFFFFFULL);
    for (const auto& wr : records) {
        bool need_u = wr.uncomp_size >= 0xFFFFFFFFULL;
        bool need_c = wr.comp_size >= 0xFFFFFFFFULL;
        bool need_o = wr.local_offset >= 0xFFFFFFFFULL;
        std::vector<uint8_t> cd_extra = wr.extra;
        if (need_u || need_c || need_o) {
            need_zip64_eocd = true;
            uint16_t zlen = static_cast<uint16_t>((need_u ? 8 : 0) + (need_c ? 8 : 0) + (need_o ? 8 : 0));
            wr16(cd_extra, kExtraZip64Id);
            wr16(cd_extra, zlen);
            if (need_u) wr64(cd_extra, wr.uncomp_size);
            if (need_c) wr64(cd_extra, wr.comp_size);
            if (need_o) wr64(cd_extra, wr.local_offset);
        }

        wr32(out, kSigCentralDir);
        wr16(out, 0x032D); // Unix / version 4.5
        wr16(out, wr.method == 99 ? 51 : ((need_u || need_c || need_o) ? 45 : 20));
        wr16(out, wr.flags);
        wr16(out, wr.method);
        wr16(out, wr.dos_time);
        wr16(out, wr.dos_date);
        wr32(out, wr.crc32);
        wr32(out, need_c ? 0xFFFFFFFFU : static_cast<uint32_t>(wr.comp_size));
        wr32(out, need_u ? 0xFFFFFFFFU : static_cast<uint32_t>(wr.uncomp_size));
        wr16(out, static_cast<uint16_t>(wr.name.size()));
        wr16(out, static_cast<uint16_t>(cd_extra.size()));
        wr16(out, static_cast<uint16_t>(wr.comment.size()));
        wr16(out, 0);
        wr16(out, 0);
        wr32(out, wr.ext_attr);
        wr32(out, need_o ? 0xFFFFFFFFU : static_cast<uint32_t>(wr.local_offset));
        out.insert(out.end(), wr.name.begin(), wr.name.end());
        out.insert(out.end(), cd_extra.begin(), cd_extra.end());
        out.insert(out.end(), wr.comment.begin(), wr.comment.end());
    }
    uint64_t cd_size = out.size() - cd_offset;
    if (cd_size >= 0xFFFFFFFFULL) need_zip64_eocd = true;

    if (need_zip64_eocd) {
        uint64_t z64_eocd_off = out.size();
        wr32(out, kSigZip64Eocd);
        wr64(out, 44);
        wr16(out, 0x032D);
        wr16(out, 45);
        wr32(out, 0);
        wr32(out, 0);
        wr64(out, records.size());
        wr64(out, records.size());
        wr64(out, cd_size);
        wr64(out, cd_offset);

        wr32(out, kSigZip64Locator);
        wr32(out, 0);
        wr64(out, z64_eocd_off);
        wr32(out, 1);
    }

    // Standard EOCD
    wr32(out, kSigEocd);
    wr16(out, 0);
    wr16(out, 0);
    wr16(out, records.size() >= 0xFFFF ? 0xFFFF : static_cast<uint16_t>(records.size()));
    wr16(out, records.size() >= 0xFFFF ? 0xFFFF : static_cast<uint16_t>(records.size()));
    wr32(out, cd_size >= 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<uint32_t>(cd_size));
    wr32(out, cd_offset >= 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<uint32_t>(cd_offset));
    wr16(out, static_cast<uint16_t>(options.comment.size()));
    out.insert(out.end(), options.comment.begin(), options.comment.end());
    return out;
}

fs::path resolve_overwrite_path(const fs::path& target, OverwriteMode mode,
                                const std::function<OverwriteMode(const fs::path&)>& prompt,
                                bool& skip_out) {
    skip_out = false;
    std::error_code ec;
    if (!fs::exists(target, ec)) return target;

    OverwriteMode eff = mode;
    if (eff == OverwriteMode::Ask && prompt) {
        eff = prompt(target);
    }
    if (eff == OverwriteMode::SkipExisting) {
        skip_out = true;
        return target;
    }
    if (eff == OverwriteMode::RenameAuto) {
        fs::path dir = target.parent_path();
        std::string stem = path_to_utf8(target.stem());
        std::string ext = path_to_utf8(target.extension());
        for (int n = 1; n < 10000; ++n) {
            fs::path cand = dir / path_from_utf8(stem + " (" + std::to_string(n) + ")" + ext);
            if (!fs::exists(cand, ec)) return cand;
        }
    }
    return target;
}

} // namespace

bool ZipArchive::list(const fs::path& archive_path, std::vector<EntryInfo>& entries,
                      ArchiveInfo* info, std::string* err) {
    entries.clear();
    size_t vol_count = 1;
    auto buf = load_zip_volumes(archive_path, &vol_count);
    if (buf.empty()) {
        if (err) *err = "Failed to read ZIP file: " + path_to_utf8(archive_path);
        return false;
    }

    std::vector<ZipRecord> records;
    std::string comment;
    if (!parse_zip(buf, records, comment, err)) return false;

    ArchiveInfo ai;
    ai.file_path = archive_path;
    ai.format = ArchiveFormat::Zip;
    ai.archive_size = buf.size();
    ai.is_multi_volume = vol_count > 1;
    ai.volume_count = vol_count;
    ai.comment = comment;
    ai.compression_method = "Deflate / Store";

    for (const auto& r : records) {
        EntryInfo e;
        bool is_dir = (!r.name.empty() && r.name.back() == '/') || ((r.external_attr & 0x10) != 0);
        e.full_path = normalize_entry_path(r.name);
        auto slash = e.full_path.rfind('/');
        if (slash != std::string::npos) {
            e.directory = e.full_path.substr(0, slash);
            e.name = e.full_path.substr(slash + 1);
        } else {
            e.name = e.full_path;
        }
        e.size = r.uncomp_size;
        e.compressed_size = r.comp_size;
        e.crc32 = r.crc32;
        e.modified_time = dos_datetime_to_unix(r.dos_time, r.dos_date);
        e.is_directory = is_dir;
        e.is_encrypted = (r.flags & 0x0001) != 0;
        e.comment = r.comment;
        if (r.is_aes) e.method = (r.actual_method == 8) ? "AES-256 + Deflate" : "AES-256 + Store";
        else if (r.method == 8) e.method = "Deflate";
        else if (r.method == 0) e.method = "Store";
        else e.method = "Method " + std::to_string(r.method);

        if (e.is_encrypted) ai.is_encrypted = true;
        if (is_dir) ai.folder_count++;
        else {
            ai.file_count++;
            ai.total_uncompressed_size += e.size;
            ai.total_compressed_size += e.compressed_size;
        }
        entries.push_back(std::move(e));
    }
    if (info) *info = std::move(ai);
    return true;
}

OperationResult ZipArchive::create(const fs::path& archive_path,
                                   const std::vector<fs::path>& inputs,
                                   const CreateOptions& options) {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<PendingEntry> pending;
    collect_inputs(inputs, options.base_dir, pending);
    if (pending.empty()) {
        return OperationResult::fail("No input files found to archive");
    }

    size_t files_done = 0;
    uint64_t bytes_done = 0;
    auto zip_bytes = build_zip_bytes(pending, options, files_done, bytes_done);
    if (options.cancel_flag && options.cancel_flag->load()) {
        return OperationResult::fail("Operation cancelled by user");
    }

    OperationResult res;
    if (options.split_volume_bytes > 0 && zip_bytes.size() > options.split_volume_bytes) {
        fs::path dir = archive_path.parent_path();
        std::string stem = path_to_utf8(archive_path.stem());
        std::string ext = path_to_utf8(archive_path.extension());
        size_t part = 1;
        for (size_t pos = 0; pos < zip_bytes.size(); pos += static_cast<size_t>(options.split_volume_bytes), ++part) {
            size_t chunk = std::min<size_t>(static_cast<size_t>(options.split_volume_bytes), zip_bytes.size() - pos);
            fs::path vol_path = dir / path_from_utf8(stem + ".part" + std::to_string(part) + ext);
            if (!write_file_bytes(vol_path, zip_bytes.data() + pos, chunk)) {
                return OperationResult::fail("Failed writing volume: " + path_to_utf8(vol_path));
            }
            res.output_files.push_back(vol_path);
        }
    } else {
        if (!write_file_bytes(archive_path, zip_bytes.data(), zip_bytes.size())) {
            return OperationResult::fail("Failed writing ZIP archive: " + path_to_utf8(archive_path));
        }
        res.output_files.push_back(archive_path);
    }

    if (options.test_after && res.output_files.size() == 1) {
        auto tr = test(archive_path, options.password);
        if (!tr.success) return tr;
    }

    if (options.delete_after) {
        std::error_code ec;
        for (const auto& inp : inputs) {
            fs::remove_all(inp, ec);
        }
    }

    res.success = true;
    res.files_processed = files_done;
    res.bytes_processed = bytes_done;
    res.duration_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    res.message = "Created ZIP archive with " + std::to_string(files_done) + " item(s)";
    return res;
}

OperationResult ZipArchive::extract(const fs::path& archive_path, const ExtractOptions& options) {
    auto t0 = std::chrono::steady_clock::now();
    auto buf = load_zip_volumes(archive_path);
    if (buf.empty()) return OperationResult::fail("Cannot read ZIP archive: " + path_to_utf8(archive_path));

    std::vector<ZipRecord> records;
    std::string comment;
    std::string err;
    if (!parse_zip(buf, records, comment, &err)) return OperationResult::fail(err);

    std::error_code ec;
    fs::create_directories(options.destination, ec);

    uint64_t total_bytes = 0;
    for (const auto& r : records) total_bytes += r.uncomp_size;

    OperationResult res;
    size_t files_done = 0;
    uint64_t bytes_done = 0;

    for (const auto& r : records) {
        if (options.cancel_flag && options.cancel_flag->load()) {
            res.success = false;
            res.message = "Extraction cancelled by user";
            return res;
        }
        std::string norm = normalize_entry_path(r.name);
        if (!options.selected_entries.empty() && !options.selected_entries.count(norm)) continue;
        if (!is_safe_relative_path(norm)) {
            res.warnings.push_back("Skipped unsafe path: " + r.name);
            continue;
        }

        bool is_dir = (!r.name.empty() && r.name.back() == '/') || ((r.external_attr & 0x10) != 0);
        fs::path norm_fs = path_from_utf8(norm);
        fs::path rel_path = options.preserve_paths ? norm_fs : norm_fs.filename();
        fs::path target = options.destination / rel_path;

        if (is_dir) {
            if (options.preserve_paths) fs::create_directories(target, ec);
            continue;
        }

        if (options.update_mode == UpdateMode::FreshExistingOnly && !fs::exists(target, ec)) continue;
        if (options.update_mode == UpdateMode::ExtractAndUpdate && fs::exists(target, ec)) {
            int64_t disk_time = file_time_to_unix(fs::last_write_time(target, ec));
            int64_t arc_time = dos_datetime_to_unix(r.dos_time, r.dos_date);
            if (disk_time >= arc_time) continue;
        }

        bool skip = false;
        target = resolve_overwrite_path(target, options.overwrite, options.overwrite_prompt, skip);
        if (skip) continue;

        std::vector<uint8_t> data;
        std::string rec_err;
        if (!extract_record_data(buf, r, options.password, data, &rec_err)) {
            res.errors.push_back(rec_err);
            if (!options.keep_broken) continue;
        }

        if (!write_file_bytes(target, data.data(), data.size())) {
            res.errors.push_back("Failed to write: " + path_to_utf8(target));
            continue;
        }
        int64_t mtime = dos_datetime_to_unix(r.dos_time, r.dos_date);
        if (mtime > 0) fs::last_write_time(target, unix_to_file_time(mtime), ec);

        files_done++;
        bytes_done += data.size();
        if (options.progress) {
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            ProgressInfo pi;
            pi.operation = "Extracting (ZIP)";
            pi.current_file = norm;
            pi.bytes_processed = bytes_done;
            pi.total_bytes = total_bytes;
            pi.files_processed = files_done;
            pi.total_files = records.size();
            pi.speed_bps = elapsed > 0.001 ? static_cast<double>(bytes_done) / elapsed : 0.0;
            options.progress(pi);
        }
    }

    res.files_processed = files_done;
    res.bytes_processed = bytes_done;
    res.duration_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    res.success = res.errors.empty();
    res.message = res.success
        ? ("Extracted " + std::to_string(files_done) + " file(s)")
        : ("Extracted with " + std::to_string(res.errors.size()) + " error(s)");
    return res;
}

bool ZipArchive::extract_entry_to_memory(const fs::path& archive_path, const std::string& entry_path,
                                         const std::string& password, std::vector<uint8_t>& out,
                                         std::string* err) {
    auto buf = load_zip_volumes(archive_path);
    if (buf.empty()) { if (err) *err = "Cannot read ZIP"; return false; }
    std::vector<ZipRecord> records;
    std::string cmt;
    if (!parse_zip(buf, records, cmt, err)) return false;
    std::string target = normalize_entry_path(entry_path);
    for (const auto& r : records) {
        if (normalize_entry_path(r.name) == target) {
            return extract_record_data(buf, r, password, out, err);
        }
    }
    if (err) *err = "Entry not found: " + entry_path;
    return false;
}

OperationResult ZipArchive::test(const fs::path& archive_path, const std::string& password,
                                 ProgressCallback progress, std::atomic<bool>* cancel_flag) {
    auto t0 = std::chrono::steady_clock::now();
    auto buf = load_zip_volumes(archive_path);
    if (buf.empty()) return OperationResult::fail("Cannot read ZIP archive");
    std::vector<ZipRecord> records;
    std::string cmt, err;
    if (!parse_zip(buf, records, cmt, &err)) return OperationResult::fail(err);

    OperationResult res;
    uint64_t total_bytes = 0;
    for (const auto& r : records) total_bytes += r.uncomp_size;
    uint64_t bytes_done = 0;
    size_t files_done = 0;

    for (const auto& r : records) {
        if (cancel_flag && cancel_flag->load()) return OperationResult::fail("Cancelled");
        bool is_dir = (!r.name.empty() && r.name.back() == '/');
        if (is_dir) continue;
        std::vector<uint8_t> out;
        std::string rec_err;
        if (!extract_record_data(buf, r, password, out, &rec_err)) {
            res.errors.push_back(rec_err);
        } else {
            files_done++;
            bytes_done += out.size();
        }
        if (progress) {
            ProgressInfo pi;
            pi.operation = "Testing (ZIP)";
            pi.current_file = r.name;
            pi.bytes_processed = bytes_done;
            pi.total_bytes = total_bytes;
            pi.files_processed = files_done;
            pi.total_files = records.size();
            progress(pi);
        }
    }
    res.files_processed = files_done;
    res.bytes_processed = bytes_done;
    res.duration_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    res.success = res.errors.empty();
    res.message = res.success
        ? ("All " + std::to_string(files_done) + " file(s) passed integrity test (OK)")
        : ("Test failed with " + std::to_string(res.errors.size()) + " error(s)");
    return res;
}

OperationResult ZipArchive::add_entries(const fs::path& archive_path,
                                        const std::vector<fs::path>& inputs,
                                        const CreateOptions& options) {
    auto existing_buf = read_file_bytes(archive_path);
    std::vector<PendingEntry> all_entries;
    std::string old_comment;

    if (!existing_buf.empty()) {
        std::vector<ZipRecord> recs;
        std::string err;
        if (parse_zip(existing_buf, recs, old_comment, &err)) {
            for (const auto& r : recs) {
                PendingEntry pe;
                pe.archive_name = r.name;
                pe.is_directory = (!r.name.empty() && r.name.back() == '/');
                pe.mtime = dos_datetime_to_unix(r.dos_time, r.dos_date);
                if (!pe.is_directory) {
                    extract_record_data(existing_buf, r, options.password, pe.raw_data, nullptr);
                }
                all_entries.push_back(std::move(pe));
            }
        }
    }

    std::vector<PendingEntry> added;
    collect_inputs(inputs, options.base_dir, added);
    std::set<std::string> added_names;
    for (const auto& a : added) added_names.insert(normalize_entry_path(a.archive_name));

    std::vector<PendingEntry> merged;
    for (auto& e : all_entries) {
        if (!added_names.count(normalize_entry_path(e.archive_name))) {
            merged.push_back(std::move(e));
        }
    }
    for (auto& a : added) merged.push_back(std::move(a));

    CreateOptions opts = options;
    if (opts.comment.empty()) opts.comment = old_comment;
    size_t files_done = 0;
    uint64_t bytes_done = 0;
    auto zip_bytes = build_zip_bytes(merged, opts, files_done, bytes_done);
    if (!write_file_bytes(archive_path, zip_bytes.data(), zip_bytes.size())) {
        return OperationResult::fail("Failed writing updated ZIP");
    }
    return OperationResult::ok("Updated ZIP archive (" + std::to_string(files_done) + " items)", files_done, bytes_done);
}

OperationResult ZipArchive::delete_entries(const fs::path& archive_path,
                                           const std::set<std::string>& entries_to_delete) {
    auto buf = read_file_bytes(archive_path);
    if (buf.empty()) return OperationResult::fail("Cannot read ZIP archive");
    std::vector<ZipRecord> recs;
    std::string comment, err;
    if (!parse_zip(buf, recs, comment, &err)) return OperationResult::fail(err);

    // Rebuild ZIP keeping raw compressed local headers & payloads of non-deleted entries!
    std::vector<uint8_t> out;
    std::vector<ZipRecord> kept;
    size_t deleted = 0;

    for (auto r : recs) {
        std::string norm = normalize_entry_path(r.name);
        bool should_del = false;
        for (const auto& target : entries_to_delete) {
            std::string nt = normalize_entry_path(target);
            if (norm == nt || norm.starts_with(nt + "/")) {
                should_del = true;
                break;
            }
        }
        if (should_del) { deleted++; continue; }

        size_t loff = r.local_header_offset;
        if (loff + 30 > buf.size()) continue;
        uint16_t l_nlen = rd16(buf.data() + loff + 26);
        uint16_t l_elen = rd16(buf.data() + loff + 28);
        size_t total_local = 30 + l_nlen + l_elen + r.comp_size;
        if (loff + total_local > buf.size()) continue;

        r.local_header_offset = static_cast<uint32_t>(out.size());
        out.insert(out.end(), buf.begin() + static_cast<ptrdiff_t>(loff),
                   buf.begin() + static_cast<ptrdiff_t>(loff + total_local));
        kept.push_back(std::move(r));
    }

    uint32_t cd_offset = static_cast<uint32_t>(out.size());
    for (const auto& r : kept) {
        std::vector<uint8_t> extra;
        if (r.is_aes) {
            wr16(extra, kExtraAesId); wr16(extra, 7); wr16(extra, 2);
            extra.push_back('A'); extra.push_back('E'); extra.push_back(r.aes_strength);
            wr16(extra, r.actual_method);
        }
        wr32(out, kSigCentralDir);
        wr16(out, 0x0314);
        wr16(out, r.is_aes ? 51 : 20);
        wr16(out, r.flags);
        wr16(out, r.method);
        wr16(out, r.dos_time);
        wr16(out, r.dos_date);
        wr32(out, r.crc32);
        wr32(out, static_cast<uint32_t>(r.comp_size));
        wr32(out, static_cast<uint32_t>(r.uncomp_size));
        wr16(out, static_cast<uint16_t>(r.name.size()));
        wr16(out, static_cast<uint16_t>(extra.size()));
        wr16(out, static_cast<uint16_t>(r.comment.size()));
        wr16(out, 0); wr16(out, 0);
        wr32(out, r.external_attr);
        wr32(out, static_cast<uint32_t>(r.local_header_offset));
        out.insert(out.end(), r.name.begin(), r.name.end());
        out.insert(out.end(), extra.begin(), extra.end());
        out.insert(out.end(), r.comment.begin(), r.comment.end());
    }
    uint32_t cd_size = static_cast<uint32_t>(out.size()) - cd_offset;

    wr32(out, kSigEocd);
    wr16(out, 0); wr16(out, 0);
    wr16(out, static_cast<uint16_t>(kept.size()));
    wr16(out, static_cast<uint16_t>(kept.size()));
    wr32(out, cd_size);
    wr32(out, cd_offset);
    wr16(out, static_cast<uint16_t>(comment.size()));
    out.insert(out.end(), comment.begin(), comment.end());

    if (!write_file_bytes(archive_path, out.data(), out.size())) {
        return OperationResult::fail("Failed writing ZIP after deletion");
    }
    return OperationResult::ok("Deleted " + std::to_string(deleted) + " entry(ies)", deleted);
}

OperationResult ZipArchive::set_comment(const fs::path& archive_path, const std::string& comment) {
    auto buf = read_file_bytes(archive_path);
    if (buf.empty()) return OperationResult::fail("Cannot read ZIP archive");
    size_t eocd_pos = 0;
    if (!find_eocd(buf, eocd_pos)) return OperationResult::fail("Invalid ZIP archive");

    buf.resize(eocd_pos + 20);
    uint16_t len = static_cast<uint16_t>(std::min<size_t>(comment.size(), 65535));
    wr16(buf, len);
    buf.insert(buf.end(), comment.begin(), comment.begin() + len);
    if (!write_file_bytes(archive_path, buf.data(), buf.size())) {
        return OperationResult::fail("Failed updating ZIP comment");
    }
    return OperationResult::ok("Updated ZIP archive comment");
}

OperationResult ZipArchive::repair(const fs::path& corrupted_path, const fs::path& repaired_path) {
    auto buf = read_file_bytes(corrupted_path);
    if (buf.empty()) return OperationResult::fail("Cannot read corrupted ZIP file");

    // Scan for Local File Header signatures (PK\x03\x04) and salvage intact entries
    std::vector<uint8_t> out;
    std::vector<ZipRecord> salvaged;

    for (size_t pos = 0; pos + 30 <= buf.size(); ++pos) {
        if (rd32(buf.data() + pos) != kSigLocalHeader) continue;

        ZipRecord r;
        r.flags       = rd16(buf.data() + pos + 6);
        r.method      = rd16(buf.data() + pos + 8);
        r.dos_time    = rd16(buf.data() + pos + 10);
        r.dos_date    = rd16(buf.data() + pos + 12);
        r.crc32       = rd32(buf.data() + pos + 14);
        r.comp_size   = rd32(buf.data() + pos + 18);
        r.uncomp_size = rd32(buf.data() + pos + 22);
        uint16_t nlen = rd16(buf.data() + pos + 26);
        uint16_t elen = rd16(buf.data() + pos + 28);

        if (nlen == 0 || nlen > 4096) continue;
        size_t total = 30 + static_cast<size_t>(nlen) + elen + r.comp_size;
        if (pos + total > buf.size()) continue;

        r.name.assign(reinterpret_cast<const char*>(buf.data() + pos + 30), nlen);
        if (!is_safe_relative_path(normalize_entry_path(r.name))) continue;
        if (elen > 0) parse_extra_fields(buf.data() + pos + 30 + nlen, elen, r);

        r.local_header_offset = static_cast<uint32_t>(out.size());
        r.external_attr = (!r.name.empty() && r.name.back() == '/') ? 0x10U : 0x20U;
        out.insert(out.end(), buf.begin() + static_cast<ptrdiff_t>(pos),
                   buf.begin() + static_cast<ptrdiff_t>(pos + total));
        salvaged.push_back(std::move(r));
        pos += total - 1;
    }

    if (salvaged.empty()) {
        return OperationResult::fail("No salvageable ZIP entries found");
    }

    uint32_t cd_offset = static_cast<uint32_t>(out.size());
    for (const auto& r : salvaged) {
        wr32(out, kSigCentralDir);
        wr16(out, 0x0314);
        wr16(out, 20);
        wr16(out, r.flags);
        wr16(out, r.method);
        wr16(out, r.dos_time);
        wr16(out, r.dos_date);
        wr32(out, r.crc32);
        wr32(out, static_cast<uint32_t>(r.comp_size));
        wr32(out, static_cast<uint32_t>(r.uncomp_size));
        wr16(out, static_cast<uint16_t>(r.name.size()));
        wr16(out, 0);
        wr16(out, 0);
        wr16(out, 0); wr16(out, 0);
        wr32(out, r.external_attr);
        wr32(out, static_cast<uint32_t>(r.local_header_offset));
        out.insert(out.end(), r.name.begin(), r.name.end());
    }
    uint32_t cd_size = static_cast<uint32_t>(out.size()) - cd_offset;
    wr32(out, kSigEocd);
    wr16(out, 0); wr16(out, 0);
    wr16(out, static_cast<uint16_t>(salvaged.size()));
    wr16(out, static_cast<uint16_t>(salvaged.size()));
    wr32(out, cd_size);
    wr32(out, cd_offset);
    wr16(out, 0);

    if (!write_file_bytes(repaired_path, out.data(), out.size())) {
        return OperationResult::fail("Failed writing repaired ZIP");
    }
    return OperationResult::ok("Repaired ZIP archive: salvaged " + std::to_string(salvaged.size()) + " entry(ies)",
                               salvaged.size());
}

} // namespace openrar
