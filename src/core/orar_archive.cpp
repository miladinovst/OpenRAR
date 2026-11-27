// OpenRAR (.orar) container implementation.
// Wire layout: [135B Fixed Header][File Table][Payload][Optional "RECR" Recovery Record].
// Supports solid mode, parallel per-file Deflate, AES-256-GCM payload & header encryption,
// interleaved parity recovery repair, and .partN.orar split volumes.

#include "openrar/orar_archive.hpp"
#include "openrar/binary_io.hpp"
#include "openrar/crypto.hpp"
#include "openrar/deflate.hpp"
#include "openrar/recovery.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace openrar {

namespace {

constexpr uint8_t kOrarMagic[6] = {'O', 'R', 'A', 'R', 0x01, 0x00};
constexpr uint32_t kFlagSolid            = 1u << 0;
constexpr uint32_t kFlagEncrypted        = 1u << 1;
constexpr uint32_t kFlagEncryptedHeaders = 1u << 2;
constexpr uint32_t kFlagHasRecovery      = 1u << 3;
constexpr uint32_t kPbkdf2Iters          = 25000;

inline void wr16(std::vector<uint8_t>& b, uint16_t v) { bin::write_u16(b, v); }
inline void wr32(std::vector<uint8_t>& b, uint32_t v) { bin::write_u32(b, v); }
inline void wr64(std::vector<uint8_t>& b, uint64_t v) { bin::write_u64(b, v); }
inline uint16_t rd16(const uint8_t* p) { return bin::read_u16(p); }
inline uint32_t rd32(const uint8_t* p) { return bin::read_u32(p); }
inline uint64_t rd64(const uint8_t* p) { return bin::read_u64(p); }

std::vector<uint8_t> load_orar_volumes(const fs::path& archive_path, size_t* volume_count_out = nullptr) {
    return bin::load_split_volumes(archive_path, "orar", volume_count_out);
}

struct OrarEntry {
    std::string path;
    bool is_directory = false;
    uint64_t uncomp_size = 0;
    uint64_t comp_size = 0;   // in non-solid mode: payload slice size; in solid mode: proportional/0
    uint64_t data_offset = 0; // offset inside payload (non-solid) or solid stream (solid)
    uint32_t crc32 = 0;
    std::array<uint8_t, 32> sha256{};
    int64_t mtime = 0;
    uint8_t comp_method = 0; // 0 = Store, 1 = Deflate
};

struct OrarHeader {
    uint32_t flags = 0;
    uint32_t recovery_percent = 0;
    uint8_t comp_level = 3;
    std::array<uint8_t, 16> salt{};
    std::array<uint8_t, 12> hdr_nonce{};
    std::array<uint8_t, 16> hdr_tag{};
    std::array<uint8_t, 12> data_nonce{};
    std::array<uint8_t, 16> data_tag{};
    uint64_t table_offset = 0;
    uint64_t table_size = 0;
    uint64_t payload_offset = 0;
    uint64_t payload_size = 0;
    uint64_t recovery_offset = 0;
    uint64_t recovery_size = 0;
};

constexpr size_t kFixedHeaderSize = 6 + 4 + 4 + 1 + 16 + 12 + 16 + 12 + 16 + 8 * 6; // 135 bytes

std::vector<uint8_t> serialize_header(const OrarHeader& h) {
    std::vector<uint8_t> b;
    b.reserve(kFixedHeaderSize);
    b.insert(b.end(), kOrarMagic, kOrarMagic + 6);
    wr32(b, h.flags);
    wr32(b, h.recovery_percent);
    b.push_back(h.comp_level);
    b.insert(b.end(), h.salt.begin(), h.salt.end());
    b.insert(b.end(), h.hdr_nonce.begin(), h.hdr_nonce.end());
    b.insert(b.end(), h.hdr_tag.begin(), h.hdr_tag.end());
    b.insert(b.end(), h.data_nonce.begin(), h.data_nonce.end());
    b.insert(b.end(), h.data_tag.begin(), h.data_tag.end());
    wr64(b, h.table_offset);
    wr64(b, h.table_size);
    wr64(b, h.payload_offset);
    wr64(b, h.payload_size);
    wr64(b, h.recovery_offset);
    wr64(b, h.recovery_size);
    return b;
}

bool parse_header(const std::vector<uint8_t>& buf, OrarHeader& h, std::string* err) {
    if (buf.size() < kFixedHeaderSize) {
        if (err) *err = "File too small to be an OpenRAR archive";
        return false;
    }
    if (std::memcmp(buf.data(), kOrarMagic, 6) != 0) {
        if (err) *err = "Invalid OpenRAR magic header";
        return false;
    }
    size_t p = 6;
    h.flags = rd32(buf.data() + p); p += 4;
    h.recovery_percent = rd32(buf.data() + p); p += 4;
    h.comp_level = buf[p++];
    std::memcpy(h.salt.data(), buf.data() + p, 16); p += 16;
    std::memcpy(h.hdr_nonce.data(), buf.data() + p, 12); p += 12;
    std::memcpy(h.hdr_tag.data(), buf.data() + p, 16); p += 16;
    std::memcpy(h.data_nonce.data(), buf.data() + p, 12); p += 12;
    std::memcpy(h.data_tag.data(), buf.data() + p, 16); p += 16;
    h.table_offset = rd64(buf.data() + p); p += 8;
    h.table_size = rd64(buf.data() + p); p += 8;
    h.payload_offset = rd64(buf.data() + p); p += 8;
    h.payload_size = rd64(buf.data() + p); p += 8;
    h.recovery_offset = rd64(buf.data() + p); p += 8;
    h.recovery_size = rd64(buf.data() + p); p += 8;

    if (h.table_offset + h.table_size > buf.size() ||
        h.payload_offset + h.payload_size > buf.size() ||
        (h.recovery_size > 0 && h.recovery_offset + h.recovery_size > buf.size())) {
        if (err) *err = "Corrupted OpenRAR section offsets";
        return false;
    }
    return true;
}

std::vector<uint8_t> serialize_table(const std::string& comment, const std::vector<OrarEntry>& entries) {
    std::vector<uint8_t> b;
    wr32(b, static_cast<uint32_t>(comment.size()));
    b.insert(b.end(), comment.begin(), comment.end());
    wr32(b, static_cast<uint32_t>(entries.size()));
    for (const auto& e : entries) {
        wr16(b, static_cast<uint16_t>(e.path.size()));
        b.insert(b.end(), e.path.begin(), e.path.end());
        b.push_back(e.is_directory ? 1 : 0);
        b.push_back(e.comp_method);
        wr64(b, e.uncomp_size);
        wr64(b, e.comp_size);
        wr64(b, e.data_offset);
        wr32(b, e.crc32);
        b.insert(b.end(), e.sha256.begin(), e.sha256.end());
        wr64(b, static_cast<uint64_t>(e.mtime));
    }
    return b;
}

bool deserialize_table(const std::vector<uint8_t>& b, std::string& comment,
                       std::vector<OrarEntry>& entries, std::string* err) {
    entries.clear();
    size_t p = 0;
    if (p + 4 > b.size()) { if (err) *err = "Corrupted table"; return false; }
    uint32_t cmt_len = rd32(b.data() + p); p += 4;
    if (p + cmt_len + 4 > b.size()) { if (err) *err = "Corrupted comment"; return false; }
    comment.assign(reinterpret_cast<const char*>(b.data() + p), cmt_len); p += cmt_len;
    uint32_t count = rd32(b.data() + p); p += 4;
    entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (p + 2 > b.size()) return false;
        uint16_t plen = rd16(b.data() + p); p += 2;
        if (p + plen + 2 + 8 * 3 + 4 + 32 + 8 > b.size()) return false;
        OrarEntry e;
        e.path.assign(reinterpret_cast<const char*>(b.data() + p), plen); p += plen;
        e.is_directory = b[p++] != 0;
        e.comp_method = b[p++];
        e.uncomp_size = rd64(b.data() + p); p += 8;
        e.comp_size = rd64(b.data() + p); p += 8;
        e.data_offset = rd64(b.data() + p); p += 8;
        e.crc32 = rd32(b.data() + p); p += 4;
        std::memcpy(e.sha256.data(), b.data() + p, 32); p += 32;
        e.mtime = static_cast<int64_t>(rd64(b.data() + p)); p += 8;
        entries.push_back(std::move(e));
    }
    return true;
}

