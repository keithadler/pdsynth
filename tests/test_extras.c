/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The four things the hardware could not do: four lines instead of two, glide,
 * aftertouch, and a multimode filter. Each is checked by what it does to the
 * sound, not by whether it compiles.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pd_voice.h"


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
/* pd_voice_next_inner hands back samples at the oversampled rate, so a second
 * of sound is PD_OVERSAMPLE times as many of them. Getting this wrong makes
 * every window in this file look at the wrong part of the note, or off the end
 * of the buffer entirely. */
#define IR (SR * PD_OVERSAMPLE)
#define N  ((int)(IR * 1.2))
static double bufL[N], bufR[N];

static int play(pd_patch_t *p, int note, double vel, double sec, int note2, double at2)
{
    pd_voice_t v;
    pd_voice_init(&v, p, SR);
    pd_voice_note_on(&v, note, vel);
    const int n = (int)(sec * IR) < N ? (int)(sec * IR) : N;
    const int when = (int)(at2 * IR);
    for (int i = 0; i < n; i++) {
        if (note2 > 0 && i == when) pd_voice_note_on(&v, note2, vel);
        pd_voice_next_inner(&v, &bufL[i], &bufR[i]);
    }
    return n;
}
static double mag(const double *b, int from, int len, double hz)
{
    double re = 0, im = 0, w = 2 * M_PI * hz / (SR * PD_OVERSAMPLE);
    for (int i = 0; i < len; i++) {
        double win = 0.5 - 0.5 * cos(2 * M_PI * i / len);
        re += b[from + i] * win * cos(w * i);
        im += b[from + i] * win * sin(w * i);
    }
    return sqrt(re * re + im * im) / len;
}
static double peak_freq(const double *b, int from, int len, double lo, double hi)
{
    double best = 0, bf = lo;
    for (double f = lo; f < hi; f *= 1.004) {
        double m = mag(b, from, len, f);
        if (m > best) { best = m; bf = f; }
    }
    return bf;
}
static double rms(const double *b, int from, int len)
{
    double s = 0;
    for (int i = 0; i < len; i++) s += b[from + i] * b[from + i];
    return sqrt(s / len);
}

static void basic(pd_patch_t *p)
{
    pd_patch_init(p);
    for (int i = 0; i < PD_MAX_LINES; i++) {
        p->line[i].wave = PD_SAW;
        p->line[i].level = 0.5;
        for (int k = 0; k < PD_ENV_STEPS; k++) {
            p->line[i].wave_env.rate[k] = 99; p->line[i].wave_env.level[k] = 70;
            p->line[i].amp_env.rate[k] = 99;  p->line[i].amp_env.level[k] = 99;
        }
        p->line[i].wave_env.sustain_step = 0; p->line[i].wave_env.end_step = 1;
        p->line[i].amp_env.sustain_step = 0;  p->line[i].amp_env.end_step = 1;
    }
    p->velocity_to_level = 0.0;
    p->velocity_to_wave = 0.0;
}

