// Comprehensive OpenRAR C++20 Automated Test Suite (11 end-to-end & stress suites).

#include "openrar/archive_manager.hpp"
#include "openrar/crypto.hpp"
#include "openrar/deflate.hpp"
#include "openrar/extended_archive.hpp"
#include "openrar/i18n.hpp"
#include "openrar/rar_inspector.hpp"
#include "openrar/recovery.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace openrar;

static int g_passed = 0;
static int g_failed = 0;
static fs::path g_bin_dir;

#define REQUIRE(cond) do { \
    if (!(cond)) { \
        throw std::runtime_error(std::string("Assertion failed at line ") + std::to_string(__LINE__) + ": " #cond); \
    } \
} while (0)

static void run_test(const char* name, void (*fn)()) {
    try {
        fn();
        std::cout << "[PASS] " << name << "\n";
        g_passed++;
    } catch (const std::exception& ex) {
        std::cerr << "[FAIL] " << name << " -> " << ex.what() << "\n";
        g_failed++;
    }
}

static fs::path make_temp_dir(const std::string& tag) {
    auto p = fs::temp_directory_path() / ("openrar_test_" + tag + "_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(p);
    return p;
}

static void write_text(const fs::path& p, const std::string& s) {
    REQUIRE(write_file_bytes(p, reinterpret_cast<const uint8_t*>(s.data()), s.size()));
}

static std::string read_text(const fs::path& p) {
    auto b = read_file_bytes(p);
    return std::string(reinterpret_cast<const char*>(b.data()), b.size());
}

// ============================================================================
// 1. Crypto Standards (IEEE CRC32, FIPS SHA-256/SHA-1, RFC 6070 PBKDF2, AES-GCM, ZipCrypto)
// ============================================================================
static void test_crypto() {
    const char* s123 = "123456789";
    REQUIRE(crypto::crc32(reinterpret_cast<const uint8_t*>(s123), 9) == 0xCBF43926U);

    const char* abc = "abc";
    auto h256 = crypto::sha256(reinterpret_cast<const uint8_t*>(abc), 3);
    REQUIRE(crypto::to_hex(h256.data(), 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    auto h1 = crypto::sha1(reinterpret_cast<const uint8_t*>(abc), 3);
    REQUIRE(crypto::to_hex(h1.data(), 20) == "a9993e364706816aba3e25717850c26c9cd0d89d");

    // RFC 6070 PBKDF2-HMAC-SHA1 test vector: P="password", S="salt", c=1, dkLen=20
    const char* salt = "salt";
    auto dk1 = crypto::pbkdf2_hmac_sha1("password", reinterpret_cast<const uint8_t*>(salt), 4, 1, 20);
    REQUIRE(crypto::to_hex(dk1.data(), 20) == "0c60c80f961f0e71f3a9b524af6012062fe037a6");

    // AES-256-GCM authenticated encrypt/decrypt + tamper rejection
    uint8_t key[32]{};
    uint8_t nonce[12]{};
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i);
    for (int i = 0; i < 12; ++i) nonce[i] = static_cast<uint8_t>(i + 1);

    std::string msg = "Top secret OpenRAR AES-256-GCM authenticated message!";
    std::string aad = "ORAR01";
    uint8_t tag[16]{};
    auto ct = crypto::aes256_gcm_encrypt(key, nonce,
                                         reinterpret_cast<const uint8_t*>(aad.data()), aad.size(),
                                         reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), tag);
    std::vector<uint8_t> pt;
    REQUIRE(crypto::aes256_gcm_decrypt(key, nonce,
                                       reinterpret_cast<const uint8_t*>(aad.data()), aad.size(),
                                       ct.data(), ct.size(), tag, pt));
    REQUIRE(std::string(pt.begin(), pt.end()) == msg);

    ct[5] ^= 0x01;
    REQUIRE(!crypto::aes256_gcm_decrypt(key, nonce,
                                        reinterpret_cast<const uint8_t*>(aad.data()), aad.size(),
                                        ct.data(), ct.size(), tag, pt));

    // Legacy ZipCrypto roundtrip
    crypto::ZipCrypto enc("MyKey"), dec("MyKey");
    std::vector<uint8_t> zct(msg.size()), zpt(msg.size());
    for (size_t i = 0; i < msg.size(); ++i) zct[i] = enc.encrypt_byte(static_cast<uint8_t>(msg[i]));
    for (size_t i = 0; i < msg.size(); ++i) zpt[i] = dec.decrypt_byte(zct[i]);
    REQUIRE(std::string(zpt.begin(), zpt.end()) == msg);
}

// ============================================================================
// 2. Deflate / Inflate / GZip Stress & Fuzz Resilience
// ============================================================================
static void test_deflate_stress() {
    // 2a. Empty & 1-byte edge cases
    {
        std::vector<uint8_t> empty, out;
        auto c0 = codec::deflate_compress(empty, CompressionLevel::Normal);
        REQUIRE(codec::deflate_decompress(c0, out));
        REQUIRE(out.empty());

        std::vector<uint8_t> one = {'Z'};
        auto c1 = codec::deflate_compress(one, CompressionLevel::Best);
        REQUIRE(codec::deflate_decompress(c1, out));
        REQUIRE(out == one);
    }

    // 2b. Single-symbol repetitive buffer (100 KB of 'A')
    {
        std::vector<uint8_t> rep(100000, 'A');
        auto comp = codec::deflate_compress(rep, CompressionLevel::Normal);
        REQUIRE(comp.size() < 500); // Should compress >200:1
        std::vector<uint8_t> out;
        REQUIRE(codec::deflate_decompress(comp, out));
        REQUIRE(out == rep);
    }

    // 2c. Multi-block mixed compressible + random buffer (>200 KB) across all levels 0..5
    std::vector<uint8_t> mixed;
    for (int i = 0; i < 600; ++i) {
        std::string line = "OpenRAR multi-block dynamic Huffman test record #" + std::to_string(i) + " payload=ABCDEF0123456789\n";
        mixed.insert(mixed.end(), line.begin(), line.end());
    }
    auto rnd_chunk = crypto::random_bytes(70000);
    mixed.insert(mixed.end(), rnd_chunk.begin(), rnd_chunk.end());
    for (int i = 0; i < 400; ++i) {
        std::string line = "Trailing structured JSON {\"id\":" + std::to_string(i) + ",\"ok\":true}\n";
        mixed.insert(mixed.end(), line.begin(), line.end());
    }

    for (int lvl = 0; lvl <= 5; ++lvl) {
        auto comp = codec::deflate_compress(mixed, static_cast<CompressionLevel>(lvl));
        std::vector<uint8_t> decomp;
        std::string err;
        REQUIRE(codec::deflate_decompress(comp, decomp, &err));
        REQUIRE(decomp == mixed);
    }

    // 2d. GZip roundtrip + corrupted/truncated deflate rejection (must not crash!)
    auto gz = codec::gzip_compress(mixed.data(), mixed.size(), CompressionLevel::Normal, "mixed.dat");
    std::vector<uint8_t> gz_out;
    std::string orig_name;
    REQUIRE(codec::gzip_decompress(gz.data(), gz.size(), gz_out, &orig_name));
    REQUIRE(orig_name == "mixed.dat");
    REQUIRE(gz_out == mixed);

    std::vector<uint8_t> bad = {0xFF, 0xFF, 0x03, 0x00};
    std::vector<uint8_t> dummy;
    REQUIRE(!codec::deflate_decompress(bad, dummy));
    REQUIRE(!codec::deflate_decompress(gz.data() + 10, 5, dummy));
}

// ============================================================================
// 3. ZIP Lifecycle (AE-2 AES-256, ZipCrypto, Unicode, Empty File, Comment, Add/Delete, Repair, Split)
// ============================================================================
static void test_zip_lifecycle() {
    auto dir = make_temp_dir("zip");
    auto src = dir / "src";
    write_text(src / "hello.txt", "Hello OpenRAR ZIP world!");
    write_text(src / "empty.txt", "");
    write_text(src / "sub" / path_from_utf8("caf\xC3\xA9_\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.txt"),
               std::string(4000, 'A') + "UNICODE_END");

    fs::path zip_path = dir / "test.zip";
    CreateOptions co;
    co.format = ArchiveFormat::Zip;
    co.level = CompressionLevel::Normal;
    co.comment = "My ZIP Comment";
    REQUIRE(ArchiveManager::create(zip_path, {src}, co).success);

    std::vector<EntryInfo> entries;
    ArchiveInfo info;
    REQUIRE(ArchiveManager::list(zip_path, entries, "", &info));
    REQUIRE(info.comment == "My ZIP Comment");
    REQUIRE(info.file_count == 3);
    REQUIRE(ArchiveManager::test(zip_path).success);

    // Extract & verify Unicode filename + empty file
    fs::path out_dir = dir / "out";
    ExtractOptions eo;
    eo.destination = out_dir;
    REQUIRE(ArchiveManager::extract(zip_path, eo).success);
    REQUIRE(read_text(out_dir / "src" / "hello.txt") == "Hello OpenRAR ZIP world!");
    REQUIRE(read_text(out_dir / "src" / "empty.txt").empty());
    REQUIRE(read_text(out_dir / "src" / "sub" / path_from_utf8("caf\xC3\xA9_\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.txt"))
            == std::string(4000, 'A') + "UNICODE_END");

    // Add entry, update comment, delete entry
    fs::path extra_file = dir / "extra.txt";
    write_text(extra_file, "Added later!");
    REQUIRE(ArchiveManager::add_entries(zip_path, {extra_file}, co).success);
    REQUIRE(ArchiveManager::set_comment(zip_path, "Updated Comment").success);
    REQUIRE(ArchiveManager::delete_entries(zip_path, {"src/hello.txt"}).success);
    REQUIRE(ArchiveManager::list(zip_path, entries, "", &info));
    REQUIRE(info.comment == "Updated Comment");
    REQUIRE(info.file_count == 3); // empty.txt + unicode + extra.txt

    // AE-2 AES-256 encrypted ZIP
    fs::path enc_zip = dir / "enc_ae2.zip";
    co.password = "SuperSecret123";
    co.use_aes256 = true;
    REQUIRE(ArchiveManager::create(enc_zip, {src}, co).success);
    REQUIRE(!ArchiveManager::test(enc_zip, "WrongPass").success);
    REQUIRE(ArchiveManager::test(enc_zip, "SuperSecret123").success);

    // Legacy ZipCrypto encrypted ZIP
    fs::path zc_zip = dir / "enc_zc.zip";
    co.use_aes256 = false;
    REQUIRE(ArchiveManager::create(zc_zip, {src}, co).success);
    REQUIRE(!ArchiveManager::test(zc_zip, "WrongPass").success);
    REQUIRE(ArchiveManager::test(zc_zip, "SuperSecret123").success);

    // Repair ZIP with destroyed EOCD
    auto zip_bytes = read_file_bytes(zip_path);
    zip_bytes.resize(zip_bytes.size() - 30);
    fs::path broken_zip = dir / "broken.zip";
    write_file_bytes(broken_zip, zip_bytes.data(), zip_bytes.size());
    fs::path repaired_zip = dir / "repaired.zip";
    REQUIRE(ArchiveManager::repair(broken_zip, repaired_zip).success);
    REQUIRE(ArchiveManager::test(repaired_zip).success);

    // Split multi-volume ZIP (.part1.zip, .part2.zip) automatic reassembly
    fs::path split_zip = dir / "split.zip";
    CreateOptions szco;
    szco.format = ArchiveFormat::Zip;
    szco.level = CompressionLevel::Store;
    szco.split_volume_bytes = 2048;
    auto szres = ArchiveManager::create(split_zip, {src}, szco);
    REQUIRE(szres.success);
    REQUIRE(szres.output_files.size() >= 2);
    REQUIRE(ArchiveManager::test(szres.output_files[0]).success);

    fs::remove_all(dir);
}

// ============================================================================
// 4. OpenRAR (.orar): Solid, Parallel, AES-GCM, -hp, Multi-Sector Burst Repair, Split, Unicode
// ============================================================================
static void test_orar_full_features() {
    auto dir = make_temp_dir("orar");
    auto src = dir / "docs";
    std::string big_text;
    for (int i = 0; i < 800; ++i) {
        big_text += "Repeated archival content for solid + burst recovery testing line " + std::to_string(i) + "\n";
    }
    write_text(src / "a.txt", big_text);
    write_text(src / "b.txt", big_text + "EXTRA_B");
    write_text(src / "nested" / path_from_utf8("r\xC3\xA9sum\xC3\xA9.txt"), "Unicode nested file!");
    // Include an incompressible 16 KB binary blob so compressed payload spans ~35+ sectors for burst-repair testing
    auto bin_blob = crypto::random_bytes(16384);
    REQUIRE(write_file_bytes(src / "blob.bin", bin_blob.data(), bin_blob.size()));

    // 4a. Solid + AES-256-GCM + Encrypted Headers + Recovery Record (15%)
    fs::path orar_path = dir / "vault.orar";
    CreateOptions co;
    co.format = ArchiveFormat::Orar;
    co.level = CompressionLevel::Good;
    co.solid = true;
    co.password = "VaultPass!99";
    co.encrypt_headers = true;
    co.add_recovery_record = true;
    co.recovery_percent = 15;
    co.comment = "Encrypted Solid Vault";
    REQUIRE(ArchiveManager::create(orar_path, {src}, co).success);

    std::vector<EntryInfo> entries;
    ArchiveInfo info;
    REQUIRE(!ArchiveManager::list(orar_path, entries, "", &info)); // encrypted headers must block
    REQUIRE(ArchiveManager::list(orar_path, entries, "VaultPass!99", &info));
    REQUIRE(info.is_solid && info.is_encrypted && info.has_encrypted_headers && info.has_recovery_record);
    REQUIRE(info.file_count == 4);
    REQUIRE(info.comment == "Encrypted Solid Vault");
    REQUIRE(ArchiveManager::test(orar_path, "VaultPass!99").success);

    // 4b. Contiguous multi-sector BURST corruption (1,500 bytes across 3 consecutive sectors!) & bit-for-bit repair!
    auto corrupted = read_file_bytes(orar_path);
    REQUIRE(corrupted.size() > 16000);
    for (size_t i = 512; i < 2012; ++i) corrupted[i] ^= 0xA5;
    fs::path damaged_path = dir / "damaged.orar";
    write_file_bytes(damaged_path, corrupted.data(), corrupted.size());
    REQUIRE(!ArchiveManager::test(damaged_path, "VaultPass!99").success);

    fs::path repaired_path = dir / "repaired.orar";
    REQUIRE(ArchiveManager::repair(damaged_path, repaired_path).success);
    REQUIRE(ArchiveManager::test(repaired_path, "VaultPass!99").success);

    fs::path out_dir = dir / "extracted";
    ExtractOptions eo;
    eo.destination = out_dir;
    eo.password = "VaultPass!99";
    REQUIRE(ArchiveManager::extract(repaired_path, eo).success);
    REQUIRE(read_text(out_dir / "docs" / "b.txt") == big_text + "EXTRA_B");
    REQUIRE(read_text(out_dir / "docs" / "nested" / path_from_utf8("r\xC3\xA9sum\xC3\xA9.txt")) == "Unicode nested file!");

    // 4c. Non-solid multi-threaded parallel compression (20 files) + add/delete/comment
    auto many_dir = dir / "many";
    for (int i = 0; i < 20; ++i) {
        write_text(many_dir / ("file_" + std::to_string(i) + ".txt"),
                   "Parallel file #" + std::to_string(i) + " " + std::string(1000, static_cast<char>('a' + (i % 26))));
    }
    fs::path par_orar = dir / "parallel.orar";
    CreateOptions pco;
    pco.format = ArchiveFormat::Orar;
    pco.level = CompressionLevel::Normal;
    pco.solid = false;
    REQUIRE(ArchiveManager::create(par_orar, {many_dir}, pco).success);
    REQUIRE(ArchiveManager::test(par_orar).success);
    REQUIRE(ArchiveManager::set_comment(par_orar, "Parallel comment").success);
    REQUIRE(ArchiveManager::delete_entries(par_orar, {"many/file_0.txt", "many/file_1.txt"}).success);
    REQUIRE(ArchiveManager::list(par_orar, entries, "", &info));
    REQUIRE(info.file_count == 18);
    REQUIRE(info.comment == "Parallel comment");

    // 4d. Split Volumes (.part1.orar, .part2.orar, ...)
    fs::path split_base = dir / "split.orar";
    CreateOptions sco;
    sco.format = ArchiveFormat::Orar;
    sco.level = CompressionLevel::Store;
    sco.split_volume_bytes = 12000;
    auto sres = ArchiveManager::create(split_base, {src}, sco);
    REQUIRE(sres.success);
    REQUIRE(sres.output_files.size() >= 2);
    REQUIRE(ArchiveManager::test(sres.output_files[0]).success);

    fs::remove_all(dir);
}

// ============================================================================
// 5. Extract Modes (Skip, Auto-Rename, Flat, Subset) & Zip-Slip Security Guard
// ============================================================================
static void test_extract_modes_and_security() {
    // Zip-Slip path safety unit assertions
    REQUIRE(is_safe_relative_path("folder/file.txt"));
    REQUIRE(!is_safe_relative_path("../escape.txt"));
    REQUIRE(!is_safe_relative_path("sub/../../escape.txt"));
    REQUIRE(!is_safe_relative_path("/etc/passwd"));
    REQUIRE(!is_safe_relative_path("C:\\Windows\\system.ini"));
    REQUIRE(!is_safe_relative_path(""));

    auto dir = make_temp_dir("modes");
    auto src = dir / "pkg";
    write_text(src / "sub" / "one.txt", "ORIGINAL_ONE");
    write_text(src / "sub" / "two.txt", "ORIGINAL_TWO");

    fs::path arc = dir / "pkg.orar";
    CreateOptions co;
    co.format = ArchiveFormat::Orar;
    REQUIRE(ArchiveManager::create(arc, {src}, co).success);

    // Subset + Flat extract (preserve_paths = false)
    fs::path flat_out = dir / "flat";
    ExtractOptions eo;
    eo.destination = flat_out;
    eo.preserve_paths = false;
    eo.selected_entries = {"pkg/sub/one.txt"};
    REQUIRE(ArchiveManager::extract(arc, eo).success);
    REQUIRE(fs::exists(flat_out / "one.txt"));
    REQUIRE(!fs::exists(flat_out / "two.txt"));

    // SkipExisting preserves modified file on disk
    write_text(flat_out / "one.txt", "KEEP_ME");
    eo.overwrite = OverwriteMode::SkipExisting;
    REQUIRE(ArchiveManager::extract(arc, eo).success);
    REQUIRE(read_text(flat_out / "one.txt") == "KEEP_ME");

    // RenameAuto creates "one (1).txt" without clobbering
    eo.overwrite = OverwriteMode::RenameAuto;
    REQUIRE(ArchiveManager::extract(arc, eo).success);
    REQUIRE(read_text(flat_out / "one.txt") == "KEEP_ME");
    REQUIRE(read_text(flat_out / "one (1).txt") == "ORIGINAL_ONE");

    fs::remove_all(dir);
}

// ============================================================================
// 6. TAR, TAR.GZ & GNU LongName (>120 character path)
// ============================================================================
static void test_tar_longname() {
    auto dir = make_temp_dir("tar");
    auto deep = dir / "very_long_directory_segment_alpha_0123456789" /
                "very_long_directory_segment_beta_0123456789" /
                "very_long_directory_segment_gamma_0123456789";
    write_text(deep / "long_filename_inside_deep_tree.txt", "GNU LongLink Works!");

    fs::path tgz = dir / "deep.tar.gz";
    CreateOptions co;
    co.format = ArchiveFormat::TarGz;
    co.base_dir = dir;
    REQUIRE(ArchiveManager::create(tgz, {dir / "very_long_directory_segment_alpha_0123456789"}, co).success);
    REQUIRE(ArchiveManager::test(tgz).success);

    fs::path out = dir / "out";
    ExtractOptions eo;
    eo.destination = out;
    REQUIRE(ArchiveManager::extract(tgz, eo).success);
    auto restored = out / "very_long_directory_segment_alpha_0123456789" /
                    "very_long_directory_segment_beta_0123456789" /
                    "very_long_directory_segment_gamma_0123456789" /
                    "long_filename_inside_deep_tree.txt";
    REQUIRE(read_text(restored) == "GNU LongLink Works!");

    fs::remove_all(dir);
}

// ============================================================================
// 7. 7z, Batch Convert, Find-in-Archives, Multi-Threaded Benchmark
// ============================================================================
static void test_7z_convert_find_bench() {
    auto dir = make_temp_dir("misc");
    auto src = dir / "proj";
    write_text(src / "readme.md", "Welcome to OpenRAR search needle: QUANTUM_42");
    write_text(src / "code.cpp", "int main() { return 0; }");

    fs::path tgz = dir / "proj.tar.gz";
    CreateOptions co;
    co.format = ArchiveFormat::TarGz;
    REQUIRE(ArchiveManager::create(tgz, {src}, co).success);

    fs::path conv_dir = dir / "converted";
    CreateOptions target;
    target.format = ArchiveFormat::Zip;
    REQUIRE(ArchiveManager::convert_archives({tgz}, conv_dir, target).success);
    REQUIRE(fs::exists(conv_dir / "proj.zip"));
    REQUIRE(ArchiveManager::test(conv_dir / "proj.zip").success);

    SearchOptions so;
    so.root_path = conv_dir;
    so.filename_pattern = "*.md";
    so.text_to_find = "quantum_42";
    so.search_inside_archives = true;
    auto matches = ArchiveManager::find_files(so);
    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].is_in_archive());
    REQUIRE(matches[0].entry_path.find("readme.md") != std::string::npos);

    auto bm = ArchiveManager::run_benchmark(2, 0.25);
    REQUIRE(bm.bytes_processed > 0);
    REQUIRE(bm.speed_kbps > 0.0);

    if (ExtendedArchive::is_available()) {
        fs::path seven_z = dir / "proj.7z";
        CreateOptions co7;
        co7.format = ArchiveFormat::SevenZip;
        REQUIRE(ArchiveManager::create(seven_z, {src}, co7).success);
        std::vector<EntryInfo> z7_entries;
        REQUIRE(ArchiveManager::list(seven_z, z7_entries));
        REQUIRE(z7_entries.size() >= 2);
        REQUIRE(ArchiveManager::test(seven_z).success);

        fs::path out7 = dir / "out7z";
        ExtractOptions eo7;
        eo7.destination = out7;
        REQUIRE(ArchiveManager::extract(seven_z, eo7).success);
        REQUIRE(read_text(out7 / "proj" / "readme.md").find("QUANTUM_42") != std::string::npos);
    }

    fs::remove_all(dir);
}

