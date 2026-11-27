#include "openrar/crypto.hpp"

#include <cstring>
#include <fstream>
#include <random>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

namespace openrar::crypto {

// ============================================================================
// CSPRNG
// ============================================================================
bool random_bytes(uint8_t* out, size_t len) {
    if (len == 0) return true;
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, out, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) {
        return true;
    }
#else
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (urandom.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(len))) {
        return true;
    }
#endif
    std::random_device rd;
    for (size_t i = 0; i < len; ++i) {
        out[i] = static_cast<uint8_t>(rd() & 0xFF);
    }
    return true;
}

std::vector<uint8_t> random_bytes(size_t len) {
    std::vector<uint8_t> buf(len);
    random_bytes(buf.data(), len);
    return buf;
}

// ============================================================================
// CRC32 (IEEE 802.3)
// ============================================================================
static const std::array<uint32_t, 256>& crc32_table() {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j) {
                c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    return table;
}

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    const auto& tbl = crc32_table();
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc = tbl[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

uint32_t crc32(const uint8_t* data, size_t len) {
    return crc32_update(0, data, len);
}

uint32_t crc32(const std::vector<uint8_t>& data) {
    return crc32_update(0, data.data(), data.size());
}

// ============================================================================
// SHA-256 (FIPS 180-4)
// ============================================================================
static constexpr uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

Sha256::Sha256() {
    state_[0] = 0x6a09e667; state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372; state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f; state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab; state_[7] = 0x5be0cd19;
    std::memset(buffer_, 0, sizeof(buffer_));
}

void Sha256::transform(const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + kSha256K[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::update(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        buffer_[buffer_len_++] = data[i];
        if (buffer_len_ == 64) {
            transform(buffer_);
            bit_len_ += 512;
            buffer_len_ = 0;
        }
    }
}

std::array<uint8_t, 32> Sha256::final() {
    uint64_t total_bits = bit_len_ + static_cast<uint64_t>(buffer_len_) * 8;
    buffer_[buffer_len_++] = 0x80;
    if (buffer_len_ > 56) {
        while (buffer_len_ < 64) buffer_[buffer_len_++] = 0x00;
        transform(buffer_);
        buffer_len_ = 0;
    }
    while (buffer_len_ < 56) buffer_[buffer_len_++] = 0x00;
    for (int i = 7; i >= 0; --i) {
        buffer_[buffer_len_++] = static_cast<uint8_t>((total_bits >> (i * 8)) & 0xFF);
    }
    transform(buffer_);

    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[i * 4]     = static_cast<uint8_t>((state_[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<uint8_t>((state_[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<uint8_t>((state_[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<uint8_t>(state_[i] & 0xFF);
    }
    return out;
}

std::array<uint8_t, 32> sha256(const uint8_t* data, size_t len) {
    Sha256 ctx;
    ctx.update(data, len);
    return ctx.final();
}

std::array<uint8_t, 32> sha256(const std::vector<uint8_t>& data) {
    return sha256(data.data(), data.size());
}

std::string to_hex(const uint8_t* data, size_t len) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string s;
    s.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        s.push_back(hex[data[i] >> 4]);
        s.push_back(hex[data[i] & 0x0F]);
    }
    return s;
}

// ============================================================================
// HMAC-SHA256 & PBKDF2-HMAC-SHA256
// ============================================================================
std::array<uint8_t, 32> hmac_sha256(const uint8_t* key, size_t key_len,
                                    const uint8_t* msg, size_t msg_len) {
    uint8_t k_block[64] = {};
    if (key_len > 64) {
        auto h = sha256(key, key_len);
        std::memcpy(k_block, h.data(), 32);
    } else if (key_len > 0) {
        std::memcpy(k_block, key, key_len);
    }

    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = k_block[i] ^ 0x36;
        opad[i] = k_block[i] ^ 0x5C;
    }

    Sha256 inner;
    inner.update(ipad, 64);
    if (msg_len > 0) inner.update(msg, msg_len);
    auto inner_hash = inner.final();

    Sha256 outer;
    outer.update(opad, 64);
    outer.update(inner_hash.data(), 32);
    return outer.final();
}

std::vector<uint8_t> pbkdf2_hmac_sha256(const std::string& password,
                                        const uint8_t* salt, size_t salt_len,
                                        uint32_t iterations, size_t dk_len) {
    std::vector<uint8_t> dk(dk_len);
    const auto* pwd = reinterpret_cast<const uint8_t*>(password.data());
    size_t pwd_len = password.size();

    uint32_t block_count = static_cast<uint32_t>((dk_len + 31) / 32);
    std::vector<uint8_t> salt_block(salt_len + 4);
    if (salt_len > 0) std::memcpy(salt_block.data(), salt, salt_len);

    size_t pos = 0;
    for (uint32_t i = 1; i <= block_count; ++i) {
        salt_block[salt_len]     = static_cast<uint8_t>((i >> 24) & 0xFF);
        salt_block[salt_len + 1] = static_cast<uint8_t>((i >> 16) & 0xFF);
        salt_block[salt_len + 2] = static_cast<uint8_t>((i >> 8) & 0xFF);
        salt_block[salt_len + 3] = static_cast<uint8_t>(i & 0xFF);

        auto u = hmac_sha256(pwd, pwd_len, salt_block.data(), salt_block.size());
        auto t = u;
        for (uint32_t iter = 1; iter < iterations; ++iter) {
            u = hmac_sha256(pwd, pwd_len, u.data(), 32);
            for (int b = 0; b < 32; ++b) t[b] ^= u[b];
        }

        size_t copy_len = std::min<size_t>(32, dk_len - pos);
        std::memcpy(dk.data() + pos, t.data(), copy_len);
        pos += copy_len;
    }
    return dk;
}

// ============================================================================
// SHA-1 / HMAC-SHA1 / PBKDF2-HMAC-SHA1 (for standard AE-2 ZIP interoperability)
// ============================================================================
static inline uint32_t rotl32(uint32_t x, uint32_t n) {
    return (x << n) | (x >> (32 - n));
}

std::array<uint8_t, 20> sha1(const uint8_t* data, size_t len) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    std::vector<uint8_t> msg(data, data + len);
    uint64_t bit_len = static_cast<uint64_t>(len) * 8;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0x00);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));

    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[off + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[off + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[off + i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(msg[off + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;            k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;            k = 0xCA62C1D6; }
            uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl32(b, 30); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    uint32_t hs[5] = {h0, h1, h2, h3, h4};
    std::array<uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i) {
        out[i * 4]     = static_cast<uint8_t>((hs[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<uint8_t>((hs[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<uint8_t>((hs[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<uint8_t>(hs[i] & 0xFF);
    }
    return out;
}

std::array<uint8_t, 20> hmac_sha1(const uint8_t* key, size_t key_len,
                                  const uint8_t* msg, size_t msg_len) {
    uint8_t k_block[64] = {};
    if (key_len > 64) {
        auto h = sha1(key, key_len);
        std::memcpy(k_block, h.data(), 20);
    } else if (key_len > 0) {
        std::memcpy(k_block, key, key_len);
    }
    std::vector<uint8_t> inner(64 + msg_len);
    uint8_t opad[64 + 20];
    for (int i = 0; i < 64; ++i) {
        inner[i] = k_block[i] ^ 0x36;
        opad[i]  = k_block[i] ^ 0x5C;
    }
    if (msg_len > 0) std::memcpy(inner.data() + 64, msg, msg_len);
    auto ih = sha1(inner.data(), inner.size());
    std::memcpy(opad + 64, ih.data(), 20);
    return sha1(opad, sizeof(opad));
}

std::vector<uint8_t> pbkdf2_hmac_sha1(const std::string& password,
                                      const uint8_t* salt, size_t salt_len,
                                      uint32_t iterations, size_t dk_len) {
    std::vector<uint8_t> dk(dk_len);
    const auto* pwd = reinterpret_cast<const uint8_t*>(password.data());
    size_t pwd_len = password.size();
    uint32_t blocks = static_cast<uint32_t>((dk_len + 19) / 20);
    std::vector<uint8_t> sb(salt_len + 4);
    if (salt_len > 0) std::memcpy(sb.data(), salt, salt_len);

    size_t pos = 0;
    for (uint32_t i = 1; i <= blocks; ++i) {
        sb[salt_len]     = static_cast<uint8_t>((i >> 24) & 0xFF);
        sb[salt_len + 1] = static_cast<uint8_t>((i >> 16) & 0xFF);
        sb[salt_len + 2] = static_cast<uint8_t>((i >> 8) & 0xFF);
        sb[salt_len + 3] = static_cast<uint8_t>(i & 0xFF);
        auto u = hmac_sha1(pwd, pwd_len, sb.data(), sb.size());
        auto t = u;
        for (uint32_t it = 1; it < iterations; ++it) {
            u = hmac_sha1(pwd, pwd_len, u.data(), 20);
            for (int b = 0; b < 20; ++b) t[b] ^= u[b];
        }
        size_t n = std::min<size_t>(20, dk_len - pos);
        std::memcpy(dk.data() + pos, t.data(), n);
        pos += n;
    }
    return dk;
}

// ============================================================================
// AES-256 (FIPS 197)
// ============================================================================
static constexpr uint8_t kSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static constexpr uint32_t kRcon[8] = {
    0x01000000, 0x02000000, 0x04000000, 0x08000000,
    0x10000000, 0x20000000, 0x40000000, 0x80000000
};

static inline uint32_t sub_word(uint32_t w) {
    return (static_cast<uint32_t>(kSbox[(w >> 24) & 0xFF]) << 24) |
           (static_cast<uint32_t>(kSbox[(w >> 16) & 0xFF]) << 16) |
           (static_cast<uint32_t>(kSbox[(w >> 8) & 0xFF]) << 8) |
           static_cast<uint32_t>(kSbox[w & 0xFF]);
}

static inline uint32_t rot_word(uint32_t w) {
    return (w << 8) | (w >> 24);
}

static inline uint8_t xtime(uint8_t x) {
    return static_cast<uint8_t>((x << 1) ^ ((x & 0x80) ? 0x1B : 0x00));
}

Aes256::Aes256(const uint8_t key[32]) {
    for (int i = 0; i < 8; ++i) {
        rk_[i] = (static_cast<uint32_t>(key[i * 4]) << 24) |
                 (static_cast<uint32_t>(key[i * 4 + 1]) << 16) |
                 (static_cast<uint32_t>(key[i * 4 + 2]) << 8) |
                 static_cast<uint32_t>(key[i * 4 + 3]);
    }
    for (int i = 8; i < 60; ++i) {
        uint32_t temp = rk_[i - 1];
        if (i % 8 == 0) {
            temp = sub_word(rot_word(temp)) ^ kRcon[(i / 8) - 1];
        } else if (i % 8 == 4) {
            temp = sub_word(temp);
        }
        rk_[i] = rk_[i - 8] ^ temp;
    }
}

void Aes256::encrypt_block(const uint8_t in[16], uint8_t out[16]) const {
    uint8_t s[16];
    std::memcpy(s, in, 16);

    auto add_round_key = [&](int round) {
        for (int c = 0; c < 4; ++c) {
            uint32_t k = rk_[round * 4 + c];
            s[c * 4 + 0] ^= static_cast<uint8_t>((k >> 24) & 0xFF);
            s[c * 4 + 1] ^= static_cast<uint8_t>((k >> 16) & 0xFF);
            s[c * 4 + 2] ^= static_cast<uint8_t>((k >> 8) & 0xFF);
            s[c * 4 + 3] ^= static_cast<uint8_t>(k & 0xFF);
        }
    };

    add_round_key(0);
    for (int round = 1; round <= 14; ++round) {
        // SubBytes
        for (int i = 0; i < 16; ++i) s[i] = kSbox[s[i]];

        // ShiftRows (column-major state)
        uint8_t t;
        t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
        std::swap(s[2], s[10]); std::swap(s[6], s[14]);
        t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;

        // MixColumns (all rounds except final)
        if (round < 14) {
            for (int c = 0; c < 4; ++c) {
                uint8_t a0 = s[c * 4 + 0];
                uint8_t a1 = s[c * 4 + 1];
                uint8_t a2 = s[c * 4 + 2];
                uint8_t a3 = s[c * 4 + 3];
                uint8_t x = a0 ^ a1 ^ a2 ^ a3;
                s[c * 4 + 0] ^= x ^ xtime(a0 ^ a1);
                s[c * 4 + 1] ^= x ^ xtime(a1 ^ a2);
                s[c * 4 + 2] ^= x ^ xtime(a2 ^ a3);
                s[c * 4 + 3] ^= x ^ xtime(a3 ^ a0);
            }
        }
        add_round_key(round);
    }
    std::memcpy(out, s, 16);
}

// ============================================================================
// AES-256-GCM (NIST SP 800-38D)
// ============================================================================
static void ghash_mul(const uint8_t X[16], const uint8_t Y[16], uint8_t Z[16]) {
    uint8_t z[16] = {};
    uint8_t v[16];
    std::memcpy(v, Y, 16);

    for (int i = 0; i < 128; ++i) {
        if ((X[i / 8] >> (7 - (i % 8))) & 1) {
            for (int j = 0; j < 16; ++j) z[j] ^= v[j];
        }
        bool lsb = (v[15] & 1) != 0;
        for (int j = 15; j > 0; --j) {
            v[j] = static_cast<uint8_t>((v[j] >> 1) | ((v[j - 1] & 1) << 7));
        }
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xE1;
    }
    std::memcpy(Z, z, 16);
}

static void ghash_update(const uint8_t H[16], uint8_t Y[16], const uint8_t* data, size_t len) {
    size_t pos = 0;
    while (pos < len) {
        uint8_t block[16] = {};
        size_t n = std::min<size_t>(16, len - pos);
        std::memcpy(block, data + pos, n);
        for (int i = 0; i < 16; ++i) Y[i] ^= block[i];
        ghash_mul(Y, H, Y);
        pos += n;
    }
}

static void inc32(uint8_t counter[16]) {
    for (int i = 15; i >= 12; --i) {
        if (++counter[i] != 0) break;
    }
}

static void gctr_crypt(const Aes256& aes, const uint8_t icb[16],
                       const uint8_t* in, uint8_t* out, size_t len) {
    uint8_t cb[16];
    std::memcpy(cb, icb, 16);
    uint8_t keystream[16];
    size_t pos = 0;
    while (pos < len) {
        aes.encrypt_block(cb, keystream);
        inc32(cb);
        size_t n = std::min<size_t>(16, len - pos);
        for (size_t i = 0; i < n; ++i) {
            out[pos + i] = in[pos + i] ^ keystream[i];
        }
        pos += n;
    }
}

static void gcm_compute_tag(const Aes256& aes, const uint8_t H[16], const uint8_t j0[16],
                            const uint8_t* aad, size_t aad_len,
                            const uint8_t* ct, size_t ct_len,
                            uint8_t tag_out[16]) {
    uint8_t S[16] = {};
    if (aad_len > 0) ghash_update(H, S, aad, aad_len);
    if (ct_len > 0) ghash_update(H, S, ct, ct_len);

    uint8_t len_block[16] = {};
    uint64_t aad_bits = static_cast<uint64_t>(aad_len) * 8;
    uint64_t ct_bits = static_cast<uint64_t>(ct_len) * 8;
    for (int i = 0; i < 8; ++i) {
        len_block[i] = static_cast<uint8_t>((aad_bits >> (56 - i * 8)) & 0xFF);
        len_block[8 + i] = static_cast<uint8_t>((ct_bits >> (56 - i * 8)) & 0xFF);
    }
    ghash_update(H, S, len_block, 16);

    uint8_t ej0[16];
    aes.encrypt_block(j0, ej0);
    for (int i = 0; i < 16; ++i) tag_out[i] = S[i] ^ ej0[i];
}

std::vector<uint8_t> aes256_gcm_encrypt(const uint8_t key[32],
                                        const uint8_t nonce[12],
                                        const uint8_t* aad, size_t aad_len,
                                        const uint8_t* plaintext, size_t pt_len,
                                        uint8_t tag_out[16]) {
    Aes256 aes(key);
    uint8_t zeros[16] = {};
    uint8_t H[16];
    aes.encrypt_block(zeros, H);

    uint8_t j0[16] = {};
    std::memcpy(j0, nonce, 12);
    j0[15] = 1;

    uint8_t icb[16];
    std::memcpy(icb, j0, 16);
    inc32(icb);

    std::vector<uint8_t> ct(pt_len);
    if (pt_len > 0) {
        gctr_crypt(aes, icb, plaintext, ct.data(), pt_len);
    }
    gcm_compute_tag(aes, H, j0, aad, aad_len, ct.data(), ct.size(), tag_out);
    return ct;
}

bool aes256_gcm_decrypt(const uint8_t key[32],
                        const uint8_t nonce[12],
                        const uint8_t* aad, size_t aad_len,
                        const uint8_t* ciphertext, size_t ct_len,
                        const uint8_t tag[16],
                        std::vector<uint8_t>& out_plaintext) {
    Aes256 aes(key);
    uint8_t zeros[16] = {};
    uint8_t H[16];
    aes.encrypt_block(zeros, H);

    uint8_t j0[16] = {};
    std::memcpy(j0, nonce, 12);
    j0[15] = 1;

    uint8_t expected_tag[16];
    gcm_compute_tag(aes, H, j0, aad, aad_len, ciphertext, ct_len, expected_tag);
    if (!constant_time_equal(expected_tag, tag, 16)) {
        out_plaintext.clear();
        return false;
    }

    uint8_t icb[16];
    std::memcpy(icb, j0, 16);
    inc32(icb);

    out_plaintext.resize(ct_len);
    if (ct_len > 0) {
        gctr_crypt(aes, icb, ciphertext, out_plaintext.data(), ct_len);
    }
    return true;
}

void aes256_ctr_crypt(const uint8_t key[32], uint8_t* data, size_t len, uint64_t start_counter) {
    Aes256 aes(key);
    uint64_t counter = start_counter;
    uint8_t ctr_block[16] = {};
    uint8_t ks[16];
    size_t pos = 0;
    while (pos < len) {
        // WinZip AE-2 uses little-endian counter in first 8 bytes
        for (int i = 0; i < 8; ++i) {
            ctr_block[i] = static_cast<uint8_t>((counter >> (i * 8)) & 0xFF);
        }
        aes.encrypt_block(ctr_block, ks);
        ++counter;
        size_t n = std::min<size_t>(16, len - pos);
        for (size_t i = 0; i < n; ++i) {
            data[pos + i] ^= ks[i];
        }
        pos += n;
    }
}

// ============================================================================
// Legacy PKWARE ZipCrypto
// ============================================================================
static inline uint32_t crc32_byte(uint32_t crc, uint8_t b) {
    return crc32_table()[(crc ^ b) & 0xFF] ^ (crc >> 8);
}

ZipCrypto::ZipCrypto(const std::string& password) {
    for (unsigned char c : password) {
        update_keys(c);
    }
}

uint8_t ZipCrypto::decrypt_key_byte() const {
    uint16_t temp = static_cast<uint16_t>(k2_ | 2);
    return static_cast<uint8_t>((temp * (temp ^ 1)) >> 8);
}

void ZipCrypto::update_keys(uint8_t b) {
    k0_ = crc32_byte(k0_, b);
    k1_ = (k1_ + (k0_ & 0xFF)) * 134775813U + 1U;
    k2_ = crc32_byte(k2_, static_cast<uint8_t>(k1_ >> 24));
}

uint8_t ZipCrypto::encrypt_byte(uint8_t b) {
    uint8_t t = decrypt_key_byte();
    update_keys(b);
    return b ^ t;
}

uint8_t ZipCrypto::decrypt_byte(uint8_t b) {
    uint8_t c = b ^ decrypt_key_byte();
    update_keys(c);
    return c;
}

bool constant_time_equal(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}

} // namespace openrar::crypto
