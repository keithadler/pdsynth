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


/* ---------------------------------------------------------------------------
 * Playing a standard MIDI file.
 *
 * Enough of the format to play one: the tracks are merged into one list of
 * events by absolute tick, the tempo map is followed as it is walked, and
 * everything that is not a note is ignored. A renderer that can only play a
 * phrase built into itself cannot be listened to for longer than a phrase.
 * ------------------------------------------------------------------------ */

typedef struct { long at; unsigned char kind, note, vel; } mev_t;

static unsigned long read_var(const unsigned char *b, long *i, long end)
{
    unsigned long v = 0;
    while (*i < end) {
        const unsigned char c = b[(*i)++];
        v = (v << 7) | (c & 0x7f);
        if (!(c & 0x80)) break;
    }
    return v;
}
static unsigned long be32(const unsigned char *b)
{
    return ((unsigned long)b[0] << 24) | ((unsigned long)b[1] << 16)
         | ((unsigned long)b[2] << 8) | b[3];
}
static int cmp_mev(const void *a, const void *b)
{
    const mev_t *x = (const mev_t *)a, *y = (const mev_t *)b;
    if (x->at != y->at) return x->at < y->at ? -1 : 1;
    /* note offs before note ons at the same instant, so a repeated note
     * retriggers rather than being cut off by its own predecessor */
    return (int)x->kind - (int)y->kind;
}

/* Returns the number of events, or -1. Times come back in samples. */
static long midi_load(const char *path, mev_t **out)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = (unsigned char *)malloc((size_t)len);
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(d); return -1; }
    fclose(f);

    if (len < 14 || memcmp(d, "MThd", 4) != 0) { free(d); fprintf(stderr, "not a MIDI file\n"); return -1; }
    const int division = (d[12] << 8) | d[13];
    if (division & 0x8000) { free(d); fprintf(stderr, "SMPTE timing not supported\n"); return -1; }

    long cap = 4096, n = 0;
    mev_t *ev = (mev_t *)malloc((size_t)cap * sizeof *ev);
    /* tempo changes, kept as (tick, microseconds per beat) */
    long tcap = 256, tn = 0;
    long *tt = (long *)malloc((size_t)tcap * sizeof *tt);
    long *tv = (long *)malloc((size_t)tcap * sizeof *tv);

    long p = 14;
    while (p + 8 <= len) {
        const int is_track = memcmp(d + p, "MTrk", 4) == 0;
        const long clen = (long)be32(d + p + 4);
        long q = p + 8, end = q + clen;
        p = end;
        if (!is_track || end > len) continue;

        long tick = 0;
        unsigned char running = 0;
        while (q < end) {
            tick += (long)read_var(d, &q, end);
            if (q >= end) break;
            unsigned char st = d[q];
            if (st & 0x80) { q++; if (st < 0xf0) running = st; }
            else st = running;

            if (st == 0xff) {
                const unsigned char meta = d[q++];
                const long mlen = (long)read_var(d, &q, end);
                if (meta == 0x51 && mlen == 3) {
                    if (tn == tcap) {
                        tcap *= 2;
                        tt = (long *)realloc(tt, (size_t)tcap * sizeof *tt);
                        tv = (long *)realloc(tv, (size_t)tcap * sizeof *tv);
                    }
                    tt[tn] = tick;
                    tv[tn] = ((long)d[q] << 16) | ((long)d[q+1] << 8) | d[q+2];
                    tn++;
                }
                q += mlen;
            } else if (st == 0xf0 || st == 0xf7) {
                const long mlen = (long)read_var(d, &q, end);
                q += mlen;
            } else {
                const int hi = st & 0xf0;
                const int nbytes = (hi == 0xc0 || hi == 0xd0) ? 1 : 2;
                if (hi == 0x90 || hi == 0x80) {
                    if (n == cap) { cap *= 2; ev = (mev_t *)realloc(ev, (size_t)cap * sizeof *ev); }
                    const unsigned char note = d[q], vel = (nbytes > 1) ? d[q+1] : 0;
                    ev[n].at = tick;
                    ev[n].note = note;
                    ev[n].vel = vel;
                    /* a note on with no velocity is a note off, which is how
                     * most files spell it */
                    ev[n].kind = (hi == 0x90 && vel > 0) ? 1 : 0;
                    n++;
                }
                q += nbytes;
            }
        }
    }
    free(d);
    qsort(ev, (size_t)n, sizeof *ev, cmp_mev);

    /* ticks to samples, following the tempo map as it is walked */
    long ti = 0;
    double usec_per_beat = 500000.0;   /* 120 bpm until told otherwise */
    long last_tick = 0;
    double seconds = 0.0;
    for (long i = 0; i < n; i++) {
        while (ti < tn && tt[ti] <= ev[i].at) {
            seconds += (double)(tt[ti] - last_tick) * usec_per_beat / division / 1e6;
            last_tick = tt[ti];
            usec_per_beat = (double)tv[ti];
            ti++;
        }
        seconds += (double)(ev[i].at - last_tick) * usec_per_beat / division / 1e6;
        last_tick = ev[i].at;
        ev[i].at = (long)(seconds * SR);
    }
    free(tt); free(tv);
    *out = ev;
    return n;
}