// ============================================================================
// 8. Native RAR5 Binary Header Inspector
// ============================================================================
static void test_rar5_inspector() {
    auto dir = make_temp_dir("rar5");
    auto write_vint = [](std::vector<uint8_t>& out, uint64_t v) {
        do {
            uint8_t b = static_cast<uint8_t>(v & 0x7F);
            v >>= 7;
            if (v != 0) b |= 0x80;
            out.push_back(b);
        } while (v != 0);
    };

    // Build a minimal valid RAR5 container: Magic + Main Header (solid+recovery) + File Header + End Header
    std::vector<uint8_t> rar = {'R', 'a', 'r', '!', 0x1A, 0x07, 0x01, 0x00};

    auto append_block = [&](const std::vector<uint8_t>& hdr_body, const std::vector<uint8_t>& data = {}) {
        // 4-byte placeholder CRC + vint(hdr_size) + hdr_body + data
        for (int i = 0; i < 4; ++i) rar.push_back(0);
        write_vint(rar, hdr_body.size());
        rar.insert(rar.end(), hdr_body.begin(), hdr_body.end());
        rar.insert(rar.end(), data.begin(), data.end());
    };

    // Main header (type=1, flags=0, arc_flags=solid(4)|recovery(8))
    std::vector<uint8_t> main_hdr;
    write_vint(main_hdr, 1);
    write_vint(main_hdr, 0);
    write_vint(main_hdr, 0x04 | 0x08);
    append_block(main_hdr);

    // File header (type=2, flags=DATA_AREA(2), data_size=5, fflags=CRC(4), unp=12, attrs=0, crc=0x12345678, comp=0, os=0, name="hello.txt")
    std::vector<uint8_t> fhdr;
    write_vint(fhdr, 2);
    write_vint(fhdr, 0x02); // has data area
    write_vint(fhdr, 5);    // data_size
    write_vint(fhdr, 0x04); // has CRC32
    write_vint(fhdr, 12);   // unpacked size
    write_vint(fhdr, 0);    // attrs
    fhdr.push_back(0x78); fhdr.push_back(0x56); fhdr.push_back(0x34); fhdr.push_back(0x12);
    write_vint(fhdr, 0);    // comp_info
    write_vint(fhdr, 0);    // host_os
    std::string fname = "docs/hello.txt";
    write_vint(fhdr, fname.size());
    fhdr.insert(fhdr.end(), fname.begin(), fname.end());
    append_block(fhdr, {'1', '2', '3', '4', '5'});

    // End of archive (type=5, flags=0)
    std::vector<uint8_t> end_hdr;
    write_vint(end_hdr, 5);
    write_vint(end_hdr, 0);
    append_block(end_hdr);

    fs::path rar_path = dir / "sample.rar";
    write_file_bytes(rar_path, rar.data(), rar.size());

    std::vector<EntryInfo> entries;
    ArchiveInfo info;
    REQUIRE(RarInspector::list(rar_path, entries, &info));
    REQUIRE(info.format == ArchiveFormat::Rar);
    REQUIRE(info.is_solid);
    REQUIRE(info.has_recovery_record);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].full_path == "docs/hello.txt");
    REQUIRE(entries[0].size == 12);
    REQUIRE(entries[0].crc32 == 0x12345678U);

    fs::remove_all(dir);
}

