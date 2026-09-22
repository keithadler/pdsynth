/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The bend and the wave it makes, live.
 *
 * This is the argument for the whole idea, so it is on the front panel rather
 * than in a manual: the phase bend on the left, the waveform it produces on
 * the right, both moving as the envelope moves. The resonant waveforms bend
 * nothing at all, so for those the left picture shows the window they are
 * shaped by instead, and says which harmonic they are riding. Drawing an
 * undistorted ramp there would claim that the most distinctive sound the
 * machine makes involves no distortion.
 */
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

extern "C" {
#include "pd_osc.h"
}

namespace pd
{
class WaveDisplay : public juce::Component
{
public:
    void setState(pd_wave_t w, float bend, juce::Colour c)
    {
        if (w == wave && std::abs(bend - amount) < 0.002f) return;
        wave = w; amount = bend; colour = c;
        repaint();
    }
    void paint(juce::Graphics&) override;

private:
    pd_wave_t    wave = PD_SAW;
    float        amount = 0.0f;
    juce::Colour colour { Theme::lineOne };
};

/* The output, drawn from the processor's ring. */
class Scope : public juce::Component
{
public:
    void setSamples(const float* s, int n) { data.assign(s, s + n); repaint(); }
    void paint(juce::Graphics&) override;
private:
    std::vector<float> data;
};
} // namespace pd
