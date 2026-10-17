// Breast wave shader patch (SHADER-PLAN.md, user 2026-10-17: breasts first).
//
// The game's skinned vertex shaders read the Sim's world matrix from cb0 rows 0-2 and the bone palette from
// row `base` on (3 rows per bone, BLENDINDICES*3 + base). Row 3 is padding (0,0,0,1) that these shaders never
// read. The patch adds, before the shader's own code:
//
//     w      = sum of BLENDWEIGHT where BLENDINDICES + kSlotTag == cb0[3].x or cb0[3].y (int bits)
//     pos    = POSITION
//     pos.z += w * cb0[3].z * sin(cb0[3].w - 30 * POSITION.y * cb1[0].x) / cb1[0].x
//
// and every later read of POSITION reads pos. Model space: +Z is the Sim's front, +Y up; cb1[0].x scales the
// stored position to metres. With row 3 untouched (0,0,0,1) the amplitude is 0: the shader draws as before.
// The container's checksum (DXBC's modified MD5) is recomputed; if the device still refuses the patched shader,
// the caller creates the original.
#include "wavepatch.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace wavepatch {
namespace {

constexpr float kWaveK = 30.0f;  // radians per metre of height: the ripple's length
// Row 3's slots are written as slot + kSlotTag (wavepatch.h, int bits): data that is not ours (0.0, ordinary
// floats) never names a bone. (0.11.15's capture: ~3% of these shaders' blocks hold other values in row 3.)

uint32_t rd(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
void wr(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
uint32_t bits(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}

// ---- DXBC checksum: MD5's transform with its own padding (size in bits first, (bits >> 2) | 1 last) ----
void md5Transform(uint32_t st[4], const uint8_t* blk) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                              14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t M[16];
    for (int i = 0; i < 16; ++i) M[i] = rd(blk + 4 * i);
    uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        int g;
        if (i < 16) f = (b & c) | (~b & d), g = i;
        else if (i < 32) f = (d & b) | (~d & c), g = (5 * i + 1) % 16;
        else if (i < 48) f = b ^ c ^ d, g = (3 * i + 5) % 16;
        else f = c ^ (b | ~d), g = (7 * i) % 16;
        f += a + K[i] + M[g];
        a = d;
        d = c;
        c = b;
        b += (f << S[i]) | (f >> (32 - S[i]));
    }
    st[0] += a;
    st[1] += b;
    st[2] += c;
    st[3] += d;
}

void checksum(std::vector<uint8_t>& dxbc) {
    const uint8_t* d = dxbc.data() + 20;
    size_t n = dxbc.size() - 20, full = n - n % 64, left = n - full;
    uint32_t st[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    for (size_t i = 0; i < full; i += 64) md5Transform(st, d + i);
    uint32_t b1 = static_cast<uint32_t>(n * 8), b2 = (b1 >> 2) | 1;
    uint8_t blk[64] = {};
    if (left >= 56) {
        std::memcpy(blk, d + full, left);
        blk[left] = 0x80;
        md5Transform(st, blk);
        std::memset(blk, 0, 64);
        wr(blk, b1);
        wr(blk + 60, b2);
        md5Transform(st, blk);
    } else {
        wr(blk, b1);
        std::memcpy(blk + 4, d + full, left);
        blk[4 + left] = 0x80;
        wr(blk + 60, b2);
        md5Transform(st, blk);
    }
    for (int i = 0; i < 4; ++i) wr(dxbc.data() + 4 + 4 * i, st[i]);
}

// ---- SHEX tokens ----
struct Operand {
    size_t pos;
    unsigned type, dim;
    bool hasIdx0;
    uint32_t idx0;
    bool relative;
};

// The operand at t[p]; returns the token after it, or 0 if it runs past `end`.
size_t operand(const std::vector<uint32_t>& t, size_t p, size_t end, Operand* o) {
    if (p >= end) return 0;
    uint32_t tok = t[p];
    Operand me{p, (tok >> 12) & 0xff, (tok >> 20) & 3, false, 0, false};
    ++p;
    if (tok >> 31) {
        while (p < end && (t[p++] >> 31)) {
        }
    }
    unsigned nc = tok & 3;
    if (me.type == 4 || me.type == 5) {
        p += (nc == 1 ? 1 : 4) * (me.type == 5 ? 2 : 1);
    } else {
        for (unsigned k = 0; k < me.dim; ++k) {
            unsigned rep = (tok >> (22 + 3 * k)) & 7;
            if (rep == 0 || rep == 3) {
                if (p >= end) return 0;
                if (k == 0) me.hasIdx0 = true, me.idx0 = t[p];
                ++p;
            } else if (rep == 1 || rep == 4) {
                p += 2;
            }
            if (rep == 2 || rep == 3 || rep == 4) {
                me.relative = true;
                p = operand(t, p, end, nullptr);
                if (!p) return 0;
            }
        }
    }
    if (o) *o = me;
    return p <= end ? p : 0;
}

bool operands(const std::vector<uint32_t>& t, size_t i, size_t len, std::vector<Operand>& out) {
    out.clear();
    size_t p = i + 1, end = i + len;
    if (t[i] >> 31) {
        while (p < end && (t[p++] >> 31)) {
        }
    }
    while (p < end) {
        Operand o;
        p = operand(t, p, end, &o);
        if (!p) return false;
        out.push_back(o);
    }
    return true;
}

// Input signature: (semantic, index) -> register.
int inputRegister(const uint8_t* chunk, size_t size, const char* name, unsigned index) {
    if (size < 8) return -1;
    uint32_t n = rd(chunk);
    for (uint32_t i = 0; i < n; ++i) {
        size_t e = 8 + 24 * size_t(i);
        if (e + 24 > size) return -1;
        uint32_t nameAt = rd(chunk + e), idx = rd(chunk + e + 4), reg = rd(chunk + e + 16);
        if (nameAt >= size) return -1;
        const char* s = reinterpret_cast<const char*>(chunk + nameAt);
        if (strnlen(s, size - nameAt) == size - nameAt) return -1;
        if (idx == index && std::strcmp(s, name) == 0) return static_cast<int>(reg);
    }
    return -1;
}

// Operand encoders (SM4/5 tokens).
void tmask(std::vector<uint32_t>& v, uint32_t r, uint32_t m) { v.insert(v.end(), {0x100002u | (m << 4), r}); }
void tswz(std::vector<uint32_t>& v, uint32_t r) { v.insert(v.end(), {0x100006u | (0xe4u << 4), r}); }
void tswzOf(std::vector<uint32_t>& v, uint32_t r, uint32_t s) { v.insert(v.end(), {0x100006u | (s << 4), r}); }
void tsel(std::vector<uint32_t>& v, uint32_t r, uint32_t c) { v.insert(v.end(), {0x10000au | (c << 4), r}); }
void iswz(std::vector<uint32_t>& v, uint32_t r, uint32_t s) { v.insert(v.end(), {0x101006u | (s << 4), r}); }
void isel(std::vector<uint32_t>& v, uint32_t r, uint32_t c) { v.insert(v.end(), {0x10100au | (c << 4), r}); }
void cbswz(std::vector<uint32_t>& v, uint32_t b, uint32_t row, uint32_t s) {
    v.insert(v.end(), {0x208006u | (s << 4), b, row});
}
void cbsel(std::vector<uint32_t>& v, uint32_t b, uint32_t row, uint32_t c) {
    v.insert(v.end(), {0x20800au | (c << 4), b, row});
}

struct Emit {
    std::vector<uint32_t> code, ops;
    void op(uint32_t opcode) {
        code.push_back(opcode | (uint32_t(1 + ops.size()) << 24));
        code.insert(code.end(), ops.begin(), ops.end());
        ops.clear();
    }
};

}  // namespace

bool patch(const uint8_t* code, size_t size, std::vector<uint8_t>& out, unsigned* base) {
    if (!code || size < 32 || size > (1u << 22) || std::memcmp(code, "DXBC", 4) != 0 || rd(code + 24) != size)
        return false;
    uint32_t count = rd(code + 28);
    if (count > 32 || 32 + 4 * size_t(count) > size) return false;
    struct Chunk {
        uint32_t tag;
        const uint8_t* data;
        uint32_t size;
    };
    std::vector<Chunk> chunks;
    const uint8_t *isgn = nullptr, *shex = nullptr;
    uint32_t isgnSize = 0, shexSize = 0, shexTag = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t at = rd(code + 32 + 4 * i);
        if (at + 8 > size) return false;
        uint32_t sz = rd(code + at + 4);
        if (at + 8 + size_t(sz) > size) return false;
        Chunk c{rd(code + at), code + at + 8, sz};
        chunks.push_back(c);
        if (std::memcmp(code + at, "ISGN", 4) == 0) isgn = c.data, isgnSize = sz;
        if (std::memcmp(code + at, "SHEX", 4) == 0 || std::memcmp(code + at, "SHDR", 4) == 0)
            shex = c.data, shexSize = sz, shexTag = c.tag;
    }
    if (!isgn || !shex || shexSize % 4 || shexSize < 8) return false;
    int vp = inputRegister(isgn, isgnSize, "POSITION", 0), vi = inputRegister(isgn, isgnSize, "BLENDINDICES", 0),
        vw = inputRegister(isgn, isgnSize, "BLENDWEIGHT", 0);
    if (vp < 0 || vi < 0 || vw < 0) return false;

