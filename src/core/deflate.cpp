// Self-contained RFC 1951 Deflate/Inflate and RFC 1952 GZip codecs.
// Encoder: 32KB hash-chain LZ77 with optional lazy matching + per-block optimal choice
// among Canonical Dynamic Huffman (type 2), Fixed Huffman (type 1), and Stored (type 0).
// Decoder: 9-bit (512-entry) O(1) fast lookup table with canonical slow-path fallback.

#include "openrar/deflate.hpp"
#include "openrar/crypto.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <queue>

namespace openrar::codec {

namespace {

// ============================================================================
// RFC 1951 Tables
// ============================================================================
constexpr int kMaxBits = 15;
constexpr int kMaxLitLen = 288;
constexpr int kMaxDist = 32;

constexpr uint16_t kLenBase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
constexpr uint8_t kLenExtra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

constexpr uint16_t kDistBase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
constexpr uint8_t kDistExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

constexpr uint8_t kCodeLenOrder[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

inline uint16_t reverse_bits(uint16_t code, int bits) {
    uint16_t rev = 0;
    for (int i = 0; i < bits; ++i) {
        rev = static_cast<uint16_t>((rev << 1) | (code & 1));
        code >>= 1;
    }
    return rev;
}

constexpr auto kLenToCodeTable = []() {
    std::array<uint8_t, 259> t{};
    for (int len = 3; len <= 258; ++len) {
        uint8_t code = 0;
        for (int i = 28; i >= 0; --i) {
            if (len >= kLenBase[i]) {
                code = static_cast<uint8_t>(i);
                break;
            }
        }
        t[static_cast<size_t>(len)] = code;
    }
    return t;
}();

constexpr auto kDistToCodeLow = []() {
    std::array<uint8_t, 256> t{};
    for (int d = 1; d <= 256; ++d) {
        uint8_t code = 0;
        for (int i = 29; i >= 0; --i) {
            if (d >= kDistBase[i]) {
                code = static_cast<uint8_t>(i);
                break;
            }
        }
        t[static_cast<size_t>(d - 1)] = code;
    }
    return t;
}();

constexpr auto kDistToCodeHigh = []() {
    std::array<uint8_t, 256> t{};
    for (int bucket = 0; bucket < 256; ++bucket) {
        int d = (bucket << 7) + 1;
        uint8_t code = 0;
        for (int i = 29; i >= 0; --i) {
            if (d >= kDistBase[i]) {
                code = static_cast<uint8_t>(i);
                break;
            }
        }
        t[static_cast<size_t>(bucket)] = code;
    }
    return t;
}();

inline int length_to_code(int len) {
    return kLenToCodeTable[static_cast<size_t>(len)];
}

inline int dist_to_code(int dist) {
    return (dist <= 256)
        ? kDistToCodeLow[static_cast<size_t>(dist - 1)]
        : kDistToCodeHigh[static_cast<size_t>((dist - 1) >> 7)];
}

// ============================================================================
// Bit Reader (LSB-first per RFC 1951)
// ============================================================================
class BitReader {
public:
    BitReader(const uint8_t* data, size_t len) : data_(data), len_(len) {}

    bool ensure(int bits) {
        while (bit_count_ < bits) {
            if (pos_ >= len_) return false;
            bit_buf_ |= static_cast<uint64_t>(data_[pos_++]) << bit_count_;
            bit_count_ += 8;
        }
        return true;
    }

    bool read_bits(int bits, uint32_t& out) {
        if (bits == 0) { out = 0; return true; }
        if (!ensure(bits)) return false;
        out = static_cast<uint32_t>(bit_buf_ & ((1ULL << bits) - 1));
        bit_buf_ >>= bits;
        bit_count_ -= bits;
        return true;
    }

    uint32_t peek_bits(int bits) {
        ensure(bits);
        return static_cast<uint32_t>(bit_buf_ & ((1ULL << bits) - 1));
    }

    void consume_bits(int bits) {
        bit_buf_ >>= bits;
        bit_count_ -= bits;
    }

    int available_bits() const { return bit_count_; }

    void align_byte() {
        int drop = bit_count_ & 7;
        bit_buf_ >>= drop;
        bit_count_ -= drop;
    }

    bool read_bytes(uint8_t* dst, size_t n) {
        align_byte();
        while (n > 0 && bit_count_ >= 8) {
            *dst++ = static_cast<uint8_t>(bit_buf_ & 0xFF);
            bit_buf_ >>= 8;
            bit_count_ -= 8;
            --n;
        }
        if (pos_ + n > len_) return false;
        if (n > 0) {
            std::memcpy(dst, data_ + pos_, n);
            pos_ += n;
        }
        return true;
    }

private:
    const uint8_t* data_;
    size_t len_;
    size_t pos_ = 0;
    uint64_t bit_buf_ = 0;
    int bit_count_ = 0;
};

// ============================================================================
// Bit Writer (LSB-first per RFC 1951)
// ============================================================================
class BitWriter {
public:
    explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}

    void write_bits(uint32_t val, int bits) {
        bit_buf_ |= (static_cast<uint64_t>(val) << bit_count_);
        bit_count_ += bits;
        while (bit_count_ >= 8) {
            out_.push_back(static_cast<uint8_t>(bit_buf_ & 0xFF));
            bit_buf_ >>= 8;
            bit_count_ -= 8;
        }
    }

    void align_byte() {
        if (bit_count_ > 0) {
            out_.push_back(static_cast<uint8_t>(bit_buf_ & 0xFF));
            bit_buf_ = 0;
            bit_count_ = 0;
        }
    }

    void write_raw_bytes(const uint8_t* data, size_t len) {
        align_byte();
        out_.insert(out_.end(), data, data + len);
    }

    void flush() { align_byte(); }

private:
    std::vector<uint8_t>& out_;
    uint64_t bit_buf_ = 0;
    int bit_count_ = 0;
};

// ============================================================================
// Huffman Decoder (with 9-bit O(1) Fast Lookup Table)
// ============================================================================
struct HuffmanTree {
    static constexpr int kFastBits = 9;
    static constexpr int kFastSize = 1 << kFastBits;

