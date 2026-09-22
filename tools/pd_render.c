/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Headless renderer. Plays the built-in demonstrations to a WAV file, so the
 * thing can be listened to rather than read about.
 *
 *     pd_render out.wav [demo]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pd_voice.h"
#include "pd_presets.h"
#include "pd_synth.h"
#include "pd_fx.h"

#define SR       48000.0
#define POLYPHONY 8

static pd_fx_params_t g_fxp;
static pd_fx_t *g_fx;

typedef pd_synth_t poly_t;
static void poly_init(poly_t *p, const pd_patch_t *patch) { pd_synth_init(p, patch, SR, POLYPHONY); }
static void poly_on(poly_t *p, int note, double vel) { pd_synth_note_on(p, note, vel); }
static void poly_off(poly_t *p, int note) { pd_synth_note_off(p, note); }
static void poly_next2(poly_t *p, double *l, double *r)
{
    pd_synth_render(p, l, r);
    *l *= 0.3; *r *= 0.3;
    if (g_fx) pd_fx_process(g_fx, &g_fxp, l, r);
}

/* an eight step envelope, written the way a CZ panel would show it */
static void env_set(pd_env_params_t *e, const int *rate, const int *level,
                    int count, int sustain, int end)
{
    memset(e, 0, sizeof(*e));
    for (int i = 0; i < count && i < PD_ENV_STEPS; i++) {
        e->rate[i]  = (uint8_t)rate[i];
        e->level[i] = (uint8_t)level[i];
    }
    e->sustain_step = (uint8_t)sustain;
    e->end_step     = (uint8_t)end;
}

typedef struct { const char *name; void (*build)(pd_patch_t *); } demo_t;

/* 1. the sweep a CZ is known for, with no filter anywhere in the path */
static void demo_sweep(pd_patch_t *p)
{
    pd_patch_init(p);
    p->line[0].wave = PD_SAW;
    p->velocity_to_wave = 0.7;
    env_set(&p->line[0].wave_env, (int[]){55, 30, 40}, (int[]){99, 35, 0}, 3, 1, 2);
    env_set(&p->line[0].amp_env,  (int[]){75, 45, 50}, (int[]){99, 80, 0}, 3, 1, 2);
}

/* 2. the resonant waveforms, where the formant climbs and the pitch does not */
static void demo_resonant(pd_patch_t *p)
{
    pd_patch_init(p);
    p->line[0].wave = PD_RESO_SAW;
    env_set(&p->line[0].wave_env, (int[]){44, 36, 42}, (int[]){99, 20, 0}, 3, 1, 2);
    env_set(&p->line[0].amp_env,  (int[]){78, 50, 48}, (int[]){99, 85, 0}, 3, 1, 2);
}

/* 3. two lines, detuned, which is most of what the machine is remembered for */
static void demo_two_lines(pd_patch_t *p)
{
    pd_patch_init(p);
    p->line_count = 2;
    p->line[0].wave = PD_SAW;       p->line[0].detune_cents = -8; p->line[0].level = 0.55;
    p->line[1].wave = PD_RESO_SAW;  p->line[1].detune_cents = +8; p->line[1].level = 0.45;
    env_set(&p->line[0].wave_env, (int[]){48, 34, 40}, (int[]){90, 30, 0}, 3, 1, 2);
    env_set(&p->line[1].wave_env, (int[]){42, 30, 40}, (int[]){99, 25, 0}, 3, 1, 2);
    for (int i = 0; i < 2; i++)
        env_set(&p->line[i].amp_env, (int[]){70, 44, 46}, (int[]){99, 78, 0}, 3, 1, 2);
}

/* 4. an eight step envelope doing what an ADSR cannot: a double attack */
static void demo_eight_step(pd_patch_t *p)
{
    pd_patch_init(p);
    p->line[0].wave = PD_SQUARE;
    env_set(&p->line[0].wave_env,
            (int[]){62, 50, 58, 46, 40}, (int[]){99, 25, 92, 40, 0}, 5, 3, 4);
    env_set(&p->line[0].amp_env,
            (int[]){76, 52, 60, 48, 46}, (int[]){99, 45, 95, 70, 0}, 5, 3, 4);
}

