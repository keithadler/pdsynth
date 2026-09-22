/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "EnvelopeEditor.h"
#include "PluginProcessor.h"

namespace pd
{
static constexpr float kMarkerH = 30.0f;
static constexpr float kRateH   = 100.0f;
static constexpr float kGap     = 16.0f;

EnvelopeEditor::EnvelopeEditor(juce::AudioProcessorValueTreeState& state) : apvts(state)
{
    setWantsKeyboardFocus(false);
    startTimerHz(24);      // the host can move these too
}

void EnvelopeEditor::show(int l, int e) { line = l; env = e; repaint(); }
void EnvelopeEditor::timerCallback() { repaint(); }

juce::String EnvelopeEditor::id(const char* what, int step) const
{ return Ids::env(line, env, what, step); }

int EnvelopeEditor::value(const char* what, int step) const
{
    if (auto* p = apvts.getRawParameterValue(id(what, step))) return (int)p->load();
    return 0;
}

void EnvelopeEditor::setValue(const char* what, int step, int v, int hi)
{
    if (auto* p = apvts.getParameter(id(what, step))) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, (float)v / (float)hi));
        p->endChangeGesture();
    }
}

juce::Rectangle<float> EnvelopeEditor::curveArea() const
{
    auto r = getLocalBounds().toFloat();
    return r.withHeight(r.getHeight() - kMarkerH - kRateH - kGap);
}
juce::Rectangle<float> EnvelopeEditor::markerArea() const
{
    auto r = getLocalBounds().toFloat();
    return r.withTop(curveArea().getBottom()).withHeight(kMarkerH);
}
juce::Rectangle<float> EnvelopeEditor::rateArea() const
{
    auto r = getLocalBounds().toFloat();
    return r.withTop(markerArea().getBottom() + kGap).withHeight(kRateH);
}
float EnvelopeEditor::stepWidth() const
{ return curveArea().getWidth() / (float)PD_ENV_STEPS; }

juce::Point<float> EnvelopeEditor::handleFor(int step) const
{
    auto a = curveArea().reduced(0.0f, 10.0f);
    float w = stepWidth();
    float x = curveArea().getX() + w * (step + 0.5f);
    float y = a.getBottom() - (value("level", step) / 99.0f) * a.getHeight();
    return { x, y };
}

void EnvelopeEditor::resized() {}

void EnvelopeEditor::paint(juce::Graphics& g)
{
    const int sustain = value("sustain");
    const int end     = value("end");

    // ---- the curve ------------------------------------------------------
    auto ca = curveArea();
    Theme::drawRecess(g, ca, 7.0f);

    g.setColour(Theme::grid);
    for (int i = 1; i < PD_ENV_STEPS; i++) {
        float x = ca.getX() + stepWidth() * i;
        g.drawLine(x, ca.getY() + 4, x, ca.getBottom() - 4, 1.0f);
    }
    for (int i = 1; i < 4; i++) {
        float y = ca.getY() + ca.getHeight() * i / 4.0f;
        g.drawLine(ca.getX() + 4, y, ca.getRight() - 4, y, 1.0f);
    }

    // the steps after the end step are not played, so they are drawn as not played
    juce::Path live, dead;
    auto start = juce::Point<float>(ca.getX(), curveArea().reduced(0.0f, 10.0f).getBottom());
    live.startNewSubPath(start);
    for (int i = 0; i < PD_ENV_STEPS; i++) {
        auto p = handleFor(i);
        if (i <= end) live.lineTo(p);
        else { if (dead.isEmpty()) dead.startNewSubPath(handleFor(end)); dead.lineTo(p); }
    }

    // a soft fill under the played part, which is what makes it read as an envelope
    {
        juce::Path fill = live;
        fill.lineTo(handleFor(juce::jmin(end, PD_ENV_STEPS - 1)).x, ca.getBottom() - 10.0f);
        fill.lineTo(start.x, ca.getBottom() - 10.0f);
        fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.22f), ca.getCentreX(), ca.getY(),
                                               accent.withAlpha(0.02f), ca.getCentreX(), ca.getBottom(), false));
        g.fillPath(fill);
    }

    g.setColour(accent);
    g.strokePath(live, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    if (!dead.isEmpty()) {
        g.setColour(Theme::textFaint.withAlpha(0.6f));
        juce::Path dashed;
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType(1.4f).createDashedStroke(dashed, dead, dashes, 2);
        g.fillPath(dashed);
    }

    // the sustain region, shaded, so it is obvious where a held key waits
    if (sustain <= end) {
        float x = handleFor(sustain).x;
        g.setColour(Theme::accent.withAlpha(0.07f));
        g.fillRect(juce::Rectangle<float>(x, ca.getY() + 2, ca.getRight() - x - 2, ca.getHeight() - 4));
    }

    for (int i = 0; i < PD_ENV_STEPS; i++) {
        auto p = handleFor(i);
        bool playing = i <= end;
        float r = (i == hover || (drag == Drag::level && i == dragStep)) ? 7.5f : 5.5f;
        g.setColour(playing ? accent : Theme::textFaint);
        g.fillEllipse(juce::Rectangle<float>(r * 2, r * 2).withCentre(p));
        g.setColour(Theme::panel);
        g.fillEllipse(juce::Rectangle<float>(r, r).withCentre(p));

        if (i == hover || (drag == Drag::level && i == dragStep)) {
            g.setColour(Theme::text);
            g.setFont(Theme::mono(11.0f));
            g.drawText(juce::String(value("level", i)),
                       juce::Rectangle<float>(p.x - 20, p.y - 26, 40, 14),
                       juce::Justification::centred, false);
        }
    }

    // ---- the sustain and end markers ------------------------------------
    auto ma = markerArea();
    g.setFont(Theme::label(10.5f));
    for (int i = 0; i < PD_ENV_STEPS; i++) {
        float x = ca.getX() + stepWidth() * i;
        auto cell = juce::Rectangle<float>(x, ma.getY(), stepWidth(), ma.getHeight());
        bool isSus = (i == sustain), isEnd = (i == end);
        if (isSus || isEnd) {
            g.setColour(isSus ? Theme::accent : Theme::lineTwo);
            g.fillRoundedRectangle(cell.reduced(6.0f, 7.0f), 4.0f);
            g.setColour(Theme::panel);
            g.drawText(isSus ? "SUS" : "END", cell, juce::Justification::centred, false);
        } else {
            g.setColour(Theme::textFaint);
            g.drawText(juce::String(i + 1), cell, juce::Justification::centred, false);
        }
    }

    // ---- the rates -------------------------------------------------------
    auto ra = rateArea();
    Theme::drawRecess(g, ra, 7.0f);
    g.setFont(Theme::label(10.0f));
    g.setColour(Theme::textDim);
    g.drawText("RATE  how fast each step is walked",
               ra.withHeight(14.0f).translated(8.0f, 3.0f),
               juce::Justification::centredLeft, false);
    g.setFont(Theme::mono(10.0f));
    for (int i = 0; i < PD_ENV_STEPS; i++) {
        float x = ca.getX() + stepWidth() * i;
        auto cell = juce::Rectangle<float>(x, ra.getY() + 16.0f, stepWidth(),
                                       ra.getHeight() - 16.0f).reduced(7.0f, 8.0f);
        float f = value("rate", i) / 99.0f;
        auto bar = cell.withTop(cell.getBottom() - cell.getHeight() * f);
        g.setColour((i <= end ? accent : Theme::textFaint).withAlpha(0.85f));
        g.fillRoundedRectangle(bar, 3.0f);
        if (i <= end) {
            g.setColour(Theme::textDim);
            g.drawText(juce::String(value("rate", i)),
                       cell.withHeight(12.0f).translated(0, cell.getHeight() - 12.0f),
                       juce::Justification::centred, false);
        }
    }

    // how long the whole thing takes, which the rates alone do not tell you
    double secs = 0, from = 0;
    for (int i = 0; i <= end; i++) {
        double to = value("level", i) / 99.0;
        secs += std::abs(to - from) * pd_env_rate_seconds((uint8_t)value("rate", i));
        from = to;
    }
    g.setColour(Theme::textDim);
    g.setFont(Theme::label(11.0f));
    g.drawText(juce::String(secs, 2) + " s to the end step",
               ra.withTop(ra.getBottom() + 2).withHeight(16.0f),
               juce::Justification::centredLeft, false);
}

