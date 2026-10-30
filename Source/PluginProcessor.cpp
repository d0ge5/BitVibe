#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <limits>
#include <map>

using namespace juce;

namespace
{
using Layout = AudioProcessorValueTreeState::ParameterLayout;

AudioParameterFloatAttributes withText(std::function<String(float, int)> fn)
{
    return AudioParameterFloatAttributes().withStringFromValueFunction(std::move(fn));
}

String hzText(float hz)
{
    return hz >= 1000.0f ? String(hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : String(roundToInt(hz)) + " Hz";
}
} // namespace

AudioProcessorValueTreeState::ParameterLayout BitVibeProcessor::createLayout()
{
    using namespace bitvibe;
    Layout layout;
    auto id = [](const char* s) { return ParameterID { s, 1 }; };

    // ---- front panel -------------------------------------------------------------------
    layout.add(std::make_unique<AudioParameterFloat>(
        id(ids::sampleRate), "Sample Rate", NormalisableRange<float>(0.0f, 1.0f), 1.0f,
        withText([](float v, int) { return hzText(mapping::sampleRateHz(v)); })));
    layout.add(std::make_unique<AudioParameterFloat>(
        id(ids::filter), "Filter", NormalisableRange<float>(0.0f, 1.0f), 1.0f,
        withText([](float v, int) { return hzText(mapping::filterHz(v)); })));
    layout.add(std::make_unique<AudioParameterFloat>(
        id(ids::bits), "Bits", NormalisableRange<float>(0.0f, 1.0f), 1.0f,
        withText([](float v, int) { return String(mapping::bitDepth(v), 1) + " bit"; })));

    layout.add(std::make_unique<AudioParameterInt>(id(ids::stutter), "Stutter", 0, mapping::kStutterPositions - 1, 0));
    layout.add(std::make_unique<AudioParameterChoice>(
        id(ids::seqPlay), "Sequencer Play",
        StringArray { "Off", "Once", "Twice", "4 Times", "8 Times", "Continuous" }, 5));
    layout.add(std::make_unique<AudioParameterChoice>(
        id(ids::seqMult), "Sequencer Mult",
        StringArray { "x1", "x2", "x4", "x8", "x16", "x32", "x64", "x128" }, 1));

    // ---- second layer (Alt): six sequencer steps ----------------------------------------
    const Params defaults;
    for (int i = 0; i < kNumSteps; ++i)
        layout.add(std::make_unique<AudioParameterFloat>(id(ids::step(i).toRawUTF8()), "Step " + String(i + 1),
                                                         NormalisableRange<float>(0.0f, 1.0f),
                                                         defaults.steps[(size_t) i]));

    layout.add(std::make_unique<AudioParameterChoice>(id(ids::seqType), "Sequencer Type",
                                                      StringArray { "Pitch", "Filter", "Sample Rate" }, 0));

    // ---- the loop section of the sequence (stored in the preset slots) ----------------------
    {
        const StringArray stepNames { "Step 1", "Step 2", "Step 3", "Step 4", "Step 5", "Step 6" };
        layout.add(std::make_unique<AudioParameterBool>(id(ids::loopOn), "Sequence Loop", false));
        layout.add(std::make_unique<AudioParameterChoice>(id(ids::loopFrom), "Loop From", stepNames, 3));
        layout.add(std::make_unique<AudioParameterChoice>(id(ids::loopTo), "Loop To", stepNames, 5));
        layout.add(std::make_unique<AudioParameterChoice>(id(ids::loopRepeats), "Loop Repeats",
                                                          StringArray { "Forever", "2 Times", "3 Times", "4 Times", "8 Times", "16 Times" }, 0));
    }

    // ---- tempo, switches ---------------------------------------------------------------
    NormalisableRange<float> tempoRange(kMinTempoBpm, kMaxTempoKnobBpm, 0.1f);
    tempoRange.setSkewForCentre(110.0f);
    layout.add(std::make_unique<AudioParameterFloat>(
        id(ids::tempo), "Tempo", tempoRange, 120.0f,
        withText([](float v, int) { return String(v, 1) + " BPM"; })));
    layout.add(std::make_unique<AudioParameterBool>(id(ids::tempoSync), "Sync To Host", false));
    layout.add(std::make_unique<AudioParameterBool>(id(ids::bypass), "Bypass", false));
    layout.add(std::make_unique<AudioParameterBool>(id(ids::hold), "Stutter Hold", false));

    // ---- global settings ---------------------------------------------------------------
    layout.add(std::make_unique<AudioParameterFloat>(
        id(ids::inputGain), "Input Gain", NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        withText([](float v, int) { return String(v, 1) + " dB"; })));
    layout.add(std::make_unique<AudioParameterFloat>(
        id(ids::outputGain), "Output Gain", NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f,
        withText([](float v, int) { return String(v, 1) + " dB"; })));
    layout.add(std::make_unique<AudioParameterChoice>(id(ids::headroom), "Headroom",
                                                      StringArray { "Instrument", "Line" }, 0));
    layout.add(std::make_unique<AudioParameterFloat>(id(ids::sensitivity), "Trigger Sensitivity",
                                                     NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    layout.add(std::make_unique<AudioParameterChoice>(id(ids::stutterLen), "Stutter Length",
                                                      StringArray { "1/4 note", "1/8 note", "1/16 note" }, 1));

    // ---- expression morph --------------------------------------------------------------
    // 0 = toe-up knob set, 1 = toe-down knob set; only heard while Expression Morph is on.
    layout.add(std::make_unique<AudioParameterFloat>(
        id(ids::expression), "Expression", NormalisableRange<float>(0.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) { return String(roundToInt(v * 100.0f)) + "%"; })
            .withValueFromStringFunction([](const String& t) { return jlimit(0.0f, 1.0f, t.getFloatValue() / 100.0f); })));
    layout.add(std::make_unique<AudioParameterBool>(id(ids::morph), "Expression Morph", false));

    // ---- blend and trigger-only effects --------------------------------------------------
    // Mix: 0 = only the clean input, 1 = only the effect. Both settings belong to the rig, not to a
    // preset slot (see the comment in the header).
    layout.add(std::make_unique<AudioParameterFloat>(
        id(ids::mix), "Mix", NormalisableRange<float>(0.0f, 1.0f), 1.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) { return String(roundToInt(v * 100.0f)) + "% effect"; })
            .withValueFromStringFunction([](const String& t) { return jlimit(0.0f, 1.0f, t.getFloatValue() / 100.0f); })));
    layout.add(std::make_unique<AudioParameterBool>(id(ids::fxTriggers), "FX Only On Triggers", false));

