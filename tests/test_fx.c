/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Chorus, delay and drive, each checked by what it does to a signal rather
 * than by whether it runs. An effect that is on and inaudible is the same as
 * one that is broken, and a delay that runs away is worse than either.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pd_fx.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int passed, failed;
static void ok(int cond, const char *fmt, ...)
{
    if (cond) { passed++; return; }
    failed++;
    va_list ap; va_start(ap, fmt);
    printf("  FAIL: "); vprintf(fmt, ap); printf("\n");
    va_end(ap);
}

#define SR 48000.0
#define N  (int)(SR * 3)
static double outL[N], outR[N];

/* a steady tone through the effect, so what comes out can be compared with it */
static void run_tone(pd_fx_params_t *p, double hz, double sec, double amp)
{
    pd_fx_t *fx = pd_fx_create(SR);
    const int n = (int)(sec * SR) < N ? (int)(sec * SR) : N;
    for (int i = 0; i < n; i++) {
        double l = amp * sin(2 * M_PI * hz * i / SR), r = l;
        pd_fx_process(fx, p, &l, &r);
        outL[i] = l; outR[i] = r;
    }
    pd_fx_destroy(fx);
}
/* one click, so a delay's repeats can be counted */
static void run_click(pd_fx_params_t *p, double sec)
{
    pd_fx_t *fx = pd_fx_create(SR);
    const int n = (int)(sec * SR) < N ? (int)(sec * SR) : N;
    for (int i = 0; i < n; i++) {
        double l = (i < 32) ? 0.8 : 0.0, r = l;
        pd_fx_process(fx, p, &l, &r);
        outL[i] = l; outR[i] = r;
    }
    pd_fx_destroy(fx);
}
static double rms(const double *b, int from, int len)
{
    double s = 0;
    for (int i = from; i < from + len; i++) s += b[i] * b[i];
    return sqrt(s / len);
}
static double peak(const double *b, int from, int len)
{
    double m = 0;
    for (int i = from; i < from + len; i++) if (fabs(b[i]) > m) m = fabs(b[i]);
    return m;
}
static double harmonics(const double *b, int from, int len, double f0)
{
    double tot = 0;
    for (int h = 2; h <= 9; h++) {
        double re = 0, im = 0, w = 2 * M_PI * f0 * h / SR;
        for (int i = 0; i < len; i++) {
            double win = 0.5 - 0.5 * cos(2 * M_PI * i / len);
            re += b[from + i] * win * cos(w * i);
            im += b[from + i] * win * sin(w * i);
        }
        tot += sqrt(re * re + im * im) / len;
    }
    return tot;
}