bool decode_table(const std::vector<uint8_t>& buf, const OrarHeader& h,
                  const std::string& password, std::string& comment,
                  std::vector<OrarEntry>& entries, std::string* err) {
    std::vector<uint8_t> table_raw(buf.begin() + static_cast<ptrdiff_t>(h.table_offset),
                                   buf.begin() + static_cast<ptrdiff_t>(h.table_offset + h.table_size));
    if (h.flags & kFlagEncryptedHeaders) {
        if (password.empty()) {
            if (err) *err = "Password required (archive headers are encrypted)";
            return false;
        }
        auto key = crypto::pbkdf2_hmac_sha256(password, h.salt.data(), 16, kPbkdf2Iters, 32);
        std::vector<uint8_t> dec;
        if (!crypto::aes256_gcm_decrypt(key.data(), h.hdr_nonce.data(), kOrarMagic, 6,
                                        table_raw.data(), table_raw.size(), h.hdr_tag.data(), dec)) {
            if (err) *err = "Wrong password or corrupted encrypted headers";
            return false;
        }
        table_raw = std::move(dec);
    }
    return deserialize_table(table_raw, comment, entries, err);
}

bool decode_payload(const std::vector<uint8_t>& buf, const OrarHeader& h,
                    const std::string& password, std::vector<uint8_t>& payload_out, std::string* err) {
    payload_out.assign(buf.begin() + static_cast<ptrdiff_t>(h.payload_offset),
                       buf.begin() + static_cast<ptrdiff_t>(h.payload_offset + h.payload_size));
    if (h.flags & kFlagEncrypted) {
        if (password.empty()) {
            if (err) *err = "Password required for encrypted OpenRAR archive";
            return false;
        }
        auto key = crypto::pbkdf2_hmac_sha256(password, h.salt.data(), 16, kPbkdf2Iters, 32);
        std::vector<uint8_t> dec;
        if (!crypto::aes256_gcm_decrypt(key.data(), h.data_nonce.data(), kOrarMagic, 6,
                                        payload_out.data(), payload_out.size(), h.data_tag.data(), dec)) {
            if (err) *err = "Wrong password or corrupted encrypted payload";
            return false;
        }
        payload_out = std::move(dec);
    }
    if (h.flags & kFlagSolid) {
        if (h.comp_level != 0 && !payload_out.empty()) {
            std::vector<uint8_t> decomp;
            if (!codec::deflate_decompress(payload_out, decomp, err)) return false;
            payload_out = std::move(decomp);
        }
    }
    return true;
}

