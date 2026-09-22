/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Draws the panel headless and writes a BMP, so what the window looks like can
 * be checked rather than assumed. The synth runs behind it for a moment first,
 * so the scope and the envelope bars show something real.
 *
 *     pd_shot out.bmp [bend 0..1]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "pd_panel.h"

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: pd_shot out.bmp [bend 0..1]\n"); return 2; }
    double bend = argc > 2 ? atof(argv[2]) : -1.0;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }

    /* run a note through a real voice so the panel has real numbers on it */
    pd_patch_t patch;
    pd_patch_init(&patch);
    patch.line_count = 2;
    patch.line[0].wave = PD_SAW;      patch.line[0].detune_cents = -7;
    patch.line[1].wave = PD_RESO_SAW; patch.line[1].detune_cents = +7;
    for (int i = 0; i < 2; i++) {
        pd_env_params_t *w = &patch.line[i].wave_env, *a = &patch.line[i].amp_env;
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
    pd_voice_t v;
    pd_voice_init(&v, &patch, 48000.0);
    pd_voice_note_on(&v, 52, 0.95);

    static float scope[600];
    for (int i = 0; i < 48000 / 12; i++) pd_voice_next(&v);      /* let it settle */
    for (int i = 0; i < 600; i++) scope[i] = (float)pd_voice_next(&v) * 1.8f;

    pd_panel_state_t s = {
        .wave = { patch.line[0].wave, patch.line[1].wave },
        .bend = { bend >= 0 ? bend : v.line[0].wave.value,
                  bend >= 0 ? bend : v.line[1].wave.value },
        .voices = 1,
        .level = 0.62,
        .scope = scope,
        .scope_len = 600,
        .midi_name = "keys",
    };

    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, PD_WIN_W, PD_WIN_H, 32,
                                                       SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *ren = SDL_CreateSoftwareRenderer(surf);
    pd_panel_draw(ren, &s);
    SDL_RenderPresent(ren);

    if (SDL_SaveBMP(surf, argv[1]) != 0) { fprintf(stderr, "save: %s\n", SDL_GetError()); return 1; }
    printf("wrote %s  (line 1 bend %.2f, line 2 bend %.2f)\n",
           argv[1], s.bend[0], s.bend[1]);
    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    SDL_Quit();
    return 0;
}
