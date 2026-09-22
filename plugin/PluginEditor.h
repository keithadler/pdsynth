/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The window. The same one in every format and on every platform, because a
 * plugin and its standalone drifting apart is how a synth ends up with two
 * personalities.
 */
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>   // MidiKeyboardComponent
#include "PluginProcessor.h"
#include "EnvelopeEditor.h"
#include "WaveDisplay.h"
#include "Theme.h"
#include <set>

namespace pd
{
class Editor : public juce::AudioProcessorEditor,
               private juce::Timer,
               private juce::MidiKeyboardState::Listener
{
public:
    explicit Editor(Processor&);
    ~Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /* Two octaves on the computer keys. There is no drawn piano: a picture of
     * a keyboard is a picture, and the keys under your hands are a keyboard. */
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;

private:
    void timerCallback() override;
    void selectLine(int);
    void selectEnv(int);
    void syncAttachments();
    void refreshToggles();

    Processor& proc;
    LookAndFeel lnf;

    int currentLine = 0, currentEnv = 1;

    /* Text is set in the constructor: TextButton's String constructor is
     * explicit, so an array of them cannot be brace initialised from literals. */
    juce::ComboBox   presetBox;
    juce::TextButton prevPreset, nextPreset;
    juce::TextButton lineBtn[PD_MAX_LINES];
    juce::TextButton lineCountBtn;      /* how many lines are running */
    juce::ComboBox   filterBox, driveBox;
    juce::Slider     choMix, choDepth, choRate, dlyMix, dlyTime, dlyFb, drvAmount;
    juce::Label      choMixL, choDepthL, choRateL, dlyMixL, dlyTimeL, dlyFbL, drvAmountL, fxL;
    juce::Slider     glide, atWave, cutoff, resonance, filtEnv;
    juce::Label      glideL, atWaveL, cutoffL, resonanceL, filtEnvL, filterL;
    juce::TextButton envBtn[3];
    juce::TextButton waveBtn[PD_WAVE_COUNT];
    juce::TextButton mixBtn[3];

    juce::Slider detune, level, pitchDepth, noise, velWave, velLevel;
    juce::Label  detuneL, levelL, pitchDepthL, noiseL, velWaveL, velLevelL;

    EnvelopeEditor envEditor;
    WaveDisplay    waveView[2];
    Scope          scope;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SA> aDetune, aLevel, aPitchDepth, aNoise, aVelWave, aVelLevel;
    std::unique_ptr<SA> aGlide, aAtWave, aCutoff, aRes, aFiltEnv;
    std::unique_ptr<SA> aChoMix, aChoDepth, aChoRate, aDlyMix, aDlyTime, aDlyFb, aDrvAmount;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<CA> aFilter, aDrive;

    static int noteForKey(int keyCode);
    std::set<int> heldKeys;
    int octaveShift = 0;

    /* The keyboard on screen and the keys under your hands both end up in the
     * same place: one MIDI stream into the synth. */
    void handleNoteOn(juce::MidiKeyboardState*, int ch, int note, float vel) override;
    void handleNoteOff(juce::MidiKeyboardState*, int ch, int note, float vel) override;

    /* Bend springs back when released, as a wheel does; mod stays put. */
    juce::Slider bendWheel, modWheel;
    juce::Label  bendWheelL, modWheelL;

    juce::MidiKeyboardState      kbState;
    juce::MidiKeyboardComponent  keyboard { kbState, juce::MidiKeyboardComponent::horizontalKeyboard };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};
} // namespace pd
