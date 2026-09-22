/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pd_fx.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *kDrive[PD_DRIVE_MODES] = { "off", "soft", "hard", "fold" };
const char *pd_drive_mode_name(pd_drive_mode_t m)
{
    return (m >= 0 && m < PD_DRIVE_MODES) ? kDrive[m] : "?";
}

void pd_fx_params_init(pd_fx_params_t *p)
{
    memset(p, 0, sizeof(*p));
    p->chorus_mix = 0.0;
    p->chorus_depth_ms = 3.2;
    p->chorus_rate_hz = 0.42;
    p->chorus_spread = 0.7;
    p->delay_mix = 0.0;
    p->delay_time_s = 0.32;
    p->delay_feedback = 0.32;
    p->delay_tone = 0.45;
    p->drive_mode = PD_DRIVE_OFF;
    p->drive_amount = 0.3;
}

struct pd_fx_t {
    double  rate;
    /* chorus: a short delay per side, read at a moving offset */
    double *cho_l, *cho_r;
    int     cho_len, cho_pos;
    double  lfo_phase;
    /* delay: a long line per side, with a one pole on the feedback */
    double *dly_l, *dly_r;
    int     dly_len, dly_pos;
    double  tone_l, tone_r;
};

pd_fx_t *pd_fx_create(double sample_rate)
{
    pd_fx_t *fx = (pd_fx_t *)calloc(1, sizeof(*fx));
    if (!fx) return 0;
    fx->rate = sample_rate > 0 ? sample_rate : 48000.0;
    fx->cho_len = (int)(0.05 * fx->rate) + 4;          /* 50 ms is plenty */
    fx->dly_len = (int)(PD_FX_MAX_DELAY_SECONDS * fx->rate) + 4;
    fx->cho_l = (double *)calloc((size_t)fx->cho_len, sizeof(double));
    fx->cho_r = (double *)calloc((size_t)fx->cho_len, sizeof(double));
    fx->dly_l = (double *)calloc((size_t)fx->dly_len, sizeof(double));
    fx->dly_r = (double *)calloc((size_t)fx->dly_len, sizeof(double));
    if (!fx->cho_l || !fx->cho_r || !fx->dly_l || !fx->dly_r) { pd_fx_destroy(fx); return 0; }
    return fx;
}

void pd_fx_destroy(pd_fx_t *fx)
{
    if (!fx) return;
    free(fx->cho_l); free(fx->cho_r); free(fx->dly_l); free(fx->dly_r);
    free(fx);
}

void pd_fx_reset(pd_fx_t *fx)
{
    if (!fx) return;
    memset(fx->cho_l, 0, (size_t)fx->cho_len * sizeof(double));
    memset(fx->cho_r, 0, (size_t)fx->cho_len * sizeof(double));
    memset(fx->dly_l, 0, (size_t)fx->dly_len * sizeof(double));
    memset(fx->dly_r, 0, (size_t)fx->dly_len * sizeof(double));
    fx->cho_pos = fx->dly_pos = 0;
    fx->lfo_phase = 0.0;
    fx->tone_l = fx->tone_r = 0.0;
}

/* Reading between samples, because a chorus whose delay moves in whole samples
 * clicks its way through the sweep instead of gliding. */
static double read_frac(const double *buf, int len, int pos, double back)
{
    if (back < 1.0) back = 1.0;
    if (back > len - 2) back = len - 2;
    double where = (double)pos - back;
    while (where < 0) where += len;
    const int i0 = (int)where;
    const int i1 = (i0 + 1) % len;
    const double f = where - i0;
    return buf[i0] * (1.0 - f) + buf[i1] * f;
}

static double drive_one(double x, pd_drive_mode_t mode, double amount)
{
    if (mode == PD_DRIVE_OFF || amount <= 0.0) return x;
    const double g = 1.0 + amount * 11.0;
    const double y = x * g;
    switch (mode) {
    case PD_DRIVE_SOFT:
        /* tanh, scaled back so the level does not leap when it is switched on */
        return tanh(y) / tanh(g > 1.0 ? g : 1.0) * (0.6 + 0.4 * (1.0 - amount));
    case PD_DRIVE_HARD:
        return (y > 1.0 ? 1.0 : (y < -1.0 ? -1.0 : y)) * (0.5 + 0.5 * (1.0 - amount));
    case PD_DRIVE_FOLD: {
        /* fold back rather than clip: the part that would have been cut off is
         * reflected instead, which is where the extra harmonics come from */
        double z = y;
        for (int i = 0; i < 4 && (z > 1.0 || z < -1.0); i++)
            z = (z > 1.0) ? (2.0 - z) : (-2.0 - z);
        return z * (0.5 + 0.5 * (1.0 - amount));
    }
    default: return x;
    }
}