int main(void)
{
    printf("-- four lines, not two --\n");
    {
        pd_patch_t p; basic(&p);
        double level[5] = {0};
        for (int n = 1; n <= 4; n++) {
            p.line_count = n;
            for (int i = 0; i < 4; i++) p.line[i].detune_cents = (i - 1.5) * 9.0;
            int len = play(&p, 57, 1.0, 0.35, 0, 0);
            level[n] = rms(bufL, len / 3, len / 3) + rms(bufR, len / 3, len / 3);
        }
        for (int n = 2; n <= 4; n++)
            ok(level[n] > level[n - 1] * 1.02,
               "%d lines should carry more than %d (%.4f vs %.4f)",
               n, n - 1, level[n], level[n - 1]);
        printf("   one to four lines: %.3f %.3f %.3f %.3f\n",
               level[1], level[2], level[3], level[4]);
    }

    printf("-- lines three and four are heard, not merely counted --\n");
    {
        /* line 3 alone, at a pitch the others do not use */
        pd_patch_t p; basic(&p);
        p.line_count = 4;
        p.line[0].level = p.line[1].level = p.line[3].level = 0.0;
        p.line[2].level = 0.8;
        p.line[2].semitones = 7;
        int len = play(&p, 45, 1.0, 0.35, 0, 0);
        double want = pd_note_to_hz(45 + 7);
        double got = peak_freq(bufL, len / 3, 8192, want * 0.7, want * 1.4);
        ok(fabs(got - want) / want < 0.03,
           "line 3 alone should sound at its own pitch (%.1f, wanted %.1f)", got, want);
        printf("   line 3 sounds at %.1f Hz\n", got);
    }

    printf("-- glide slides, and only when asked --\n");
    {
        pd_patch_t p; basic(&p);
        p.line_count = 1; p.line[0].level = 0.9;
        p.glide_seconds = 0.0;
        int len = play(&p, 45, 1.0, 0.7, 57, 0.25);
        double mid = peak_freq(bufL, (int)(0.45 * IR), 8192, 80, 900);
        double want_hi = pd_note_to_hz(57);
        ok(fabs(mid - want_hi) / want_hi < 0.05,
           "with no glide the second note should arrive at once (%.1f vs %.1f)", mid, want_hi);

        p.glide_seconds = 0.45;
        len = play(&p, 45, 1.0, 0.7, 57, 0.25);
        (void)len;
        double early = peak_freq(bufL, (int)(0.30 * IR), 8192, 80, 900);
        double later = peak_freq(bufL, (int)(0.62 * IR), 8192, 80, 900);
        double lo = pd_note_to_hz(45);
        ok(early < want_hi * 0.93 && early > lo * 0.95,
           "with glide it should still be climbing just after the note (%.1f)", early);
        ok(later > early * 1.05, "and higher later (%.1f -> %.1f)", early, later);
        printf("   glide: %.1f Hz climbing to %.1f Hz, target %.1f\n", early, later, want_hi);
    }

    printf("-- aftertouch opens the waveform --\n");
    {
        pd_patch_t p; basic(&p);
        p.line_count = 1; p.line[0].level = 0.9;
        for (int k = 0; k < PD_ENV_STEPS; k++) p.line[0].wave_env.level[k] = 20;
        p.aftertouch_to_wave = 0.7;

        pd_voice_t v; pd_voice_init(&v, &p, SR);
        pd_voice_note_on(&v, 45, 1.0);
        int len = (int)(0.25 * IR);
        for (int i = 0; i < len; i++) pd_voice_next_inner(&v, &bufL[i], &bufR[i]);
        double f0 = pd_note_to_hz(45);
        double dry = 0, wet = 0;
        for (int h = 2; h <= 12; h++) dry += mag(bufL, len / 2, 8192, f0 * h);

        pd_voice_set_pressure(&v, 1.0);
        for (int i = 0; i < len; i++) pd_voice_next_inner(&v, &bufL[i], &bufR[i]);
        for (int h = 2; h <= 12; h++) wet += mag(bufL, len / 2, 8192, f0 * h);

        ok(wet > dry * 1.3, "pressure should add harmonics (%.5f -> %.5f)", dry, wet);
        printf("   upper harmonics %.5f with no pressure, %.5f with it\n", dry, wet);
    }

    printf("-- the filter filters, in all four shapes --\n");
    {
        pd_patch_t p; basic(&p);
        p.line_count = 1; p.line[0].level = 0.9;
        for (int k = 0; k < PD_ENV_STEPS; k++) p.line[0].wave_env.level[k] = 95;
        const double f0 = pd_note_to_hz(45);

        double open_low = 0, open_high = 0;
        p.filter_mode = PD_FILTER_OFF;
        int len = play(&p, 45, 1.0, 0.3, 0, 0);
        for (int h = 1; h <= 3; h++)   open_low  += mag(bufL, len / 3, 8192, f0 * h);
        for (int h = 10; h <= 20; h++) open_high += mag(bufL, len / 3, 8192, f0 * h);

        p.filter_mode = PD_FILTER_LOWPASS; p.filter_cutoff_hz = f0 * 3.0;
        len = play(&p, 45, 1.0, 0.3, 0, 0);
        double lp_low = 0, lp_high = 0;
        for (int h = 1; h <= 3; h++)   lp_low  += mag(bufL, len / 3, 8192, f0 * h);
        for (int h = 10; h <= 20; h++) lp_high += mag(bufL, len / 3, 8192, f0 * h);
        ok(lp_high < open_high * 0.35, "low pass should remove the top (%.5f -> %.5f)", open_high, lp_high);
        ok(lp_low > open_low * 0.5, "and keep the bottom (%.5f -> %.5f)", open_low, lp_low);

        p.filter_mode = PD_FILTER_HIGHPASS; p.filter_cutoff_hz = f0 * 6.0;
        len = play(&p, 45, 1.0, 0.3, 0, 0);
        double hp_low = 0;
        for (int h = 1; h <= 3; h++) hp_low += mag(bufL, len / 3, 8192, f0 * h);
        ok(hp_low < open_low * 0.35, "high pass should remove the bottom (%.5f -> %.5f)", open_low, hp_low);

        p.filter_mode = PD_FILTER_BANDPASS; p.filter_cutoff_hz = f0 * 6.0;
        p.filter_resonance = 0.6;
        len = play(&p, 45, 1.0, 0.3, 0, 0);
        double bp_low = 0, bp_band = 0;
        for (int h = 1; h <= 2; h++) bp_low  += mag(bufL, len / 3, 8192, f0 * h);
        for (int h = 5; h <= 7; h++) bp_band += mag(bufL, len / 3, 8192, f0 * h);
        ok(bp_band > bp_low, "band pass should favor its band (%.5f vs %.5f)", bp_band, bp_low);

        p.filter_mode = PD_FILTER_NOTCH; p.filter_cutoff_hz = f0 * 6.0;
        p.filter_resonance = 0.75;
        len = play(&p, 45, 1.0, 0.3, 0, 0);
        double notch_at = mag(bufL, len / 3, 8192, f0 * 6.0);
        double open_at = 0;
        p.filter_mode = PD_FILTER_OFF;
        len = play(&p, 45, 1.0, 0.3, 0, 0);
        open_at = mag(bufL, len / 3, 8192, f0 * 6.0);
        ok(notch_at < open_at * 0.6, "notch should cut its own frequency (%.6f -> %.6f)", open_at, notch_at);
        printf("   low pass, high pass, band pass and notch all do what they say\n");
    }

    printf("-- the filter stays put when swept hard --\n");
    {
        pd_patch_t p; basic(&p);
        p.line_count = 1; p.line[0].level = 0.9;
        p.filter_mode = PD_FILTER_LOWPASS;
        p.filter_cutoff_hz = 200.0;
        p.filter_resonance = 0.95;
        p.filter_env_depth = 5.0;     /* five octaves of sweep */
        for (int k = 0; k < PD_ENV_STEPS; k++) { p.line[0].wave_env.rate[k] = 40; }
        p.line[0].wave_env.level[0] = 99; p.line[0].wave_env.level[1] = 0;
        p.line[0].wave_env.sustain_step = 1; p.line[0].wave_env.end_step = 2;

        int len = play(&p, 45, 1.0, 1.0, 0, 0);
        int finite = 1, loud = 0;
        for (int i = 0; i < len; i++) {
            if (!isfinite(bufL[i]) || !isfinite(bufR[i])) finite = 0;
            if (fabs(bufL[i]) > 8.0) loud++;
        }
        ok(finite, "a five octave sweep at full resonance must stay finite");
        ok(loud == 0, "and must not blow up (%d samples over 8.0)", loud);
        printf("   five octaves at full resonance, still stable\n");
    }

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
