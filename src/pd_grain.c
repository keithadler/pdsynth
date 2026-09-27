/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Ported from the asynchronous granular oscillator in ZedSynth:
 *   Copyright (C) 2005 Sean Bolton and others.
 *   Portions of that file come from MSS, copyright (C) 2002 Mats Olsson.
 *
 * What is theirs: the five window shapes and what `edge` means in each, the
 * overlap parameter that sets density by dividing the grain length, the onset
 * jitter, and starting a grain at the phase a continuous oscillator would have
 * reached rather than at a random one. That last is the load bearing idea. A
 * random phase per grain makes a cloud that hisses and has no pitch; taking
 * the phase from a virtual oscillator that keeps running whether or not a
 * grain is sounding is what leaves the note where the player put it.
 *
 * What is different here, and why:
 *
 * The windows are computed rather than tabulated. ZedSynth builds 31 envelope
 * buffers at a fixed set of lengths and shapes, mallocs each one, and picks
 * among them with an integer. That means grain length comes in 31 steps, a
 * shape change mid-grain can leave a grain reading past the end of its
 * envelope, which their code has to check for on every block, and the whole
 * table is rebuilt when the sample rate changes. Computing the window from a
 * position between 0 and 1 makes length and edge continuous, removes the
 * allocation, and removes that check because there is nothing to read past.
 * The Gaussian shoulders use one shared 1025 entry table of exp(-u*u/2), which
 * is the only transcendental left in the loop.
 *
 * Each cloud owns its grains. ZedSynth keeps one pool for the whole synth on a
 * freelist, so a voice can find the pool empty and drop grains because other
 * voices are holding them, which its own source comments on. A fixed array per
 * cloud cannot starve that way. It can still fill up, so that is counted.
 *
 * The per grain detune is centered, and theirs is not. ZedSynth draws
 * `random_float(-1, 2)`, squares it keeping the sign, and multiplies the
 * frequency by 1 + dist * that. The draw is asymmetric, so the mean is above
 * zero before the squaring and further above it after, and the cloud drifts
 * sharp as the spread opens; the source carries a `-FIX- does not center on
 * frequency` note about exactly this. Here the draw is symmetric and the
 * detune is in cents, so the cloud spreads around the note instead of away
 * from it. test_grain measures the median pitch against the note to hold it.
 */
#include <math.h>
#include <string.h>

#include "pd_grain.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const char *pd_grain_shape_name(pd_grain_shape_t s)
{
    switch (s) {
    case PD_GRAIN_RECT:      return "rectangular";
    case PD_GRAIN_TRAPEZOID: return "trapezoid";
    case PD_GRAIN_TRIANGLE:  return "triangle";
    case PD_GRAIN_GAUSSIAN:  return "gaussian";
    case PD_GRAIN_ROADSIAN:  return "roadsian";
    default:                 return "unknown";
    }
}

void pd_grain_params_init(pd_grain_params_t *g)
{
    memset(g, 0, sizeof(*g));
    g->on           = 0;
    g->shape        = PD_GRAIN_ROADSIAN;
    g->edge         = 0.25;
    g->length_ms    = 40.0;
    g->overlap      = 6.0;
    g->onset_spread = 0.4;
    g->pitch_spread = 0.0;
}

/* ------------------------------------------------------------------------- *
 * The Gaussian shoulder, tabulated once.
 *
 * exp(-u*u/2) for u from 0 to GAUSS_MAX, where GAUSS_MAX is where the curve
 * reaches -96 dB and a window may as well be closed. ZedSynth uses the same
 * 4.70158 for its Roadsian shoulders and arrives at it the same way.
 *
 * Built by the first pd_cloud_init, which happens when a voice is set up and
 * not on the audio thread. Two threads racing here would both write the same
 * numbers, so the race is harmless, but the flag is set last so nobody reads a
 * half filled table.
 * ------------------------------------------------------------------------- */