    // ---- reverb on the sequenced sound (rig settings too). Mix 0 = off. -------------------
    auto percent = AudioParameterFloatAttributes()
                       .withStringFromValueFunction([](float v, int) { return String(roundToInt(v * 100.0f)) + "%"; })
                       .withValueFromStringFunction([](const String& t) { return jlimit(0.0f, 1.0f, t.getFloatValue() / 100.0f); });
    layout.add(std::make_unique<AudioParameterFloat>(id(ids::reverbTone), "Reverb Tone", NormalisableRange<float>(0.0f, 1.0f), 0.5f, percent));
    layout.add(std::make_unique<AudioParameterFloat>(id(ids::reverbMix), "Reverb Mix", NormalisableRange<float>(0.0f, 1.0f), 0.0f, percent));
    layout.add(std::make_unique<AudioParameterFloat>(
        id(ids::reverbLevel), "Reverb Level", NormalisableRange<float>(-30.0f, 6.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) { return String(v, 1) + " dB"; })
            .withValueFromStringFunction([](const String& t) { return t.getFloatValue(); })));
    layout.add(std::make_unique<AudioParameterChoice>(id(ids::reverbType), "Reverb Type", StringArray { "Room", "Hall", "Spring", "Plate", "Shimmer", "Gated" }, 0));
    layout.add(std::make_unique<AudioParameterBool>(id(ids::reverbOn), "Reverb On", true));
    layout.add(std::make_unique<AudioParameterFloat>(id(ids::reverbDecay), "Reverb Decay", NormalisableRange<float>(0.0f, 1.0f), 0.5f, percent));
    // Each type of reverb has its own REVERSE tick (the tail swells up after the sound instead of dying away).
    layout.add(std::make_unique<AudioParameterBool>(id(ids::reverbReverseRoom), "Reverse Room Reverb", false));
    layout.add(std::make_unique<AudioParameterBool>(id(ids::reverbReverseHall), "Reverse Hall Reverb", false));
    layout.add(std::make_unique<AudioParameterBool>(id(ids::reverbReverseSpring), "Reverse Spring Reverb", false));
    layout.add(std::make_unique<AudioParameterBool>(id(ids::reverbReversePlate), "Reverse Plate Reverb", false));
    layout.add(std::make_unique<AudioParameterBool>(id(ids::reverbReverseShimmer), "Reverse Shimmer Reverb", false));
    layout.add(std::make_unique<AudioParameterBool>(id(ids::reverbReverseGated), "Reverse Gated Reverb", false));
    // ---- distortion on the sequenced sound (rig settings): drive, tone and level like the classic green pedal ----
    layout.add(std::make_unique<AudioParameterBool>(id(ids::distOn), "Distortion On", false));
    layout.add(std::make_unique<AudioParameterChoice>(id(ids::distType), "Distortion Type", StringArray { "Overdrive", "Fuzz" }, 0));
    layout.add(std::make_unique<AudioParameterFloat>(id(ids::distDrive), "Distortion Drive", NormalisableRange<float>(0.0f, 1.0f), 0.5f, percent));
    layout.add(std::make_unique<AudioParameterFloat>(id(ids::distTone), "Distortion Tone", NormalisableRange<float>(0.0f, 1.0f), 0.5f, percent));
    layout.add(std::make_unique<AudioParameterFloat>(id(ids::distLevel), "Distortion Level", NormalisableRange<float>(0.0f, 1.0f), 0.8f, percent));
    layout.add(std::make_unique<AudioParameterBool>(id(ids::distReverb), "Distort Reverb Too", false));

    // ---- sample blend: a random sound from the player's list joins every sequencer step (rig settings) ----
    layout.add(std::make_unique<AudioParameterBool>(id(ids::sampleOn), "Sample Blend On", true));

    // ---- the EQ on the sequenced sound (rig settings): eight bands and an output gain ----
    {
        layout.add(std::make_unique<AudioParameterBool>(id(ids::eqOn), "EQ On", false));
        auto dbText = AudioParameterFloatAttributes()
                          .withStringFromValueFunction([](float v, int) { return String(v, 1) + " dB"; })
                          .withValueFromStringFunction([](const String& t) { return t.getFloatValue(); });
        layout.add(std::make_unique<AudioParameterFloat>(id(ids::eqOutput), "EQ Output", NormalisableRange<float>(-15.0f, 15.0f, 0.1f), 0.0f, dbText));
        NormalisableRange<float> freqRange(bitvibe::kEqMinHz, bitvibe::kEqMaxHz);
        freqRange.setSkewForCentre(1000.0f);
        NormalisableRange<float> qRange(bitvibe::kEqMinQ, bitvibe::kEqMaxQ);
        qRange.setSkewForCentre(1.0f);
        auto hzText = AudioParameterFloatAttributes()
                          .withStringFromValueFunction([](float v, int) { return v >= 1000.0f ? String(v / 1000.0f, 2) + " kHz" : String(v, 1) + " Hz"; })
                          .withValueFromStringFunction([](const String& t) { return t.containsIgnoreCase("k") ? t.getFloatValue() * 1000.0f : t.getFloatValue(); });
        StringArray types;
        for (int t = 0; t < bitvibe::kNumEqTypes; ++t)
            types.add(bitvibe::eqTypeName((bitvibe::EqType) t));
        const auto eqDefaults = bitvibe::defaultEqBands();
        for (int b = 0; b < bitvibe::kNumEqBands; ++b)
        {
            const auto& d = eqDefaults[(size_t) b];
            const String n = "EQ " + String(b + 1) + " ";
            layout.add(std::make_unique<AudioParameterBool>(ParameterID { ids::eqBand(b, "On"), 1 }, n + "On", d.on));
            layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::eqBand(b, "Type"), 1 }, n + "Type", types, (int) d.type));
            layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::eqBand(b, "Freq"), 1 }, n + "Frequency", freqRange, d.freq, hzText));
            layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::eqBand(b, "Gain"), 1 }, n + "Gain",
                                                             NormalisableRange<float>(-bitvibe::kEqMaxGainDb, bitvibe::kEqMaxGainDb, 0.1f), d.gainDb, dbText));
            layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::eqBand(b, "Q"), 1 }, n + "Q", qRange, d.q,
                                                             AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return String(v, 2); })));
        }
    }
    layout.add(std::make_unique<AudioParameterFloat>(id(ids::sampleMix), "Sample Mix", NormalisableRange<float>(0.0f, 1.0f), 0.0f, percent));
    layout.add(std::make_unique<AudioParameterFloat>(id(ids::sampleChance), "Sample Chance", NormalisableRange<float>(0.0f, 1.0f), 1.0f, percent));
    // The volume range of the sounds: MIX goes from the base volume (0) to the max volume (1). The bottom of a knob is silence.
    auto volumeText = AudioParameterFloatAttributes()
                          .withStringFromValueFunction([](float v, int) { return v <= bitvibe::Engine::kSampleOffDb + 0.05f ? String("Off") : String(v, 1) + " dB"; })
                          .withValueFromStringFunction([](const String& t) { return t.trim().equalsIgnoreCase("off") ? bitvibe::Engine::kSampleOffDb : t.getFloatValue(); });
    layout.add(std::make_unique<AudioParameterFloat>(id(ids::sampleBase), "Sample Base Volume",
                                                     NormalisableRange<float>(bitvibe::Engine::kSampleOffDb, 6.0f, 0.5f), bitvibe::Engine::kSampleOffDb, volumeText));
    layout.add(std::make_unique<AudioParameterFloat>(id(ids::sampleMax), "Sample Max Volume",
                                                     NormalisableRange<float>(bitvibe::Engine::kSampleOffDb, 6.0f, 0.5f), -9.0f, volumeText));
    {
        // Low cut (high-pass) and high cut (low-pass) on the reverb; the ends of the range mean "off".
        auto hzText = [](float off, bool low)
        {
            return [off, low](float v, int)
            {
                if (low ? v <= off + 0.5f : v >= off - 50.0f)
                    return String("Off");
                return v >= 1000.0f ? String(v / 1000.0f, 1) + " kHz" : String(roundToInt(v)) + " Hz";
            };
        };
        NormalisableRange<float> lowRange(20.0f, 2000.0f);
        lowRange.setSkewForCentre(200.0f);
        NormalisableRange<float> highRange(200.0f, 20000.0f);
        highRange.setSkewForCentre(3000.0f);
        layout.add(std::make_unique<AudioParameterFloat>(id(ids::reverbLowCut), "Reverb Low Cut", lowRange, 20.0f,
                                                         AudioParameterFloatAttributes().withStringFromValueFunction(hzText(20.0f, true))));
        layout.add(std::make_unique<AudioParameterFloat>(id(ids::reverbHighCut), "Reverb High Cut", highRange, 20000.0f,
                                                         AudioParameterFloatAttributes().withStringFromValueFunction(hzText(20000.0f, false))));
    }

    // ---- drum beat (DRUMS tab) -----------------------------------------------------------
    // Also rig settings, not stored in preset slots.
    layout.add(std::make_unique<AudioParameterBool>(id(ids::beatPlay), "Beat Play", false));
    layout.add(std::make_unique<AudioParameterFloat>(
        id(ids::beatLevel), "Beat Level", NormalisableRange<float>(DrumBeat::kBeatOffDb, 6.0f, 0.1f), -6.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) { return v <= DrumBeat::kBeatOffDb ? String("Off") : String(v, 1) + " dB"; })
            .withValueFromStringFunction([](const String& t)
                                         { return t.trim().equalsIgnoreCase("off") ? DrumBeat::kBeatOffDb : t.getFloatValue(); })));
    layout.add(std::make_unique<AudioParameterBool>(id(ids::pickTriggers), "Pick Triggers", true));
    layout.add(std::make_unique<AudioParameterBool>(id(ids::patternLock), "Pattern Lock", false));
    layout.add(std::make_unique<AudioParameterChoice>(id(ids::stepClock), "Step Clock", StringArray { "Tempo", "Drum hits" }, 0));
    // Where the beat's sound goes (see BeatRoute.h). Also a rig setting.
    layout.add(std::make_unique<AudioParameterChoice>(id(ids::beatOutput), "Beat Output", StringArray { "Auto", "Monitor", "Main" }, 0));
    return layout;
}

