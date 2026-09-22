/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pd_panel.h"
#include "pd_font.h"


static void set_rgb(SDL_Renderer *r, int c) {
    SDL_SetRenderDrawColor(r, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, 255);
}
static void box(SDL_Renderer *r, int x, int y, int w, int h, int c) {
    set_rgb(r, c); SDL_Rect q = { x, y, w, h }; SDL_RenderFillRect(r, &q);
}
static void frame(SDL_Renderer *r, int x, int y, int w, int h, int c) {
    set_rgb(r, c); SDL_Rect q = { x, y, w, h }; SDL_RenderDrawRect(r, &q);
}

/* A five by seven stroke font, enough for a panel's worth of words. */
#include "pd_font.h"

static void text(SDL_Renderer *r, int x, int y, int scale, const char *s, int c)
{
    set_rgb(r, c);
    for (; *s; s++) {
        const unsigned char *g = pd_font_glyph(*s);
        if (g) {
            for (int col = 0; col < 5; col++)
                for (int row = 0; row < 7; row++)
                    if (g[col] & (1 << row)) {
                        SDL_Rect q = { x + col * scale, y + row * scale, scale, scale };
                        SDL_RenderFillRect(r, &q);
                    }
        }
        x += 6 * scale;
    }
}

/* the phase distortion curve itself: input phase across, bent phase up */
static void draw_bend(SDL_Renderer *r, int x, int y, int w, int h,
                      pd_wave_t wave, double amount)
{
    box(r, x, y, w, h, 0x0d1016);
    set_rgb(r, 0x243040);
    for (int i = 1; i < 4; i++) {
        SDL_RenderDrawLine(r, x + w * i / 4, y, x + w * i / 4, y + h);
        SDL_RenderDrawLine(r, x, y + h * i / 4, x + w, y + h * i / 4);
    }
    int px, py;
    if (wave >= PD_RESO_SAW) {
        /*
         * The resonant waveforms bend nothing. Drawing an undistorted ramp
         * here would say "no distortion" when in fact the sound is a sine at a
         * multiple of the note under a falling window, so draw that window
         * instead, which is the thing doing the shaping.
         */
        set_rgb(r, 0x6fd3ff);
        px = x; py = y + h;
        for (int i = 0; i <= w; i++) {
            double p = (double)i / w;
            int cx = x + i, cy = y + h - (int)(pd_window(p, wave) * (h - 2));
            if (i) SDL_RenderDrawLine(r, px, py, cx, cy);
            px = cx; py = cy;
        }
    } else {
        set_rgb(r, 0x35507a);
        SDL_RenderDrawLine(r, x, y + h, x + w, y);      /* the undistorted ramp */
        set_rgb(r, 0x6fd3ff);
        px = x; py = y + h;
        for (int i = 0; i <= w; i++) {
            double p = (double)i / w;
            int cx = x + i, cy = y + h - (int)(pd_distort(p, wave, amount) * h);
            if (i) SDL_RenderDrawLine(r, px, py, cx, cy);
            px = cx; py = cy;
        }
    }
    frame(r, x, y, w, h, 0x2a3a52);
}

/* one cycle of what that bend produces */
static void draw_wave(SDL_Renderer *r, int x, int y, int w, int h,
                      pd_wave_t wave, double amount)
{
    box(r, x, y, w, h, 0x0d1016);
    set_rgb(r, 0x243040);
    SDL_RenderDrawLine(r, x, y + h / 2, x + w, y + h / 2);

    pd_osc_t o; pd_osc_init(&o);
    pd_osc_set_freq(&o, 1.0, (double)w);
    set_rgb(r, 0x7dffc0);
    int px = x, py = y + h / 2;
    for (int i = 0; i <= w; i++) {
        double s = pd_osc_next(&o, wave, amount);
        int cx = x + i, cy = y + h / 2 - (int)(s * (h / 2 - 2));
        if (i) SDL_RenderDrawLine(r, px, py, cx, cy);
        px = cx; py = cy;
    }
    frame(r, x, y, w, h, 0x2a3a52);
}

