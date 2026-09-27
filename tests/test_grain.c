/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A grain cloud cannot be judged the way the oscillator is.
 *
 * test_osc asks which harmonics of the note are present and how loud, because
 * a phase distorted sine is strictly harmonic and anything off a harmonic is a
 * fault. None of that transfers. Scattering a tone into windowed fragments
 * smears its spectrum on purpose: energy between the harmonics is the effect
 * working, so the aliasing measure that guards the oscillator would fail every
 * cloud that does what it is for.
 *
 * So this asks different questions, and they are the ones a player would ask.
 * Is it still the note I played. Does it stay that note when I open the
 * spread. Does the level hold still when I change the density. Does it click.
 * Does the window actually do anything. And does turning it off leave the
 * synth exactly as it was.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "pd_grain.h"
#include "pd_osc.h"
#include "pd_voice.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int passed, failed;

static void ok(int cond, const char *fmt, ...)
{
    if (cond) { passed++; return; }
    failed++;
    va_list ap;
    va_start(ap, fmt);
    printf("  FAIL: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

#define SR  48000.0
#define N   16384

/* Magnitude at one frequency, Hann windowed. */
static double bin(const double *buf, int n, double hz)
{
    double re = 0, im = 0;
    const double w = 2.0 * M_PI * hz / SR;
    for (int i = 0; i < n; i++) {
        const double win = 0.5 - 0.5 * cos(2.0 * M_PI * i / n);
        re += buf[i] * win * cos(w * i);
        im += buf[i] * win * sin(w * i);
    }
    return sqrt(re * re + im * im) / n;
}

static double rms(const double *buf, int n)
{
    double s = 0;
    for (int i = 0; i < n; i++) s += buf[i] * buf[i];
    return sqrt(s / n);
}

/*
 * Where the energy sits around the note, and how far it is spread, both in
 * cents. The first number is the centering; the second is the one that stops
 * a control from passing its test by doing nothing at all.
 */
static void spectrum_shape(const double *buf, int n, double f0,
                           double *center_cents, double *width_cents)
{
    double num = 0, den = 0, num2 = 0;
    for (double hz = f0 * 0.25; hz <= f0 * 4.0; hz *= 1.0035) {
        const double m = bin(buf, n, hz);
        const double p = m * m;
        const double cents = 1200.0 * log2(hz / f0);
        num  += p * cents;
        num2 += p * cents * cents;
        den  += p;
    }
    if (den <= 0) { *center_cents = *width_cents = 0; return; }
    const double mean = num / den;
    const double var  = num2 / den - mean * mean;
    *center_cents = mean;
    *width_cents  = sqrt(var > 0 ? var : 0);
}

static void render(double *buf, int n, const pd_grain_params_t *g, double f0,
                   pd_wave_t wave, double bend, uint32_t seed)
{
    pd_cloud_t c;
    pd_cloud_init(&c, seed);
    const double inc = f0 / SR;
    for (int i = 0; i < n; i++)
        buf[i] = pd_cloud_next(&c, g, wave, bend, inc, SR);
}

/* ------------------------------------------------------------------ */
static void test_windows(void)
{
    printf("windows\n");

    for (int s = 0; s < PD_GRAIN_SHAPE_COUNT; s++) {
        const pd_grain_shape_t sh = (pd_grain_shape_t)s;
        const char *nm = pd_grain_shape_name(sh);

        for (double edge = 0.0; edge <= 1.0001; edge += 0.125) {
            double prev = pd_grain_window(sh, edge, 0.0);
            double peak = 0.0, worst_jump = 0.0;
            int in_range = 1;

            /* Finely enough to resolve the shortest taper a shape allows,
             * which is 0.5% of the grain. Probing coarser than the taper
             * measures the probe rather than the window: at 2000 points the
             * Roadsian shoulder read as a 0.27 step and is not one. */
            for (int i = 0; i <= 20000; i++) {
                const double t = (double)i / 20000.0;
                const double v = pd_grain_window(sh, edge, t);
                if (v < -1e-9 || v > 1.0 + 1e-9) in_range = 0;
                if (v > peak) peak = v;
                const double jump = fabs(v - prev);
                if (jump > worst_jump) worst_jump = jump;
                prev = v;
            }

            ok(in_range, "%s edge %.3f stays within 0 and 1", nm, edge);
            ok(peak > 0.99, "%s edge %.3f reaches full open (peak %.3f)", nm, edge, peak);

            /*
             * A window is silent at both ends or the grain begins and ends
             * with a step, and a step at the grain rate is a click. The
             * rectangle is the exception and is here so the others have
             * something to be compared against.
             */
            const double a = pd_grain_window(sh, edge, 0.0);
            const double b = pd_grain_window(sh, edge, 1.0);
            if (sh == PD_GRAIN_RECT) {
                ok(a == 1.0 && b == 1.0, "rectangular is open at both ends by definition");
            } else {
                ok(fabs(a) < 1e-9 && fabs(b) < 1e-9,
                   "%s edge %.3f closes at both ends (%.2e, %.2e)", nm, edge, a, b);
                /*
                 * And gets there without a step. The bound is what the
                 * shortest taper any shape allows implies over one probe
                 * interval: a real discontinuity measures about 1.0, so this
                 * separates the two by a wide margin rather than measuring
                 * how steep a short taper is allowed to be.
                 */
                ok(worst_jump < 0.15,
                   "%s edge %.3f has no step in it (worst %.4f)", nm, edge, worst_jump);
            }
        }
    }

    /* ZedSynth's own note: a trapezoid with full edges is a triangle. */
    double worst = 0;
    for (int i = 1; i < 1000; i++) {
        const double t = (double)i / 1000.0;
        const double d = fabs(pd_grain_window(PD_GRAIN_TRAPEZOID, 1.0, t)
                            - pd_grain_window(PD_GRAIN_TRIANGLE, 0.5, t));
        if (d > worst) worst = d;
    }
    ok(worst < 1e-9, "a trapezoid with full edges is a triangle (worst %.2e)", worst);
}

/* ------------------------------------------------------------------ */
static void test_still_the_note(void)
{
    printf("pitch\n");

    static double buf[N];
    pd_grain_params_t g;
    pd_grain_params_init(&g);
    g.on = 1;
    g.pitch_spread = 0.0;

    const double f0 = 440.0;
    render(buf, N, &g, f0, PD_SAW, 0.5, 12345);

    const double at_note = bin(buf, N, f0);
    ok(at_note > 1e-4, "a cloud of a 440 Hz tone has energy at 440 (%.5f)", at_note);

    /*
     * The fundamental has to be the strongest thing in the bottom of the
     * spectrum, or the grain rate has become the pitch, which is the classic
     * way a granular oscillator goes wrong: the ear then hears the scheduler
     * rather than the note.
     */
    double best = 0; double best_hz = 0;
    for (double hz = 40.0; hz < 700.0; hz += 2.0) {
        const double m = bin(buf, N, hz);
        if (m > best) { best = m; best_hz = hz; }
    }
    ok(fabs(best_hz - f0) < 8.0,
       "the loudest partial below 700 Hz is the note, not the grain rate (%.0f Hz)", best_hz);

    /* and the grain rate itself must not be sitting there as a tone */
    const double grain_hz = g.overlap / (g.length_ms * 0.001);
    if (grain_hz > 40.0 && grain_hz < 700.0) {
        ok(bin(buf, N, grain_hz) < at_note * 0.5,
           "the grain rate (%.0f Hz) is not louder than the note", grain_hz);
    }
}

/* ------------------------------------------------------------------ */
/*
 * The detune has to spread around the note rather than away from it.
 *
 * ZedSynth draws its per grain frequency from random_float(-1, 2), which has a
 * mean of +0.5, and then squares it keeping the sign, which pushes the mean
 * further up; its source carries a "-FIX- does not center on frequency" note
 * about this. So opening the spread there also raises the pitch. This measures
 * the spectral centroid around the note and requires it to stay put.
 */
static void test_spread_is_centered(void)
{
    printf("spread centering\n");

    static double buf[N];
    pd_grain_params_t g;
    pd_grain_params_init(&g);
    g.on = 1;
    g.length_ms = 60.0;
    g.overlap = 8.0;

    const double f0 = 440.0;

    for (int k = 0; k < 3; k++) {
        const double spr = (k == 0) ? 0.0 : (k == 1 ? 0.15 : 0.30);
        g.pitch_spread = spr;

        /* average over seeds: one cloud is a sample, not a measurement */
        double sum_cent = 0.0;
        const int seeds = 6;
        for (int s = 0; s < seeds; s++) {
            render(buf, N, &g, f0, PD_SAW, 0.4, 1000u + 7919u * (uint32_t)s);

            /* energy weighted mean frequency across an octave either side */
            double num = 0, den = 0;
            for (double hz = f0 * 0.5; hz <= f0 * 2.0; hz += 2.0) {
                const double m = bin(buf, N, hz);
                const double p = m * m;
                num += p * log2(hz / f0);
                den += p;
            }
            sum_cent += (den > 0 ? num / den : 0.0) * 1200.0;
        }
        const double cents = sum_cent / seeds;

        ok(fabs(cents) < 45.0,
           "spread %.2f leaves the cloud centered on the note (%.1f cents off)",
           spr, cents);
    }

    /*
     * And it has to actually spread. Checking only that the cloud stays
     * centered is a test a control that does nothing passes perfectly, which
     * is the way this project has been fooled before: the mutation that made
     * pitch spread a no-op went unnoticed until this was added.
     */
    double width[3], center[3];
    const double spreads[3] = { 0.0, 0.15, 0.4 };
    for (int k = 0; k < 3; k++) {
        g.pitch_spread = spreads[k];
        double wsum = 0, csum = 0;
        for (int s = 0; s < 4; s++) {
            render(buf, N, &g, f0, PD_SAW, 0.4, 555u + 7919u * (uint32_t)s);
            double c1, w1;
            spectrum_shape(buf, N, f0, &c1, &w1);
            wsum += w1; csum += c1;
        }
        width[k] = wsum / 4.0; center[k] = csum / 4.0;
    }
    ok(width[1] > width[0] * 1.15,
       "opening the spread widens the cloud (%.0f -> %.0f cents)", width[0], width[1]);
    ok(width[2] > width[1] * 1.15,
       "opening it further widens it more (%.0f -> %.0f cents)", width[1], width[2]);
    ok(fabs(center[2]) < 80.0,
       "and it is still the same note when wide open (%.0f cents off)", center[2]);
}

/* ------------------------------------------------------------------ *
 * Grain length and overlap both have to mean something.
 *
 * My first attempt here measured spectral width, on the idea that short
 * grains smear a tone wider than long ones. That is true of a single grain
 * and false of this cloud: the grains are cut from one running oscillator, so
 * at any decent overlap they add back up to the tone they came from and no
 * smearing survives. The measurement said 3 ms grains were narrower than
 * 20 ms ones and it was right to.
 *
 * What the controls really do is put a periodic envelope on that tone. The
 * grain train repeats at overlap divided by grain length, so the cloud carries
 * sidebands at exactly the note plus and minus that rate, and at nothing else.
 * That is a sharp, falsifiable place to look: move either control and the
 * sidebands move to where the arithmetic says they will be.
 * ------------------------------------------------------------------ */
static void test_grain_rate_sidebands(void)
{
    printf("grain rate\n");

    static double buf[N];
    const double f0 = 440.0;
    const double lens[4] = { 8.0, 20.0, 60.0, 120.0 };
    const double ovs[2]  = { 1.5, 6.0 };

    for (int o = 0; o < 2; o++) {
        for (int k = 0; k < 4; k++) {
            pd_grain_params_t g;
            pd_grain_params_init(&g);
            g.on = 1;
            g.shape = PD_GRAIN_ROADSIAN;
            g.overlap = ovs[o];
            g.length_ms = lens[k];
            g.onset_spread = 0.0;   /* jitter smears the sidebands; this is
                                       measuring where they are, not how sharp */
            g.pitch_spread = 0.0;

            render(buf, N, &g, f0, PD_SAW, 0.4, 7);

            const double rate = g.overlap / (g.length_ms * 0.001);
            const double at_f0 = bin(buf, N, f0);
            const double side  = bin(buf, N, f0 + rate);
            const double off   = bin(buf, N, f0 + 1.5 * rate);

            ok(side > 0.01 * at_f0,
               "overlap %.1f, %.0f ms: a sideband sits %.0f Hz above the note "
               "(%.4f vs %.4f at the note)", ovs[o], lens[k], rate, side, at_f0);
            ok(side > 5.0 * off,
               "overlap %.1f, %.0f ms: and not at one and a half times that "
               "(%.5f vs %.5f)", ovs[o], lens[k], side, off);
        }
    }

    /*
     * The cross check, which is the part that a hardcoded length cannot
     * survive: look for 20 ms grains' sidebands in a 120 ms cloud. If grain
     * length were ignored they would be sitting right there.
     */
    pd_grain_params_t g;
    pd_grain_params_init(&g);
    g.on = 1; g.shape = PD_GRAIN_ROADSIAN; g.overlap = 1.5;
    g.onset_spread = 0.0; g.pitch_spread = 0.0;

    g.length_ms = 120.0;
    render(buf, N, &g, f0, PD_SAW, 0.4, 7);
    const double mine    = bin(buf, N, f0 + 1.5 / 0.120);
    const double someone = bin(buf, N, f0 + 1.5 / 0.020);
    ok(mine > 5.0 * someone,
       "a 120 ms cloud has its own sidebands and not another length's "
       "(%.5f vs %.5f)", mine, someone);

    /* and the same for overlap */
    g.length_ms = 20.0; g.overlap = 1.5;
    render(buf, N, &g, f0, PD_SAW, 0.4, 7);
    const double at_1_5 = bin(buf, N, f0 + 1.5 / 0.020);
    const double at_6   = bin(buf, N, f0 + 6.0 / 0.020);
    ok(at_1_5 > 5.0 * at_6,
       "overlap 1.5 puts the sideband where overlap 1.5 puts it, not where 6 would "
       "(%.5f vs %.5f)", at_1_5, at_6);
}

/* ------------------------------------------------------------------ */
static void test_density_is_not_volume(void)
{
    printf("density\n");

    static double buf[N];
    pd_grain_params_t g;
    pd_grain_params_init(&g);
    g.on = 1;
    g.shape = PD_GRAIN_ROADSIAN;
    g.onset_spread = 0.3;

    double lo = 1e9, hi = 0;
    for (double ov = 1.0; ov <= 16.0; ov *= 2.0) {
        g.overlap = ov;
        double acc = 0;
        for (int s = 0; s < 4; s++) {
            render(buf, N, &g, 220.0, PD_SAW, 0.5, 900u + 104729u * (uint32_t)s);
            acc += rms(buf, N);
        }
        acc /= 4.0;
        if (acc < lo) lo = acc;
        if (acc > hi) hi = acc;
    }
    const double db = 20.0 * log10(hi / (lo > 1e-12 ? lo : 1e-12));
    ok(db < 6.0,
       "overlap from 1 to 16 moves the level by less than 6 dB (%.1f dB)", db);
}

/* ------------------------------------------------------------------ */
/*
 * Does the window earn its place? A rectangular grain starts and stops on a
 * step, so the output jumps. Everything else should not. If both measured the
 * same, the windowing code would be doing nothing and every other test here
 * would still pass.
 */
static void test_window_stops_the_clicks(void)
{
    printf("clicks\n");

    static double buf[N];
    pd_grain_params_t g;
    pd_grain_params_init(&g);
    g.on = 1;
    g.overlap = 3.0;
    g.length_ms = 20.0;
    g.onset_spread = 0.0;

    double worst[PD_GRAIN_SHAPE_COUNT];
    for (int s = 0; s < PD_GRAIN_SHAPE_COUNT; s++) {
        g.shape = (pd_grain_shape_t)s;
        g.edge = (s == PD_GRAIN_TRIANGLE) ? 0.5 : 0.25;
        render(buf, N, &g, 220.0, PD_SAW, 0.3, 4242);
        double w = 0;
        for (int i = 1; i < N; i++) {
            const double d = fabs(buf[i] - buf[i - 1]);
            if (d > w) w = d;
        }
        worst[s] = w;
    }

    for (int s = 1; s < PD_GRAIN_SHAPE_COUNT; s++) {
        ok(worst[s] < worst[PD_GRAIN_RECT],
           "%s jumps less than an unwindowed grain (%.4f vs %.4f)",
           pd_grain_shape_name((pd_grain_shape_t)s), worst[s], worst[PD_GRAIN_RECT]);
    }
    ok(worst[PD_GRAIN_ROADSIAN] < worst[PD_GRAIN_RECT] * 0.5,
       "the Roadsian window at least halves the worst jump (%.4f vs %.4f)",
       worst[PD_GRAIN_ROADSIAN], worst[PD_GRAIN_RECT]);
}

/* ------------------------------------------------------------------ */
static void test_housekeeping(void)
{
    printf("housekeeping\n");

    static double buf[N];
    pd_grain_params_t g;
    pd_grain_params_init(&g);
    g.on = 1;

    /* nothing silly comes out, at any setting anyone can dial */
    int bad = 0;
    double peak = 0;
    for (int s = 0; s < PD_GRAIN_SHAPE_COUNT; s++) {
        for (double ov = 0.05; ov <= 40.0; ov *= 3.0) {
            for (double ms = 0.2; ms <= 400.0; ms *= 6.0) {
                g.shape = (pd_grain_shape_t)s;
                g.overlap = ov;
                g.length_ms = ms;
                g.pitch_spread = 0.7;
                g.onset_spread = 0.9;
                render(buf, 2048, &g, 55.0, PD_RESO_SAW, 0.9, 31337);
                for (int i = 0; i < 2048; i++) {
                    if (!isfinite(buf[i])) bad++;
                    if (fabs(buf[i]) > peak) peak = fabs(buf[i]);
                }
            }
        }
    }
    ok(bad == 0, "no NaN or infinity anywhere in the parameter space (%d bad)", bad);
    ok(peak < 40.0, "output stays bounded at every setting (peak %.2f)", peak);

    /* the same seed is the same cloud, or nothing above is repeatable */
    static double a[4096], b[4096];
    pd_grain_params_init(&g); g.on = 1; g.pitch_spread = 0.5;
    render(a, 4096, &g, 330.0, PD_SAW, 0.5, 777);
    render(b, 4096, &g, 330.0, PD_SAW, 0.5, 777);
    ok(memcmp(a, b, sizeof(a)) == 0, "the same seed gives the same cloud");

    render(b, 4096, &g, 330.0, PD_SAW, 0.5, 778);
    ok(memcmp(a, b, sizeof(a)) != 0, "a different seed gives a different cloud");

    /* the pool holds up at settings a player would actually use */
    pd_cloud_t c;
    pd_cloud_init(&c, 5150);
    pd_grain_params_init(&g); g.on = 1; g.overlap = 16.0; g.length_ms = 80.0;
    long live_sum = 0;
    const int n = 48000;
    for (int i = 0; i < n; i++) {
        pd_cloud_next(&c, &g, PD_SAW, 0.5, 220.0 / SR, SR);
        live_sum += pd_cloud_live(&c);
    }
    ok(c.starved == 0, "no grain is dropped at overlap 16 (%ld dropped)", c.starved);

    /*
     * And the overlap control means what it says: with onset spread on, the
     * average number sounding should land near it rather than near anything
     * else, because that is the number the player is dialing.
     */
    const double avg = (double)live_sum / (double)n;
    ok(fabs(avg - 16.0) < 4.0,
       "overlap 16 really does keep about 16 grains sounding (%.1f)", avg);
}

/* ------------------------------------------------------------------ */
/*
 * Off has to mean off. A patch that does not ask for grains must come out of
 * the voice exactly as it did before any of this existed, sample for sample,
 * or every preset in the bank has quietly changed.
 */
static void test_off_changes_nothing(void)
{
    printf("off is off\n");

    pd_patch_t p;
    pd_patch_init(&p);
    ok(p.line[0].grain.on == 0, "a fresh patch has granular off");

    pd_voice_t v;
    pd_voice_init(&v, &p, SR);
    pd_voice_note_on(&v, 60, 0.8);
    static double plain[4096];
    for (int i = 0; i < 4096; i++) plain[i] = pd_voice_next(&v);

    /* turning it on has to change something, or "off is off" proves nothing */
    p.line[0].grain.on = 1;
    pd_voice_init(&v, &p, SR);
    pd_voice_note_on(&v, 60, 0.8);
    static double grainy[4096];
    for (int i = 0; i < 4096; i++) grainy[i] = pd_voice_next(&v);
    ok(memcmp(plain, grainy, sizeof(plain)) != 0,
       "turning granular on changes the sound");

    /* and back off has to return the original, exactly */
    p.line[0].grain.on = 0;
    pd_voice_init(&v, &p, SR);
    pd_voice_note_on(&v, 60, 0.8);
    static double again[4096];
    for (int i = 0; i < 4096; i++) again[i] = pd_voice_next(&v);
    ok(memcmp(plain, again, sizeof(plain)) == 0,
       "off gives back the original voice, sample for sample");
}

int main(void)
{
    printf("pd_grain\n\n");
    test_windows();
    test_still_the_note();
    test_spread_is_centered();
    test_grain_rate_sidebands();
    test_density_is_not_volume();
    test_window_stops_the_clicks();
    test_housekeeping();
    test_off_changes_nothing();
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
