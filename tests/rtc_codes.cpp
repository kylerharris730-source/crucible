#include <stdio.h>
#include <string.h>
#include <string>

/* Connection codes, as the Windows build reads and writes them.

   Online play works between a browser and the Windows build only if both
   understand the same codes, and a browser writes 'CLZ' -- gzip -- whenever
   it has CompressionStream. So the Windows build carries its own inflater
   (src/rtc/codes.cpp), and a mistake in it would show up as "the code my
   friend sent me is damaged" -- only for desktop players, only against
   browser hosts, and never on the developer's machine.

   The fixtures are real gzip streams, made by Python's gzip module, covering
   all three DEFLATE block types: dynamic Huffman (an SDP at level 9), stored
   (level 0) and fixed Huffman (level 1). The expected lengths and CRC-32s are
   of the SDP inside.

   codes.cpp lives in src/rtc/, which the test runner does not compile -- the
   rest of that folder needs libdatachannel -- so it is included directly.
   That also keeps it building with the old GCC the tests use. */
#include "../src/rtc/codes.cpp"

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

static unsigned crc32(const std::string& s) {
    unsigned c = 0xFFFFFFFFu;
    for (size_t i = 0; i < s.size(); ++i) {
        c ^= (unsigned char)s[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

struct Fixture { const char* what; const char* code; const char* type; size_t len; unsigned crc; };

static const Fixture FIXTURES[] = {
    { "dynamic Huffman (an SDP, level 9)",
      "CLZH4sIAAAAAAAC/53UTW+bMBgH8K9icR6pH9vY+JE4NJBKlboqUlvt0osLToLWAAJn2Yv23QdOqjGfJuAAth//bMn6+1fkIiRRu9vZPvpEompqfcvoa//atFlMhARQHASlglOaAOeSMHL/SO63ggBTKzq+MFUPWTx9XEaJn22yfd+eOly/PBYPm4++41BX8WCPpnF1ieTL56ep/5iZrnuvS+PqtiGavBTbm+L54enmKX/ekrN9610ZV8aZ8mCaxr5Pc8rsugvq93D1S9NU9VhpkRIgp6ojDBhjkjLGCWi2ApmuYEVJQseHuB8dObSDI3vb2P6yPCWNdee2/xrXFYGQhZBlMxY8CwtYFrIwY5ln2QKWhyydsdyzfAErAhb0jBWeFQvYJGTTGZt4NlnAypBVM1Z6Vi5gVcjKGas8qxawacgmMzb1bLqA1SErZqz2rF4ShzBmMI8Z+JzBopyFQYN/guaTBv+ftLq08WnXmz2at7L629edK6TAuEikSvU0Zncf7UvVrm72tu/6unE4HEzMEkluExQF5rcIKbIEOcX1GqFAWSBwZDkWGywkMo5qjWz81wgb5HeoGMId5mOxRgUICoVALaZioZHnqAtM8svCg3Xj9WlK15lhuN6e9bjb6+jYH3dt73A6uuuw+R4f7TCYvY2H+ud4q8jxnMU0GP3+A/ZqrG7qBQAA",
      "offer", 1437, 0xd2b4dba8u },
    { "stored block (level 0)",
      "CLZH4sIAAAAAAAE/wEcAOP/eyJ0IjogIm9mZmVyIiwgImQiOiAieHh4eHgifYvuuNYcAAAA",
      "offer", 5, 0x42d1e778u },
    { "fixed Huffman (level 1)",
      "CLZH4sIAAAAAAAE/6tWKlGyUlDKT0tLLVLSUVBKAfEqQECpFgCL7rjWHAAAAA==",
      "offer", 5, 0x42d1e778u },
};

int main() {
    for (size_t i = 0; i < sizeof(FIXTURES) / sizeof(FIXTURES[0]); ++i) {
        const Fixture& f = FIXTURES[i];
        std::string type, sdp, error;
        const bool ok = rtcUnpackCode(f.code, &type, &sdp, &error);
        char msg[160];
        snprintf(msg, sizeof(msg), "%s: unpacks (%s)", f.what, error.c_str());
        check(ok, msg);
        if (!ok) continue;
        snprintf(msg, sizeof(msg), "%s: type is %s", f.what, f.type);
        check(type == f.type, msg);
        snprintf(msg, sizeof(msg), "%s: %u bytes with the right CRC", f.what, (unsigned)f.len);
        check(sdp.size() == f.len && crc32(sdp) == f.crc, msg);

        /* What we write must read back as what we read. */
        std::string type2, sdp2, error2;
        snprintf(msg, sizeof(msg), "%s: round trip through rtcPackCode", f.what);
        check(rtcUnpackCode(rtcPackCode(type.c_str(), sdp).c_str(), &type2, &sdp2, &error2) &&
              type2 == type && sdp2 == sdp, msg);
    }

    /* SDP is full of CR LF, which JSON has to escape; a code that went
       through a chat client may come back wrapped in whitespace. */
    {
        const std::string sdp = "v=0\r\na=\"quoted\" back\\slash\ttab\r\n";
        std::string packed = rtcPackCode("answer", sdp);
        packed.insert(20, "\r\n  ");
        std::string type, out, error;
        check(rtcUnpackCode(packed.c_str(), &type, &out, &error) && type == "answer" && out == sdp,
              "escapes and wrapped whitespace survive a round trip");
    }

    /* Rejected rather than read as garbage. */
    {
        std::string type, sdp, error;
        check(!rtcUnpackCode("192.168.1.5", &type, &sdp, &error), "an address is not a code");
        check(!rtcUnpackCode("CLZAAAA", &type, &sdp, &error), "a truncated gzip code is rejected");
        check(!rtcUnpackCode("CLR!!!!", &type, &sdp, &error), "bad base64 is rejected");
    }

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("rtc_codes: all checks passed\n");
    return 0;
}
