/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * One place for how the thing looks, so the plugin and the standalone cannot
 * drift apart and so a colour is never chosen twice.
 */
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace pd
{
struct Theme
{
    // A dark panel that is not black: black flattens, and the curves need
    // something to sit against.
    static const juce::Colour bg;         // the window
    static const juce::Colour panel;      // a recessed area
    static const juce::Colour panelEdge;
    static const juce::Colour raised;     // a control that sticks out
    static const juce::Colour raisedOn;
    static const juce::Colour text;
    static const juce::Colour textDim;
    static const juce::Colour textFaint;
    static const juce::Colour lineOne;    // line 1 blue
    static const juce::Colour lineTwo;    // line 2 green
    static const juce::Colour accent;     // amplitude, meters
    static const juce::Colour grid;

    static juce::Font title(float h);
    static juce::Font label(float h);
    static juce::Font mono(float h);

    /* A recessed panel with a soft inner shadow: the shadow is what stops a
     * flat rectangle looking like a rectangle. */
    static void drawRecess(juce::Graphics&, juce::Rectangle<float>, float radius = 6.0f);
    /* A raised control with a light top edge. */
    static void drawRaised(juce::Graphics&, juce::Rectangle<float>, bool on, float radius = 5.0f);
};

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h,
                          float pos, float min, float max,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getLabelFont(juce::Label&) override;
};
} // namespace pd
