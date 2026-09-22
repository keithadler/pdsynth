/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "pd_sysex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Where each thing lives in the 128 byte voice. The names are Casio's, kept
 * so this can be read next to the published specification.
 * ------------------------------------------------------------------------ */
enum {
    O_PFLAG = 0,    /* line select and octave        */
    O_PDS   = 1,    /* detune sign                   */
    O_PDET  = 2,    /* detune, fine then coarse      */
    O_PVK   = 4,    /* vibrato wave                  */
    O_VDLY  = 5,
    O_VRATE = 8,
    O_VDEP  = 11,
    O_MFW   = 14,   /* line 1 waveform and modulation */
    O_MAM   = 16,   /* line 1 DCA key follow          */
    O_MWM   = 18,   /* line 1 DCW key follow          */
    O_PMAL  = 20,   /* line 1 DCA end step            */
    O_PMA   = 21,   /* line 1 DCA envelope            */
    O_PMWL  = 37,
    O_PMW   = 38,   /* line 1 DCW envelope            */
    O_PMPL  = 54,
    O_PMP   = 55,   /* line 1 DCO envelope            */
    O_SFW   = 71,   /* line 2 waveform                */
    O_SAM   = 73,
    O_SWM   = 75,
    O_PSAL  = 77,
    O_PSA   = 78,   /* line 2 DCA envelope            */
    O_PSWL  = 94,
    O_PSW   = 95,   /* line 2 DCW envelope            */
    O_PSPL  = 111,
    O_PSP   = 112   /* line 2 DCO envelope            */
};

/* ---------------------------------------------------------------------------
 * The report. A translation that loses something says which control and why.
 * ------------------------------------------------------------------------ */
void pd_sysex_report_init(pd_sysex_report_t *r)
{
    if (!r) return;
    r->count = 0;
    r->dropped = 0;
}

static void note(pd_sysex_report_t *r, const char *control, const char *fmt, ...)
{
    if (!r) return;
    if (r->count >= PD_SYSEX_MAX_NOTES) { r->dropped++; return; }
    pd_sysex_note_t *n = &r->note[r->count++];
    snprintf(n->control, sizeof n->control, "%s", control);

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(n->what, sizeof n->what, fmt, ap);
    va_end(ap);
}

/* ---------------------------------------------------------------------------
 * Half bytes. Low half first: the byte 5F travels as 0F 05.
 * ------------------------------------------------------------------------ */
