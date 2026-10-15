// Loads the built VST3 the way a DAW would (through JUCE's plugin host), pushes audio through it,
// renders the editor to a PNG and writes a few demo WAV files.
//   OttobitSmokeTest <path to .vst3> <output directory>

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
        std::puts("usage: OttobitSmokeTest <plugin.vst3> <output dir>");
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
    check(plugin->getParameters().size() == 22, "exposes" + String(plugin->getParameters().size()) + " parameters");
    for (auto* p : plugin->getParameters())
        std::printf("    - %-22s default=%-12s\n", p->getName(64).toRawUTF8(), p->getText(p->getDefaultValue(), 32).toRawUTF8());

    const auto pluck = makePluck(4.0);
    float peak = 0;

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

    // 8. (optional) Editor through the VST3 view
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