#define GAUSS_N   1024
#define GAUSS_MAX 4.70158

static double gauss_tab[GAUSS_N + 1];
static volatile int gauss_ready = 0;

static void gauss_init(void)
{
    for (int i = 0; i <= GAUSS_N; i++) {
        const double u = GAUSS_MAX * (double)i / (double)GAUSS_N;
        gauss_tab[i] = exp(-u * u * 0.5);
    }
    gauss_ready = 1;
}

/* exp(-u*u/2), linearly interpolated, clamped to zero past the table.
 *
 * The flag is checked here rather than trusting a caller to have run
 * pd_cloud_init: pd_grain_window is public, and a public function that returns
 * zero unless something else was called first is a trap. That is not
 * hypothetical, it is what the first run of test_grain found, and every
 * Gaussian window in it measured silent. */
static double gauss(double u)
{
    if (!gauss_ready) gauss_init();
    if (u < 0.0) u = -u;
    if (u >= GAUSS_MAX) return 0.0;
    const double x = u * (double)GAUSS_N / GAUSS_MAX;
    const int    i = (int)x;
    const double f = x - (double)i;
    return gauss_tab[i] + f * (gauss_tab[i + 1] - gauss_tab[i]);
}

/* ------------------------------------------------------------------------- *
 * The windows.
 *
 * `t` runs 0 to 1 across the grain. Every shape but RECT is 0 at both ends,
 * because a window that is not is a click, and clicks in a cloud arrive at the
 * grain rate and sound like a fault rather than a texture.
 * ------------------------------------------------------------------------- */
/*
 * The shortest taper any shape is allowed, as a fraction of the grain.
 *
 * Without a floor, edge 0 turns the trapezoid and the Roadsian into the
 * rectangle and the triangle into a sawtooth, all of which start or end on a
 * step, which is a click at the grain rate. pdsynth already has a shape for
 * that and calls it rectangular; a player who picks one of the others has
 * asked for a window. At 40 ms this is about ten samples, which is enough.
 */
#define PD_GRAIN_MIN_TAPER 0.005

double pd_grain_window(pd_grain_shape_t shape, double edge, double t)
{
    if (t <= 0.0 || t >= 1.0) return shape == PD_GRAIN_RECT ? 1.0 : 0.0;
    if (edge < 0.0) edge = 0.0;
    if (edge > 1.0) edge = 1.0;

    switch (shape) {
    case PD_GRAIN_RECT:
        return 1.0;

    case PD_GRAIN_TRAPEZOID: {
        /* Rise over the first `edge` of the grain, fall over the last. Half is
         * the most that leaves a flat part, and at half it is a triangle,
         * which is the identity ZedSynth's own notes point out. */
        double e = edge * 0.5;
        if (e < PD_GRAIN_MIN_TAPER) e = PD_GRAIN_MIN_TAPER;
        if (t < e)       return t / e;
        if (t > 1.0 - e) return (1.0 - t) / e;
        return 1.0;
    }

    case PD_GRAIN_TRIANGLE: {
        /* `edge` is where the peak sits, so 0.1 is a percussive grain and 0.9
         * is one that swells and stops. */
        double peak = edge;
        if (peak < PD_GRAIN_MIN_TAPER)       peak = PD_GRAIN_MIN_TAPER;
        if (peak > 1.0 - PD_GRAIN_MIN_TAPER) peak = 1.0 - PD_GRAIN_MIN_TAPER;
        return t < peak ? t / peak : (1.0 - t) / (1.0 - peak);
    }

    case PD_GRAIN_GAUSSIAN: {
        /*
         * A bell reaching `min` at both ends, where min comes from edge. A
         * Gaussian never actually reaches zero, so it is shifted down by its
         * own end value and renormalized: otherwise the ends sit at `min` and
         * the grain clicks at exactly the level the control asked for.
         */
        const double min   = 1e-4 + (1.0 - 1e-4) * (edge * edge) * 0.5;
        const double scale = 2.0 * sqrt(-2.0 * log(min));
        const double u     = (t - 0.5) * scale;
        const double raw   = gauss(u);
        const double ends  = gauss(0.5 * scale);
        if (ends >= 1.0) return 0.0;
        return (raw - ends) / (1.0 - ends);
    }

    case PD_GRAIN_ROADSIAN: {
        /* Flat, with Gaussian shoulders `edge` wide at each end. */
        double e = edge * 0.5;
        if (e < PD_GRAIN_MIN_TAPER) e = PD_GRAIN_MIN_TAPER;
        if (t < e)       return gauss((1.0 - t / e) * GAUSS_MAX);
        if (t > 1.0 - e) return gauss((1.0 - (1.0 - t) / e) * GAUSS_MAX);
        return 1.0;
    }

    default:
        return 1.0;
    }
}