int pd_sysex_unpack(const uint8_t *in, size_t len, uint8_t voice[PD_SYSEX_VOICE])
{
    if (!in || !voice) return -1;
    if (len != PD_SYSEX_BYTES && len != PD_SYSEX_BYTES_ALT) return -1;
    if (in[0] != 0xF0 || in[len - 1] != 0xF7) return -3;
    if (in[1] != 0x44) return -2;

    const uint8_t *nib = in + (len - 1) - PD_SYSEX_VOICE * 2;
    for (int i = 0; i < PD_SYSEX_VOICE; i++) {
        const uint8_t lo = nib[i * 2], hi = nib[i * 2 + 1];
        if (lo > 0x0F || hi > 0x0F) return -4;
        voice[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

size_t pd_sysex_pack(const uint8_t voice[PD_SYSEX_VOICE], int channel, int program,
                     uint8_t *out, size_t cap)
{
    if (!voice || !out || cap < PD_SYSEX_BYTES) return 0;
    if (channel < 0) channel = 0;
    if (channel > 15) channel = 15;

    size_t n = 0;
    out[n++] = 0xF0;
    out[n++] = 0x44;
    out[n++] = 0x00;
    out[n++] = 0x00;
    out[n++] = (uint8_t)(0x70 + channel);
    out[n++] = 0x20;                      /* receive request: here is a voice */
    out[n++] = (uint8_t)(program & 0x7F);
    for (int i = 0; i < PD_SYSEX_VOICE; i++) {
        out[n++] = (uint8_t)(voice[i] & 0x0F);
        out[n++] = (uint8_t)(voice[i] >> 4);
    }
    out[n++] = 0xF7;
    return n;
}

/* ---------------------------------------------------------------------------
 * Rates and levels. Casio encodes each of the three envelope kinds its own
 * way. These are the published formulas, checked against real dumps: every
 * rate byte and every level byte in a sample of twenty patches came back
 * through them unchanged, bar nine amplitude levels that third party editors
 * had written one step off.
 *
 * Reading uses the nearest legal value rather than the published inverse, so
 * a byte no legal value produces still lands somewhere sensible instead of
 * out of range.
 * ------------------------------------------------------------------------ */
static uint8_t clamp99(int v) { return (uint8_t)(v < 0 ? 0 : (v > 99 ? 99 : v)); }

static int lvl_enc(int v)      { return v <= 0 ? 0 : (v >= 99 ? 127 : 127 * v / 99); }
static int amp_rate_enc(int v) { return 119 * clamp99(v) / 99; }
static int dcw_rate_enc(int v) { return 119 * clamp99(v) / 99 + 8; }
static int dco_rate_enc(int v) { return v <= 0 ? 0 : (v >= 99 ? 127 : 127 * v / 99); }
/* The pitch envelope keeps its levels in two runs with a gap between them. */
static int dco_lvl_enc(int v)  { return clamp99(v) <= 63 ? clamp99(v) : clamp99(v) + 4; }

static uint8_t nearest(int byte, int (*enc)(int))
{
    int best = 0, bestd = 1 << 20;
    for (int v = 0; v <= 99; v++) {
        int d = enc(v) - byte;
        if (d < 0) d = -d;
        if (d < bestd) { bestd = d; best = v; }
    }
    return (uint8_t)best;
}

/* ---------------------------------------------------------------------------
 * Waveforms. Five of the eight have a three bit code of their own; the three
 * resonant ones share a code and are told apart by two more bits in the second
 * byte. That pair is shared between the lines, so two resonant lines on one
 * voice must be the same resonant waveform.
 * ------------------------------------------------------------------------ */
static const uint8_t kWaveCode[8] = { 0, 1, 2, 4, 5, 6, 6, 6 };
static const uint8_t kWaveSub[8]  = { 0, 0, 0, 0, 0, 1, 2, 3 };

static pd_wave_t wave_from(uint8_t code, uint8_t sub)
{
    if (code == 6) {
        if (sub == 2) return PD_RESO_TRIANGLE;
        if (sub == 3) return PD_RESO_TRAPEZOID;
        return PD_RESO_SAW;
    }
    for (int i = 0; i < 5; i++) if (kWaveCode[i] == code) return (pd_wave_t)i;
    return PD_SAW;
}

/* ---------------------------------------------------------------------------
 * Reading.
 * ------------------------------------------------------------------------ */
static void read_env(const uint8_t *v, int off, int endoff,
                     pd_env_params_t *e, int kind)
{
    /* kind: 0 amplitude, 1 waveform, 2 pitch */
    int sustain = -1;
    for (int s = 0; s < PD_ENV_STEPS; s++) {
        const uint8_t rb = v[off + s * 2];
        const uint8_t lb = v[off + s * 2 + 1];
        const int rate_byte = rb & 0x7F;      /* the top bit says falling */
        const int lvl_byte  = lb & 0x7F;      /* the top bit marks sustain */
        if (lb & 0x80) sustain = s;

        e->rate[s]  = (kind == 0) ? nearest(rate_byte, amp_rate_enc)
                    : (kind == 1) ? nearest(rate_byte, dcw_rate_enc)
                                  : nearest(rate_byte, dco_rate_enc);
        e->level[s] = (kind == 2) ? nearest(lvl_byte, dco_lvl_enc)
                                  : nearest(lvl_byte, lvl_enc);
    }
    e->end_step     = (uint8_t)(v[endoff] & 0x07);
    e->sustain_step = (uint8_t)(sustain >= 0 ? sustain : e->end_step);
}

int pd_sysex_read_ex(const uint8_t *in, size_t len, pd_patch_t *out,
                     uint8_t base[PD_SYSEX_VOICE], pd_sysex_report_t *report)
{
    if (!out) return -1;
    uint8_t v[PD_SYSEX_VOICE];
    const int rc = pd_sysex_unpack(in, len, v);
    if (rc != 0) return rc;
    if (base) memcpy(base, v, sizeof v);

    pd_sysex_report_init(report);
    memset(out, 0, sizeof *out);

    /* line select: 0 line 1, 1 line 2, 2 line 1 and itself an octave up,
     * 3 both lines. The dump always carries both lines whichever is chosen. */
    const int ls   = v[O_PFLAG] & 0x03;
    const int octb = (v[O_PFLAG] >> 2) & 0x03;
    const int octave = (octb == 1) ? 1 : (octb == 2) ? -1 : 0;

    out->line_count = (ls == 0 || ls == 1) ? 1 : 2;
    out->mix = PD_MIX_BOTH;

    const int mod = (v[O_MFW + 1] >> 3) & 0x07;
    if (mod == 4) out->mix = PD_MIX_RING;
    else if (mod == 3) { out->mix = PD_MIX_NOISE; out->noise_amount = 1.0; }

    /* Each line picks a waveform, and may pick a second to go with it. The
     * three resonant shapes share one two bit code across the whole voice. */
    const uint8_t sub = (uint8_t)((v[O_MFW + 1] >> 6) & 0x03);
    const uint8_t sub2 = (uint8_t)((v[O_SFW + 1] >> 6) & 0x03);

    /*
     * Detune between the lines, held once for the voice. A CZ counts it in
     * octaves, semitones and cents, and reaches three octaves, so it is not a
     * fine trim: real patches use it to put line 2 an octave or a fifth away.
     * pdsynth already has an octave and a semitone control per line, so it is
     * split across the three rather than crammed into the cents control, where
     * anything past a semitone would be lost.
     */
    const int fine  = (v[O_PDET] & 0x0F) + 15 * (v[O_PDET] >> 4);
    const int note_ = v[O_PDET + 1] % 12, oct_ = v[O_PDET + 1] / 12;
    const int sign  = (v[O_PDS] & 0x01) ? -1 : 1;

    for (int i = 0; i < 2; i++) {
        pd_line_params_t *L = &out->line[i];
        const int wbyte = (i == 0) ? O_MFW : O_SFW;
        L->wave   = wave_from((uint8_t)((v[wbyte] >> 5) & 0x07), i == 0 ? sub : sub2);
        L->octave = (i == 1 && ls == 2) ? octave + 1 : octave;
        L->semitones = 0;
        L->level  = 1.0;
        if (i == 1) {
            L->octave    += sign * oct_;
            L->semitones  = sign * note_;
            L->detune_cents = sign * (double)fine;
        }
        L->pitch_env_depth_semitones = 12.0;
        read_env(v, (i == 0) ? O_PMA : O_PSA, (i == 0) ? O_PMAL : O_PSAL, &L->amp_env,   0);
        read_env(v, (i == 0) ? O_PMW : O_PSW, (i == 0) ? O_PMWL : O_PSWL, &L->wave_env,  1);
        read_env(v, (i == 0) ? O_PMP : O_PSP, (i == 0) ? O_PMPL : O_PSPL, &L->pitch_env, 2);
    }

    out->bend_range_semitones = 2.0;
    out->spread = 0.0;
    out->filter_mode = PD_FILTER_OFF;

    /* What the dump held that pdsynth reads differently, or not at all. */
    if (ls == 1)
        note(report, "Line select",
             "This voice plays its second line alone. pdsynth plays its lines "
             "from the first, so both are loaded and both will sound.");
    if (ls == 2)
        note(report, "Line select",
             "This voice plays line 1 together with itself an octave up. "
             "pdsynth loads line 2 as that octave, so the two are independent "
             "here and were not on the hardware.");
    if ((v[O_MFW] >> 1) & 0x01)
        note(report, "Line 1 waveform",
             "The CZ pairs two waveforms on a line and this voice uses both. "
             "pdsynth has one waveform per line, so the second is not read.");
    if ((v[O_SFW] >> 1) & 0x01)
        note(report, "Line 2 waveform",
             "The CZ pairs two waveforms on a line and this voice uses both. "
             "pdsynth has one waveform per line, so the second is not read.");
    if (v[O_PVK] || v[O_VDEP] || v[O_VDEP + 2])
        note(report, "Vibrato",
             "The dump carries the CZ's own vibrato. pdsynth has no vibrato "
             "section, so it is not read.");
    if (v[O_MAM] || v[O_MWM] || v[O_SAM] || v[O_SWM])
        note(report, "Key follow",
             "Key follow on the level and the waveform is in the dump but has "
             "no control here yet, so it is not read.");
    note(report, "Pitch envelope depth",
         "The CZ pitch envelope is absolute. pdsynth scales its envelope by a "
         "depth control, set to 12 semitones on reading, which you may want to "
         "change.");
    return 0;
}

int pd_sysex_read(const uint8_t *in, size_t len,
                  pd_patch_t *out, pd_sysex_report_t *report)
{
    return pd_sysex_read_ex(in, len, out, NULL, report);
}

/* ---------------------------------------------------------------------------
 * Writing.
 * ------------------------------------------------------------------------ */
static void write_env(uint8_t *v, const uint8_t *base, int off, int endoff,
                      const pd_env_params_t *e, int kind)
{
    for (int s = 0; s < PD_ENV_STEPS; s++) {
        int (*renc)(int) = (kind == 0) ? amp_rate_enc
                         : (kind == 1) ? dcw_rate_enc : dco_rate_enc;
        int (*lenc)(int) = (kind == 2) ? dco_lvl_enc : lvl_enc;

        /*
         * A byte the player never touched is written back exactly as it
         * arrived. Two things make this matter. The top bit of a rate says the
         * step falls, and across real dumps that bit agrees with the levels
         * only 94 percent of the time and also turns up on the first step
         * where no earlier level exists, so it is stored, not worked out. And
         * some editors in the wild encode a level one step off what the
         * published formula gives; re-encoding would quietly move it. Only a
         * value that actually changed is encoded afresh.
         */
        const int rb = base ? base[off + s * 2]     : -1;
        const int lb = base ? base[off + s * 2 + 1] : -1;
        const int rate_same = rb >= 0 && nearest(rb & 0x7F, renc) == e->rate[s];
        const int lvl_same  = lb >= 0 && nearest(lb & 0x7F, lenc) == e->level[s];

        int r, l;
        if (rate_same) r = rb;
        else {
            r = renc(e->rate[s]);
            if (rb >= 0) r |= rb & 0x80;
            else if (s > 0 && e->level[s] < e->level[s - 1]) r |= 0x80;
        }
        if (lvl_same) l = lb;
        else {
            l = lenc(e->level[s]);
            if (s == e->sustain_step) l |= 0x80;
        }
        v[off + s * 2]     = (uint8_t)r;
        v[off + s * 2 + 1] = (uint8_t)l;
    }
    v[endoff] = (uint8_t)(e->end_step & 0x07);
}

size_t pd_sysex_write_ex(const pd_patch_t *p, const uint8_t *base,
                         int channel, int program,
                         uint8_t *out, size_t cap, pd_sysex_report_t *report)
{
    if (!p || !out) return 0;
    pd_sysex_report_init(report);

    uint8_t v[PD_SYSEX_VOICE];
    if (base) memcpy(v, base, sizeof v);
    else      memset(v, 0, sizeof v);

    const int lines = p->line_count < 1 ? 1 : p->line_count;
    if (lines > 2)
        note(report, "Lines 3 and 4",
             "A CZ has two lines and this patch has %d. Lines 3 and 4 are not "
             "written, so the dump will sound thinner than what you hear here.",
             lines);

    const pd_line_params_t *L1 = &p->line[0];
    const pd_line_params_t *L2 = &p->line[1];

    /* Keep the voice's own line select when it came from a dump, because 1+1'
     * is a thing the hardware does that pdsynth has no way to say. */
    const int ls_base = base ? (base[O_PFLAG] & 0x03) : -1;
    const int ls = (ls_base == 1 || ls_base == 2) ? ls_base : (lines > 1 ? 3 : 0);
    v[O_PFLAG] = (uint8_t)(ls | ((L1->octave > 0 ? 1 : L1->octave < 0 ? 2 : 0) << 2));

    /*
     * Detune belongs to the voice, not to a line, so it is always written, and
     * it is gathered back from the three controls it was split across. A voice
     * that plays line 1 with itself an octave up carries that octave in the
     * line select, not in the detune, so it comes back off here.
     */
    double cents = (double)((L2->octave - L1->octave - (ls == 2 ? 1 : 0)) * 1200
                          + L2->semitones * 100) + L2->detune_cents;
    v[O_PDS] = (uint8_t)(cents < 0 ? 1 : 0);
    if (cents < 0) cents = -cents;
    if (cents > 4799.0) {
        note(report, "Detune",
             "Detune of %.0f cents is past the CZ's range and is written as "
             "its largest.", cents);
        cents = 4799.0;
    }
    {
        const int total = (int)(cents + 0.5);
        int semis = total / 100, fine = total % 100;
        /*
         * A CZ's cents only reach 60, so 61 to 99 is not expressible. The
         * nearest thing it can hold is either 60 or the next semitone up, and
         * which of those is nearer depends on the value: 61 is one cent from
         * 60 and thirty nine from 100.
         */
        if (fine > 60) {
            if (fine >= 80) { semis += 1; fine = 0; }
            else            { fine = 60; }
            note(report, "Detune",
                 "A CZ counts cents only as far as 60. %d is written as the "
                 "nearest it can hold, which is %d.",
                 total % 100, fine ? fine : 100);
        }
        /* fine 0..15 goes out as 00..0F, then 15 to a row: 11..1F, 21..2F ... */
        const int hi = fine <= 15 ? 0 : (fine - 1) / 15;
        const uint8_t d0 = (uint8_t)((hi << 4) | (fine - 15 * hi));
        const uint8_t d1 = (uint8_t)(semis > 47 ? 47 : semis);
        /* a detune that did not move keeps the bytes it came in with */
        if (base) {
            const int bf = (base[O_PDET] & 0x0F) + 15 * (base[O_PDET] >> 4);
            const double bc = bf + 100.0 * (base[O_PDET + 1] % 12)
                                 + 1200.0 * (base[O_PDET + 1] / 12);
            if ((int)(bc + 0.5) == total) {
                v[O_PDET] = base[O_PDET];
                v[O_PDET + 1] = base[O_PDET + 1];
            } else { v[O_PDET] = d0; v[O_PDET + 1] = d1; }
        } else { v[O_PDET] = d0; v[O_PDET + 1] = d1; }

        /*
         * A detune of nothing has no direction, and the machine still keeps a
         * sign byte for it. Asking whether the amount is zero has to be asked
         * of the bytes, not of the number: the amount reaches here through a
         * float parameter, and a value that went in as zero can come back as
         * a millionth of a cent, which is not zero and is not a detune either.
         */
        if (base && v[O_PDET] == 0 && v[O_PDET + 1] == 0) v[O_PDS] = base[O_PDS];
    }

    const pd_wave_t wa = L1->wave, wb = L2->wave;
    int mod = 0;
    if (p->mix == PD_MIX_RING)  mod = 4;
    if (p->mix == PD_MIX_NOISE) mod = 3;

    /*
     * A line picks a waveform and may pair a second with it. pdsynth has one
     * per line, so only the first slot is written; the pairing bits are left
     * as they arrived rather than thrown away.
     */
    const uint8_t keep1 = (uint8_t)(base ? base[O_MFW] & 0x1F : 0);
    const uint8_t keep2 = (uint8_t)(base ? base[O_SFW] & 0x1F : 0);
    v[O_MFW]     = (uint8_t)((kWaveCode[wa] << 5) | keep1);
    v[O_MFW + 1] = (uint8_t)(((kWaveSub[wa] ? kWaveSub[wa]
                              : (base ? (base[O_MFW + 1] >> 6) & 0x03 : 0)) << 6)
                           | (mod << 3) | (base ? base[O_MFW + 1] & 0x07 : 0));
    v[O_SFW]     = (uint8_t)((kWaveCode[wb] << 5) | keep2);
    v[O_SFW + 1] = (uint8_t)(((kWaveSub[wb] ? kWaveSub[wb]
                              : (base ? (base[O_SFW + 1] >> 6) & 0x03 : 0)) << 6)
                           | (base ? base[O_SFW + 1] & 0x3F : 0));

    if (kWaveSub[wa] && kWaveSub[wb] && kWaveSub[wa] != kWaveSub[wb])
        note(report, "Waveform",
             "Both lines use a resonant waveform but the CZ keeps one resonant "
             "shape per voice. Line 2 is written as %s, the same as line 1.",
             pd_wave_name(wa));

    write_env(v, base, O_PMA, O_PMAL, &L1->amp_env,   0);
    write_env(v, base, O_PMW, O_PMWL, &L1->wave_env,  1);
    write_env(v, base, O_PMP, O_PMPL, &L1->pitch_env, 2);
    write_env(v, base, O_PSA, O_PSAL, &L2->amp_env,   0);
    write_env(v, base, O_PSW, O_PSWL, &L2->wave_env,  1);
    write_env(v, base, O_PSP, O_PSPL, &L2->pitch_env, 2);

    /* Everything the machine has no place to put. */
    if (p->filter_mode != PD_FILTER_OFF)
        note(report, "Filter",
             "The CZ has no filter. The one on this patch is not written, and "
             "the dump will sound brighter or duller than what you hear here.");
    if (p->velocity_to_wave != 0.0 || p->velocity_to_level != 0.0)
        note(report, "Velocity",
             "A CZ-101 cannot hear velocity; it reads every note as 64. This "
             "patch responds to it, and the dump will not.");
    if (p->aftertouch_to_wave != 0.0 || p->aftertouch_to_level != 0.0)
        note(report, "Pressure",
             "A CZ-101 has no aftertouch. Pressure will do nothing on the "
             "hardware.");
    if (p->glide_seconds > 0.0)
        note(report, "Glide",
             "Portamento is set per machine on a CZ, not per voice, so glide "
             "is not part of a voice dump. Set it on the instrument.");
    if (p->spread != 0.0)
        note(report, "Stereo spread",
             "A CZ-101 is mono. The stereo placement of the lines is not "
             "written.");
    if (p->mod_to_wave != 0.0)
        note(report, "Mod wheel",
             "The CZ's mod wheel drives its own vibrato, not the waveform, so "
             "this routing is not written.");
    for (int i = 0; i < lines && i < 2; i++)
        if (p->line[i].semitones != 0)
            note(report, "Semitone offset",
                 "A CZ tunes a line in octaves and by detuning, not in "
                 "semitones. The %d semitone offset on line %d is not written.",
                 p->line[i].semitones, i + 1);

    return pd_sysex_pack(v, channel, program, out, cap);
}

size_t pd_sysex_write(const pd_patch_t *p, int channel, int program,
                      uint8_t *out, size_t cap, pd_sysex_report_t *report)
{
    return pd_sysex_write_ex(p, NULL, channel, program, out, cap, report);
}
