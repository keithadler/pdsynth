/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The grain scheduler and its window shapes are a port of the asynchronous
 * granular oscillator in ZedSynth (formerly WhySynth):
 *
 *   Copyright (C) 2005 Sean Bolton and others.
 *   Portions of that file come from MSS, copyright (C) 2002 Mats Olsson.
 *
 * ZedSynth is GPL-2.0-or-later and so is pdsynth, and Sean Bolton gave his
 * blessing for this. The five window shapes, the overlap parameter that sets
 * grain density by dividing the grain length, and the idea of starting each
 * grain at the phase a continuous oscillator would have reached, are all
 * theirs. What is different here is written down at the top of pd_grain.c.
 *
 * A cloud, not a chord.
 *
 * Granular synthesis cuts a sound into fragments a few milliseconds long,
 * windows each one so it starts and ends at silence, and scatters them. Play
 * enough of them and the ear stops hearing fragments and hears texture. Fewer
 * and slower and it hears the fragments, which is its own sound and the reason
 * to expose the controls rather than choose for the player.
 *
 * What is granulated here is not a sample. It is the same phase distorted sine
 * the rest of pdsynth makes, so a grain carries whatever bend its line's DCW
 * envelope has reached. A CZ could not do this and was never going to: the
 * hardware had one phase accumulator per line, and this wants twenty at once.
 */
#ifndef PDSYNTH_PD_GRAIN_H
#define PDSYNTH_PD_GRAIN_H

#include <stdint.h>
#include "pd_osc.h"

/*
 * The five windows, in the order ZedSynth lists them.
 *
 * RECT is no window at all, which clicks at both ends, and it is here because
 * the click is sometimes the point. TRAPEZOID rises, holds and falls.
 * TRIANGLE rises to a peak and falls, so `edge` moves the peak. GAUSSIAN is a
 * bell with no flat part. ROADSIAN is a flat top with Gaussian shoulders,
 * named for Curtis Roads, and is the one that sounds like nothing is
 * happening, which for a window is the compliment.
 */
typedef enum {
    PD_GRAIN_RECT = 0,
    PD_GRAIN_TRAPEZOID,
    PD_GRAIN_TRIANGLE,
    PD_GRAIN_GAUSSIAN,
    PD_GRAIN_ROADSIAN,
    PD_GRAIN_SHAPE_COUNT
} pd_grain_shape_t;

const char *pd_grain_shape_name(pd_grain_shape_t s);

/* How far a grain may be detuned at full pitch spread. Two octaves is enough
 * to turn a note into weather and still land on something playable. */
#define PD_GRAIN_MAX_CENTS 1200.0

/* One cloud's worth. Twenty overlapping grains is already thick; the rest is
 * headroom for short grains under heavy overlap. Fixed, because allocating on
 * the audio thread is not allowed and starving one voice because another is
 * busy is worse than a ceiling. */
#define PD_MAX_GRAINS 64

typedef struct {
    int              on;              /* off is the plain oscillator, bit for bit */
    pd_grain_shape_t shape;
    double           edge;            /* 0 to 1, what the shape does with its ends */
    double           length_ms;       /* one grain, 1 to 500 */
    double           overlap;         /* how many sound at once, 0.1 to 20 */
    double           onset_spread;    /* 0 to 1, jitter in when they start */
    double           pitch_spread;    /* 0 to 1, per grain detune */
} pd_grain_params_t;

void pd_grain_params_init(pd_grain_params_t *g);

typedef struct {
    int    used;
    double env_pos;      /* samples into the window */
    double phase;        /* its own place in the waveform */
    double inc_ratio;    /* its own rate, as a multiple of the line's */
} pd_grain_t;

typedef struct {
    pd_grain_t grain[PD_MAX_GRAINS];
    double     vphase;       /* the oscillator the grains are cut from */
    double     next_onset;   /* samples until the next grain starts */
    uint32_t   rng;
    int        live;         /* how many are sounding right now */
    long       starved;      /* grains wanted and not available, for tests */

    /* The level compensation, and what it was worked out from. Recomputed
     * only when one of those moves, which is never on most samples. */
    double     norm;
    double     n_shape, n_edge, n_overlap, n_pitch_spread;
} pd_cloud_t;

void pd_cloud_init(pd_cloud_t *c, uint32_t seed);

/*
 * One sample at whatever rate the caller is running. `increment` is the
 * line's cycles per sample, `bend` its distortion depth, and both are read
 * fresh every sample so a cloud follows its envelopes like anything else.
 */
double pd_cloud_next(pd_cloud_t *c, const pd_grain_params_t *g,
                     pd_wave_t wave, double bend,
                     double increment, double sample_rate);

/* The window on its own, `t` from 0 at the grain's start to 1 at its end.
 * Exposed because a window that is not 0 at both ends clicks, and that is
 * worth being able to assert. */
double pd_grain_window(pd_grain_shape_t shape, double edge, double t);

int pd_cloud_live(const pd_cloud_t *c);

#endif