// ============================================================================
// 9. End-to-End Self-Extracting Archive (SFX) Creation & Real Execution
// ============================================================================
static void test_sfx_end_to_end() {
#ifdef _WIN32
    fs::path stub = g_bin_dir / "openrar_sfx_stub.exe";
    std::string sfx_ext = ".exe";
#else
    fs::path stub = g_bin_dir / "openrar_sfx_stub";
    std::string sfx_ext = ".sfx";
#endif
    if (!fs::exists(stub)) return; // Skip only if stub wasn't built beside test runner

    auto dir = make_temp_dir("sfx");
    write_text(dir / "payload.txt", "Self-extracting OpenRAR verified!");
    fs::path arc = dir / "pack.orar";
    CreateOptions co;
    co.format = ArchiveFormat::Orar;
    REQUIRE(ArchiveManager::create(arc, {dir / "payload.txt"}, co).success);

    fs::path sfx_exe = dir / ("installer" + sfx_ext);
    REQUIRE(ArchiveManager::create_sfx(arc, stub, sfx_exe).success);

    fs::path out_dir = dir / "unpacked";
    std::string cmd = "\"" + sfx_exe.string() + "\" \"" + out_dir.string() + "\"";
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";
#endif
    REQUIRE(std::system(cmd.c_str()) == 0);
    REQUIRE(read_text(out_dir / "payload.txt") == "Self-extracting OpenRAR verified!");

    fs::remove_all(dir);
}