namespace
{
// Parameters a preset slot stores, in this order. Bypass, stutter hold, host sync, the expression
// position, the input / output / headroom / trigger settings, the mix, the reverb and the effects-on-triggers
// option are left out on purpose (see the comment in the header).
std::vector<String> presetParameterIds()
{
    std::vector<String> list { ids::sampleRate, ids::filter, ids::bits, ids::stutter, ids::seqPlay, ids::seqMult };
    for (int i = 0; i < bitvibe::kNumSteps; ++i)
        list.push_back(ids::step(i));
    for (const char* id : { ids::seqType, ids::tempo, ids::stutterLen, ids::morph, ids::loopOn, ids::loopFrom, ids::loopTo, ids::loopRepeats })
        list.emplace_back(id);
    return list;
}

constexpr int kMaxPresetNameLength = 32;

String cleanPresetName(const String& name, const String& fallback)
{
    const auto trimmed = name.trim().substring(0, kMaxPresetNameLength).trim();
    return trimmed.isEmpty() ? fallback : trimmed;
}

// Knob sets are written as one node of plain attributes named like the parameters.
ValueTree knobsToTree(const bitvibe::KnobSet& k, const Identifier& type)
{
    ValueTree t(type);
    t.setProperty(ids::sampleRate, (double) k.sampleRate, nullptr);
    t.setProperty(ids::filter, (double) k.filter, nullptr);
    t.setProperty(ids::bits, (double) k.bits, nullptr);
    t.setProperty(ids::stutter, k.stutter, nullptr);
    t.setProperty(ids::seqPlay, k.seqPlay, nullptr);
    t.setProperty(ids::seqMult, k.seqMult, nullptr);
    t.setProperty(ids::seqType, (int) k.seqType, nullptr);
    for (int i = 0; i < bitvibe::kNumSteps; ++i)
        t.setProperty(Identifier(ids::step(i)), (double) k.steps[(size_t) i], nullptr);
    return t;
}

// Anything missing keeps the value in `fallback`; everything is pulled into its legal range.
bitvibe::KnobSet knobsFromTree(const ValueTree& t, const bitvibe::KnobSet& fallback)
{
    if (!t.isValid())
        return fallback;

    bitvibe::KnobSet k = fallback;
    auto number = [&t](const char* id, double def) { return (double) t.getProperty(Identifier(id), def); };
    k.sampleRate = (float) number(ids::sampleRate, k.sampleRate);
    k.filter = (float) number(ids::filter, k.filter);
    k.bits = (float) number(ids::bits, k.bits);
    k.stutter = roundToInt(number(ids::stutter, k.stutter));
    k.seqPlay = roundToInt(number(ids::seqPlay, k.seqPlay));
    k.seqMult = roundToInt(number(ids::seqMult, k.seqMult));
    k.seqType = (bitvibe::SeqType) roundToInt(number(ids::seqType, (double) (int) k.seqType));
    for (int i = 0; i < bitvibe::kNumSteps; ++i)
        k.steps[(size_t) i] = (float) number(ids::step(i).toRawUTF8(), k.steps[(size_t) i]);
    return bitvibe::morph::sanitise(k);
}
} // namespace

