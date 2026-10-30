// Loads the built VST3 the way a DAW would (through JUCE's plugin host), pushes audio through it,
// renders the editor to a PNG and writes a few demo WAV files.
//   BitVibeSmokeTest <path to .vst3> <output directory>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace juce;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 512;
int failures = 0;

float gPeak = 0;

void check(bool ok, const String& what)
{
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what.toRawUTF8());
    failures += ok ? 0 : 1;
}

// Two plucked-string notes (Karplus-Strong), mono, a few seconds long.
AudioBuffer<float> makePluck(double seconds)
{
    AudioBuffer<float> b(2, (int) (seconds * kFs));
    b.clear();
    Random rng(5);
    auto pluck = [&](double startSec, double hz, float amp)
    {
        const int period = (int) (kFs / hz);
        std::vector<float> line((size_t) period);
        for (auto& v : line)
            v = rng.nextFloat() * 2.0f - 1.0f;
        size_t idx = 0;
        for (int i = (int) (startSec * kFs); i < b.getNumSamples(); ++i)
        {
            const float cur = line[idx];
            const float next = line[(idx + 1) % line.size()];
            line[idx] = 0.4985f * (cur + next);
            b.setSample(0, i, cur * amp);
            b.setSample(1, i, cur * amp);
            idx = (idx + 1) % line.size();
        }
    };
    pluck(0.10, 196.0, 0.5f);
    pluck(1.50, 261.6, 0.5f);
    return b;
}

bool near(float a, float b, float tol = 2.0e-3f) { return std::abs(a - b) <= tol; }

AudioProcessorParameter* findParam(AudioProcessor& p, const String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName(64) == name)
            return prm;
    return nullptr;
}

void setByText(AudioProcessor& p, const String& name, const String& text)
{
    if (auto* prm = findParam(p, name))
        prm->setValueNotifyingHost(prm->getValueForText(text));
    else
        check(false, "parameter missing: " + name);
}

void setNormalised(AudioProcessor& p, const String& name, float v)
{
    if (auto* prm = findParam(p, name))
        prm->setValueNotifyingHost(v);
    else
        check(false, "parameter missing: " + name);
}

AudioBuffer<float> render(AudioProcessor& p, const AudioBuffer<float>& in)
{
    AudioBuffer<float> out(in);
    MidiBuffer midi;
    for (int pos = 0; pos < in.getNumSamples(); pos += kBlock)
    {
        const int n = jmin(kBlock, in.getNumSamples() - pos);
        AudioBuffer<float> block(out.getArrayOfWritePointers(), 2, pos, n);
        p.processBlock(block, midi);
    }
    return out;
}

bool allFinite(const AudioBuffer<float>& b, float& peak)
{
    peak = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = b.getSample(c, i);
            if (! std::isfinite(v))
                return false;
            peak = jmax(peak, std::abs(v));
        }
    return true;
}

