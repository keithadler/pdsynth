/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Casio CZ voice dumps, read and written.
 *
 * A CZ voice travels as 128 bytes sent a half byte at a time, low half first,
 * so the byte 5F goes out as 0F 05. Around it sits a seven byte header and a
 * terminator, making 264 bytes for a whole dump, or 263 when the program byte
 * is left off.
 *
 * pdsynth is not a CZ. It has four lines where the hardware has two, a filter
 * the hardware never had, and it responds to velocity and pressure that the
 * CZ-101 cannot hear. So a dump cannot carry everything. The rule here is to
 * say so rather than to refuse or to quietly round: a translation always
 * happens, and it always comes back with a written account of what it could
 * not take with it, naming the control in each case.
 *
 * The format is the CZ-101/1000/5000 one. A CZ-1 dump carries extra bytes for
 * the patch name and for velocity, so this is the shape every CZ can read.
 */
#ifndef PDSYNTH_PD_SYSEX_H
#define PDSYNTH_PD_SYSEX_H

#include <stddef.h>
#include <stdint.h>
#include "pd_voice.h"

/* A whole dump, with and without the program byte. */
#define PD_SYSEX_BYTES      264
#define PD_SYSEX_BYTES_ALT  263
/* The voice itself, once the half bytes are put back together. */
#define PD_SYSEX_VOICE      128

#define PD_SYSEX_MAX_NOTES  40

typedef struct {
    char control[40];   /* the thing the player would reach for */
    char what[168];     /* what happens to it, in a sentence */
} pd_sysex_note_t;

typedef struct {
    pd_sysex_note_t note[PD_SYSEX_MAX_NOTES];
    int count;
    int dropped;        /* notes there was no room for */
} pd_sysex_report_t;

void pd_sysex_report_init(pd_sysex_report_t *r);

/*
 * Read a dump into a patch. Returns 0, or negative if the bytes are not a CZ
 * voice dump at all, which is the one case where there is nothing to translate
 * and so nothing to report.
 *
 *   -1  too short or too long      -3  not a voice dump
 *   -2  not Casio                  -4  a half byte with its high half set
 */
int pd_sysex_read(const uint8_t *in, size_t len,
                  pd_patch_t *out, pd_sysex_report_t *report);

/*
 * As above, and also keeps the voice exactly as it arrived. Hand that back to
 * pd_sysex_write_ex and everything pdsynth does not model comes through
 * untouched: the vibrato section, the key follow, the CZ's second waveform per
 * line, and the per step falling bit, which real dumps show is stored rather
 * than worked out from the levels. A librarian that cannot load a patch and
 * save it back unchanged is not a librarian.
 */
int pd_sysex_read_ex(const uint8_t *in, size_t len, pd_patch_t *out,
                     uint8_t base[PD_SYSEX_VOICE], pd_sysex_report_t *report);

/*
 * Write a patch as a dump. Returns the number of bytes written, or 0 if the
 * buffer is too small. channel is 0 to 15; program is the CZ program number,
 * 0x60 for the sound the machine is holding right now.
 *
 * This never refuses and never clamps silently. Anything the CZ cannot hold is
 * named in the report.
 */
size_t pd_sysex_write(const pd_patch_t *p, int channel, int program,
                      uint8_t *out, size_t cap, pd_sysex_report_t *report);

/* base may be NULL, or the voice a patch was loaded from. */
size_t pd_sysex_write_ex(const pd_patch_t *p, const uint8_t *base,
                         int channel, int program,
                         uint8_t *out, size_t cap, pd_sysex_report_t *report);

/* The two halves on their own, for tests and for anyone holding raw voices. */
int    pd_sysex_unpack(const uint8_t *in, size_t len, uint8_t voice[PD_SYSEX_VOICE]);
size_t pd_sysex_pack(const uint8_t voice[PD_SYSEX_VOICE], int channel, int program,
                     uint8_t *out, size_t cap);

#endif