/* 5. ring modulation, which the 2026 reissue does not have */
static void demo_ring(pd_patch_t *p)
{
    pd_patch_init(p);
    p->line_count = 2;
    p->mix = PD_MIX_RING;
    p->line[0].wave = PD_SAW;
    p->line[1].wave = PD_SAW;
    p->line[1].semitones = 7;
    env_set(&p->line[0].wave_env, (int[]){50, 36, 42}, (int[]){95, 30, 0}, 3, 1, 2);
    for (int i = 0; i < 2; i++)
        env_set(&p->line[i].amp_env, (int[]){74, 46, 48}, (int[]){99, 75, 0}, 3, 1, 2);
}

/* 6. noise modulation, which it does not have either */
static void demo_noise(pd_patch_t *p)
{
    pd_patch_init(p);
    p->mix = PD_MIX_NOISE;
    p->noise_amount = 0.55;
    p->line[0].wave = PD_RESO_TRAPEZOID;
    env_set(&p->line[0].wave_env, (int[]){46, 34, 42}, (int[]){88, 22, 0}, 3, 1, 2);
    env_set(&p->line[0].amp_env,  (int[]){72, 48, 46}, (int[]){99, 80, 0}, 3, 1, 2);
}

static const demo_t kDemos[] = {
    { "sweep",      demo_sweep },
    { "resonant",   demo_resonant },
    { "two lines",  demo_two_lines },
    { "eight step", demo_eight_step },
    { "ring",       demo_ring },
    { "noise",      demo_noise },
};
#define DEMO_COUNT ((int)(sizeof kDemos / sizeof kDemos[0]))

/* note, start, length, velocity */
static const struct { int note; double at, len, vel; } kPhrase[] = {
    { 45, 0.00, 0.50, 0.55 }, { 52, 0.30, 0.50, 0.70 }, { 57, 0.60, 0.60, 0.90 },
    { 64, 1.00, 0.30, 1.00 }, { 62, 1.25, 0.30, 0.85 }, { 60, 1.50, 0.30, 0.70 },
    { 45, 1.95, 1.50, 0.95 }, { 57, 1.95, 1.50, 0.95 },
    { 64, 1.95, 1.50, 0.95 }, { 69, 1.95, 1.50, 0.95 },
};
#define PHRASE_COUNT ((int)(sizeof kPhrase / sizeof kPhrase[0]))
#define PHRASE_SECONDS 5.0

static long render_demo(const demo_t *d, float *out, long cap)
{
    pd_patch_t patch;
    d->build(&patch);
    poly_t poly;
    poly_init(&poly, &patch);

    long total = (long)(PHRASE_SECONDS * SR);
    if (total > cap) total = cap;
    for (long i = 0; i < total; i++) {
        for (int e = 0; e < PHRASE_COUNT; e++) {
            if (i == (long)(kPhrase[e].at * SR)) poly_on(&poly, kPhrase[e].note, kPhrase[e].vel);
            if (i == (long)((kPhrase[e].at + kPhrase[e].len) * SR)) poly_off(&poly, kPhrase[e].note);
        }
        double l, r; poly_next2(&poly, &l, &r); out[i] = (float)(0.5 * (l + r));
    }
    return total;
}

/* The whole factory bank, each preset playing the same phrase, so it can be
 * listened through rather than read off a table. */
