/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Does a preset behave like the thing it is named after?
 *
 * The bank check asks whether a preset is broken. This asks a harder question,
 * and the one that actually decides whether a bank is worth playing: a marimba
 * that sustains for four seconds is not a marimba however clean it is, and a
 * brass patch whose brightness does not arrive after the note is a sawtooth
 * with an envelope on it.
 *
 * The targets are not taste. They come from how the instruments work:
 *
 *   marimba, vibes    a struck bar rings and then stops. Rosewood bars are
 *                     tuned so the first overtone is four times the
 *                     fundamental, which is why a marimba reads as hollow.
 *   brass             the tone brightens AFTER the note starts, because the
 *                     player's air takes time to excite the upper modes. That
 *                     lag is the single most recognisable thing about brass.
 *   strings, pads     a slow start, and two sources close enough in pitch to
 *                     beat against each other a few times a second.
 *   bells             partials that are not whole multiples of anything, and
 *                     a long decay.
 *   organ             no transient at either end and no change in between: it
 *                     is a switch, not a shape.
 *   plucked           bright at the start, gone within a second or so, and
 *                     duller as it decays because the high partials die first.
 *   wind              very little above the fundamental, and a start that is
 *                     soft rather than instant.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pd_presets.h"


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define SR 48000.0
#define N  (int)(SR * 3)
static double buf[N];
static int    used;

static void play(const pd_patch_t *p, int note, double vel, double hold)
{
    pd_voice_t v;
    pd_voice_init(&v, p, SR);
    pd_voice_note_on(&v, note, vel);
    const int off = (int)(hold * SR);
    used = N;
    for (int i = 0; i < N; i++) {
        if (i == off) pd_voice_note_off(&v);
        buf[i] = pd_voice_next(&v);
    }
}

static double rms_at(double sec, double win)
{
    int a = (int)(sec * SR), b = a + (int)(win * SR);
    if (b > used) b = used;
    if (a >= b) return 0;
    double s = 0;
    for (int i = a; i < b; i++) s += buf[i] * buf[i];
    return sqrt(s / (b - a));
}
static double mag_at(double hz, double from, double win)
{
    int a = (int)(from * SR), n = (int)(win * SR);
    if (a + n > used) n = used - a;
    if (n < 256) return 0;
    double re = 0, im = 0, w = 2 * M_PI * hz / SR;
    for (int i = 0; i < n; i++) {
        double h = 0.5 - 0.5 * cos(2 * M_PI * i / n);
        re += buf[a + i] * h * cos(w * i);
        im += buf[a + i] * h * sin(w * i);
    }
    return sqrt(re * re + im * im) / n;
}
/* brightness relative to the note, so it can be compared across pitches */
static double bright(double f0, double from, double win)
{
    double num = 0, den = 0;
    for (int k = 1; k <= 60; k++) {
        double m = mag_at(k * f0, from, win);
        num += m * k; den += m;
    }
    return den > 0 ? num / den : 1.0;
}
/*
 * How much of the sound is not a harmonic of the note.
 *
 * Phase distortion makes a harmonic series by construction: bend the phase of
 * a sine however you like and every partial still lands on a multiple of the
 * note. Metal does not behave that way, and a waterphone least of all. So for
 * anything claiming to be struck or bowed metal, the question is whether
 * there is anything at all sitting between the harmonics. This compares what
 * is halfway between neighbouring harmonics with what is on them; a harmonic
 * sound has almost nothing there, and a bell has plenty.
 */
