#include "rtcnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

/* ============================================================================
   codes.cpp -- the connection-code format web/webrtc.js defined.

       'CLZ' + base64( gzip( JSON {t:"offer"|"answer", d:<sdp>} ) )
       'CLR' + base64(       JSON {t:"offer"|"answer", d:<sdp>} )

   A browser writes CLZ whenever it has CompressionStream, so this has to be
   able to READ gzip. It never needs to WRITE it: every browser reads CLR, and
   codes travel through the room broker rather than a chat window, so the few
   hundred bytes compression would save buy nothing. That asymmetry is why
   there is an inflater here and no deflater.

   The inflater is written out rather than pulled in as zlib: it is the one
   piece of zlib this needs, it is about a hundred lines, and a second
   third-party library in build.bat costs more than that to keep building.
   ========================================================================== */

/* --- base64 ----------------------------------------------------------------- */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64encode(const std::string& in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const u32 v = ((u8)in[i] << 16) | ((u8)in[i + 1] << 8) | (u8)in[i + 2];
        out += B64[(v >> 18) & 63]; out += B64[(v >> 12) & 63];
        out += B64[(v >> 6) & 63];  out += B64[v & 63];
    }
    if (i + 1 == in.size()) {
        const u32 v = (u8)in[i] << 16;
        out += B64[(v >> 18) & 63]; out += B64[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        const u32 v = ((u8)in[i] << 16) | ((u8)in[i + 1] << 8);
        out += B64[(v >> 18) & 63]; out += B64[(v >> 12) & 63];
        out += B64[(v >> 6) & 63]; out += '=';
    }
    return out;
}

/* Whitespace is skipped: a code that went through a chat client may come back
   wrapped, and webrtc.js strips it for the same reason. */
static bool b64decode(const char* in, std::vector<u8>* out) {
    u32 acc = 0; int bits = 0;
    for (; *in; ++in) {
        const char c = *in;
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (c == '=') break;
        const char* at = strchr(B64, c);
        if (!at || !c) return false;
        acc = (acc << 6) | (u32)(at - B64); bits += 6;
        if (bits >= 8) { bits -= 8; out->push_back((u8)(acc >> bits)); }
    }
    return true;
}

/* --- inflate (RFC 1951) inside gzip (RFC 1952) ------------------------------ */

namespace {

struct Bits {
    const u8* p; size_t n, at; u32 buf; int count; bool bad;
    int bit() {
        if (!count) {
            if (at >= n) { bad = true; return 0; }
            buf = p[at++]; count = 8;
        }
        const int b = buf & 1; buf >>= 1; --count; return b;
    }
    u32 take(int k) { u32 v = 0; for (int i = 0; i < k; ++i) v |= (u32)bit() << i; return v; }
};

/* Canonical Huffman decoding by counts, as in Mark Adler's puff.c: slower
   than a lookup table and far simpler, and the input is a kilobyte. */
struct Huff {
    short count[16], symbol[320];
    bool build(const u8* lengths, int n) {
        memset(count, 0, sizeof(count));
        for (int i = 0; i < n; ++i) count[lengths[i]]++;
        if (count[0] == n) return true;          /* no codes: legal, unusable */
        int left = 1;
        for (int len = 1; len < 16; ++len) { left <<= 1; left -= count[len]; if (left < 0) return false; }
        short offs[16]; offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = offs[len] + count[len];
        for (int i = 0; i < n; ++i) if (lengths[i]) symbol[offs[lengths[i]]++] = (short)i;
        return true;
    }
    int decode(Bits& in) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= in.bit();
            const int c = count[len];
            if (code - c < first) return symbol[index + (code - first)];
            index += c; first += c; first <<= 1; code <<= 1;
            if (in.bad) return -1;
        }
        return -1;
    }
};

