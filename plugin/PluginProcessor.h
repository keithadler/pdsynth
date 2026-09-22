/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The processor. It owns the parameters and a pool of voices, and it does no
 * synthesis of its own: every sample comes from the same plain C engine the
 * tests exercise, so the plugin cannot sound different from the thing that was
 * measured.
 */
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>   // MidiMessageCollector
#include <atomic>
#include <vector>

extern "C" {
#include "pd_voice.h"
#include "pd_synth.h"
#include "pd_presets.h"
}

namespace pd
{
/* The parameter identities, built once and shared by the processor and the
 * editor so a typo cannot silently create a second parameter. */
struct Ids
{
    static juce::String line(int l, const char* what)
    { return "l" + juce::String(l + 1) + "_" + what; }
    static juce::String env(int l, int e, const char* what, int step = -1)
    {
        static const char* kEnv[] = { "pitch", "wave", "amp" };
        auto id = "l" + juce::String(l + 1) + "_" + kEnv[e] + "_" + what;
        return step >= 0 ? id + juce::String(step + 1) : id;
    }
};

class Processor : public juce::AudioProcessor,
                  private juce::MidiInputCallback
{
public:
    Processor();
    ~Processor() override = default;

    void prepareToPlay(double sampleRate, int blockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "pdsynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "pdsynth"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    /* Loading a preset writes the parameters, so the host sees a parameter
     * change it can undo and automate rather than state swapped behind it. */
    void loadPreset(int index);
    int  currentPreset = 0;

    /* Notes played on the computer keyboard. They join the same MIDI stream the
     * host sends, so there is one path into the synth rather than two. */
    juce::MidiMessageCollector uiNotes;

    /* What the editor draws its meters from. Written on the audio thread and
     * read on the message thread, so both are plain atomics rather than
     * anything that could block the audio. */
    std::atomic<float> bendNow[2] { 0.0f, 0.0f };
    std::atomic<int>   voicesNow  { 0 };
    std::atomic<float> levelNow   { 0.0f };

    /* A ring of recent output for the scope. */
    static constexpr int kScope = 1024;
    std::array<std::atomic<float>, kScope> scope {};
    std::atomic<int> scopePos { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void pullParameters();          // parameters -> the C patch

    /* Standalone only: publish a port called "pdsynth" so a DAW, a keyboard or
     * a script can play it without anyone opening a settings dialog first. In
     * a plugin the host already delivers MIDI, so this stays closed. */
    void openVirtualMidi();
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
    std::unique_ptr<juce::MidiInput> virtualIn;

    pd_patch_t patch {};
    double wheelBend = 0.0, wheelMod = 0.0;   /* where the wheels are now */
    pd_synth_t synth {};
    double sr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};
} // namespace pd
