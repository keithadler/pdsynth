/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The standalone: a window, a keyboard, and the two lines drawn as they move.
 *
 * The point of drawing it is that phase distortion is hard to believe from a
 * description. The panel shows the bend as a curve, live, next to the waveform
 * it produces, so it is plain that one sine table is being read through a
 * changing phase and nothing is being filtered.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "pd_voice.h"
#include "pd_midi.h"
#include "pd_panel.h"

#define SR        48000
#define POLYPHONY 10
#define WIN_W     900
#define WIN_H     520
#define SCOPE_N   600

typedef struct {
    pd_voice_t v[POLYPHONY];
    pd_patch_t patch;
    SDL_mutex *lock;
    float      scope[SCOPE_N];
    int        scope_pos;
    double     level;
    int        active_voices;
} synth_t;

static synth_t g;

/* ---- audio ------------------------------------------------------------- */

static void audio_cb(void *user, Uint8 *stream, int len)
{
    (void)user;
    float *out = (float *)stream;
    int frames = len / (int)sizeof(float);

    SDL_LockMutex(g.lock);
    int active = 0;
    for (int i = 0; i < POLYPHONY; i++) if (pd_voice_active(&g.v[i])) active++;
    g.active_voices = active;

    for (int i = 0; i < frames; i++) {
        double s = 0;
        for (int j = 0; j < POLYPHONY; j++) s += pd_voice_next(&g.v[j]);
        s *= 0.25;
        if (s > 1.0) s = 1.0;
        if (s < -1.0) s = -1.0;
        out[i] = (float)s;
        g.scope[g.scope_pos] = (float)s;
        g.scope_pos = (g.scope_pos + 1) % SCOPE_N;
        double a = fabs(s);
        g.level = a > g.level ? a : g.level * 0.9995;
    }
    SDL_UnlockMutex(g.lock);
}

/* ---- notes ------------------------------------------------------------- */

static void note_on(int note, double vel)
{
    SDL_LockMutex(g.lock);
    for (int i = 0; i < POLYPHONY; i++)
        if (!pd_voice_active(&g.v[i])) { pd_voice_note_on(&g.v[i], note, vel); SDL_UnlockMutex(g.lock); return; }
    pd_voice_note_on(&g.v[0], note, vel);
    SDL_UnlockMutex(g.lock);
}
static void note_off(int note)
{
    SDL_LockMutex(g.lock);
    for (int i = 0; i < POLYPHONY; i++)
        if (pd_voice_active(&g.v[i]) && g.v[i].note == note) pd_voice_note_off(&g.v[i]);
    SDL_UnlockMutex(g.lock);
}
static void all_off(void)
{
    SDL_LockMutex(g.lock);
    for (int i = 0; i < POLYPHONY; i++) pd_voice_note_off(&g.v[i]);
    SDL_UnlockMutex(g.lock);
}

/* MIDI arrives on another thread; the same lock covers it */
static void on_midi(void *user, const unsigned char *msg, int len)
{
    (void)user;
    if (len < 2) return;
    int status = msg[0] & 0xf0;
    if (status == 0x90 && len >= 3 && msg[2] > 0)      note_on(msg[1], msg[2] / 127.0);
    else if (status == 0x80 || (status == 0x90 && len >= 3)) note_off(msg[1]);
    else if (status == 0xb0 && len >= 3 && msg[1] == 123) all_off();
}

/* ---- the computer keyboard as two octaves ------------------------------ */

