/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "PluginEditor.h"

namespace pd
{
static constexpr int kW = 1080, kH = 780;

Editor::Editor(Processor& p)
    : juce::AudioProcessorEditor(&p), proc(p), envEditor(p.apvts)
{
    setLookAndFeel(&lnf);

    auto addBtn = [&](juce::TextButton& b, const char* label) {
        b.setButtonText(label);
        b.setClickingTogglesState(false);
        addAndMakeVisible(b);
    };
    const char* kLineNames[2] = { "LINE 1", "LINE 2" };
    const char* kEnvLabels[3] = { "PITCH", "WAVEFORM", "AMPLITUDE" };
    const char* kMixLabels[3] = { "BOTH", "RING", "NOISE" };

    for (int i = 0; i < 2; i++) {
        addBtn(lineBtn[i], kLineNames[i]);
        lineBtn[i].onClick = [this, i] { selectLine(i); };
    }
    addBtn(twoLines, "TWO LINES");
    twoLines.onClick = [this] {
        if (auto* pr = proc.apvts.getParameter("lines")) {
            bool two = pr->getValue() > 0.5f;
            pr->beginChangeGesture();
            pr->setValueNotifyingHost(two ? 0.0f : 1.0f);
            pr->endChangeGesture();
        }
    };
    for (int i = 0; i < 3; i++) {
        addBtn(envBtn[i], kEnvLabels[i]);
        envBtn[i].onClick = [this, i] { selectEnv(i); };
    }
    for (int i = 0; i < PD_WAVE_COUNT; i++) {
        addBtn(waveBtn[i], "");
        waveBtn[i].setButtonText(juce::String(pd_wave_name((pd_wave_t)i)).toUpperCase());
        waveBtn[i].onClick = [this, i] {
            if (auto* pr = proc.apvts.getParameter(Ids::line(currentLine, "wave"))) {
                pr->beginChangeGesture();
                pr->setValueNotifyingHost((float)i / (float)(PD_WAVE_COUNT - 1));
                pr->endChangeGesture();
            }
        };
    }
    for (int i = 0; i < 3; i++) {
        addBtn(mixBtn[i], kMixLabels[i]);
        mixBtn[i].onClick = [this, i] {
            if (auto* pr = proc.apvts.getParameter("mix")) {
                pr->beginChangeGesture();
                pr->setValueNotifyingHost((float)i / 2.0f);
                pr->endChangeGesture();
            }
        };
    }

    auto knob = [&](juce::Slider& s, juce::Label& l, const char* name, juce::Colour c,
                    int decimals = 2, const char* suffix = "") {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 15);
        juce::ignoreUnused(decimals, suffix);   // the parameter formats itself
        s.setColour(juce::Slider::rotarySliderFillColourId, c);
        addAndMakeVisible(s);
        l.setText(name, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(Theme::label(10.5f));
        addAndMakeVisible(l);
    };
    knob(detune,     detuneL,     "DETUNE",    Theme::lineOne);
    knob(level,      levelL,      "LEVEL",     Theme::lineOne);
    knob(pitchDepth, pitchDepthL, "PITCH ENV", Theme::lineTwo);
    knob(noise,      noiseL,      "NOISE",     Theme::accent);
    knob(velWave,    velWaveL,    "VEL>WAVE",  Theme::accent);
    knob(velLevel,   velLevelL,   "VEL>LEVEL", Theme::accent);

    addAndMakeVisible(envEditor);
    for (auto& w : waveView) addAndMakeVisible(w);
    addAndMakeVisible(scope);

    aNoise    = std::make_unique<SA>(proc.apvts, "noise", noise);
    aVelWave  = std::make_unique<SA>(proc.apvts, "vel_wave", velWave);
    aVelLevel = std::make_unique<SA>(proc.apvts, "vel_level", velLevel);
    syncAttachments();
    selectLine(0);
    selectEnv(1);
    refreshToggles();

    auto wheel = [&](juce::Slider& s, juce::Label& l, const char* name, juce::Colour c) {
        s.setSliderStyle(juce::Slider::LinearVertical);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setColour(juce::Slider::trackColourId, c);
        s.setRange(-1.0, 1.0, 0.0);
        addAndMakeVisible(s);
        l.setText(name, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(Theme::label(9.5f));
        addAndMakeVisible(l);
    };
    wheel(bendWheel, bendWheelL, "BEND", Theme::lineOne);
    wheel(modWheel,  modWheelL,  "MOD",  Theme::accent);
    modWheel.setRange(0.0, 1.0, 0.0);
    bendWheel.setValue(0.0, juce::dontSendNotification);
    bendWheel.onValueChange = [this] {
        const int v = juce::jlimit(0, 16383, (int)std::lround(bendWheel.getValue() * 8192.0) + 8192);
        proc.uiNotes.addMessageToQueue(juce::MidiMessage::pitchWheel(1, v)
            .withTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001));
    };
    /* a real bend wheel returns to centre when you let go of it */
    bendWheel.onDragEnd = [this] { bendWheel.setValue(0.0, juce::sendNotificationSync); };
    modWheel.onValueChange = [this] {
        proc.uiNotes.addMessageToQueue(
            juce::MidiMessage::controllerEvent(1, 1, (int)std::lround(modWheel.getValue() * 127.0))
            .withTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001));
    };