int main(void)
{
    pd_fx_params_t p;

    printf("-- with everything off, nothing happens --\n");
    {
        pd_fx_params_init(&p);
        run_tone(&p, 220.0, 0.3, 0.5);
        double err = 0;
        for (int i = 100; i < 12000; i++) {
            double want = 0.5 * sin(2 * M_PI * 220.0 * i / SR);
            double d = fabs(outL[i] - want);
            if (d > err) err = d;
        }
        ok(err < 1e-9, "a bypassed chain must pass the signal untouched (worst %.2e)", err);
        printf("   untouched to within %.1e\n", err);
    }

    printf("-- the chorus moves the sound, and moves the sides apart --\n");
    {
        pd_fx_params_init(&p);
        p.chorus_mix = 0.6;
        run_tone(&p, 330.0, 1.2, 0.5);

        /* the two sides must differ, or it is a wobble and not a chorus */
        double diff = 0, level = 0;
        for (int i = 20000; i < 50000; i++) {
            diff += fabs(outL[i] - outR[i]);
            level += fabs(outL[i]);
        }
        ok(diff > level * 0.05, "the sides should differ (%.4f against %.4f)", diff, level);

        /* and the level should breathe as the delay sweeps */
        double lo = 9e9, hi = 0;
        for (int w = 20000; w + 4000 < 55000; w += 4000) {
            double r = rms(outL, w, 4000);
            if (r > hi) hi = r;
            if (r < lo) lo = r;
        }
        ok(20 * log10(hi / lo) > 0.3, "a chorus should not sit still (%.2f dB)", 20 * log10(hi / lo));
        printf("   sides differ, level breathes %.2f dB\n", 20 * log10(hi / lo));
    }

    printf("-- the delay repeats, at the time it was asked for --\n");
    {
        pd_fx_params_init(&p);
        p.delay_mix = 0.8;
        p.delay_time_s = 0.25;
        p.delay_feedback = 0.5;
        run_click(&p, 1.4);

        /* the first repeat should land a quarter of a second in */
        int at = 0;
        double best = 0;
        for (int i = 2000; i < (int)(0.45 * SR); i++)
            if (fabs(outL[i]) > best) { best = fabs(outL[i]); at = i; }
        const double seconds = at / SR;
        ok(fabs(seconds - 0.25) < 0.01, "the first repeat should be at 0.25 s (found %.3f)", seconds);
        ok(best > 0.05, "and should be audible (%.3f)", best);

        /* and each repeat should be quieter than the one before */
        double r1 = peak(outL, (int)(0.24 * SR), 2000);
        double r2 = peak(outL, (int)(0.49 * SR), 2000);
        double r3 = peak(outL, (int)(0.74 * SR), 2000);
        ok(r2 < r1 && r3 < r2, "repeats should fade (%.3f, %.3f, %.3f)", r1, r2, r3);
        printf("   first repeat at %.3f s, fading %.3f to %.3f to %.3f\n", seconds, r1, r2, r3);
    }

    printf("-- the delay cannot run away, however hard it is pushed --\n");
    {
        pd_fx_params_init(&p);
        p.delay_mix = 1.0;
        p.delay_time_s = 0.08;
        p.delay_feedback = 1.0;      /* asked for more than is allowed */
        run_click(&p, 3.0);
        double late = peak(outL, (int)(2.5 * SR), 10000);
        double early = peak(outL, 0, 10000);
        ok(late < early, "feedback of 1.0 must still decay (%.3f -> %.3f)", early, late);
        int finite = 1;
        for (int i = 0; i < (int)(3.0 * SR); i++) if (!isfinite(outL[i])) finite = 0;
        ok(finite, "and must stay finite");
        printf("   feedback clamped: %.3f decays to %.3f after three seconds\n", early, late);
    }

    printf("-- the repeats get darker, which is what makes them sound distant --\n");
    {
        pd_fx_params_init(&p);
        p.delay_mix = 0.9; p.delay_time_s = 0.2; p.delay_feedback = 0.7;
        p.delay_tone = 0.15;
        run_tone(&p, 2000.0, 1.6, 0.4);
        const double dark = rms(outL, (int)(1.2 * SR), 8192);
        p.delay_tone = 1.0;
        run_tone(&p, 2000.0, 1.6, 0.4);
        const double bright = rms(outL, (int)(1.2 * SR), 8192);
        ok(bright > dark * 1.2, "a bright setting should keep more top (%.4f vs %.4f)", dark, bright);
        printf("   dark repeats %.4f, bright repeats %.4f at 2 kHz\n", dark, bright);
    }

    printf("-- drive adds harmonics, and each shape adds more than the last --\n");
    {
        pd_fx_params_init(&p);
        run_tone(&p, 220.0, 0.4, 0.5);
        const double clean = harmonics(outL, 2000, 8192, 220.0);

        double h[PD_DRIVE_MODES];
        for (int m = PD_DRIVE_SOFT; m < PD_DRIVE_MODES; m++) {
            pd_fx_params_init(&p);
            p.drive_mode = (pd_drive_mode_t)m;
            p.drive_amount = 0.8;
            run_tone(&p, 220.0, 0.4, 0.5);
            h[m] = harmonics(outL, 2000, 8192, 220.0);
            ok(h[m] > clean * 3.0, "%s drive should add harmonics (%.5f against %.5f clean)",
               pd_drive_mode_name((pd_drive_mode_t)m), h[m], clean);
            ok(peak(outL, 2000, 8192) <= 1.0001, "%s drive must not leave the rails",
               pd_drive_mode_name((pd_drive_mode_t)m));
        }
        ok(h[PD_DRIVE_FOLD] > h[PD_DRIVE_SOFT],
           "folding should be richer than a soft knee (%.5f against %.5f)",
           h[PD_DRIVE_FOLD], h[PD_DRIVE_SOFT]);
        printf("   clean %.5f, soft %.5f, hard %.5f, fold %.5f\n",
               clean, h[PD_DRIVE_SOFT], h[PD_DRIVE_HARD], h[PD_DRIVE_FOLD]);
    }

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
