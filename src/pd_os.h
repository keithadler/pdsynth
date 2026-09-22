/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Oversampling, because phase distortion aliases badly and sounds sour when it
 * does.
 *
 * Bending the phase of a sine puts energy far above the fundamental: that is
 * the whole point of the technique, and it is also the problem. Computed one
 * sample at a time at the output rate, everything above Nyquist folds back
 * onto frequencies that are not multiples of the note, and the ear hears that
 * as sourness rather than brightness. Measured before this existed, a C5 had
 * between 78 and 99 percent of its energy off harmonic, depending on waveform.
 *
 * The fix is ordinary: run the oscillator several times faster than the output,
 * where there is room for those harmonics, then filter and throw the extra
 * samples away. What matters is that the filter is actually steep: a gentle one
 * lets through exactly the band that folds, and then the oversampling has
 * bought nothing.
 */
#ifndef PDSYNTH_PD_OS_H
#define PDSYNTH_PD_OS_H

/* Four is where the improvement stops paying for the arithmetic, given a
 * filter steep enough to make use of it. */
#define PD_OVERSAMPLE   4
#define PD_DECIM_TAPS  95

typedef struct {
    double h[PD_DECIM_TAPS];
    double z[PD_DECIM_TAPS];
    int    pos;
} pd_decimator_t;

void   pd_decimator_init(pd_decimator_t *d, double output_rate);
/* Every inner sample goes in; a sample is read out once per PD_OVERSAMPLE. */
void   pd_decimator_push(pd_decimator_t *d, double x);
double pd_decimator_read(const pd_decimator_t *d);

#endif