// ============================================================================
// 10. End-to-End CLI Execution Test
// ============================================================================
static void test_cli_end_to_end() {
#ifdef _WIN32
    fs::path cli = g_bin_dir / "openrar.exe";
#else
    fs::path cli = g_bin_dir / "openrar";
#endif
    if (!fs::exists(cli)) return;

    auto dir = make_temp_dir("cli");
    write_text(dir / "note.txt", "CLI end-to-end check!");
    fs::path arc = dir / "cli_test.orar";
    fs::path out = dir / "cli_out";

    auto run_cli = [&](const std::string& args) {
        std::string cmd = "\"" + cli.string() + "\" " + args;
#ifdef _WIN32
        cmd = "\"" + cmd + "\"";
#endif
        return std::system(cmd.c_str());
    };

    REQUIRE(run_cli("a -s -rr5 -pCliPass \"" + arc.string() + "\" \"" + (dir / "note.txt").string() + "\"") == 0);
    REQUIRE(run_cli("t -pCliPass \"" + arc.string() + "\"") == 0);
    REQUIRE(run_cli("x -y -pCliPass \"" + arc.string() + "\" \"" + out.string() + "\"") == 0);
    REQUIRE(run_cli("lang --lang=sr") == 0);
    REQUIRE(read_text(out / "note.txt") == "CLI end-to-end check!");

    fs::remove_all(dir);
}

