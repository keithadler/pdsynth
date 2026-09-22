/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The voice: two lines, and what can be done between them.
 *
 * A CZ voice is two independent lines, each an oscillator with three eight
 * step envelopes on its pitch, its waveform and its level. Two lines detuned
 * against each other is most of what the machine is known for. The originals
 * could also ring modulate one line by the other, and modulate with noise, and
 * both are here because both are worth having and the 2026 reissue dropped
 * them.
 */
#ifndef PDSYNTH_PD_VOICE_H
#define PDSYNTH_PD_VOICE_H

#include "pd_osc.h"
#include "pd_env.h"

typedef struct {
    pd_wave_t       wave;
    int             octave;         /* -2 to +2 */
    int             semitones;      /* -12 to +12 */
    double          detune_cents;
    double          level;          /* 0 to 1, the line's own trim */
    pd_env_params_t pitch_env;      /* DCO */
    double          pitch_env_depth_semitones;
    pd_env_params_t wave_env;       /* DCW: how far the phase is bent */
    pd_env_params_t amp_env;        /* DCA */
} pd_line_params_t;

typedef enum {
    PD_MIX_BOTH = 0,    /* the two lines side by side */
    PD_MIX_RING,        /* line 1 ring modulated by line 2 */
    PD_MIX_NOISE        /* line 1 modulated by noise */
} pd_mix_t;

typedef struct {
    pd_line_params_t line[2];
    int              line_count;    /* 1 or 2 */
    pd_mix_t         mix;
    double           noise_amount;  /* for PD_MIX_NOISE, 0 to 1 */
    double           velocity_to_wave;   /* how much playing harder opens it */
    double           velocity_to_level;
} pd_patch_t;

typedef struct {
    pd_osc_t osc;
    pd_env_t pitch, wave, amp;
} pd_line_t;

typedef struct {
    const pd_patch_t *patch;
    pd_line_t   line[2];
    double      sample_rate;
    double      base_hz;
    int         note;
    double      velocity;    /* 0 to 1 */
    int         active;
    uint32_t    noise_state;
} pd_voice_t;

void   pd_patch_init(pd_patch_t *p);
void   pd_voice_init(pd_voice_t *v, const pd_patch_t *patch, double sample_rate);
void   pd_voice_note_on(pd_voice_t *v, int midi_note, double velocity);
void   pd_voice_note_off(pd_voice_t *v);
double pd_voice_next(pd_voice_t *v);
int    pd_voice_active(const pd_voice_t *v);

double pd_note_to_hz(int midi_note);

#endif
