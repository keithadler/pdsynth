/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Loads a Casio voice dump through the plugin and writes it back out.
 *
 * The C translator is checked on its own, but the plugin does not hand it a
 * patch directly: the voice goes out to the parameters and comes back from
 * them, and parameters are floats with ranges and step sizes. A rate of 73
 * that returns as 72 would be invisible in the C tests and would still quietly
 * alter every patch a player saved. So this does the trip the player does.
 */
#include <juce_audio_processors/juce_audio_processors.h>
#include "../plugin/PluginProcessor.h"
#include "pd_sysex.h"
#include <cstdio>

/* JUCE's logger writes to the debugger on Windows and to the console
 * elsewhere, which means a failure on Windows would report nothing at all
 * about which byte moved. This goes to stdout on every platform. */
static void say(const juce::String& s)
{
    std::fputs(s.toRawUTF8(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    int files = 0, identical = 0, failed = 0;
    long bytes = 0, same = 0;

    /* With no file given, build dumps out of the factory bank instead, so this
     * still checks something on a machine with no CZ patches on it. */
    juce::Array<juce::File> inputs;
    juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("pd_syxplugcheck");
    tmp.createDirectory();

    if (argc > 1) {
        for (int i = 1; i < argc; i++) inputs.add(juce::File(juce::String(argv[i])));
    } else {
        pd::Processor gen;
        for (int i = 0; i < pd_preset_count(); i++) {
            uint8_t out[PD_SYSEX_BYTES];
            pd_sysex_report_t rep;
            const size_t n = pd_sysex_write(&pd_preset(i)->patch, 0, 0x60,
                                            out, sizeof out, &rep);
            if (n == 0) continue;
            auto f = tmp.getChildFile("preset" + juce::String(i) + ".syx");
            f.replaceWithData(out, n);
            inputs.add(f);
        }
        say("no files given, using "
            + juce::String(inputs.size()) + " dumps made from the factory bank");
    }

    for (const auto& f : inputs) {
        juce::MemoryBlock before;
        if (!f.loadFileAsData(before)) { failed++; continue; }

        pd::Processor proc;
        proc.prepareToPlay(48000.0, 512);

        juce::String report;
        if (!proc.loadSysex(f, report)) {
            say("  cannot load " + f.getFileName() + ": " + report);
            failed++;
            continue;
        }
        auto outF = tmp.getChildFile("out_" + f.getFileName());
        if (!proc.saveSysex(outF, report)) { failed++; continue; }

        juce::MemoryBlock after;
        outF.loadFileAsData(after);

        files++;
        uint8_t a[PD_SYSEX_VOICE], b[PD_SYSEX_VOICE];
        if (pd_sysex_unpack((const uint8_t*)before.getData(), before.getSize(), a) != 0
         || pd_sysex_unpack((const uint8_t*)after.getData(), after.getSize(), b) != 0) {
            failed++;
            continue;
        }
        int diff = 0;
        for (int i = 0; i < PD_SYSEX_VOICE; i++) {
            bytes++;
            if (a[i] == b[i]) same++;
            else {
                diff++;
                if (diff <= 4)
                    say("  " + f.getFileName()
                        + " byte " + juce::String(i) + ": "
                        + juce::String::toHexString(a[i]) + " became "
                        + juce::String::toHexString(b[i]));
            }
        }
        if (diff == 0) identical++;
    }

    say("\nthrough the plugin's own parameters:");
    say("  " + juce::String(files) + " voices, "
        + juce::String(identical) + " identical, " + juce::String(failed) + " failed");
    say("  " + juce::String(same) + " of " + juce::String(bytes)
        + " bytes unchanged");

    tmp.deleteRecursively();
    return (failed == 0 && files > 0 && identical == files) ? 0 : 1;
}
