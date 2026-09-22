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
              StringArray{ "One", "Two", "Three", "Four" }, 1));
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
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"spread", 1}, "Stereo Spread",
              NormalisableRange<float>(0.0f, 1.0f), 0.45f, pct));

    // the four things the hardware could not do
    auto secs = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return v < 0.005f ? juce::String("off")
                                             : juce::String(v, 2) + " s"; });
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"glide", 1}, "Glide",
              NormalisableRange<float>(0.0f, 2.0f, 0.01f), 0.0f, secs));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"at_wave", 1}, "Pressure to Wave",
              NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"at_level", 1}, "Pressure to Level",
              NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct));

    StringArray modes;
    for (int i = 0; i < PD_FILTER_MODES; i++)
        modes.add(juce::String(pd_filter_mode_name((pd_filter_mode_t)i)));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"filt_mode", 1}, "Filter", modes, 0));
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return v >= 1000.0f ? juce::String(v / 1000.0f, 2) + " kHz"
                                               : juce::String(juce::roundToInt(v)) + " Hz"; });
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"filt_cutoff", 1}, "Cutoff",
              NormalisableRange<float>(20.0f, 18000.0f, 1.0f, 0.3f), 8000.0f, hz));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"filt_res", 1}, "Resonance",
              NormalisableRange<float>(0.0f, 1.0f), 0.2f, pct));
    auto oct = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return juce::String(v, 1) + " oct"; });
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"filt_env", 1}, "Cutoff from DCW",
              NormalisableRange<float>(-6.0f, 6.0f, 0.1f), 0.0f, oct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"filt_key", 1}, "Cutoff Key Track",
              NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct));

    /* Chorus, delay and drive. The hardware reissue has a chorus, and a synth
     * that arrives completely dry sounds thinner than the box beside it. */
    auto ms = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return juce::String(v, 1) + " ms"; });
    auto hz2 = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return juce::String(v, 2) + " Hz"; });
    auto secs2 = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return juce::String(juce::roundToInt(v * 1000.0f)) + " ms"; });
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"cho_mix", 1}, "Chorus",
              NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"cho_depth", 1}, "Chorus Depth",
              NormalisableRange<float>(0.2f, 12.0f, 0.1f), 3.2f, ms));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"cho_rate", 1}, "Chorus Rate",
              NormalisableRange<float>(0.05f, 6.0f, 0.01f), 0.42f, hz2));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"cho_spread", 1}, "Chorus Spread",
              NormalisableRange<float>(0.0f, 1.0f), 0.7f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"dly_mix", 1}, "Delay",
              NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"dly_time", 1}, "Delay Time",
              NormalisableRange<float>(0.01f, 2.0f, 0.001f, 0.4f), 0.32f, secs2));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"dly_fb", 1}, "Delay Feedback",
              NormalisableRange<float>(0.0f, 1.0f), 0.32f, pct));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"dly_tone", 1}, "Delay Tone",
              NormalisableRange<float>(0.0f, 1.0f), 0.45f, pct));
    StringArray drives;
    for (int i = 0; i < PD_DRIVE_MODES; i++)
        drives.add(juce::String(pd_drive_mode_name((pd_drive_mode_t)i)));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"drv_mode", 1}, "Drive", drives, 0));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"drv_amount", 1}, "Drive Amount",
              NormalisableRange<float>(0.0f, 1.0f), 0.3f, pct));

    for (int i = 0; i < PD_MAX_LINES; i++) {
        auto n = juce::String(i + 1);
        l.add(std::make_unique<AudioParameterChoice>(
              ParameterID{Ids::line(i, "wave"), 1}, "Line " + n + " Wave", waves, i == 0 ? 0 : 5));
        l.add(std::make_unique<AudioParameterInt>(
              /* A CZ puts its own octave on a line and then detunes the second
               * by up to three octaves more, so two either way is not enough
               * to hold a real patch without flattening it. */
              ParameterID{Ids::line(i, "octave"), 1}, "Line " + n + " Octave", -4, 4, 0));
        l.add(std::make_unique<AudioParameterInt>(
              ParameterID{Ids::line(i, "semis"), 1}, "Line " + n + " Semitones", -12, 12, 0));
        auto cents = AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1) + " ct"; });
        auto semis = AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1) + " st"; });
        l.add(std::make_unique<AudioParameterFloat>(
              ParameterID{Ids::line(i, "detune"), 1}, "Line " + n + " Detune",
              NormalisableRange<float>(-60.0f, 60.0f, 0.1f), i == 0 ? -7.0f : 7.0f, cents));
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
    /*
     * A second output bus carrying pitch, gate and velocity as control
     * voltages, for driving a modular rig from whatever is playing this. It is
     * disabled by default, because a host that routes it to speakers by
     * mistake plays a loud steady tone at whatever the last note was.
     */
    : AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)
          .withOutput("CV", juce::AudioChannelSet::discreteChannels(3), false)),
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

    pd_synth_init(&synth, &patch, sr, kPolyphony);
    pd_fx_params_init(&fxp);
    fx.reset(pd_fx_create(sr));
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
    pd_synth_init(&synth, &patch, sr, kPolyphony);
    fx.reset(pd_fx_create(sr));
}

