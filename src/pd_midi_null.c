/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * No MIDI on this platform yet. The standalone still plays from the computer
 * keyboard, which is why this exists rather than the build simply failing.
 */
#include "pd_midi.h"

const char *pd_midi_open(pd_midi_fn fn, void *user) { (void)fn; (void)user; return 0; }
void        pd_midi_close(void) {}
