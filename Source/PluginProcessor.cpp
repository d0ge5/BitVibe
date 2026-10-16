#include "PluginProcessor.h"
#include "PluginEditor.h"

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

AudioProcessorValueTreeState::ParameterLayout OttobitProcessor::createLayout()
{
    using namespace ottobit;
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

    // ---- tempo, switches ---------------------------------------------------------------
    NormalisableRange<float> tempoRange(kMinTempoBpm, kMaxTempoBpm, 0.1f);
    tempoRange.setSkewForCentre(140.0f);
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
    return layout;
}

OttobitProcessor::OttobitProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", AudioChannelSet::stereo(), true)
                         .withOutput("Output", AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Ottoclone", createLayout())
{
    auto raw = [this](const char* id) { return apvts.getRawParameterValue(id); };
    pSample = raw(ids::sampleRate);
    pFilter = raw(ids::filter);
    pBits = raw(ids::bits);
    pStutter = raw(ids::stutter);
    pSeqPlay = raw(ids::seqPlay);
    pSeqMult = raw(ids::seqMult);
    pSeqType = raw(ids::seqType);
    pTempo = raw(ids::tempo);
    pTempoSync = raw(ids::tempoSync);
    pBypass = raw(ids::bypass);
    pHold = raw(ids::hold);
    pInGain = raw(ids::inputGain);
    pOutGain = raw(ids::outputGain);
    pHeadroom = raw(ids::headroom);
    pSensitivity = raw(ids::sensitivity);
    pStutterLen = raw(ids::stutterLen);
    for (int i = 0; i < ottobit::kNumSteps; ++i)
        pSteps[(size_t) i] = raw(ids::step(i).toRawUTF8());

    bypassParam = apvts.getParameter(ids::bypass);
}

void OttobitProcessor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate);
}

bool OttobitProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (in == AudioChannelSet::mono() || in == AudioChannelSet::stereo());
}

ottobit::Params OttobitProcessor::readParams()
{
    ottobit::Params p;
    p.sampleRate = pSample->load();
    p.filter = pFilter->load();
    p.bits = pBits->load();
    p.stutter = roundToInt(pStutter->load());
    p.seqPlay = roundToInt(pSeqPlay->load());
    p.seqMult = roundToInt(pSeqMult->load());
    p.seqType = (ottobit::SeqType) roundToInt(pSeqType->load());
    for (int i = 0; i < ottobit::kNumSteps; ++i)
        p.steps[(size_t) i] = pSteps[(size_t) i]->load();

    p.tempoBpm = pTempo->load();
    if (pTempoSync->load() > 0.5f)
        if (auto* head = getPlayHead())
            if (auto pos = head->getPosition())
                if (auto bpm = pos->getBpm())
                    p.tempoBpm = (float) *bpm;
    p.tempoBpm = jlimit(ottobit::kMinTempoBpm, ottobit::kMaxTempoBpm, p.tempoBpm);
    effectiveBpm.store(p.tempoBpm, std::memory_order_relaxed);

    p.bypass = pBypass->load() > 0.5f;
    p.stutterHold = pHold->load() > 0.5f;
    p.inputGainDb = pInGain->load();
    p.outputGainDb = pOutGain->load();
    p.lineHeadroom = roundToInt(pHeadroom->load()) == 1;
    p.triggerSensitivity = pSensitivity->load();
    p.stutterDivision = roundToInt(pStutterLen->load());
    return p;
}

void OttobitProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int numChannels = std::min(buffer.getNumChannels(), ottobit::Engine::kMaxChannels);
    for (int c = numChannels; c < buffer.getNumChannels(); ++c)
        buffer.clear(c, 0, buffer.getNumSamples());

    engine.process(buffer.getArrayOfWritePointers(), numChannels, buffer.getNumSamples(), readParams());
}

void OttobitProcessor::tapTempo()
{
    const double now = Time::getMillisecondCounterHiRes();
    const double interval = now - lastTapMs;
    lastTapMs = now;

    // A pause longer than the slowest tempo (23.4 BPM = 2564 ms) starts a new tap sequence.
    if (interval > 60000.0 / ottobit::kMinTempoBpm || interval < 60000.0 / ottobit::kMaxTempoBpm)
    {
        tapCount = 0;
        return;
    }

    tapAverageMs = tapCount == 0 ? interval : 0.5 * (tapAverageMs + interval);
    ++tapCount;

    const float bpm = jlimit(ottobit::kMinTempoBpm, ottobit::kMaxTempoBpm, (float) (60000.0 / tapAverageMs));
    if (auto* param = apvts.getParameter(ids::tempo))
        param->setValueNotifyingHost(param->convertTo0to1(bpm));
}

AudioProcessorEditor* OttobitProcessor::createEditor()
{
    return new OttobitEditor(*this);
}

void OttobitProcessor::getStateInformation(MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void OttobitProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(ValueTree::fromXml(*xml));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OttobitProcessor();
}
