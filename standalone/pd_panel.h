/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The panel, drawn from a snapshot of what the synth is doing. Kept apart from
 * the app so it can also be drawn headless and looked at, rather than being
 * assumed to be right because it compiled.
 */
#ifndef PDSYNTH_PD_PANEL_H
#define PDSYNTH_PD_PANEL_H

#include <SDL2/SDL.h>
#include "pd_voice.h"

#define PD_WIN_W 900
#define PD_WIN_H 520

typedef struct {
    pd_wave_t wave[2];
    double    bend[2];      /* what each line's waveform envelope is doing now */
    int       voices;
    double    level;
    const float *scope;     /* SCOPE_N samples, oldest first */
    int       scope_len;
    const char *midi_name;  /* NULL when there is no MIDI source */
} pd_panel_state_t;

void pd_panel_draw(SDL_Renderer *r, const pd_panel_state_t *s);

#endif
