/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A filter, which the hardware never had.
 *
 * The whole argument of a CZ is that it does not need one: the DCW envelope
 * bends the phase and the sound opens and closes without anything being
 * subtracted. That argument is sound and the filter here does not weaken it,
 * because the filter is off by default and the presets do not use it.
 *
 * It is here because a bend can only ever add harmonics, and there is no way
 * to reach for a low pass sweep on top of a rich waveform, or to notch
 * something out, or to make a band pass honk. The 2026 reissue has no filter
 * either, and it was the first thing asked for.
 *
 * A state variable filter, which gives all four responses from one structure
 * and stays stable when its cutoff is swept hard, which is exactly what will
 * happen to it here.
 */
#ifndef PDSYNTH_PD_FILTER_H
#define PDSYNTH_PD_FILTER_H

typedef enum {
    PD_FILTER_OFF = 0,
    PD_FILTER_LOWPASS,
    PD_FILTER_HIGHPASS,
    PD_FILTER_BANDPASS,
    PD_FILTER_NOTCH,
    PD_FILTER_MODES
} pd_filter_mode_t;

const char *pd_filter_mode_name(pd_filter_mode_t m);

typedef struct { double ic1, ic2; } pd_filter_t;

void   pd_filter_reset(pd_filter_t *f);
/*
 * cutoff in Hz, resonance 0 to 1. The rate is the one the filter is running
 * at, which here is the oversampled one, so a sweep to the top of its range
 * does not fold.
 */
double pd_filter_step(pd_filter_t *f, double in, pd_filter_mode_t mode,
                      double cutoff_hz, double resonance, double rate);

#endif