/* ------------------------------------------------------------------------- *
 * The cloud
 * ------------------------------------------------------------------------- */

void pd_cloud_init(pd_cloud_t *c, uint32_t seed)
{
    if (!gauss_ready) gauss_init();
    memset(c, 0, sizeof(*c));
    /* Zero is a fixed point of xorshift, and a cloud that never scatters is
     * not a cloud. */
    c->rng = seed ? seed : 0x9e3779b9u;
    c->next_onset = 0.0;   /* the first grain starts at once */
}

static double rnd(pd_cloud_t *c)   /* 0 to 1 */
{
    uint32_t x = c->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    c->rng = x;
    return (double)(x >> 8) / 16777216.0;
}

/* -1 to 1, weighted toward the middle. The signed square is ZedSynth's, and
 * it is a good idea: it puts most grains near the note and a few far from it,
 * which is what a spread control should feel like. The symmetry is not
 * theirs. */
static double rnd_centered(pd_cloud_t *c)
{
    const double r = rnd(c) * 2.0 - 1.0;
    return r * fabs(r);
}

int pd_cloud_live(const pd_cloud_t *c) { return c->live; }

/*
 * Keeping the density control from doubling as a volume control.
 *
 * My first attempt divided by the square root of the overlap, on the reasoning
 * that grains sum like noise. They do not, and the test caught it: sweeping
 * overlap from 1 to 16 moved the level by 10.8 dB. Every grain is cut from the
 * same virtual oscillator, so with no pitch spread they are all the same
 * frequency at a continuous phase, and they add like copies of one signal
 * rather than like independent sources. Copies add in proportion to how many
 * there are, not to the square root.
 *
 * So the divisor is the sum of the window values that are expected to be
 * sounding, which is the overlap times the window's own mean. Expected rather
 * than the running total, because dividing by the running total would turn a
 * thin cloud into a loud one and pump on every grain boundary.
 *
 * Pitch spread is what breaks the coherence, and once broken the sum really is
 * the square root again, so the exponent slides between the two. Nothing here
 * runs per sample unless a control has moved.
 */
static double cloud_norm(pd_cloud_t *c, const pd_grain_params_t *g, double overlap)
{
    double spread = g->pitch_spread;
    if (spread < 0.0) spread = 0.0;
    if (spread > 1.0) spread = 1.0;

    if (c->norm > 0.0
        && c->n_shape        == (double)g->shape
        && c->n_edge         == g->edge
        && c->n_overlap      == overlap
        && c->n_pitch_spread == spread)
        return c->norm;

    /* the window's mean, by integration; 256 points is plenty for shapes this
     * smooth and this runs only when a knob moves */
    double wmean = 0.0;
    for (int i = 0; i < 256; i++)
        wmean += pd_grain_window(g->shape, g->edge, ((double)i + 0.5) / 256.0);
    wmean /= 256.0;
    if (wmean < 1e-6) wmean = 1e-6;

    const double sum = overlap * wmean;
    /* 1.0 while the grains are copies of each other, 0.5 once they are not */
    const double exponent = 1.0 - 0.5 * spread;
    double norm = pow(sum > 1e-9 ? sum : 1e-9, exponent);
    if (norm < 1e-6) norm = 1e-6;

    c->norm = norm;
    c->n_shape = (double)g->shape;
    c->n_edge = g->edge;
    c->n_overlap = overlap;
    c->n_pitch_spread = spread;
    return norm;
}

