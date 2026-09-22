/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Can a host actually turn the CV bus on? "Enabling all buses failed" would
 * pluginval reports "Enabling all buses failed" and that could mean either of
 * two things: a layout this plugin deliberately declines, such as an LCR main
 * bus on a stereo synth, or a CV bus no host can switch on, which would make
 * the whole feature unusable. The difference matters, so it is checked rather
 * than shrugged at.
 */
#include <juce_audio_processors/juce_audio_processors.h>
#include "../plugin/PluginProcessor.h"

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    pd::Processor p;
    printf("output buses: %d\n", p.getBusCount(false));
    for (int i = 0; i < p.getBusCount(false); i++) {
        auto* b = p.getBus(false, i);
        printf("  bus %d %-8s enabled=%d  default=%s  current=%s\n", i,
               b->getName().toRawUTF8(), (int)b->isEnabled(),
               b->getDefaultLayout().getDescription().toRawUTF8(),
               b->getCurrentLayout().getDescription().toRawUTF8());
    }
    auto* cv = p.getBus(false, 1);
    if (!cv) { printf("no CV bus\n"); return 1; }

    const bool ok = cv->setCurrentLayout(juce::AudioChannelSet::discreteChannels(3));
    printf("enable CV as 3 discrete: %s\n", ok ? "accepted" : "REFUSED");
    printf("  now enabled=%d  layout=%s\n", (int)cv->isEnabled(),
           cv->getCurrentLayout().getDescription().toRawUTF8());

    /* and does it still render */
    p.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> buf(p.getTotalNumOutputChannels(), 256);
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    p.processBlock(buf, m);
    printf("channels after enabling: %d\n", buf.getNumChannels());
    if (buf.getNumChannels() >= 5) {
        printf("  pitch ch2 = %.4f   gate ch3 = %.4f\n",
               buf.getSample(2, 100), buf.getSample(3, 100));
    }
    return ok ? 0 : 1;
}
