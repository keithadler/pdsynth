/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The envelope.
 *
 * A CZ envelope is eight steps, each a rate and a level, with one step marked
 * as the sustain and one as the end. Holding a key walks the steps in order
 * and stops on the sustain step; releasing it resumes from there and walks on
 * to the end step. There is no attack-decay-sustain-release here: an eight
 * step envelope can be an ADSR, and it can also be a multi-stage swell, a
 * double attack, or a decay that pauses halfway down, none of which an ADSR
 * can express.
 *
 * Three of these run per line, on the pitch, the waveform and the amplitude.
 * The one on the waveform is the reason a CZ needs no filter: it sweeps how
 * far the phase is bent, and a bend that opens and closes sounds like
 * something opening and closing.
 */
#ifndef PDSYNTH_PD_ENV_H
#define PDSYNTH_PD_ENV_H

#include <stdint.h>

#define PD_ENV_STEPS 8

typedef struct {
    uint8_t rate[PD_ENV_STEPS];   /* 0 slowest, 99 instant */
    uint8_t level[PD_ENV_STEPS];  /* 0 to 99 */
    uint8_t sustain_step;         /* where a held key stops */
    uint8_t end_step;             /* the last step a released key walks to */
} pd_env_params_t;

typedef struct {
    const pd_env_params_t *p;
    double   value;               /* 0 to 1 */
    double   target;
    double   step_per_sample;
    int      step;
    int      held;
    int      finished;
    double   sample_rate;
} pd_env_t;

/* An envelope that does nothing but stay open, for parts of a patch that
 * should not be shaped. */
void pd_env_params_flat(pd_env_params_t *p);

void   pd_env_init(pd_env_t *e, const pd_env_params_t *p, double sample_rate);
void   pd_env_key_down(pd_env_t *e);
void   pd_env_key_up(pd_env_t *e);
double pd_env_next(pd_env_t *e);
int    pd_env_finished(const pd_env_t *e);

/* Seconds a step takes to cross the whole 0 to 1 range at this rate. Exposed
 * because the shape of that curve is a decision worth being able to inspect. */
double pd_env_rate_seconds(uint8_t rate);

#endif