    std::array<int, kMaxBits + 1> count{};
    std::vector<uint16_t> symbols;
    std::array<uint16_t, kFastSize> fast{};

    bool build(const uint8_t* lengths, int num_symbols) {
        count.fill(0);
        fast.fill(0);
        for (int i = 0; i < num_symbols; ++i) {
            if (lengths[i] > kMaxBits) return false;
            count[lengths[i]]++;
        }
        if (count[0] == num_symbols) {
            symbols.clear();
            return true;
        }

        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0) return false;
        }

        std::array<int, kMaxBits + 1> offsets{};
        for (int len = 1; len < kMaxBits; ++len) {
            offsets[len + 1] = offsets[len] + count[len];
        }

        symbols.assign(static_cast<size_t>(num_symbols), 0);
        for (int sym = 0; sym < num_symbols; ++sym) {
            if (lengths[sym] != 0) {
                symbols[offsets[lengths[sym]]++] = static_cast<uint16_t>(sym);
            }
        }

        int code = 0;
        int idx = 0;
        for (int len = 1; len <= kFastBits; ++len) {
            int cnt = count[len];
            for (int i = 0; i < cnt; ++i) {
                uint16_t sym = symbols[static_cast<size_t>(idx++)];
                uint16_t rev = reverse_bits(static_cast<uint16_t>(code), len);
                uint16_t entry = static_cast<uint16_t>((len << 12) | (sym & 0x0FFF));
                int step = 1 << len;
                for (int fill = rev; fill < kFastSize; fill += step) {
                    fast[static_cast<size_t>(fill)] = entry;
                }
                ++code;
            }
            code <<= 1;
        }
        return true;
    }

    bool decode(BitReader& br, uint16_t& sym_out) const {
        uint32_t peek = br.peek_bits(kFastBits);
        uint16_t entry = fast[peek];
        if (entry != 0) {
            int bits = entry >> 12;
            if (br.available_bits() >= bits) {
                br.consume_bits(bits);
                sym_out = static_cast<uint16_t>(entry & 0x0FFF);
                return true;
            }
        }

        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            uint32_t bit = 0;
            if (!br.read_bits(1, bit)) return false;
            code = (code << 1) | static_cast<int>(bit);
            int cnt = count[len];
            if (code - cnt < first) {
                size_t pos = static_cast<size_t>(index + (code - first));
                if (pos >= symbols.size()) return false;
                sym_out = symbols[pos];
                return true;
            }
            index += cnt;
            first = (first + cnt) << 1;
        }
        return false;
    }
};

void build_fixed_trees(HuffmanTree& litlen, HuffmanTree& dist) {
    uint8_t ll_lens[kMaxLitLen];
    int i = 0;
    for (; i <= 143; ++i) ll_lens[i] = 8;
    for (; i <= 255; ++i) ll_lens[i] = 9;
    for (; i <= 279; ++i) ll_lens[i] = 7;
    for (; i <= 287; ++i) ll_lens[i] = 8;
    litlen.build(ll_lens, kMaxLitLen);

    uint8_t d_lens[kMaxDist];
    std::memset(d_lens, 5, sizeof(d_lens));
    dist.build(d_lens, kMaxDist);
}

