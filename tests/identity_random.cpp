#include "identity.h"
#include <stdio.h>
#include <string.h>
#include <set>
#include <string>

/* Identities come from the system's cryptographic generator.
 *
 * A host gives a remembered pack to whoever presents that player's identity,
 * so an identity that can be predicted can be claimed. The old generator mixed
 * the clock, the process id and a stack address: two made in the same
 * millisecond by the same process were the same string, and one made at a
 * known moment was a small search away. */

int main() {
    int failures = 0;
    std::set<std::string> seen;
    int ones = 0, bits = 0;
    const int N = 2000;
    for (int i = 0; i < N; ++i) {
        char id[PLAYER_IDENTITY_CHARS + 1];
        memset(id, 'x', sizeof(id));
        identityGenerate(id);
        bool hex = id[PLAYER_IDENTITY_CHARS] == 0 && (int)strlen(id) == PLAYER_IDENTITY_CHARS;
        for (int k = 0; hex && k < PLAYER_IDENTITY_CHARS; ++k) {
            const char c = id[k];
            const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (v < 0) { hex = false; break; }
            for (int b = 0; b < 4; ++b) ones += (v >> b) & 1;
            bits += 4;
        }
        if (!hex) { fprintf(stderr, "FAIL: identity %d is not 32 lowercase hex: %.33s\n", i, id); ++failures; break; }
        seen.insert(id);
    }
    /* Made back to back into the same buffer, which is exactly what the old
       generator could not tell apart. */
    if ((int)seen.size() != N) {
        fprintf(stderr, "FAIL: %d of %d identities made in a row were repeats\n", N - (int)seen.size(), N);
        ++failures;
    }
    /* 128000 bits; a fair source lands within a fraction of a percent of half. */
    const double share = bits ? (double)ones / bits : 0.0;
    if (share < 0.49 || share > 0.51) {
        fprintf(stderr, "FAIL: %.4f of identity bits are set, expected about 0.5\n", share);
        ++failures;
    }
    if (failures) return 1;
    printf("%d identities, all distinct, %.4f of bits set\n", N, share);
    return 0;
}
