/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Casio CZ voice dumps.
 *
 * The thing worth guarding here is not that a patch survives a round trip. It
 * is that a patch the player never touched comes back byte for byte, including
 * the parts pdsynth has no control for. A librarian that rewrites bytes it does
 * not understand corrupts a collection quietly, one save at a time.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pd_sysex.h"

static int passed, failed;
static void ok(int cond, const char *fmt, ...)
{
    if (cond) { passed++; return; }
    failed++;
    va_list ap; va_start(ap, fmt);
    printf("  FAIL: "); vprintf(fmt, ap); printf("\n");
    va_end(ap);
}

/* A voice with something in every field, including the parts pdsynth does not
 * model, so that anything dropped on the way through shows up. */
static void fill_voice(uint8_t v[PD_SYSEX_VOICE], unsigned seed)
{
    for (int i = 0; i < PD_SYSEX_VOICE; i++) {
        seed = seed * 1103515245u + 12345u;
        v[i] = (uint8_t)(seed >> 16);
    }
    v[0] = (uint8_t)(0x03 | (1 << 2));      /* both lines, octave up */
    v[1] = 0x00;
    v[2] = 0x0A; v[3] = 0x05;               /* a legal detune */
    v[14] = 0x40; v[15] = 0x00;             /* line 1 waveform 3, no modulation */
    v[71] = 0x20; v[72] = 0x00;             /* line 2 waveform 2 */
    v[1] = (uint8_t)(seed & 1);             /* detune down as well as up */
    v[20] = 7; v[37] = 7; v[54] = 7;
    v[77] = 7; v[94] = 7; v[111] = 7;

    /* Envelope bytes have to be ones the machine can actually hold, and the
     * pitch envelope keeps its levels in two runs with a gap between them. */
    const int amp[4]   = { 21, 78, 38, 95 };
    const int pitch[2] = { 55, 112 };
    for (int e = 0; e < 4; e++)
        for (int s = 0; s < 8; s++) {
            v[amp[e] + s * 2]     = (uint8_t)((s * 13) % 112);
            v[amp[e] + s * 2 + 1] = (uint8_t)((s * 17) % 100);
        }
    for (int e = 0; e < 2; e++)
        for (int s = 0; s < 8; s++) {
            v[pitch[e] + s * 2]     = (uint8_t)((s * 11) % 120);
            /* levels above 63 live at 0x44 and up, so exercise that run */
            v[pitch[e] + s * 2 + 1] = (uint8_t)(s < 4 ? s * 15 : 0x44 + (s - 4) * 6);
        }

    /* A step marked as the sustain, and a step whose falling bit disagrees
     * with its levels, which real dumps do and which cannot be worked out. */
    v[21 + 2 * 2 + 1] |= 0x80;
    v[38 + 3 * 2 + 1] |= 0x80;
    v[55 + 1 * 2 + 1] |= 0x80;
    v[78 + 2 * 2 + 1] |= 0x80;
    v[95 + 3 * 2 + 1] |= 0x80;
    v[112 + 1 * 2 + 1] |= 0x80;
    v[21 + 1 * 2] |= 0x80;      /* falls, though the level rises */
    v[78 + 5 * 2] |= 0x80;
}