bool decode_block(BitReader& br, const HuffmanTree& litlen, const HuffmanTree& dist,
                  std::vector<uint8_t>& out, std::string* err) {
    while (true) {
        uint16_t sym = 0;
        if (!litlen.decode(br, sym)) {
            if (err) *err = "Invalid Huffman symbol in deflate stream";
            return false;
        }
        if (sym < 256) {
            out.push_back(static_cast<uint8_t>(sym));
        } else if (sym == 256) {
            return true;
        } else if (sym <= 285) {
            int len_idx = sym - 257;
            uint32_t extra = 0;
            if (kLenExtra[len_idx] > 0 && !br.read_bits(kLenExtra[len_idx], extra)) {
                if (err) *err = "Unexpected EOF reading length extra bits";
                return false;
            }
            size_t length = kLenBase[len_idx] + extra;

            uint16_t dsym = 0;
            if (!dist.decode(br, dsym) || dsym >= 30) {
                if (err) *err = "Invalid distance Huffman symbol";
                return false;
            }
            uint32_t dextra = 0;
            if (kDistExtra[dsym] > 0 && !br.read_bits(kDistExtra[dsym], dextra)) {
                if (err) *err = "Unexpected EOF reading distance extra bits";
                return false;
            }
            size_t distance = kDistBase[dsym] + dextra;
            if (distance == 0 || distance > out.size()) {
                if (err) *err = "Invalid LZ77 back-reference distance";
                return false;
            }
            size_t start = out.size() - distance;
            for (size_t k = 0; k < length; ++k) {
                out.push_back(out[start + k]);
            }
        } else {
            if (err) *err = "Illegal literal/length symbol (>285)";
            return false;
        }
    }
}

// ============================================================================
// Canonical Length-Limited Huffman Code Builder (for Dynamic Huffman Block Type 2)
// ============================================================================
void compute_huffman_lengths(const uint32_t* freqs, int num_syms, int max_bits, uint8_t* lengths_out) {
    std::memset(lengths_out, 0, static_cast<size_t>(num_syms));

    struct Node {
        uint64_t freq;
        int sym;
        int left;
        int right;
    };
    std::vector<Node> nodes;
    nodes.reserve(static_cast<size_t>(num_syms) * 2);

    auto cmp = [&](int a, int b) {
        if (nodes[a].freq != nodes[b].freq) return nodes[a].freq > nodes[b].freq;
        return a > b;
    };
    std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);

    int active = 0;
    int last_sym = -1;
    for (int i = 0; i < num_syms; ++i) {
        if (freqs[i] > 0) {
            int id = static_cast<int>(nodes.size());
            nodes.push_back({freqs[i], i, -1, -1});
            pq.push(id);
            active++;
            last_sym = i;
        }
    }

    if (active == 0) return;
    if (active == 1) {
        lengths_out[last_sym] = 1;
        return;
    }

    while (pq.size() > 1) {
        int a = pq.top(); pq.pop();
        int b = pq.top(); pq.pop();
        int id = static_cast<int>(nodes.size());
        nodes.push_back({nodes[a].freq + nodes[b].freq, -1, a, b});
        pq.push(id);
    }

    // DFS to compute raw depths
    std::vector<std::pair<int, int>> stack;
    stack.emplace_back(pq.top(), 0);
    int max_seen = 0;
    while (!stack.empty()) {
        auto [u, d] = stack.back();
        stack.pop_back();
        if (nodes[u].sym >= 0) {
            lengths_out[nodes[u].sym] = static_cast<uint8_t>(std::min(d, max_bits));
            if (d > max_seen) max_seen = d;
        } else {
            stack.emplace_back(nodes[u].left, d + 1);
            stack.emplace_back(nodes[u].right, d + 1);
        }
    }

    // Enforce Kraft inequality if any code was clamped to max_bits
    if (max_seen > max_bits) {
        uint32_t kraft = 0;
        const uint32_t target = 1U << max_bits;
        std::vector<int> syms;
        for (int i = 0; i < num_syms; ++i) {
            if (lengths_out[i] > 0) {
                kraft += (1U << (max_bits - lengths_out[i]));
                syms.push_back(i);
            }
        }
        // Sort by frequency ascending (lengthen least frequent codes first to reduce Kraft sum)
        std::sort(syms.begin(), syms.end(), [&](int a, int b) {
            if (freqs[a] != freqs[b]) return freqs[a] < freqs[b];
            return lengths_out[a] < lengths_out[b];
        });
        while (kraft > target) {
            bool changed = false;
            for (int s : syms) {
                if (lengths_out[s] < max_bits) {
                    kraft -= (1U << (max_bits - lengths_out[s] - 1));
                    lengths_out[s]++;
                    changed = true;
                    if (kraft <= target) break;
                }
            }
            if (!changed) break;
        }
        // Tighten back up if under-subscribed
        std::reverse(syms.begin(), syms.end());
        for (int s : syms) {
            while (lengths_out[s] > 1) {
                uint32_t add = 1U << (max_bits - lengths_out[s]);
                if (kraft + add <= target) {
                    kraft += add;
                    lengths_out[s]--;
                } else {
                    break;
                }
            }
        }
    }
}