const short LBASE[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
const short LEXT[29]  = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
const short DBASE[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,
                          2049,3073,4097,6145,8193,12289,16385,24577 };
const short DEXT[30]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

/* Nothing a connection code decompresses to is anywhere near this; the cap
   is there so a malformed or hostile code cannot ask for gigabytes. */
const size_t INFLATE_LIMIT = 1 << 20;

bool codes(Bits& in, std::vector<u8>& out, const Huff& lit, const Huff& dist) {
    for (;;) {
        int sym = lit.decode(in);
        if (sym < 0 || in.bad) return false;
        if (sym < 256) { out.push_back((u8)sym); }
        else if (sym == 256) return true;
        else {
            sym -= 257;
            if (sym >= 29) return false;
            const size_t len = LBASE[sym] + in.take(LEXT[sym]);
            const int ds = dist.decode(in);
            if (ds < 0 || ds >= 30) return false;
            const size_t d = DBASE[ds] + in.take(DEXT[ds]);
            if (d > out.size() || in.bad) return false;
            for (size_t i = 0; i < len; ++i) out.push_back(out[out.size() - d]);
        }
        if (out.size() > INFLATE_LIMIT) return false;
    }
}

bool inflateRaw(const u8* p, size_t n, std::vector<u8>& out) {
    Bits in = { p, n, 0, 0, 0, false };
    int last;
    do {
        last = in.bit();
        const u32 type = in.take(2);
        if (in.bad) return false;
        if (type == 0) {
            in.count = 0;                                     /* to a byte boundary */
            if (in.at + 4 > n) return false;
            const u32 len = p[in.at] | (p[in.at + 1] << 8);
            const u32 nlen = p[in.at + 2] | (p[in.at + 3] << 8);
            in.at += 4;
            if ((len ^ 0xFFFF) != nlen || in.at + len > n) return false;
            out.insert(out.end(), p + in.at, p + in.at + len); in.at += len;
        } else if (type == 1) {
            /* Rebuilt per block rather than cached in a static: it costs
               nothing at this size and leaves no shared state between
               threads. */
            Huff lit, dist;
            u8 l[288];
            for (int i = 0; i < 144; ++i) l[i] = 8;
            for (int i = 144; i < 256; ++i) l[i] = 9;
            for (int i = 256; i < 280; ++i) l[i] = 7;
            for (int i = 280; i < 288; ++i) l[i] = 8;
            lit.build(l, 288);
            for (int i = 0; i < 30; ++i) l[i] = 5;
            dist.build(l, 30);
            if (!codes(in, out, lit, dist)) return false;
        } else if (type == 2) {
            const int nlen = (int)in.take(5) + 257, ndist = (int)in.take(5) + 1, ncode = (int)in.take(4) + 4;
            if (nlen > 286 || ndist > 30) return false;
            static const u8 ORDER[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
            u8 lengths[320]; memset(lengths, 0, sizeof(lengths));
            for (int i = 0; i < ncode; ++i) lengths[ORDER[i]] = (u8)in.take(3);
            Huff lencode;
            if (!lencode.build(lengths, 19)) return false;
            int at = 0;
            while (at < nlen + ndist) {
                const int sym = lencode.decode(in);
                if (sym < 0 || in.bad) return false;
                if (sym < 16) { lengths[at++] = (u8)sym; continue; }
                u8 fill = 0; int rep;
                if (sym == 16) { if (!at) return false; fill = lengths[at - 1]; rep = 3 + (int)in.take(2); }
                else if (sym == 17) rep = 3 + (int)in.take(3);
                else rep = 11 + (int)in.take(7);
                if (at + rep > nlen + ndist) return false;
                while (rep--) lengths[at++] = fill;
            }
            Huff lit, dist;
            if (!lit.build(lengths, nlen) || !dist.build(lengths + nlen, ndist)) return false;
            if (!codes(in, out, lit, dist)) return false;
        } else return false;
    } while (!last);
    return true;
}

bool gunzip(const std::vector<u8>& z, std::vector<u8>& out) {
    if (z.size() < 18 || z[0] != 0x1F || z[1] != 0x8B || z[2] != 8) return false;
    const u8 flags = z[3];
    size_t at = 10;
    if (flags & 4) { if (at + 2 > z.size()) return false; at += 2 + (z[at] | (z[at + 1] << 8)); }
    if (flags & 8)  { while (at < z.size() && z[at]) ++at; ++at; }
    if (flags & 16) { while (at < z.size() && z[at]) ++at; ++at; }
    if (flags & 2) at += 2;
    if (at >= z.size()) return false;
    return inflateRaw(&z[at], z.size() - at, out);
}

/* --- the smallest JSON that covers {t, d} ------------------------------------ */

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if ((u8)c < 0x20) { char e[8]; sprintf(e, "\\u%04x", (unsigned)(u8)c); out += e; }
            else out += c;
        }
    }
    return out;
}

} /* namespace */