BitVibeProcessor::BitVibeProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", AudioChannelSet::stereo(), true)
                         .withOutput("Output", AudioChannelSet::stereo(), true)
                         // Optional: the drum beat for the player to hear, kept out of the main output.
                         .withOutput("Beat Monitor", AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "BitVibe", createLayout())
{
    auto raw = [this](const char* id) { return apvts.getRawParameterValue(id); };
    pSample = raw(ids::sampleRate);
    pFilter = raw(ids::filter);
    pBits = raw(ids::bits);
    pStutter = raw(ids::stutter);
    pSeqPlay = raw(ids::seqPlay);
    pSeqMult = raw(ids::seqMult);
    pSeqType = raw(ids::seqType);
    pLoopOn = raw(ids::loopOn);
    pLoopFrom = raw(ids::loopFrom);
    pLoopTo = raw(ids::loopTo);
    pLoopRepeats = raw(ids::loopRepeats);
    pTempo = raw(ids::tempo);
    pTempoSync = raw(ids::tempoSync);
    pBypass = raw(ids::bypass);
    pHold = raw(ids::hold);
    pInGain = raw(ids::inputGain);
    pOutGain = raw(ids::outputGain);
    pHeadroom = raw(ids::headroom);
    pSensitivity = raw(ids::sensitivity);
    pStutterLen = raw(ids::stutterLen);
    pExpression = raw(ids::expression);
    pMorph = raw(ids::morph);
    pMix = raw(ids::mix);
    pFxTriggers = raw(ids::fxTriggers);
    pReverbTone = raw(ids::reverbTone);
    pReverbMix = raw(ids::reverbMix);
    pReverbLevel = raw(ids::reverbLevel);
    pReverbType = raw(ids::reverbType);
    pReverbOn = raw(ids::reverbOn);
    pSampleOn = raw(ids::sampleOn);
    pEqOn = raw(ids::eqOn);
    pEqOutput = raw(ids::eqOutput);
    for (int b = 0; b < bitvibe::kNumEqBands; ++b)
    {
        auto& e = pEqBands[(size_t) b];
        e.on = apvts.getRawParameterValue(ids::eqBand(b, "On"));
        e.type = apvts.getRawParameterValue(ids::eqBand(b, "Type"));
        e.freq = apvts.getRawParameterValue(ids::eqBand(b, "Freq"));
        e.gain = apvts.getRawParameterValue(ids::eqBand(b, "Gain"));
        e.q = apvts.getRawParameterValue(ids::eqBand(b, "Q"));
    }
    pReverbDecay = raw(ids::reverbDecay);
    pReverbReverse = { raw(ids::reverbReverseRoom), raw(ids::reverbReverseHall), raw(ids::reverbReverseSpring),
                       raw(ids::reverbReversePlate), raw(ids::reverbReverseShimmer), raw(ids::reverbReverseGated) };
    pDistOn = raw(ids::distOn);
    pDistType = raw(ids::distType);
    pDistDrive = raw(ids::distDrive);
    pDistTone = raw(ids::distTone);
    pDistLevel = raw(ids::distLevel);
    pDistReverb = raw(ids::distReverb);
    pSampleMix = raw(ids::sampleMix);
    pSampleChance = raw(ids::sampleChance);
    pSampleBase = raw(ids::sampleBase);
    pSampleMax = raw(ids::sampleMax);
    pReverbLowCut = raw(ids::reverbLowCut);
    pReverbHighCut = raw(ids::reverbHighCut);
    pBeatPlay = raw(ids::beatPlay);
    pBeatLevel = raw(ids::beatLevel);
    pBeatOutput = raw(ids::beatOutput);
    pPickTriggers = raw(ids::pickTriggers);
    pPatternLock = raw(ids::patternLock);
    pStepClock = raw(ids::stepClock);
    for (int i = 0; i < bitvibe::kNumSteps; ++i)
        pSteps[(size_t) i] = raw(ids::step(i).toRawUTF8());

    bypassParam = apvts.getParameter(ids::bypass);

    // A new instance starts with the sample MIX and CHANCE the player last set (a saved project overrides them).
    {
        SampleBank::Levels remembered;
        if (sampleBank.getRememberedLevels(remembered))
        {
            const std::array<std::pair<const char*, float>, 4> levels { { { ids::sampleMix, remembered.mix }, { ids::sampleChance, remembered.chance },
                                                                          { ids::sampleBase, remembered.baseDb }, { ids::sampleMax, remembered.maxDb } } };
            for (const auto& level : levels)
                if (auto* param = apvts.getParameter(level.first))
                    param->setValueNotifyingHost(param->convertTo0to1(level.second));
        }
    }

    for (const auto& id : presetParameterIds())
        presetParams.push_back({ id, apvts.getParameter(id) });
    jassert(std::none_of(presetParams.begin(), presetParams.end(), [](const PresetParam& p) { return p.param == nullptr; }));
    resetToFactoryBank();
}

void BitVibeProcessor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate);
    drumBeat.setDeviceSampleRate(sampleRate);
    drumBeat.restartPlayback();
}

bool BitVibeProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (!(in == out && (in == AudioChannelSet::mono() || in == AudioChannelSet::stereo())))
        return false;

    // The optional "Beat Monitor" output is either off or stereo; there is only the one input.
    if (layouts.inputBuses.size() > 1 || layouts.outputBuses.size() > 2)
        return false;
    if (layouts.outputBuses.size() == 2)
    {
        const auto& monitor = layouts.outputBuses.getReference(1);
        if (!monitor.isDisabled() && monitor != AudioChannelSet::stereo())
            return false;
    }
    return true;
}