void writeWav(const AudioBuffer<float>& b, const File& f)
{
    f.deleteFile();
    WavAudioFormat wav;
    if (auto stream = std::unique_ptr<FileOutputStream>(f.createOutputStream()))
        if (auto writer = std::unique_ptr<AudioFormatWriter>(wav.createWriterFor(stream.get(), kFs, 2, 24, {}, 0)))
        {
            stream.release();
            writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
        }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::puts("usage: BitVibeSmokeTest <plugin.vst3> <output dir>");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    ScopedJuceInitialiser_GUI juceInit;

    const File pluginFile(argv[1]);
    const File outDir(argv[2]);
    outDir.createDirectory();

    VST3PluginFormat format;
    OwnedArray<PluginDescription> found;
    format.findAllTypesForFile(found, pluginFile.getFullPathName());
    check(found.size() == 1, "VST3 scan found " + String(found.size()) + " plugin(s) in the bundle");
    if (found.isEmpty())
        return 1;

    const auto& desc = *found[0];
    std::printf("  name=%s  manufacturer=%s  category=%s  inputs=%d outputs=%d\n", desc.name.toRawUTF8(),
                desc.manufacturerName.toRawUTF8(), desc.category.toRawUTF8(), desc.numInputChannels, desc.numOutputChannels);

    String error;
    auto plugin = format.createInstanceFromDescription(desc, kFs, kBlock, error);
    check(plugin != nullptr, "instantiated through the VST3 host" + (error.isEmpty() ? String() : " (" + error + ")"));
    if (plugin == nullptr)
        return 1;

    plugin->setPlayConfigDetails(2, 2, kFs, kBlock);
    plugin->prepareToPlay(kFs, kBlock);
    check(plugin->getTotalNumInputChannels() == 2 && plugin->getTotalNumOutputChannels() == 2, "stereo in / stereo out");
    // 31 plug-in parameters, plus the "Program" selector the VST3 wrapper adds for the 16 preset slots.
    check(plugin->getParameters().size() == 40 && findParam(*plugin, "Program") != nullptr
              && findParam(*plugin, "Mix") != nullptr && findParam(*plugin, "FX Only On Triggers") != nullptr
              && findParam(*plugin, "Beat Play") != nullptr && findParam(*plugin, "Beat Level") != nullptr
              && findParam(*plugin, "Pick Triggers") != nullptr && findParam(*plugin, "Step Clock") != nullptr
              && findParam(*plugin, "Beat Output") != nullptr,
          "exposes " + String(plugin->getParameters().size()) + " parameters (39 + Program selector, incl. the drum-map ones and Beat Output)");
    for (auto* p : plugin->getParameters())
        std::printf("    - %-22s default=%-12s\n", p->getName(64).toRawUTF8(), p->getText(p->getDefaultValue(), 32).toRawUTF8());

    float peak = 0;

    // The optional "Beat Monitor" output: a second stereo output the host can switch on, so that the drum beat
    // reaches the player's headphones without going into the main output and everything after it.
    {
        auto* monitor = plugin->getBus(false, 1);
        check(plugin->getBusCount(false) == 2 && monitor != nullptr && monitor->getName() == "Beat Monitor" && !monitor->isEnabled(),
              "the VST3 has a second output, 'Beat Monitor', that starts switched off");
        // VST3 only allows bus arrangements to change while the plug-in is deactivated, so release it first, the way
        // a host does, then change the arrangement and prepare it again.
        plugin->releaseResources();
        const bool enableResult = monitor != nullptr && monitor->enable(true);
        const bool on = enableResult && monitor->isEnabled();
        check(on && plugin->getTotalNumOutputChannels() == 4,
              "the host can switch the Beat Monitor output on (stereo) (enable() " + String((int) enableResult) + ", enabled "
                  + String((int) (monitor != nullptr && monitor->isEnabled())) + ", " + String(plugin->getTotalNumOutputChannels())
                  + " output channels, " + String(plugin->getBusesLayout().outputBuses.size()) + " output buses)");
        if (on)
        {
            plugin->prepareToPlay(kFs, kBlock);
            bool finite = true, monitorSilent = true;
            MidiBuffer midi;
            for (int pass = 0; pass < 4; ++pass)
            {
                AudioBuffer<float> block(4, kBlock);
                for (int i = 0; i < kBlock; ++i)
                    for (int c = 0; c < 4; ++c)
                        block.setSample(c, i, c < 2 ? 0.3f * std::sin(0.05f * (float) (i + pass * kBlock)) : 0.9f); // junk in the monitor channels
                plugin->processBlock(block, midi);
                for (int c = 0; c < 4; ++c)
                    for (int i = 0; i < kBlock; ++i)
                    {
                        finite = finite && std::isfinite(block.getSample(c, i));
                        if (c >= 2)
                            monitorSilent = monitorSilent && block.getSample(c, i) == 0.0f;
                    }
            }
            check(finite, "four-channel blocks (main + Beat Monitor) are finite");
            check(monitorSilent, "with no beat playing the Beat Monitor output is silent (the junk it arrived with is cleared)");
            plugin->releaseResources();
            check(monitor->enable(false) && plugin->getTotalNumOutputChannels() == 2, "the host can switch it off again");
        }
        plugin->prepareToPlay(kFs, kBlock);
    }

    const auto pluck = makePluck(4.0);

    // 1. Factory sound (pitch sequencer, continuous)
    auto factory = render(*plugin, pluck);
    const bool fin_factory = allFinite(factory, peak);
    check(fin_factory && peak > 0.05f && peak < 2.0f, "factory sound is finite, peak " + String(peak, 2));
    writeWav(pluck, outDir.getChildFile("0_dry_pluck.wav"));
    writeWav(factory, outDir.getChildFile("1_factory_pitch_sequence.wav"));

    // 2. Bitcrush + sample-rate + filter, sequencer off
    plugin->reset();
    setByText(*plugin, "Sequencer Play", "Off");
    setNormalised(*plugin, "Sample Rate", 0.55f);
    setNormalised(*plugin, "Bits", 0.22f);
    setNormalised(*plugin, "Filter", 0.62f);
    auto crushed = render(*plugin, pluck);
    const bool fin_crushed = allFinite(crushed, peak);
    check(fin_crushed && peak > 0.02f, "bitcrush + filter is finite, peak " + String(peak, 2));
    writeWav(crushed, outDir.getChildFile("2_crush_filter.wav"));

    // 3. Stutter: full speed, x4, nothing else
    plugin->reset();
    setNormalised(*plugin, "Sample Rate", 1.0f);
    setNormalised(*plugin, "Bits", 1.0f);
    setNormalised(*plugin, "Filter", 1.0f);
    setNormalised(*plugin, "Stutter", 4.0f / 22.0f);
    if (auto* t = findParam(*plugin, "Tempo"))
        t->setValueNotifyingHost(t->getValueForText("140 BPM"));
    auto stutter = render(*plugin, pluck);
    const bool fin_stutter = allFinite(stutter, peak);
    check(fin_stutter && peak > 0.05f, "stutter is finite, peak " + String(peak, 2));
    writeWav(stutter, outDir.getChildFile("3_stutter_x4.wav"));

    // 4. Filter sequencer + sample-rate crush at audio-ish speed
    plugin->reset();
    setNormalised(*plugin, "Stutter", 0.0f);
    setByText(*plugin, "Sequencer Type", "Filter");
    setByText(*plugin, "Sequencer Play", "Continuous");
    setByText(*plugin, "Sequencer Mult", "x4");
    setNormalised(*plugin, "Filter", 0.8f);
    setNormalised(*plugin, "Step 1", 0.93f);
    setNormalised(*plugin, "Step 2", 0.15f);
    setNormalised(*plugin, "Step 3", 0.45f);
    setNormalised(*plugin, "Step 4", 0.80f);
    setNormalised(*plugin, "Step 5", 0.25f);
    setNormalised(*plugin, "Step 6", 0.60f);
    auto filterSeq = render(*plugin, pluck);
    const bool fin_filterSeq = allFinite(filterSeq, peak);
    check(fin_filterSeq && peak > 0.02f, "filter sequence is finite, peak " + String(peak, 2));
    writeWav(filterSeq, outDir.getChildFile("4_filter_sequence.wav"));

    // 5. Bypass passes the input through unchanged
    plugin->reset();
    setByText(*plugin, "Bypass", "On");
    auto byp = render(*plugin, pluck);
    double maxDiff = 0;
    for (int i = 0; i < pluck.getNumSamples(); ++i)
        maxDiff = jmax(maxDiff, (double) std::abs(byp.getSample(0, i) - pluck.getSample(0, i)));
    check(maxDiff < 1.0e-4, "bypass is transparent (max diff " + String(maxDiff, 6) + ")");
    setByText(*plugin, "Bypass", "Off");

    // 6. State round trip
    MemoryBlock state;
    plugin->getStateInformation(state);
    check(state.getSize() > 100, "state saved (" + String((int) state.getSize()) + " bytes)");
    setNormalised(*plugin, "Bits", 0.3f);
    plugin->setStateInformation(state.getData(), (int) state.getSize());
    check(std::abs(findParam(*plugin, "Bits")->getValue() - 1.0f) < 1.0e-3f, "state restores parameters");

    // 7. Odd block sizes (never larger than the size given to prepareToPlay, as a host would do)
    {
        bool ok = true;
        for (int size : { 1, 7, 64, 333, 511 })
        {
            AudioBuffer<float> blk(2, size);
            for (int i = 0; i < size; ++i)
                for (int c = 0; c < 2; ++c)
                    blk.setSample(c, i, 0.3f * std::sin(0.05f * (float) i));
            MidiBuffer midi;
            plugin->processBlock(blk, midi);
            ok = ok && allFinite(blk, peak);
        }
        check(ok, "odd-sized blocks (1, 7, 64, 333, 511) are finite");
    }

    // 8. Preset slots: recall through the host's program list, global settings left alone, state round trip
    {
        auto value = [&](const char* name)
        {
            auto* prm = findParam(*plugin, name);
            return prm != nullptr ? prm->getValue() : -1.0f;
        };
        auto text = [&](const char* name)
        {
            auto* prm = findParam(*plugin, name);
            return prm != nullptr ? prm->getText(prm->getValue(), 32) : String("?");
        };
        auto maxDifference = [](const AudioBuffer<float>& a, const AudioBuffer<float>& b)
        {
            double d = 0;
            for (int c = 0; c < a.getNumChannels(); ++c)
                for (int i = 0; i < a.getNumSamples(); ++i)
                    d = jmax(d, (double) std::abs(a.getSample(c, i) - b.getSample(c, i)));
            return d;
        };

        check(plugin->getNumPrograms() == 16, "exposes " + String(plugin->getNumPrograms()) + " preset slots");
        StringArray names;
        for (int i = 0; i < plugin->getNumPrograms(); ++i)
            names.add(plugin->getProgramName(i));
        StringArray unique(names);
        unique.removeDuplicates(false);
        check(names.size() == 16 && unique.size() == 16 && !names.contains(""), "preset names are set and unique");
        check(names[0] == "Pitch Sequence", "slot 1 is '" + names[0] + "'");
        std::printf("    presets: %s\n", names.joinIntoString(" | ").toRawUTF8());
        check(plugin->getCurrentProgram() == 0, "starts on slot 1");

        // Recall slot 3 on top of edited settings: the sound comes from the slot, the rig settings stay.
        setNormalised(*plugin, "Bits", 0.3f);
        setNormalised(*plugin, "Input Gain", 0.75f);
        plugin->setCurrentProgram(2);
        check(plugin->getCurrentProgram() == 2, "slot 3 selected");
        check(near(value("Sample Rate"), 0.62f) && near(value("Filter"), 0.55f) && near(value("Bits"), 0.45f),
              "slot 3 restores its Sample / Filter / Bits knobs");
        check(text("Sequencer Play") == "Off", "slot 3 turns the sequencer off (" + text("Sequencer Play") + ")");
        check(near(value("Input Gain"), 0.75f), "recalling a preset leaves the input gain alone");
        setNormalised(*plugin, "Input Gain", 0.5f); // back to 0 dB

        // Slot 1 brings the factory sound back exactly: same knobs and the same audio as at start-up.
        plugin->setCurrentProgram(0);
        check(text("Sequencer Play") == "Continuous" && text("Sequencer Type") == "Pitch", "slot 1 runs the pitch sequencer");
        bool stepsOk = near(value("Bits"), 1.0f) && near(value("Filter"), 1.0f);
        const float expectedSteps[6] = { 6.5f / 27, 10.5f / 27, 1.5f / 27, 20.5f / 27, 17.5f / 27, 13.5f / 27 };
        for (int i = 0; i < 6; ++i)
            stepsOk = stepsOk && near(value(("Step " + String(i + 1)).toRawUTF8()), expectedSteps[(size_t) i]);
        check(stepsOk, "slot 1 restores the factory steps (-7, -3, -12, +7, +4, 0 semitones)");
        plugin->reset();
        const auto again = render(*plugin, pluck);
        const double diff = maxDifference(again, factory);
        check(diff < 1.0e-3, "slot 1 sounds like the factory sound (max diff " + String(diff, 6) + ")");

        // Mix and "FX Only On Triggers" belong to the rig, not to a slot. A host does not necessarily re-initialise the
        // plug-in when prepareToPlay is called again, so before each comparison 0.6 s of silence is played through it:
        // that lets every parameter smoother settle (the slowest takes ~0.12 s) and cannot trigger anything.
        auto fresh = [&]
        {
            AudioBuffer<float> silence(2, (int) (0.6 * kFs));
            silence.clear();
            render(*plugin, silence);
        };
        check(text("Mix") == "100% effect" && text("FX Only On Triggers") == "Off",
              "mix starts at 100% effect with the trigger-only option off (" + text("Mix") + ")");
        setNormalised(*plugin, "Mix", 0.4f);
        setNormalised(*plugin, "FX Only On Triggers", 1.0f);
        plugin->setCurrentProgram(2); // Lo-Fi Radio: crush + filter, sequencer and stutter off
        check(near(value("Mix"), 0.4f) && text("FX Only On Triggers") == "On",
              "recalling a preset leaves Mix and the trigger-only option alone");

        setNormalised(*plugin, "Mix", 1.0f);
        fresh();
        const auto idleOut = render(*plugin, pluck);
        check(maxDifference(idleOut, pluck) < 1.0e-6,
              "trigger-only option on, nothing triggering: the signal stays clean (diff " + String(maxDifference(idleOut, pluck), 8) + ")");

        setNormalised(*plugin, "FX Only On Triggers", 0.0f);
        fresh();
        const auto lofi = render(*plugin, pluck);
        check(maxDifference(lofi, pluck) > 0.02, "trigger-only option off: the Lo-Fi Radio sound is applied");

        setNormalised(*plugin, "Mix", 0.0f);
        fresh();
        const double mixZero = maxDifference(render(*plugin, pluck), pluck);
        check(mixZero < 1.0e-6, "Mix at 0% returns the clean input (diff " + String(mixZero, 8) + ")");

        setNormalised(*plugin, "Mix", 0.5f);
        fresh();
        const auto half = render(*plugin, pluck);
        check(maxDifference(half, pluck) > 0.005 && maxDifference(half, lofi) > 0.005,
              "Mix at 50% differs from both the clean input and the full effect");
        // The sample-rate hold keeps its timing from one render to the next (the host does not reset the engine), so
        // the crushed signal is not identical sample for sample between renders. Compare how far each is from the
        // clean input instead: at 50% the effect should contribute about half of its full deviation. (The exact
        // sample-for-sample blend is checked on the processor itself in BitVibeEditorSnapshot.)
        auto rmsFromClean = [&](const AudioBuffer<float>& b)
        {
            double acc = 0;
            for (int c = 0; c < b.getNumChannels(); ++c)
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const double d = (double) b.getSample(c, i) - (double) pluck.getSample(c, i);
                    acc += d * d;
                }
            return std::sqrt(acc / (double) (b.getNumChannels() * b.getNumSamples()));
        };
        const double ratio = rmsFromClean(half) / jmax(1.0e-12, rmsFromClean(lofi));
        check(ratio > 0.4 && ratio < 0.6, "Mix at 50% adds about half of the effect's deviation (ratio " + String(ratio, 3) + ")");

        setNormalised(*plugin, "Mix", 1.0f);
        plugin->setCurrentProgram(0);
        fresh();

        // A slot with a stored expression-morph set-up: clean at toe up, crushed + stutter at toe down.
        plugin->setCurrentProgram(10);
        check(text("Expression Morph") == "On", "slot 11 switches the morph on");
        plugin->reset();
        const auto toeUp = render(*plugin, pluck);
        check(maxDifference(toeUp, pluck) < 1.0e-3, "expression at toe up leaves the signal clean");
        setNormalised(*plugin, "Expression", 1.0f);
        plugin->reset();
        const auto toeDown = render(*plugin, pluck);
        float toeDownPeak = 0;
        check(allFinite(toeDown, toeDownPeak) && maxDifference(toeDown, pluck) > 0.05,
              "expression at toe down plays the stored crushed set (diff " + String(maxDifference(toeDown, pluck), 3) + ")");
        writeWav(toeDown, outDir.getChildFile("5_morph_toe_down.wav"));

        // Halfway: continuous knobs glide, so the result is neither end point.
        setNormalised(*plugin, "Expression", 0.8f);
        plugin->reset();
        const auto morphed = render(*plugin, pluck);
        check(maxDifference(morphed, toeUp) > 0.01 && maxDifference(morphed, toeDown) > 0.01,
              "expression at 80% differs from both end points");

        // State round trip: the stored sets, the slot list, the current slot and the expression survive.
        MemoryBlock saved;
        plugin->getStateInformation(saved);
        plugin->setCurrentProgram(0); // wipes the live sound and the stored sets
        setNormalised(*plugin, "Expression", 0.0f);
        check(text("Expression Morph") == "Off", "slot 1 switches the morph off again");
        plugin->setStateInformation(saved.getData(), (int) saved.getSize());
        check(plugin->getCurrentProgram() == 10, "state restores the selected slot (" + String(plugin->getCurrentProgram()) + ")");
        check(text("Expression Morph") == "On" && near(value("Expression"), 0.8f), "state restores the morph switch and expression");
        plugin->reset();
        const auto restored = render(*plugin, pluck);
        const double stateDiff = maxDifference(restored, morphed);
        check(stateDiff < 1.0e-4, "restored state sounds identical, stored sets included (max diff " + String(stateDiff, 6) + ")");
        check(plugin->getProgramName(10) == names[10] && plugin->getProgramName(15) == names[15], "slot names survive the round trip");

        // Back to slot 1 and the shipped state for the remaining checks.
        plugin->setCurrentProgram(0);
        setByText(*plugin, "Expression Morph", "Off");
        plugin->reset();
    }

    // 9. Drum map parameters through the host (loading and analysing beats is checked by BitVibeEditorSnapshot,
    //    which drives the processor directly).
    {
        auto maxDiff = [](const AudioBuffer<float>& a, const AudioBuffer<float>& b)
        {
            double d = 0;
            for (int c = 0; c < a.getNumChannels(); ++c)
                for (int i = 0; i < a.getNumSamples(); ++i)
                    d = jmax(d, (double) std::abs(a.getSample(c, i) - b.getSample(c, i)));
            return d;
        };
        auto text = [&](const char* name)
        {
            auto* prm = findParam(*plugin, name);
            return prm != nullptr ? prm->getText(prm->getValue(), 32) : String("?");
        };

        // A sequencer that only runs when something triggers it, and nothing else on.
        plugin->setCurrentProgram(1); // "Clean"
        setByText(*plugin, "Sequencer Play", "Once");
        plugin->reset();
        const auto picked = render(*plugin, pluck);
        check(maxDiff(picked, pluck) > 0.01, "Pick Triggers on: the pluck starts the sequence (diff " + String(maxDiff(picked, pluck), 3) + ")");
        setByText(*plugin, "Pick Triggers", "Off");
        plugin->reset();
        const auto unpicked = render(*plugin, pluck);
        check(maxDiff(unpicked, pluck) < 1.0e-4, "Pick Triggers off: nothing triggers, the signal is clean (diff "
                                                      + String(maxDiff(unpicked, pluck), 6) + ")");

        // Beat Play with no beat loaded changes nothing.
        setByText(*plugin, "Pick Triggers", "On");
        setByText(*plugin, "Beat Play", "On");
        setNormalised(*plugin, "Beat Level", 1.0f);
        plugin->reset();
        check(maxDiff(render(*plugin, pluck), picked) < 1.0e-6, "Beat Play with no beat loaded changes nothing");
        setNormalised(*plugin, "Beat Level", 0.0f);
        check(text("Beat Level") == "Off", "Beat Level fully down reads 'Off' (" + text("Beat Level") + ")");
        setByText(*plugin, "Step Clock", "Drum hits");
        check(text("Step Clock") == "Drum hits", "Step Clock can follow the drum hits");

        // The drum map state round-trips through the host (settings stay at their defaults here).
        MemoryBlock drumState;
        plugin->getStateInformation(drumState);
        setByText(*plugin, "Step Clock", "Tempo");
        setByText(*plugin, "Beat Play", "Off");
        plugin->setStateInformation(drumState.getData(), (int) drumState.getSize());
        check(text("Step Clock") == "Drum hits" && text("Beat Play") == "On", "drum map parameters survive the host state round trip");

        setByText(*plugin, "Step Clock", "Tempo");
        setByText(*plugin, "Beat Play", "Off");
        setNormalised(*plugin, "Beat Level", findParam(*plugin, "Beat Level")->getDefaultValue());
        plugin->setCurrentProgram(0);
        plugin->reset();
    }

    // 10. (optional) Editor through the VST3 view
    const bool wantEditor = argc > 3 && String(argv[3]) == "--editor";
    plugin->reset();
    std::unique_ptr<AudioProcessorEditor> editor(wantEditor ? plugin->createEditorIfNeeded() : nullptr);
    if (wantEditor)
        check(editor != nullptr, "editor created");
    if (editor != nullptr)
    {
        auto snap = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        File png = outDir.getChildFile("editor.png");
        png.deleteFile();
        PNGImageFormat fmt;
        if (auto out = std::unique_ptr<FileOutputStream>(png.createOutputStream()))
            fmt.writeImageToStream(snap, *out);
        check(png.getSize() > 5000, "editor rendered to " + png.getFullPathName() + " (" + String(editor->getWidth()) + "x"
                                        + String(editor->getHeight()) + ")");

        // alt layer too
        editor->setSize(editor->getWidth(), editor->getHeight());
    }
    editor.reset();
    plugin->releaseResources();

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL OK" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