static void draw_scope(SDL_Renderer *r, int x, int y, int w, int h,
                       const float *scope, int len)
{
    box(r, x, y, w, h, 0x0d1016);
    set_rgb(r, 0x243040);
    SDL_RenderDrawLine(r, x, y + h / 2, x + w, y + h / 2);
    set_rgb(r, 0xffd27d);
    int px = x, py = y + h / 2;
    for (int i = 0; i < w && len > 0; i++) {
        int idx = (int)((double)i / w * len);
        int cx = x + i, cy = y + h / 2 - (int)(scope[idx] * (h / 2 - 2));
        if (i) SDL_RenderDrawLine(r, px, py, cx, cy);
        px = cx; py = cy;
    }
    frame(r, x, y, w, h, 0x2a3a52);
}


void pd_panel_draw(SDL_Renderer *ren, const pd_panel_state_t *s)
{
    box(ren, 0, 0, PD_WIN_W, PD_WIN_H, 0x14171c);
    box(ren, 0, 0, PD_WIN_W, 46, 0x1b1f26);
    text(ren, 18, 15, 3, "PDSYNTH", 0xdfe6f0);
    text(ren, 190, 20, 2, "PHASE DISTORTION", 0x6fd3ff);
    text(ren, 470, 20, 2, s->midi_name ? "MIDI IN" : "KEYS ONLY", 0x8d97a8);

    for (int line = 0; line < 2; line++) {
        int y = 70 + line * 190;
        double amt = s->bend[line];
        pd_wave_t wv = s->wave[line];
        char label[48];
        snprintf(label, sizeof label, "LINE %d   %s", line + 1, pd_wave_name(wv));
        for (char *c = label; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
        text(ren, 26, y, 2, label, 0xdfe6f0);
        if (wv >= PD_RESO_SAW) {
            char rl[32];
            snprintf(rl, sizeof rl, "WINDOW  X%d", pd_resonant_harmonic(amt));
            text(ren, 26, y + 24, 1, rl, 0x8d97a8);
        } else {
            text(ren, 26, y + 24, 1, "PHASE BEND", 0x8d97a8);
        }
        draw_bend(ren, 26, y + 40, 200, 130, wv, amt);
        text(ren, 250, y + 24, 1, "WAVEFORM", 0x8d97a8);
        draw_wave(ren, 250, y + 40, 330, 130, wv, amt);
        text(ren, 604, y + 24, 1, "DCW", 0x8d97a8);
        box(ren, 604, y + 40, 26, 130, 0x0d1016);
        int hh = (int)(amt * 128);
        box(ren, 605, y + 40 + (129 - hh), 24, hh, line ? 0x7dffc0 : 0x6fd3ff);
        frame(ren, 604, y + 40, 26, 130, 0x2a3a52);
    }

    text(ren, 660, 94, 1, "OUTPUT", 0x8d97a8);
    draw_scope(ren, 660, 110, 220, 90, s->scope, s->scope_len);
    char st[64];
    snprintf(st, sizeof st, "VOICES %d", s->voices);
    text(ren, 660, 218, 1, st, 0xdfe6f0);
    box(ren, 660, 236, 220, 10, 0x0d1016);
    double lvl = s->level > 1 ? 1 : s->level;
    box(ren, 661, 237, (int)(218 * lvl), 8, 0xffd27d);

    text(ren, 660, 300, 1, "Z..M  Q..I   PLAY", 0x8d97a8);
    text(ren, 660, 316, 1, "1..8  LINE 1 WAVE", 0x8d97a8);
    text(ren, 660, 332, 1, "[  ]  LINE 2 WAVE", 0x8d97a8);
    text(ren, 660, 348, 1, "SPACE PANIC", 0x8d97a8);
    text(ren, 660, 470, 1, "NO FILTER IN THE PATH", 0x556074);
}