static double between_harmonics(const pd_patch_t *p, double f0,
                                double from, double win)
{
    /* Measured against the note the preset actually sounds, not against the
     * note that was pressed. A patch transposed down an octave has its odd
     * harmonics sitting exactly halfway between the multiples of the pressed
     * note, so probing there reads a perfectly harmonic bass as though it
     * were a bell. The first version of this did precisely that and made a
     * fretless bass look more inharmonic than a tubular bell. */
    const double semis = p->line[0].octave * 12.0 + p->line[0].semitones;
    const double f = f0 * pow(2.0, semis / 12.0);

    /* Nothing divided by nothing is not an answer. A preset that has already
     * died by this point in the note read 0.3448 here, the same number for
     * two different presets, which is what dividing one floating point zero
     * by another gets you. That number is above the threshold this is
     * compared against, so silence used to pass the test. */
    if (rms_at(from, win) < 1e-5)
        return 0.0;

    double on = 0, between = 0;
    for (int k = 1; k <= 40; k++) {
        on += mag_at(k * f, from, win);
        between += mag_at((k + 0.5) * f, from, win);
    }
    return on > 0 ? between / on : 0.0;
}

static double attack_ms(void)
{
    double pk = 0;
    for (int i = 0; i < (int)(0.4 * SR) && i < used; i++) if (fabs(buf[i]) > pk) pk = fabs(buf[i]);
    for (int i = 0; i < used; i++) if (fabs(buf[i]) >= pk * 0.9) return i / SR * 1000.0;
    return -1;
}

static int failures;
static void want(int ok, const char *name, const char *why, double got, double lo, double hi)
{
    if (ok) return;
    failures++;
    printf("  %-14s %-46s got %.2f, want %.2f to %.2f\n", name, why, got, lo, hi);
}

