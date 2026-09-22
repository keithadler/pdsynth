/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "Theme.h"

namespace pd
{
const juce::Colour Theme::bg        { 0xff15181e };
const juce::Colour Theme::panel     { 0xff0e1116 };
const juce::Colour Theme::panelEdge { 0xff272f3d };
const juce::Colour Theme::raised    { 0xff222834 };
const juce::Colour Theme::raisedOn  { 0xff2d4a6b };
const juce::Colour Theme::text      { 0xffe4eaf3 };
const juce::Colour Theme::textDim   { 0xff8e99ab };
const juce::Colour Theme::textFaint { 0xff58637a };
const juce::Colour Theme::lineOne   { 0xff5cc8ff };
const juce::Colour Theme::lineTwo   { 0xff5ef0b4 };
const juce::Colour Theme::accent    { 0xffffc46b };
const juce::Colour Theme::grid      { 0xff1e2531 };

juce::Font Theme::title(float h)
{
    return juce::Font(juce::FontOptions{}.withHeight(h)
        .withName(juce::Font::getDefaultSansSerifFontName())
        .withStyle("Bold"));
}
juce::Font Theme::label(float h)
{
    return juce::Font(juce::FontOptions{}.withHeight(h)
        .withName(juce::Font::getDefaultSansSerifFontName()));
}
juce::Font Theme::mono(float h)
{
    return juce::Font(juce::FontOptions{}.withHeight(h)
        .withName(juce::Font::getDefaultMonospacedFontName()));
}

void Theme::drawRecess(juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour(panel);
    g.fillRoundedRectangle(r, radius);
    // a hairline of shadow along the top, which is what reads as depth
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawRoundedRectangle(r.reduced(0.5f).translated(0.0f, 0.5f), radius, 1.0f);
    g.setColour(panelEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}

void Theme::drawRaised(juce::Graphics& g, juce::Rectangle<float> r, bool on, float radius)
{
    juce::ColourGradient grad(on ? raisedOn.brighter(0.16f) : raised.brighter(0.10f), r.getTopLeft(),
                              on ? raisedOn.darker(0.12f)   : raised.darker(0.16f),   r.getBottomLeft(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(r, radius);
    g.setColour(on ? lineOne.withAlpha(0.55f) : panelEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}

LookAndFeel::LookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, Theme::bg);
    setColour(juce::Label::textColourId, Theme::textDim);
    setColour(juce::Slider::textBoxTextColourId, Theme::text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId, Theme::raised);
    setColour(juce::ComboBox::textColourId, Theme::text);
    setColour(juce::ComboBox::outlineColourId, Theme::panelEdge);
    setColour(juce::PopupMenu::backgroundColourId, Theme::raised);
    setColour(juce::PopupMenu::textColourId, Theme::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::raisedOn);
}

juce::Font LookAndFeel::getLabelFont(juce::Label&) { return Theme::label(12.0f); }

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                   float pos, float start, float end, juce::Slider& s)
{
    auto r = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(3.0f);
    auto centre = r.getCentre();
    auto radius = juce::jmin(r.getWidth(), r.getHeight()) * 0.5f;
    auto angle = start + pos * (end - start);

    // the track
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius - 3.0f, radius - 3.0f,
                        0.0f, start, end, true);
    g.setColour(Theme::panel);
    g.strokePath(track, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    // the filled part
    juce::Path value;
    value.addCentredArc(centre.x, centre.y, radius - 3.0f, radius - 3.0f,
                        0.0f, start, angle, true);
    auto c = s.findColour(juce::Slider::rotarySliderFillColourId, true);
    if (c == juce::Colour()) c = Theme::lineOne;
    g.setColour(c);
    g.strokePath(value, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    // the body
    g.setColour(Theme::raised.brighter(0.05f));
    g.fillEllipse(juce::Rectangle<float>(radius * 1.24f, radius * 1.24f).withCentre(centre));
    g.setColour(Theme::panelEdge);
    g.drawEllipse(juce::Rectangle<float>(radius * 1.24f, radius * 1.24f).withCentre(centre), 1.0f);
    // the pointer
    juce::Point<float> tip(centre.x + std::sin(angle) * radius * 0.52f,
                           centre.y - std::cos(angle) * radius * 0.52f);
    juce::Point<float> root(centre.x + std::sin(angle) * radius * 0.18f,
                            centre.y - std::cos(angle) * radius * 0.18f);
    g.setColour(Theme::text);
    g.drawLine({ root, tip }, 2.0f);
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                                   float pos, float, float,
                                   juce::Slider::SliderStyle style, juce::Slider& s)
{
    auto r = juce::Rectangle<int>(x, y, w, h).toFloat();
    auto c = s.findColour(juce::Slider::trackColourId, true);
    if (c == juce::Colour()) c = Theme::lineOne;

    if (style == juce::Slider::LinearVertical) {
        // a wheel: a slot with a grip in it, which is what the hardware has
        auto slot = r.withSizeKeepingCentre(juce::jmin(r.getWidth(), 24.0f), r.getHeight());
        Theme::drawRecess(g, slot, 5.0f);
        auto grip = juce::Rectangle<float>(slot.getWidth() - 4.0f, 16.0f)
                        .withCentre({ slot.getCentreX(), pos });
        juce::ColourGradient grad(c.brighter(0.25f), grip.getTopLeft(),
                                  c.darker(0.35f), grip.getBottomLeft(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(grip, 3.0f);
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawRoundedRectangle(grip, 3.0f, 1.0f);
        // a centre notch, so a bend wheel's rest position is visible
        g.setColour(Theme::textFaint.withAlpha(0.5f));
        g.drawLine(slot.getX() + 3, slot.getCentreY(), slot.getRight() - 3, slot.getCentreY(), 1.0f);
        return;
    }

    auto track = r.withSizeKeepingCentre(r.getWidth(), 6.0f);
    Theme::drawRecess(g, track, 3.0f);
    g.setColour(c);
    g.fillRoundedRectangle(track.withWidth(juce::jmax(6.0f, pos - track.getX())), 3.0f);
    g.setColour(Theme::text);
    g.fillEllipse(juce::Rectangle<float>(11.0f, 11.0f).withCentre({ pos, track.getCentreY() }));
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b,
                                       const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    bool on = b.getToggleState() || down;
    Theme::drawRaised(g, r, on);
    if (over && !on) {
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.fillRoundedRectangle(r, 5.0f);
    }
}

void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont(Theme::label(12.0f));
    g.setColour(b.getToggleState() ? Theme::text : Theme::textDim);
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}
} // namespace pd
