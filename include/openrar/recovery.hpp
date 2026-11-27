#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// Block-interleaved parity recovery records (inspired by classic archiver Recovery Record).
///
/// Divides protected archive bytes into 512-byte sectors, records each sector's CRC32,
/// and interleaves sectors across `P` parity stripes (`stripe = sector_idx % P`).
/// This allows exact bit-for-bit reconstruction of contiguous burst corruptions up to `P * 512` bytes!
namespace openrar::recovery {

/// Diagnostic report returned after attempting recovery on a damaged payload.
struct RecoveryStats {
    bool valid = false;                  ///< True if the recovery record itself passed CRC validation
    uint32_t sector_size = 512;          ///< Sector size in bytes
    uint32_t total_sectors = 0;          ///< Total number of protected data sectors
    uint32_t parity_sectors = 0;         ///< Number of interleaved XOR parity sectors
    uint32_t corrupted_sectors = 0;      ///< Number of sectors failing CRC32 before repair
    uint32_t repaired_sectors = 0;       ///< Number of corrupted sectors successfully reconstructed
    uint32_t unrecoverable_sectors = 0;  ///< Sectors that could not be repaired (>1 bad in same stripe)
    std::string message;                 ///< Human-readable summary of repair outcome
};

/// Generates a serialized Recovery Record ("RECR" format) for `payload[0..payload_len)`
/// with target redundancy overhead `percent` (clamped to 1..50).
std::vector<uint8_t> build_recovery_record(const uint8_t* payload, size_t payload_len, uint32_t percent);

/// Verifies `payload` against `record` and reconstructs corrupted sectors in-place.
RecoveryStats repair_with_recovery_record(std::vector<uint8_t>& payload,
                                          const uint8_t* record, size_t record_len);

} // namespace openrar::recovery