bitvibe::KnobSet BitVibeProcessor::liveKnobs() const
{
    bitvibe::KnobSet k;
    k.sampleRate = pSample->load();
    k.filter = pFilter->load();
    k.bits = pBits->load();
    k.stutter = roundToInt(pStutter->load());
    k.seqPlay = roundToInt(pSeqPlay->load());
    k.seqMult = roundToInt(pSeqMult->load());
    k.seqType = (bitvibe::SeqType) roundToInt(pSeqType->load());
    for (int i = 0; i < bitvibe::kNumSteps; ++i)
        k.steps[(size_t) i] = pSteps[(size_t) i]->load();
    return k;
}

void BitVibeProcessor::refreshMorphCache()
{
    // Never wait on the message thread: if it is in the middle of a capture, keep last block's copy.
    const SpinLock::ScopedTryLockType lock(morphLock);
    if (lock.isLocked())
    {
        cachedToeUp = toeUp;
        cachedToeDown = toeDown;
    }
}

bitvibe::Params BitVibeProcessor::readParams()
{
    bitvibe::Params p;
    bitvibe::applyKnobs(p, liveKnobs());

    // With the morph on, the twelve knobs (front panel and Alt layer) come from the two stored sets,
    // blended by the expression; the knobs themselves only matter when capturing a set.
    if (pMorph->load() > 0.5f)
    {
        refreshMorphCache();
        bitvibe::applyKnobs(p, bitvibe::morph::blend(cachedToeUp, cachedToeDown, pExpression->load()));
    }

    p.loopOn = pLoopOn->load() > 0.5f;
    p.loopFrom = roundToInt(pLoopFrom->load());
    p.loopTo = roundToInt(pLoopTo->load());
    {
        static constexpr int kPlays[] { 0, 2, 3, 4, 8, 16 }; // as listed in the Loop Repeats parameter
        p.loopPlays = kPlays[jlimit(0, 5, roundToInt(pLoopRepeats->load()))];
    }

    p.tempoBpm = pTempo->load();
    if (pTempoSync->load() > 0.5f)
        if (auto* head = getPlayHead())
            if (auto pos = head->getPosition())
                if (auto bpm = pos->getBpm())
                    p.tempoBpm = (float) *bpm;
    p.tempoBpm = jlimit(bitvibe::kMinTempoBpm, bitvibe::kMaxTempoBpm, p.tempoBpm);
    effectiveBpm.store(p.tempoBpm, std::memory_order_relaxed);

    {
        // The beat pattern fires from the plug-in's own tempo; while following the host it lines up with the host's position.
        const auto pattern = drumBeat.getPattern();
        p.patternOn = pattern.action != bitvibe::DrumAction::Off && pattern.steps != 0;
        p.patternAction = bitvibe::toTriggerAction(pattern.action);
        p.patternSteps = pattern.steps;
        p.patternBarSteps = bitvibe::timeSignature(pattern.signature).totalSteps;
        if (pTempoSync->load() > 0.5f)
            if (auto* head = getPlayHead())
                if (auto pos = head->getPosition())
                    if (pos->getIsPlaying())
                        if (auto ppq = pos->getPpqPosition())
                            p.hostPpq = *ppq;
    }

    p.bypass = pBypass->load() > 0.5f;
    p.stutterHold = pHold->load() > 0.5f;
    p.inputGainDb = pInGain->load();
    p.outputGainDb = pOutGain->load();
    p.lineHeadroom = roundToInt(pHeadroom->load()) == 1;
    p.triggerSensitivity = pSensitivity->load();
    p.stutterDivision = roundToInt(pStutterLen->load());
    p.mix = pMix->load();
    p.fxOnlyOnTriggers = pFxTriggers->load() > 0.5f;
    p.reverbTone = pReverbTone->load();
    p.reverbMix = pReverbMix->load();
    p.reverbLevelDb = pReverbLevel->load();
    p.reverbType = roundToInt(pReverbType->load());
    p.reverbOn = pReverbOn->load() > 0.5f;
    p.sampleOn = pSampleOn->load() > 0.5f;
    p.eqOn = pEqOn->load() > 0.5f;
    p.eqOutputDb = pEqOutput->load();
    for (int b = 0; b < bitvibe::kNumEqBands; ++b)
    {
        const auto& e = pEqBands[(size_t) b];
        auto& band = p.eqBands[(size_t) b];
        band.on = e.on->load() > 0.5f;
        band.type = (bitvibe::EqType) jlimit(0, bitvibe::kNumEqTypes - 1, roundToInt(e.type->load()));
        band.freq = e.freq->load();
        band.gainDb = e.gain->load();
        band.q = e.q->load();
    }
    p.reverbDecay = pReverbDecay->load();
    p.reverbReverse = pReverbReverse[(size_t) jlimit(0, 5, p.reverbType)]->load() > 0.5f;
    p.distOn = pDistOn->load() > 0.5f;
    p.distType = roundToInt(pDistType->load());
    p.distDrive = pDistDrive->load();
    p.distTone = pDistTone->load();
    p.distLevel = pDistLevel->load();
    p.distReverb = pDistReverb->load() > 0.5f;
    p.sampleMix = pSampleMix->load();
    p.sampleChance = pSampleChance->load();
    p.sampleBaseDb = pSampleBase->load();
    p.sampleMaxDb = pSampleMax->load();
    p.sampleSet = sampleBank.beginBlock();
    p.reverbLowCutHz = pReverbLowCut->load();
    p.reverbHighCutHz = pReverbHighCut->load();
    p.pickTriggers = pPickTriggers->load() > 0.5f;
    p.patternLock = pPatternLock->load() > 0.5f;
    p.stepsOnHits = roundToInt(pStepClock->load()) == 1;
    return p;
}

void BitVibeProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // The main bus carries the guitar; the optional "Beat Monitor" bus (two channels after it) is only there
    // when the host has switched it on. Both are views into the host's buffer, nothing is allocated.
    auto mainOut = getBusBuffer(buffer, false, 0);
    auto monitor = getBusBuffer(buffer, false, 1);
    const bool monitorConnected = monitor.getNumChannels() > 0;
    const int numChannels = std::min(mainOut.getNumChannels(), bitvibe::Engine::kMaxChannels);
    for (int c = numChannels; c < mainOut.getNumChannels(); ++c)
        mainOut.clear(c, 0, numSamples);
    if (monitorConnected)
        monitor.clear(); // output-only channels arrive with whatever the host left in them

    // The drum beat: its hits trigger the sequencer / stutter, and its sound is added after the effect (the
    // drums are never crushed) to exactly one place: the main output, or the Beat Monitor output so that it
    // stays out of the main output and everything after it. Bypass silences both, so it stays transparent.
    const auto params = readParams();
    const int numEvents = drumBeat.beginBlock(numSamples, pBeatPlay->load() > 0.5f, !params.bypass, triggerEvents.data(),
                                              (int) triggerEvents.size());
    if (const int counter = previewCounter.load(std::memory_order_acquire); counter != seenPreviewCounter)
    {
        seenPreviewCounter = counter; // the player asked to hear a sound at the base or the max volume
        engine.previewSample(params.sampleSet, previewPick.load(std::memory_order_relaxed),
                             std::pow(10.0f, previewDb.load(std::memory_order_relaxed) / 20.0f));
    }
    engine.process(mainOut.getArrayOfWritePointers(), numChannels, numSamples, params, triggerEvents.data(), numEvents);

    const auto route = bitvibe::chooseBeatRoute(bitvibe::beatOutputFromIndex(roundToInt(pBeatOutput->load())), monitorConnected);
    beatRoute.store((int) route, std::memory_order_relaxed);
    if (!params.bypass)
    {
        if (route == bitvibe::BeatRoute::Main)
            drumBeat.addAudio(mainOut, numChannels, numSamples, pBeatLevel->load());
        else if (route == bitvibe::BeatRoute::Monitor)
            drumBeat.addAudio(monitor, monitor.getNumChannels(), numSamples, pBeatLevel->load());
    }
    drumBeat.endBlock(numSamples);
}

void BitVibeProcessor::tapTempo()
{
    const double now = Time::getMillisecondCounterHiRes();
    const double interval = now - lastTapMs;
    lastTapMs = now;

    // A pause longer than the slowest tempo (23.4 BPM = 2564 ms) starts a new tap sequence.
    if (interval > 60000.0 / bitvibe::kMinTempoBpm || interval < 60000.0 / bitvibe::kMaxTempoBpm)
    {
        tapCount = 0;
        return;
    }

    tapAverageMs = tapCount == 0 ? interval : 0.5 * (tapAverageMs + interval);
    ++tapCount;

    const float bpm = jlimit(bitvibe::kMinTempoBpm, bitvibe::kMaxTempoKnobBpm, (float) (60000.0 / tapAverageMs));
    if (auto* param = apvts.getParameter(ids::tempo))
        param->setValueNotifyingHost(param->convertTo0to1(bpm));
}

AudioProcessorEditor* BitVibeProcessor::createEditor()
{
    return new BitVibeEditor(*this);
}

// ------------------------------------------------------------------------------------------
// Expression morph sets

void BitVibeProcessor::captureToeUp()
{
    const auto knobs = liveKnobs();
    const SpinLock::ScopedLockType lock(morphLock);
    toeUp = knobs;
}

void BitVibeProcessor::captureToeDown()
{
    const auto knobs = liveKnobs();
    const SpinLock::ScopedLockType lock(morphLock);
    toeDown = knobs;
}

bitvibe::KnobSet BitVibeProcessor::getToeUp() const
{
    const SpinLock::ScopedLockType lock(morphLock);
    return toeUp;
}

bitvibe::KnobSet BitVibeProcessor::getToeDown() const
{
    const SpinLock::ScopedLockType lock(morphLock);
    return toeDown;
}

// ------------------------------------------------------------------------------------------
// Preset slots

int BitVibeProcessor::presetIndex(const String& id) const
{
    for (size_t i = 0; i < presetParams.size(); ++i)
        if (presetParams[i].id == id)
            return (int) i;
    return -1;
}

BitVibeProcessor::PresetSlot BitVibeProcessor::makeFactorySlot(int index) const
{
    const auto f = bitvibe::factoryPreset(index);

    PresetSlot s;
    s.name = f.name;
    s.toeUp = f.toeUp;
    s.toeDown = f.toeDown;
    s.values.reserve(presetParams.size());
    for (const auto& pp : presetParams)
        s.values.push_back(pp.param->getDefaultValue());

    auto set = [&](const String& id, float plain)
    {
        const int i = presetIndex(id);
        jassert(i >= 0);
        if (i >= 0)
            s.values[(size_t) i] = presetParams[(size_t) i].param->convertTo0to1(plain);
    };
    set(ids::sampleRate, f.knobs.sampleRate);
    set(ids::filter, f.knobs.filter);
    set(ids::bits, f.knobs.bits);
    set(ids::stutter, (float) f.knobs.stutter);
    set(ids::seqPlay, (float) f.knobs.seqPlay);
    set(ids::seqMult, (float) f.knobs.seqMult);
    set(ids::seqType, (float) (int) f.knobs.seqType);
    for (int i = 0; i < bitvibe::kNumSteps; ++i)
        set(ids::step(i), f.knobs.steps[(size_t) i]);
    set(ids::tempo, f.tempoBpm);
    set(ids::stutterLen, (float) f.stutterDivision);
    set(ids::morph, f.morphOn ? 1.0f : 0.0f);
    return s;
}

BitVibeProcessor::PresetSlot BitVibeProcessor::snapshotCurrent() const
{
    PresetSlot s;
    s.values.reserve(presetParams.size());
    for (const auto& pp : presetParams)
        s.values.push_back(pp.param->getValue());
    s.toeUp = getToeUp();
    s.toeDown = getToeDown();
    return s;
}

void BitVibeProcessor::applySlot(const PresetSlot& s)
{
    {
        const SpinLock::ScopedLockType lock(morphLock);
        toeUp = s.toeUp;
        toeDown = s.toeDown;
    }
    for (size_t i = 0; i < presetParams.size() && i < s.values.size(); ++i)
        presetParams[i].param->setValueNotifyingHost(s.values[i]);
}

void BitVibeProcessor::resetToFactoryBank()
{
    const ScopedLock lock(presetLock);
    for (int i = 0; i < bitvibe::kNumPresets; ++i)
        slots[(size_t) i] = makeFactorySlot(i);
    currentProgram = 0;
    // The plug-in starts on slot 1, which is the default sound the parameters already have.
    applySlot(slots[0]);
}

void BitVibeProcessor::recallProgram(int index)
{
    PresetSlot slot;
    {
        const ScopedLock lock(presetLock);
        currentProgram = index;
        slot = slots[(size_t) index];
    }
    applySlot(slot);
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
}

void BitVibeProcessor::setCurrentProgram(int index)
{
    index = jlimit(0, bitvibe::kNumPresets - 1, index);

    // Some hosts select the current program again right after restoring the plug-in state. Honouring
    // that would throw away knob changes that the state has just brought back, so it is ignored; the
    // editor's own prev / next buttons always move to a different slot.
    if (index == currentProgram.load())
        return;
    recallProgram(index);
}

