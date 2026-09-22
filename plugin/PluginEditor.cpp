/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "PluginEditor.h"

namespace pd
{
static constexpr int kW = 1080, kH = 900;

Editor::Editor(Processor& p)
    : juce::AudioProcessorEditor(&p), proc(p), envEditor(p.apvts)
{
    setLookAndFeel(&lnf);

    leftView.setViewedComponent(&leftHolder, false);
    leftView.setScrollBarsShown(true, false, true, false);
    leftView.setScrollBarThickness(8);
    addAndMakeVisible(leftView);

    auto addBtn = [&](juce::TextButton& b, const char* label) {
        b.setButtonText(label);
        b.setClickingTogglesState(false);
        leftHolder.addAndMakeVisible(b);
    };
    const char* kLineNames[PD_MAX_LINES] = { "LINE 1", "LINE 2", "LINE 3", "LINE 4" };
    const char* kEnvLabels[3] = { "PITCH", "WAVEFORM", "AMPLITUDE" };
    const char* kMixLabels[3] = { "BOTH", "RING", "NOISE" };

    for (int i = 0; i < PD_MAX_LINES; i++) {
        addBtn(lineBtn[i], kLineNames[i]);
        lineBtn[i].onClick = [this, i] { selectLine(i); };
    }
    /* cycles 1 to 4: the hardware stacked two and there is no reason to */
    addBtn(lineCountBtn, "LINES 2");
    lineCountBtn.onClick = [this] {
        if (auto* pr = proc.apvts.getParameter("lines")) {
            const int now = (int)std::lround(pr->getValue() * 3.0f);
            const int next = (now + 1) % PD_MAX_LINES;
            pr->beginChangeGesture();
            pr->setValueNotifyingHost((float)next / 3.0f);
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
                    bool left = true, int decimals = 2, const char* suffix = "") {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 15);
        juce::ignoreUnused(decimals, suffix);   // the parameter formats itself
        s.setColour(juce::Slider::rotarySliderFillColourId, c);
        (left ? leftHolder : *this).addAndMakeVisible(s);
        l.setText(name, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(Theme::label(10.5f));
        (left ? leftHolder : *this).addAndMakeVisible(l);
    };
    knob(detune,     detuneL,     "DETUNE",    Theme::lineOne);
    knob(level,      levelL,      "LEVEL",     Theme::lineOne);
    knob(pitchDepth, pitchDepthL, "PITCH ENV", Theme::lineTwo);
    knob(noise,      noiseL,      "NOISE",     Theme::accent);
    knob(velWave,    velWaveL,    "VEL>WAVE",  Theme::accent);
    knob(velLevel,   velLevelL,   "VEL>LEVEL", Theme::accent);
    knob(glide,      glideL,      "GLIDE",     Theme::lineTwo);
    knob(atWave,     atWaveL,     "PRESSURE",  Theme::lineTwo);
    knob(cutoff,     cutoffL,     "CUTOFF",    Theme::accent);
    knob(resonance,  resonanceL,  "RESONANCE", Theme::accent);
    knob(filtEnv,    filtEnvL,    "DCW>CUTOFF",Theme::accent);
    knob(choMix,     choMixL,     "CHORUS",    Theme::lineTwo, false);
    knob(choDepth,   choDepthL,   "DEPTH",     Theme::lineTwo, false);
    knob(choRate,    choRateL,    "RATE",      Theme::lineTwo, false);
    knob(dlyMix,     dlyMixL,     "DELAY",     Theme::lineOne, false);
    knob(dlyTime,    dlyTimeL,    "TIME",      Theme::lineOne, false);
    knob(dlyFb,      dlyFbL,      "FEEDBACK",  Theme::lineOne, false);
    knob(drvAmount,  drvAmountL,  "DRIVE",     Theme::accent,  false);

    for (int i = 0; i < PD_DRIVE_MODES; i++)
        driveBox.addItem(juce::String(pd_drive_mode_name((pd_drive_mode_t)i)).toUpperCase(), i + 1);
    addAndMakeVisible(driveBox);
    fxL.setText("DRIVE", juce::dontSendNotification);
    fxL.setFont(Theme::label(10.5f));
    addAndMakeVisible(fxL);

    for (int i = 0; i < PD_FILTER_MODES; i++)
        filterBox.addItem(juce::String(pd_filter_mode_name((pd_filter_mode_t)i)).toUpperCase(), i + 1);
    leftHolder.addAndMakeVisible(filterBox);
    filterL.setText("FILTER", juce::dontSendNotification);
    filterL.setFont(Theme::label(10.5f));
    filterL.setJustificationType(juce::Justification::centredLeft);
    leftHolder.addAndMakeVisible(filterL);

    addAndMakeVisible(envEditor);
    for (auto& w : waveView) addAndMakeVisible(w);
    addAndMakeVisible(scope);

    aNoise    = std::make_unique<SA>(proc.apvts, "noise", noise);
    aVelWave  = std::make_unique<SA>(proc.apvts, "vel_wave", velWave);
    aVelLevel = std::make_unique<SA>(proc.apvts, "vel_level", velLevel);
    aGlide    = std::make_unique<SA>(proc.apvts, "glide", glide);
    aAtWave   = std::make_unique<SA>(proc.apvts, "at_wave", atWave);
    aCutoff   = std::make_unique<SA>(proc.apvts, "filt_cutoff", cutoff);
    aRes      = std::make_unique<SA>(proc.apvts, "filt_res", resonance);
    aFiltEnv  = std::make_unique<SA>(proc.apvts, "filt_env", filtEnv);
    aFilter   = std::make_unique<CA>(proc.apvts, "filt_mode", filterBox);
    aDrive    = std::make_unique<CA>(proc.apvts, "drv_mode", driveBox);
    aChoMix   = std::make_unique<SA>(proc.apvts, "cho_mix", choMix);
    aChoDepth = std::make_unique<SA>(proc.apvts, "cho_depth", choDepth);
    aChoRate  = std::make_unique<SA>(proc.apvts, "cho_rate", choRate);
    aDlyMix   = std::make_unique<SA>(proc.apvts, "dly_mix", dlyMix);
    aDlyTime  = std::make_unique<SA>(proc.apvts, "dly_time", dlyTime);
    aDlyFb    = std::make_unique<SA>(proc.apvts, "dly_fb", dlyFb);
    aDrvAmount= std::make_unique<SA>(proc.apvts, "drv_amount", drvAmount);
    syncAttachments();
    selectLine(0);
    selectEnv(1);
    refreshToggles();

    /* The bank, grouped by family, because twenty eight names in one list is a
     * list and twenty eight names in eight groups is a bank. */
    {
        juce::String family;
        int id = 1;
        for (int i = 0; i < pd_preset_count(); i++) {
            const pd_preset_t* pr = pd_preset(i);
            if (family != pr->family) {
                family = pr->family;
                presetBox.addSectionHeading(family);
            }
            presetBox.addItem(pr->name, id++);
        }
        presetBox.setSelectedId(1, juce::dontSendNotification);
        presetBox.onChange = [this] {
            const int idx = presetBox.getSelectedId() - 1;
            if (idx >= 0) { proc.loadPreset(idx); refreshToggles(); repaint(); }
        };
        addAndMakeVisible(presetBox);
        auto step = [this](int d) {
            int n = juce::jlimit(1, pd_preset_count(), presetBox.getSelectedId() + d);
            presetBox.setSelectedId(n, juce::sendNotificationSync);
        };
        prevPreset.setButtonText("<");
        nextPreset.setButtonText(">");
        prevPreset.onClick = [step] { step(-1); };
        nextPreset.onClick = [step] { step(+1); };
        addAndMakeVisible(prevPreset);
        addAndMakeVisible(nextPreset);

        loadSyxBtn.setButtonText("Load .syx");
        saveSyxBtn.setButtonText("Save .syx");
        loadSyxBtn.setTooltip("Read a Casio CZ voice dump");
        saveSyxBtn.setTooltip("Write this patch as a Casio CZ voice dump");
        loadSyxBtn.onClick = [this] { chooseSysexToLoad(); };
        saveSyxBtn.onClick = [this] { chooseSysexToSave(); };
        addAndMakeVisible(loadSyxBtn);
        addAndMakeVisible(saveSyxBtn);
    }

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
    setResizable(true, true);
    /*
     * The minimum has to fit the smallest screen anyone still uses. A 1366 by
     * 768 laptop has about 708 pixels of usable height once the menu bar and
     * the taskbar are gone, so a window that insists on 820 is taller than the
     * screen, and a window taller than the screen has its title bar pushed off
     * the top with no way to reach it. That is exactly what was reported, and
     * it was introduced by growing the window to fit the effects.
     */
    /* 640 rather than 560: a 1366 by 768 laptop has about 708 usable, so this
     * fits with room, and below it the knobs collapse to something nobody can
     * grab and the filter row falls off the bottom. A window that fits but
     * cannot be used is not a fix. */
    setResizeLimits(880, 640, 2200, 1600);

    /* Open at the intended size, or at whatever the display can actually show,
     * whichever is smaller. */
    int w = kW, h = kH;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
        /* userBounds is in physical units, so round to whole pixels before
         * comparing with a size in pixels. */
        const auto area = d->userBounds.toNearestInt();
        w = juce::jmin(w, area.getWidth()  - 40);
        h = juce::jmin(h, area.getHeight() - 60);
    }
    setSize(juce::jmax(880, w), juce::jmax(560, h));
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
    int nLines = 2;
    if (auto* lp = proc.apvts.getRawParameterValue("lines")) nLines = (int)lp->load() + 1;
    for (int i = 0; i < PD_MAX_LINES; i++) {
        lineBtn[i].setToggleState(i == currentLine, juce::dontSendNotification);
        lineBtn[i].setEnabled(i < nLines);
    }
    lineCountBtn.setButtonText("LINES " + juce::String(nLines));
    lineCountBtn.setToggleState(nLines > 1, juce::dontSendNotification);
    for (int i = 0; i < 3; i++) envBtn[i].setToggleState(i == currentEnv, juce::dontSendNotification);
    if (auto* w = proc.apvts.getRawParameterValue(Ids::line(currentLine, "wave")))
        for (int i = 0; i < PD_WAVE_COUNT; i++)
            waveBtn[i].setToggleState(i == (int)w->load(), juce::dontSendNotification);
    if (auto* m = proc.apvts.getRawParameterValue("mix"))
        for (int i = 0; i < 3; i++)
            mixBtn[i].setToggleState(i == (int)m->load(), juce::dontSendNotification);

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
    /* The claim has to stop being made the moment it stops being true. */
    int fm = 0;
    if (auto* f = proc.apvts.getRawParameterValue("filt_mode")) fm = (int)f->load();
    g.drawText(fm == 0 ? "no filter in the path"
                       : juce::String(pd_filter_mode_name((pd_filter_mode_t)fm)) + " engaged",
               juce::Rectangle<int>(22, 58, 300, 16),
               juce::Justification::centredLeft, false);
}

void Editor::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop(58);
    r.reduce(20, 14);

