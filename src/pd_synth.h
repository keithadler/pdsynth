/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The polyphonic instrument: voices, note allocation, and the one place the
 * oversampled stream is filtered back down.
 *
 * Decimation is linear, so filtering the sum of the voices once gives exactly
 * what filtering each voice separately would give, at a sixteenth of the cost
 * on a sixteen voice instrument. That is why the filter lives here rather than
 * inside pd_voice.
 */
#ifndef PDSYNTH_PD_SYNTH_H
#define PDSYNTH_PD_SYNTH_H

#include "pd_voice.h"
#include "pd_os.h"

#define PD_MAX_VOICES 16

typedef struct {
    pd_voice_t     voice[PD_MAX_VOICES];
    int            voice_count;
    pd_decimator_t decim_l, decim_r;
    double         sample_rate;
    const pd_patch_t *patch;
    double         bend, mod;      /* where the wheels are */
    unsigned long  stamp;          /* for stealing the oldest voice */
    unsigned long  started[PD_MAX_VOICES];
} pd_synth_t;

void pd_synth_init(pd_synth_t *s, const pd_patch_t *patch, double sample_rate, int voices);
void pd_synth_note_on(pd_synth_t *s, int note, double velocity);
void pd_synth_note_off(pd_synth_t *s, int note);
void pd_synth_all_off(pd_synth_t *s);
void pd_synth_set_bend(pd_synth_t *s, double minus_one_to_one);
void pd_synth_set_mod(pd_synth_t *s, double zero_to_one);
void pd_synth_render(pd_synth_t *s, double *left, double *right);
int  pd_synth_active(const pd_synth_t *s);

#endif
