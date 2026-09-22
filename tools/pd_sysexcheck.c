/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Reads real CZ dumps, writes them back, and says which bytes moved.
 *
 * A perfect score is not the goal and would be a lie: pdsynth has no vibrato
 * section and no key follow, so those bytes cannot come back. What this
 * measures is whether the parts pdsynth does model survive the trip, and it
 * prints the rest by name rather than hiding it in a total.
 */
#include "pd_sysex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *name; int off, len; int modelled; } sec_t;

static const sec_t kSec[] = {
    { "line select/octave", 0,   1, 1 },
    { "detune sign",        1,   1, 1 },
    { "detune amount",      2,   2, 1 },
    { "vibrato",            4,  10, 0 },
    { "line 1 waveform",   14,   2, 1 },
    { "line 1 key follow", 16,   4, 0 },
    { "line 1 DCA end",    20,   1, 1 },
    { "line 1 DCA env",    21,  16, 1 },
    { "line 1 DCW end",    37,   1, 1 },
    { "line 1 DCW env",    38,  16, 1 },
    { "line 1 DCO end",    54,   1, 1 },
    { "line 1 DCO env",    55,  16, 1 },
    { "line 2 waveform",   71,   2, 1 },
    { "line 2 key follow", 73,   4, 0 },
    { "line 2 DCA end",    77,   1, 1 },
    { "line 2 DCA env",    78,  16, 1 },
    { "line 2 DCW end",    94,   1, 1 },
    { "line 2 DCW env",    95,  16, 1 },
    { "line 2 DCO end",   111,   1, 1 },
    { "line 2 DCO env",   112,  16, 1 },
};
#define NSEC ((int)(sizeof kSec / sizeof kSec[0]))

int main(int argc, char **argv)
{
    int verbose = 0, first = 1;
    if (argc > 1 && strcmp(argv[1], "-v") == 0) { verbose = 1; first = 2; }
    if (argc < first + 1) { printf("usage: pd_sysexcheck [-v] file.syx ...\n"); return 2; }

    long sec_bytes[NSEC], sec_same[NSEC];
    memset(sec_bytes, 0, sizeof sec_bytes);
    memset(sec_same, 0, sizeof sec_same);
    int files = 0, unreadable = 0;

    for (int a = first; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) { printf("cannot open %s\n", argv[a]); unreadable++; continue; }
        uint8_t raw[512];
        const size_t n = fread(raw, 1, sizeof raw, f);
        fclose(f);

        uint8_t before[PD_SYSEX_VOICE];
        if (pd_sysex_unpack(raw, n, before) != 0) {
            printf("%-28s not a CZ voice dump (%zu bytes)\n", argv[a], n);
            unreadable++;
            continue;
        }

        pd_patch_t p;
        pd_sysex_report_t rr, wr;
        uint8_t basev[PD_SYSEX_VOICE];
        if (pd_sysex_read_ex(raw, n, &p, basev, &rr) != 0) { unreadable++; continue; }

        uint8_t outb[PD_SYSEX_BYTES];
        const size_t wn = pd_sysex_write_ex(&p, basev, 0, 0x60, outb, sizeof outb, &wr);
        uint8_t after[PD_SYSEX_VOICE];
        if (wn == 0 || pd_sysex_unpack(outb, wn, after) != 0) { unreadable++; continue; }

        files++;
        if (verbose) {
            printf("\n%s\n", argv[a]);
            for (int s = 0; s < NSEC; s++)
                for (int i = 0; i < kSec[s].len; i++) {
                    const int o = kSec[s].off + i;
                    if (before[o] != after[o])
                        printf("   %-20s byte %3d  %02X -> %02X\n",
                               kSec[s].name, o, before[o], after[o]);
                }
        }
        for (int s = 0; s < NSEC; s++)
            for (int i = 0; i < kSec[s].len; i++) {
                sec_bytes[s]++;
                if (before[kSec[s].off + i] == after[kSec[s].off + i]) sec_same[s]++;
            }
    }

    printf("\n%d files read, %d unreadable\n\n", files, unreadable);
    printf("%-20s %8s %8s %7s\n", "section", "bytes", "same", "");
    long mb = 0, ms = 0;
    for (int s = 0; s < NSEC; s++) {
        const double pct = sec_bytes[s] ? 100.0 * (double)sec_same[s] / (double)sec_bytes[s] : 0.0;
        printf("%-20s %8ld %8ld %6.1f%%  %s\n", kSec[s].name, sec_bytes[s], sec_same[s],
               pct, kSec[s].modelled ? "" : "(not modelled, expected)");
        if (kSec[s].modelled) { mb += sec_bytes[s]; ms += sec_same[s]; }
    }
    printf("\nmodelled bytes: %ld of %ld identical (%.2f%%)\n", ms, mb,
           mb ? 100.0 * (double)ms / (double)mb : 0.0);
    return (mb && ms == mb) ? 0 : 1;
}