void build_canonical_codes(const uint8_t* lengths, int num_syms, uint16_t* codes_out) {
    std::array<int, kMaxBits + 1> bl_count{};
    for (int i = 0; i < num_syms; ++i) {
        if (lengths[i] > 0) bl_count[lengths[i]]++;
    }
    std::array<uint16_t, kMaxBits + 1> next_code{};
    uint16_t code = 0;
    for (int bits = 1; bits <= kMaxBits; ++bits) {
        code = static_cast<uint16_t>((code + bl_count[bits - 1]) << 1);
        next_code[bits] = code;
    }
    for (int i = 0; i < num_syms; ++i) {
        int len = lengths[i];
        if (len > 0) {
            codes_out[i] = reverse_bits(next_code[len]++, len);
        } else {
            codes_out[i] = 0;
        }
    }
}

struct FixedEncTables {
    uint16_t ll_code[286]{};
    uint8_t  ll_bits[286]{};
    uint16_t dist_code[30]{};

    FixedEncTables() {
        for (int i = 0; i <= 143; ++i) { ll_code[i] = reverse_bits(static_cast<uint16_t>(0x30 + i), 8); ll_bits[i] = 8; }
        for (int i = 144; i <= 255; ++i) { ll_code[i] = reverse_bits(static_cast<uint16_t>(0x190 + (i - 144)), 9); ll_bits[i] = 9; }
        for (int i = 256; i <= 279; ++i) { ll_code[i] = reverse_bits(static_cast<uint16_t>(i - 256), 7); ll_bits[i] = 7; }
        for (int i = 280; i <= 285; ++i) { ll_code[i] = reverse_bits(static_cast<uint16_t>(0xC0 + (i - 280)), 8); ll_bits[i] = 8; }
        for (int i = 0; i < 30; ++i) { dist_code[i] = reverse_bits(static_cast<uint16_t>(i), 5); }
    }
};

const FixedEncTables& fixed_enc() {
    static const FixedEncTables tbl;
    return tbl;
}

void write_stored_blocks(const uint8_t* data, size_t len, BitWriter& bw, bool is_final_stream) {
    size_t pos = 0;
    do {
        size_t chunk = std::min<size_t>(65535, len - pos);
        bool final_block = is_final_stream && (pos + chunk >= len);
        bw.write_bits(final_block ? 1U : 0U, 1);
        bw.write_bits(0U, 2); // BTYPE = 00 (stored)
        bw.align_byte();
        uint16_t l = static_cast<uint16_t>(chunk);
        uint16_t nl = static_cast<uint16_t>(~l);
        uint8_t hdr[4] = {
            static_cast<uint8_t>(l & 0xFF), static_cast<uint8_t>(l >> 8),
            static_cast<uint8_t>(nl & 0xFF), static_cast<uint8_t>(nl >> 8)
        };
        bw.write_raw_bytes(hdr, 4);
        if (chunk > 0) bw.write_raw_bytes(data + pos, chunk);
        pos += chunk;
    } while (pos < len);
}

struct LzToken {
    uint16_t lit_or_len; // <256 literal, >=259 means match length = lit_or_len - 256 (3..258)
    uint16_t dist;       // 0 if literal, 1..32768 if match
};

