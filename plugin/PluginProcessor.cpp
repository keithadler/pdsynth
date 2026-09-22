/*
 * pdsynth - Casio CZ phase distortion
 *
 * Copyright (C) 2026 Keith Adler
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace pd
{
static constexpr int kPolyphony = 16;
static const char* kEnvNames[3] = { "Pitch", "Wave", "Amp" };

juce::AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    StringArray waves;
    for (int i = 0; i < PD_WAVE_COUNT; i++)
        waves.add(juce::String(pd_wave_name((pd_wave_t)i)).replaceCharacter('_', ' '));

    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"lines", 1}, "Lines",
              StringArray{ "One", "Two" }, 1));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"mix", 1}, "Mix",
              StringArray{ "Both", "Ring", "Noise" }, 0));
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; });
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"noise", 1}, "Noise",
              NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"vel_wave", 1}, "Vel to Wave",
              NormalisableRange<float>(0.0f, 1.0f), 0.5f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"vel_level", 1}, "Vel to Level",
              NormalisableRange<float>(0.0f, 1.0f), 0.8f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"bend_range", 1}, "Bend Range",
              NormalisableRange<float>(0.0f, 24.0f, 1.0f), 2.0f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"mod_wave", 1}, "Mod to Wave",
              NormalisableRange<float>(0.0f, 1.0f), 0.3f));

    for (int i = 0; i < 2; i++) {
        auto n = juce::String(i + 1);
        l.add(std::make_unique<AudioParameterChoice>(
              ParameterID{Ids::line(i, "wave"), 1}, "Line " + n + " Wave", waves, i == 0 ? 0 : 5));
        l.add(std::make_unique<AudioParameterInt>(
              ParameterID{Ids::line(i, "octave"), 1}, "Line " + n + " Octave", -2, 2, 0));
        l.add(std::make_unique<AudioParameterInt>(
              ParameterID{Ids::line(i, "semis"), 1}, "Line " + n + " Semitones", -12, 12, 0));
        auto cents = AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1) + " ct"; });
        auto semis = AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1) + " st"; });
        l.add(std::make_unique<AudioParameterFloat>(
              ParameterID{Ids::line(i, "detune"), 1}, "Line " + n + " Detune",
              NormalisableRange<float>(-50.0f, 50.0f, 0.1f), i == 0 ? -7.0f : 7.0f, cents));
        l.add(std::make_unique<AudioParameterFloat>(
              ParameterID{Ids::line(i, "level"), 1}, "Line " + n + " Level",
              NormalisableRange<float>(0.0f, 1.0f), i == 0 ? 0.6f : 0.5f, pct));
        l.add(std::make_unique<AudioParameterFloat>(
              ParameterID{Ids::line(i, "pitch_depth"), 1}, "Line " + n + " Pitch Env Depth",
              NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, semis));

        for (int e = 0; e < 3; e++) {
            auto en = juce::String(kEnvNames[e]);
            for (int s = 0; s < PD_ENV_STEPS; s++) {
                l.add(std::make_unique<AudioParameterInt>(
                    ParameterID{Ids::env(i, e, "rate", s), 1},
                    "L" + n + " " + en + " Rate " + juce::String(s + 1), 0, 99, 70));
                l.add(std::make_unique<AudioParameterInt>(
                    ParameterID{Ids::env(i, e, "level", s), 1},
                    "L" + n + " " + en + " Level " + juce::String(s + 1), 0, 99, 0));
            }
            l.add(std::make_unique<AudioParameterInt>(
                ParameterID{Ids::env(i, e, "sustain"), 1},
                "L" + n + " " + en + " Sustain Step", 0, PD_ENV_STEPS - 1, 1));
            l.add(std::make_unique<AudioParameterInt>(
                ParameterID{Ids::env(i, e, "end"), 1},
                "L" + n + " " + en + " End Step", 0, PD_ENV_STEPS - 1, 2));
        }
    }
    return l;
}

Processor::Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "pdsynth", layout())
{
    pd_patch_init(&patch);

    /* A sound on load, rather than silence with a shrug. Two detuned lines,
     * a waveform envelope that opens and closes, which is the machine in one
     * patch. */
    auto set = [&](int line, int env, std::initializer_list<int> rates,
                   std::initializer_list<int> levels, int sus, int end)
    {
        int s = 0;
        for (int r : rates) apvts.getParameter(Ids::env(line, env, "rate", s++))
                                 ->setValueNotifyingHost(r / 99.0f);
        s = 0;
        for (int v : levels) apvts.getParameter(Ids::env(line, env, "level", s++))
                                  ->setValueNotifyingHost(v / 99.0f);
        auto setInt = [&](const juce::String& id, int v, int hi) {
            apvts.getParameter(id)->setValueNotifyingHost((float)v / (float)hi);
        };
        setInt(Ids::env(line, env, "sustain"), sus, PD_ENV_STEPS - 1);
        setInt(Ids::env(line, env, "end"), end, PD_ENV_STEPS - 1);
    };
    for (int i = 0; i < 2; i++) {
        set(i, 1, { 52, 34, 40, 0, 0, 0, 0, 0 }, { 99, 28, 0, 0, 0, 0, 0, 0 }, 1, 2);
        set(i, 2, { 76, 46, 48, 0, 0, 0, 0, 0 }, { 99, 82, 0, 0, 0, 0, 0, 0 }, 1, 2);
        set(i, 0, { 99, 99, 99, 0, 0, 0, 0, 0 }, { 50, 50, 50, 0, 0, 0, 0, 0 }, 0, 1);
    }

    loadPreset(0);

    voices.resize(kPolyphony);
    for (auto& v : voices) pd_voice_init(&v, &patch, sr);
    for (auto& s : scope) s.store(0.0f);

    if (juce::PluginHostType::getPluginLoadedAs() == AudioProcessor::wrapperType_Standalone)
        openVirtualMidi();
}