double pd_cloud_next(pd_cloud_t *c, const pd_grain_params_t *g,
                     pd_wave_t wave, double bend,
                     double increment, double sample_rate)
{
    /* The window length in samples. Clamped so a grain is always at least a
     * couple of samples long: a one sample grain is an impulse and the window
     * cannot do anything about its ends. */
    double len_ms = g->length_ms;
    if (len_ms < 0.05) len_ms = 0.05;
    if (len_ms > 1000.0) len_ms = 1000.0;
    double env_len = len_ms * 0.001 * sample_rate;
    if (env_len < 4.0) env_len = 4.0;

    double overlap = g->overlap;
    if (overlap < 0.01) overlap = 0.01;
    if (overlap > (double)PD_MAX_GRAINS) overlap = (double)PD_MAX_GRAINS;

    double spread = g->onset_spread;
    if (spread < 0.0) spread = 0.0;
    if (spread > 1.0) spread = 1.0;

    /*
     * Start whatever grains are due this sample.
     *
     * `overlap` is grains sounding at once, so the gap between onsets is the
     * grain length divided by it. That is ZedSynth's lz, and expressing
     * density this way rather than as a rate is the reason the sound holds
     * together when grain length is swept: the overlap stays put and only the
     * grain size changes.
     */
    c->next_onset -= 1.0;
    int guard = PD_MAX_GRAINS + 1;   /* one sample cannot start more than the pool */
    while (c->next_onset <= 0.0 && guard-- > 0) {
        int slot = -1;
        for (int i = 0; i < PD_MAX_GRAINS; i++)
            if (!c->grain[i].used) { slot = i; break; }

        if (slot < 0) {
            c->starved++;
        } else {
            pd_grain_t *gr = &c->grain[slot];
            gr->used    = 1;
            gr->env_pos = 0.0;
            /* Cut from the oscillator that keeps running underneath. This is
             * what gives the cloud a pitch rather than a hiss. */
            gr->phase   = c->vphase;
            if (g->pitch_spread > 0.0) {
                const double cents = g->pitch_spread * PD_GRAIN_MAX_CENTS
                                   * rnd_centered(c);
                gr->inc_ratio = pow(2.0, cents / 1200.0);
            } else {
                gr->inc_ratio = 1.0;
            }
        }

        double step = env_len / overlap;
        if (spread > 0.0) step *= 1.0 + spread * rnd_centered(c) * 0.9;
        if (step < 1.0) step = 1.0;   /* never more than one grain per sample */
        c->next_onset += step;
    }

    /* Render every grain that is sounding. */
    double out = 0.0;
    int live = 0;
    for (int i = 0; i < PD_MAX_GRAINS; i++) {
        pd_grain_t *gr = &c->grain[i];
        if (!gr->used) continue;

        const double t   = gr->env_pos / env_len;
        const double win = pd_grain_window(g->shape, g->edge, t);
        const double inc = increment * gr->inc_ratio;

        out += pd_osc_at(gr->phase, inc, wave, bend) * win;

        gr->phase += inc;
        if (gr->phase >= 1.0) gr->phase -= floor(gr->phase);
        gr->env_pos += 1.0;
        if (gr->env_pos >= env_len) gr->used = 0;
        else live++;
    }
    c->live = live;

    c->vphase += increment;
    if (c->vphase >= 1.0) c->vphase -= floor(c->vphase);

    return out / cloud_norm(c, g, overlap);
}
