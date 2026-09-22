/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <math.h>
#include <string.h>
#include "pd_voice.h"

double pd_note_to_hz(int midi_note)
{
    return 440.0 * pow(2.0, (midi_note - 69) / 12.0);
}

void pd_patch_init(pd_patch_t *p)
{
    memset(p, 0, sizeof(*p));
    p->line_count = 1;
    p->mix = PD_MIX_BOTH;
    for (int i = 0; i < 2; i++) {
        pd_line_params_t *l = &p->line[i];
        l->wave = PD_SAW;
        l->level = 1.0;
        pd_env_params_flat(&l->wave_env);
        pd_env_params_flat(&l->amp_env);
        pd_env_params_flat(&l->pitch_env);
        l->pitch_env_depth_semitones = 0.0;
    }
    p->velocity_to_level = 1.0;
    p->velocity_to_wave = 0.0;
    p->bend_range_semitones = 2.0;
    p->mod_to_wave = 0.0;
    p->spread = 0.45;
}

void pd_voice_init(pd_voice_t *v, const pd_patch_t *patch, double sample_rate)
{
    memset(v, 0, sizeof(*v));
    v->patch = patch;
    v->sample_rate = sample_rate > 0 ? sample_rate : 48000.0;
    v->inner_rate  = v->sample_rate * PD_OVERSAMPLE;
    v->noise_state = 0x1234567u;
    pd_decimator_init(&v->decim, v->sample_rate);
    /* a one pole high pass at about 8 Hz: below anything anyone plays, and
     * gentle enough that it does not touch the shape of a bass note */
    v->dc_r = 1.0 - 2.0 * 3.14159265358979323846 * 8.0 / v->sample_rate;
    v->dc_x1 = v->dc_y1 = 0.0;
    for (int i = 0; i < 2; i++) {
        pd_osc_init(&v->line[i].osc);
        /* the envelopes run at the inner rate too, or a patch would play four
         * times faster than it was written */
        pd_env_init(&v->line[i].pitch, &patch->line[i].pitch_env, v->inner_rate);
        pd_env_init(&v->line[i].wave,  &patch->line[i].wave_env,  v->inner_rate);
        pd_env_init(&v->line[i].amp,   &patch->line[i].amp_env,   v->inner_rate);
    }
}

void pd_voice_note_on(pd_voice_t *v, int midi_note, double velocity)
{
    v->note = midi_note;
    v->velocity = velocity < 0 ? 0 : (velocity > 1 ? 1 : velocity);
    v->base_hz = pd_note_to_hz(midi_note);
    v->active = 1;
    for (int i = 0; i < v->patch->line_count; i++) {
        pd_env_key_down(&v->line[i].pitch);
        pd_env_key_down(&v->line[i].wave);
        pd_env_key_down(&v->line[i].amp);
    }
}

void pd_voice_set_bend(pd_voice_t *v, double b)
{
    v->bend = b < -1.0 ? -1.0 : (b > 1.0 ? 1.0 : b);
}

void pd_voice_set_mod(pd_voice_t *v, double m)
{
    v->mod = m < 0.0 ? 0.0 : (m > 1.0 ? 1.0 : m);
}

void pd_voice_note_off(pd_voice_t *v)
{
    for (int i = 0; i < v->patch->line_count; i++) {
        pd_env_key_up(&v->line[i].pitch);
        pd_env_key_up(&v->line[i].wave);
        pd_env_key_up(&v->line[i].amp);
    }
}

/* white noise, cheap and good enough to modulate with */
static double noise(pd_voice_t *v)
{
    v->noise_state = v->noise_state * 1664525u + 1013904223u;
    return ((double)(v->noise_state >> 8) / 8388608.0) - 1.0;
}

