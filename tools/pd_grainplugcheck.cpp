/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Does granular survive the trip through the plugin?
 *
 * test_grain checks the engine, where a patch is a struct and nothing is lost
 * getting to it. The plugin does not work that way. A preset is written out to
 * the parameters, which are floats with ranges and skews, and then read back
 * into a patch on every block. Either half of that can be forgotten without
 * anything failing to compile and without a single engine test noticing: the
 * cloud would simply never appear, or appear and never turn off, and the first
 * person to find out would be somebody playing it.
 *
 * That is not hypothetical. Three separate parameter bugs in this plugin were
 * found only by taking a patch out through the parameters and back, and none
 * of them was visible anywhere else.
 *
 * So this goes both ways and then listens: the parameters have to hold what
 * the patch said, and the audio has to actually change when the switch moves.
 */
#include <juce_audio_processors/juce_audio_processors.h>
#include "../plugin/PluginProcessor.h"
#include "pd_presets.h"
#include <cstdio>
#include <cmath>

static int failures = 0;

static void say(const juce::String& s)
{
    std::fputs(s.toRawUTF8(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

static void ok(bool cond, const juce::String& what)
{
    if (cond) { say("  ok    " + what); return; }
    failures++;
    say("  FAIL  " + what);
}

static float param(pd::Processor& p, const juce::String& id)
{
    auto* v = p.apvts.getRawParameterValue(id);
    return v ? v->load() : -12345.0f;
}

/* Play a note and hand back what came out. */
static void renderNote(pd::Processor& p, std::vector<float>& out, int blocks = 40)
{
    const int bs = 512;
    p.prepareToPlay(48000.0, bs);
    juce::AudioBuffer<float> buf(2, bs);
    out.clear();

    for (int b = 0; b < blocks; b++) {
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        p.processBlock(buf, midi);
        const float* l = buf.getReadPointer(0);
        for (int i = 0; i < bs; i++) out.push_back(l[i]);
    }
}

static double rms(const std::vector<float>& v)
{
    double s = 0;
    for (float x : v) s += (double)x * x;
    return v.empty() ? 0.0 : std::sqrt(s / (double)v.size());
}

static double difference(const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min(a.size(), b.size());
    double s = 0;
    for (size_t i = 0; i < n; i++) { const double d = a[i] - b[i]; s += d * d; }
    return n ? std::sqrt(s / (double)n) : 0.0;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    say("granular through the plugin\n");

    /* ---- a patch carrying grains has to reach the parameters ---- */
    say("a patch reaches the parameters");
    {
        pd::Processor p;
        pd_patch_t q;
        pd_patch_init(&q);
        q.line_count = 1;
        q.line[0].level = 0.8;
        q.line[0].grain.on           = 1;
        q.line[0].grain.shape        = PD_GRAIN_TRIANGLE;
        q.line[0].grain.edge         = 0.7;
        q.line[0].grain.length_ms    = 17.5;
        q.line[0].grain.overlap      = 3.25;
        q.line[0].grain.onset_spread = 0.6;
        q.line[0].grain.pitch_spread = 0.35;
        p.applyPatch(q);

        ok(param(p, "l1_grain_on") > 0.5f, "the switch is on");
        ok((int)param(p, "l1_grain_shape") == PD_GRAIN_TRIANGLE, "the shape is the one asked for");
        ok(std::abs(param(p, "l1_grain_edge") - 0.7f) < 0.01f,
           "edge survives (" + juce::String(param(p, "l1_grain_edge"), 3) + ")");
        ok(std::abs(param(p, "l1_grain_len") - 17.5f) < 0.2f,
           "length survives the skew (" + juce::String(param(p, "l1_grain_len"), 2) + " ms)");
        ok(std::abs(param(p, "l1_grain_overlap") - 3.25f) < 0.05f,
           "overlap survives the skew (" + juce::String(param(p, "l1_grain_overlap"), 3) + ")");
        ok(std::abs(param(p, "l1_grain_onset") - 0.6f) < 0.01f, "scatter survives");
        ok(std::abs(param(p, "l1_grain_pitch") - 0.35f) < 0.01f, "detune survives");
    }

    /* ---- and the parameters have to reach the sound ---- */
    say("\nthe parameters reach the sound");
    {
        pd_patch_t q;
        pd_patch_init(&q);
        q.line_count = 1;
        q.line[0].level = 0.8;
        q.line[0].grain.on        = 1;
        q.line[0].grain.shape     = PD_GRAIN_ROADSIAN;
        q.line[0].grain.length_ms = 12.0;
        q.line[0].grain.overlap   = 2.0;

        pd::Processor on;
        on.applyPatch(q);
        std::vector<float> withGrains;
        renderNote(on, withGrains);

        q.line[0].grain.on = 0;
        pd::Processor off;
        off.applyPatch(q);
        std::vector<float> without;
        renderNote(off, without);

        ok(rms(withGrains) > 1e-4, "a granular patch makes a sound at all ("
           + juce::String(rms(withGrains), 5) + ")");
        ok(rms(without) > 1e-4, "and so does the same patch with grains off");

        const double d = difference(withGrains, without);
        ok(d > 1e-3,
           "turning grains on actually changes what comes out (difference "
           + juce::String(d, 5) + ")");

        /*
         * The trap this is really here for: a switch that is written to the
         * parameters but never read back would leave both renders identical,
         * every engine test would still pass, and the control would do nothing
         * in the only place anyone touches it.
         */
        ok(d > rms(without) * 0.05,
           "and changes it by more than rounding (" + juce::String(d, 5)
           + " against " + juce::String(rms(without), 5) + ")");
    }

    /* ---- the factory presets that say they are granular have to be ---- */
    say("\nthe factory bank");
    {
        pd::Processor p;
        int granular = 0;
        for (int i = 0; i < pd_preset_count(); i++) {
            const pd_preset_t* pr = pd_preset(i);
            if (juce::String(pr->family) != "Granular") continue;
            granular++;
            p.loadPreset(i);
            const bool anyOn = param(p, "l1_grain_on") > 0.5f
                            || param(p, "l2_grain_on") > 0.5f;
            ok(anyOn, juce::String(pr->name) + " loads with its grains switched on");
            ok(param(p, "l1_grain_len") > 0.5f,
               juce::String(pr->name) + " has a grain length rather than zero ("
               + juce::String(param(p, "l1_grain_len"), 1) + " ms)");
        }
        ok(granular > 0, "the bank has granular presets to check (" + juce::String(granular) + ")");

        /* and a preset that is not granular must not arrive switched on */
        for (int i = 0; i < pd_preset_count(); i++) {
            const pd_preset_t* pr = pd_preset(i);
            if (juce::String(pr->family) == "Granular") continue;
            p.loadPreset(i);
            if (param(p, "l1_grain_on") > 0.5f || param(p, "l2_grain_on") > 0.5f) {
                ok(false, juce::String(pr->name) + " is not granular but loads switched on");
                break;
            }
        }
        ok(true, "no ordinary preset arrives with grains switched on");
    }

    /* ---- the hardware-only view ---- */
    say("\nthe CZ ONLY view");
    {
        pd::Processor p;
        ok(!p.czOnly(), "everything is shown by default");

        p.setCzOnly(true);
        ok(p.czOnly(), "and the view can be switched");

        /*
         * A view preference that does not survive a reload is the kind of
         * thing nobody notices until they have set it twenty times. It rides
         * in the state tree beside the parameters, so this checks it comes
         * back rather than assuming it does.
         */
        juce::MemoryBlock state;
        p.getStateInformation(state);

        pd::Processor q;
        q.setStateInformation(state.getData(), (int)state.getSize());
        ok(q.czOnly(), "and it survives being saved and reloaded");

        pd::Processor r;
        juce::MemoryBlock plain;
        r.getStateInformation(plain);
        pd::Processor t;
        t.setCzOnly(true);
        t.setStateInformation(plain.getData(), (int)plain.getSize());
        ok(!t.czOnly(), "a session saved without it does not turn it on");

        /* And the editor has to build in both, since half the panel is being
         * hidden and the layout arithmetic changes with it. */
        for (int pass = 0; pass < 2; pass++) {
            pd::Processor e;
            e.setCzOnly(pass == 1);
            std::unique_ptr<juce::AudioProcessorEditor> ed(e.createEditor());
            ed->setSize(1040, 900);
            ok(ed != nullptr && ed->getWidth() == 1040,
               pass ? "the editor builds with the CZ view on"
                    : "the editor builds with the CZ view off");
        }
    }

    say(failures ? "\n" + juce::String(failures) + " failure(s)" : "\nall good");
    return failures ? 1 : 0;
}
