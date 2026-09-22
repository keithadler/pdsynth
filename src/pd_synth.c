/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <string.h>
#include "pd_synth.h"

void pd_synth_init(pd_synth_t *s, const pd_patch_t *patch, double sample_rate, int voices)
{
    memset(s, 0, sizeof(*s));
    s->patch = patch;
    s->sample_rate = sample_rate > 0 ? sample_rate : 48000.0;
    s->voice_count = voices < 1 ? 1 : (voices > PD_MAX_VOICES ? PD_MAX_VOICES : voices);
    for (int i = 0; i < s->voice_count; i++) pd_voice_init(&s->voice[i], patch, s->sample_rate);
    pd_decimator_init(&s->decim_l, s->sample_rate);
    pd_decimator_init(&s->decim_r, s->sample_rate);
}

void pd_synth_note_on(pd_synth_t *s, int note, double velocity)
{
    int pick = -1;
    for (int i = 0; i < s->voice_count; i++)
        if (!pd_voice_active(&s->voice[i])) { pick = i; break; }
    if (pick < 0) {
        /* steal the one that has been sounding longest, which is the least
         * likely to be missed */
        pick = 0;
        for (int i = 1; i < s->voice_count; i++)
            if (s->started[i] < s->started[pick]) pick = i;
    }
    pd_voice_set_bend(&s->voice[pick], s->bend);
    pd_voice_set_mod(&s->voice[pick], s->mod);
    pd_voice_set_pressure(&s->voice[pick], s->pressure);
    pd_voice_note_on(&s->voice[pick], note, velocity);
    s->started[pick] = ++s->stamp;
}

void pd_synth_note_off(pd_synth_t *s, int note)
{
    for (int i = 0; i < s->voice_count; i++)
        if (pd_voice_active(&s->voice[i]) && s->voice[i].note == note)
            pd_voice_note_off(&s->voice[i]);
}

void pd_synth_all_off(pd_synth_t *s)
{
    for (int i = 0; i < s->voice_count; i++) pd_voice_note_off(&s->voice[i]);
}

void pd_synth_set_bend(pd_synth_t *s, double b)
{
    s->bend = b;
    for (int i = 0; i < s->voice_count; i++) pd_voice_set_bend(&s->voice[i], b);
}
void pd_synth_set_mod(pd_synth_t *s, double m)
{
    s->mod = m;
    for (int i = 0; i < s->voice_count; i++) pd_voice_set_mod(&s->voice[i], m);
}

void pd_synth_set_pressure(pd_synth_t *s, double p)
{
    s->pressure = p;
    for (int i = 0; i < s->voice_count; i++) pd_voice_set_pressure(&s->voice[i], p);
}

void pd_synth_render(pd_synth_t *s, double *left, double *right)
{
    for (int k = 0; k < PD_OVERSAMPLE; k++) {
        double l = 0, r = 0;
        for (int i = 0; i < s->voice_count; i++) {
            double vl, vr;
            pd_voice_next_inner(&s->voice[i], &vl, &vr);
            l += vl; r += vr;
        }
        pd_decimator_push(&s->decim_l, l);
        pd_decimator_push(&s->decim_r, r);
    }
    *left  = pd_decimator_read(&s->decim_l);
    *right = pd_decimator_read(&s->decim_r);
}

int pd_synth_active(const pd_synth_t *s)
{
    for (int i = 0; i < s->voice_count; i++)
        if (pd_voice_active(&s->voice[i])) return 1;
    return 0;
}