bool extract_single_entry(const OrarHeader& h, const std::vector<uint8_t>& decoded_payload,
                          const OrarEntry& e, std::vector<uint8_t>& out, std::string* err) {
    if (e.is_directory) { out.clear(); return true; }
    if (h.flags & kFlagSolid) {
        if (e.data_offset + e.uncomp_size > decoded_payload.size()) {
            if (err) *err = "Solid stream bounds error for " + e.path;
            return false;
        }
        out.assign(decoded_payload.begin() + static_cast<ptrdiff_t>(e.data_offset),
                   decoded_payload.begin() + static_cast<ptrdiff_t>(e.data_offset + e.uncomp_size));
    } else {
        if (e.data_offset + e.comp_size > decoded_payload.size()) {
            if (err) *err = "Payload bounds error for " + e.path;
            return false;
        }
        const uint8_t* ptr = decoded_payload.data() + e.data_offset;
        if (e.comp_method == 1) {
            if (!codec::deflate_decompress(ptr, static_cast<size_t>(e.comp_size), out, err)) return false;
        } else {
            out.assign(ptr, ptr + e.comp_size);
        }
    }
    if (crypto::crc32(out) != e.crc32) {
        if (err) *err = "CRC32 mismatch for " + e.path;
        return false;
    }
    auto digest = crypto::sha256(out);
    if (!crypto::constant_time_equal(digest.data(), e.sha256.data(), 32)) {
        if (err) *err = "SHA-256 integrity check failed for " + e.path;
        return false;
    }
    return true;
}

struct InputItem {
    std::string rel_path;
    bool is_directory = false;
    int64_t mtime = 0;
    std::vector<uint8_t> data;
};

void gather_inputs(const std::vector<fs::path>& inputs, const fs::path& base_dir,
                   std::vector<InputItem>& items) {
    for (const auto& inp : inputs) {
        std::error_code ec;
        if (!fs::exists(inp, ec)) continue;
        fs::path abs_inp = fs::absolute(inp, ec);
        fs::path root = base_dir.empty() ? abs_inp.parent_path() : fs::absolute(base_dir, ec);
        if (fs::is_directory(abs_inp, ec)) {
            std::string drel = normalize_entry_path(path_to_utf8(fs::relative(abs_inp, root, ec)));
            if (!drel.empty() && drel != ".") {
                items.push_back({drel, true, file_time_to_unix(fs::last_write_time(abs_inp, ec)), {}});
            }
            for (const auto& it : fs::recursive_directory_iterator(abs_inp, ec)) {
                std::string rel = normalize_entry_path(path_to_utf8(fs::relative(it.path(), root, ec)));
                if (rel.empty() || rel == ".") continue;
                InputItem item;
                item.rel_path = rel;
                item.is_directory = it.is_directory(ec);
                item.mtime = file_time_to_unix(it.last_write_time(ec));
                if (!item.is_directory) item.data = read_file_bytes(it.path());
                items.push_back(std::move(item));
            }
        } else {
            std::string rel = normalize_entry_path(path_to_utf8(fs::relative(abs_inp, root, ec)));
            if (rel.empty() || rel.find("..") == 0) rel = path_to_utf8(abs_inp.filename());
            InputItem item;
            item.rel_path = rel;
            item.is_directory = false;
            item.mtime = file_time_to_unix(fs::last_write_time(abs_inp, ec));
            item.data = read_file_bytes(abs_inp);
            items.push_back(std::move(item));
        }
    }
}

