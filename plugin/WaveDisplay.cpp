/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "WaveDisplay.h"

namespace pd
{
void WaveDisplay::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto left  = r.removeFromLeft(r.getWidth() * 0.42f).reduced(0.0f, 0.0f);
    r.removeFromLeft(8.0f);
    auto right = r;

    const bool resonant = wave >= PD_RESO_SAW;

    Theme::drawRecess(g, left, 6.0f);
    auto la = left.reduced(6.0f);
    g.setColour(Theme::grid);
    if (!resonant) g.drawLine(la.getX(), la.getBottom(), la.getRight(), la.getY(), 1.0f);

    juce::Path bend;
    for (int i = 0; i <= (int)la.getWidth(); i++) {
        double p = i / (double)la.getWidth();
        double q = resonant ? pd_window(p, wave) : pd_distort(p, wave, amount);
        float x = la.getX() + (float)i;
        float y = la.getBottom() - (float)q * la.getHeight();
        if (i == 0) bend.startNewSubPath(x, y); else bend.lineTo(x, y);
    }
    g.setColour(colour);
    g.strokePath(bend, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));

    g.setFont(Theme::label(10.0f));
    g.setColour(Theme::textDim);
    g.drawText(resonant ? "WINDOW x" + juce::String(pd_resonant_harmonic(amount)) : "PHASE BEND",
               left.reduced(6.0f, 4.0f), juce::Justification::topLeft, false);

    Theme::drawRecess(g, right, 6.0f);
    auto ra = right.reduced(6.0f);
    g.setColour(Theme::grid);
    g.drawLine(ra.getX(), ra.getCentreY(), ra.getRight(), ra.getCentreY(), 1.0f);

    pd_osc_t o;
    pd_osc_init(&o);
    pd_osc_set_freq(&o, 1.0, (double)ra.getWidth());
    juce::Path w;
    for (int i = 0; i <= (int)ra.getWidth(); i++) {
        double s = pd_osc_next(&o, wave, amount);
        float x = ra.getX() + (float)i;
        float y = ra.getCentreY() - (float)s * (ra.getHeight() * 0.46f);
        if (i == 0) w.startNewSubPath(x, y); else w.lineTo(x, y);
    }
    g.setColour(colour);
    g.strokePath(w, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
    g.setColour(Theme::textDim);
    g.drawText("WAVEFORM", right.reduced(6.0f, 4.0f), juce::Justification::topLeft, false);
}

void Scope::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    Theme::drawRecess(g, r, 6.0f);
    auto a = r.reduced(6.0f);
    g.setColour(Theme::grid);
    g.drawLine(a.getX(), a.getCentreY(), a.getRight(), a.getCentreY(), 1.0f);
    if (data.empty()) return;

    juce::Path p;
    for (int i = 0; i <= (int)a.getWidth(); i++) {
        int idx = juce::jlimit(0, (int)data.size() - 1,
                               (int)(i / a.getWidth() * (float)data.size()));
        float x = a.getX() + (float)i;
        float y = a.getCentreY() - data[(size_t)idx] * (a.getHeight() * 0.46f) * 3.0f;
        y = juce::jlimit(a.getY(), a.getBottom(), y);
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    g.setColour(Theme::accent);
    g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved));
    g.setFont(Theme::label(10.0f));
    g.setColour(Theme::textDim);
    g.drawText("OUTPUT", r.reduced(6.0f, 4.0f), juce::Justification::topLeft, false);
}
} // namespace pd
