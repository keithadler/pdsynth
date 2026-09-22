/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The envelope is judged by where it is at a given moment, because an envelope
 * that reaches the right levels in the wrong time is a different instrument.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pd_env.h"

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

static double run(pd_env_t *e, double seconds)
{
    long n = (long)(seconds * SR);
    double v = 0;
    for (long i = 0; i < n; i++) v = pd_env_next(e);
    return v;
}

int main(void)
{
    printf("-- the rate curve spans what a synthesist needs --\n");
    {
        double slow = pd_env_rate_seconds(0), fast = pd_env_rate_seconds(99);
        ok(slow > 20.0, "rate 0 should be a long sweep (%.1f s)", slow);
        ok(fast < 0.001, "rate 99 should be effectively instant (%.6f s)", fast);
        int monotonic = 1;
        for (int r = 1; r <= 99; r++)
            if (pd_env_rate_seconds(r) >= pd_env_rate_seconds(r - 1)) monotonic = 0;
        ok(monotonic, "every rate step should be faster than the one below it");
        printf("   rate 0 = %.1f s, rate 50 = %.3f s, rate 99 = %.6f s\n",
               slow, pd_env_rate_seconds(50), fast);
    }

    printf("-- a held key stops on the sustain step and stays there --\n");
    {
        pd_env_params_t p; memset(&p, 0, sizeof p);
        p.rate[0] = 80; p.level[0] = 99;     /* attack  */
        p.rate[1] = 70; p.level[1] = 50;     /* decay   */
        p.rate[2] = 60; p.level[2] = 0;      /* release */
        p.sustain_step = 1;
        p.end_step = 2;

        pd_env_t e; pd_env_init(&e, &p, SR);
        pd_env_key_down(&e);
        double at_half = run(&e, 0.5);
        double at_two  = run(&e, 1.5);
        ok(fabs(at_half - 0.5051) < 0.02, "should settle on the sustain level (%.3f)", at_half);
        ok(fabs(at_two - at_half) < 0.001, "should not drift while held (%.4f -> %.4f)", at_half, at_two);
        ok(!pd_env_finished(&e), "a held envelope should not report itself finished");

        pd_env_key_up(&e);
        double after = run(&e, 1.0);
        ok(after < 0.01, "should fall to the end level once released (%.4f)", after);
        ok(pd_env_finished(&e), "should report finished after the end step");
        printf("   held at %.3f, released to %.4f\n", at_half, after);
    }

    printf("-- eight steps really are eight, not a disguised ADSR --\n");
    {
        /*
         * A shape no ADSR can make: up, part way down, up again, then down to
         * a sustain. Rather than guess when each step lands, record the whole
         * curve and look at its turning points, which is what the shape is.
         */
        pd_env_params_t p; memset(&p, 0, sizeof p);
        p.rate[0] = 46; p.level[0] = 89;
        p.rate[1] = 46; p.level[1] = 30;
        p.rate[2] = 46; p.level[2] = 98;
        p.rate[3] = 46; p.level[3] = 40;
        p.sustain_step = 3;
        p.end_step = 4;      /* level 0 */

        pd_env_t e; pd_env_init(&e, &p, SR);
        pd_env_key_down(&e);

        const int N = (int)(SR * 0.6);
        static double curve[28800];
        for (int i = 0; i < N && i < 28800; i++) curve[i] = pd_env_next(&e);

        double peak1 = 0, trough = 1, peak2 = 0, settle = curve[N - 1];
        int i_peak1 = 0, i_trough = 0;
        for (int i = 0; i < N; i++) {              /* first rise */
            if (curve[i] > peak1) { peak1 = curve[i]; i_peak1 = i; }
            if (curve[i] > 0.85) break;
        }
        for (int i = i_peak1; i < N; i++) {        /* the dip */
            if (curve[i] < trough) { trough = curve[i]; i_trough = i; }
            if (curve[i] < 0.35 && curve[i + 1] > curve[i]) break;
        }
        for (int i = i_trough; i < N; i++)         /* the second rise */
            if (curve[i] > peak2) peak2 = curve[i];

        ok(peak1 > 0.85, "should climb to the first level (%.3f)", peak1);
        ok(trough < 0.40, "should fall back part way (%.3f)", trough);
        ok(peak2 > 0.90, "should climb again, higher (%.3f)", peak2);
        ok(fabs(settle - 0.404) < 0.03, "should settle on the sustain step (%.3f)", settle);
        ok(trough < peak1 - 0.3 && peak2 > trough + 0.3,
           "the middle of the shape must dip and rise, which an ADSR cannot do");
        printf("   %.2f up, %.2f down, %.2f up, holding at %.2f\n",
               peak1, trough, peak2, settle);
    }

    printf("-- rates control time, and the time is about right --\n");
    {
        for (int rate = 40; rate <= 80; rate += 20) {
            pd_env_params_t p; memset(&p, 0, sizeof p);
            p.rate[0] = (uint8_t)rate; p.level[0] = 99;
            p.sustain_step = 0; p.end_step = 1;

            pd_env_t e; pd_env_init(&e, &p, SR);
            pd_env_key_down(&e);
            double expect = pd_env_rate_seconds((uint8_t)rate);
            long n = 0, limit = (long)(SR * 60);
            while (pd_env_next(&e) < 0.99 && n < limit) n++;
            double took = n / SR;
            ok(fabs(took - expect) < expect * 0.25 + 0.002,
               "rate %d should take about %.4f s, took %.4f s", rate, expect, took);
            printf("   rate %d: %.4f s (curve says %.4f s)\n", rate, took, expect);
        }
    }

    printf("-- retriggering does not jump --\n");
    {
        pd_env_params_t p; memset(&p, 0, sizeof p);
        p.rate[0] = 70; p.level[0] = 99;
        p.rate[1] = 65; p.level[1] = 60;
        p.rate[2] = 38; p.level[2] = 0;    /* a slow release, so the retrigger
                                              lands while it is still falling */
        p.sustain_step = 1; p.end_step = 2;

        pd_env_t e; pd_env_init(&e, &p, SR);
        pd_env_key_down(&e);
        run(&e, 0.4);
        pd_env_key_up(&e);
        run(&e, 0.05);
        double before = e.value;
        /*
         * The point is to catch the retrigger in flight. If the release has
         * already reached zero there is no step to make and the check passes
         * without testing anything, so say so rather than count it.
         */
        ok(before > 0.1 && before < 0.9,
           "the release should still be in flight when retriggered (%.3f), "
           "or this check proves nothing", before);
        pd_env_key_down(&e);
        double after = pd_env_next(&e);
        ok(fabs(after - before) < 0.01,
           "a retrigger should continue from where it was (%.3f -> %.3f)", before, after);
        printf("   retriggered mid release from %.3f without a step\n", before);
    }

    printf("-- a flat envelope holds wide open --\n");
    {
        pd_env_params_t p; pd_env_params_flat(&p);
        pd_env_t e; pd_env_init(&e, &p, SR);
        pd_env_key_down(&e);
        double v = run(&e, 0.5);
        ok(v > 0.99, "a flat envelope should sit at the top (%.4f)", v);
    }

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
