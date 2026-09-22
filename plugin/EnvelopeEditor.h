/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The eight step envelope, drawn and dragged.
 *
 * A CZ is an eight step envelope machine, so this is the middle of the window
 * rather than something behind a menu. The levels are handles on a curve and
 * the rates are bars beneath, which is the pair of decisions the hardware asks
 * for. The sustain and end steps are markers you click, because that is what
 * turns eight arbitrary points into an envelope that responds to a key.
 */
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

extern "C" {
#include "pd_env.h"
}

namespace pd
{
class EnvelopeEditor : public juce::Component,
                       private juce::Timer
{
public:
    EnvelopeEditor(juce::AudioProcessorValueTreeState& state);

    /* Which envelope is on screen: line 0 or 1, envelope 0 pitch, 1 wave, 2 amp. */
    void show(int line, int env);
    void setAccent(juce::Colour c) { accent = c; repaint(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::String id(const char* what, int step = -1) const;
    int   value(const char* what, int step = -1) const;
    void  setValue(const char* what, int step, int v, int hi);

    juce::Rectangle<float> curveArea() const;
    juce::Rectangle<float> rateArea() const;
    juce::Rectangle<float> markerArea() const;
    juce::Point<float>     handleFor(int step) const;
    float                  stepWidth() const;

    juce::AudioProcessorValueTreeState& apvts;
    int line = 0, env = 1;
    juce::Colour accent { Theme::lineOne };

    enum class Drag { none, level, rate };
    Drag drag = Drag::none;
    int  dragStep = 0;
    int  hover = -1;
};
} // namespace pd
