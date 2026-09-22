/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Control voltage and gate, for driving a modular rig from the keyboard or the
 * sequencer that is already playing this.
 *
 * A modular expects volts. A plugin has audio channels, which run from -1 to
 * 1, so the convention is to agree a scale: here one volt is 0.1, which is the
 * usual choice because it puts a ten octave range inside the signal a plugin
 * can carry. The interface has to be DC coupled for any of this to survive the
 * trip out, which is a fact about the hardware and not something software can
 * arrange.
 *
 * Pitch follows one volt per octave from a C at zero volts, which is what
 * almost everything since the Minimoog expects. The gate is high while a key
 * is down.
 */
#ifndef PDSYNTH_PD_CV_H
#define PDSYNTH_PD_CV_H

/* Audio sample per volt. Ten volts, ten octaves, inside a signal that clips
 * at one. */
#define PD_VOLT 0.1

/* The note that sits at zero volts. C1 by convention, so the usual playing
 * range lands between about two and six volts. */
#define PD_CV_ZERO_NOTE 24

typedef struct {
    double pitch;      /* the sample to write to the pitch channel */
    double gate;       /* the sample to write to the gate channel */
    double velocity;   /* and to the velocity channel */
    int    note;       /* which note it is following, -1 for none */
    double glide_hz;   /* so a glide comes out of the pitch channel too */
} pd_cv_t;

void pd_cv_init(pd_cv_t *c);
void pd_cv_note_on(pd_cv_t *c, int note, double velocity);
void pd_cv_note_off(pd_cv_t *c, int note);
void pd_cv_all_off(pd_cv_t *c);

/* Follows the given sounding frequency when it is above zero, so a glide or a
 * bend appears on the pitch channel rather than only inside the synth. */
void pd_cv_track_hz(pd_cv_t *c, double hz);

/* What a given note is worth, in samples, for tests and for anyone checking. */
double pd_cv_volts_for_note(int note);

#endif