int main(void)
{
    const double f0 = 261.6256;   /* middle C, the note everything is judged at */

    printf("character: does each preset behave like the thing it is named after\n\n");
    for (int i = 0; i < pd_preset_count(); i++) {
        const pd_preset_t *pr = pd_preset(i);
        const char *f = pr->family, *n = pr->name;

        play(&pr->patch, 60, 0.95, 1.2);
        const double a_ms  = attack_ms();
        const double early = rms_at(0.03, 0.05);
        const double at1   = early > 0 ? rms_at(1.00, 0.05) / early : 0;
        const double at2   = early > 0 ? rms_at(2.00, 0.05) / early : 0;
        const double b_on  = bright(f0, 0.010, 0.06);
        const double b_mid = bright(f0, 0.30, 0.10);

        if (!strcmp(f, "Mallet")) {
            /* a struck bar rings and stops, and it starts bright */
            want(at1 < 0.35, n, "a struck bar should be well down after a second", at1, 0.0, 0.35);
            want(at2 < 0.10, n, "and near gone after two", at2, 0.0, 0.10);
            want(a_ms >= 0 && a_ms < 30, n, "struck means an immediate attack (ms)", a_ms, 0.0, 30.0);
            want(b_on > b_mid * 1.25, n, "and duller as it decays", b_on / (b_mid > 0 ? b_mid : 1), 1.25, 99.0);
        } else if (!strcmp(f, "Brass")) {
            /* the lag: the note arrives, then the tone brightens */
            double b_10 = bright(f0, 0.010, 0.04), b_120 = bright(f0, 0.12, 0.08);
            want(b_120 > b_10 * 1.15, n, "brass brightens AFTER the note starts", b_120 / (b_10 > 0 ? b_10 : 1), 1.15, 99.0);
            want(at1 > 0.45, n, "and holds while the key is down", at1, 0.45, 9.9);
        } else if (!strcmp(f, "Pluck")) {
            want(at1 < 0.45, n, "a plucked note is gone within about a second", at1, 0.0, 0.45);
            want(a_ms >= 0 && a_ms < 30, n, "and starts at once (ms)", a_ms, 0.0, 30.0);
        } else if (!strcmp(f, "Strings") || !strcmp(f, "Pad") || !strcmp(f, "Vocal")) {
            want(at1 > 0.40, n, "should still be sounding after a second", at1, 0.40, 9.9);
            {
                want(a_ms > 25, n, "bowed and sung things do not start instantly (ms)", a_ms, 25.0, 9999.0);
                /* beating: two sources close in pitch make the level wobble */
                double lo = 9e9, hi = 0;
                for (double t = 0.4; t < 1.1; t += 0.05) {
                    double r = rms_at(t, 0.05);
                    if (r > hi) hi = r;
                    if (r < lo && r > 1e-7) lo = r;
                }
                double wob = (lo > 0) ? 20 * log10(hi / lo) : 0;
                want(wob > 0.5, n, "two detuned sources should beat against each other (dB)", wob, 0.5, 99.0);
            }
        } else if (!strcmp(f, "Bell")) {
            want(at1 > 0.20, n, "a bell rings on", at1, 0.20, 9.9);
            want(b_on > 3.0, n, "and is rich at the strike", b_on, 3.0, 99.0);
        } else if (!strcmp(f, "Water")) {
            /* The defining trait, and the hard one: a waterphone's partials
             * are not harmonics. Anything a bent sine does on its own would
             * fail this, which is the point of measuring it. */
            /* Asked before the question about its partials, because the
             * answer to that one is meaningless if there is no sound. */
            want(rms_at(0.40, 0.20) > 0.01, n,
                 "it has to still be sounding to be measured", rms_at(0.40, 0.20),
                 0.01, 9.9);
            const double ih = between_harmonics(&pr->patch, f0, 0.40, 0.20);
            /* Measured: as built it reads 5.0. Put the second line an octave
             * away instead of a tritone and it falls to 0.4, which is what a
             * threshold of 0.25 used to let through. One of 1.0 still leaves
             * five times the margin and catches that. */
            want(ih > 1.0, n, "its partials should not be harmonics", ih, 1.0, 99.0);
            want(a_ms > 25, n, "bowed, so it does not start instantly (ms)", a_ms, 25.0, 9999.0);
            want(at1 > 0.35, n, "and it rings on while bowed", at1, 0.35, 9.9);
            /*
             * The pitch wander is not measured here, and it is worth saying
             * why rather than leaving a gap that looks like an oversight. A
             * first attempt scanned for the loudest thing near the note at
             * two moments and compared them. It reported 188 cents of
             * movement from an envelope that can only produce 42, because
             * ring modulation fills the space around the note with partials
             * and the loudest one keeps changing. Removing the wander
             * entirely did not fail it. A measurement that passes for a
             * reason other than the one it names is worse than no
             * measurement, so it is gone until there is a way to follow one
             * partial through the middle of all that.
             */
        } else if (!strcmp(f, "Organ")) {
            want(a_ms >= 0 && a_ms < 25, n, "an organ is a switch, not a shape (ms)", a_ms, 0.0, 25.0);
            want(at1 > 0.85 && at1 < 1.2, n, "and does not change while held", at1, 0.85, 1.2);
        } else if (!strcmp(f, "Keys") || !strcmp(f, "Strings")) {
            want(at1 < 0.85, n, "a plucked or struck key decays", at1, 0.0, 0.85);
        } else if (!strcmp(f, "Wind")) {
            want(b_mid < 3.5, n, "wind instruments are close to a sine", b_mid, 0.0, 3.5);
            want(at1 > 0.6, n, "and hold while blown", at1, 0.6, 9.9);
        } else if (!strcmp(f, "Bass")) {
            want(b_on > b_mid * 1.2, n, "a bass should open and close, not sit still", b_on / (b_mid > 0 ? b_mid : 1), 1.2, 99.0);
            want(at1 > 0.4, n, "and hold under the hand", at1, 0.4, 9.9);
        }
    }

    /* The control. If a plain harmonic preset also reads as inharmonic then
     * the measurement above is not measuring anything. */
    {
        const pd_preset_t *organ = 0;
        for (int i = 0; i < pd_preset_count(); i++)
            if (!strcmp(pd_preset(i)->family, "Organ")) organ = pd_preset(i);
        if (organ) {
            play(&organ->patch, 60, 0.95, 1.2);
            const double ih = between_harmonics(&organ->patch, f0, 0.40, 0.20);
            want(ih < 0.10, organ->name,
                 "a harmonic preset must read as harmonic, or the test is empty",
                 ih, 0.0, 0.10);
        }
    }

    printf("\n%d characteristics missed\n", failures);
    return failures ? 1 : 0;
}
