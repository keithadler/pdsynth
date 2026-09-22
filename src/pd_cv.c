/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <math.h>
#include <string.h>
#include "pd_cv.h"

double pd_cv_volts_for_note(int note)
{
    return (note - PD_CV_ZERO_NOTE) / 12.0;
}

void pd_cv_init(pd_cv_t *c)
{
    memset(c, 0, sizeof(*c));
    c->note = -1;
}

void pd_cv_note_on(pd_cv_t *c, int note, double velocity)
{
    c->note = note;
    c->pitch = pd_cv_volts_for_note(note) * PD_VOLT;
    c->gate = PD_VOLT * 5.0;                 /* five volts, the usual gate */
    c->velocity = velocity * PD_VOLT * 5.0;
    c->glide_hz = 0.0;
}

void pd_cv_note_off(pd_cv_t *c, int note)
{
    /* Only the note that is sounding closes the gate, so releasing an older
     * key while a newer one is held does not cut the newer one off. */
    if (c->note == note) {
        c->gate = 0.0;
        c->note = -1;
    }
}

void pd_cv_all_off(pd_cv_t *c)
{
    c->gate = 0.0;
    c->note = -1;
}

void pd_cv_track_hz(pd_cv_t *c, double hz)
{
    if (hz <= 0.0) return;
    /* the inverse of pd_note_to_hz, so a glide between notes comes out as the
     * continuous voltage it is rather than as two steps */
    const double note = 69.0 + 12.0 * log2(hz / 440.0);
    c->glide_hz = hz;
    c->pitch = (note - PD_CV_ZERO_NOTE) / 12.0 * PD_VOLT;
}