void emit_deflate_block(BitWriter& bw, const std::vector<LzToken>& tokens,
                        const uint8_t* raw_slice, size_t raw_len, bool is_final) {
    // 1. Count symbol frequencies
    uint32_t ll_freq[286] = {};
    uint32_t d_freq[30] = {};
    ll_freq[256] = 1; // End-of-block

    for (const auto& t : tokens) {
        if (t.dist == 0) {
            ll_freq[t.lit_or_len]++;
        } else {
            int lidx = length_to_code(t.lit_or_len - 256);
            int didx = dist_to_code(t.dist);
            ll_freq[257 + lidx]++;
            d_freq[didx]++;
        }
    }

    // Ensure at least one distance symbol exists for RFC 1951 validity
    bool any_dist = false;
    for (int i = 0; i < 30; ++i) if (d_freq[i] > 0) { any_dist = true; break; }
    if (!any_dist) d_freq[0] = 1;

    // 2. Build Dynamic Huffman lengths
    uint8_t ll_len[286];
    uint8_t d_len[30];
    compute_huffman_lengths(ll_freq, 286, 15, ll_len);
    compute_huffman_lengths(d_freq, 30, 15, d_len);

    int hlit = 286;
    while (hlit > 257 && ll_len[hlit - 1] == 0) --hlit;
    int hdist = 30;
    while (hdist > 1 && d_len[hdist - 1] == 0) --hdist;

    // 3. RLE-encode combined code lengths using symbols 0..18
    std::vector<uint8_t> all_lens;
    all_lens.reserve(static_cast<size_t>(hlit + hdist));
    all_lens.insert(all_lens.end(), ll_len, ll_len + hlit);
    all_lens.insert(all_lens.end(), d_len, d_len + hdist);

    struct RleSym { uint8_t sym; uint8_t extra_val; uint8_t extra_bits; };
    std::vector<RleSym> rle;
    rle.reserve(all_lens.size());
    uint32_t cl_freq[19] = {};

    for (size_t i = 0; i < all_lens.size();) {
        uint8_t v = all_lens[i];
        size_t run = 1;
        while (i + run < all_lens.size() && all_lens[i + run] == v) ++run;
        size_t orig_run = run;
        if (v == 0) {
            while (run > 0) {
                if (run >= 11) {
                    size_t take = std::min<size_t>(run, 138);
                    rle.push_back({18, static_cast<uint8_t>(take - 11), 7});
                    cl_freq[18]++;
                    run -= take;
                } else if (run >= 3) {
                    rle.push_back({17, static_cast<uint8_t>(run - 3), 3});
                    cl_freq[17]++;
                    run = 0;
                } else {
                    rle.push_back({0, 0, 0});
                    cl_freq[0]++;
                    run--;
                }
            }
        } else {
            rle.push_back({v, 0, 0});
            cl_freq[v]++;
            run--;
            while (run >= 3) {
                size_t take = std::min<size_t>(run, 6);
                rle.push_back({16, static_cast<uint8_t>(take - 3), 2});
                cl_freq[16]++;
                run -= take;
            }
            while (run > 0) {
                rle.push_back({v, 0, 0});
                cl_freq[v]++;
                run--;
            }
        }
        i += orig_run;
    }

    uint8_t cl_len[19];
    compute_huffman_lengths(cl_freq, 19, 7, cl_len);
    int hclen = 19;
    while (hclen > 4 && cl_len[kCodeLenOrder[hclen - 1]] == 0) --hclen;

    // 4. Compare exact bit costs: Dynamic vs Fixed vs Stored!
    const auto& fix = fixed_enc();
    uint64_t dyn_bits = 3 + 5 + 5 + 4 + static_cast<uint64_t>(hclen) * 3;
    for (const auto& r : rle) dyn_bits += cl_len[r.sym] + r.extra_bits;
    uint64_t fix_bits = 3;
    for (int i = 0; i < 286; ++i) {
        if (ll_freq[i] > 0) {
            uint64_t eb = (i >= 257) ? kLenExtra[i - 257] : 0;
            dyn_bits += static_cast<uint64_t>(ll_freq[i]) * (ll_len[i] + eb);
            fix_bits += static_cast<uint64_t>(ll_freq[i]) * (fix.ll_bits[i] + eb);
        }
    }
    for (int i = 0; i < 30; ++i) {
        if (d_freq[i] > 0 && any_dist) {
            dyn_bits += static_cast<uint64_t>(d_freq[i]) * (d_len[i] + kDistExtra[i]);
            fix_bits += static_cast<uint64_t>(d_freq[i]) * (5 + kDistExtra[i]);
        }
    }
    uint64_t stored_bits = (raw_len + ((raw_len / 65535) + 1) * 5) * 8 + 8;

    if (stored_bits <= dyn_bits && stored_bits <= fix_bits) {
        write_stored_blocks(raw_slice, raw_len, bw, is_final);
        return;
    }

    if (dyn_bits < fix_bits) {
        // Emit Dynamic Huffman Block (BTYPE = 10)
        uint16_t ll_code[286], d_code[30], cl_code[19];
        build_canonical_codes(ll_len, 286, ll_code);
        build_canonical_codes(d_len, 30, d_code);
        build_canonical_codes(cl_len, 19, cl_code);

        bw.write_bits(is_final ? 1U : 0U, 1);
        bw.write_bits(2U, 2); // Dynamic Huffman
        bw.write_bits(static_cast<uint32_t>(hlit - 257), 5);
        bw.write_bits(static_cast<uint32_t>(hdist - 1), 5);
        bw.write_bits(static_cast<uint32_t>(hclen - 4), 4);
        for (int i = 0; i < hclen; ++i) bw.write_bits(cl_len[kCodeLenOrder[i]], 3);
        for (const auto& r : rle) {
            bw.write_bits(cl_code[r.sym], cl_len[r.sym]);
            if (r.extra_bits > 0) bw.write_bits(r.extra_val, r.extra_bits);
        }
        for (const auto& t : tokens) {
            if (t.dist == 0) {
                bw.write_bits(ll_code[t.lit_or_len], ll_len[t.lit_or_len]);
            } else {
                int len = t.lit_or_len - 256;
                int lidx = length_to_code(len);
                int lsym = 257 + lidx;
                bw.write_bits(ll_code[lsym], ll_len[lsym]);
                if (kLenExtra[lidx] > 0) bw.write_bits(static_cast<uint32_t>(len - kLenBase[lidx]), kLenExtra[lidx]);
                int didx = dist_to_code(t.dist);
                bw.write_bits(d_code[didx], d_len[didx]);
                if (kDistExtra[didx] > 0) bw.write_bits(static_cast<uint32_t>(t.dist - kDistBase[didx]), kDistExtra[didx]);
            }
        }
        bw.write_bits(ll_code[256], ll_len[256]);
    } else {
        // Emit Fixed Huffman Block (BTYPE = 01)
        bw.write_bits(is_final ? 1U : 0U, 1);
        bw.write_bits(1U, 2);
        for (const auto& t : tokens) {
            if (t.dist == 0) {
                bw.write_bits(fix.ll_code[t.lit_or_len], fix.ll_bits[t.lit_or_len]);
            } else {
                int len = t.lit_or_len - 256;
                int lidx = length_to_code(len);
                int lsym = 257 + lidx;
                bw.write_bits(fix.ll_code[lsym], fix.ll_bits[lsym]);
                if (kLenExtra[lidx] > 0) bw.write_bits(static_cast<uint32_t>(len - kLenBase[lidx]), kLenExtra[lidx]);
                int didx = dist_to_code(t.dist);
                bw.write_bits(fix.dist_code[didx], 5);
                if (kDistExtra[didx] > 0) bw.write_bits(static_cast<uint32_t>(t.dist - kDistBase[didx]), kDistExtra[didx]);
            }
        }
        bw.write_bits(fix.ll_code[256], fix.ll_bits[256]);
    }
}