    keyboard.setAvailableRange(36, 84);
    keyboard.setKeyWidth(22.0f);
    keyboard.setScrollButtonsVisible(false);
    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffd8dee8));
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff1a1f28));
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, Theme::panelEdge);
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::lineOne.withAlpha(0.75f));
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::lineOne.withAlpha(0.28f));
    keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha(0.5f));
    keyboard.setColour(juce::MidiKeyboardComponent::textLabelColourId, Theme::textFaint);
    addAndMakeVisible(keyboard);
    kbState.addListener(this);

    setWantsKeyboardFocus(true);
    setSize(kW, kH);
    setResizable(true, true);
    setResizeLimits(960, 700, 1800, 1300);
    startTimerHz(30);
}

Editor::~Editor()
{
    kbState.removeListener(this);
    setLookAndFeel(nullptr);
}

void Editor::handleNoteOn(juce::MidiKeyboardState*, int ch, int note, float vel)
{
    proc.uiNotes.addMessageToQueue(juce::MidiMessage::noteOn(ch, note, vel)
        .withTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001));
}
void Editor::handleNoteOff(juce::MidiKeyboardState*, int ch, int note, float vel)
{
    proc.uiNotes.addMessageToQueue(juce::MidiMessage::noteOff(ch, note, vel)
        .withTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001));
}

void Editor::syncAttachments()
{
    aDetune     = std::make_unique<SA>(proc.apvts, Ids::line(currentLine, "detune"), detune);
    aLevel      = std::make_unique<SA>(proc.apvts, Ids::line(currentLine, "level"), level);
    aPitchDepth = std::make_unique<SA>(proc.apvts, Ids::line(currentLine, "pitch_depth"), pitchDepth);
}

void Editor::selectLine(int l)
{
    currentLine = l;
    syncAttachments();
    envEditor.show(currentLine, currentEnv);
    refreshToggles();
    auto c = l == 0 ? Theme::lineOne : Theme::lineTwo;
    detune.setColour(juce::Slider::rotarySliderFillColourId, c);
    level.setColour(juce::Slider::rotarySliderFillColourId, c);
    repaint();
}

void Editor::selectEnv(int e)
{
    currentEnv = e;
    envEditor.show(currentLine, currentEnv);
    refreshToggles();
    envEditor.setAccent(e == 0 ? Theme::lineTwo : (e == 2 ? Theme::accent : Theme::lineOne));
    repaint();
}

void Editor::refreshToggles()
{
    for (int i = 0; i < 2; i++) lineBtn[i].setToggleState(i == currentLine, juce::dontSendNotification);
    for (int i = 0; i < 3; i++) envBtn[i].setToggleState(i == currentEnv, juce::dontSendNotification);
    if (auto* w = proc.apvts.getRawParameterValue(Ids::line(currentLine, "wave")))
        for (int i = 0; i < PD_WAVE_COUNT; i++)
            waveBtn[i].setToggleState(i == (int)w->load(), juce::dontSendNotification);
    if (auto* m = proc.apvts.getRawParameterValue("mix"))
        for (int i = 0; i < 3; i++)
            mixBtn[i].setToggleState(i == (int)m->load(), juce::dontSendNotification);
    if (auto* l = proc.apvts.getRawParameterValue("lines"))
        twoLines.setToggleState(l->load() > 0.5f, juce::dontSendNotification);
}