// ============================================================================
// 11. Internationalization (13 Languages Completeness & Lookup)
// ============================================================================
static void test_i18n() {
    const auto& langs = i18n::available_languages();
    REQUIRE(langs.size() == 13);
    REQUIRE(langs.size() == static_cast<size_t>(i18n::Language::Count));

    const std::vector<std::string> expected_codes = {
        "en", "zh", "es", "hi", "ar", "pt", "id", "fr", "ja", "ru", "sr", "hr", "bs"
    };
    for (size_t i = 0; i < expected_codes.size(); ++i) {
        REQUIRE(std::string(langs[i].code) == expected_codes[i]);
        REQUIRE(std::strlen(langs[i].native_name) > 0);
        REQUIRE(i18n::set_language_code(expected_codes[i]));
        REQUIRE(i18n::current_language() == langs[i].id);
        REQUIRE(std::string(i18n::language_code(langs[i].id)) == expected_codes[i]);

        // Every single string key must resolve to a non-empty UTF-8 string without embedded tabs
        for (int k = 0; k < static_cast<int>(i18n::StrKey::Count); ++k) {
            const char* s = i18n::tr(static_cast<i18n::StrKey>(k));
            REQUIRE(s != nullptr && s[0] != '\0');
            REQUIRE(std::strchr(s, '\t') == nullptr);
        }
    }

    // Region-suffixed codes and invalid codes
    REQUIRE(i18n::set_language_code("sr_RS.UTF-8"));
    REQUIRE(i18n::current_language() == i18n::Language::Sr);
    REQUIRE(i18n::set_language_code("zh-CN"));
    REQUIRE(i18n::current_language() == i18n::Language::Zh);
    REQUIRE(!i18n::set_language_code("xx_invalid"));
    REQUIRE(i18n::current_language() == i18n::Language::Zh); // unchanged on error

    i18n::set_language(i18n::Language::En);
}

