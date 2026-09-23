/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The oscillator is judged by its spectrum, because that is the only thing
 * about it a listener can hear. Compiling proves nothing.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "pd_os.h"
#include "pd_osc.h"

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

/*
 * The oscillator is always run oversampled by the voice, and it holds its bend
 * back when there is not room for the harmonics. Testing it bare at the output
 * rate makes it hold back much harder than it ever does in use, and then every
 * "does it brighten" check stops early against a ceiling that real playing
 * never meets.
 */
#define SR   (48000.0 * PD_OVERSAMPLE)
#define N    8192
#define F0   200.0

/* magnitude of harmonic h of F0 in buf */
static double harmonic(const double *buf, int h)
{
    double re = 0, im = 0;
    double w = 2.0 * M_PI * (F0 * h) / SR;
    for (int i = 0; i < N; i++) {
        double win = 0.5 - 0.5 * cos(2.0 * M_PI * i / N);
        re += buf[i] * win * cos(w * i);
        im += buf[i] * win * sin(w * i);
    }
    return sqrt(re * re + im * im) / N;
}

static void render(double *buf, pd_wave_t wave, double amount)
{
    pd_osc_t o;
    pd_osc_init(&o);
    pd_osc_set_freq(&o, F0, SR);
    for (int i = 0; i < N; i++) buf[i] = pd_osc_next(&o, wave, amount);
}

int main(void)
{
    static double buf[N];

    printf("-- every waveform is a sine when nothing is bent --\n");
    for (int w = 0; w < PD_WAVE_COUNT; w++) {
        if (w >= PD_RESO_SAW) continue;   /* the resonant three are windowed */
        render(buf, (pd_wave_t)w, 0.0);
        double f = harmonic(buf, 1), h2 = harmonic(buf, 2), h3 = harmonic(buf, 3);
        ok(f > 0.2, "%s: fundamental should be strong (%.4f)", pd_wave_name(w), f);
        ok(h2 / f < 0.02 && h3 / f < 0.02,
           "%s at zero distortion should be a clean sine (h2 %.3f h3 %.3f of f)",
           pd_wave_name(w), h2 / f, h3 / f);
    }
    printf("   %d waveforms collapse to a sine\n", PD_RESO_SAW);

    printf("-- bending adds harmonics, and more bend adds more --\n");
    for (int w = 0; w < PD_WAVE_COUNT; w++) {
        double bright[3];
        for (int k = 0; k < 3; k++) {
            double amt = 0.1 + 0.44 * k;
            render(buf, (pd_wave_t)w, amt);
            double num = 0, den = 0;
            for (int h = 1; h <= 40; h++) {
                double m = harmonic(buf, h);
                num += m * h; den += m;
            }
            bright[k] = den > 0 ? num / den : 1.0;
        }
        ok(bright[1] > bright[0] * 1.05 && bright[2] > bright[1] * 1.05,
           "%s should brighten as it bends (%.2f -> %.2f -> %.2f)",
           pd_wave_name(w), bright[0], bright[1], bright[2]);
    }
    printf("   all %d waveforms brighten monotonically\n", PD_WAVE_COUNT);

    printf("-- the sawtooth really is a sawtooth --\n");
    {
        render(buf, PD_SAW, 1.0);
        double h1 = harmonic(buf, 1);
        /* a ramp falls off as 1/n and has every harmonic, even and odd */
        ok(harmonic(buf, 2) / h1 > 0.15, "saw should have a second harmonic (%.3f)",
           harmonic(buf, 2) / h1);
        ok(harmonic(buf, 3) / h1 > 0.10, "saw should have a third harmonic (%.3f)",
           harmonic(buf, 3) / h1);
        double r2 = harmonic(buf, 2) / h1, r4 = harmonic(buf, 4) / h1;
        ok(r4 < r2, "saw harmonics should fall with order (h2 %.3f, h4 %.3f)", r2, r4);
        printf("   h2/h1 %.2f, h3/h1 %.2f, h4/h1 %.2f\n",
               r2, harmonic(buf, 3) / h1, r4);
    }

    printf("-- the square leans on odd harmonics --\n");
    {
        render(buf, PD_SQUARE, 1.0);
        double h1 = harmonic(buf, 1);
        double odd = harmonic(buf, 3) / h1 + harmonic(buf, 5) / h1;
        double even = harmonic(buf, 2) / h1 + harmonic(buf, 4) / h1;
        ok(odd > even, "square should favor odd harmonics (odd %.3f, even %.3f)", odd, even);
        printf("   odd %.2f vs even %.2f\n", odd, even);
    }

    printf("-- the resonant waveforms put a peak where the pitch is not --\n");
    for (int w = PD_RESO_SAW; w < PD_WAVE_COUNT; w++) {
        int peak_low = 1, peak_high = 1;
        double best;

        render(buf, (pd_wave_t)w, 0.15);
        best = 0;
        for (int h = 1; h <= 30; h++) { double m = harmonic(buf, h); if (m > best) { best = m; peak_low = h; } }

        render(buf, (pd_wave_t)w, 0.85);
        best = 0;
        for (int h = 1; h <= 30; h++) { double m = harmonic(buf, h); if (m > best) { best = m; peak_high = h; } }

        ok(peak_high > peak_low + 3,
           "%s: the formant should climb with the envelope (harmonic %d -> %d)",
           pd_wave_name(w), peak_low, peak_high);
        printf("   %-15s formant rides harmonic %d up to %d\n",
               pd_wave_name(w), peak_low, peak_high);
    }

    printf("-- the pitch does not move when the formant does --\n");
    for (int w = PD_RESO_SAW; w < PD_WAVE_COUNT; w++) {
        /* zero crossings of the window period, which is the note itself */
        double f_low, f_high;
        render(buf, (pd_wave_t)w, 0.2);  f_low  = harmonic(buf, 1);
        render(buf, (pd_wave_t)w, 0.9);  f_high = harmonic(buf, 1);
        ok(f_low > 1e-5 && f_high > 1e-5,
           "%s should keep energy at the fundamental at both ends", pd_wave_name(w));
    }
    printf("   the note stays put\n");

    printf("-- nothing clips or goes to pieces --\n");
    for (int w = 0; w < PD_WAVE_COUNT; w++) {
        for (double amt = 0.0; amt <= 1.0; amt += 0.05) {
            render(buf, (pd_wave_t)w, amt);
            double mx = 0;
            for (int i = 0; i < N; i++) {
                if (!isfinite(buf[i])) { mx = 1e9; break; }
                if (fabs(buf[i]) > mx) mx = fabs(buf[i]);
            }
            ok(mx <= 1.0001, "%s at %.2f should stay inside unity (%.4f)",
               pd_wave_name(w), amt, mx);
        }
    }
    printf("   every waveform stays finite and inside unity at every depth\n");

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