static int key_to_note(SDL_Keycode k)
{
    switch (k) {
    case SDLK_z: return 48; case SDLK_s: return 49; case SDLK_x: return 50;
    case SDLK_d: return 51; case SDLK_c: return 52; case SDLK_v: return 53;
    case SDLK_g: return 54; case SDLK_b: return 55; case SDLK_h: return 56;
    case SDLK_n: return 57; case SDLK_j: return 58; case SDLK_m: return 59;
    case SDLK_q: return 60; case SDLK_2: return 61; case SDLK_w: return 62;
    case SDLK_3: return 63; case SDLK_e: return 64; case SDLK_r: return 65;
    case SDLK_5: return 66; case SDLK_t: return 67; case SDLK_6: return 68;
    case SDLK_y: return 69; case SDLK_7: return 70; case SDLK_u: return 71;
    case SDLK_i: return 72;
    default: return -1;
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    g.lock = SDL_CreateMutex();

    pd_patch_init(&g.patch);
    g.patch.line_count = 2;
    g.patch.line[0].wave = PD_SAW;       g.patch.line[0].detune_cents = -7; g.patch.line[0].level = 0.55;
    g.patch.line[1].wave = PD_RESO_SAW;  g.patch.line[1].detune_cents = +7; g.patch.line[1].level = 0.45;
    g.patch.velocity_to_wave = 0.5;
    for (int i = 0; i < 2; i++) {
        pd_env_params_t *w = &g.patch.line[i].wave_env, *a = &g.patch.line[i].amp_env;
        memset(w, 0, sizeof *w); memset(a, 0, sizeof *a);
        w->rate[0] = 52; w->level[0] = 99;
        w->rate[1] = 34; w->level[1] = 28;
        w->rate[2] = 40; w->level[2] = 0;
        w->sustain_step = 1; w->end_step = 2;
        a->rate[0] = 76; a->level[0] = 99;
        a->rate[1] = 46; a->level[1] = 82;
        a->rate[2] = 48; a->level[2] = 0;
        a->sustain_step = 1; a->end_step = 2;
    }
    for (int i = 0; i < POLYPHONY; i++) pd_voice_init(&g.v[i], &g.patch, SR);

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SR; want.format = AUDIO_F32SYS; want.channels = 1;
    want.samples = 256; want.callback = audio_cb;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) { fprintf(stderr, "audio: %s\n", SDL_GetError()); return 1; }
    SDL_PauseAudioDevice(dev, 0);

    const char *midi_name = pd_midi_open(on_midi, NULL);
    printf("pdsynth standalone\n  audio: %d Hz\n  MIDI: %s\n",
           have.freq, midi_name ? midi_name : "(none found; use the computer keyboard)");
    printf("  keys: z..m and q..i play, 1-8 pick line 1's waveform,\n"
           "        [ and ] pick line 2's, space is panic\n");

    SDL_Window *win = SDL_CreateWindow("pdsynth", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Keycode k = e.key.keysym.sym;
                int n = key_to_note(k);
                if (n >= 0) note_on(n, 0.85);
                else if (k == SDLK_SPACE) all_off();
                else if (k == SDLK_ESCAPE) running = 0;
                else if (k >= SDLK_1 && k <= SDLK_8 && !(k >= SDLK_2 && k <= SDLK_7 && key_to_note(k) >= 0))
                    g.patch.line[0].wave = (pd_wave_t)(k - SDLK_1);
                else if (k == SDLK_LEFTBRACKET)
                    g.patch.line[1].wave = (pd_wave_t)((g.patch.line[1].wave + PD_WAVE_COUNT - 1) % PD_WAVE_COUNT);
                else if (k == SDLK_RIGHTBRACKET)
                    g.patch.line[1].wave = (pd_wave_t)((g.patch.line[1].wave + 1) % PD_WAVE_COUNT);
            } else if (e.type == SDL_KEYUP) {
                int n = key_to_note(e.key.keysym.sym);
                if (n >= 0) note_off(n);
            }
        }

        /* what the first voice's waveform envelope is doing right now */
        double bend0, bend1;
        SDL_LockMutex(g.lock);
        bend0 = g.v[0].line[0].wave.value;
        bend1 = g.v[0].line[1].wave.value;
        int voices = g.active_voices;
        double lvl = g.level;
        SDL_UnlockMutex(g.lock);

        static float snap[SCOPE_N];
        SDL_LockMutex(g.lock);
        for (int i = 0; i < SCOPE_N; i++) snap[i] = g.scope[(g.scope_pos + i) % SCOPE_N];
        SDL_UnlockMutex(g.lock);

        pd_panel_state_t ps = {
            .wave  = { g.patch.line[0].wave, g.patch.line[1].wave },
            .bend  = { bend0, bend1 },
            .voices = voices,
            .level  = lvl,
            .scope  = snap,
            .scope_len = SCOPE_N,
            .midi_name = midi_name,
        };
        pd_panel_draw(ren, &ps);

        SDL_RenderPresent(ren);
    }

    pd_midi_close();
    SDL_CloseAudioDevice(dev);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_DestroyMutex(g.lock);
    SDL_Quit();
    return 0;
}