    /* The keyboard sits along the bottom and gives up height first when the
     * window is short, because two octaves of smaller keys is a better trade
     * than an envelope editor nobody can see. */
    const int kbH = juce::jlimit(54, 96, r.getHeight() / 6);
    auto bottom = r.removeFromBottom(kbH);
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

    /* the bank sits in the header, where a player looks first */
    {
        auto hdr = getLocalBounds().withHeight(58).withTrimmedLeft(300).withTrimmedRight(230);
        auto bar = hdr.withSizeKeepingCentre(hdr.getWidth(), 26);
        prevPreset.setBounds(bar.removeFromLeft(28));
        bar.removeFromLeft(4);
        nextPreset.setBounds(bar.removeFromRight(28));
        bar.removeFromRight(6);
        saveSyxBtn.setBounds(bar.removeFromRight(72));
        bar.removeFromRight(4);
        loadSyxBtn.setBounds(bar.removeFromRight(72));
        bar.removeFromRight(6);
        presetBox.setBounds(bar);
    }

    r.removeFromTop(18);           // room for the "no filter" line

    /*
     * The column is laid out at the height it needs, not the height available,
     * and the viewport shows as much of it as fits. Everything stays reachable
     * at any window size.
     */
    const int kLeftW = 268;
    auto leftArea = r.removeFromLeft(kLeftW);
    r.removeFromLeft(18);
    const int naturalH = 26 + 5 + 26 + 12      /* the two line rows */
                       + 4 * 34 + 14           /* the waveform grid */
                       + 2 * 61 + 8 + 61       /* three knob rows */
                       + 10 + 24 + 6 + 61      /* filter row and its knobs */
                       + 12 + 28;              /* the mix row */
    leftView.setBounds(leftArea);
    const bool scrolls = naturalH > leftArea.getHeight();
    leftHolder.setBounds(0, 0, kLeftW - (scrolls ? 10 : 0), juce::jmax(naturalH, leftArea.getHeight()));
    auto left = leftHolder.getLocalBounds();
    auto right = r.removeFromRight(292);
    r.removeFromRight(18);

