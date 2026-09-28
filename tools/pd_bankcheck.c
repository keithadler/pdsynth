/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Measures the factory bank. A bank of presets is not good because someone
 * wrote it with care; it is good when every patch sounds, answers the hand,
 * sits at the same level as its neighbours, and does something the others do
 * not. This prints those four things for all of them.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pd_presets.h"


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define SR 48000.0
#define N  32768

static double buf[N];

static int render(const pd_patch_t *p, int note, double vel, double onSec, double offSec)
{
    pd_voice_t v;
    pd_voice_init(&v, p, SR);
    pd_voice_note_on(&v, note, vel);
    const int total = (int)((onSec + offSec) * SR) < N ? (int)((onSec + offSec) * SR) : N;
    const int off = (int)(onSec * SR);
    for (int i = 0; i < total; i++) {
        if (i == off) pd_voice_note_off(&v);
        buf[i] = pd_voice_next(&v);
    }
    return total;
}

static double peak(int n) { double m = 0; for (int i = 0; i < n; i++) if (fabs(buf[i]) > m) m = fabs(buf[i]); return m; }

/* loudness while the note is actually sounding: a fixed window over a short
 * pluck averages in silence and calls a brief patch a quiet one */
static double gated(int n)
{
    double pk = peak(n);
    if (pk <= 0) return 0;
    const double gate = pk * 0.0316;
    const int W = (int)(0.01 * SR);
    double sum = 0; long used = 0;
    for (int i = 0; i + W <= n; i += W) {
        double s = 0;
        for (int j = i; j < i + W; j++) s += buf[j] * buf[j];
        double r = sqrt(s / W);
        if (r >= gate) { sum += s; used += W; }
    }
    return used ? sqrt(sum / (double)used) : 0;
}

/* brightness measured early, while the waveform envelope is still open */
static double centroid(int from)
{
    const int M = 2048;
    double num = 0, den = 0;
    for (int k = 1; k < M * 9000 / (int)SR; k++) {
        double re = 0, im = 0, w = 2.0 * M_PI * k / M;
        for (int i = 0; i < M; i++) {
            double win = 0.5 - 0.5 * cos(2.0 * M_PI * i / M);
            double x = (from + i < N) ? buf[from + i] : 0.0;
            re += x * win * cos(w * i);
            im += x * win * sin(w * i);
        }
        double m = sqrt(re * re + im * im);
        num += m * (k * SR / M);
        den += m;
    }
    return den > 0 ? num / den : 0;
}


/*
 * What happens if the player switches the filter on.
 *
 * A CZ has no filter and pdsynth's is off in every preset, so this was never
 * measured. Then the generator was found to be leaving `filter_cutoff_hz` to
 * zero fill, and switching the filter on in a factory preset dropped it to
 * between one and seventeen percent of its level. The plugin's own status line
 * invites exactly that: "no filter engaged, as on a CZ. FILTER, in the left
 * column, adds one."
 *
 * Off by default is not a reason not to check it. A control the interface
 * offers has to do something survivable when it is used.
 *
 * The filter lives on the stereo path, so this cannot use `render`, which sums
 * the lines and never calls it. Getting that wrong first made the filter look
 * like it did nothing at all.
 */
static double with_filter(const pd_patch_t *p, pd_filter_mode_t mode)
{
    pd_patch_t q = *p;
    q.filter_mode = mode;
    pd_voice_t v;
    pd_voice_init(&v, &q, SR);
    pd_voice_note_on(&v, 60, 0.9);
    const int n = (int)(0.5 * SR) * PD_OVERSAMPLE;
    double sum = 0;
    for (int i = 0; i < n; i++) {
        double l, r;
        pd_voice_next_inner(&v, &l, &r);
        sum += (l + r) * (l + r);
    }
    return sqrt(sum / (double)n);
}

