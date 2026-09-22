/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <math.h>
#include "pd_osc.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *kNames[PD_WAVE_COUNT] = {
    "saw", "square", "pulse", "double sine",
    "saw pulse", "reso saw", "reso triangle", "reso trapezoid"
};

const char *pd_wave_name(pd_wave_t w)
{
    return (w >= 0 && w < PD_WAVE_COUNT) ? kNames[w] : "?";
}

void pd_osc_init(pd_osc_t *o)
{
    o->phase = 0.0;
    o->increment = 0.0;
}

void pd_osc_set_freq(pd_osc_t *o, double hz, double sample_rate)
{
    o->increment = (sample_rate > 0.0) ? hz / sample_rate : 0.0;
}

/*
 * The two segment bend that makes a sawtooth.
 *
 * Split the cycle at `m`. The first segment covers the first half of the sine
 * in however long `m` lasts, the second segment covers the rest. At m = 0.5
 * nothing is bent and a sine comes out. As m falls the first half is rushed
 * and the second crawls, and the result approaches a ramp.
 */
static double bend_two_segment(double p, double m)
{
    if (m < 1e-6)   m = 1e-6;
    if (m > 1.0 - 1e-6) m = 1.0 - 1e-6;
    return (p < m) ? (0.5 * p / m)
                   : (0.5 + 0.5 * (p - m) / (1.0 - m));
}

/*
 * A window that falls from 1 to 0 across the cycle, shaped three ways. The
 * resonant waveforms are a fast sine multiplied by one of these, which is what
 * puts a formant peak in the spectrum and lets it sweep with the envelope
 * while the pitch stays put.
 */
double pd_window(double p, pd_wave_t wave)
{
    switch (wave) {
    case PD_RESO_SAW:
        return 1.0 - p;                                  /* straight fall */
    case PD_RESO_TRIANGLE:
        return (p < 0.5) ? (2.0 * p) : (2.0 * (1.0 - p)); /* rise then fall */
    case PD_RESO_TRAPEZOID:
        if (p < 0.25) return 4.0 * p;                    /* rise, hold, fall */
        if (p < 0.5)  return 1.0;
        return 2.0 * (1.0 - p);
    default:
        return 1.0;
    }
}

int pd_resonant_harmonic(double amount)
{
    if (amount < 0.0) amount = 0.0;
    if (amount > 1.0) amount = 1.0;
    return 1 + (int)floor(amount * 15.0);
}

double pd_distort(double phase, pd_wave_t wave, double amount)
{
    double p = phase - floor(phase);
    if (amount < 0.0) amount = 0.0;
    if (amount > 1.0) amount = 1.0;

    switch (wave) {
    case PD_SAW: {
        /*
         * The knee walks from the middle of the cycle towards its end, so the
         * first half of the sine is stretched over most of the period and the
         * second half is rushed: a slow climb and a quick fall.
         *
         * It stops at 0.92 rather than running to the edge. Past about 0.94
         * the harmonics start weakening again, because the rushed half becomes
         * too short to resolve and the waveform collapses back towards a
         * hump. Measured across the range, the spectrum is richest just under
         * 0.9, which is where this stops.
         */
        double m = 0.5 + 0.42 * amount;
        return bend_two_segment(p, m);
    }
    case PD_SQUARE: {
        /*
         * Hold the phase still and the output holds still too, which is how a
         * sine becomes a square without a filter anywhere. The holds have to
         * sit on the sine's peak and trough, phase 0.25 and 0.75, not on its
         * zero crossings: parking at a zero crossing holds the output at
         * nothing and the waveform stays a sine with a stutter in it.
         *
         * Five segments: climb to the peak, hold, cross to the trough, hold,
         * climb home. The two holds take `h` of the cycle between them and the
         * three climbs share the rest in the proportion 1:2:1, which at h = 0
         * lays them end to end as the identity, so nothing is bent and a sine
         * comes out.
         */
        double h = 0.5 * amount;
        double a = (1.0 - h) * 0.25;          /* the short climbs */
        double t1 = a, t2 = t1 + h * 0.5, t3 = t2 + 2.0 * a, t4 = t3 + h * 0.5;
        if (p < t1) return 0.25 * p / a;
        if (p < t2) return 0.25;
        if (p < t3) return 0.25 + 0.5 * (p - t2) / (2.0 * a);
        if (p < t4) return 0.75;
        return 0.75 + 0.25 * (p - t4) / a;
    }
    case PD_PULSE: {
        /*
         * One narrow excursion per cycle and silence either side: the phase
         * sweeps the whole sine inside a shrinking window, then parks.
         */
        double w = 1.0 - 0.98 * amount;
        if (p < w) return p / w;
        return 1.0;
    }
    case PD_DOUBLE_SINE: {
        /* two cycles of the sine in one, each bent like the sawtooth */
        double m = 0.5 + 0.42 * amount;
        double q = p * 2.0;
        if (q >= 1.0) q -= 1.0;
        double bent = bend_two_segment(q, m);
        return (p < 0.5) ? 0.5 * bent : 0.5 + 0.5 * bent;
    }
    case PD_SAW_PULSE: {
        /* a sawtooth bend for the first part, parked for the rest */
        double w = 1.0 - 0.9 * amount;
        double m = 0.5 + 0.42 * amount;
        if (p < w) return bend_two_segment(p / w, m);
        return 1.0;
    }
    default:
        return p;   /* the resonant three do not bend phase; see pd_osc_next */
    }
}