    std::vector<uint32_t> t(shexSize / 4);
    std::memcpy(t.data(), shex, shexSize);
    if (t[1] != t.size()) return false;
    struct Ins {
        size_t at, len;
        unsigned op;
    };
    std::vector<Ins> ins;
    for (size_t i = 2; i < t.size();) {
        unsigned op = t[i] & 0x7ff;
        size_t len = op == 53 ? (i + 1 < t.size() ? t[i + 1] : 0) : (t[i] >> 24) & 0x7f;
        if (!len || i + len > t.size()) return false;
        ins.push_back({i, len, op});
        i += len;
    }
    auto isCode = [](unsigned op) { return op < 88 && op != 53; };
    size_t tempsAt = 0, first = 0;
    bool haveFirst = false;
    uint32_t temps = 0;
    uint32_t posMask = 0;  // POSITION's declared components
    std::vector<uint32_t> bases;
    std::vector<Operand> ops;
    for (const Ins& n : ins) {
        if (n.op == 104 && n.len == 2) tempsAt = n.at + 1, temps = t[n.at + 1];
        if (n.op == 95 && n.len == 3 && t[n.at + 2] == uint32_t(vp)) posMask = (t[n.at + 1] >> 4) & 0xf;
        if (!isCode(n.op)) continue;
        if (!haveFirst) first = n.at, haveFirst = true;
        if (!operands(t, n.at, n.len, ops)) return false;
        for (const Operand& o : ops) {
            if (o.type != 8 || o.dim != 2 || !o.hasIdx0 || o.idx0 != 0) continue;
            unsigned rep1 = (t[o.pos] >> 25) & 7;
            size_t idx1 = o.pos + 2 + ((t[o.pos] >> 31) ? 1 : 0);
            if (idx1 >= t.size()) return false;
            if (rep1 == 3) {
                if (std::find(bases.begin(), bases.end(), t[idx1]) == bases.end()) bases.push_back(t[idx1]);
            } else if (rep1 == 0 && t[idx1] == 3) {
                return false;  // this shader reads row 3 itself
            }
        }
    }
    if (!tempsAt || !haveFirst || !posMask || bases.size() != 1 || temps > 4000) return false;
    *base = bases[0];
    const uint32_t P = temps, A = temps + 1, B = temps + 2;