bool Processor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (l.outputBuses.size() > 1) {
        const auto cvBus = l.getChannelSet(false, 1);
        if (!cvBus.isDisabled() && cvBus.size() != 3) return false;
    }
    return true;
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
    patch.spread               = raw("spread");
    patch.glide_seconds        = raw("glide");
    patch.aftertouch_to_wave   = raw("at_wave");
    patch.aftertouch_to_level  = raw("at_level");
    patch.filter_mode          = (pd_filter_mode_t)(int)raw("filt_mode");
    patch.filter_cutoff_hz     = raw("filt_cutoff");
    patch.filter_resonance     = raw("filt_res");
    patch.filter_env_depth     = raw("filt_env");
    patch.filter_key_track     = raw("filt_key");

    fxp.chorus_mix      = raw("cho_mix");
    fxp.chorus_depth_ms = raw("cho_depth");
    fxp.chorus_rate_hz  = raw("cho_rate");
    fxp.chorus_spread   = raw("cho_spread");
    fxp.delay_mix       = raw("dly_mix");
    fxp.delay_time_s    = raw("dly_time");
    fxp.delay_feedback  = raw("dly_fb");
    fxp.delay_tone      = raw("dly_tone");
    fxp.drive_mode      = (pd_drive_mode_t)(int)raw("drv_mode");
    fxp.drive_amount    = raw("drv_amount");

    for (int i = 0; i < PD_MAX_LINES; i++) {
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
            /*
             * A sustain step past the end step is not a mistake to correct:
             * 18 percent of the envelopes in real CZ patches are like that,
             * and it simply means the hold is never reached. Moving the end
             * step to meet it rewrites the player's envelope, and on a patch
             * loaded from a dump it would rewrite the dump. The engine already
             * bounds both where it uses them.
             */
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

    const bool stereo = buffer.getNumChannels() > 1;
    auto emit = [&](int pos) {
        double l = 0, r = 0;
        pd_synth_render(&synth, &l, &r);
        l *= 0.25; r *= 0.25;
        /* The effects run here, at the output rate and after the voices are
         * summed: none of them needs the oversampled stream, and running them
         * there would cost four times as much for nothing. */
        if (fx) pd_fx_process(fx.get(), &fxp, &l, &r);
        buffer.setSample(0, pos, (float)l);
        if (stereo) buffer.setSample(1, pos, (float)r);
    };

    int pos = 0;
    for (const auto meta : midi) {
        const int at = juce::jlimit(0, n, meta.samplePosition);
        /* render up to the event, then apply it, so timing inside a block is
         * the timing the host asked for */
        for (; pos < at; pos++) emit(pos);

        const auto m = meta.getMessage();
        if (m.isNoteOn()) {
            pd_synth_note_on(&synth, m.getNoteNumber(), m.getVelocity() / 127.0);
            pd_cv_note_on(&cv, m.getNoteNumber(), m.getVelocity() / 127.0);
        } else if (m.isNoteOff()) {
            pd_synth_note_off(&synth, m.getNoteNumber());
            pd_cv_note_off(&cv, m.getNoteNumber());
        } else if (m.isPitchWheel()) {
            /* 0 to 16383 with 8192 at rest, which is the only place the
             * asymmetry of the MIDI wheel needs handling */
            wheelBend = (m.getPitchWheelValue() - 8192) / 8192.0;
            pd_synth_set_bend(&synth, wheelBend);
        } else if (m.isChannelPressure()) {
            pd_synth_set_pressure(&synth, m.getChannelPressureValue() / 127.0);
        } else if (m.isAftertouch()) {
            pd_synth_set_pressure(&synth, m.getAfterTouchValue() / 127.0);
        } else if (m.isController() && m.getControllerNumber() == 1) {
            wheelMod = m.getControllerValue() / 127.0;
            pd_synth_set_mod(&synth, wheelMod);
        } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
            pd_synth_all_off(&synth);
            pd_cv_all_off(&cv);
        }
    }
    for (; pos < n; pos++) emit(pos);

    /* The control voltages, if the host asked for that bus. The pitch follows
     * the sounding frequency rather than the note number, so a glide or a bend
     * leaves by the same wire the note did. */
    if (getBusCount(false) > 1) {
        if (auto cvBuf = getBusBuffer(buffer, false, 1); cvBuf.getNumChannels() >= 3) {
            if (synth.voice_count > 0 && synth.voice[0].base_hz > 0.0)
                pd_cv_track_hz(&cv, synth.voice[0].base_hz);
            for (int ch = 0; ch < 3; ch++) {
                const float v = (float)(ch == 0 ? cv.pitch : ch == 1 ? cv.gate : cv.velocity);
                for (int i = 0; i < n; i++) cvBuf.setSample(ch, i, v);
            }
        }
    }

    /* what the editor draws */
    int live = 0;
    for (int i = 0; i < synth.voice_count; i++) if (pd_voice_active(&synth.voice[i])) live++;
    voicesNow.store(live);
    bendNow[0].store((float)synth.voice[0].line[0].wave.value);
    bendNow[1].store((float)synth.voice[0].line[1].wave.value);
    /* also expose the filter's cutoff so the editor can draw where it sits */
    cutoffNow.store((float)patch.filter_cutoff_hz);

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

