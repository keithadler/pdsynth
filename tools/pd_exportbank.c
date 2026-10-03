/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Writes the factory bank out as Casio voice dump files, for a flash drive, a
 * MIDI utility, or a CZ.
 *
 *     pd_exportbank <directory> [channel 1-16]
 *
 * Asked for by Reaper10: "can you take the original presets for the
 * synthesizer and make them a bank file also", to sit beside the Casio CZ
 * preset banks.
 *
 * A CZ-101 holds sixteen internal voices, program 0x20 to 0x2F, so the bank is
 * written sixteen at a time. 27 presets is two files, and a bank that does not
 * divide evenly is not padded: padding a CZ's memory with copies of voice 1
 * would overwrite sounds somebody had not asked to lose.
 *
 * Not every preset can be honest on the hardware, and this says so rather than
 * letting a file look more faithful than it is. A CZ has two lines, no filter,
 * no grains and no effects. pdsynth has four lines, a filter, a grain cloud and
 * a rack of effects, so a preset that leans on those arrives as the part of it
 * a CZ can hold. The manifest names each one and counts what was left behind.
 *
 * Nothing is written without being read back first. The output is parsed again
 * with the same code a player's own dumps go through, the program numbers are
 * checked against the memory map, and writing what was read back has to give
 * the same bytes. A file that fails any of that is removed rather than left
 * behind, because a half-trusted bank on a flash drive is worse than none.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pd_presets.h"
#include "pd_sysex.h"

#define CZ_BANK       16      /* voices in one CZ memory area */
#define CZ_INTERNAL   0x20    /* program number of internal voice 1 */

/*
 * Losses that are about how a CZ is played rather than about what the voice
 * is. A CZ-101 has no velocity, no stereo field and no mod wheel, so every
 * pdsynth preset loses all three, and saying so on every line would make the
 * flag mean nothing: the first version marked 26 of 26 voices approximate.
 * They are stated once at the top instead.
 *
 * Anything not on this list counts as part of the voice, and that is the safe
 * default: a loss the librarian invents next year shows up as "approximate"
 * until somebody decides it belongs here, rather than hiding.
 */
static int is_performance_feel(const char *control)
{
    static const char *const feel[] = { "Velocity", "Stereo spread", "Mod wheel", "Pressure" };
    for (size_t i = 0; i < sizeof feel / sizeof feel[0]; i++)
        if (strcmp(control, feel[i]) == 0) return 1;
    return 0;
}