const String BitVibeProcessor::getProgramName(int index)
{
    if (index < 0 || index >= bitvibe::kNumPresets)
        return {};
    const ScopedLock lock(presetLock);
    return slots[(size_t) index].name;
}

void BitVibeProcessor::changeProgramName(int index, const String& newName)
{
    if (index < 0 || index >= bitvibe::kNumPresets)
        return;
    {
        const ScopedLock lock(presetLock);
        auto& slot = slots[(size_t) index];
        slot.name = cleanPresetName(newName, slot.name);
    }
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
}

void BitVibeProcessor::saveCurrentPreset()
{
    auto now = snapshotCurrent();
    const ScopedLock lock(presetLock);
    auto& slot = slots[(size_t) currentProgram.load()];
    now.name = slot.name;
    slot = std::move(now);
}

bool BitVibeProcessor::isCurrentPresetModified() const
{
    const auto now = snapshotCurrent();
    const ScopedLock lock(presetLock);
    const auto& slot = slots[(size_t) currentProgram.load()];
    if (!(now.toeUp == slot.toeUp) || !(now.toeDown == slot.toeDown) || now.values.size() != slot.values.size())
        return true;
    for (size_t i = 0; i < now.values.size(); ++i)
        if (std::abs(now.values[i] - slot.values[i]) > 1.0e-4f)
            return true;
    return false;
}

// ------------------------------------------------------------------------------------------
// Preset files
//
//   <BITVIBE_PRESET version="1" name="My sound">
//     <PRESET name=".." sample=".." ...> <UP/> <DOWN/> </PRESET>     the slot, as in the project state
//     <EFFECTS> <PARAM id="reverbMix" value="0.4"/> ... </EFFECTS>     plain values (dB, Hz, %) of the shared effects
//     <PATTERN action="1" steps="4369" randomBeats="31" signature="0"/>
//     <SOUNDS> <FILE name="snare.wav"/> ... </SOUNDS>
//   </BITVIBE_PRESET>

namespace
{
// The parameters outside the preset slots that are part of a shared sound.
bool isSharedEffect(const String& id)
{
    for (const char* prefix : { "reverb", "dist", "sample", "eq" })
        if (id.startsWith(prefix))
            return true;
    return id == ids::mix || id == ids::fxTriggers || id == ids::patternLock;
}
} // namespace

bool BitVibeProcessor::saveCapturedClip(const File& file, String& message)
{
    std::vector<std::vector<float>> clip;
    if (!engine.getEqReference().copyClip(clip) || clip.empty() || clip[0].empty())
    {
        message = "There is no captured clip yet: play something, then press CAPTURE.";
        return false;
    }
    AudioBuffer<float> buffer((int) clip.size(), (int) clip[0].size());
    for (size_t c = 0; c < clip.size(); ++c)
        buffer.copyFrom((int) c, 0, clip[c].data(), (int) clip[c].size());

    file.deleteFile();
    std::unique_ptr<OutputStream> out = std::make_unique<FileOutputStream>(file);
    if (static_cast<FileOutputStream*>(out.get())->failedToOpen())
    {
        message = "BitVibe could not write " + file.getFullPathName() + ".";
        return false;
    }
    auto writer = WavAudioFormat().createWriterFor(out, AudioFormatWriterOptions()
                                                            .withSampleRate(engine.getSampleRate())
                                                            .withNumChannels(buffer.getNumChannels())
                                                            .withBitsPerSample(24));
    if (writer == nullptr || !writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()))
    {
        message = "BitVibe could not write " + file.getFullPathName() + ".";
        return false;
    }
    message = "Saved " + String(buffer.getNumSamples() / engine.getSampleRate(), 1) + " s to " + file.getFullPathName() + ".";
    return true;
}

File BitVibeProcessor::presetFolder()
{
    return File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("BitVibe").getChildFile("Presets");
}

std::unique_ptr<XmlElement> BitVibeProcessor::exportPreset() const
{
    auto slot = snapshotCurrent();
    {
        const ScopedLock lock(presetLock);
        slot.name = slots[(size_t) currentProgram.load()].name;
    }
    auto xml = std::make_unique<XmlElement>("BITVIBE_PRESET");
    xml->setAttribute("version", 1);
    xml->setAttribute("name", slot.name);
    xml->addChildElement(slotToTree(slot).createXml().release());

    auto* effects = xml->createNewChildElement("EFFECTS");
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<RangedAudioParameter*>(p); ranged != nullptr && isSharedEffect(ranged->paramID))
        {
            auto* e = effects->createNewChildElement("PARAM");
            e->setAttribute("id", ranged->paramID);
            e->setAttribute("value", (double) ranged->convertFrom0to1(ranged->getValue()));
        }

    const auto pattern = drumBeat.getPattern();
    auto* pat = xml->createNewChildElement("PATTERN");
    pat->setAttribute("action", (int) pattern.action);
    pat->setAttribute("steps", (int) pattern.steps);
    pat->setAttribute("randomBeats", (int) pattern.randomBeats);
    pat->setAttribute("signature", pattern.signature);

    auto* sounds = xml->createNewChildElement("SOUNDS");
    for (const auto& name : sampleBank.getSelected())
        sounds->createNewChildElement("FILE")->setAttribute("name", name);
    return xml;
}

