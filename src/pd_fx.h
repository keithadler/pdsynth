/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Chorus, delay and drive.
 *
 * The argument for having these at all is that the hardware reissue has a
 * chorus, and a synth that arrives completely dry sounds thinner than the box
 * it is standing next to, whatever its oscillators are doing. Two detuned
 * lines already beat against each other; a chorus is the same idea applied to
 * the whole voice and it is what makes a CZ string patch sound like the
 * records.
 *
 * All three are written here rather than borrowed. A chorus is a delay that
 * wobbles and a drive is a curve, and the plugin suites worth reading for
 * approach are GPL, which would follow the code home if any were lifted.
 *
 * Everything runs at the output rate, after the voices are summed and
 * decimated, because none of it needs the oversampled stream and running it
 * there would cost four times as much for nothing.
 */
#ifndef PDSYNTH_PD_FX_H
#define PDSYNTH_PD_FX_H

typedef enum {
    PD_DRIVE_OFF = 0,
    PD_DRIVE_SOFT,     /* a gentle knee: thickens without announcing itself */
    PD_DRIVE_HARD,     /* clipped, for when it should be obvious */
    PD_DRIVE_FOLD,     /* folded back: the harsh one, and the interesting one */
    PD_DRIVE_MODES
} pd_drive_mode_t;

const char *pd_drive_mode_name(pd_drive_mode_t m);

typedef struct {
    /* chorus */
    double  chorus_mix;        /* 0 dry, 1 wet */
    double  chorus_depth_ms;
    double  chorus_rate_hz;
    double  chorus_spread;     /* how far the two sides are moved apart */
    /* delay */
    double  delay_mix;
    double  delay_time_s;
    double  delay_feedback;
    double  delay_tone;        /* 0 dark repeats, 1 bright */
    /* drive, which comes first in the chain */
    pd_drive_mode_t drive_mode;
    double  drive_amount;
} pd_fx_params_t;

void pd_fx_params_init(pd_fx_params_t *p);

#define PD_FX_MAX_DELAY_SECONDS 2.0
#define PD_FX_CHORUS_TAPS       3

typedef struct pd_fx_t pd_fx_t;

/* Heap allocated, because the delay line is large enough that putting it in a
 * voice or a processor by value would be unkind to whatever holds those. */
pd_fx_t *pd_fx_create(double sample_rate);
void     pd_fx_destroy(pd_fx_t *fx);
void     pd_fx_reset(pd_fx_t *fx);
void     pd_fx_process(pd_fx_t *fx, const pd_fx_params_t *p, double *left, double *right);

#endif