inline uint32_t hash3(const uint8_t* p) {
    uint32_t v = (static_cast<uint32_t>(p[0]) << 16) |
                 (static_cast<uint32_t>(p[1]) << 8) |
                 static_cast<uint32_t>(p[2]);
    return ((v * 2654435761U) >> 17) & 0x7FFF;
}

} // namespace

// ============================================================================
// Public API: Deflate / Inflate
// ============================================================================
std::vector<uint8_t> deflate_compress(const uint8_t* data, size_t len, CompressionLevel level) {
    std::vector<uint8_t> out;
    BitWriter bw(out);
    if (level == CompressionLevel::Store || len == 0) {
        write_stored_blocks(data, len, bw, true);
        bw.flush();
        return out;
    }

    int max_chain = 32;
    int nice_len = 64;
    bool use_lazy = false;
    switch (level) {
        case CompressionLevel::Fastest: max_chain = 4;   nice_len = 16;  use_lazy = false; break;
        case CompressionLevel::Fast:    max_chain = 12;  nice_len = 32;  use_lazy = false; break;
        case CompressionLevel::Normal:  max_chain = 32;  nice_len = 64;  use_lazy = true;  break;
        case CompressionLevel::Good:    max_chain = 96;  nice_len = 128; use_lazy = true;  break;
        case CompressionLevel::Best:    max_chain = 256; nice_len = 258; use_lazy = true;  break;
        default: break;
    }

    out.reserve(len / 2 + 64);
    constexpr size_t kHashSize = 32768;
    constexpr size_t kWindowSize = 32768;
    constexpr size_t kBlockTokens = 32768;

    std::vector<int> head(kHashSize, -1);
    std::vector<int> prev(kWindowSize, -1);

    auto find_match = [&](size_t pos, int& best_len, int& best_dist, bool insert_hash) {
        best_len = 0;
        best_dist = 0;
        if (pos + 3 > len) return;
        uint32_t h = hash3(data + pos);
        int cur = head[h];
        if (insert_hash) {
            prev[pos & (kWindowSize - 1)] = cur;
            head[h] = static_cast<int>(pos);
        }
        int chain = 0;
        size_t max_len = std::min<size_t>(258, len - pos);
        while (cur >= 0 && chain < max_chain) {
            size_t dist = pos - static_cast<size_t>(cur);
            if (dist == 0 || dist > kWindowSize) break;
            const uint8_t* a = data + cur;
            const uint8_t* b = data + pos;
            if (a[best_len] == b[best_len] && a[0] == b[0] && a[1] == b[1] && a[2] == b[2]) {
                size_t mlen = 3;
                while (mlen < max_len && a[mlen] == b[mlen]) ++mlen;
                if (static_cast<int>(mlen) > best_len) {
                    best_len = static_cast<int>(mlen);
                    best_dist = static_cast<int>(dist);
                    if (best_len >= nice_len) break;
                }
            }
            int next_cur = prev[static_cast<size_t>(cur) & (kWindowSize - 1)];
            if (next_cur >= cur) break;
            cur = next_cur;
            ++chain;
        }
    };

    std::vector<LzToken> tokens;
    tokens.reserve(kBlockTokens);
    size_t block_raw_start = 0;

    size_t pos = 0;
    while (pos < len) {
        int best_len = 0, best_dist = 0;
        find_match(pos, best_len, best_dist, true);

        // Lazy matching: check if pos+1 has an even better match!
        if (use_lazy && best_len >= 3 && best_len < nice_len && pos + 4 <= len) {
            int next_len = 0, next_dist = 0;
            find_match(pos + 1, next_len, next_dist, false);
            if (next_len > best_len + 1) {
                tokens.push_back({data[pos], 0});
                pos++;
                continue;
            }
        }

        if (best_len >= 3) {
            tokens.push_back({static_cast<uint16_t>(256 + best_len), static_cast<uint16_t>(best_dist)});
            for (int k = 1; k < best_len; ++k) {
                size_t p = pos + static_cast<size_t>(k);
                if (p + 3 <= len) {
                    uint32_t h = hash3(data + p);
                    prev[p & (kWindowSize - 1)] = head[h];
                    head[h] = static_cast<int>(p);
                }
            }
            pos += static_cast<size_t>(best_len);
        } else {
            tokens.push_back({data[pos], 0});
            pos++;
        }

        if (tokens.size() >= kBlockTokens && pos < len) {
            emit_deflate_block(bw, tokens, data + block_raw_start, pos - block_raw_start, false);
            tokens.clear();
            block_raw_start = pos;
        }
    }

    emit_deflate_block(bw, tokens, data + block_raw_start, len - block_raw_start, true);
    bw.flush();
    return out;
}