void Editor::timerCallback()
{
    for (int i = 0; i < 2; i++) {
        auto* w = proc.apvts.getRawParameterValue(Ids::line(i, "wave"));
        waveView[i].setState(w ? (pd_wave_t)(int)w->load() : PD_SAW,
                             proc.bendNow[i].load(),
                             i == 0 ? Theme::lineOne : Theme::lineTwo);
    }
    std::vector<float> s((size_t)Processor::kScope);
    int pos = proc.scopePos.load();
    for (int i = 0; i < Processor::kScope; i++)
        s[(size_t)i] = proc.scope[(size_t)((pos + i) % Processor::kScope)].load();
    scope.setSamples(s.data(), (int)s.size());

    refreshToggles();
    repaint();
}

void Editor::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setGradientFill(juce::ColourGradient(Theme::bg.brighter(0.05f), 0.0f, 0.0f,
                                           Theme::bg.darker(0.25f), 0.0f, (float)r.getHeight(), false));
    g.fillAll();

    auto header = r.removeFromTop(58).toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.22f));
    g.fillRect(header);
    g.setColour(Theme::panelEdge);
    g.drawLine(0.0f, header.getBottom(), (float)r.getWidth(), header.getBottom(), 1.0f);

    g.setFont(Theme::title(23.0f));
    g.setColour(Theme::text);
    g.drawText("pdsynth", header.withTrimmedLeft(22.0f), juce::Justification::centredLeft, false);
    g.setFont(Theme::label(12.5f));
    g.setColour(Theme::lineOne);
    g.drawText("PHASE DISTORTION", header.withTrimmedLeft(140.0f),
               juce::Justification::centredLeft, false);

    g.setColour(Theme::textDim);
    g.setFont(Theme::mono(12.0f));
    const int nv = proc.voicesNow.load();
    g.drawText(juce::String(nv) + (nv == 1 ? " voice" : " voices"),
               header.withTrimmedRight(24.0f), juce::Justification::centredRight, false);

    // the level meter, tucked under the header
    auto meter = juce::Rectangle<float>((float)getWidth() - 200.0f, 40.0f, 176.0f, 5.0f);
    Theme::drawRecess(g, meter, 2.5f);
    g.setColour(Theme::accent);
    g.fillRoundedRectangle(meter.withWidth(meter.getWidth() *
        juce::jlimit(0.0f, 1.0f, proc.levelNow.load())), 2.5f);

    g.setFont(Theme::label(10.5f));
    g.setColour(Theme::textFaint);
    g.drawText("z-m and q-i play    left/right octave    space panic",
               getLocalBounds().withTrimmedRight(24).withTrimmedBottom(8),
               juce::Justification::bottomRight, false);
    g.drawText("no filter in the path",
               juce::Rectangle<int>(22, 58, 300, 16),
               juce::Justification::centredLeft, false);
}