void Processor::openVirtualMidi()
{
    virtualIn = juce::MidiInput::createNewDevice("pdsynth", this);
    if (virtualIn) virtualIn->start();
}

void Processor::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m)
{
    uiNotes.addMessageToQueue(
        m.withTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001));
}

void Processor::prepareToPlay(double sampleRate, int)
{
    sr = sampleRate;
    uiNotes.reset(sampleRate);
    pullParameters();
    for (auto& v : voices) pd_voice_init(&v, &patch, sr);
}

bool Processor::isBusesLayoutSupported(const BusesLayout& l) const
{
    auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void Processor::pullParameters()
{
    auto raw = [&](const juce::String& id) {
        auto* p = apvts.getRawParameterValue(id);
        return p ? p->load() : 0.0f;
    };
    patch.line_count  = (int)raw("lines") + 1;
    patch.mix         = (pd_mix_t)(int)raw("mix");
    patch.noise_amount     = raw("noise");
    patch.velocity_to_wave = raw("vel_wave");
    patch.velocity_to_level= raw("vel_level");
    patch.bend_range_semitones = raw("bend_range");
    patch.mod_to_wave          = raw("mod_wave");

    for (int i = 0; i < 2; i++) {
        auto& L = patch.line[i];
        L.wave         = (pd_wave_t)(int)raw(Ids::line(i, "wave"));
        L.octave       = (int)raw(Ids::line(i, "octave"));
        L.semitones    = (int)raw(Ids::line(i, "semis"));
        L.detune_cents = raw(Ids::line(i, "detune"));
        L.level        = raw(Ids::line(i, "level"));
        L.pitch_env_depth_semitones = raw(Ids::line(i, "pitch_depth"));

        pd_env_params_t* envs[3] = { &L.pitch_env, &L.wave_env, &L.amp_env };
        for (int e = 0; e < 3; e++) {
            for (int s = 0; s < PD_ENV_STEPS; s++) {
                envs[e]->rate[s]  = (uint8_t)raw(Ids::env(i, e, "rate", s));
                envs[e]->level[s] = (uint8_t)raw(Ids::env(i, e, "level", s));
            }
            envs[e]->sustain_step = (uint8_t)raw(Ids::env(i, e, "sustain"));
            envs[e]->end_step     = (uint8_t)raw(Ids::env(i, e, "end"));
            if (envs[e]->end_step < envs[e]->sustain_step)
                envs[e]->end_step = envs[e]->sustain_step;
        }
    }
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard;
    pullParameters();
    uiNotes.removeNextBlockOfMessages(midi, buffer.getNumSamples());

    const int n = buffer.getNumSamples();
    buffer.clear();

    int pos = 0;
    for (const auto meta : midi) {
        const int at = juce::jlimit(0, n, meta.samplePosition);
        /* render up to the event, then apply it, so timing inside a block is
         * the timing the host asked for */
        for (; pos < at; pos++) {
            double s = 0;
            for (auto& v : voices) s += pd_voice_next(&v);
            buffer.setSample(0, pos, (float)(s * 0.25));
        }
        const auto m = meta.getMessage();
        if (m.isNoteOn()) {
            pd_voice_t* pick = nullptr;
            for (auto& v : voices) if (!pd_voice_active(&v)) { pick = &v; break; }
            if (!pick) pick = &voices[0];
            /* a new voice starts where the wheels currently are, not at zero */
            pd_voice_set_bend(pick, wheelBend);
            pd_voice_set_mod(pick, wheelMod);
            pd_voice_note_on(pick, m.getNoteNumber(), m.getVelocity() / 127.0);
        } else if (m.isNoteOff()) {
            for (auto& v : voices)
                if (pd_voice_active(&v) && v.note == m.getNoteNumber()) pd_voice_note_off(&v);
        } else if (m.isPitchWheel()) {
            /* 0 to 16383 with 8192 at rest, which is the only place the
             * asymmetry of the MIDI wheel needs handling */
            const double b = (m.getPitchWheelValue() - 8192) / 8192.0;
            wheelBend = b;
            for (auto& v : voices) pd_voice_set_bend(&v, b);
        } else if (m.isController() && m.getControllerNumber() == 1) {
            const double mod = m.getControllerValue() / 127.0;
            wheelMod = mod;
            for (auto& v : voices) pd_voice_set_mod(&v, mod);
        } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
            for (auto& v : voices) pd_voice_note_off(&v);
        }
    }
    for (; pos < n; pos++) {
        double s = 0;
        for (auto& v : voices) s += pd_voice_next(&v);
        buffer.setSample(0, pos, (float)(s * 0.25));
    }

    /* mono engine, both ears */
    if (buffer.getNumChannels() > 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, n);

    /* what the editor draws */
    int live = 0;
    for (auto& v : voices) if (pd_voice_active(&v)) live++;
    voicesNow.store(live);
    bendNow[0].store((float)voices[0].line[0].wave.value);
    bendNow[1].store((float)voices[0].line[1].wave.value);

    float pk = 0.0f;
    int sp = scopePos.load();
    for (int i = 0; i < n; i++) {
        float s = buffer.getSample(0, i);
        pk = juce::jmax(pk, std::abs(s));
        scope[(size_t)sp].store(s);
        sp = (sp + 1) % kScope;
    }
    scopePos.store(sp);
    levelNow.store(juce::jmax(pk, levelNow.load() * 0.82f));
}