void EnvelopeEditor::mouseDown(const juce::MouseEvent& e)
{
    auto p = e.position;
    auto ma = markerArea();

    if (ma.contains(p)) {
        int i = juce::jlimit(0, PD_ENV_STEPS - 1,
                             (int)((p.x - curveArea().getX()) / stepWidth()));
        /* the left half of the cell sets the sustain, the right half the end:
         * two markers, one row, without a modifier key to remember */
        bool leftHalf = (p.x - (curveArea().getX() + stepWidth() * i)) < stepWidth() * 0.5f;
        if (leftHalf) {
            setValue("sustain", -1, i, PD_ENV_STEPS - 1);
            if (value("end") < i) setValue("end", -1, i, PD_ENV_STEPS - 1);
        } else {
            setValue("end", -1, i, PD_ENV_STEPS - 1);
            if (value("sustain") > i) setValue("sustain", -1, i, PD_ENV_STEPS - 1);
        }
        repaint();
        return;
    }
    if (rateArea().contains(p)) {
        drag = Drag::rate;
        dragStep = juce::jlimit(0, PD_ENV_STEPS - 1,
                                (int)((p.x - curveArea().getX()) / stepWidth()));
        mouseDrag(e);
        return;
    }
    if (curveArea().contains(p)) {
        drag = Drag::level;
        dragStep = juce::jlimit(0, PD_ENV_STEPS - 1,
                                (int)((p.x - curveArea().getX()) / stepWidth()));
        mouseDrag(e);
    }
}

void EnvelopeEditor::mouseDrag(const juce::MouseEvent& e)
{
    auto p = e.position;
    if (drag == Drag::level) {
        auto a = curveArea().reduced(0.0f, 10.0f);
        float f = 1.0f - (p.y - a.getY()) / a.getHeight();
        setValue("level", dragStep, (int)std::lround(juce::jlimit(0.0f, 1.0f, f) * 99.0f), 99);
    } else if (drag == Drag::rate) {
        auto a = rateArea().reduced(0.0f, 8.0f);
        float f = 1.0f - (p.y - a.getY()) / a.getHeight();
        setValue("rate", dragStep, (int)std::lround(juce::jlimit(0.0f, 1.0f, f) * 99.0f), 99);
    }
    hover = dragStep;
    repaint();
}

void EnvelopeEditor::mouseUp(const juce::MouseEvent&) { drag = Drag::none; repaint(); }
} // namespace pd
