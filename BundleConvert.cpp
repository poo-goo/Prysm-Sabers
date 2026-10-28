#include "BundleConvert.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace PrysmSabers {
    namespace {
        using Bytes = std::vector<uint8_t>;

        constexpr int32_t AndroidPlatform = 13;

        // ------------------------------------------------------------------------------------------------
        // LZ4 block decoder
        // ------------------------------------------------------------------------------------------------
        bool Lz4Block(uint8_t const* src, size_t srcSize, uint8_t* dst, size_t dstSize) {
            size_t i = 0, o = 0;
            while (i < srcSize) {
                unsigned token = src[i++];
                size_t lit = token >> 4;
                if (lit == 15) {
                    unsigned b;
                    do {
                        if (i >= srcSize)
                            return false;
                        b = src[i++];
                        lit += b;
                    } while (b == 255);
                }
                if (i + lit > srcSize || o + lit > dstSize)
                    return false;
                std::memcpy(dst + o, src + i, lit);
                i += lit;
                o += lit;
                if (i >= srcSize)
                    break;
                if (i + 2 > srcSize)
                    return false;
                size_t off = src[i] | (src[i + 1] << 8);
                i += 2;
                size_t ml = token & 15;
                if (ml == 15) {
                    unsigned b;
                    do {
                        if (i >= srcSize)
                            return false;
                        b = src[i++];
                        ml += b;
                    } while (b == 255);
                }
                ml += 4;
                if (off == 0 || off > o || o + ml > dstSize)
                    return false;
                for (size_t k = 0; k < ml; k++, o++)
                    dst[o] = dst[o - off];
            }
            return o == dstSize;
        }

        // ------------------------------------------------------------------------------------------------
        // LZMA (LZMA1, as used by Unity: 5 property bytes, then the raw stream; output size is known)
        // ------------------------------------------------------------------------------------------------
        struct RangeDecoder {
            uint8_t const* buf;
            size_t size, pos = 0;
            uint32_t range = 0xFFFFFFFFu, code = 0;
            bool bad = false;

            RangeDecoder(uint8_t const* b, size_t s) : buf(b), size(s) {
                if (size < 5 || buf[0] != 0) {
                    bad = true;
                    return;
                }
                for (int i = 1; i < 5; i++)
                    code = (code << 8) | buf[i];
                pos = 5;
            }
            uint8_t Next() {
                if (pos >= size) {
                    bad = true;
                    return 0;
                }
                return buf[pos++];
            }
            void Normalize() {
                if (range < (1u << 24)) {
                    range <<= 8;
                    code = (code << 8) | Next();
                }
            }
            unsigned Bit(uint16_t* prob) {
                uint32_t v = *prob;
                uint32_t bound = (range >> 11) * v;
                unsigned sym;
                if (code < bound) {
                    v += (2048 - v) >> 5;
                    range = bound;
                    sym = 0;
                } else {
                    v -= v >> 5;
                    code -= bound;
                    range -= bound;
                    sym = 1;
                }
                *prob = (uint16_t) v;
                Normalize();
                return sym;
            }
            uint32_t Direct(unsigned n) {
                uint32_t res = 0;
                while (n--) {
                    range >>= 1;
                    code -= range;
                    uint32_t t = 0 - (code >> 31);
                    code += range & t;
                    if (code == range)
                        bad = true;
                    Normalize();
                    res = (res << 1) + t + 1;
                }
                return res;
            }
        };

        unsigned BitTree(RangeDecoder& rc, uint16_t* probs, unsigned bits) {
            unsigned m = 1;
            for (unsigned i = 0; i < bits; i++)
                m = (m << 1) + rc.Bit(probs + m);
            return m - (1u << bits);
        }

        unsigned BitTreeReverse(RangeDecoder& rc, uint16_t* probs, unsigned bits) {
            unsigned m = 1, sym = 0;
            for (unsigned i = 0; i < bits; i++) {
                unsigned b = rc.Bit(probs + m);
                m = (m << 1) + b;
                sym |= b << i;
            }
            return sym;
        }

        struct LenDecoder {
            uint16_t choice, choice2, low[16][8], mid[16][8], high[256];
            void Init() {
                choice = choice2 = 1024;
                for (auto& a : low)
                    for (auto& p : a)
                        p = 1024;
                for (auto& a : mid)
                    for (auto& p : a)
                        p = 1024;
                for (auto& p : high)
                    p = 1024;
            }
            unsigned Decode(RangeDecoder& rc, unsigned posState) {
                if (rc.Bit(&choice) == 0)
                    return BitTree(rc, low[posState], 3);
                if (rc.Bit(&choice2) == 0)
                    return 8 + BitTree(rc, mid[posState], 3);
                return 16 + BitTree(rc, high, 8);
            }
        };

        bool Lzma(uint8_t const* src, size_t srcSize, uint8_t* out, size_t outSize) {
            if (srcSize < 5)
                return false;
            unsigned d = src[0];
            if (d >= 9 * 5 * 5)
                return false;
            unsigned lc = d % 9;
            d /= 9;
            unsigned lp = d % 5;
            unsigned pb = d / 5;

            RangeDecoder rc(src + 5, srcSize - 5);
            if (rc.bad)
                return false;

            std::vector<uint16_t> lit((size_t) 0x300 << (lc + lp), 1024);
            uint16_t isMatch[12 << 4], isRep[12], isRepG0[12], isRepG1[12], isRepG2[12], isRep0Long[12 << 4];
            uint16_t posSlot[4][64], posDecoders[1 + 128 - 14], align[16];
            auto fill = [](uint16_t* p, size_t n) {
                for (size_t i = 0; i < n; i++)
                    p[i] = 1024;
            };
            fill(isMatch, 12 << 4);
            fill(isRep, 12);
            fill(isRepG0, 12);
            fill(isRepG1, 12);
            fill(isRepG2, 12);
            fill(isRep0Long, 12 << 4);
            fill(&posSlot[0][0], 4 * 64);
            fill(posDecoders, 1 + 128 - 14);
            fill(align, 16);
            LenDecoder lenDec, repLenDec;
            lenDec.Init();
            repLenDec.Init();

            size_t pos = 0;
            uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
            unsigned state = 0;

            while (pos < outSize) {
                if (rc.bad)
                    return false;
                unsigned posState = (unsigned) pos & ((1u << pb) - 1);

                if (rc.Bit(&isMatch[(state << 4) + posState]) == 0) {
                    unsigned prev = pos ? out[pos - 1] : 0;
                    unsigned symbol = 1;
                    unsigned litState = (((unsigned) pos & ((1u << lp) - 1)) << lc) + (prev >> (8 - lc));
                    uint16_t* probs = &lit[(size_t) 0x300 * litState];
                    if (state >= 7) {
                        if (rep0 >= pos)
                            return false;
                        unsigned matchByte = out[pos - rep0 - 1];
                        do {
                            unsigned matchBit = (matchByte >> 7) & 1;
                            matchByte <<= 1;
                            unsigned bit = rc.Bit(&probs[((1 + matchBit) << 8) + symbol]);
                            symbol = (symbol << 1) | bit;
                            if (matchBit != bit)
                                break;
                        } while (symbol < 0x100);
                    }
                    while (symbol < 0x100)
                        symbol = (symbol << 1) | rc.Bit(&probs[symbol]);
                    out[pos++] = (uint8_t) symbol;
                    state = state < 4 ? 0 : (state < 10 ? state - 3 : state - 6);
                    continue;
                }

                unsigned len;
                if (rc.Bit(&isRep[state]) != 0) {
                    if (pos == 0)
                        return false;
                    if (rc.Bit(&isRepG0[state]) == 0) {
                        if (rc.Bit(&isRep0Long[(state << 4) + posState]) == 0) {
                            if (rep0 >= pos)
                                return false;
                            state = state < 7 ? 9 : 11;
                            out[pos] = out[pos - rep0 - 1];
                            pos++;
                            continue;
                        }
                    } else {
                        uint32_t dist;
                        if (rc.Bit(&isRepG1[state]) == 0) {
                            dist = rep1;
                        } else {
                            if (rc.Bit(&isRepG2[state]) == 0) {
                                dist = rep2;
                            } else {
                                dist = rep3;
                                rep3 = rep2;
                            }
                            rep2 = rep1;
                        }
                        rep1 = rep0;
                        rep0 = dist;
                    }
                    len = repLenDec.Decode(rc, posState);
                    state = state < 7 ? 8 : 11;
                } else {
                    rep3 = rep2;
                    rep2 = rep1;
                    rep1 = rep0;
                    len = lenDec.Decode(rc, posState);
                    state = state < 7 ? 7 : 10;

                    unsigned lenState = len > 3 ? 3 : len;
                    unsigned slot = BitTree(rc, posSlot[lenState], 6);
                    if (slot < 4) {
                        rep0 = slot;
                    } else {
                        unsigned direct = (slot >> 1) - 1;
                        uint32_t dist = (2 | (slot & 1)) << direct;
                        if (slot < 14) {
                            dist += BitTreeReverse(rc, posDecoders + dist - slot, direct);
                        } else {
                            dist += rc.Direct(direct - 4) << 4;
                            dist += BitTreeReverse(rc, align, 4);
                        }
                        rep0 = dist;
                    }
                    if (rep0 == 0xFFFFFFFFu)
                        break;  // end marker
                }

                len += 2;
                if (rep0 >= pos || pos + len > outSize)
                    return false;
                for (unsigned k = 0; k < len; k++, pos++)
                    out[pos] = out[pos - rep0 - 1];
            }
            return pos == outSize && !rc.bad;
        }

        bool Decompress(uint8_t const* src, size_t srcSize, uint8_t* dst, size_t outSize, unsigned type) {
            switch (type) {
                case 0:
                    if (srcSize != outSize)
                        return false;
                    std::memcpy(dst, src, outSize);
                    return true;
                case 1: return Lzma(src, srcSize, dst, outSize);
                case 2:
                case 3: return Lz4Block(src, srcSize, dst, outSize);
                default: return false;
            }
        }

        bool Decompress(uint8_t const* src, size_t srcSize, Bytes& out, size_t outSize, unsigned type) {
            out.resize(outSize);
            return Decompress(src, srcSize, out.data(), outSize, type);
        }

        uint32_t Be32(uint8_t const* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }
        uint64_t Be64(uint8_t const* p) { return ((uint64_t) Be32(p) << 32) | Be32(p + 4); }
        void PutBe32(Bytes& b, uint32_t v) {
            for (int s = 24; s >= 0; s -= 8)
                b.push_back((uint8_t) (v >> s));
        }
        void PutBe64(Bytes& b, uint64_t v) {
            PutBe32(b, (uint32_t) (v >> 32));
            PutBe32(b, (uint32_t) v);
        }

        struct Node {
            uint64_t offset, size;
            uint32_t flags;
            std::string name;
        };
    }

    ConvertResult ConvertBundleToAndroid(std::string const& inPath, std::string const& outPath) {
        ConvertResult res;

        Bytes file;
        {
            std::ifstream f(inPath, std::ios::binary | std::ios::ate);
            if (!f) {
                res.message = "cannot open file";
                return res;
            }
            auto size = (size_t) f.tellg();
            if (size < 64 || size > 400u * 1024u * 1024u) {
                res.message = "file size not supported";
                return res;
            }
            file.resize(size);
            f.seekg(0);
            f.read((char*) file.data(), (std::streamsize) size);
        }
        auto fail = [&](char const* m) {
            res.message = m;
            return res;
        };

        if (std::memcmp(file.data(), "UnityFS\0", 8) != 0)
            return fail("not a UnityFS bundle");
        size_t p = 8;
        uint32_t version = Be32(&file[p]);
        p += 4;
        if (version > 7)
            return fail("bundle format version not supported");

        auto readCStr = [&](std::string& s) {
            size_t e = p;
            while (e < file.size() && file[e] != 0)
                e++;
            if (e >= file.size())
                return false;
            s.assign((char const*) &file[p], e - p);
            p = e + 1;
            return true;
        };
        std::string playerVersion, engineVersion;
        if (!readCStr(playerVersion) || !readCStr(engineVersion) || p + 20 > file.size())
            return fail("damaged header");

        uint64_t total = Be64(&file[p]);
        uint32_t infoComp = Be32(&file[p + 8]), infoUncomp = Be32(&file[p + 12]), flags = Be32(&file[p + 16]);
        p += 20;
        (void) total;
        if (version >= 7 && (p % 16))
            p += 16 - p % 16;

        size_t infoPos = p;
        if (flags & 0x80) {
            if (infoComp > file.size())
                return fail("damaged header");
            infoPos = file.size() - infoComp;
        } else {
            p += infoComp;
        }
        if (infoPos + infoComp > file.size())
            return fail("damaged header");
        if (version >= 7 && !(flags & 0x80) && (p % 16))
            p += 16 - p % 16;

        Bytes info;
        if (!Decompress(&file[infoPos], infoComp, info, infoUncomp, flags & 0x3f))
            return fail("could not read the bundle's directory");

        size_t q = 16;
        if (q + 4 > info.size())
            return fail("damaged directory");
        int32_t blockCount = (int32_t) Be32(&info[q]);
        q += 4;
        struct Block {
            uint32_t usize, csize;
            uint16_t flags;
        };
        std::vector<Block> blocks;
        for (int32_t i = 0; i < blockCount; i++) {
            if (q + 10 > info.size())
                return fail("damaged directory");
            blocks.push_back({Be32(&info[q]), Be32(&info[q + 4]), (uint16_t) ((info[q + 8] << 8) | info[q + 9])});
            q += 10;
        }
        if (q + 4 > info.size())
            return fail("damaged directory");
        int32_t nodeCount = (int32_t) Be32(&info[q]);
        q += 4;
        std::vector<Node> nodes;
        for (int32_t i = 0; i < nodeCount; i++) {
            if (q + 20 > info.size())
                return fail("damaged directory");
            Node n;
            n.offset = Be64(&info[q]);
            n.size = Be64(&info[q + 8]);
            n.flags = Be32(&info[q + 16]);
            q += 20;
            size_t e = q;
            while (e < info.size() && info[e] != 0)
                e++;
            if (e >= info.size())
                return fail("damaged directory");
            n.name.assign((char const*) &info[q], e - q);
            q = e + 1;
            nodes.push_back(std::move(n));
        }

        // Cheap check first: decode only the start of the data and look at the platform. Already-Android bundles
        // (the common case for .whacker files) then cost almost nothing and are never copied.
        if (!blocks.empty() && !nodes.empty()) {
            auto const& b0 = blocks[0];
            size_t peekSize = std::min<size_t>(b0.usize, (b0.flags & 0x3f) == 1 ? 4096 : b0.usize);
            Bytes head;
            if (p + b0.csize <= file.size() && Decompress(&file[p], b0.csize, head, peekSize, b0.flags & 0x3f)) {
                auto const& n0 = nodes[0];
                if (n0.offset + 40 <= head.size() && Be32(&head[(size_t) n0.offset + 8]) >= 17 && Be32(&head[(size_t) n0.offset + 8]) <= 21) {
                    size_t e = (size_t) n0.offset + 20;
                    while (e < head.size() && head[e] != 0)
                        e++;
                    int32_t plat;
                    if (e + 5 <= head.size()) {
                        std::memcpy(&plat, &head[e + 1], 4);
                        if (plat == AndroidPlatform) {
                            res.ok = true;
                            res.message = "already an Android bundle";
                            return res;
                        }
                    }
                }
            }
        }

        // decompress all data blocks straight into one buffer
        Bytes data;
        {
            size_t totalSize = 0;
            for (auto const& b : blocks)
                totalSize += b.usize;
            if (totalSize > 400u * 1024u * 1024u)
                return fail("bundle too large");
            data.resize(totalSize);
            size_t pos = p, outPos = 0;
            for (auto const& b : blocks) {
                if (pos + b.csize > file.size())
                    return fail("damaged data");
                if (!Decompress(&file[pos], b.csize, &data[outPos], b.usize, b.flags & 0x3f))
                    return fail("could not decompress the bundle data");
                pos += b.csize;
                outPos += b.usize;
            }
        }
        file.clear();
        file.shrink_to_fit();

        // switch the target platform inside each serialized file
        int patched = 0, alreadyAndroid = 0;
        for (auto const& n : nodes) {
            if (n.offset + n.size > data.size() || n.size < 32)
                continue;
            uint8_t* sf = &data[(size_t) n.offset];
            uint32_t sfVersion = Be32(sf + 8);
            if (sfVersion < 17 || sfVersion > 21)
                continue;  // not a serialized file (e.g. a .resS stream), or a layout we do not know
            size_t e = 20;
            while (e < n.size && sf[e] != 0)
                e++;
            if (e + 5 > n.size)
                continue;
            int32_t plat;
            std::memcpy(&plat, sf + e + 1, 4);
            if (plat == AndroidPlatform) {
                alreadyAndroid++;
                continue;
            }
            int32_t android = AndroidPlatform;
            std::memcpy(sf + e + 1, &android, 4);
            patched++;
        }
        if (patched == 0) {
            res.ok = alreadyAndroid > 0;
            res.message = alreadyAndroid > 0 ? "already an Android bundle" : "no serialized file found in the bundle";
            return res;
        }

        // write it back uncompressed (128 KiB blocks)
        constexpr size_t BlockSize = 131072;
        Bytes newInfo(16, 0);
        size_t blockTotal = (data.size() + BlockSize - 1) / BlockSize;
        PutBe32(newInfo, (uint32_t) blockTotal);
        for (size_t o = 0; o < data.size(); o += BlockSize) {
            uint32_t sz = (uint32_t) std::min(BlockSize, data.size() - o);
            PutBe32(newInfo, sz);
            PutBe32(newInfo, sz);
            newInfo.push_back(0);
            newInfo.push_back(0);
        }
        PutBe32(newInfo, (uint32_t) nodes.size());
        for (auto const& n : nodes) {
            PutBe64(newInfo, n.offset);
            PutBe64(newInfo, n.size);
            PutBe32(newInfo, n.flags);
            newInfo.insert(newInfo.end(), n.name.begin(), n.name.end());
            newInfo.push_back(0);
        }

        Bytes head;
        head.insert(head.end(), {'U', 'n', 'i', 't', 'y', 'F', 'S', 0});
        PutBe32(head, version);
        head.insert(head.end(), playerVersion.begin(), playerVersion.end());
        head.push_back(0);
        head.insert(head.end(), engineVersion.begin(), engineVersion.end());
        head.push_back(0);
        size_t headerEnd = head.size() + 8 + 12;
        PutBe64(head, headerEnd + newInfo.size() + data.size());
        PutBe32(head, (uint32_t) newInfo.size());
        PutBe32(head, (uint32_t) newInfo.size());
        PutBe32(head, 0x40);

        std::ofstream o(outPath, std::ios::binary | std::ios::trunc);
        if (!o)
            return fail("cannot write the converted file");
        o.write((char const*) head.data(), (std::streamsize) head.size());
        o.write((char const*) newInfo.data(), (std::streamsize) newInfo.size());
        o.write((char const*) data.data(), (std::streamsize) data.size());
        o.close();
        if (!o)
            return fail("writing the converted file failed");

        res.ok = true;
        res.converted = true;
        res.message = "converted to Android (" + std::to_string(patched) + " serialized file(s) retargeted)";
        return res;
    }
}
