#!/usr/bin/env python3
"""
Generates src/pd_presets.c, the factory bank.

These are original designs. Not one is a transcription of a Casio ROM patch.

The bank is generated rather than typed because a preset is a wall of
punctuation, and a generator makes the differences between patches visible
instead of burying them. It also means levelling is done here, in one table,
rather than by rewriting the C afterwards: the first attempt did that with a
regular expression, and the expression matched two fields rather than one, so
every patch it touched had its pitch envelope depth silently overwritten with
a level. Generated code should be regenerated, never edited.

    python3 tools/gen-presets.py
"""
import pathlib

W = dict(saw=0, square=1, pulse=2, dsine=3, sawpulse=4, rsaw=5, rtri=6, rtrap=7)
MIX = dict(both=0, ring=1, noise=2)
FLAT_R, FLAT_L = [99]*8, [99]*8

def env(rates, levels, sus, end):
    r = list(rates) + [0]*(8-len(rates))
    l = list(levels) + [0]*(8-len(levels))
    return "{ {%s}, {%s}, %d, %d }" % (",".join(map(str, r)), ",".join(map(str, l)), sus, end)

FLAT = env(FLAT_R, FLAT_L, 0, 7)

class Line:
    def __init__(self, wave, level=1.0, detune=0.0, octave=0, semis=0,
                 wenv=None, aenv=None, penv=None, pdepth=0.0):
        self.wave, self.level, self.detune = wave, level, detune
        self.octave, self.semis = octave, semis
        self.wenv, self.aenv, self.penv, self.pdepth = wenv or FLAT, aenv or FLAT, penv or FLAT, pdepth
    def c(self, gain):
        return ("{ %d, %d, %d, %.4f, %.4f, %s, %.4f, %s, %s }"
                % (W[self.wave], self.octave, self.semis, self.detune,
                   min(1.0, self.level * gain), self.penv, self.pdepth,
                   self.wenv, self.aenv))

class Preset:
    def __init__(self, name, family, lines, count=1, mix='both', noise=0.0,
                 velw=0.5, velv=0.8, bend=2.0, modw=0.3, gain=1.0):
        self.__dict__.update(locals()); del self.self
        while len(self.lines) < 2:
            self.lines.append(Line('saw', level=0.0))
    def c(self):
        return ('    { "%s", "%s", { { %s, %s }, %d, %d, %.3f, %.3f, %.3f, %.2f, %.3f } },'
                % (self.name, self.family, self.lines[0].c(self.gain),
                   self.lines[1].c(self.gain), self.count, MIX[self.mix],
                   self.noise, self.velw, self.velv, self.bend, self.modw))