std::vector<uint8_t> deflate_compress(const std::vector<uint8_t>& data, CompressionLevel level) {
    return deflate_compress(data.data(), data.size(), level);
}

bool deflate_decompress(const uint8_t* data, size_t len, std::vector<uint8_t>& out, std::string* err) {
    out.clear();
    BitReader br(data, len);
    HuffmanTree fixed_ll, fixed_dist;
    bool fixed_ready = false;

    uint32_t bfinal = 0;
    do {
        uint32_t btype = 0;
        if (!br.read_bits(1, bfinal) || !br.read_bits(2, btype)) {
            if (err) *err = "Unexpected EOF in deflate block header";
            return false;
        }

        if (btype == 0) {
            uint8_t hdr[4];
            if (!br.read_bytes(hdr, 4)) {
                if (err) *err = "Unexpected EOF in stored block header";
                return false;
            }
            uint16_t blen = static_cast<uint16_t>(hdr[0] | (hdr[1] << 8));
            uint16_t nlen = static_cast<uint16_t>(hdr[2] | (hdr[3] << 8));
            if (static_cast<uint16_t>(blen ^ 0xFFFF) != nlen) {
                if (err) *err = "Stored block length complement mismatch";
                return false;
            }
            size_t old_sz = out.size();
            out.resize(old_sz + blen);
            if (blen > 0 && !br.read_bytes(out.data() + old_sz, blen)) {
                if (err) *err = "Unexpected EOF reading stored block payload";
                return false;
            }
        } else if (btype == 1) {
            if (!fixed_ready) {
                build_fixed_trees(fixed_ll, fixed_dist);
                fixed_ready = true;
            }
            if (!decode_block(br, fixed_ll, fixed_dist, out, err)) return false;
        } else if (btype == 2) {
            uint32_t hlit = 0, hdist = 0, hclen = 0;
            if (!br.read_bits(5, hlit) || !br.read_bits(5, hdist) || !br.read_bits(4, hclen)) {
                if (err) *err = "Unexpected EOF in dynamic Huffman header";
                return false;
            }
            int num_ll = static_cast<int>(hlit) + 257;
            int num_dist = static_cast<int>(hdist) + 1;
            int num_clen = static_cast<int>(hclen) + 4;
            if (num_ll > kMaxLitLen || num_dist > kMaxDist) {
                if (err) *err = "Dynamic Huffman symbol counts out of range";
                return false;
            }

            uint8_t clen_lens[19] = {};
            for (int i = 0; i < num_clen; ++i) {
                uint32_t v = 0;
                if (!br.read_bits(3, v)) {
                    if (err) *err = "Unexpected EOF reading code length table";
                    return false;
                }
                clen_lens[kCodeLenOrder[i]] = static_cast<uint8_t>(v);
            }

            HuffmanTree clen_tree;
            if (!clen_tree.build(clen_lens, 19)) {
                if (err) *err = "Invalid code length Huffman tree";
                return false;
            }

            std::vector<uint8_t> lengths(static_cast<size_t>(num_ll + num_dist), 0);
            int idx = 0;
            while (idx < num_ll + num_dist) {
                uint16_t sym = 0;
                if (!clen_tree.decode(br, sym)) {
                    if (err) *err = "Failed decoding dynamic code lengths";
                    return false;
                }
                if (sym < 16) {
                    lengths[idx++] = static_cast<uint8_t>(sym);
                } else {
                    uint8_t repeat_val = 0;
                    uint32_t extra = 0;
                    int repeat_cnt = 0;
                    if (sym == 16) {
                        if (idx == 0 || !br.read_bits(2, extra)) return false;
                        repeat_val = lengths[idx - 1];
                        repeat_cnt = 3 + static_cast<int>(extra);
                    } else if (sym == 17) {
                        if (!br.read_bits(3, extra)) return false;
                        repeat_cnt = 3 + static_cast<int>(extra);
                    } else if (sym == 18) {
                        if (!br.read_bits(7, extra)) return false;
                        repeat_cnt = 11 + static_cast<int>(extra);
                    } else {
                        return false;
                    }
                    if (idx + repeat_cnt > num_ll + num_dist) {
                        if (err) *err = "Dynamic code length repeat overflow";
                        return false;
                    }
                    for (int r = 0; r < repeat_cnt; ++r) lengths[idx++] = repeat_val;
                }
            }

            HuffmanTree dyn_ll, dyn_dist;
            if (!dyn_ll.build(lengths.data(), num_ll) ||
                !dyn_dist.build(lengths.data() + num_ll, num_dist)) {
                if (err) *err = "Failed building dynamic Huffman trees";
                return false;
            }
            if (!decode_block(br, dyn_ll, dyn_dist, out, err)) return false;
        } else {
            if (err) *err = "Reserved deflate block type (3)";
            return false;
        }
    } while (!bfinal);

    return true;
}