OperationResult build_and_write_orar(const fs::path& archive_path,
                                     const std::vector<InputItem>& items,
                                     const CreateOptions& options) {
    auto t0 = std::chrono::steady_clock::now();
    OrarHeader hdr;
    hdr.comp_level = static_cast<uint8_t>(options.level);
    if (options.solid) hdr.flags |= kFlagSolid;
    if (!options.password.empty()) {
        hdr.flags |= kFlagEncrypted;
        if (options.encrypt_headers) hdr.flags |= kFlagEncryptedHeaders;
    }
    if (options.add_recovery_record) {
        hdr.flags |= kFlagHasRecovery;
        hdr.recovery_percent = std::clamp<uint32_t>(options.recovery_percent, 1, 50);
    }

    uint64_t total_bytes = 0;
    for (const auto& it : items) total_bytes += it.data.size();

    std::vector<OrarEntry> entries;
    entries.reserve(items.size());
    std::vector<uint8_t> payload;
    uint64_t bytes_done = 0;

    if (options.solid) {
        std::vector<uint8_t> solid_raw;
        solid_raw.reserve(static_cast<size_t>(total_bytes));
        for (size_t i = 0; i < items.size(); ++i) {
            if (options.cancel_flag && options.cancel_flag->load()) return OperationResult::fail("Cancelled");
            const auto& it = items[i];
            OrarEntry e;
            e.path = it.rel_path;
            e.is_directory = it.is_directory;
            e.mtime = it.mtime;
            e.uncomp_size = it.data.size();
            e.comp_size = it.data.size();
            e.data_offset = solid_raw.size();
            e.comp_method = (options.level == CompressionLevel::Store) ? 0 : 1;
            if (!it.is_directory) {
                e.crc32 = crypto::crc32(it.data);
                e.sha256 = crypto::sha256(it.data);
                solid_raw.insert(solid_raw.end(), it.data.begin(), it.data.end());
            }
            entries.push_back(std::move(e));
            bytes_done += it.data.size();
            if (options.progress) {
                ProgressInfo pi{"Compressing (OpenRAR Solid)", it.rel_path, bytes_done, total_bytes, i + 1, items.size(), 0.0};
                options.progress(pi);
            }
        }
        payload = (options.level == CompressionLevel::Store)
            ? std::move(solid_raw)
            : codec::deflate_compress(solid_raw, options.level);
    } else {
        // Multi-threaded parallel per-file compression & hashing across CPU cores!
        struct CompSlot {
            OrarEntry entry;
            std::vector<uint8_t> data;
        };
        std::vector<CompSlot> slots(items.size());
        unsigned int num_threads = std::min<unsigned int>(
            std::max<unsigned int>(1, std::thread::hardware_concurrency()),
            static_cast<unsigned int>(std::max<size_t>(1, items.size())));
        std::atomic<size_t> next_idx{0};
        std::atomic<size_t> done_cnt{0};

        auto worker = [&] {
            while (true) {
                if (options.cancel_flag && options.cancel_flag->load()) return;
                size_t i = next_idx.fetch_add(1, std::memory_order_relaxed);
                if (i >= items.size()) return;
                const auto& it = items[i];
                OrarEntry e;
                e.path = it.rel_path;
                e.is_directory = it.is_directory;
                e.mtime = it.mtime;
                e.uncomp_size = it.data.size();
                std::vector<uint8_t> comp_buf;
                if (!it.is_directory) {
                    e.crc32 = crypto::crc32(it.data);
                    e.sha256 = crypto::sha256(it.data);
                    if (options.level == CompressionLevel::Store || it.data.empty()) {
                        e.comp_method = 0;
                        e.comp_size = it.data.size();
                        comp_buf = it.data;
                    } else {
                        auto comp = codec::deflate_compress(it.data, options.level);
                        if (comp.size() < it.data.size()) {
                            e.comp_method = 1;
                            e.comp_size = comp.size();
                            comp_buf = std::move(comp);
                        } else {
                            e.comp_method = 0;
                            e.comp_size = it.data.size();
                            comp_buf = it.data;
                        }
                    }
                }
                slots[i].entry = std::move(e);
                slots[i].data = std::move(comp_buf);
                done_cnt.fetch_add(1, std::memory_order_relaxed);
            }
        };

        if (num_threads <= 1 || items.size() < 4) {
            worker();
        } else {
            std::vector<std::thread> pool;
            pool.reserve(num_threads);
            for (unsigned int t = 0; t < num_threads; ++t) pool.emplace_back(worker);
            for (auto& th : pool) th.join();
        }
        if (options.cancel_flag && options.cancel_flag->load()) return OperationResult::fail("Cancelled");

        for (size_t i = 0; i < slots.size(); ++i) {
            slots[i].entry.data_offset = payload.size();
            payload.insert(payload.end(), slots[i].data.begin(), slots[i].data.end());
            bytes_done += slots[i].entry.uncomp_size;
            entries.push_back(std::move(slots[i].entry));
            if (options.progress) {
                double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                ProgressInfo pi{
                    "Compressing (OpenRAR)", entries.back().path, bytes_done, total_bytes, i + 1, items.size(),
                    elapsed > 0.001 ? static_cast<double>(bytes_done) / elapsed : 0.0
                };
                options.progress(pi);
            }
        }
    }

    auto table_bytes = serialize_table(options.comment, entries);

    if (!options.password.empty()) {
        crypto::random_bytes(hdr.salt.data(), 16);
        auto key = crypto::pbkdf2_hmac_sha256(options.password, hdr.salt.data(), 16, kPbkdf2Iters, 32);

        if (options.encrypt_headers) {
            crypto::random_bytes(hdr.hdr_nonce.data(), 12);
            table_bytes = crypto::aes256_gcm_encrypt(key.data(), hdr.hdr_nonce.data(),
                                                     kOrarMagic, 6,
                                                     table_bytes.data(), table_bytes.size(),
                                                     hdr.hdr_tag.data());
        }
        crypto::random_bytes(hdr.data_nonce.data(), 12);
        payload = crypto::aes256_gcm_encrypt(key.data(), hdr.data_nonce.data(),
                                             kOrarMagic, 6,
                                             payload.data(), payload.size(),
                                             hdr.data_tag.data());
    }

    hdr.table_offset = kFixedHeaderSize;
    hdr.table_size = table_bytes.size();
    hdr.payload_offset = hdr.table_offset + hdr.table_size;
    hdr.payload_size = payload.size();

    std::vector<uint8_t> rec_bytes;
    if (options.add_recovery_record) {
        // Protect table + payload together so either can be repaired!
        std::vector<uint8_t> protected_body;
        protected_body.reserve(table_bytes.size() + payload.size());
        protected_body.insert(protected_body.end(), table_bytes.begin(), table_bytes.end());
        protected_body.insert(protected_body.end(), payload.begin(), payload.end());
        rec_bytes = recovery::build_recovery_record(protected_body.data(), protected_body.size(), hdr.recovery_percent);
        hdr.recovery_offset = hdr.payload_offset + hdr.payload_size;
        hdr.recovery_size = rec_bytes.size();
    }

    auto hdr_bytes = serialize_header(hdr);
    std::vector<uint8_t> archive_bytes;
    archive_bytes.reserve(hdr_bytes.size() + table_bytes.size() + payload.size() + rec_bytes.size());
    archive_bytes.insert(archive_bytes.end(), hdr_bytes.begin(), hdr_bytes.end());
    archive_bytes.insert(archive_bytes.end(), table_bytes.begin(), table_bytes.end());
    archive_bytes.insert(archive_bytes.end(), payload.begin(), payload.end());
    archive_bytes.insert(archive_bytes.end(), rec_bytes.begin(), rec_bytes.end());

    OperationResult res;
    if (options.split_volume_bytes > 0 && archive_bytes.size() > options.split_volume_bytes) {
        fs::path dir = archive_path.parent_path();
        std::string stem = path_to_utf8(archive_path.stem());
        size_t part = 1;
        for (size_t pos = 0; pos < archive_bytes.size(); pos += static_cast<size_t>(options.split_volume_bytes), ++part) {
            size_t chunk = std::min<size_t>(static_cast<size_t>(options.split_volume_bytes), archive_bytes.size() - pos);
            fs::path vp = dir / path_from_utf8(stem + ".part" + std::to_string(part) + ".orar");
            if (!write_file_bytes(vp, archive_bytes.data() + pos, chunk)) {
                return OperationResult::fail("Failed writing volume: " + path_to_utf8(vp));
            }
            res.output_files.push_back(vp);
        }
    } else {
        if (!write_file_bytes(archive_path, archive_bytes.data(), archive_bytes.size())) {
            return OperationResult::fail("Failed writing OpenRAR archive: " + path_to_utf8(archive_path));
        }
        res.output_files.push_back(archive_path);
    }

    res.success = true;
    res.files_processed = items.size();
    res.bytes_processed = total_bytes;
    res.duration_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    res.message = "Created OpenRAR archive with " + std::to_string(items.size()) + " item(s)";
    return res;
}

} // namespace

