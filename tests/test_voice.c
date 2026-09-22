/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The voice: two lines and what happens between them. These check pitch,
 * detuning, the waveform envelope doing the work a filter would do elsewhere,
 * and the two modulation modes the 2026 reissue leaves out.
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
#define N  8192
static double buf[N];

static void play(pd_patch_t *p, int note, double vel, double seconds, int release_after_ms)
{
    pd_voice_t v;
    pd_voice_init(&v, p, SR);
    pd_voice_note_on(&v, note, vel);
    long n = (long)(seconds * SR);
    long rel = release_after_ms > 0 ? (long)(release_after_ms / 1000.0 * SR) : -1;
    for (long i = 0; i < n && i < N; i++) {
        if (rel > 0 && i == rel) pd_voice_note_off(&v);
        buf[i] = pd_voice_next(&v);
    }
}

static double mag_at(double hz)
{
    double re = 0, im = 0, w = 2.0 * M_PI * hz / SR;
    for (int i = 0; i < N; i++) {
        double win = 0.5 - 0.5 * cos(2.0 * M_PI * i / N);
        re += buf[i] * win * cos(w * i);
        im += buf[i] * win * sin(w * i);
    }
    return sqrt(re * re + im * im) / N;
}
static double peak(void)
{
    double m = 0;
    for (int i = 0; i < N; i++) if (fabs(buf[i]) > m) m = fabs(buf[i]);
    return m;
}
static double centroid(void)
{
    double num = 0, den = 0;
    for (double f = 50; f < 12000; f += 50) { double m = mag_at(f); num += m * f; den += m; }
    return den > 0 ? num / den : 0;
}