/* Finds "key":"value" at the top level of a flat object and unescapes the
   value. Enough for everything that crosses a wire here -- webrtc.js's code
   bodies and the broker's replies -- and deliberately no more. Also used by
   room.cpp, which is why it is not static. */
bool rtcJsonString(const std::string& json, const char* key, std::string* out) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t at = 0;
    for (;;) {
        at = json.find(needle, at);
        if (at == std::string::npos) return false;
        size_t p = at + needle.size();
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r')) ++p;
        if (p < json.size() && json[p] == ':') {
            ++p;
            while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r')) ++p;
            if (p >= json.size() || json[p] != '"') return false;
            ++p;
            out->clear();
            while (p < json.size() && json[p] != '"') {
                char c = json[p++];
                if (c == '\\' && p < json.size()) {
                    c = json[p++];
                    switch (c) {
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'u': {
                        if (p + 4 > json.size()) return false;
                        const unsigned v = (unsigned)strtoul(json.substr(p, 4).c_str(), 0, 16);
                        p += 4;
                        /* SDP and broker text are ASCII. Anything wider is
                           written as UTF-8 so it is at least not corrupted. */
                        if (v < 0x80) c = (char)v;
                        else if (v < 0x800) { *out += (char)(0xC0 | (v >> 6)); c = (char)(0x80 | (v & 63)); }
                        else { *out += (char)(0xE0 | (v >> 12)); *out += (char)(0x80 | ((v >> 6) & 63));
                               c = (char)(0x80 | (v & 63)); }
                        break;
                    }
                    default: break;           /* \" \\ \/ are themselves */
                    }
                }
                *out += c;
            }
            return p < json.size();
        }
        at = p;
    }
}

bool rtcJsonInt(const std::string& json, const char* key, int* out) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t at = json.find(needle);
    if (at == std::string::npos) return false;
    size_t p = at + needle.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == ':')) ++p;
    if (p >= json.size() || !(json[p] == '-' || (json[p] >= '0' && json[p] <= '9'))) return false;
    *out = atoi(json.c_str() + p);
    return true;
}

std::string rtcJsonQuote(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }

std::string rtcPackCode(const char* type, const std::string& sdp) {
    const std::string body = std::string("{\"t\":\"") + type + "\",\"d\":\"" + jsonEscape(sdp) + "\"}";
    return "CLR" + b64encode(body);
}

bool rtcUnpackCode(const char* code, std::string* type, std::string* sdp, std::string* error) {
    while (*code == ' ' || *code == '\n' || *code == '\r' || *code == '\t') ++code;
    const bool zipped = strncmp(code, "CLZ", 3) == 0;
    if (!zipped && strncmp(code, "CLR", 3) != 0) { *error = "not a Cinderlift code"; return false; }
    std::vector<u8> raw;
    if (!b64decode(code + 3, &raw)) { *error = "that code is damaged"; return false; }
    std::vector<u8> json;
    if (zipped) { if (!gunzip(raw, json)) { *error = "that code is damaged"; return false; } }
    else json.swap(raw);
    const std::string text(json.begin(), json.end());
    if (!rtcJsonString(text, "t", type) || !rtcJsonString(text, "d", sdp)) {
        *error = "that code is damaged"; return false;
    }
    return true;
}