/*
 * Parameters, not state. A preset and a Casio voice dump both arrive the same
 * way: as parameter changes the host can see, undo and automate. Swapping
 * state behind the host's back is how a plugin ends up disagreeing with the
 * session that saved it.
 */
void Processor::applyPatch(const pd_patch_t& q)
{
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

    set("lines", (float)(q.line_count - 1) / 3.0f);
    set("mix", (float)q.mix / 2.0f);
    setRanged("noise", (float)q.noise_amount);
    setRanged("vel_wave", (float)q.velocity_to_wave);
    setRanged("vel_level", (float)q.velocity_to_level);
    setRanged("bend_range", (float)q.bend_range_semitones);
    setRanged("mod_wave", (float)q.mod_to_wave);
    setRanged("spread", (float)q.spread);

    setRanged("filt_mode", (float)q.filter_mode);
    setRanged("filt_cutoff", (float)q.filter_cutoff_hz);
    setRanged("filt_res", (float)q.filter_resonance);
    setRanged("filt_env", (float)q.filter_env_depth);
    setRanged("filt_key", (float)q.filter_key_track);
    setRanged("glide", (float)q.glide_seconds);
    setRanged("at_wave", (float)q.aftertouch_to_wave);
    setRanged("at_level", (float)q.aftertouch_to_level);

    for (int i = 0; i < PD_MAX_LINES; i++) {
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

void Processor::loadPreset(int index)
{
    const pd_preset_t *p = pd_preset(index);
    if (!p) return;
    currentPreset = index;
    haveSysexBase = false;      /* a preset is not a dump anybody sent us */
    applyPatch(p->patch);

    auto setRanged = [&](const juce::String& id, float v) {
        if (auto* par = apvts.getParameter(id))
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(par)) {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost(rp->convertTo0to1(v));
                rp->endChangeGesture();
            }
    };
    auto set = [&](const juce::String& id, float norm) {
        if (auto* par = apvts.getParameter(id)) {
            par->beginChangeGesture();
            par->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
            par->endChangeGesture();
        }
    };
    /* the effects come with the preset, because for some of these the effect
     * is the sound rather than a decoration on it */
    const pd_fx_params_t &f = p->fx;
    setRanged("cho_mix", (float)f.chorus_mix);
    setRanged("cho_depth", (float)f.chorus_depth_ms);
    setRanged("cho_rate", (float)f.chorus_rate_hz);
    setRanged("cho_spread", (float)f.chorus_spread);
    setRanged("dly_mix", (float)f.delay_mix);
    setRanged("dly_time", (float)f.delay_time_s);
    setRanged("dly_fb", (float)f.delay_feedback);
    setRanged("dly_tone", (float)f.delay_tone);
    set("drv_mode", (float)f.drive_mode / (float)(PD_DRIVE_MODES - 1));
    setRanged("drv_amount", (float)f.drive_amount);

}

/* ---------------------------------------------------------------------------
 * Casio CZ voice dumps.
 * ------------------------------------------------------------------------ */
static juce::String formatReport(const pd_sysex_report_t& r, const juce::String& lead)
{
    juce::String t = lead;
    if (r.count == 0) { t << "\n\nNothing was lost on the way."; return t; }
    t << "\n";
    for (int i = 0; i < r.count; i++)
        t << "\n" << juce::String(r.note[i].control) << ": "
          << juce::String(r.note[i].what) << "\n";
    if (r.dropped > 0)
        t << "\nand " << r.dropped << " more.";
    return t;
}

bool Processor::loadSysex(const juce::File& f, juce::String& report)
{
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) { report = "That file could not be read."; return false; }

    pd_patch_t q {};
    pd_sysex_report_t rep;
    const int rc = pd_sysex_read_ex((const uint8_t*)mb.getData(), mb.getSize(),
                                    &q, sysexBase, &rep);
    if (rc != 0) {
        report = rc == -2 ? "That is system exclusive, but not Casio's."
               : rc == -3 ? "That is not a system exclusive message."
               : rc == -4 ? "That looks like a Casio message with its data damaged."
                          : "That is not a CZ voice dump. A dump is 263 or 264 bytes; "
                            "this file is " + juce::String((int)mb.getSize()) + ".";
        return false;
    }
    haveSysexBase = true;
    applyPatch(q);
    report = formatReport(rep, "Loaded " + f.getFileName() + ".");
    return true;
}

bool Processor::saveSysex(const juce::File& f, juce::String& report)
{
    pullParameters();
    uint8_t out[PD_SYSEX_BYTES];
    pd_sysex_report_t rep;
    const size_t n = pd_sysex_write_ex(&patch, haveSysexBase ? sysexBase : nullptr,
                                       0, 0x60, out, sizeof out, &rep);
    if (n == 0) { report = "The dump could not be written."; return false; }
    if (!f.replaceWithData(out, n)) {
        report = "That file could not be written.";
        return false;
    }
    report = formatReport(rep, "Wrote " + f.getFileName()
                               + ", " + juce::String((int)n) + " bytes.");
    return true;
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