void Editor::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop(58);
    r.reduce(20, 14);

    /* the keyboard sits along the bottom, the width of the window */
    auto bottom = r.removeFromBottom(96);
    auto wheels = bottom.removeFromLeft(86);
    auto bw = wheels.removeFromLeft(42);
    bendWheelL.setBounds(bw.removeFromBottom(13));
    bendWheel.setBounds(bw.reduced(6, 4));
    auto mw = wheels;
    modWheelL.setBounds(mw.removeFromBottom(13));
    modWheel.setBounds(mw.reduced(6, 4));
    bottom.removeFromLeft(10);
    {
        auto kb = bottom.reduced(0, 6);
        const int lowest = 36, highest = 84;
        int whites = 0;
        for (int n = lowest; n <= highest; n++)
            if (!juce::MidiMessage::isMidiNoteBlack(n)) whites++;
        keyboard.setKeyWidth(juce::jmax(12.0f, (float)kb.getWidth() / (float)whites));
        keyboard.setBounds(kb);
    }
    r.removeFromBottom(10);

    r.removeFromTop(18);           // room for the "no filter" line
    auto left = r.removeFromLeft(268);
    r.removeFromLeft(18);
    auto right = r.removeFromRight(292);
    r.removeFromRight(18);

    // left: which line, its waveform, its knobs, the mix
    auto row = left.removeFromTop(28);
    lineBtn[0].setBounds(row.removeFromLeft(78));
    row.removeFromLeft(6);
    lineBtn[1].setBounds(row.removeFromLeft(78));
    row.removeFromLeft(10);
    twoLines.setBounds(row);
    left.removeFromTop(16);

    for (int i = 0; i < PD_WAVE_COUNT; i++) {
        int col = i % 2, rowI = i / 2;
        waveBtn[i].setBounds(left.getX() + col * 136, left.getY() + rowI * 34, 130, 28);
    }
    left.removeFromTop(4 * 34 + 14);

    auto knobs = left.removeFromTop(150);
    auto place = [&](juce::Slider& s, juce::Label& l, juce::Rectangle<int> cell) {
        l.setBounds(cell.removeFromTop(14));
        s.setBounds(cell);
    };
    auto rowA = knobs.removeFromTop(75);
    place(detune,     detuneL,     rowA.removeFromLeft(89));
    place(level,      levelL,      rowA.removeFromLeft(89));
    place(pitchDepth, pitchDepthL, rowA);
    auto rowB = knobs;
    place(noise,    noiseL,    rowB.removeFromLeft(89));
    place(velWave,  velWaveL,  rowB.removeFromLeft(89));
    place(velLevel, velLevelL, rowB);

    left.removeFromTop(juce::jmax(14, left.getHeight() - 42));
    auto mixRow = left.removeFromTop(28);
    for (int i = 0; i < 3; i++) {
        mixBtn[i].setBounds(mixRow.removeFromLeft(84));
        mixRow.removeFromLeft(6);
    }

    // middle: the envelope
    auto envRow = r.removeFromTop(28);
    for (int i = 0; i < 3; i++) {
        envBtn[i].setBounds(envRow.removeFromLeft(126));
        envRow.removeFromLeft(8);
    }
    r.removeFromTop(14);
    envEditor.setBounds(r);

    // right: what it is doing
    scope.setBounds(right.removeFromTop(juce::jmax(110, right.getHeight() / 4)));
    right.removeFromTop(14);
    const int each = (right.getHeight() - 12) / 2;
    waveView[0].setBounds(right.removeFromTop(each));
    right.removeFromTop(12);
    waveView[1].setBounds(right.removeFromTop(each));
}
} // namespace pd

namespace pd
{
/* The usual two rows: z..m is the lower octave, q..i the upper, with the
 * black keys on the row above each. */
int Editor::noteForKey(int k)
{
    switch (k) {
    case 'Z': return 48; case 'S': return 49; case 'X': return 50;
    case 'D': return 51; case 'C': return 52; case 'V': return 53;
    case 'G': return 54; case 'B': return 55; case 'H': return 56;
    case 'N': return 57; case 'J': return 58; case 'M': return 59;
    case 'Q': return 60; case '2': return 61; case 'W': return 62;
    case '3': return 63; case 'E': return 64; case 'R': return 65;
    case '5': return 66; case 'T': return 67; case '6': return 68;
    case 'Y': return 69; case '7': return 70; case 'U': return 71;
    case 'I': return 72;
    default:  return -1;
    }
}

bool Editor::keyPressed(const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::leftKey)  { octaveShift = juce::jmax(-3, octaveShift - 1); return true; }
    if (k.getKeyCode() == juce::KeyPress::rightKey) { octaveShift = juce::jmin( 3, octaveShift + 1); return true; }
    if (k.getKeyCode() == juce::KeyPress::spaceKey) {
        proc.uiNotes.addMessageToQueue(
            juce::MidiMessage::allNotesOff(1).withTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001));
        heldKeys.clear();
        kbState.allNotesOff(1);
        return true;
    }
    return noteForKey(k.getKeyCode()) >= 0;   // handled in keyStateChanged
}

/* Note off needs a key release, and JUCE only reports that through the state
 * change, so the held set is tracked here rather than in keyPressed. */
bool Editor::keyStateChanged(bool)
{
    static const char* kKeys = "ZSXDCVGBHNJMQ2W3ER5T6Y7UI";
    std::set<int> down;
    for (const char* p = kKeys; *p; p++)
        if (juce::KeyPress::isKeyCurrentlyDown(*p)) down.insert(*p);

    for (int k : down)
        if (!heldKeys.count(k)) {
            int n = noteForKey(k) + octaveShift * 12;
            if (n >= 0 && n < 128) kbState.noteOn(1, n, 0.8f);
        }
    for (int k : heldKeys)
        if (!down.count(k)) {
            int n = noteForKey(k) + octaveShift * 12;
            if (n >= 0 && n < 128) kbState.noteOff(1, n, 0.0f);
        }
    heldKeys = down;
    return true;
}
} // namespace pd