static int fail(const char *what, const char *file)
{
    fprintf(stderr, "pd_exportbank: %s (%s)\n", what, file);
    if (file && *file) remove(file);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: pd_exportbank <directory> [channel 1-16]\n");
        return 2;
    }
    const char *dir = argv[1];
    int channel = 0;
    if (argc > 2) {
        channel = atoi(argv[2]) - 1;
        if (channel < 0 || channel > 15) {
            fprintf(stderr, "pd_exportbank: channel must be 1 to 16\n");
            return 2;
        }
    }

    const int total = pd_preset_count();
    const int banks = (total + CZ_BANK - 1) / CZ_BANK;

    char manifest_path[1024];
    snprintf(manifest_path, sizeof manifest_path, "%s/pdsynth-factory-slots.txt", dir);
    FILE *man = fopen(manifest_path, "w");
    if (!man) {
        fprintf(stderr, "pd_exportbank: cannot write in %s\n", dir);
        return 1;
    }
    fprintf(man, "pdsynth factory bank, %d voices in %d file%s\n", total, banks,
            banks == 1 ? "" : "s");
    fprintf(man, "Each file is one CZ internal memory area: send it and it fills\n"
                 "Internal 1 to 16. MIDI channel %d.\n\n", channel + 1);
    fprintf(man, "A CZ has no velocity, no stereo field and no mod wheel, so every\n"
                 "voice plays more plainly on the hardware than it does in pdsynth.\n"
                 "That is true of all of them and is not repeated below.\n"
                 "\"Approximate\" means something that changes the voice itself is\n"
                 "missing from the CZ's side of it.\n\n");

    int status = 0;
    int approx_total = 0;

    for (int b = 0; b < banks; b++) {
        const int first = b * CZ_BANK;
        const int count = (total - first) < CZ_BANK ? (total - first) : CZ_BANK;

        pd_patch_t patches[CZ_BANK];
        for (int i = 0; i < count; i++) patches[i] = pd_preset(first + i)->patch;

        static uint8_t buf[CZ_BANK * PD_SYSEX_BYTES + 64];
        pd_sysex_report_t rep;
        pd_sysex_report_init(&rep);
        const size_t n = pd_sysex_write_bank(patches, NULL, count, channel,
                                             CZ_INTERNAL, buf, sizeof buf, &rep);

        char path[1024];
        snprintf(path, sizeof path, "%s/pdsynth-factory-%dof%d.syx", dir, b + 1, banks);

        if (n == 0) { status |= fail("the bank would not fit its buffer", ""); continue; }

        /* ---- read it back before it is allowed to exist ---- */
        const int found = pd_sysex_bank_count(buf, n);
        if (found != count) {
            fprintf(stderr, "pd_exportbank: wrote %d voices and read back %d\n", count, found);
            status |= 1;
            continue;
        }

        int bad = 0;
        for (int i = 0; i < count; i++) {
            const uint8_t *msg = NULL;
            size_t mlen = 0;
            if (pd_sysex_bank_at(buf, n, i, &msg, &mlen) != 0) { bad = 1; break; }

            /* program byte: header is F0 44 00 00 7c 20 pp, so byte 6. The
             * memory map is checked as an absolute number rather than derived
             * from the same constant the writer used. */
            if (mlen < 7 || msg[6] != (uint8_t)(0x20 + i)) {
                fprintf(stderr, "pd_exportbank: voice %d went to program 0x%02x, wanted 0x%02x\n",
                        first + i + 1, mlen < 7 ? 0xff : msg[6], 0x20 + i);
                bad = 1;
                break;
            }

            pd_patch_t back;
            pd_sysex_report_t rr;
            if (pd_sysex_read(msg, mlen, &back, &rr) != 0) { bad = 1; break; }

            /* what was read back, written again, is the same bytes */
            uint8_t again[PD_SYSEX_BYTES + 8];
            pd_sysex_report_t r2;
            pd_sysex_report_init(&r2);
            const size_t m2 = pd_sysex_write(&back, channel, 0x20 + i,
                                             again, sizeof again, &r2);
            if (m2 != mlen || memcmp(again, msg, mlen) != 0) {
                fprintf(stderr, "pd_exportbank: voice %d does not survive a round trip\n",
                        first + i + 1);
                bad = 1;
                break;
            }
        }
        if (bad) { status |= 1; continue; }

        FILE *f = fopen(path, "wb");
        if (!f || fwrite(buf, 1, n, f) != n) {
            if (f) fclose(f);
            status |= fail("could not write the file", path);
            continue;
        }
        fclose(f);

        /* ---- the manifest, with an honest line for each voice ---- */
        fprintf(man, "pdsynth-factory-%dof%d.syx\n", b + 1, banks);
        for (int i = 0; i < count; i++) {
            const pd_preset_t *pr = pd_preset(first + i);
            uint8_t one[PD_SYSEX_BYTES + 8];
            pd_sysex_report_t r1;
            pd_sysex_report_init(&r1);
            pd_sysex_write(&pr->patch, channel, CZ_INTERNAL + i, one, sizeof one, &r1);
            int lost = r1.dropped;     /* ones with no room to be named count */
            for (int k = 0; k < r1.count; k++)
                if (!is_performance_feel(r1.note[k].control)) lost++;
            if (lost) approx_total++;
            fprintf(man, "  Internal %2d  %-16s %s\n", i + 1, pr->name,
                    lost == 0 ? "" : "(approximate on a real CZ)");
            for (int k = 0; k < r1.count; k++)
                if (!is_performance_feel(r1.note[k].control))
                    fprintf(man, "                  - %s\n", r1.note[k].control);
        }
        fprintf(man, "\n");
        printf("wrote %s  (%d voices, %zu bytes)\n", path, count, n);
    }

    fprintf(man, "%d of %d voices lean on something that changes the voice itself.\n",
            approx_total, total);
    fclose(man);
    printf("wrote %s\n", manifest_path);
    printf("%d of %d voices are approximate on a real CZ\n", approx_total, total);
    return status;
}