bool deflate_decompress(const std::vector<uint8_t>& data, std::vector<uint8_t>& out, std::string* err) {
    return deflate_decompress(data.data(), data.size(), out, err);
}

// ============================================================================
// RFC 1952 GZip Wrapper
// ============================================================================
std::vector<uint8_t> gzip_compress(const uint8_t* data, size_t len, CompressionLevel level,
                                   const std::string& original_name) {
    std::vector<uint8_t> out;
    out.push_back(0x1F);
    out.push_back(0x8B);
    out.push_back(0x08);
    uint8_t flg = original_name.empty() ? 0x00 : 0x08;
    out.push_back(flg);
    out.push_back(0); out.push_back(0); out.push_back(0); out.push_back(0);
    out.push_back(level == CompressionLevel::Best ? 2 : 0);
    out.push_back(255);

    if (!original_name.empty()) {
        out.insert(out.end(), original_name.begin(), original_name.end());
        out.push_back(0);
    }

    auto comp = deflate_compress(data, len, level);
    out.insert(out.end(), comp.begin(), comp.end());

    uint32_t crc = crypto::crc32(data, len);
    uint32_t isize = static_cast<uint32_t>(len & 0xFFFFFFFFULL);
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((crc >> (i * 8)) & 0xFF));
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((isize >> (i * 8)) & 0xFF));
    return out;
}

bool gzip_decompress(const uint8_t* data, size_t len, std::vector<uint8_t>& out,
                     std::string* original_name, std::string* err) {
    if (len < 18 || data[0] != 0x1F || data[1] != 0x8B || data[2] != 0x08) {
        if (err) *err = "Invalid GZip header";
        return false;
    }
    uint8_t flg = data[3];
    size_t pos = 10;

    if (flg & 0x04) {
        if (pos + 2 > len) return false;
        uint16_t xlen = static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8));
        pos += 2 + xlen;
    }
    if (flg & 0x08) {
        size_t start = pos;
        while (pos < len && data[pos] != 0) ++pos;
        if (pos >= len) return false;
        if (original_name) original_name->assign(reinterpret_cast<const char*>(data + start), pos - start);
        ++pos;
    }
    if (flg & 0x10) {
        while (pos < len && data[pos] != 0) ++pos;
        if (pos >= len) return false;
        ++pos;
    }
    if (flg & 0x02) pos += 2;
    if (pos + 8 > len) {
        if (err) *err = "Truncated GZip stream";
        return false;
    }

    size_t deflate_len = len - pos - 8;
    if (!deflate_decompress(data + pos, deflate_len, out, err)) return false;

    const uint8_t* trailer = data + len - 8;
    uint32_t expected_crc = static_cast<uint32_t>(trailer[0]) |
                            (static_cast<uint32_t>(trailer[1]) << 8) |
                            (static_cast<uint32_t>(trailer[2]) << 16) |
                            (static_cast<uint32_t>(trailer[3]) << 24);
    uint32_t expected_sz  = static_cast<uint32_t>(trailer[4]) |
                            (static_cast<uint32_t>(trailer[5]) << 8) |
                            (static_cast<uint32_t>(trailer[6]) << 16) |
                            (static_cast<uint32_t>(trailer[7]) << 24);

    if (crypto::crc32(out) != expected_crc) {
        if (err) *err = "GZip CRC32 checksum mismatch";
        return false;
    }
    if (static_cast<uint32_t>(out.size() & 0xFFFFFFFFULL) != expected_sz) {
        if (err) *err = "GZip ISIZE mismatch";
        return false;
    }
    return true;
}

} // namespace openrar::codec