static int render_bank(float *out, long cap, long *lengths)
{
    long n = 0;
    for (int i = 0; i < pd_preset_count() && n < cap; i++) {
        const pd_preset_t *pr = pd_preset(i);
        poly_t poly;
        poly_init(&poly, &pr->patch);
        /* each preset through its own effects, which is how it is meant to be
         * heard, and a fresh chain so one preset's repeats do not spill into
         * the next one */
        g_fxp = pr->fx;
        pd_fx_destroy(g_fx);
        g_fx = pd_fx_create(SR);
        long total = (long)(PHRASE_SECONDS * SR);
        if (n + total > cap) total = cap - n;
        for (long k = 0; k < total; k++) {
            for (int e = 0; e < PHRASE_COUNT; e++) {
                if (k == (long)(kPhrase[e].at * SR)) poly_on(&poly, kPhrase[e].note, kPhrase[e].vel);
                if (k == (long)((kPhrase[e].at + kPhrase[e].len) * SR)) poly_off(&poly, kPhrase[e].note);
            }
            double l, r; poly_next2(&poly, &l, &r); out[n + k] = (float)(0.5 * (l + r));
        }
        fprintf(stderr, "  %2d. %-16s %s\n", i + 1, pr->name, pr->family);
        n += total + (long)(0.35 * SR);
        if (lengths) lengths[i] = total;
    }
    return (int)n;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: pd_render out.wav [demo name | bank]\n  demos:");
        for (int i = 0; i < DEMO_COUNT; i++) printf(" %s%s", kDemos[i].name,
                                                    i + 1 < DEMO_COUNT ? "," : "\n");
        return 2;
    }
    const int units = pd_preset_count() > DEMO_COUNT ? pd_preset_count() : DEMO_COUNT;
    long cap = (long)(PHRASE_SECONDS * SR) * units + (long)SR * units;
    pd_fx_params_init(&g_fxp);
    g_fx = pd_fx_create(SR);
    /* "fx" plays the bank through a chorus and a delay, which is how anyone
     * would actually run it, and is the difference between a demonstration and
     * a measurement. */
    if (argc > 3 && strcmp(argv[3], "fx") == 0) {
        g_fxp.chorus_mix = 0.45;
        g_fxp.delay_mix = 0.22;
        g_fxp.delay_time_s = 0.28;
        g_fxp.delay_feedback = 0.3;
    }
    float *buf = calloc((size_t)cap, sizeof *buf);
    if (!buf) return 1;

    long n = 0;
    if (argc > 2 && strcmp(argv[2], "bank") == 0) {
        n = render_bank(buf, cap, 0);
    } else
    for (int i = 0; i < DEMO_COUNT; i++) {
        if (argc > 2 && strcmp(argv[2], kDemos[i].name) != 0) continue;
        printf("  %s\n", kDemos[i].name);
        n += render_demo(&kDemos[i], buf + n, cap - n);
        n += (long)(0.4 * SR);              /* a breath between demonstrations */
    }
    if (n == 0) { printf("no such demo\n"); free(buf); return 2; }

    double pk = 0;
    for (long i = 0; i < n; i++) if (fabs(buf[i]) > pk) pk = fabs(buf[i]);
    double g = pk > 0 ? 0.89 / pk : 1.0;

    FILE *f = fopen(argv[1], "wb");
    if (!f) { perror(argv[1]); free(buf); return 1; }
    long bytes = n * 2;
    fwrite("RIFF", 1, 4, f);
    uint32_t v32 = (uint32_t)(36 + bytes); fwrite(&v32, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v32 = 16;               fwrite(&v32, 4, 1, f);
    uint16_t v16 = 1;       fwrite(&v16, 2, 1, f);
    v16 = 1;                fwrite(&v16, 2, 1, f);
    v32 = (uint32_t)SR;     fwrite(&v32, 4, 1, f);
    v32 = (uint32_t)SR * 2; fwrite(&v32, 4, 1, f);
    v16 = 2;                fwrite(&v16, 2, 1, f);
    v16 = 16;               fwrite(&v16, 2, 1, f);
    fwrite("data", 1, 4, f);
    v32 = (uint32_t)bytes;  fwrite(&v32, 4, 1, f);
    for (long i = 0; i < n; i++) {
        double s = buf[i] * g * 32767.0;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        int16_t v = (int16_t)lrint(s);
        fwrite(&v, 2, 1, f);
    }
    fclose(f);
    printf("wrote %s (%.1f s, gain %.2fx)\n", argv[1], n / SR, g);
    pd_fx_destroy(g_fx);
    free(buf);
    return 0;
}
