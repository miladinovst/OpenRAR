#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// Self-contained cryptographic primitives (zero external dependencies, Windows & Linux).
namespace openrar::crypto {

/// Generates `len` cryptographically secure random bytes using OS CSPRNG
/// (`BCryptGenRandom` on Windows, `/dev/urandom` on POSIX).
bool random_bytes(uint8_t* out, size_t len);

/// Convenience overload returning a newly allocated vector of `len` secure random bytes.
std::vector<uint8_t> random_bytes(size_t len);

/// Incrementally updates an IEEE 802.3 CRC32 checksum with `data[0..len)`.
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len);

/// Computes a one-shot IEEE 802.3 CRC32 checksum over `data[0..len)`.
uint32_t crc32(const uint8_t* data, size_t len);

/// Computes a one-shot IEEE 802.3 CRC32 checksum over a byte vector.
uint32_t crc32(const std::vector<uint8_t>& data);

/// Streaming FIPS 180-4 SHA-256 hash context.
class Sha256 {
public:
    Sha256();
    /// Absorbs `data[0..len)` into the running SHA-256 state.
    void update(const uint8_t* data, size_t len);
    /// Pads the message and returns the final 32-byte digest.
    std::array<uint8_t, 32> final();

private:
    void transform(const uint8_t block[64]);
    uint32_t state_[8];
    uint64_t bit_len_ = 0;
    uint8_t buffer_[64];
    size_t buffer_len_ = 0;
};

/// Computes a one-shot 32-byte SHA-256 digest over `data[0..len)`.
std::array<uint8_t, 32> sha256(const uint8_t* data, size_t len);

/// Computes a one-shot 32-byte SHA-256 digest over a byte vector.
std::array<uint8_t, 32> sha256(const std::vector<uint8_t>& data);

/// Encodes raw bytes as a lowercase hexadecimal string (2 chars per byte).
std::string to_hex(const uint8_t* data, size_t len);

/// Computes RFC 2104 HMAC-SHA256(key, msg).
std::array<uint8_t, 32> hmac_sha256(const uint8_t* key, size_t key_len,
                                    const uint8_t* msg, size_t msg_len);

/// Derives a `dk_len`-byte key from a user password and random salt using RFC 2898 PBKDF2-HMAC-SHA256.
std::vector<uint8_t> pbkdf2_hmac_sha256(const std::string& password,
                                        const uint8_t* salt, size_t salt_len,
                                        uint32_t iterations, size_t dk_len);

/// Computes a 20-byte SHA-1 digest (used strictly for standard AE-2 ZIP encryption interoperability).
std::array<uint8_t, 20> sha1(const uint8_t* data, size_t len);

/// Computes RFC 2104 HMAC-SHA1(key, msg) (used for standard AE-2 ZIP authentication tags).
std::array<uint8_t, 20> hmac_sha1(const uint8_t* key, size_t key_len,
                                  const uint8_t* msg, size_t msg_len);

/// Derives a `dk_len`-byte key using PBKDF2-HMAC-SHA1 (standard AE-2 ZIP key derivation).
std::vector<uint8_t> pbkdf2_hmac_sha1(const std::string& password,
                                      const uint8_t* salt, size_t salt_len,
                                      uint32_t iterations, size_t dk_len);

/// FIPS 197 AES-256 block cipher (14 rounds, 256-bit key).
class Aes256 {
public:
    /// Expands a 32-byte key into 15 round keys.
    explicit Aes256(const uint8_t key[32]);
    /// Encrypts a single 16-byte plaintext block `in` into `out`.
    void encrypt_block(const uint8_t in[16], uint8_t out[16]) const;

private:
    uint32_t rk_[60]; // 15 round keys * 4 words
};

/// NIST SP 800-38D AES-256-GCM authenticated encryption.
/// Returns ciphertext (same size as plaintext) and writes a 16-byte authentication tag to `tag_out`.
std::vector<uint8_t> aes256_gcm_encrypt(const uint8_t key[32],
                                        const uint8_t nonce[12],
                                        const uint8_t* aad, size_t aad_len,
                                        const uint8_t* plaintext, size_t pt_len,
                                        uint8_t tag_out[16]);

/// NIST SP 800-38D AES-256-GCM authenticated decryption.
/// Verifies `tag` in constant time before decrypting; returns false on wrong key or tampering.
bool aes256_gcm_decrypt(const uint8_t key[32],
                        const uint8_t nonce[12],
                        const uint8_t* aad, size_t aad_len,
                        const uint8_t* ciphertext, size_t ct_len,
                        const uint8_t tag[16],
                        std::vector<uint8_t>& out_plaintext);

/// In-place AES-256-CTR stream encryption/decryption (used for AE-2 ZIP encryption).
void aes256_ctr_crypt(const uint8_t key[32], uint8_t* data, size_t len, uint64_t start_counter = 1);

/// Legacy PKWARE ZipCrypto stream cipher for backwards compatibility with older ZIP tools.
class ZipCrypto {
public:
    explicit ZipCrypto(const std::string& password);
    uint8_t encrypt_byte(uint8_t b);
    uint8_t decrypt_byte(uint8_t b);

private:
    uint8_t decrypt_key_byte() const;
    void update_keys(uint8_t b);
    uint32_t k0_ = 305419896;
    uint32_t k1_ = 591751049;
    uint32_t k2_ = 878082192;
};

/// Timing-safe byte comparison (prevents timing side-channels when verifying MACs/tags).
bool constant_time_equal(const uint8_t* a, const uint8_t* b, size_t len);

} // namespace openrar::crypto