void pd_fx_process(pd_fx_t *fx, const pd_fx_params_t *p, double *left, double *right)
{
    if (!fx || !p) return;
    double l = *left, r = *right;

    /* drive first: everything after it should hear what it did */
    if (p->drive_mode != PD_DRIVE_OFF) {
        l = drive_one(l, p->drive_mode, p->drive_amount);
        r = drive_one(r, p->drive_mode, p->drive_amount);
    }

    /* ---- chorus ---------------------------------------------------------
     * Three taps at different points of the same slow cycle, the sides moving
     * apart rather than together, which is what stops it sounding like one
     * wobble laid over the top.
     */
    if (p->chorus_mix > 0.0) {
        fx->cho_l[fx->cho_pos] = l;
        fx->cho_r[fx->cho_pos] = r;

        const double depth = p->chorus_depth_ms * 0.001 * fx->rate;
        const double base  = 0.009 * fx->rate;     /* 9 ms, under the flange range */
        double wl = 0, wr = 0;
        for (int t = 0; t < PD_FX_CHORUS_TAPS; t++) {
            const double off = (double)t / PD_FX_CHORUS_TAPS;
            const double ml = sin(2.0 * M_PI * (fx->lfo_phase + off));
            const double mr = sin(2.0 * M_PI * (fx->lfo_phase + off + 0.25 * p->chorus_spread));
            wl += read_frac(fx->cho_l, fx->cho_len, fx->cho_pos, base + depth * (1.0 + ml));
            wr += read_frac(fx->cho_r, fx->cho_len, fx->cho_pos, base + depth * (1.0 + mr));
        }
        wl /= PD_FX_CHORUS_TAPS;
        wr /= PD_FX_CHORUS_TAPS;

        fx->lfo_phase += p->chorus_rate_hz / fx->rate;
        if (fx->lfo_phase >= 1.0) fx->lfo_phase -= 1.0;
        fx->cho_pos = (fx->cho_pos + 1) % fx->cho_len;

        l = l * (1.0 - p->chorus_mix) + wl * p->chorus_mix;
        r = r * (1.0 - p->chorus_mix) + wr * p->chorus_mix;
    }

    /* ---- delay ----------------------------------------------------------
     * The repeats get darker as they go, because a repeat that keeps all its
     * top end stops sounding like distance and starts sounding like a second
     * instrument playing along.
     */
    if (p->delay_mix > 0.0) {
        double back = p->delay_time_s * fx->rate;
        if (back < 1.0) back = 1.0;
        if (back > fx->dly_len - 2) back = fx->dly_len - 2;

        /* the two sides are offset, so repeats bounce rather than sit in the middle */
        const double el = read_frac(fx->dly_l, fx->dly_len, fx->dly_pos, back);
        const double er = read_frac(fx->dly_r, fx->dly_len, fx->dly_pos, back * 0.75);

        const double a = 0.08 + 0.9 * p->delay_tone;    /* one pole, brighter as tone rises */
        fx->tone_l += a * (el - fx->tone_l);
        fx->tone_r += a * (er - fx->tone_r);

        double fb = p->delay_feedback;
        if (fb > 0.95) fb = 0.95;                       /* never let it run away */
        fx->dly_l[fx->dly_pos] = l + fx->tone_l * fb;
        fx->dly_r[fx->dly_pos] = r + fx->tone_r * fb;
        fx->dly_pos = (fx->dly_pos + 1) % fx->dly_len;

        l = l * (1.0 - p->delay_mix * 0.5) + el * p->delay_mix;
        r = r * (1.0 - p->delay_mix * 0.5) + er * p->delay_mix;
    }

    if (l > 1.0) l = 1.0; if (l < -1.0) l = -1.0;
    if (r > 1.0) r = 1.0; if (r < -1.0) r = -1.0;
    *left = l; *right = r;
}
