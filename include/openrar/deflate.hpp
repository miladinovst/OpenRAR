#pragma once

#include "openrar/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// Self-contained RFC 1951 Deflate/Inflate and RFC 1952 GZip codecs.
namespace openrar::codec {

/// Compresses `data[0..len)` into a raw RFC 1951 Deflate stream.
/// Uses hash-chain LZ77 (with lazy matching on Normal/Good/Best) and automatically selects
/// the smallest encoding per block: Canonical Dynamic Huffman (type 2), Fixed Huffman (type 1),
/// or Stored (type 0).
std::vector<uint8_t> deflate_compress(const uint8_t* data, size_t len, CompressionLevel level);

/// Convenience overload accepting a byte vector.
std::vector<uint8_t> deflate_compress(const std::vector<uint8_t>& data, CompressionLevel level);

/// Decompresses a raw RFC 1951 Deflate stream (supports Stored, Fixed, and Dynamic Huffman blocks)
/// using a 9-bit O(1) fast lookup table. Returns false and sets `err` on malformed input.
bool deflate_decompress(const uint8_t* data, size_t len, std::vector<uint8_t>& out, std::string* err = nullptr);

/// Convenience overload accepting a byte vector.
bool deflate_decompress(const std::vector<uint8_t>& data, std::vector<uint8_t>& out, std::string* err = nullptr);

/// Wraps compressed data in an RFC 1952 GZip container (header + Deflate + CRC32 + ISIZE).
std::vector<uint8_t> gzip_compress(const uint8_t* data, size_t len, CompressionLevel level,
                                   const std::string& original_name = "");

/// Decompresses an RFC 1952 GZip stream and verifies both CRC32 and ISIZE trailers.
bool gzip_decompress(const uint8_t* data, size_t len, std::vector<uint8_t>& out,
                     std::string* original_name = nullptr, std::string* err = nullptr);

} // namespace openrar::codec