    // Later reads of POSITION read P.
    for (const Ins& n : ins) {
        if (!isCode(n.op)) continue;
        operands(t, n.at, n.len, ops);
        for (const Operand& o : ops)
            if (o.type == 1 && o.dim == 1 && o.hasIdx0 && o.idx0 == uint32_t(vp) && !o.relative && !(t[o.pos] >> 31)) {
                t[o.pos] &= ~(0xffu << 12);  // input -> temp
                t[o.pos + 1] = P;
            }
    }
    t[tempsAt] = temps + 3;

    Emit e;
    // mov P.<declared>, v: every declared component (0.11.19 copied only xyz; a shader reading POSITION.w, which
    // divides by it, then drew nothing: the invisible woman, user 2026-10-17 14:42Z).
    tmask(e.ops, P, posMask), iswz(e.ops, vp, 0xe4), e.op(54);
    tmask(e.ops, B, 3), cbswz(e.ops, 0, 3, 0x04);                             // iadd B.xy, cb0[3].xyxx, -kSlotTag
    e.ops.insert(e.ops.end(), {0x4002u, 0u - kSlotTag, 0u - kSlotTag, 0u, 0u}), e.op(30);
    tmask(e.ops, A, 15), iswz(e.ops, vi, 0xe4), tswzOf(e.ops, B, 0x00), e.op(32);  // ieq A, vI, B.xxxx
    tmask(e.ops, B, 15), iswz(e.ops, vi, 0xe4), tswzOf(e.ops, B, 0x55), e.op(32);  // ieq B, vI, B.yyyy
    tmask(e.ops, A, 15), tswz(e.ops, A), tswz(e.ops, B), e.op(60);             // or A, A, B
    tmask(e.ops, A, 15), tswz(e.ops, A), iswz(e.ops, vw, 0xe4), e.op(1);       // and A, A, vW
    tmask(e.ops, A, 1), tswz(e.ops, A);                                        // dp4 A.x, A, l(1,1,1,1)
    e.ops.insert(e.ops.end(), {0x4002u, bits(1), bits(1), bits(1), bits(1)}), e.op(17);
    tmask(e.ops, A, 1), tsel(e.ops, A, 0), cbsel(e.ops, 0, 3, 2), e.op(56);   // mul A.x, A.x, cb0[3].z
    tmask(e.ops, B, 1), isel(e.ops, vp, 1), cbsel(e.ops, 1, 0, 0), e.op(56);  // mul B.x, v.y, cb1[0].x
    tmask(e.ops, B, 1), tsel(e.ops, B, 0);                                     // mad B.x, B.x, -K, cb0[3].w
    e.ops.insert(e.ops.end(), {0x4001u, bits(-kWaveK)}), cbsel(e.ops, 0, 3, 3), e.op(50);
    tmask(e.ops, B, 1), e.ops.push_back(0xd000u), tsel(e.ops, B, 0), e.op(77);  // sincos B.x, null, B.x
    tmask(e.ops, A, 1), tsel(e.ops, A, 0), tsel(e.ops, B, 0), e.op(56);         // mul A.x, A.x, B.x
    tmask(e.ops, A, 1), tsel(e.ops, A, 0), cbsel(e.ops, 1, 0, 0), e.op(14);     // div A.x, A.x, cb1[0].x
    tmask(e.ops, P, 4), tsel(e.ops, P, 2), tsel(e.ops, A, 0), e.op(0);          // add P.z, P.z, A.x

    t.insert(t.begin() + first, e.code.begin(), e.code.end());
    t[1] = static_cast<uint32_t>(t.size());

    // Rebuild the container with the new SHEX.
    size_t header = 32 + 4 * chunks.size(), total = header;
    for (const Chunk& c : chunks) total += 8 + (c.tag == shexTag && c.data == shex ? t.size() * 4 : c.size);
    out.assign(total, 0);
    std::memcpy(out.data(), code, 32);
    wr(out.data() + 24, static_cast<uint32_t>(total));
    size_t at = header;
    for (size_t i = 0; i < chunks.size(); ++i) {
        const Chunk& c = chunks[i];
        bool mine = c.tag == shexTag && c.data == shex;
        uint32_t sz = mine ? static_cast<uint32_t>(t.size() * 4) : c.size;
        wr(out.data() + 32 + 4 * i, static_cast<uint32_t>(at));
        wr(out.data() + at, c.tag);
        wr(out.data() + at + 4, sz);
        std::memcpy(out.data() + at + 8, mine ? reinterpret_cast<const uint8_t*>(t.data()) : c.data, sz);
        at += 8 + sz;
    }
    checksum(out);
    return true;
}

}  // namespace wavepatch
