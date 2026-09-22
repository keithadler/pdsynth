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
#include "pd_os.h"
#include "pd_filter.h"

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

/*
 * The hardware stacks two. There is no reason software should: the limit on a
 * CZ was the cost of the chips, and four lines is the same code run twice more.
 * They are independent rather than paired, because a pair is four lines with
 * two of the detunes set the same, and the reverse is not true.
 */
#define PD_MAX_LINES 4

typedef struct {
    pd_line_params_t line[PD_MAX_LINES];
    int              line_count;    /* 1 to PD_MAX_LINES */
    pd_mix_t         mix;
    double           noise_amount;  /* for PD_MIX_NOISE, 0 to 1 */
    double           velocity_to_wave;   /* how much playing harder opens it */
    double           velocity_to_level;
    double           bend_range_semitones;  /* what a full wheel is worth */
    double           mod_to_wave;           /* the mod wheel opening the waveform */
    double           spread;                /* 0 mono, 1 lines hard apart */
    double           glide_seconds;         /* 0 for none: time to cross an octave */
    double           aftertouch_to_wave;    /* pressure opening the waveform */
    double           aftertouch_to_level;

    /* The filter the hardware never had. Off unless a patch asks for it. */
    pd_filter_mode_t filter_mode;
    double           filter_cutoff_hz;
    double           filter_resonance;
    double           filter_env_depth;   /* the DCW envelope of line 1, applied
                                            to cutoff, in octaves */
    double           filter_key_track;   /* 0 fixed, 1 follows the note */
} pd_patch_t;

typedef struct {
    pd_osc_t osc;
    pd_env_t pitch, wave, amp;
} pd_line_t;

typedef struct {
    const pd_patch_t *patch;
    pd_line_t   line[PD_MAX_LINES];
    double      sample_rate;
    double      base_hz;
    int         note;
    double      velocity;    /* 0 to 1 */
    int         active;
    uint32_t    noise_state;
    double      bend;        /* -1 to 1, the wheel */
    double      mod;         /*  0 to 1 */
    double      pressure;    /*  0 to 1, aftertouch */

    /* Glide: the sounding pitch chases the played one rather than jumping. */
    double      glide_hz, glide_target_hz, glide_rate;

    /* Everything inside the voice runs at PD_OVERSAMPLE times the rate it is
     * asked for, and pd_voice_next filters and returns one sample in four. */
    pd_decimator_t decim;
    double      inner_rate;

    /* A bent sine is not symmetric about zero, so most of these waveforms
     * carry a steady offset. One voice of it is inaudible; sixteen of them
     * stacked is wasted headroom and a thump on every note. */
    double      dc_x1, dc_y1, dc_r;
    pd_filter_t filt_l, filt_r;
} pd_voice_t;

void   pd_patch_init(pd_patch_t *p);
void   pd_voice_init(pd_voice_t *v, const pd_patch_t *patch, double sample_rate);
void   pd_voice_note_on(pd_voice_t *v, int midi_note, double velocity);
void   pd_voice_note_off(pd_voice_t *v);
double pd_voice_next(pd_voice_t *v);

/*
 * One sample at the oversampled rate, the two lines placed across the stereo
 * field. The filtering and the decimating belong to whatever is mixing the
 * voices, not to each voice: decimation is linear, so filtering the sum once
 * gives the same answer as filtering sixteen voices separately and costs a
 * sixteenth as much.
 */
void   pd_voice_next_inner(pd_voice_t *v, double *left, double *right);

/* The wheels. Safe to call while a note is sounding, which is the point. */
void   pd_voice_set_bend(pd_voice_t *v, double minus_one_to_one);
void   pd_voice_set_mod(pd_voice_t *v, double zero_to_one);
void   pd_voice_set_pressure(pd_voice_t *v, double zero_to_one);
int    pd_voice_active(const pd_voice_t *v);

double pd_note_to_hz(int midi_note);

#endif
