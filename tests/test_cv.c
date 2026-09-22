/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Control voltage, checked as voltage. An octave has to be a volt, or the rig
 * on the other end plays the wrong notes, and that is not something anyone
 * discovers gently.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "pd_cv.h"

static int passed, failed;
static void ok(int cond, const char *fmt, ...)
{
    if (cond) { passed++; return; }
    failed++;
    va_list ap; va_start(ap, fmt);
    printf("  FAIL: "); vprintf(fmt, ap); printf("\n");
    va_end(ap);
}

int main(void)
{
    pd_cv_t c;

    printf("-- an octave is a volt --\n");
    {
        for (int n = PD_CV_ZERO_NOTE; n <= PD_CV_ZERO_NOTE + 84; n += 12) {
            const double want = (n - PD_CV_ZERO_NOTE) / 12.0;
            ok(fabs(pd_cv_volts_for_note(n) - want) < 1e-9,
               "note %d should be %.1f V (got %.4f)", n, want, pd_cv_volts_for_note(n));
        }
        /* and the step between adjacent octaves is exactly one, everywhere */
        for (int n = PD_CV_ZERO_NOTE; n <= PD_CV_ZERO_NOTE + 72; n += 12) {
            const double d = pd_cv_volts_for_note(n + 12) - pd_cv_volts_for_note(n);
            ok(fabs(d - 1.0) < 1e-9, "the octave above note %d should be 1.0 V (got %.6f)", n, d);
        }
        printf("   eight octaves, every one exactly a volt apart\n");
    }

    printf("-- a semitone is a twelfth of a volt --\n");
    {
        for (int n = 36; n < 48; n++) {
            const double d = pd_cv_volts_for_note(n + 1) - pd_cv_volts_for_note(n);
            ok(fabs(d - 1.0 / 12.0) < 1e-9, "semitone at %d should be %.6f V (got %.6f)",
               n, 1.0 / 12.0, d);
        }
        printf("   twelve semitones, each one twelfth\n");
    }

    printf("-- the pitch channel carries the right sample --\n");
    {
        pd_cv_init(&c);
        pd_cv_note_on(&c, PD_CV_ZERO_NOTE, 1.0);
        ok(fabs(c.pitch - 0.0) < 1e-9, "the zero note should sit at zero volts (%.4f)", c.pitch);
        pd_cv_note_on(&c, PD_CV_ZERO_NOTE + 60, 1.0);
        ok(fabs(c.pitch - 5.0 * PD_VOLT) < 1e-9,
           "five octaves up should be five volts (%.4f, wanted %.4f)", c.pitch, 5.0 * PD_VOLT);
        ok(c.pitch < 1.0, "and must stay inside what an audio channel can carry (%.4f)", c.pitch);
        printf("   zero note at %.2f, five octaves up at %.2f of full scale\n",
               0.0, 5.0 * PD_VOLT);
    }

    printf("-- the gate opens and shuts --\n");
    {
        pd_cv_init(&c);
        ok(c.gate == 0.0, "a gate starts shut");
        pd_cv_note_on(&c, 60, 1.0);
        ok(c.gate > 0.0, "and opens on a note");
        pd_cv_note_off(&c, 60);
        ok(c.gate == 0.0, "and shuts when that note is released");
    }

    printf("-- releasing an older key does not cut the newer one --\n");
    {
        pd_cv_init(&c);
        pd_cv_note_on(&c, 48, 1.0);
        pd_cv_note_on(&c, 60, 1.0);      /* a second key, while the first is held */
        pd_cv_note_off(&c, 48);          /* let go of the first */
        ok(c.gate > 0.0, "the gate should still be open for the key still held");
        ok(fabs(c.pitch - pd_cv_volts_for_note(60) * PD_VOLT) < 1e-9,
           "and the pitch should be the newer note (%.4f)", c.pitch);
        pd_cv_note_off(&c, 60);
        ok(c.gate == 0.0, "and shut once that one goes too");
        printf("   legato does not drop the gate\n");
    }

    printf("-- velocity comes out as a voltage too --\n");
    {
        pd_cv_init(&c);
        pd_cv_note_on(&c, 60, 0.0);
        const double soft = c.velocity;
        pd_cv_note_on(&c, 60, 1.0);
        const double hard = c.velocity;
        ok(hard > soft, "harder should be higher (%.4f -> %.4f)", soft, hard);
        ok(hard < 1.0, "and stay inside full scale (%.4f)", hard);
    }

    printf("-- a glide comes out of the pitch channel, not just the synth --\n");
    {
        pd_cv_init(&c);
        pd_cv_note_on(&c, 45, 1.0);
        const double start = c.pitch;
        /* a quarter of the way from A2 to A3 */
        const double hz = 110.0 * pow(2.0, 0.25);
        pd_cv_track_hz(&c, hz);
        const double part = c.pitch;
        pd_cv_track_hz(&c, 220.0);
        const double end = c.pitch;
        ok(part > start && part < end, "the voltage should be between the two notes (%.4f)", part);
        ok(fabs((end - start) - PD_VOLT) < 1e-6,
           "and an octave of glide should be exactly one volt (%.6f)", (end - start) / PD_VOLT);
        printf("   glide reads %.4f, %.4f, %.4f across an octave\n", start, part, end);
    }

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