int main(void)
{
    uint8_t voice[PD_SYSEX_VOICE], dump[PD_SYSEX_BYTES];

    printf("-- half bytes go low half first --\n");
    {
        uint8_t v[PD_SYSEX_VOICE];
        memset(v, 0, sizeof v);
        v[0] = 0x5F;
        const size_t n = pd_sysex_pack(v, 0, 0x60, dump, sizeof dump);
        ok(n == PD_SYSEX_BYTES, "a dump is %d bytes, got %zu", PD_SYSEX_BYTES, n);
        ok(dump[0] == 0xF0 && dump[1] == 0x44, "starts F0 44, Casio");
        ok(dump[4] == 0x70 && dump[5] == 0x20 && dump[6] == 0x60,
           "channel 0, receive request, temp area");
        ok(dump[7] == 0x0F && dump[8] == 0x05, "5F travels as 0F 05, not 05 0F");
        ok(dump[n - 1] == 0xF7, "ends F7");

        uint8_t back[PD_SYSEX_VOICE];
        ok(pd_sysex_unpack(dump, n, back) == 0, "unpacks");
        ok(back[0] == 0x5F, "and comes back 5F");
    }

    printf("-- every byte of a voice survives packing --\n");
    {
        fill_voice(voice, 7);
        const size_t n = pd_sysex_pack(voice, 3, 0x20, dump, sizeof dump);
        ok(dump[4] == 0x73, "channel 3 lands in the header");
        uint8_t back[PD_SYSEX_VOICE];
        pd_sysex_unpack(dump, n, back);
        ok(memcmp(voice, back, PD_SYSEX_VOICE) == 0, "all 128 bytes identical");
    }

    printf("-- a dump that is not one is refused, not guessed at --\n");
    {
        uint8_t bad[PD_SYSEX_BYTES];
        fill_voice(voice, 1);
        pd_sysex_pack(voice, 0, 0x60, bad, sizeof bad);
        uint8_t out[PD_SYSEX_VOICE];

        ok(pd_sysex_unpack(bad, 10, out) < 0, "too short");
        ok(pd_sysex_unpack(bad, 400, out) < 0, "too long");
        uint8_t t[PD_SYSEX_BYTES];
        memcpy(t, bad, sizeof t); t[0] = 0x00;
        ok(pd_sysex_unpack(t, sizeof t, out) == -3, "no F0");
        memcpy(t, bad, sizeof t); t[1] = 0x43;
        ok(pd_sysex_unpack(t, sizeof t, out) == -2, "Yamaha, not Casio");
        memcpy(t, bad, sizeof t); t[7] = 0x7F;
        ok(pd_sysex_unpack(t, sizeof t, out) == -4, "a half byte with a high half");
        ok(pd_sysex_unpack(NULL, PD_SYSEX_BYTES, out) < 0, "no input at all");
    }

    printf("-- a voice nobody edited comes back byte for byte --\n");
    {
        for (unsigned seed = 1; seed <= 24; seed++) {
            fill_voice(voice, seed);
            pd_sysex_pack(voice, 0, 0x60, dump, sizeof dump);

            pd_patch_t p;
            pd_sysex_report_t rr, wr;
            uint8_t base[PD_SYSEX_VOICE];
            ok(pd_sysex_read_ex(dump, PD_SYSEX_BYTES, &p, base, &rr) == 0,
               "seed %u reads", seed);

            uint8_t out[PD_SYSEX_BYTES], back[PD_SYSEX_VOICE];
            const size_t n = pd_sysex_write_ex(&p, base, 0, 0x60, out, sizeof out, &wr);
            pd_sysex_unpack(out, n, back);
            if (memcmp(voice, back, PD_SYSEX_VOICE) != 0) {
                for (int i = 0; i < PD_SYSEX_VOICE; i++)
                    if (voice[i] != back[i])
                        printf("    seed %u byte %d: %02X became %02X\n",
                               seed, i, voice[i], back[i]);
            }
            ok(memcmp(voice, back, PD_SYSEX_VOICE) == 0,
               "seed %u unchanged through read and write", seed);
        }
    }

    printf("-- editing one thing changes that thing and nothing else --\n");
    {
        fill_voice(voice, 5);
        pd_sysex_pack(voice, 0, 0x60, dump, sizeof dump);
        pd_patch_t p; pd_sysex_report_t rr, wr;
        uint8_t base[PD_SYSEX_VOICE];
        pd_sysex_read_ex(dump, PD_SYSEX_BYTES, &p, base, &rr);

        p.line[0].amp_env.level[5] = (uint8_t)(p.line[0].amp_env.level[5] == 40 ? 70 : 40);

        uint8_t out[PD_SYSEX_BYTES], back[PD_SYSEX_VOICE];
        const size_t n = pd_sysex_write_ex(&p, base, 0, 0x60, out, sizeof out, &wr);
        pd_sysex_unpack(out, n, back);

        int moved = 0, which = -1;
        for (int i = 0; i < PD_SYSEX_VOICE; i++)
            if (voice[i] != back[i]) { moved++; which = i; }
        ok(moved == 1, "exactly one byte moved, got %d", moved);
        ok(which == 21 + 5 * 2 + 1, "and it was the level that was edited (byte %d)", which);
    }

    printf("-- the voice is read back as the same voice --\n");
    {
        fill_voice(voice, 11);
        pd_sysex_pack(voice, 0, 0x60, dump, sizeof dump);
        pd_patch_t a, b; pd_sysex_report_t r;
        uint8_t base[PD_SYSEX_VOICE], out[PD_SYSEX_BYTES];
        pd_sysex_read_ex(dump, PD_SYSEX_BYTES, &a, base, &r);
        pd_sysex_write_ex(&a, base, 0, 0x60, out, sizeof out, &r);
        pd_sysex_read_ex(out, PD_SYSEX_BYTES, &b, NULL, &r);

        ok(a.line_count == b.line_count, "line count holds");
        ok(a.mix == b.mix, "modulation holds");
        for (int i = 0; i < 2; i++) {
            ok(a.line[i].wave == b.line[i].wave, "line %d waveform holds", i + 1);
            const pd_env_params_t *x = &a.line[i].amp_env, *y = &b.line[i].amp_env;
            int same = 1;
            for (int s = 0; s < PD_ENV_STEPS; s++)
                if (x->rate[s] != y->rate[s] || x->level[s] != y->level[s]) same = 0;
            ok(same, "line %d amplitude envelope holds", i + 1);
            ok(x->end_step == y->end_step, "line %d end step holds", i + 1);
        }
    }

    printf("-- what a CZ cannot hold is named, not silently dropped --\n");
    {
        pd_patch_t p;
        memset(&p, 0, sizeof p);
        p.line_count = 4;
        p.filter_mode = PD_FILTER_LOWPASS;
        p.filter_cutoff_hz = 2000.0;
        p.velocity_to_wave = 0.5;
        p.aftertouch_to_level = 0.3;
        p.glide_seconds = 0.2;
        p.spread = 1.0;
        p.mod_to_wave = 0.4;
        p.line[0].semitones = 5;
        for (int i = 0; i < 4; i++) { p.line[i].level = 1.0; }

        uint8_t out[PD_SYSEX_BYTES];
        pd_sysex_report_t rep;
        const size_t n = pd_sysex_write(&p, 0, 0x60, out, sizeof out, &rep);
        ok(n == PD_SYSEX_BYTES, "it writes a dump anyway rather than refusing");
        ok(rep.count >= 8, "and says what it lost: %d notes", rep.count);

        const char *want[] = { "Lines 3 and 4", "Filter", "Velocity", "Pressure",
                               "Glide", "Stereo spread", "Mod wheel",
                               "Semitone offset" };
        for (size_t w = 0; w < sizeof want / sizeof *want; w++) {
            int found = 0;
            for (int i = 0; i < rep.count; i++)
                if (strcmp(rep.note[i].control, want[w]) == 0) found = 1;
            ok(found, "names the control: %s", want[w]);
        }
        for (int i = 0; i < rep.count; i++) {
            ok(rep.note[i].control[0] != '\0', "note %d names a control", i);
            ok(strlen(rep.note[i].what) > 20, "note %d explains itself", i);
        }
    }

    printf("-- a patch written from nothing still says what it means --\n");
    {
        pd_patch_t p; memset(&p, 0, sizeof p);
        p.line_count = 2;
        for (int i = 0; i < 2; i++) {
            p.line[i].level = 1.0;
            p.line[i].wave = PD_SAW;
            for (int s = 0; s < PD_ENV_STEPS; s++) {
                /* a shape that rises then falls, so falling bits must appear */
                p.line[i].amp_env.rate[s]  = (uint8_t)(10 + s * 9);
                p.line[i].amp_env.level[s] = (uint8_t)(s < 4 ? s * 25 : 99 - s * 12);
                p.line[i].wave_env.rate[s]  = (uint8_t)(5 + s * 7);
                p.line[i].wave_env.level[s] = (uint8_t)(s * 11);
                p.line[i].pitch_env.rate[s]  = (uint8_t)(s * 8);
                /* levels either side of the gap in the pitch encoding */
                p.line[i].pitch_env.level[s] = (uint8_t)(s < 4 ? s * 20 : 64 + s * 4);
            }
            p.line[i].amp_env.sustain_step = 5;
            p.line[i].amp_env.end_step = 7;
            p.line[i].wave_env.sustain_step = 4;
            p.line[i].wave_env.end_step = 7;
            p.line[i].pitch_env.sustain_step = 3;
            p.line[i].pitch_env.end_step = 7;
        }
        p.line[1].detune_cents = -14.0;

        uint8_t out[PD_SYSEX_BYTES], v2[PD_SYSEX_VOICE];
        pd_sysex_report_t rep;
        const size_t n = pd_sysex_write(&p, 0, 0x60, out, sizeof out, &rep);
        ok(n == PD_SYSEX_BYTES, "writes without anything to copy from");
        pd_sysex_unpack(out, n, v2);

        ok((v2[21 + 5 * 2 + 1] & 0x80) != 0, "the sustain step is marked");
        ok((v2[38 + 4 * 2 + 1] & 0x80) != 0, "on the waveform envelope too");
        ok((v2[55 + 3 * 2 + 1] & 0x80) != 0, "and on the pitch envelope");
        int falling = 0;
        for (int s = 5; s < 8; s++) if (v2[21 + s * 2] & 0x80) falling++;
        ok(falling == 3, "steps that fall are marked as falling, got %d of 3", falling);
        ok(v2[1] == 1, "a detune downwards sets the sign byte");

        pd_patch_t q; pd_sysex_report_t r2;
        pd_sysex_read(out, n, &q, &r2);
        int lvl_ok = 1, hi_seen = 0;
        for (int s = 0; s < PD_ENV_STEPS; s++) {
            if (q.line[0].pitch_env.level[s] != p.line[0].pitch_env.level[s]) lvl_ok = 0;
            if (p.line[0].pitch_env.level[s] > 63) hi_seen = 1;
        }
        ok(hi_seen, "the test actually used pitch levels above the gap");
        ok(lvl_ok, "and they come back as the same levels");
        ok(q.line[0].amp_env.sustain_step == 5, "the sustain step comes back");
        ok(q.line[1].detune_cents < 0.0, "the detune is still downwards");
    }

    printf("-- an end step past the eighth cannot escape into the dump --\n");
    {
        pd_patch_t p; memset(&p, 0, sizeof p);
        p.line_count = 1; p.line[0].level = 1.0;
        p.line[0].amp_env.end_step = 9;      /* there is no ninth step */
        uint8_t out[PD_SYSEX_BYTES], v2[PD_SYSEX_VOICE];
        pd_sysex_report_t rep;
        const size_t n = pd_sysex_write(&p, 0, 0x60, out, sizeof out, &rep);
        pd_sysex_unpack(out, n, v2);
        ok(v2[20] <= 7, "the end step byte stays inside 0 to 7, got %d", v2[20]);
    }

    /*
     * Everything above this point is a round trip, and a round trip cannot see
     * a change made consistently at both ends: encode and decode can drift
     * together and still agree with each other. These pin the bytes to the
     * published encoding, so the numbers have to be right in themselves.
     */
    printf("-- the encodings match the published ones, not just each other --\n");
    {
        struct { int value; int expect; const char *what; } kRate[] = {
            {  0,   0, "amplitude rate 0"  }, { 50,  60, "amplitude rate 50" },
            { 99, 119, "amplitude rate 99" },
        };
        struct { int value; int expect; const char *what; } kWRate[] = {
            {  0,   8, "waveform rate 0, offset by eight" },
            { 50,  68, "waveform rate 50" }, { 99, 127, "waveform rate 99" },
        };
        struct { int value; int expect; const char *what; } kLevel[] = {
            {  0,   0, "level 0" }, { 50,  64, "level 50" }, { 99, 127, "level 99" },
        };
        struct { int value; int expect; const char *what; } kPitch[] = {
            {  0,   0, "pitch level 0"  }, { 63, 0x3F, "pitch level 63, below the gap" },
            { 64, 0x44, "pitch level 64, above the gap" },
            { 99, 0x67, "pitch level 99" },
        };

        for (size_t i = 0; i < sizeof kRate / sizeof *kRate; i++) {
            pd_patch_t p; memset(&p, 0, sizeof p);
            p.line_count = 1; p.line[0].level = 1.0;
            p.line[0].amp_env.rate[0]   = (uint8_t)kRate[i].value;
            p.line[0].wave_env.rate[0]  = (uint8_t)kWRate[i].value;
            p.line[0].pitch_env.level[0] = 0;
            p.line[0].amp_env.level[0]  = (uint8_t)kLevel[i].value;
            uint8_t o[PD_SYSEX_BYTES], v2[PD_SYSEX_VOICE];
            pd_sysex_report_t r;
            pd_sysex_unpack(o, pd_sysex_write(&p, 0, 0x60, o, sizeof o, &r), v2);
            ok((v2[21] & 0x7F) == kRate[i].expect, "%s is %02X, got %02X",
               kRate[i].what, kRate[i].expect, v2[21] & 0x7F);
            ok((v2[38] & 0x7F) == kWRate[i].expect, "%s is %02X, got %02X",
               kWRate[i].what, kWRate[i].expect, v2[38] & 0x7F);
            ok((v2[22] & 0x7F) == kLevel[i].expect, "%s is %02X, got %02X",
               kLevel[i].what, kLevel[i].expect, v2[22] & 0x7F);
        }
        /* The pitch envelope scales its rates by 127, not 119 like the others. */
        struct { int value; int expect; const char *what; } kPRate[] = {
            {  0,   0, "pitch rate 0" }, { 50,  64, "pitch rate 50" },
            { 99, 127, "pitch rate 99" },
        };
        for (size_t i = 0; i < sizeof kPitch / sizeof *kPitch; i++) {
            pd_patch_t p; memset(&p, 0, sizeof p);
            p.line_count = 1; p.line[0].level = 1.0;
            p.line[0].pitch_env.level[0] = (uint8_t)kPitch[i].value;
            uint8_t o[PD_SYSEX_BYTES], v2[PD_SYSEX_VOICE];
            pd_sysex_report_t r;
            pd_sysex_unpack(o, pd_sysex_write(&p, 0, 0x60, o, sizeof o, &r), v2);
            ok((v2[56] & 0x7F) == kPitch[i].expect, "%s is %02X, got %02X",
               kPitch[i].what, kPitch[i].expect, v2[56] & 0x7F);
        }
        for (size_t i = 0; i < sizeof kPRate / sizeof *kPRate; i++) {
            pd_patch_t p; memset(&p, 0, sizeof p);
            p.line_count = 1; p.line[0].level = 1.0;
            p.line[0].pitch_env.rate[0] = (uint8_t)kPRate[i].value;
            uint8_t o[PD_SYSEX_BYTES], v2[PD_SYSEX_VOICE];
            pd_sysex_report_t r;
            pd_sysex_unpack(o, pd_sysex_write(&p, 0, 0x60, o, sizeof o, &r), v2);
            ok((v2[55] & 0x7F) == kPRate[i].expect, "%s is %02X, got %02X",
               kPRate[i].what, kPRate[i].expect, v2[55] & 0x7F);
        }

        /* Detune fine runs in rows of fifteen with a step at each row. */
        struct { int cents; int expect; } kFine[] = {
            { 0, 0x00 }, { 15, 0x0F }, { 16, 0x11 }, { 30, 0x1F },
            { 31, 0x21 }, { 45, 0x2F }, { 46, 0x31 }, { 60, 0x3F },
        };
        for (size_t i = 0; i < sizeof kFine / sizeof *kFine; i++) {
            pd_patch_t p; memset(&p, 0, sizeof p);
            p.line_count = 2; p.line[0].level = 1.0; p.line[1].level = 1.0;
            p.line[1].detune_cents = kFine[i].cents;
            uint8_t o[PD_SYSEX_BYTES], v2[PD_SYSEX_VOICE];
            pd_sysex_report_t r;
            pd_sysex_unpack(o, pd_sysex_write(&p, 0, 0x60, o, sizeof o, &r), v2);
            ok(v2[2] == kFine[i].expect, "detune of %d cents is %02X, got %02X",
               kFine[i].cents, kFine[i].expect, v2[2]);
        }
    }

    printf("-- editing a rate keeps the falling bit that came with it --\n");
    {
        fill_voice(voice, 3);
        pd_sysex_pack(voice, 0, 0x60, dump, sizeof dump);
        pd_patch_t p; pd_sysex_report_t r;
        uint8_t base[PD_SYSEX_VOICE];
        pd_sysex_read_ex(dump, PD_SYSEX_BYTES, &p, base, &r);

        const int step = 1;                       /* fill_voice marks this one */
        ok((voice[21 + step * 2] & 0x80) != 0, "the voice really does mark it");
        ok(p.line[0].amp_env.level[step] > p.line[0].amp_env.level[step - 1],
           "and its levels rise, so the bit cannot be worked out");

        p.line[0].amp_env.rate[step] = (uint8_t)(p.line[0].amp_env.rate[step] + 20);
        uint8_t o[PD_SYSEX_BYTES], v2[PD_SYSEX_VOICE];
        pd_sysex_unpack(o, pd_sysex_write_ex(&p, base, 0, 0x60, o, sizeof o, &r), v2);
        ok((v2[21 + step * 2] & 0x80) != 0,
           "the bit is still there after the rate changed");
        ok((v2[21 + step * 2] & 0x7F) != (voice[21 + step * 2] & 0x7F),
           "and the rate itself did change");
    }

    printf("-- the CZ's second waveform per line is left alone --\n");
    {
        fill_voice(voice, 9);
        voice[14] = 0x4A;     /* waveform 3, paired with waveform 2 */
        voice[71] = 0x2A;     /* waveform 2, paired with waveform 2 */
        pd_sysex_pack(voice, 0, 0x60, dump, sizeof dump);
        pd_patch_t p; pd_sysex_report_t rr, wr;
        uint8_t base[PD_SYSEX_VOICE];
        pd_sysex_read_ex(dump, PD_SYSEX_BYTES, &p, base, &rr);

        int told = 0;
        for (int i = 0; i < rr.count; i++)
            if (strcmp(rr.note[i].control, "Line 1 waveform") == 0) told = 1;
        ok(told, "reading says the second waveform is not modelled");

        uint8_t o[PD_SYSEX_BYTES], v2[PD_SYSEX_VOICE];
        pd_sysex_unpack(o, pd_sysex_write_ex(&p, base, 0, 0x60, o, sizeof o, &wr), v2);
        ok(v2[14] == 0x4A, "line 1 pairing survives, wanted 4A got %02X", v2[14]);
        ok(v2[71] == 0x2A, "line 2 pairing survives, wanted 2A got %02X", v2[71]);
    }

    printf("-- a buffer too small writes nothing rather than part of a dump --\n");
    {
        pd_patch_t p; memset(&p, 0, sizeof p);
        p.line_count = 1; p.line[0].level = 1.0;
        uint8_t small[64];
        pd_sysex_report_t rep;
        ok(pd_sysex_write(&p, 0, 0x60, small, sizeof small, &rep) == 0,
           "refuses a short buffer");
    }

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
