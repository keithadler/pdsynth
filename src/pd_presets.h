/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The factory bank.
 *
 * These are original designs. Not one of them is a transcription of a Casio
 * ROM patch: they are built from the synthesis structure, the way anyone with
 * the manual and an afternoon would build them, and they belong to this
 * project. What they borrow from the hardware is the set of ideas a CZ is good
 * at, which is not anybody's property.
 *
 * They live in C rather than in the plugin so that the headless renderer can
 * audition and measure the whole bank without a host, which is how they were
 * levelled and how they stay levelled.
 */
#ifndef PDSYNTH_PD_PRESETS_H
#define PDSYNTH_PD_PRESETS_H

#include "pd_voice.h"

typedef struct {
    const char *name;
    const char *family;     /* for grouping in a menu */
    pd_patch_t  patch;
} pd_preset_t;

int                 pd_preset_count(void);
const pd_preset_t  *pd_preset(int index);

#endif
