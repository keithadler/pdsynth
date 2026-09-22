/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <math.h>
#include <string.h>
#include "pd_env.h"

/*
 * Rate 0 is the slowest step the hardware offers and 99 is effectively
 * instant, and the span between them is not a straight line: the useful
 * musical detail is all at the fast end, so the curve is exponential. Roughly
 * forty seconds at the bottom down to a hundred microseconds at the top, which
 * puts a plucked attack and a slow pad swell both inside the range with room
 * to spare.
 */
double pd_env_rate_seconds(uint8_t rate)
{
    double r = rate > 99 ? 99.0 : (double)rate;
    return 40.0 * pow(10.0, -5.6 * (r / 99.0));
}

void pd_env_params_flat(pd_env_params_t *p)
{
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < PD_ENV_STEPS; i++) {
        p->rate[i]  = 99;
        p->level[i] = 99;
    }
    p->sustain_step = 0;
    p->end_step     = PD_ENV_STEPS - 1;
}

static void aim(pd_env_t *e, int step)
{
    const pd_env_params_t *p = e->p;
    if (step < 0) step = 0;
    if (step >= PD_ENV_STEPS) step = PD_ENV_STEPS - 1;
    e->step   = step;
    e->target = p->level[step] / 99.0;

    double secs = pd_env_rate_seconds(p->rate[step]);
    double samples = secs * e->sample_rate;
    e->step_per_sample = (samples > 1.0) ? (1.0 / samples) : 1.0;
}

void pd_env_init(pd_env_t *e, const pd_env_params_t *p, double sample_rate)
{
    memset(e, 0, sizeof(*e));
    e->p = p;
    e->sample_rate = sample_rate > 0 ? sample_rate : 48000.0;
    e->value = 0.0;
    e->finished = 1;
    aim(e, 0);
}

void pd_env_key_down(pd_env_t *e)
{
    e->held = 1;
    e->finished = 0;
    /*
     * Start from wherever the envelope currently sits rather than snapping to
     * zero, so retriggering a note that is still ringing does not click.
     */
    aim(e, 0);
}

void pd_env_key_up(pd_env_t *e)
{
    e->held = 0;
    /* resume from the step after the sustain, walking on towards the end */
    int next = e->p->sustain_step + 1;
    if (next > e->p->end_step) next = e->p->end_step;
    aim(e, next);
}

double pd_env_next(pd_env_t *e)
{
    if (e->finished) return e->value;

    double d = e->target - e->value;
    if (fabs(d) <= e->step_per_sample) {
        e->value = e->target;
        /* the step is reached; move on unless this is a place to wait */
        if (e->held && e->step >= e->p->sustain_step) {
            /* hold here until the key is released */
        } else if (e->step >= (e->held ? e->p->sustain_step : e->p->end_step)) {
            if (!e->held) e->finished = 1;
        } else {
            aim(e, e->step + 1);
        }
    } else {
        e->value += (d > 0 ? e->step_per_sample : -e->step_per_sample);
    }

    if (e->value < 0.0) e->value = 0.0;
    if (e->value > 1.0) e->value = 1.0;
    return e->value;
}

int pd_env_finished(const pd_env_t *e)
{
    return e->finished;
}