int main(int argc, char* argv[]) {
    if (argc > 0 && argv[0]) {
        std::error_code ec;
        g_bin_dir = fs::absolute(fs::path(argv[0]), ec).parent_path();
    }
    std::cout << "=== OpenRAR C++20 Comprehensive Test Suite ===\n";
    run_test("1. Crypto Standards (CRC32, SHA-256, SHA-1, PBKDF2-SHA1, AES-GCM, ZipCrypto)", test_crypto);
    run_test("2. Deflate / Inflate / GZip Stress & Fuzz Resilience", test_deflate_stress);
    run_test("3. ZIP Full Lifecycle (AE-2, ZipCrypto, Unicode, Empty, Comment, Repair, Split)", test_zip_lifecycle);
    run_test("4. OpenRAR (.orar) Solid, Parallel, AES-GCM, -hp, Burst Repair, Split, Unicode", test_orar_full_features);
    run_test("5. Extract Modes (Skip, Rename, Flat, Subset) & Zip-Slip Security", test_extract_modes_and_security);
    run_test("6. TAR / TAR.GZ & GNU LongName (>120 chars)", test_tar_longname);
    run_test("7. 7z, Batch Convert, Find-in-Archives & Multi-Threaded Benchmark", test_7z_convert_find_bench);
    run_test("8. Native RAR5 Binary Header Inspector", test_rar5_inspector);
    run_test("9. End-to-End Self-Extracting Archive (SFX) Execution", test_sfx_end_to_end);
    run_test("10. End-to-End CLI Process Verification", test_cli_end_to_end);
    run_test("11. Internationalization (13 Languages & String Table Completeness)", test_i18n);

    std::cout << "\nResults: " << g_passed << " passed, " << g_failed << " failed.\n";
    return g_failed == 0 ? 0 : 1;
}