int main(void)
{
    const int count = pd_preset_count();
    double loud[128], bright[128], velL[128], velB[128], decay[128];
    int problems = 0;

    printf("%-16s %-8s %7s %8s %9s %10s %8s\n",
           "preset", "family", "peak", "loud", "bright", "vel level", "at 0.4s");
    for (int i = 0; i < count; i++) {
        const pd_preset_t *pr = pd_preset(i);
        int n = render(&pr->patch, 52, 0.95, 0.45, 0.2);
        double pk = peak(n);
        loud[i] = gated(n);
        bright[i] = centroid((int)(0.012 * SR));

        /* level a second in, relative to the opening, so a patch that dies
         * instantly is visible next to one that holds */
        double open = 0, late = 0;
        for (int k = (int)(0.02 * SR); k < (int)(0.05 * SR); k++) open += buf[k] * buf[k];
        for (int k = (int)(0.40 * SR); k < (int)(0.43 * SR); k++) late += buf[k] * buf[k];
        decay[i] = open > 0 ? sqrt(late / open) : 0;

        /* use what render actually filled: asking for more samples than the
         * buffer holds walks off the end of it */
        int ns = render(&pr->patch, 52, 0.25, 0.7, 0.2);
        double soft = peak(ns), softB = centroid((int)(0.012 * SR));
        int nh = render(&pr->patch, 52, 1.00, 0.7, 0.2);
        double hard = peak(nh), hardB = centroid((int)(0.012 * SR));
        velL[i] = soft > 1e-9 ? hard / soft : 0;
        velB[i] = softB > 1e-6 ? hardB / softB : 1.0;

        printf("%-16s %-8s %7.3f %8.4f %9.0f %9.2fx %7.2f\n",
               pr->name, pr->family, pk, loud[i], bright[i], velL[i], decay[i]);

        if (pk < 0.02)      { printf("    ^ barely sounds\n"); problems++; }
        if (velL[i] < 1.2)  { printf("    ^ ignores velocity\n"); problems++; }
    }

    /* how level the bank is, which is what decides whether scrolling through
     * it is pleasant or a hand on the volume control */
    double sorted[128];
    memcpy(sorted, loud, sizeof(double) * (size_t)count);
    for (int i = 0; i < count; i++)
        for (int j = i + 1; j < count; j++)
            if (sorted[j] < sorted[i]) { double t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t; }
    double med = sorted[count / 2];
    int within = 0, loudest = 0;
    for (int i = 0; i < count; i++) {
        double db = 20.0 * log10(loud[i] / med);
        if (fabs(db) <= 3.0) within++;
        if (loud[i] > loud[loudest]) loudest = i;
    }
    printf("\n%d presets\n", count);
    printf("level: %d of %d within 3 dB of the median, loudest %+.1f dB (%s)\n",
           within, count, 20.0 * log10(loud[loudest] / med), pd_preset(loudest)->name);

    int dead = 0, dull = 0;
    for (int i = 0; i < count; i++) {
        if (velL[i] < 1.2) dead++;
        if (bright[i] < 1.15 * 164.8) dull++;   /* 164.8 Hz is the note played */
    }
    /* Switching the filter on has to leave a sound behind. */
    int muted = 0;
    for (int i = 0; i < count; i++) {
        const pd_patch_t *pp = &pd_preset(i)->patch;
        const double off = with_filter(pp, PD_FILTER_OFF);
        const double on  = with_filter(pp, PD_FILTER_LOWPASS);
        if (off > 1e-6 && on / off < 0.5) {
            printf("  %-16s loses %.0f%% of its level when the filter is switched on "
                   "(cutoff %.0f Hz)\n",
                   pd_preset(i)->name, 100.0 * (1.0 - on / off),
                   pp->filter_cutoff_hz);
            muted++;
            problems++;
        }
    }

    printf("velocity: %d of %d respond\n", count - dead, count);
    printf("filter: %d of %d survive being switched on\n", count - muted, count);
    printf("timbre: %d of %d carry harmonics above a sine\n", count - dull, count);
    printf("%d problems\n", problems);
    return problems ? 1 : 0;
}
