/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Do the octave buttons move the keyboard drawn on screen?
 *
 * Asked for by Reaper10: a button to shift the octave, shown on the on-screen
 * keyboard. The computer keys could always be shifted with the arrow keys, but
 * nothing on screen said so or said where you were, and the keyboard drawn
 * below never moved, so a note a few octaves up was playable and not visible.
 *
 * Nothing about this is covered by an engine test or by the parameters, and a
 * button that is drawn is not a button that works, so this builds the real
 * editor with no window, finds the buttons and the keyboard in it, clicks, and
 * reads what the keyboard says it is showing.
 */
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../plugin/PluginProcessor.h"
#include "../plugin/PluginEditor.h"
#include <cstdio>
#include <functional>

static int failures = 0;

static void say(const juce::String& s) { std::fputs(s.toRawUTF8(), stdout); std::fputc('\n', stdout); std::fflush(stdout); }

static void ok(bool cond, const juce::String& what)
{
    if (cond) { say("  ok    " + what); return; }
    failures++;
    say("  FAIL  " + what);
}

static void walk(juce::Component* c, const std::function<void(juce::Component*)>& f)
{
    f(c);
    for (auto* k : c->getChildren()) walk(k, f);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    pd::Processor proc;
    proc.prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setSize(1040, 680);

    juce::MidiKeyboardComponent* kb = nullptr;
    juce::TextButton *minus = nullptr, *plus = nullptr;
    juce::Label *octWord = nullptr;
    int keyboards = 0;
    walk(ed.get(), [&](juce::Component* c) {
        if (auto* k = dynamic_cast<juce::MidiKeyboardComponent*>(c)) { kb = k; keyboards++; }
        if (auto* b = dynamic_cast<juce::TextButton*>(c)) {
            if (b->getButtonText() == "-") minus = b;
            if (b->getButtonText() == "+") plus = b;
        }
        if (auto* l = dynamic_cast<juce::Label*>(c))
            if (l->getText() == "OCTAVE") octWord = l;
    });

    say("octave buttons");
    ok(kb && keyboards == 1, "the editor has one keyboard drawn on screen");
    ok(minus && plus, "and a minus and a plus button");
    ok(octWord != nullptr, "and a label saying OCTAVE");
    if (!kb || !minus || !plus || !octWord) { say("cannot go on without them"); return 1; }

    /* the number shown is the label directly under the word OCTAVE */
    auto shown = [&]() -> juce::String {
        juce::String t = "(none)";
        walk(ed.get(), [&](juce::Component* c) {
            if (auto* l = dynamic_cast<juce::Label*>(c))
                if (l != octWord && l->getX() == octWord->getX() && l->getY() == octWord->getBottom()) t = l->getText();
        });
        return t;
    };
    auto settle = [&] { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); };

    ok(kb->getRangeStart() == 36 && kb->getRangeEnd() == 84, "it starts on the same four octaves it always showed, 36 to 84");
    ok(shown() == "0", "and says 0");

    plus->triggerClick(); settle();
    ok(kb->getRangeStart() == 48 && kb->getRangeEnd() == 96, "one click of + moves the keyboard up an octave (48 to 96)");
    ok(shown() == "+1", "and says +1");
    ok(kb->getRangeEnd() - kb->getRangeStart() == 48, "still four octaves, so the keys do not change size");

    for (int i = 0; i < 10; i++) plus->triggerClick();
    settle();
    ok(kb->getRangeStart() == 72 && kb->getRangeEnd() == 120, "it stops three octaves up (72 to 120), as the arrow keys always did");
    ok(shown() == "+3", "and says +3");
    ok(!plus->isEnabled() && minus->isEnabled(), "and the + button is greyed out there");

    for (int i = 0; i < 10; i++) minus->triggerClick();
    settle();
    ok(kb->getRangeStart() == 0 && kb->getRangeEnd() == 48, "three octaves down it stops at 0 to 48");
    ok(shown() == "-3", "and says -3");
    ok(!minus->isEnabled() && plus->isEnabled(), "and the - button is greyed out there");

    /* the arrow keys are the same control and have to move the picture too */
    ed->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)); settle();
    ok(kb->getRangeStart() == 12 && shown() == "-2", "the right arrow key moves the keyboard and the number as well");
    ed->keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
    ed->keyPressed(juce::KeyPress(juce::KeyPress::leftKey)); settle();
    ok(kb->getRangeStart() == 0 && shown() == "-3", "the left arrow key too, and it stops at the end");

    /* the point of the whole thing: notes the new presets need can be reached on the screen keyboard */
    for (int i = 0; i < 5; i++) ed->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    settle();                                                   /* now +2 */
    ok(kb->getRangeStart() <= 86 && 86 <= kb->getRangeEnd(), "the tin whistle's highest note, D6 (86), can be reached");
    for (int i = 0; i < 3; i++) minus->triggerClick();
    settle();                                                   /* now -1 */
    ok(kb->getRangeStart() <= 33 && 33 <= kb->getRangeEnd(), "and the didgeridoo's lowest drone, A1 (33), can be reached");

    /* clicking must not take the keyboard away from the computer keys, which
     * are what the buttons are for */
    ok(!plus->getWantsKeyboardFocus() && !minus->getWantsKeyboardFocus(),
       "the buttons do not take keyboard focus, so the computer keys keep playing after a click");

    say(failures ? juce::String(failures) + " failure(s)" : "all good");
    return failures ? 1 : 0;
}
