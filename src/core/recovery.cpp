// Interleaved 512-byte sector parity recovery record ("RECR" format).
// Sector s belongs to parity stripe (s % parity_sectors), allowing exact bit-for-bit
// recovery of single-sector faults and contiguous burst corruptions up to P * 512 bytes.

#include "openrar/recovery.hpp"
#include "openrar/binary_io.hpp"
#include "openrar/crypto.hpp"

#include <algorithm>
#include <cstring>

namespace openrar::recovery {

namespace {

constexpr uint32_t kRecMagic = 0x52454352; // "RECR"
constexpr uint32_t kDefaultSectorSize = 512;

using bin::read_u32;
using bin::read_u64;
using bin::write_u32;
using bin::write_u64;

} // namespace

std::vector<uint8_t> build_recovery_record(const uint8_t* payload, size_t payload_len, uint32_t percent) {
    if (percent < 1) percent = 1;
    if (percent > 50) percent = 50;

    uint32_t sector_size = kDefaultSectorSize;
    uint32_t total_sectors = static_cast<uint32_t>((payload_len + sector_size - 1) / sector_size);
    if (total_sectors == 0) total_sectors = 1;

    uint32_t parity_sectors = std::max<uint32_t>(1, (total_sectors * percent + 99) / 100);
    if (parity_sectors > total_sectors) parity_sectors = total_sectors;

    std::vector<uint32_t> sector_crcs(total_sectors, 0);
    std::vector<uint8_t> parity(static_cast<size_t>(parity_sectors) * sector_size, 0);

    std::vector<uint8_t> sec_buf(sector_size, 0);
    for (uint32_t s = 0; s < total_sectors; ++s) {
        std::fill(sec_buf.begin(), sec_buf.end(), uint8_t{0});
        size_t offset = static_cast<size_t>(s) * sector_size;
        if (offset < payload_len) {
            size_t n = std::min<size_t>(sector_size, payload_len - offset);
            std::memcpy(sec_buf.data(), payload + offset, n);
        }
        sector_crcs[s] = crypto::crc32(sec_buf);

        // Interleaved stripe assignment: sector s -> parity stripe (s % parity_sectors)
        // This allows repairing contiguous burst errors of up to parity_sectors * sector_size bytes!
        uint32_t stripe = s % parity_sectors;
        uint8_t* p_dst = parity.data() + static_cast<size_t>(stripe) * sector_size;
        for (uint32_t i = 0; i < sector_size; ++i) {
            p_dst[i] ^= sec_buf[i];
        }
    }

    std::vector<uint8_t> out;
    out.reserve(24 + total_sectors * 4 + parity.size() + 4);
    write_u32(out, kRecMagic);
    write_u32(out, sector_size);
    write_u64(out, static_cast<uint64_t>(payload_len));
    write_u32(out, total_sectors);
    write_u32(out, parity_sectors);
    for (uint32_t c : sector_crcs) write_u32(out, c);
    out.insert(out.end(), parity.begin(), parity.end());
    uint32_t rec_crc = crypto::crc32(out);
    write_u32(out, rec_crc);
    return out;
}

RecoveryStats repair_with_recovery_record(std::vector<uint8_t>& payload,
                                          const uint8_t* record, size_t record_len) {
    RecoveryStats stats;
    if (record_len < 28) {
        stats.message = "Recovery record is truncated";
        return stats;
    }
    uint32_t magic = read_u32(record);
    if (magic != kRecMagic) {
        stats.message = "Invalid recovery record signature";
        return stats;
    }
    uint32_t expected_rec_crc = read_u32(record + record_len - 4);
    if (crypto::crc32(record, record_len - 4) != expected_rec_crc) {
        stats.message = "Recovery record itself is corrupted";
        return stats;
    }

    uint32_t sector_size = read_u32(record + 4);
    uint64_t orig_len = read_u64(record + 8);
    uint32_t total_sectors = read_u32(record + 16);
    uint32_t parity_sectors = read_u32(record + 20);

    size_t expected_size = 24 + static_cast<size_t>(total_sectors) * 4 +
                           static_cast<size_t>(parity_sectors) * sector_size + 4;
    if (record_len != expected_size || sector_size == 0 || parity_sectors == 0) {
        stats.message = "Recovery record metadata mismatch";
        return stats;
    }

    stats.valid = true;
    stats.sector_size = sector_size;
    stats.total_sectors = total_sectors;
    stats.parity_sectors = parity_sectors;

    // Resize payload to original size if truncated/padded
    if (payload.size() != orig_len) {
        payload.resize(static_cast<size_t>(orig_len), 0);
    }

    const uint8_t* crc_ptr = record + 24;
    const uint8_t* parity_ptr = crc_ptr + static_cast<size_t>(total_sectors) * 4;

    std::vector<bool> bad(total_sectors, false);
    std::vector<uint8_t> sec_buf(sector_size, 0);
    for (uint32_t s = 0; s < total_sectors; ++s) {
        uint32_t expected_crc = read_u32(crc_ptr + static_cast<size_t>(s) * 4);
        std::fill(sec_buf.begin(), sec_buf.end(), uint8_t{0});
        size_t offset = static_cast<size_t>(s) * sector_size;
        if (offset < payload.size()) {
            size_t n = std::min<size_t>(sector_size, payload.size() - offset);
            std::memcpy(sec_buf.data(), payload.data() + offset, n);
        }
        if (crypto::crc32(sec_buf) != expected_crc) {
            bad[s] = true;
            stats.corrupted_sectors++;
        }
    }

    if (stats.corrupted_sectors == 0) {
        stats.message = "Archive payload is intact; no repair needed";
        return stats;
    }

    // Attempt per-stripe reconstruction
    for (uint32_t stripe = 0; stripe < parity_sectors; ++stripe) {
        std::vector<uint32_t> bad_in_stripe;
        for (uint32_t s = stripe; s < total_sectors; s += parity_sectors) {
            if (bad[s]) bad_in_stripe.push_back(s);
        }
        if (bad_in_stripe.empty()) continue;
        if (bad_in_stripe.size() > 1) {
            stats.unrecoverable_sectors += static_cast<uint32_t>(bad_in_stripe.size());
            continue;
        }

        // Exactly 1 corrupted sector in this stripe -> reconstruct via XOR!
        uint32_t target = bad_in_stripe[0];
        std::vector<uint8_t> reconstructed(sector_size);
        std::memcpy(reconstructed.data(), parity_ptr + static_cast<size_t>(stripe) * sector_size, sector_size);

        for (uint32_t s = stripe; s < total_sectors; s += parity_sectors) {
            if (s == target) continue;
            size_t offset = static_cast<size_t>(s) * sector_size;
            size_t n = (offset < payload.size()) ? std::min<size_t>(sector_size, payload.size() - offset) : 0;
            for (size_t i = 0; i < n; ++i) {
                reconstructed[i] ^= payload[offset + i];
            }
        }

        uint32_t expected_crc = read_u32(crc_ptr + static_cast<size_t>(target) * 4);
        if (crypto::crc32(reconstructed) == expected_crc) {
            size_t offset = static_cast<size_t>(target) * sector_size;
            if (offset < payload.size()) {
                size_t n = std::min<size_t>(sector_size, payload.size() - offset);
                std::memcpy(payload.data() + offset, reconstructed.data(), n);
            }
            stats.repaired_sectors++;
        } else {
            stats.unrecoverable_sectors++;
        }
    }

    if (stats.unrecoverable_sectors == 0) {
        stats.message = "Successfully repaired " + std::to_string(stats.repaired_sectors) + " corrupted sector(s)";
    } else {
        stats.message = "Repaired " + std::to_string(stats.repaired_sectors) + " sector(s), " +
                        std::to_string(stats.unrecoverable_sectors) + " sector(s) unrecoverable";
    }
    return stats;
}

} // namespace openrar::recovery