static long render_midi(const char *path, const pd_preset_t *pr, float *out, long cap)
{
    mev_t *ev = 0;
    const long n = midi_load(path, &ev);
    if (n <= 0) { free(ev); if (n == 0) fprintf(stderr, "no notes in %s\n", path); return 0; }

    poly_t poly;
    poly_init(&poly, &pr->patch);
    g_fxp = pr->fx;
    pd_fx_destroy(g_fx);
    g_fx = pd_fx_create(SR);

    const long tail = (long)(3.0 * SR);
    long total = ev[n - 1].at + tail;
    if (total > cap) total = cap;

    long e = 0;
    for (long i = 0; i < total; i++) {
        while (e < n && ev[e].at <= i) {
            if (ev[e].kind) poly_on(&poly, ev[e].note, ev[e].vel / 127.0);
            else            poly_off(&poly, ev[e].note);
            e++;
        }
        double l, r;
        poly_next2(&poly, &l, &r);
        out[i] = (float)(0.5 * (l + r));
    }
    free(ev);
    fprintf(stderr, "  %ld events, %.1f seconds\n", n, total / SR);
    return total;
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
    if (argc >= 4 && strcmp(argv[2], "--midi") == 0) {
        /* pd_render out.wav --midi song.mid [preset name] */
        const char *want = (argc > 4) ? argv[4] : "Digi Strings";
        const pd_preset_t *pr = 0;
        for (int i = 0; i < pd_preset_count(); i++)
            if (strcmp(pd_preset(i)->name, want) == 0) pr = pd_preset(i);
        if (!pr) { printf("no preset called \"%s\"\n", want); return 2; }

        long cap2 = (long)(SR * 900);
        float *buf2 = (float *)calloc((size_t)cap2, sizeof *buf2);
        if (!buf2) return 1;
        pd_fx_params_init(&g_fxp);
        fprintf(stderr, "%s, played on \"%s\"\n", argv[3], pr->name);
        long n2 = render_midi(argv[3], pr, buf2, cap2);
        if (n2 <= 0) { free(buf2); return 1; }

        double pk2 = 0;
        for (long i = 0; i < n2; i++) if (fabs(buf2[i]) > pk2) pk2 = fabs(buf2[i]);
        const double g2 = pk2 > 0 ? 0.89 / pk2 : 1.0;
        FILE *fp = fopen(argv[1], "wb");
        if (!fp) { free(buf2); return 1; }
        long bytes2 = n2 * 2;
        fwrite("RIFF", 1, 4, fp);
        uint32_t v = (uint32_t)(36 + bytes2); fwrite(&v, 4, 1, fp);
        fwrite("WAVEfmt ", 1, 8, fp);
        v = 16; fwrite(&v, 4, 1, fp);
        uint16_t w = 1; fwrite(&w, 2, 1, fp);
        w = 1; fwrite(&w, 2, 1, fp);
        v = (uint32_t)SR; fwrite(&v, 4, 1, fp);
        v = (uint32_t)SR * 2; fwrite(&v, 4, 1, fp);
        w = 2; fwrite(&w, 2, 1, fp);
        w = 16; fwrite(&w, 2, 1, fp);
        fwrite("data", 1, 4, fp);
        v = (uint32_t)bytes2; fwrite(&v, 4, 1, fp);
        for (long i = 0; i < n2; i++) {
            double s2 = buf2[i] * g2 * 32767.0;
            if (s2 > 32767) s2 = 32767;
            if (s2 < -32768) s2 = -32768;
            int16_t iv = (int16_t)lrint(s2);
            fwrite(&iv, 2, 1, fp);
        }
        fclose(fp);
        printf("wrote %s (%.1f s)\n", argv[1], n2 / SR);
        pd_fx_destroy(g_fx);
        free(buf2);
        return 0;
    }

    if (argc < 2) {
        printf("usage: pd_render out.wav [demo name | bank | --midi song.mid [preset]]\n  demos:");
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