int main(void)
{
    printf("-- a note plays at the pitch it was asked for --\n");
    {
        for (int note = 45; note <= 81; note += 12) {
            pd_patch_t p; pd_patch_init(&p);
            p.line[0].wave = PD_SAW;
            play(&p, note, 1.0, 0.17, 0);
            double want = pd_note_to_hz(note);
            double got = 0, best = 0;
            for (double f = want * 0.85; f < want * 1.15; f += want * 0.002) {
                double m = mag_at(f);
                if (m > best) { best = m; got = f; }
            }
            ok(fabs(got - want) / want < 0.01,
               "note %d should sound at %.1f Hz (found %.1f)", note, want, got);
        }
        printf("   four octaves land in tune\n");
    }

    printf("-- two lines detuned make one thicker sound --\n");
    {
        pd_patch_t p; pd_patch_init(&p);
        p.line_count = 2;
        p.line[0].wave = PD_SAW; p.line[1].wave = PD_SAW;
        p.line[0].detune_cents = -7; p.line[1].detune_cents = +7;
        play(&p, 57, 1.0, 0.17, 0);
        double f0 = pd_note_to_hz(57);
        /* the beat between them puts energy either side of the nominal pitch */
        double below = mag_at(f0 * 0.996), above = mag_at(f0 * 1.004), centre = mag_at(f0);
        ok(below > centre * 0.5 && above > centre * 0.5,
           "detuned lines should spread the fundamental (%.4f %.4f %.4f)", below, centre, above);
        printf("   the fundamental spreads either side\n");
    }

    printf("-- the waveform envelope does a filter's job --\n");
    {
        pd_patch_t p; pd_patch_init(&p);
        p.line[0].wave = PD_SAW;
        /* open, then close, over the length of the note */
        memset(&p.line[0].wave_env, 0, sizeof p.line[0].wave_env);
        p.line[0].wave_env.rate[0] = 52; p.line[0].wave_env.level[0] = 99;
        p.line[0].wave_env.rate[1] = 40; p.line[0].wave_env.level[1] = 0;
        p.line[0].wave_env.sustain_step = 1;
        p.line[0].wave_env.end_step = 2;

        pd_voice_t v; pd_voice_init(&v, &p, SR);
        pd_voice_note_on(&v, 45, 1.0);
        /* early */
        for (int i = 0; i < N; i++) buf[i] = pd_voice_next(&v);
        double bright_open = centroid();
        /* later, once the envelope has closed */
        for (int i = 0; i < (int)(SR * 0.6); i++) pd_voice_next(&v);
        for (int i = 0; i < N; i++) buf[i] = pd_voice_next(&v);
        double bright_closed = centroid();

        ok(bright_open > bright_closed * 1.5,
           "the sound should darken as the waveform envelope closes (%.0f -> %.0f Hz)",
           bright_open, bright_closed);
        printf("   %.0f Hz open, %.0f Hz closed, with no filter in the path\n",
               bright_open, bright_closed);
    }

    printf("-- playing harder opens the waveform --\n");
    {
        pd_patch_t p; pd_patch_init(&p);
        p.line[0].wave = PD_SAW;
        p.velocity_to_wave = 0.9;
        p.velocity_to_level = 0.3;
        play(&p, 45, 0.2, 0.17, 0); double soft = centroid();
        play(&p, 45, 1.0, 0.17, 0); double hard = centroid();
        ok(hard > soft * 1.3, "velocity should brighten the voice (%.0f -> %.0f Hz)", soft, hard);
        printf("   %.0f Hz soft, %.0f Hz hard\n", soft, hard);
    }

    printf("-- ring modulation makes sums and differences --\n");
    {
        pd_patch_t p; pd_patch_init(&p);
        p.line_count = 2;
        p.mix = PD_MIX_RING;
        p.line[0].wave = PD_SAW; p.line[1].wave = PD_SAW;
        p.line[1].semitones = 7;                 /* a fifth above */
        play(&p, 45, 1.0, 0.17, 0);

        double f1 = pd_note_to_hz(45), f2 = pd_note_to_hz(45 + 7);
        double sum = mag_at(f1 + f2), diff = mag_at(f2 - f1), carrier = mag_at(f1);
        ok(sum > carrier * 0.1, "ring should produce the sum tone (%.5f vs carrier %.5f)", sum, carrier);
        ok(diff > carrier * 0.1, "ring should produce the difference tone (%.5f)", diff);
        printf("   sum %.4f, difference %.4f, against a carrier of %.4f\n", sum, diff, carrier);
    }

    printf("-- noise modulation adds noise and keeps the note --\n");
    {
        pd_patch_t p; pd_patch_init(&p);
        p.mix = PD_MIX_NOISE;
        p.line[0].wave = PD_SAW;

        p.noise_amount = 0.0; play(&p, 45, 1.0, 0.17, 0);
        double clean_f0 = mag_at(pd_note_to_hz(45));
        double clean_between = mag_at(pd_note_to_hz(45) * 1.37);

        p.noise_amount = 0.8; play(&p, 45, 1.0, 0.17, 0);
        double noisy_f0 = mag_at(pd_note_to_hz(45));
        double noisy_between = mag_at(pd_note_to_hz(45) * 1.37);

        ok(noisy_between > clean_between * 4.0,
           "noise should fill the gaps between harmonics (%.6f -> %.6f)", clean_between, noisy_between);
        ok(noisy_f0 > clean_f0 * 0.25,
           "the note should survive the noise (%.4f -> %.4f)", clean_f0, noisy_f0);
        printf("   between harmonics %.6f -> %.6f, fundamental %.4f -> %.4f\n",
               clean_between, noisy_between, clean_f0, noisy_f0);
    }

    printf("-- a voice ends, and does not end early --\n");
    {
        pd_patch_t p; pd_patch_init(&p);
        p.line[0].wave = PD_SAW;
        memset(&p.line[0].amp_env, 0, sizeof p.line[0].amp_env);
        p.line[0].amp_env.rate[0] = 70; p.line[0].amp_env.level[0] = 99;
        p.line[0].amp_env.rate[1] = 45; p.line[0].amp_env.level[1] = 0;
        p.line[0].amp_env.sustain_step = 0;
        p.line[0].amp_env.end_step = 1;

        pd_voice_t v; pd_voice_init(&v, &p, SR);
        pd_voice_note_on(&v, 45, 1.0);
        for (int i = 0; i < (int)(SR * 0.5); i++) pd_voice_next(&v);
        ok(pd_voice_active(&v), "a held voice should still be sounding after half a second");
        pd_voice_note_off(&v);
        long n = 0;
        while (pd_voice_active(&v) && n < (long)(SR * 10)) { pd_voice_next(&v); n++; }
        ok(!pd_voice_active(&v), "a released voice should end");
        ok(n > (long)(SR * 0.02), "it should not end instantly (%.3f s)", n / SR);
        printf("   held, then released and gone in %.3f s\n", n / SR);
    }

    printf("-- nothing in the voice clips --\n");
    {
        for (int w = 0; w < PD_WAVE_COUNT; w++) {
            pd_patch_t p; pd_patch_init(&p);
            p.line_count = 2;
            p.line[0].wave = (pd_wave_t)w; p.line[1].wave = (pd_wave_t)w;
            p.line[0].level = 0.5; p.line[1].level = 0.5;
            play(&p, 45, 1.0, 0.17, 0);
            ok(peak() <= 1.0001, "%s: two lines should not clip (%.4f)", pd_wave_name(w), peak());
        }
        printf("   every waveform stays inside unity with two lines running\n");
    }

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