bool OrarArchive::list(const fs::path& archive_path, std::vector<EntryInfo>& entries,
                          const std::string& password, ArchiveInfo* info, std::string* err) {
    entries.clear();
    size_t vol_count = 1;
    auto buf = load_orar_volumes(archive_path, &vol_count);
    if (buf.empty()) { if (err) *err = "Cannot read OpenRAR archive"; return false; }

    OrarHeader hdr;
    if (!parse_header(buf, hdr, err)) return false;

    std::string comment;
    std::vector<OrarEntry> raw_entries;
    if (!decode_table(buf, hdr, password, comment, raw_entries, err)) return false;

    ArchiveInfo ai;
    ai.file_path = archive_path;
    ai.format = ArchiveFormat::Orar;
    ai.archive_size = buf.size();
    ai.is_solid = (hdr.flags & kFlagSolid) != 0;
    ai.is_encrypted = (hdr.flags & kFlagEncrypted) != 0;
    ai.has_encrypted_headers = (hdr.flags & kFlagEncryptedHeaders) != 0;
    ai.has_recovery_record = (hdr.flags & kFlagHasRecovery) != 0;
    ai.recovery_percent = hdr.recovery_percent;
    ai.is_multi_volume = vol_count > 1;
    ai.volume_count = vol_count;
    ai.comment = comment;
    ai.compression_method = ai.is_solid ? "Solid Deflate (ORAR)" : "Deflate (ORAR)";

    for (const auto& r : raw_entries) {
        EntryInfo e;
        e.full_path = normalize_entry_path(r.path);
        auto slash = e.full_path.rfind('/');
        if (slash != std::string::npos) {
            e.directory = e.full_path.substr(0, slash);
            e.name = e.full_path.substr(slash + 1);
        } else {
            e.name = e.full_path;
        }
        e.size = r.uncomp_size;
        e.compressed_size = ai.is_solid ? r.uncomp_size : r.comp_size;
        e.crc32 = r.crc32;
        e.sha256_hex = crypto::to_hex(r.sha256.data(), 32);
        e.modified_time = r.mtime;
        e.is_directory = r.is_directory;
        e.is_encrypted = ai.is_encrypted;
        e.method = (r.comp_method == 1)
            ? (ai.is_encrypted ? "AES-256-GCM + Deflate" : "Deflate")
            : (ai.is_encrypted ? "AES-256-GCM + Store" : "Store");
        if (e.is_directory) ai.folder_count++;
        else {
            ai.file_count++;
            ai.total_uncompressed_size += e.size;
            ai.total_compressed_size += e.compressed_size;
        }
        entries.push_back(std::move(e));
    }
    if (ai.is_solid) ai.total_compressed_size = hdr.payload_size;
    if (info) *info = std::move(ai);
    return true;
}