L = Line
PRESETS = [
 # --- keys ----------------------------------------------------------------
 Preset("Tine Keys", "Keys", [
   L('saw', .62, -6, wenv=env([60,40,44],[92,24,0],1,2), aenv=env([84,44,50],[99,70,0],1,2)),
   L('rsaw', .34, +6, wenv=env([72,46,48],[99,10,0],1,2), aenv=env([88,50,52],[99,52,0],1,2))],
   2, velw=.85, velv=.75, gain=0.64),
 Preset("Glass Keys", "Keys", [
   L('rtri', .55, -4, wenv=env([66,42,46],[99,18,0],1,2), aenv=env([86,46,50],[99,62,0],1,2)),
   L('dsine', .42, +5, wenv=env([58,40,44],[80,22,0],1,2), aenv=env([84,48,52],[99,66,0],1,2))],
   2, velw=.8, velv=.7, gain=0.78),
 Preset("Wood Piano", "Keys", [
   L('saw', .66, -3, wenv=env([62,38,42],[86,20,0],1,2), aenv=env([88,42,48],[99,58,0],1,2)),
   L('square', .32, +3, wenv=env([64,40,44],[70,14,0],1,2), aenv=env([88,44,50],[99,50,0],1,2))],
   2, velw=.9, velv=.8, gain=0.56),
 Preset("Clav Bite", "Keys", [
   L('sawpulse', .70, 0, wenv=env([78,52,56],[99,26,0],1,2), aenv=env([92,54,62],[99,44,0],1,2))],
   1, velw=.9, velv=.85, gain=1.00),
 # --- bass ----------------------------------------------------------------
 Preset("Deep Bass", "Bass", [
   L('saw', .78, 0, octave=-1, wenv=env([70,44,50],[82,18,0],1,2), aenv=env([88,46,54],[99,72,0],1,2))],
   1, velw=.7, velv=.85, gain=0.52),
 Preset("Reso Bass", "Bass", [
   L('rsaw', .74, 0, octave=-1, wenv=env([66,40,48],[94,14,0],1,2), aenv=env([90,48,54],[99,66,0],1,2))],
   1, velw=.9, velv=.8, gain=0.96),
 Preset("Slap Bass", "Bass", [
   L('sawpulse', .72, -2, octave=-1, wenv=env([86,56,58],[99,20,0],1,2), aenv=env([94,58,64],[99,40,0],1,2)),
   L('square', .30, +2, octave=-1, wenv=env([82,54,56],[76,12,0],1,2), aenv=env([94,60,64],[99,34,0],1,2))],
   2, velw=.95, velv=.9, gain=0.78),
 Preset("Sub Round", "Bass", [
   L('dsine', .82, 0, octave=-1, wenv=env([54,36,44],[50,10,0],1,2), aenv=env([86,44,52],[99,78,0],1,2))],
   1, velw=.4, velv=.8, gain=0.46),
 # --- the sweep the machine is known for ----------------------------------
 Preset("Reso Sweep", "Sweep", [
   L('rsaw', .68, -5, wenv=env([44,34,42],[99,16,0],1,2), aenv=env([76,48,50],[99,84,0],1,2)),
   L('rtrap', .40, +5, wenv=env([42,32,40],[92,12,0],1,2), aenv=env([76,50,50],[99,78,0],1,2))],
   2, velw=.7, velv=.7, gain=0.70),
 Preset("Slow Open", "Sweep", [
   L('saw', .62, -7, wenv=env([30,26,38],[99,30,0],1,2), aenv=env([56,44,48],[99,88,0],1,2)),
   L('saw', .52, +7, wenv=env([28,24,36],[95,26,0],1,2), aenv=env([54,42,46],[99,86,0],1,2))],
   2, velw=.5, velv=.6, gain=0.46),
 Preset("Formant Rise", "Sweep", [
   L('rtri', .66, 0, wenv=env([34,30,40],[99,22,0],1,2), aenv=env([62,46,48],[99,82,0],1,2))],
   1, velw=.6, velv=.7, gain=0.92),
 # --- pads ----------------------------------------------------------------
 Preset("Warm Pad", "Pad", [
   L('saw', .56, -9, wenv=env([26,24,34],[72,34,0],1,2), aenv=env([40,38,40],[99,90,0],1,2)),
   L('dsine', .48, +9, wenv=env([24,22,32],[64,30,0],1,2), aenv=env([38,36,38],[99,88,0],1,2))],
   2, velw=.35, velv=.5, gain=0.62),
 Preset("Glass Pad", "Pad", [
   L('rtrap', .52, -8, wenv=env([28,26,36],[88,26,0],1,2), aenv=env([44,40,42],[99,86,0],1,2)),
   L('rtri', .46, +8, wenv=env([26,24,34],[82,22,0],1,2), aenv=env([42,38,40],[99,84,0],1,2))],
   2, velw=.4, velv=.5, gain=0.82),
 Preset("Choir Air", "Pad", [
   L('dsine', .54, -6, wenv=env([30,26,36],[58,28,0],1,2), aenv=env([46,40,42],[99,88,0],1,2)),
   L('saw', .40, +6, wenv=env([28,24,34],[50,24,0],1,2), aenv=env([44,38,40],[99,84,0],1,2))],
   2, mix='noise', noise=.18, velw=.3, velv=.5, gain=0.60),
 Preset("Dark Drift", "Pad", [
   L('square', .50, -11, wenv=env([22,20,32],[54,24,0],1,2), aenv=env([34,34,38],[99,92,0],1,2)),
   L('saw', .44, +11, wenv=env([20,18,30],[46,20,0],1,2), aenv=env([32,32,36],[99,90,0],1,2))],
   2, velw=.3, velv=.45, gain=0.70),
 # --- brass and strings ---------------------------------------------------
 Preset("Brass Section", "Brass", [
   L('saw', .60, -5, wenv=env([52,40,46],[90,46,0],1,2), aenv=env([62,46,50],[99,84,0],1,2)),
   L('saw', .50, +5, wenv=env([50,38,44],[84,42,0],1,2), aenv=env([60,44,48],[99,82,0],1,2))],
   2, velw=.8, velv=.7, gain=0.42),
 Preset("Soft Horn", "Brass", [
   L('dsine', .62, -3, wenv=env([44,36,44],[72,38,0],1,2), aenv=env([52,42,48],[99,86,0],1,2))],
   1, velw=.65, velv=.65, gain=0.56),
 Preset("Bow Strings", "Strings", [
   L('saw', .54, -10, wenv=env([34,30,38],[76,36,0],1,2), aenv=env([46,40,44],[99,88,0],1,2)),
   L('saw', .50, +10, wenv=env([32,28,36],[70,32,0],1,2), aenv=env([44,38,42],[99,86,0],1,2))],
   2, velw=.55, velv=.6, gain=0.52),
 # --- bells, plucks -------------------------------------------------------
 Preset("Tube Bell", "Bell", [
   L('rtri', .60, 0, semis=7, wenv=env([76,40,44],[99,12,0],1,2), aenv=env([92,34,40],[99,30,0],1,2)),
   L('dsine', .38, 0, wenv=env([72,38,42],[80,10,0],1,2), aenv=env([92,32,38],[99,24,0],1,2))],
   2, velw=.85, velv=.85, gain=0.89),
 # Ring modulation multiplies the lines, so both have to sit near the top to
 # reach the bank's level, and line two's amplitude envelope stays open: in
 # ring mode it is a modulator, and closing it closes the whole sound twice.
 Preset("Metal Struck", "Bell", [
   L('rtrap', 1.0, 0, wenv=env([84,44,48],[99,10,0],1,2), aenv=env([96,40,44],[99,18,0],1,2)),
   L('saw', 1.0, 0, semis=5, wenv=env([84,44,48],[88,8,0],1,2), aenv=env([99,99,99],[99,99,0],1,2))],
   2, mix='ring', velw=.8, velv=.9, gain=1.00),
 Preset("Pluck Harp", "Pluck", [
   L('saw', .66, 0, wenv=env([80,46,50],[92,14,0],1,2), aenv=env([94,42,48],[99,32,0],1,2))],
   1, velw=.85, velv=.85, gain=0.94),
 Preset("Mallet Soft", "Pluck", [
   L('dsine', .68, 0, wenv=env([76,44,48],[64,10,0],1,2), aenv=env([92,40,46],[99,36,0],1,2))],
   1, velw=.6, velv=.85, gain=0.86),
 # --- leads and effects ---------------------------------------------------
 Preset("Square Lead", "Lead", [
   L('square', .66, -4, wenv=env([70,48,52],[88,40,0],1,2), aenv=env([88,50,54],[99,88,0],1,2)),
   L('square', .40, +4, wenv=env([68,46,50],[80,36,0],1,2), aenv=env([86,48,52],[99,84,0],1,2))],
   2, velw=.7, velv=.6, gain=0.36),
 Preset("Reso Lead", "Lead", [
   L('rsaw', .70, 0, wenv=env([62,44,50],[96,34,0],1,2), aenv=env([90,50,54],[99,86,0],1,2))],
   1, velw=.9, velv=.65, modw=.6, gain=0.80),
 # A gain on a ring patch lands twice, because the output is the product of the
 # two lines: halving both quarters the result. The figure here is the square
 # root of the change that was wanted.
 Preset("Ring Lead", "Lead", [
   L('saw', 1.0, 0, wenv=env([66,46,50],[90,30,0],1,2), aenv=env([88,50,54],[99,84,0],1,2)),
   L('square', 1.0, 0, semis=7, wenv=env([64,44,48],[80,26,0],1,2), aenv=env([99,99,99],[99,99,0],1,2))],
   2, mix='ring', velw=.75, velv=.7, gain=0.66),
 Preset("Breath Reed", "Lead", [
   L('rtri', .64, 0, wenv=env([50,40,46],[82,30,0],1,2), aenv=env([70,48,52],[99,86,0],1,2))],
   1, mix='noise', noise=.32, velw=.7, velv=.7, modw=.5, gain=0.88),
 Preset("Noise Wash", "Effect", [
   L('rtrap', .58, -7, wenv=env([26,24,34],[90,28,0],1,2), aenv=env([38,36,40],[99,88,0],1,2)),
   L('saw', .36, +7, wenv=env([24,22,32],[70,24,0],1,2), aenv=env([36,34,38],[99,84,0],1,2))],
   2, mix='noise', noise=.55, velw=.4, velv=.5, gain=0.78),
 Preset("Drop Pitch", "Effect", [
   L('saw', .70, 0, pdepth=-12.0, penv=env([70,50,50],[99,40,0],1,2),
     wenv=env([64,44,48],[92,20,0],1,2), aenv=env([90,44,50],[99,60,0],1,2))],
   1, velw=.7, velv=.8, gain=0.62),
]

HEADER = '''/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * GENERATED by tools/gen-presets.py. Edit that, not this.
 *
 * The factory bank: original designs, see pd_presets.h. The field order below
 * follows pd_line_params_t and pd_patch_t exactly, so a change to either shows
 * up as a compiler error rather than as a patch that quietly plays something
 * else.
 */
#include "pd_presets.h"

static const pd_preset_t kPresets[] = {
%s
};

int pd_preset_count(void) { return (int)(sizeof kPresets / sizeof kPresets[0]); }

const pd_preset_t *pd_preset(int i)
{
    if (i < 0 || i >= pd_preset_count()) return 0;
    return &kPresets[i];
}
'''

out = HEADER % "\n".join(p.c() for p in PRESETS)
pathlib.Path(__file__).resolve().parent.parent.joinpath('src/pd_presets.c').write_text(out)
print("wrote %d presets" % len(PRESETS))
