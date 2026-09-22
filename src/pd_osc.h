/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The oscillator.
 *
 * Phase distortion is not FM and it is not a filter, though it was sold in
 * 1984 against machines that had both. A CZ reads one sine table, and bends
 * the phase on the way in. The phase of a plain oscillator climbs from 0 to 1
 * at a constant rate; bend that climb so it rushes through part of the cycle
 * and crawls through the rest, and the sine you read out acquires harmonics.
 * How far it is bent is one number, and on the hardware that number comes from
 * an eight step envelope, which is why a CZ sweeps the way it does without
 * owning a filter at all.
 *
 * Every waveform here is that same sine table under a different bend. The
 * resonant three are the exception that proves it: they sweep a sine at a
 * multiple of the fundamental and multiply it by a window that falls across
 * the cycle, which is where the formant comes from.
 */
#ifndef PDSYNTH_PD_OSC_H
#define PDSYNTH_PD_OSC_H

#include <stdint.h>

/* The eight waveforms of a CZ-101, in the order its panel lists them. */
typedef enum {
    PD_SAW = 0,
    PD_SQUARE,
    PD_PULSE,
    PD_DOUBLE_SINE,
    PD_SAW_PULSE,
    PD_RESO_SAW,
    PD_RESO_TRIANGLE,
    PD_RESO_TRAPEZOID,
    PD_WAVE_COUNT
} pd_wave_t;

const char *pd_wave_name(pd_wave_t w);

typedef struct {
    double phase;       /* 0 to 1, the undistorted ramp */
    double increment;   /* cycles per sample */
} pd_osc_t;

/*
 * How much bend a note can carry before it starts folding.
 *
 * Bending the phase generates bandwidth, and how much depends on how far it is
 * bent. Low down there is room for all of it. Near the top of the keyboard
 * there is not, and what does not fit does not disappear: it folds back onto
 * frequencies that are not multiples of the note, which the ear hears as
 * sourness. Oversampling buys headroom but cannot buy an unlimited amount, so
 * above a point the bend itself has to give.
 *
 * This is not a compromise forced by software. A CZ does the same thing, for
 * the same reason, and so does every DX7 patch that scales its modulators down
 * as it climbs: the top of the keyboard gets less of the effect, and it is the
 * difference between bright and shrill.
 */
double pd_bend_ceiling(double hz, double sample_rate);

void   pd_osc_init(pd_osc_t *o);
void   pd_osc_set_freq(pd_osc_t *o, double hz, double sample_rate);

/*
 * One sample. `amount` is the distortion depth, 0 to 1: at 0 every waveform is
 * a pure sine, which is what a CZ does when its DCW envelope is at zero, and
 * at 1 it is as bent as the hardware goes.
 */
double pd_osc_next(pd_osc_t *o, pd_wave_t wave, double amount);

/* The phase mapping on its own, for testing and for anyone curious. */
double pd_distort(double phase, pd_wave_t wave, double amount);

/*
 * The resonant waveforms do not bend phase at all. They are a sine at a whole
 * multiple of the note, multiplied by a window that falls across the cycle.
 * These two expose that window and that multiple, so a panel can show what is
 * actually shaping the sound instead of drawing an undistorted phase ramp and
 * implying nothing is happening.
 */
double pd_window(double phase, pd_wave_t wave);
int    pd_resonant_harmonic(double amount);

#endif