void Processor::loadPreset(int index)
{
    const pd_preset_t *p = pd_preset(index);
    if (!p) return;
    currentPreset = index;

    auto set = [&](const juce::String& id, float norm) {
        if (auto* par = apvts.getParameter(id)) {
            par->beginChangeGesture();
            par->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
            par->endChangeGesture();
        }
    };
    auto setRanged = [&](const juce::String& id, float v) {
        if (auto* par = apvts.getParameter(id)) {
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(par)) {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost(rp->convertTo0to1(v));
                rp->endChangeGesture();
            }
        }
    };

    const pd_patch_t &q = p->patch;
    set("lines", q.line_count == 2 ? 1.0f : 0.0f);
    set("mix", (float)q.mix / 2.0f);
    setRanged("noise", (float)q.noise_amount);
    setRanged("vel_wave", (float)q.velocity_to_wave);
    setRanged("vel_level", (float)q.velocity_to_level);
    setRanged("bend_range", (float)q.bend_range_semitones);
    setRanged("mod_wave", (float)q.mod_to_wave);

    for (int i = 0; i < 2; i++) {
        const pd_line_params_t &L = q.line[i];
        set(Ids::line(i, "wave"), (float)L.wave / (float)(PD_WAVE_COUNT - 1));
        setRanged(Ids::line(i, "octave"), (float)L.octave);
        setRanged(Ids::line(i, "semis"), (float)L.semitones);
        setRanged(Ids::line(i, "detune"), (float)L.detune_cents);
        setRanged(Ids::line(i, "level"), (float)L.level);
        setRanged(Ids::line(i, "pitch_depth"), (float)L.pitch_env_depth_semitones);

        const pd_env_params_t *envs[3] = { &L.pitch_env, &L.wave_env, &L.amp_env };
        for (int e = 0; e < 3; e++) {
            for (int s2 = 0; s2 < PD_ENV_STEPS; s2++) {
                setRanged(Ids::env(i, e, "rate", s2), (float)envs[e]->rate[s2]);
                setRanged(Ids::env(i, e, "level", s2), (float)envs[e]->level[s2]);
            }
            setRanged(Ids::env(i, e, "sustain"), (float)envs[e]->sustain_step);
            setRanged(Ids::env(i, e, "end"), (float)envs[e]->end_step);
        }
    }
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

void Processor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, dest);
}
void Processor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}
} // namespace pd

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new pd::Processor(); }