OperationResult OrarArchive::create(const fs::path& archive_path,
                                       const std::vector<fs::path>& inputs,
                                       const CreateOptions& options) {
    std::vector<InputItem> items;
    gather_inputs(inputs, options.base_dir, items);
    if (items.empty()) return OperationResult::fail("No input files found to archive");

    auto res = build_and_write_orar(archive_path, items, options);
    if (!res.success) return res;

    if (options.test_after && !res.output_files.empty()) {
        auto tr = test(res.output_files[0], options.password);
        if (!tr.success) return tr;
    }
    if (options.delete_after) {
        std::error_code ec;
        for (const auto& inp : inputs) fs::remove_all(inp, ec);
    }
    return res;
}

OperationResult OrarArchive::extract(const fs::path& archive_path, const ExtractOptions& options) {
    auto t0 = std::chrono::steady_clock::now();
    auto buf = load_orar_volumes(archive_path);
    if (buf.empty()) return OperationResult::fail("Cannot read OpenRAR archive");

    OrarHeader hdr;
    std::string err, comment;
    std::vector<OrarEntry> entries;
    if (!parse_header(buf, hdr, &err)) return OperationResult::fail(err);
    if (!decode_table(buf, hdr, options.password, comment, entries, &err)) return OperationResult::fail(err);

    std::vector<uint8_t> payload;
    if (!decode_payload(buf, hdr, options.password, payload, &err)) return OperationResult::fail(err);

    std::error_code ec;
    fs::create_directories(options.destination, ec);

    uint64_t total_bytes = 0;
    for (const auto& e : entries) total_bytes += e.uncomp_size;
    OperationResult res;
    size_t files_done = 0;
    uint64_t bytes_done = 0;

    for (const auto& e : entries) {
        if (options.cancel_flag && options.cancel_flag->load()) return OperationResult::fail("Cancelled");
        std::string norm = normalize_entry_path(e.path);
        if (!options.selected_entries.empty() && !options.selected_entries.count(norm)) continue;
        if (!is_safe_relative_path(norm)) { res.warnings.push_back("Skipped unsafe path: " + e.path); continue; }

        fs::path norm_fs = path_from_utf8(norm);
        fs::path rel = options.preserve_paths ? norm_fs : norm_fs.filename();
        fs::path target = options.destination / rel;

        if (e.is_directory) {
            if (options.preserve_paths) fs::create_directories(target, ec);
            continue;
        }
        if (options.update_mode == UpdateMode::FreshExistingOnly && !fs::exists(target, ec)) continue;
        if (options.update_mode == UpdateMode::ExtractAndUpdate && fs::exists(target, ec)) {
            if (file_time_to_unix(fs::last_write_time(target, ec)) >= e.mtime) continue;
        }
        if (fs::exists(target, ec)) {
            OverwriteMode mode = options.overwrite;
            if (mode == OverwriteMode::Ask && options.overwrite_prompt) mode = options.overwrite_prompt(target);
            if (mode == OverwriteMode::SkipExisting) continue;
            if (mode == OverwriteMode::RenameAuto) {
                fs::path dir = target.parent_path();
                std::string stem = path_to_utf8(target.stem()), ext = path_to_utf8(target.extension());
                for (int n = 1; n < 10000; ++n) {
                    fs::path cand = dir / path_from_utf8(stem + " (" + std::to_string(n) + ")" + ext);
                    if (!fs::exists(cand, ec)) { target = cand; break; }
                }
            }
        }

        std::vector<uint8_t> data;
        std::string e_err;
        if (!extract_single_entry(hdr, payload, e, data, &e_err)) {
            res.errors.push_back(e_err);
            if (!options.keep_broken) continue;
        }
        if (!write_file_bytes(target, data.data(), data.size())) {
            res.errors.push_back("Failed writing: " + path_to_utf8(target));
            continue;
        }
        if (e.mtime > 0) fs::last_write_time(target, unix_to_file_time(e.mtime), ec);

        files_done++;
        bytes_done += data.size();
        if (options.progress) {
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            ProgressInfo pi{
                "Extracting (OpenRAR)", norm, bytes_done, total_bytes, files_done, entries.size(),
                elapsed > 0.001 ? static_cast<double>(bytes_done) / elapsed : 0.0
            };
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

bool OrarArchive::extract_entry_to_memory(const fs::path& archive_path, const std::string& entry_path,
                                             const std::string& password, std::vector<uint8_t>& out,
                                             std::string* err) {
    auto buf = load_orar_volumes(archive_path);
    if (buf.empty()) { if (err) *err = "Cannot read OpenRAR"; return false; }
    OrarHeader hdr;
    std::string comment;
    std::vector<OrarEntry> entries;
    if (!parse_header(buf, hdr, err)) return false;
    if (!decode_table(buf, hdr, password, comment, entries, err)) return false;
    std::vector<uint8_t> payload;
    if (!decode_payload(buf, hdr, password, payload, err)) return false;
    std::string target = normalize_entry_path(entry_path);
    for (const auto& e : entries) {
        if (normalize_entry_path(e.path) == target) {
            return extract_single_entry(hdr, payload, e, out, err);
        }
    }
    if (err) *err = "Entry not found: " + entry_path;
    return false;
}

OperationResult OrarArchive::test(const fs::path& archive_path, const std::string& password,
                                     ProgressCallback progress, std::atomic<bool>* cancel_flag) {
    auto t0 = std::chrono::steady_clock::now();
    auto buf = load_orar_volumes(archive_path);
    if (buf.empty()) return OperationResult::fail("Cannot read OpenRAR archive");

    OrarHeader hdr;
    std::string err, comment;
    std::vector<OrarEntry> entries;
    if (!parse_header(buf, hdr, &err)) return OperationResult::fail(err);
    if (!decode_table(buf, hdr, password, comment, entries, &err)) return OperationResult::fail(err);

    std::vector<uint8_t> payload;
    if (!decode_payload(buf, hdr, password, payload, &err)) return OperationResult::fail(err);

    OperationResult res;
    size_t files_done = 0;
    uint64_t bytes_done = 0;
    for (const auto& e : entries) {
        if (cancel_flag && cancel_flag->load()) return OperationResult::fail("Cancelled");
        if (e.is_directory) continue;
        std::vector<uint8_t> data;
        std::string e_err;
        if (!extract_single_entry(hdr, payload, e, data, &e_err)) {
            res.errors.push_back(e_err);
        } else {
            files_done++;
            bytes_done += data.size();
        }
        if (progress) {
            ProgressInfo pi{"Testing (OpenRAR)", e.path, bytes_done, bytes_done, files_done, entries.size(), 0.0};
            progress(pi);
        }
    }
    res.files_processed = files_done;
    res.bytes_processed = bytes_done;
    res.duration_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    res.success = res.errors.empty();
    res.message = res.success
        ? ("All " + std::to_string(files_done) + " file(s) passed CRC32 + SHA-256 test (OK)")
        : ("Test failed with " + std::to_string(res.errors.size()) + " error(s)");
    return res;
}

OperationResult OrarArchive::add_entries(const fs::path& archive_path,
                                            const std::vector<fs::path>& inputs,
                                            const CreateOptions& options) {
    std::vector<InputItem> existing;
    std::string old_comment;
    OrarHeader old_hdr;
    auto buf = load_orar_volumes(archive_path);
    if (!buf.empty() && parse_header(buf, old_hdr, nullptr)) {
        std::vector<OrarEntry> entries;
        std::vector<uint8_t> payload;
        if (decode_table(buf, old_hdr, options.password, old_comment, entries, nullptr) &&
            decode_payload(buf, old_hdr, options.password, payload, nullptr)) {
            for (const auto& e : entries) {
                InputItem it{e.path, e.is_directory, e.mtime, {}};
                if (!e.is_directory) extract_single_entry(old_hdr, payload, e, it.data, nullptr);
                existing.push_back(std::move(it));
            }
        }
    }

    std::vector<InputItem> added;
    gather_inputs(inputs, options.base_dir, added);
    std::set<std::string> added_names;
    for (const auto& a : added) added_names.insert(normalize_entry_path(a.rel_path));

    std::vector<InputItem> merged;
    for (auto& e : existing) {
        if (!added_names.count(normalize_entry_path(e.rel_path))) merged.push_back(std::move(e));
    }
    for (auto& a : added) merged.push_back(std::move(a));

    CreateOptions opts = options;
    if (opts.comment.empty()) opts.comment = old_comment;
    return build_and_write_orar(archive_path, merged, opts);
}

OperationResult OrarArchive::delete_entries(const fs::path& archive_path,
                                               const std::set<std::string>& entries_to_delete,
                                               const std::string& password) {
    auto buf = load_orar_volumes(archive_path);
    if (buf.empty()) return OperationResult::fail("Cannot read OpenRAR archive");

    OrarHeader hdr;
    std::string err, comment;
    std::vector<OrarEntry> entries;
    if (!parse_header(buf, hdr, &err)) return OperationResult::fail(err);
    if (!decode_table(buf, hdr, password, comment, entries, &err)) return OperationResult::fail(err);
    std::vector<uint8_t> payload;
    if (!decode_payload(buf, hdr, password, payload, &err)) return OperationResult::fail(err);

    std::vector<InputItem> kept;
    size_t deleted = 0;
    for (const auto& e : entries) {
        std::string norm = normalize_entry_path(e.path);
        bool del = false;
        for (const auto& t : entries_to_delete) {
            std::string nt = normalize_entry_path(t);
            if (norm == nt || norm.starts_with(nt + "/")) { del = true; break; }
        }
        if (del) { deleted++; continue; }
        InputItem it{e.path, e.is_directory, e.mtime, {}};
        if (!e.is_directory) extract_single_entry(hdr, payload, e, it.data, nullptr);
        kept.push_back(std::move(it));
    }

    CreateOptions opts;
    opts.format = ArchiveFormat::Orar;
    opts.level = static_cast<CompressionLevel>(hdr.comp_level);
    opts.password = password;
    opts.encrypt_headers = (hdr.flags & kFlagEncryptedHeaders) != 0;
    opts.solid = (hdr.flags & kFlagSolid) != 0;
    opts.add_recovery_record = (hdr.flags & kFlagHasRecovery) != 0;
    opts.recovery_percent = hdr.recovery_percent;
    opts.comment = comment;
    auto r = build_and_write_orar(archive_path, kept, opts);
    if (r.success) r.message = "Deleted " + std::to_string(deleted) + " entry(ies)";
    return r;
}

OperationResult OrarArchive::set_comment(const fs::path& archive_path,
                                            const std::string& comment,
                                            const std::string& password) {
    auto buf = load_orar_volumes(archive_path);
    if (buf.empty()) return OperationResult::fail("Cannot read OpenRAR archive");
    OrarHeader hdr;
    std::string err, old_cmt;
    std::vector<OrarEntry> entries;
    if (!parse_header(buf, hdr, &err)) return OperationResult::fail(err);
    if (!decode_table(buf, hdr, password, old_cmt, entries, &err)) return OperationResult::fail(err);
    std::vector<uint8_t> payload;
    if (!decode_payload(buf, hdr, password, payload, &err)) return OperationResult::fail(err);

    std::vector<InputItem> items;
    for (const auto& e : entries) {
        InputItem it{e.path, e.is_directory, e.mtime, {}};
        if (!e.is_directory) extract_single_entry(hdr, payload, e, it.data, nullptr);
        items.push_back(std::move(it));
    }
    CreateOptions opts;
    opts.format = ArchiveFormat::Orar;
    opts.level = static_cast<CompressionLevel>(hdr.comp_level);
    opts.password = password;
    opts.encrypt_headers = (hdr.flags & kFlagEncryptedHeaders) != 0;
    opts.solid = (hdr.flags & kFlagSolid) != 0;
    opts.add_recovery_record = (hdr.flags & kFlagHasRecovery) != 0;
    opts.recovery_percent = hdr.recovery_percent;
    opts.comment = comment;
    auto r = build_and_write_orar(archive_path, items, opts);
    if (r.success) r.message = "Updated OpenRAR archive comment";
    return r;
}

OperationResult OrarArchive::repair(const fs::path& corrupted_path, const fs::path& repaired_path) {
    auto buf = load_orar_volumes(corrupted_path);
    if (buf.empty()) return OperationResult::fail("Cannot read corrupted OpenRAR file");

    OrarHeader hdr;
    std::string err;
    if (!parse_header(buf, hdr, &err)) return OperationResult::fail(err);
    if (!(hdr.flags & kFlagHasRecovery) || hdr.recovery_size == 0) {
        return OperationResult::fail("Archive does not contain a Recovery Record");
    }

    std::vector<uint8_t> body(buf.begin() + static_cast<ptrdiff_t>(hdr.table_offset),
                              buf.begin() + static_cast<ptrdiff_t>(hdr.payload_offset + hdr.payload_size));
    const uint8_t* rec_ptr = buf.data() + hdr.recovery_offset;
    auto stats = recovery::repair_with_recovery_record(body, rec_ptr, static_cast<size_t>(hdr.recovery_size));
    if (!stats.valid) return OperationResult::fail(stats.message);

    // Reassemble repaired file
    std::vector<uint8_t> out = buf;
    std::memcpy(out.data() + hdr.table_offset, body.data(), body.size());
    if (!write_file_bytes(repaired_path, out.data(), out.size())) {
        return OperationResult::fail("Failed writing repaired archive");
    }

    if (stats.unrecoverable_sectors > 0) {
        auto r = OperationResult::fail(stats.message);
        r.output_files.push_back(repaired_path);
        return r;
    }
    auto r = OperationResult::ok(stats.message, stats.repaired_sectors);
    r.output_files.push_back(repaired_path);
    return r;
}

} // namespace openrar