    // left: which line, its waveform, its knobs, the mix
    auto row = left.removeFromTop(26);
    for (int i = 0; i < 2; i++) { lineBtn[i].setBounds(row.removeFromLeft(62)); row.removeFromLeft(5); }
    row.removeFromLeft(4);
    lineCountBtn.setBounds(row);
    left.removeFromTop(5);
    auto row2 = left.removeFromTop(26);
    for (int i = 2; i < 4; i++) { lineBtn[i].setBounds(row2.removeFromLeft(62)); row2.removeFromLeft(5); }
    left.removeFromTop(12);

    for (int i = 0; i < PD_WAVE_COUNT; i++) {
        int col = i % 2, rowI = i / 2;
        waveBtn[i].setBounds(left.getX() + col * 136, left.getY() + rowI * 34, 130, 28);
    }
    left.removeFromTop(4 * 34 + 14);

    auto knobs = left.removeFromTop(122);
    auto place = [&](juce::Slider& s, juce::Label& l, juce::Rectangle<int> cell) {
        l.setBounds(cell.removeFromTop(14));
        s.setBounds(cell);
    };
    const int knobRow = 61;
    auto rowA = knobs.removeFromTop(knobRow);
    place(detune,     detuneL,     rowA.removeFromLeft(89));
    place(level,      levelL,      rowA.removeFromLeft(89));
    place(pitchDepth, pitchDepthL, rowA);
    auto rowB = knobs;
    place(noise,    noiseL,    rowB.removeFromLeft(89));
    place(velWave,  velWaveL,  rowB.removeFromLeft(89));
    place(velLevel, velLevelL, rowB);

