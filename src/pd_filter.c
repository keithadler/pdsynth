/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <math.h>
#include "pd_filter.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *kNames[PD_FILTER_MODES] = {
    "off", "low pass", "high pass", "band pass", "notch"
};
const char *pd_filter_mode_name(pd_filter_mode_t m)
{
    return (m >= 0 && m < PD_FILTER_MODES) ? kNames[m] : "?";
}

void pd_filter_reset(pd_filter_t *f) { f->ic1 = f->ic2 = 0.0; }

/*
 * Andrew Simper's topology preserving state variable filter. The reason for
 * this one rather than a simpler two pole is that its coefficients are exact
 * at every cutoff rather than approaching correct at low frequencies, so a
 * sweep that runs up near Nyquist stays where it is put instead of drifting
 * flat, and it does not blow up when the cutoff is moved quickly, which is the
 * normal way anyone uses a filter.
 */
double pd_filter_step(pd_filter_t *f, double in, pd_filter_mode_t mode,
                      double cutoff, double resonance, double rate)
{
    if (mode == PD_FILTER_OFF || mode >= PD_FILTER_MODES) return in;

    const double nyq = rate * 0.5;
    if (cutoff < 15.0)         cutoff = 15.0;
    if (cutoff > nyq * 0.95)   cutoff = nyq * 0.95;
    if (resonance < 0.0) resonance = 0.0;
    if (resonance > 1.0) resonance = 1.0;

    /* q from a gentle 0.5 up to something that sings without self oscillating */
    const double q = 0.5 + resonance * 11.5;
    const double g = tan(M_PI * cutoff / rate);
    const double k = 1.0 / q;
    const double a1 = 1.0 / (1.0 + g * (g + k));
    const double a2 = g * a1;
    const double a3 = g * a2;

    const double v3 = in - f->ic2;
    const double v1 = a1 * f->ic1 + a2 * v3;
    const double v2 = f->ic2 + a2 * f->ic1 + a3 * v3;
    f->ic1 = 2.0 * v1 - f->ic1;
    f->ic2 = 2.0 * v2 - f->ic2;

    switch (mode) {
    case PD_FILTER_LOWPASS:  return v2;
    case PD_FILTER_HIGHPASS: return in - k * v1 - v2;
    case PD_FILTER_BANDPASS: return v1;
    case PD_FILTER_NOTCH:    return in - k * v1;
    default:                 return in;
    }
}
