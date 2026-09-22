/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Renders the editor to a PNG without opening a window, so what the plugin
 * looks like can be checked rather than assumed. A panel that compiles is not
 * a panel that reads.
 */
#include <juce_gui_basics/juce_gui_basics.h>
#include "../plugin/PluginProcessor.h"
#include "../plugin/PluginEditor.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::String out = argc > 1 ? argv[1] : "editor.png";
    const int w = argc > 3 ? juce::String(argv[2]).getIntValue() : 1040;
    const int h = argc > 3 ? juce::String(argv[3]).getIntValue() : 680;

    pd::Processor proc;
    proc.prepareToPlay(48000.0, 512);

    /* put something through it so the meters and the live views have real
     * numbers rather than zeroes */
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 52, (juce::uint8)100), 0);
    proc.processBlock(buf, midi);
    midi.clear();
    for (int i = 0; i < 12; i++) proc.processBlock(buf, midi);

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setSize(w, h);

    /* Let the editor's timers run and keep feeding it audio, so the meters and
     * the live views hold real numbers. Without this the render shows an empty
     * scope and an unbent phase, which is a picture of a synth that is not
     * running rather than one that is. */
    for (int i = 0; i < 40; i++) {
        proc.processBlock(buf, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(12);
    }

    juce::Image img(juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g(img);
        ed->paintEntireComponent(g, true);
    }

    juce::File f(juce::File::getCurrentWorkingDirectory().getChildFile(out));
    f.deleteFile();
    juce::FileOutputStream fs(f);
    juce::PNGImageFormat png;
    if (!png.writeImageToStream(img, fs)) { printf("could not write %s\n", out.toRawUTF8()); return 1; }
    printf("wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), w, h);
    return 0;
}