static double run_line(pd_voice_t *v, int i, double bend_offset)
{
    const pd_line_params_t *lp = &v->patch->line[i];
    pd_line_t *l = &v->line[i];

    double pitch_env = pd_env_next(&l->pitch);
    double wave_env  = pd_env_next(&l->wave);
    double amp_env   = pd_env_next(&l->amp);

    /*
     * The pitch envelope bends the note away from where it would otherwise
     * sit, and its resting place is its own sustain level. Measuring the
     * deviation from there rather than from zero means a patch that sets a
     * depth but leaves the envelope flat plays in tune, instead of being
     * transposed by the depth for as long as the key is down.
     */
    double pitch_rest = lp->pitch_env.level[lp->pitch_env.sustain_step] / 99.0;
    double semis = lp->octave * 12.0 + lp->semitones
                 + lp->detune_cents / 100.0
                 + (pitch_env - pitch_rest) * lp->pitch_env_depth_semitones
                 + v->bend * v->patch->bend_range_semitones;
    double hz = v->base_hz * pow(2.0, semis / 12.0);
    pd_osc_set_freq(&l->osc, hz, v->inner_rate);

    /* Playing harder opens the waveform, which on a CZ is the closest thing
     * there is to opening a filter. */
    double bend = wave_env * (1.0 - v->patch->velocity_to_wave
                              + v->patch->velocity_to_wave * v->velocity);
    bend += bend_offset + v->mod * v->patch->mod_to_wave;
    if (bend < 0.0) bend = 0.0;
    if (bend > 1.0) bend = 1.0;
    double amp = amp_env * lp->level;
    amp *= (1.0 - v->patch->velocity_to_level + v->patch->velocity_to_level * v->velocity);

    return pd_osc_next(&l->osc, lp->wave, bend) * amp;
}

/* One sample at the inner rate, with the two lines kept apart so the caller
 * can place them. */
static void voice_inner_lines(pd_voice_t *v, double *outA, double *outB)
{

    /*
     * Noise modulation shakes how far the phase is bent, not how loud the line
     * is. Shaking the amplitude would be ring modulation with a noise source
     * and it hollows the note out: at full depth the tone itself is most of
     * what is removed. Shaking the bend leaves the note where it is and makes
     * its timbre restless, which is the sound the hardware had and the reason
     * anyone wants it.
     */
    double bend_noise = (v->patch->mix == PD_MIX_NOISE)
                      ? noise(v) * v->patch->noise_amount * 0.5 : 0.0;

    double a = run_line(v, 0, bend_noise);
    double b = (v->patch->line_count > 1) ? run_line(v, 1, 0.0) : 0.0;

    if (v->patch->mix == PD_MIX_RING) {
        /* the second line is a modulator rather than a voice of its own, so
         * there is only one signal to place */
        a = (v->patch->line_count > 1) ? a * b : a;
        b = 0.0;
    }

    /* a voice is done when every line it uses has finished its amplitude */
    int alive = 0;
    for (int i = 0; i < v->patch->line_count; i++)
        if (!pd_env_finished(&v->line[i].amp)) alive = 1;
    if (!alive) v->active = 0;

    *outA = a;
    *outB = b;
}

void pd_voice_next_inner(pd_voice_t *v, double *left, double *right)
{
    if (!v->active) { *left = *right = 0.0; return; }
    double a, b;
    voice_inner_lines(v, &a, &b);

    /* Equal gain either side of centre. A CZ is a mono box, but two lines
     * detuned against each other are begging to be placed apart, and a synth
     * that arrives in one spot in the middle sounds smaller than it is. */
    const double s = v->patch->spread;
    const double la = 0.5 + 0.5 * s, ra = 0.5 - 0.5 * s;
    *left  = a * la + b * ra;
    *right = a * ra + b * la;
}

double pd_voice_next(pd_voice_t *v)
{
    if (!v->active) return 0.0;
    /* Run fast, filter, keep one. Filtering every inner sample rather than
     * only the one that is kept is the whole point: what is discarded is
     * exactly what would have folded back into the audible band. */
    for (int i = 0; i < PD_OVERSAMPLE; i++) {
        double a, b;
        voice_inner_lines(v, &a, &b);
        pd_decimator_push(&v->decim, a + b);
    }
    double out = pd_decimator_read(&v->decim);

    const double hp = out - v->dc_x1 + v->dc_r * v->dc_y1;
    v->dc_x1 = out;
    v->dc_y1 = hp;
    out = hp;

    if (out > 1.0) out = 1.0;
    if (out < -1.0) out = -1.0;
    return out;
}

int pd_voice_active(const pd_voice_t *v) { return v->active; }