    left.removeFromTop(8);
    auto rowC = left.removeFromTop(knobRow);
    place(glide,     glideL,     rowC.removeFromLeft(89));
    place(atWave,    atWaveL,    rowC.removeFromLeft(89));
    place(filtEnv,   filtEnvL,   rowC);

    left.removeFromTop(10);
    auto fRow = left.removeFromTop(24);
    filterL.setBounds(fRow.removeFromLeft(48));
    filterBox.setBounds(fRow);
    left.removeFromTop(6);
    auto rowD = left.removeFromTop(knobRow);
    place(cutoff,    cutoffL,    rowD.removeFromLeft(89));
    place(resonance, resonanceL, rowD.removeFromLeft(89));

    left.removeFromTop(12);
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
    /* the effects live under the meters, across the right column */
    {
        /* the effects give up their second row before the meters do */
        const int fxH = juce::jlimit(78, 168, right.getHeight() / 3);
        auto fxArea = right.removeFromBottom(fxH);
        auto place2 = [&](juce::Slider& s2, juce::Label& l2, juce::Rectangle<int> cell) {
            l2.setBounds(cell.removeFromTop(13));
            s2.setBounds(cell);
        };
        const int fxRow = juce::jmax(36, (fxArea.getHeight() - 4) / 2);
        auto r1 = fxArea.removeFromTop(fxRow);
        place2(choMix,  choMixL,  r1.removeFromLeft(72));
        place2(choDepth,choDepthL,r1.removeFromLeft(72));
        place2(choRate, choRateL, r1.removeFromLeft(72));
        place2(drvAmount, drvAmountL, r1);
        fxArea.removeFromTop(4);
        auto r2 = fxArea.removeFromTop(fxRow);
        place2(dlyMix,  dlyMixL,  r2.removeFromLeft(72));
        place2(dlyTime, dlyTimeL, r2.removeFromLeft(72));
        place2(dlyFb,   dlyFbL,   r2.removeFromLeft(72));
        auto dr = r2;
        fxL.setBounds(dr.removeFromTop(13));
        driveBox.setBounds(dr.removeFromTop(24));
        right.removeFromBottom(10);
    }

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
/* ---------------------------------------------------------------------------
 * Casio voice dumps, in and out.
 *
 * Both say afterwards what did not survive the trip, because a translation
 * that quietly loses a control is worse than one that refuses: the player
 * finds out on the hardware, in front of somebody.
 * ------------------------------------------------------------------------ */
void Editor::showReport(const juce::String& title, const juce::String& body)
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::NoIcon,
                                           title, body, "OK", this);
}

void Editor::chooseSysexToLoad()
{
    chooser = std::make_unique<juce::FileChooser>(
        "Open a Casio CZ voice dump", juce::File(), "*.syx;*.SYX");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                       | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            juce::String report;
            const bool ok = proc.loadSysex(f, report);
            if (ok) {
                presetBox.setSelectedId(0, juce::dontSendNotification);
                syncAttachments();
                refreshToggles();
                repaint();
            }
            showReport(ok ? "Voice loaded" : "Not a CZ voice dump", report);
        });
}

void Editor::chooseSysexToSave()
{
    chooser = std::make_unique<juce::FileChooser>(
        "Save as a Casio CZ voice dump",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("pdsynth.syx"), "*.syx");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode
                       | juce::FileBrowserComponent::canSelectFiles
                       | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            juce::String report;
            const bool ok = proc.saveSysex(f, report);
            showReport(ok ? "Voice written" : "Could not write the dump", report);
        });
}

} // namespace pd
