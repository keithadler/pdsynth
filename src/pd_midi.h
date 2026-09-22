/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * MIDI input, one backend per platform behind one call. The callback runs on
 * whatever thread the platform delivers on, so whatever it touches has to be
 * guarded by the caller.
 */
#ifndef PDSYNTH_PD_MIDI_H
#define PDSYNTH_PD_MIDI_H

typedef void (*pd_midi_fn)(void *user, const unsigned char *msg, int len);

/* Returns the name of the source it attached to, or NULL if there is none. */
const char *pd_midi_open(pd_midi_fn fn, void *user);
void        pd_midi_close(void);

#endif