bool BitVibeProcessor::importPreset(const XmlElement& xml, String& message)
{
    const auto* slotXml = xml.getChildByName("PRESET");
    if (!xml.hasTagName("BITVIBE_PRESET") || slotXml == nullptr)
    {
        message = "This is not a BitVibe preset file.";
        return false;
    }

    // the slot: name and sound, into the selected slot
    const int program = currentProgram.load();
    auto slot = slotFromTree(ValueTree::fromXml(*slotXml), program);
    slot.name = cleanPresetName(xml.getStringAttribute("name", slot.name), slot.name);
    {
        const ScopedLock lock(presetLock);
        slots[(size_t) program] = slot;
    }
    applySlot(slot);

    // the effects: everything shared starts from its default, then takes what the file says (so a file from an older
    // version of BitVibe sounds the way it did there)
    std::map<String, double> values;
    if (const auto* effects = xml.getChildByName("EFFECTS"))
        for (const auto* e : effects->getChildWithTagNameIterator("PARAM"))
            values[e->getStringAttribute("id")] = e->getDoubleAttribute("value", std::numeric_limits<double>::quiet_NaN());
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<RangedAudioParameter*>(p); ranged != nullptr && isSharedEffect(ranged->paramID))
        {
            float normalised = ranged->getDefaultValue();
            if (const auto it = values.find(ranged->paramID); it != values.end() && std::isfinite(it->second))
                normalised = ranged->convertTo0to1((float) jlimit((double) ranged->getNormalisableRange().start,
                                                                  (double) ranged->getNormalisableRange().end, it->second));
            ranged->setValueNotifyingHost(normalised);
        }

    // the beat pattern
    bitvibe::DrumPattern pattern;
    if (const auto* pat = xml.getChildByName("PATTERN"))
    {
        pattern.action = (bitvibe::DrumAction) jlimit(0, bitvibe::kNumDrumActions - 1, pat->getIntAttribute("action", 0));
        pattern.steps = (uint32_t) pat->getIntAttribute("steps", 0) & bitvibe::kPatternMask;
        pattern.randomBeats = (uint32_t) pat->getIntAttribute("randomBeats", 0x1F) & 0x1Fu;
        pattern.signature = jlimit(0, bitvibe::kNumTimeSignatures - 1, pat->getIntAttribute("signature", 0));
    }
    drumBeat.setPattern(pattern);

    // the sounds: those this player has
    int missing = 0;
    if (const auto* sounds = xml.getChildByName("SOUNDS"))
    {
        StringArray available, wanted;
        for (const auto& f : SampleBank::libraryFiles())
            available.add(f.getFileName());
        for (const auto* f : sounds->getChildWithTagNameIterator("FILE"))
        {
            const auto name = f->getStringAttribute("name");
            if (available.contains(name))
                wanted.add(name);
            else if (name.isNotEmpty())
                ++missing;
        }
        sampleBank.setSelected(wanted, false); // the remembered list stays as the player left it
    }

    updateHostDisplay(ChangeDetails().withProgramChanged(true).withParameterInfoChanged(true));
    message = "Loaded \"" + slot.name + "\" into slot " + String(program + 1) + ".";
    if (missing > 0)
        message << " " << missing << (missing == 1 ? " sound it uses is" : " sounds it uses are")
                << " not in your sounds folder, so the sample blend plays without " << (missing == 1 ? "it." : "them.");
    return true;
}

// ------------------------------------------------------------------------------------------
// State
//
//   <BitVibe program="0">
//     <PARAM id="sample" value="1.0"/> ...                  all parameters (the value tree of the APVTS)
//     <MORPH> <UP sample=".." .../> <DOWN .../> </MORPH>     the two expression-morph knob sets
//     <PRESETS> <PRESET name=".." sample=".." ...> <UP/> <DOWN/> </PRESET> x16 </PRESETS>
//   </BitVibe>
// A state saved before the presets existed has neither MORPH nor PRESETS and loads with the factory bank.

ValueTree BitVibeProcessor::slotToTree(const PresetSlot& s) const
{
    ValueTree t("PRESET");
    t.setProperty("name", s.name, nullptr);
    for (size_t i = 0; i < presetParams.size() && i < s.values.size(); ++i)
        t.setProperty(Identifier(presetParams[i].id), (double) s.values[i], nullptr);
    t.appendChild(knobsToTree(s.toeUp, "UP"), nullptr);
    t.appendChild(knobsToTree(s.toeDown, "DOWN"), nullptr);
    return t;
}

BitVibeProcessor::PresetSlot BitVibeProcessor::slotFromTree(const ValueTree& t, int index) const
{
    PresetSlot s = makeFactorySlot(index); // anything the file does not say stays as shipped
    s.name = cleanPresetName(t["name"].toString(), s.name);
    for (size_t i = 0; i < presetParams.size(); ++i)
    {
        const Identifier key(presetParams[i].id);
        if (t.hasProperty(key))
        {
            const double v = (double) t[key];
            if (std::isfinite(v))
                s.values[i] = (float) jlimit(0.0, 1.0, v);
        }
    }
    s.toeUp = knobsFromTree(t.getChildWithName("UP"), s.toeUp);
    s.toeDown = knobsFromTree(t.getChildWithName("DOWN"), s.toeDown);
    return s;
}

void BitVibeProcessor::getStateInformation(MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("program", currentProgram.load(), nullptr);

    ValueTree morphNode("MORPH");
    morphNode.appendChild(knobsToTree(getToeUp(), "UP"), nullptr);
    morphNode.appendChild(knobsToTree(getToeDown(), "DOWN"), nullptr);
    state.appendChild(morphNode, nullptr);

    ValueTree bank("PRESETS");
    {
        const ScopedLock lock(presetLock);
        for (const auto& slot : slots)
            bank.appendChild(slotToTree(slot), nullptr);
    }
    state.appendChild(bank, nullptr);
    state.appendChild(drumBeat.toState(), nullptr);
    state.appendChild(sampleBank.toState(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void BitVibeProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType()))
        return;

    auto state = ValueTree::fromXml(*xml);
    const auto morphNode = state.getChildWithName("MORPH");
    const auto bank = state.getChildWithName("PRESETS");
    const auto drums = state.getChildWithName("DRUMS");
    const auto samples = state.getChildWithName("SAMPLES");
    const int program = jlimit(0, bitvibe::kNumPresets - 1, (int) state.getProperty("program", 0));
    state.removeChild(morphNode, nullptr); // keep the parameter tree free of the extra nodes
    state.removeChild(bank, nullptr);
    state.removeChild(drums, nullptr);
    state.removeChild(samples, nullptr);
    state.removeProperty("program", nullptr);
    apvts.replaceState(state);

    {
        const SpinLock::ScopedLockType lock(morphLock);
        toeUp = knobsFromTree(morphNode.getChildWithName("UP"), bitvibe::KnobSet {});
        toeDown = knobsFromTree(morphNode.getChildWithName("DOWN"), bitvibe::KnobSet {});
    }
    drumBeat.fromState(drums); // a state from before the drum map resets it
    sampleBank.fromState(samples); // a project from before the sample blend keeps the current list

    const ScopedLock lock(presetLock);
    for (int i = 0; i < bitvibe::kNumPresets; ++i)
    {
        const auto node = bank.isValid() && i < bank.getNumChildren() ? bank.getChild(i) : ValueTree();
        slots[(size_t) i] = node.isValid() ? slotFromTree(node, i) : makeFactorySlot(i);
    }
    currentProgram = program;
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BitVibeProcessor();
}