/*
 * `cycles` is the oscillator's increment: cycles per sample at whatever rate it
 * is actually running, which is the oversampled one. Half of one over that is
 * how many harmonics of this note fit underneath that Nyquist, and harmonics
 * that do not fit do not vanish, they fold.
 *
 * PD_BEND_FULL and PD_BEND_MIN were chosen by measuring off-harmonic energy
 * across the keyboard and moving them until the top two octaves came clean
 * without the middle of the keyboard losing its character.
 */
#define PD_BEND_FULL 300.0   /* harmonics of room needed for an unheld bend */
#define PD_BEND_MIN   0.30   /* never quieter in character than this */

double pd_bend_ceiling(double cycles_per_sample, double unused)
{
    (void)unused;
    if (cycles_per_sample <= 1e-9) return 1.0;
    const double room = 0.5 / cycles_per_sample;
    if (room >= PD_BEND_FULL) return 1.0;
    const double f = room / PD_BEND_FULL;
    return f < PD_BEND_MIN ? PD_BEND_MIN : f;
}

double pd_osc_next(pd_osc_t *o, pd_wave_t wave, double amount)
{
    double p = o->phase;
    double out;

    if (amount < 0.0) amount = 0.0;
    if (amount > 1.0) amount = 1.0;

    /* The oscillator knows its own frequency, so it can hold the bend back
     * itself rather than relying on every caller to remember. */
    {
        const double ceiling = pd_bend_ceiling(o->increment, 1.0);
        if (amount > ceiling) amount = ceiling;
    }

    if (wave >= PD_RESO_SAW) {
        /*
         * Resonant: a sine whose frequency is a whole multiple of the note,
         * multiplied by a window that falls across one cycle of the note. The
         * multiple is what the envelope sweeps, so the formant moves and the
         * pitch does not. Keeping it a whole number keeps the window and the
         * sine locked together, which is what stops it buzzing.
         */
        /* A resonant waveform states its harmonic outright, so it can be
         * capped exactly: never ask for a partial that will not fit. */
        double harmonic = (double)pd_resonant_harmonic(amount);
        const double top = 0.45 / (o->increment > 1e-9 ? o->increment : 1e-9);
        if (harmonic > top) harmonic = floor(top) < 1.0 ? 1.0 : floor(top);
        out = sin(2.0 * M_PI * p * harmonic) * pd_window(p, wave);
    } else {
        out = sin(2.0 * M_PI * pd_distort(p, wave, amount));
    }

    o->phase = p + o->increment;
    if (o->phase >= 1.0) o->phase -= floor(o->phase);
    return out;
}
