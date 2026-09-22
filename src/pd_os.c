/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <math.h>
#include <string.h>
#include "pd_os.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Modified Bessel function of the first kind, for the Kaiser window. */
static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 24; k++) {
        term *= (x * 0.5) / k;
        sum += term * term;
    }
    return sum;
}

/*
 * A windowed sinc, not a biquad cascade.
 *
 * The first attempt used three biquads, a sixth order Butterworth at 21.6 kHz
 * running at 192 kHz. In normalised terms that is an extremely gentle slope:
 * at 30 kHz it is barely down at all, so everything between the output's
 * Nyquist and the inner one survived the filter and folded when the extra
 * samples were thrown away. The measurement said so plainly, and no amount of
 * raising the oversampling factor would have fixed a filter that was not
 * filtering.
 *
 * This is a 95 tap Kaiser windowed sinc with its corner at 20 kHz, which puts
 * the stopband more than 80 dB down by 24 kHz. It costs one multiply-add per
 * tap per output sample, not per inner sample, because only the samples that
 * are kept need computing.
 */
void pd_decimator_init(pd_decimator_t *d, double output_rate)
{
    memset(d, 0, sizeof(*d));
    const double inner = output_rate * PD_OVERSAMPLE;
    const double fc = 20000.0 / inner;          /* normalised corner */
    const double beta = 8.6;                    /* about 85 dB of stopband */
    const int M = PD_DECIM_TAPS - 1;
    const double denom = bessel_i0(beta);

    double sum = 0.0;
    for (int n = 0; n < PD_DECIM_TAPS; n++) {
        const double m = n - M * 0.5;
        const double s = (fabs(m) < 1e-9) ? (2.0 * fc)
                       : sin(2.0 * M_PI * fc * m) / (M_PI * m);
        const double r = 2.0 * n / (double)M - 1.0;
        const double w = bessel_i0(beta * sqrt(fabs(1.0 - r * r))) / denom;
        d->h[n] = s * w;
        sum += d->h[n];
    }
    for (int n = 0; n < PD_DECIM_TAPS; n++) d->h[n] /= sum;   /* unity at DC */
    d->pos = 0;
}

void pd_decimator_push(pd_decimator_t *d, double x)
{
    d->z[d->pos] = x;
    d->pos = (d->pos + 1) % PD_DECIM_TAPS;
}

double pd_decimator_read(const pd_decimator_t *d)
{
    double acc = 0.0;
    int i = d->pos;
    for (int n = PD_DECIM_TAPS - 1; n >= 0; n--) {
        acc += d->h[n] * d->z[i];
        i = (i + 1) % PD_DECIM_TAPS;
    }
    return acc;
}
