// Instantiates the processor and editor directly (no plugin host), renders the GUI to PNG files so the
// look can be checked without opening a DAW, and runs the processor-level checks that a host cannot
// reach (capturing the expression-morph sets, renaming and saving presets, state round trips).
//   BitVibeEditorSnapshot <output directory> [--load <audio file>]
// --load also loads that file into the DRUMS tab the way the plug-in would (neural helper included when it
// is installed) and prints what was found.
// Exit code 0 means every check passed.

#include "PluginEditor.h"
#include "EqPanel.h"
#include "SamplePanel.h"
#include "UiSettings.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

using namespace juce;

namespace
{
int failures = 0;

void check(bool ok, const String& what)
{
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what.toRawUTF8());
    failures += ok ? 0 : 1;
}

void setValue(BitVibeProcessor& p, const char* id, float plain)
{
    if (auto* prm = p.apvts.getParameter(id))
        prm->setValueNotifyingHost(prm->convertTo0to1(plain));
}

float getValue(BitVibeProcessor& p, const char* id)
{
    auto* prm = p.apvts.getParameter(id);
    return prm->convertFrom0to1(prm->getValue());
}

// Renders a component into a software image. Component::createComponentSnapshot uses the platform's native image type
// (Direct2D on Windows), and a graphics-driver hang there once left this tool stuck in the driver for good, so the tests
// keep to the CPU.
Image snapshot(Component& c, float scale)
{
    Image img(Image::ARGB, jmax(1, roundToInt((float) c.getWidth() * scale)), jmax(1, roundToInt((float) c.getHeight() * scale)), true,
              SoftwareImageType());
    Graphics g(img);
    g.addTransform(AffineTransform::scale(scale));
    c.paintEntireComponent(g, true);
    return img;
}

void save(BitVibeEditor& ed, const File& file)
{
    auto img = snapshot(ed, 1.5f);
    file.deleteFile();
    PNGImageFormat png;
    if (auto out = std::unique_ptr<FileOutputStream>(file.createOutputStream()))
        png.writeImageToStream(img, *out);
    std::printf("wrote %s (%d bytes)\n", file.getFullPathName().toRawUTF8(), (int) file.getSize());
}

std::unique_ptr<BitVibeProcessor> makeProcessor()
{
    auto p = std::make_unique<BitVibeProcessor>();
    p->setPlayConfigDetails(2, 2, 48000.0, 512);
    p->prepareToPlay(48000.0, 512);
    return p;
}

// Every parameter's normalised value, in parameter order.
std::vector<float> allValues(BitVibeProcessor& p)
{
    std::vector<float> v;
    for (auto* prm : p.getParameters())
        v.push_back(prm->getValue());
    return v;
}

// Two plucked-string-like bursts after 100 ms of silence, deterministic, mono copied to stereo. The
// silence matters: the engine's pitch-crossfade smoother is not cleared by a reset, so a signal that
// starts at sample 0 can differ from run to run for the first few milliseconds.
AudioBuffer<float> makeInput(int numSamples)
{
    AudioBuffer<float> b(2, numSamples);
    Random rng(7);
    for (int i = 0; i < numSamples; ++i)
    {
        const int t = i - 4800;
        const float env = t < 0 ? 0.0f : std::exp(-(float) (t % 24000) / 6000.0f);
        const float s = env * (0.45f * std::sin(0.031f * (float) i) + 0.1f * (rng.nextFloat() * 2.0f - 1.0f));
        b.setSample(0, i, s);
        b.setSample(1, i, s);
    }
    return b;
}

AudioBuffer<float> run(BitVibeProcessor& p, const AudioBuffer<float>& in)
{
    p.prepareToPlay(48000.0, 512); // resets the engine
    AudioBuffer<float> out(in);
    MidiBuffer midi;
    for (int pos = 0; pos < in.getNumSamples(); pos += 512)
    {
        AudioBuffer<float> block(out.getArrayOfWritePointers(), 2, pos, jmin(512, in.getNumSamples() - pos));
        p.processBlock(block, midi);
    }
    return out;
}

bool isFinite(const AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (!std::isfinite(b.getSample(c, i)))
                return false;
    return true;
}

double maxDifference(const AudioBuffer<float>& a, const AudioBuffer<float>& b)
{
    double d = 0;
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = 0; i < a.getNumSamples(); ++i)
            d = jmax(d, (double) std::abs(a.getSample(c, i) - b.getSample(c, i)));
    return d;
}

bitvibe::KnobSet exampleSet(float amount, int stutter, bitvibe::SeqType type)
{
    bitvibe::KnobSet k;
    k.sampleRate = 0.3f + 0.4f * amount;
    k.filter = 0.9f - 0.5f * amount;
    k.bits = 0.2f + 0.7f * amount;
    k.stutter = stutter;
    k.seqPlay = 5;
    k.seqMult = 2;
    k.steps = { 0.2f * amount + 0.1f, 0.5f, 0.9f - 0.3f * amount, 0.35f, 0.65f, 0.5f };
    k.seqType = type;
    return k;
}

void applyKnobParams(BitVibeProcessor& p, const bitvibe::KnobSet& k)
{
    setValue(p, ids::sampleRate, k.sampleRate);
    setValue(p, ids::filter, k.filter);
    setValue(p, ids::bits, k.bits);
    setValue(p, ids::stutter, (float) k.stutter);
    setValue(p, ids::seqPlay, (float) k.seqPlay);
    setValue(p, ids::seqMult, (float) k.seqMult);
    setValue(p, ids::seqType, (float) (int) k.seqType);
    for (int i = 0; i < bitvibe::kNumSteps; ++i)
        setValue(p, ids::step(i).toRawUTF8(), k.steps[(size_t) i]);
}

void testPresetsAndMorph()
{
    std::puts("processor checks");
    const auto input = makeInput(48000);

    // ---- slots ----------------------------------------------------------------------------
    auto p = makeProcessor();
    check(p->getNumPrograms() == 16 && p->getCurrentProgram() == 0, "16 slots, starts on slot 1");
    check(p->getProgramName(0) == "Pitch Sequence" && p->getProgramName(15) == "Init 16" && p->getProgramName(16).isEmpty(),
          "slot names (first '" + p->getProgramName(0) + "', last '" + p->getProgramName(15) + "')");
    check(!p->isCurrentPresetModified(), "a fresh plug-in matches slot 1");

    const auto factoryValues = allValues(*p);
    const auto factoryAudio = run(*p, input);

    // Edits mark the slot modified; recalling another slot and coming back restores slot 1 exactly.
    setValue(*p, ids::bits, 0.4f);
    check(p->isCurrentPresetModified(), "editing a knob marks the slot modified");
    p->setCurrentProgram(2);
    check(p->getCurrentProgram() == 2 && !p->isCurrentPresetModified(), "recalling slot 3 selects it, unmodified");
    check(std::abs(getValue(*p, ids::bits) - 0.45f) < 1.0e-4f, "slot 3 has its own Bits value");
    p->setCurrentProgram(0);
    check(allValues(*p) == factoryValues, "slot 1 restores every parameter exactly");
    const double recallDiff = maxDifference(run(*p, input), factoryAudio);
    check(recallDiff < 1.0e-6, "slot 1 sounds like the start-up sound (max diff " + String(recallDiff, 8) + ")");
    check(maxDifference(run(*makeProcessor(), input), factoryAudio) < 1.0e-6, "a second fresh plug-in renders the same audio");

    // Re-selecting the current slot (hosts do this after loading state) keeps unsaved edits.
    setValue(*p, ids::filter, 0.35f);
    p->setCurrentProgram(0);
    check(std::abs(getValue(*p, ids::filter) - 0.35f) < 1.0e-4f, "re-selecting the current slot keeps unsaved edits");

    // Rig settings and switches are not part of a slot.
    setValue(*p, ids::inputGain, 7.0f);
    setValue(*p, ids::mix, 0.4f);
    setValue(*p, ids::fxTriggers, 1.0f);
    setValue(*p, ids::bypass, 1.0f);
    setValue(*p, ids::tempoSync, 1.0f);
    p->setCurrentProgram(4);
    check(std::abs(getValue(*p, ids::inputGain) - 7.0f) < 0.11f && getValue(*p, ids::bypass) > 0.5f &&
              getValue(*p, ids::tempoSync) > 0.5f,
          "recall leaves input gain, bypass and host sync alone");
    check(std::abs(getValue(*p, ids::mix) - 0.4f) < 0.01f && getValue(*p, ids::fxTriggers) > 0.5f,
          "recall leaves the mix and the trigger-only option alone");
    setValue(*p, ids::inputGain, 0.0f);
    setValue(*p, ids::mix, 1.0f);
    setValue(*p, ids::fxTriggers, 0.0f);
    setValue(*p, ids::bypass, 0.0f);
    setValue(*p, ids::tempoSync, 0.0f);

    // Rename and save: a stored slot survives other recalls; names are cleaned up.
    p->setCurrentProgram(7);
    p->changeProgramName(7, "   My Filter Thing   ");
    setValue(*p, ids::filter, 0.123f);
    check(p->isCurrentPresetModified(), "changed knob marks slot 8 modified");
    p->saveCurrentPreset();
    check(!p->isCurrentPresetModified() && p->getProgramName(7) == "My Filter Thing", "SAVE stores the live sound, name is trimmed");
    p->changeProgramName(7, String());
    check(p->getProgramName(7) == "My Filter Thing", "an empty name is ignored");
    p->changeProgramName(7, String::repeatedString("x", 80));
    check(p->getProgramName(7).length() == 32, "names are limited to 32 characters");
    p->changeProgramName(7, "My Filter Thing");
    p->setCurrentProgram(1);
    p->setCurrentProgram(7);
    check(std::abs(getValue(*p, ids::filter) - 0.123f) < 1.0e-4f, "a saved slot comes back with the stored value");

    // ---- expression morph -----------------------------------------------------------------------
    p = makeProcessor();
    const auto up = exampleSet(0.0f, 0, bitvibe::SeqType::Filter);
    const auto down = exampleSet(1.0f, 6, bitvibe::SeqType::SampleRate);

    applyKnobParams(*p, up);
    p->captureToeUp();
    applyKnobParams(*p, down);
    p->captureToeDown();
    check(p->getToeUp() == up && p->getToeDown() == down, "SET UP / SET DOWN capture the current knobs");
    check(p->isCurrentPresetModified(), "capturing marks the slot modified");

    // With the morph on, the sound is exactly what the knobs would give when set to the blended values.
    const auto liveRun = [&](const bitvibe::KnobSet& k)
    {
        auto q = makeProcessor();
        setValue(*q, ids::morph, 0.0f);
        applyKnobParams(*q, k);
        return run(*q, input);
    };
    setValue(*p, ids::morph, 1.0f);
    applyKnobParams(*p, exampleSet(0.5f, 3, bitvibe::SeqType::Pitch)); // knobs are ignored while morphing
    for (float expression : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        setValue(*p, ids::expression, expression);
        const auto morphed = run(*p, input);
        const auto reference = liveRun(bitvibe::morph::blend(up, down, expression));
        check(maxDifference(morphed, reference) < 1.0e-6,
              "expression " + String(expression, 2) + " sounds like the blended knob values");
    }
    setValue(*p, ids::morph, 0.0f);
    check(maxDifference(run(*p, input), liveRun(exampleSet(0.5f, 3, bitvibe::SeqType::Pitch))) < 1.0e-6,
          "with the morph off, the knobs are heard again");
    setValue(*p, ids::morph, 1.0f);
    setValue(*p, ids::expression, 0.7f);

    // ---- state round trip ------------------------------------------------------------------------
    // Slot 6 gets its own sets, name and tempo (recalling a slot replaces the sets, so capture afterwards).
    p->setCurrentProgram(5);
    applyKnobParams(*p, up);
    p->captureToeUp();
    applyKnobParams(*p, down);
    p->captureToeDown();
    setValue(*p, ids::morph, 1.0f);
    setValue(*p, ids::expression, 0.7f);
    p->changeProgramName(5, "Round Trip");
    setValue(*p, ids::tempo, 187.5f);
    p->saveCurrentPreset();
    MemoryBlock state;
    p->getStateInformation(state);

    auto q = makeProcessor();
    q->setStateInformation(state.getData(), (int) state.getSize());
    check(q->getToeUp() == up && q->getToeDown() == down, "the two morph sets survive the state round trip");
    check(q->getCurrentProgram() == 5 && q->getProgramName(5) == "Round Trip", "current slot and slot names survive");
    check(allValues(*q) == allValues(*p), "every parameter survives");
    check(!q->isCurrentPresetModified(), "a restored plug-in is not marked modified");
    bool slotsMatch = true;
    for (int i = 0; i < 16; ++i)
    {
        p->setCurrentProgram(i);
        q->setCurrentProgram(i);
        slotsMatch = slotsMatch && allValues(*p) == allValues(*q) && p->getToeUp() == q->getToeUp() &&
                     p->getToeDown() == q->getToeDown() && p->getProgramName(i) == q->getProgramName(i);
    }
    check(slotsMatch, "all 16 slots recall identically after the round trip");

    // The sets stored with a slot come back with the slot.
    q = makeProcessor();
    q->setStateInformation(state.getData(), (int) state.getSize());
    q->setCurrentProgram(10);
    check(q->getToeUp() != up && q->getToeUp() == bitvibe::factoryPreset(10).toeUp,
          "recalling a slot replaces the morph sets with the slot's own");
    q->setCurrentProgram(5);
    check(q->getToeUp() == up && q->getToeDown() == down, "...and recalling slot 6 brings back the sets saved with it");

    // ---- old and damaged state -----------------------------------------------------------------------
    {
        // A state from before presets and morph existed: only the parameter tree.
        auto old = makeProcessor();
        setValue(*old, ids::bits, 0.2f);
        MemoryBlock oldState;
        if (auto xml = old->apvts.copyState().createXml())
            AudioProcessor::copyXmlToBinary(*xml, oldState);

        auto fresh = makeProcessor();
        fresh->setStateInformation(oldState.getData(), (int) oldState.getSize());
        check(std::abs(getValue(*fresh, ids::bits) - 0.2f) < 1.0e-4f, "an old state still restores its parameters");
        check(fresh->getProgramName(3) == bitvibe::factoryPreset(3).name && fresh->getToeUp() == bitvibe::KnobSet {},
              "...with the factory bank and default morph sets");

        // Out-of-range numbers and a short bank must be repaired, not trusted.
        auto xml = AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize());
        check(xml != nullptr, "state is readable XML");
        if (xml != nullptr)
        {
            if (auto* bank = xml->getChildByName("PRESETS"))
            {
                while (bank->getNumChildElements() > 3)
                    bank->removeChildElement(bank->getChildElement(bank->getNumChildElements() - 1), true);
                if (auto* first = bank->getFirstChildElement())
                {
                    first->setAttribute(ids::sampleRate, 7.5);
                    first->setAttribute(ids::stutter, 400);
                    first->setAttribute("name", String::repeatedString("n", 200));
                    if (auto* up = first->getChildByName("UP"))
                        up->setAttribute(ids::seqMult, -9);
                }
            }
            xml->setAttribute("program", 99);
            MemoryBlock damaged;
            AudioProcessor::copyXmlToBinary(*xml, damaged);
            auto d = makeProcessor();
            d->setStateInformation(damaged.getData(), (int) damaged.getSize());
            check(d->getCurrentProgram() >= 0 && d->getCurrentProgram() < 16, "an out-of-range program number is clamped");
            check(d->getProgramName(10) == bitvibe::factoryPreset(10).name, "slots missing from the state keep their factory sound");
            d->setCurrentProgram(1);
            d->setCurrentProgram(0);
            check(std::abs(getValue(*d, ids::sampleRate) - 1.0f) < 1.0e-6f && getValue(*d, ids::stutter) <= 22.0f &&
                      d->getProgramName(0).length() == 32 && d->getToeUp().seqMult == 0,
                  "out-of-range values in a slot are clamped");
            check(isFinite(run(*d, input)), "the repaired plug-in still renders audio");
        }
    }
}

// The reverb parameters reach the engine: reverb only on the sequenced sound, never on the clean signal.
void testReverbParams()
{
    std::puts("reverb (processor level)");
    const auto in = makeInput(48000);
    auto p = makeProcessor();
    setValue(*p, ids::seqPlay, 5.0f); // continuous sequencer: everything is sequenced
    setValue(*p, ids::tempo, 120.0f);

    const auto plain = run(*p, in);
    setValue(*p, ids::reverbMix, 1.0f);
    const auto reverbed = run(*p, in);
    check(maxDifference(reverbed, plain) > 0.01, "reverb mix up: the sequenced sound gets reverb (max diff " + String(maxDifference(reverbed, plain), 4) + ")");

    setValue(*p, ids::reverbType, 1.0f);
    const auto hall = run(*p, in);
    setValue(*p, ids::reverbType, 2.0f);
    const auto spring = run(*p, in);
    check(maxDifference(hall, reverbed) > 0.01f && maxDifference(spring, hall) > 0.01f && isFinite(spring), "room, hall and spring sound different");
    {
        // the newer types are all reachable and sound different from each other
        std::vector<AudioBuffer<float>> byType;
        for (int type = 3; type <= 5; ++type)
        {
            setValue(*p, ids::reverbType, (float) type);
            byType.push_back(run(*p, in));
            check(isFinite(byType.back()) && maxDifference(byType.back(), plain) > 0.005f, "reverb type " + String(type) + " (plate, shimmer, gated) adds a reverb");
        }
        check(maxDifference(byType[0], byType[1]) > 0.005f && maxDifference(byType[1], byType[2]) > 0.005f, "plate, shimmer and gated sound different");
        setValue(*p, ids::reverbReversePlate, 1.0f);
        setValue(*p, ids::reverbType, 3.0f);
        check(maxDifference(run(*p, in), byType[0]) > 0.005f, "the plate has its own REVERSE tick");
        setValue(*p, ids::reverbReversePlate, 0.0f);
    }
    setValue(*p, ids::reverbType, 0.0f);

    // Decay and reverse: each type of reverb has its own REVERSE tick.
    setValue(*p, ids::reverbDecay, 1.0f);
    const auto longTail = run(*p, in);
    check(maxDifference(longTail, reverbed) > 0.005f, "reverb decay changes the tail");
    setValue(*p, ids::reverbDecay, 0.5f);
    setValue(*p, ids::reverbReverseRoom, 1.0f);
    const auto roomReversed = run(*p, in);
    check(maxDifference(roomReversed, reverbed) > 0.01f && isFinite(roomReversed), "ticking REVERSE for Room turns the room reverb around");
    setValue(*p, ids::reverbType, 1.0f);
    check(maxDifference(run(*p, in), hall) < 1.0e-6f, "the Hall reverb is not affected by the Room tick");
    setValue(*p, ids::reverbType, 0.0f);
    setValue(*p, ids::reverbReverseRoom, 0.0f);

    setValue(*p, ids::reverbLowCut, 2000.0f);
    setValue(*p, ids::reverbHighCut, 200.0f);
    const auto cut = run(*p, in);
    check(maxDifference(cut, plain) < maxDifference(reverbed, plain) * 0.5f, "low cut and high cut thin the reverb right out");
    setValue(*p, ids::reverbLowCut, 20.0f);
    setValue(*p, ids::reverbHighCut, 20000.0f);

    setValue(*p, ids::reverbLevel, -30.0f);
    const auto quiet = run(*p, in);
    check(maxDifference(quiet, plain) < maxDifference(reverbed, plain) * 0.2f, "reverb level -30 dB: almost nothing is added");

    setValue(*p, ids::reverbLevel, 0.0f);
    setValue(*p, ids::mix, 0.0f);
    const auto clean = run(*p, in);
    check(maxDifference(clean, in) < 1.0e-5f, "dry/wet mix 0: the clean signal comes back without reverb");

    setValue(*p, ids::mix, 1.0f);
    setValue(*p, ids::seqPlay, 0.0f);
    const auto idle = run(*p, in);
    check(maxDifference(idle, in) < 1.0e-5f, "no sequence or stutter running: the reverb adds nothing");
}

// The sample blend through the processor: the picked sounds reach the engine, mix and chance work, the list is saved.
void testSampleBlendParams()
{
    std::puts("sample blend (processor level)");
    const auto library = SampleBank::libraryFiles();
    if (library.isEmpty())
    {
        std::puts("  (no sounds in any sounds folder: these checks are skipped)");
        return;
    }
    const auto in = makeInput(48000);
    auto p = makeProcessor();
    setValue(*p, ids::seqPlay, 5.0f); // a looping sequence: steps keep starting
    setValue(*p, ids::tempo, 240.0f);
    p->getSampleBank().selectAll();
    check(p->getSampleBank().getNumSelected() == library.size(), "every sound in the folders is picked and decoded (" + String(p->getSampleBank().getNumSelected()) + ")");

    const auto plain = run(*p, in);
    setValue(*p, ids::sampleMix, 1.0f);
    const auto blended = run(*p, in);
    check(maxDifference(blended, plain) > 0.05f && isFinite(blended), "sample mix up: the sequence gets sounds (max diff " + String(maxDifference(blended, plain), 3) + ")");

    p->getSampleBank().selectNone();
    check(maxDifference(run(*p, in), plain) < 1.0e-6f, "no sounds picked: nothing is added");

    p->getSampleBank().setSelected({ library[0].getFileName() });
    check(p->getSampleBank().getNumSelected() == 1 && maxDifference(run(*p, in), plain) > 0.05f, "a list of one sound plays that sound");

    setValue(*p, ids::sampleChance, 0.0f);
    check(maxDifference(run(*p, in), plain) < 1.0e-6f, "chance 0: no step gets a sound");
    setValue(*p, ids::sampleChance, 1.0f);

    // The volume range: MIX moves between the base and the max volume.
    setValue(*p, ids::sampleBase, -20.0f);
    setValue(*p, ids::sampleMax, -6.0f);
    setValue(*p, ids::sampleMix, 0.0f);
    const auto atBase = run(*p, in);
    setValue(*p, ids::sampleMix, 1.0f);
    const auto atMax = run(*p, in);
    const float baseSize = maxDifference(atBase, plain), maxSize = maxDifference(atMax, plain);
    check(baseSize > 0.001f && maxSize > baseSize * 3.0f, "MIX 0 plays at the base volume and MIX 1 at the max volume (" + String(baseSize, 3) + " then " + String(maxSize, 3) + ")");

    // The preview: a picked sound at a volume, with no sequencer running.
    {
        auto q = makeProcessor();
        q->getSampleBank().selectAll();
        q->prepareToPlay(48000.0, 512);
        AudioBuffer<float> buffer(2, 512);
        MidiBuffer midi;
        buffer.clear();
        q->processBlock(buffer, midi);
        check(buffer.getMagnitude(0, 512) == 0.0f, "nothing plays before a preview is asked for");
        q->requestSamplePreview(-12.0f, 3);
        buffer.clear();
        q->processBlock(buffer, midi);
        float loudest = 0.0f;
        for (int block = 0; block < 200; ++block) // about two seconds: some sounds start quietly
        {
            loudest = jmax(loudest, buffer.getMagnitude(0, 512));
            buffer.clear();
            q->processBlock(buffer, midi);
        }
        check(loudest > 0.005f && loudest < 0.5f, "a preview plays a picked sound (" + String(loudest, 3) + ")");
    }

    // The picked list is saved with the project; a project without one picks everything.
    p->getSampleBank().setSelected({ library[0].getFileName(), library[library.size() - 1].getFileName() });
    MemoryBlock state;
    p->getStateInformation(state);
    auto q = makeProcessor();
    q->setStateInformation(state.getData(), (int) state.getSize());
    check(q->getSampleBank().getSelected() == p->getSampleBank().getSelected(), "the picked list survives a state round trip");
    MemoryBlock oldState;
    if (auto xml = makeProcessor()->apvts.copyState().createXml())
        AudioProcessor::copyXmlToBinary(*xml, oldState);
    q->getSampleBank().setSelected({ library[0].getFileName() }, false);
    q->setStateInformation(oldState.getData(), (int) oldState.getSize());
    check(q->getSampleBank().getSelected() == StringArray { library[0].getFileName() }, "a project from before the sample blend keeps the current list");
}

// The distortion parameters reach the engine.
void testDistortionParams()
{
    std::puts("distortion (processor level)");
    const auto in = makeInput(48000);
    auto p = makeProcessor();
    setValue(*p, ids::seqPlay, 5.0f);
    setValue(*p, ids::tempo, 120.0f);
    const auto plain = run(*p, in);
    setValue(*p, ids::distDrive, 1.0f);
    check(maxDifference(run(*p, in), plain) < 1.0e-6f, "distortion switched off: nothing changes");
    setValue(*p, ids::distOn, 1.0f);
    const auto driven = run(*p, in);
    check(maxDifference(driven, plain) > 0.02f && isFinite(driven), "distortion on: the sequenced sound changes (max diff " + String(maxDifference(driven, plain), 3) + ")");
    setValue(*p, ids::mix, 0.0f);
    check(maxDifference(run(*p, in), in) < 1.0e-5f, "dry/wet mix 0 still gives the clean signal");
}

// Sound banks and the remembered sample settings (the test settings file lives in a temp folder).
void testSoundBanks()
{
    std::puts("sound banks and remembered sample settings");
    const auto library = SampleBank::libraryFiles();
    if (library.size() < 3)
    {
        std::puts("  (fewer than three sounds in the sounds folders: these checks are skipped)");
        return;
    }
    SampleBank::settingsFile().deleteFile();

    {
        auto p = makeProcessor();
        auto& bank = p->getSampleBank();
        check(bank.getNumSelected() == library.size() && bank.getBankNames().isEmpty(), "a first run picks every sound and has no banks");

        const StringArray lowList { library[0].getFileName(), library[1].getFileName() };
        bank.setSelected(lowList);
        check(bank.saveBank("  Low  sounds "), "a bank can be saved");
        check(bank.getBankNames() == StringArray { "Low sounds" } && bank.getActiveBank() == "Low sounds" && bank.selectionMatchesActiveBank(),
              "its name is tidied up and it becomes the showing bank");

        bank.setSelected({ library[2].getFileName() });
        check(!bank.selectionMatchesActiveBank(), "changing the list afterwards means it is no longer the bank");
        check(bank.saveBank("Single"), "a second bank");
        check(bank.loadBank("low SOUNDS") && bank.getSelected() == lowList && bank.getNumSelected() == 2, "loading a bank brings its list back (name case ignored)");
        check(!bank.loadBank("nope") && bank.getSelected() == lowList, "an unknown bank changes nothing");
        check(bank.saveBank("Single") && bank.getBankNames().size() == 2 && bank.getBankFiles("Single") == lowList,
              "saving under an existing name replaces that bank");
        check(!bank.saveBank("   ") && !bank.saveBank("%%%"), "an empty name is refused");

        // the knobs, as the panel records them when the player lets go
        setValue(*p, ids::sampleMix, 0.35f);
        setValue(*p, ids::sampleChance, 0.6f);
        SampleBank::Levels levels;
        levels.mix = 0.35f;
        levels.chance = 0.6f;
        levels.baseDb = -30.0f;
        levels.maxDb = -12.0f;
        setValue(*p, ids::sampleBase, -30.0f);
        setValue(*p, ids::sampleMax, -12.0f);
        bank.rememberLevels(levels);
    }

    {
        // a new instance (a new project, a restart of the host): the list, the banks and the knobs come back
        auto p = makeProcessor();
        auto& bank = p->getSampleBank();
        check(bank.getSelected().size() == 2 && bank.getBankNames().size() == 2 && bank.getActiveBank() == "Single",
              "a new instance starts with the remembered list and banks");
        check(std::abs(getValue(*p, ids::sampleMix) - 0.35f) < 0.01f && std::abs(getValue(*p, ids::sampleChance) - 0.6f) < 0.01f &&
                  std::abs(getValue(*p, ids::sampleBase) + 30.0f) < 0.6f && std::abs(getValue(*p, ids::sampleMax) + 12.0f) < 0.6f,
              "and with the sample MIX, CHANCE, BASE and MAX volumes the player left");

        // a saved project brings its own list and does not disturb what is remembered
        bank.setSelected({ library[2].getFileName() });
        MemoryBlock project;
        p->getStateInformation(project);
        bank.setSelected({ library[0].getFileName(), library[1].getFileName() });
        auto other = makeProcessor();
        other->setStateInformation(project.getData(), (int) project.getSize());
        check(other->getSampleBank().getSelected() == StringArray { library[2].getFileName() }, "a project brings back its own list");
        auto third = makeProcessor();
        check(third->getSampleBank().getSelected().size() == 2, "and loading it left the remembered list alone");

        bank.deleteBank("Single");
        check(bank.getBankNames() == StringArray { "Low sounds" } && bank.getActiveBank().isEmpty(), "a bank can be deleted");
    }

    {
        auto p = makeProcessor();
        check(p->getSampleBank().getBankNames() == StringArray { "Low sounds" }, "the deletion was remembered too");
    }

    // A damaged settings file is ignored rather than crashing, and the first-run defaults come back.
    SampleBank::settingsFile().replaceWithText("<not xml");
    {
        auto p = makeProcessor();
        check(p->getSampleBank().getNumSelected() == library.size() && p->getSampleBank().getBankNames().isEmpty(), "a damaged settings file falls back to the defaults");
    }
}

// The window can be resized: the content is scaled to the window, the shape stays, and the size is remembered.
void testResizing()
{
    std::puts("resizable window");
    auto p = makeProcessor();
    std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(p->createEditor()));
    const int w = ed->getWidth(), h = ed->getHeight();
    check(w == 948 && h == 940, "the editor opens at its designed size (" + String(w) + " x " + String(h) + ")");
    check(ed->isResizable(), "the editor can be resized");

    ed->setSize(w * 3 / 2, h * 3 / 2);
    const auto scaleNow = ed->getContent().getTransform().getScaleFactor();
    check(std::abs(scaleNow - 1.5f) < 0.01f && ed->getContent().getWidth() == w, "at 150% the pane is drawn 1.5 times bigger (scale " + String(scaleNow, 3) + ")");
    auto big = snapshot(*ed, 1.0f);
    check(big.getWidth() == w * 3 / 2 && big.getHeight() == h * 3 / 2, "and it renders at the new size");
    // the content really fills the bigger window: the bottom right of the pedal's global panel is drawn, not empty
    check(big.getPixelAt(big.getWidth() / 4, big.getHeight() - 40).getBrightness() > 0.04f, "the scaled content reaches the bottom of the window");

    ed->setSize(w / 2, h / 2);
    check(std::abs(ed->getContent().getTransform().getScaleFactor() - 0.5f) < 0.01f, "and 50% draws it half the size");

    // the remembered size
    uisettings::saveWindowScale(1.25f);
    check(std::abs(uisettings::loadWindowScale() - 1.25f) < 1.0e-4f, "the window size is remembered");
    std::unique_ptr<BitVibeEditor> again(static_cast<BitVibeEditor*>(p->createEditor()));
    check(std::abs((float) again->getWidth() / (float) w - 1.25f) < 0.01f, "and a new editor opens at it");
    uisettings::saveWindowScale(5.0f);
    check(uisettings::loadWindowScale() <= uisettings::kMaxScale, "a remembered size is kept inside the limits");
    SampleBank::settingsFolder().getChildFile("ui_settings.xml").deleteFile();

    // The quick-start help opens by itself the first time only.
    {
        std::unique_ptr<BitVibeEditor> first(static_cast<BitVibeEditor*>(p->createEditor()));
        bool shown = false;
        for (auto* c : first->getContent().getChildren())
            shown = shown || (dynamic_cast<HelpOverlay*>(c) != nullptr && c->isVisible());
        std::unique_ptr<BitVibeEditor> second(static_cast<BitVibeEditor*>(p->createEditor()));
        bool shownAgain = false;
        for (auto* c : second->getContent().getChildren())
            shownAgain = shownAgain || (dynamic_cast<HelpOverlay*>(c) != nullptr && c->isVisible());
        check(shown && !shownAgain, "the quick-start help opens by itself the first time, and not after that");
        save(*first, File::getSpecialLocation(File::tempDirectory).getChildFile("bitvibe_help.png"));
    }
    uisettings::markHelpSeen();
}

// SIMPLE and ADVANCED on the right-hand side: ADVANCED shows more controls, SIMPLE hides them (their settings still
// apply), the choice is remembered, and both are drawn for the README.
void testRightModes(const File& outDir)
{
    std::puts("simple and advanced controls on the right-hand side");
    auto p = makeProcessor();
    auto countRight = [](BitVibeEditor& ed)
    {
        int n = 0;
        for (auto* c : ed.getContent().getChildren())
            if (c->isVisible() && c->getX() >= 480 && c->getY() > 40
                && (dynamic_cast<Slider*>(c) != nullptr || dynamic_cast<Button*>(c) != nullptr || dynamic_cast<ComboBox*>(c) != nullptr))
                ++n;
        return n;
    };
    uisettings::setAdvancedRight(false);
    std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(p->createEditor()));
    const int simple = countRight(*ed);
    check(!ed->isAdvancedRight(), "a new player starts in SIMPLE");
    save(*ed, outDir.getChildFile("editor_simple.png"));
    ed->setRightMode(true);
    const int advanced = countRight(*ed);
    check(advanced == simple + 8, "ADVANCED shows the eight extra distortion, delay and reverb controls (" + String(simple) + " then " + String(advanced) + ")");
    save(*ed, outDir.getChildFile("editor_advanced.png"));
    std::unique_ptr<BitVibeEditor> again(static_cast<BitVibeEditor*>(p->createEditor()));
    check(again->isAdvancedRight(), "the choice is remembered");
    again->setRightMode(false);
    uisettings::setAdvancedRight(true); // the other screenshots show every control
}

// The EQ through the processor: off by default, then the bands reach the engine.
void testEqParams()
{
    std::puts("eq (processor level)");
    const auto in = makeInput(48000);
    auto p = makeProcessor();
    setValue(*p, ids::seqPlay, 5.0f);
    setValue(*p, ids::tempo, 120.0f);
    const auto plain = run(*p, in);
    setValue(*p, ids::eqBand(3, "Gain").toRawUTF8(), 12.0f);
    check(maxDifference(run(*p, in), plain) < 1.0e-6f, "the EQ does nothing until it is switched on");
    setValue(*p, ids::eqOn, 1.0f);
    const auto boosted = run(*p, in);
    check(maxDifference(boosted, plain) > 0.01f && isFinite(boosted), "switched on, a +12 dB band changes the sound (max diff " + String(maxDifference(boosted, plain), 3) + ")");
    setValue(*p, ids::mix, 0.0f);
    check(maxDifference(run(*p, in), in) < 1.0e-5f, "the clean signal is untouched");
}

// The loop section is part of a preset: it is saved into a slot, comes back with it, changes the sound and travels
// in a preset file.
void testSequenceLoopParams()
{
    std::puts("sequence loop (processor level)");
    const auto in = makeInput(48000);
    auto p = makeProcessor();
    p->setCurrentProgram(0);
    setValue(*p, ids::seqPlay, 1.0f); // once
    setValue(*p, ids::seqMult, 3.0f);
    const auto plain = run(*p, in);
    check(getValue(*p, ids::loopOn) < 0.5f, "the loop starts off");
    setValue(*p, ids::loopOn, 1.0f);
    setValue(*p, ids::loopFrom, 3.0f);
    setValue(*p, ids::loopTo, 5.0f);
    setValue(*p, ids::loopRepeats, 0.0f);
    const auto looped = run(*p, in);
    check(maxDifference(looped, plain) > 0.01f && isFinite(looped), "a forever loop keeps the sequence going (max diff " + String(maxDifference(looped, plain), 3) + ")");
    p->saveCurrentPreset();
    p->setCurrentProgram(1);
    check(getValue(*p, ids::loopOn) < 0.5f, "another factory slot has no loop");
    p->setCurrentProgram(0);
    check(getValue(*p, ids::loopOn) > 0.5f && std::abs(getValue(*p, ids::loopFrom) - 3.0f) < 0.01f && std::abs(getValue(*p, ids::loopTo) - 5.0f) < 0.01f,
          "the loop comes back with its slot");
    check(maxDifference(run(*p, in), looped) < 1.0e-5f, "and sounds the same");
    const auto text = p->exportPreset()->toString();
    check(text.contains("loopOn") && text.contains("loopRepeats"), "a preset file holds the loop");
}

// The EQ's reference clip through the processor: captured after playing, kept when the host prepares again, played
// through the EQ, and saved as a WAV file that reads back with the right length and rate.
void testEqReferenceClip()
{
    std::puts("eq reference clip (processor level)");
    auto p = makeProcessor();
    setValue(*p, ids::seqPlay, 5.0f);
    run(*p, makeInput(48000));
    auto& ref = p->getEngine().getEqReference();
    MidiBuffer midi;
    auto process = [&](int samples)
    {
        AudioBuffer<float> buffer(2, samples);
        buffer.clear();
        for (int pos = 0; pos < samples; pos += 512)
        {
            AudioBuffer<float> block(buffer.getArrayOfWritePointers(), 2, pos, jmin(512, samples - pos));
            p->processBlock(block, midi);
        }
        return buffer;
    };
    ref.requestCapture();
    process(512);
    const int n = ref.getClipSamples();
    check(n > 4800 && n <= 8 * 48000, "CAPTURE keeps what was just played (" + String(n) + " samples)");
    p->prepareToPlay(48000.0, 512);
    check(ref.getClipSamples() == n, "the host preparing again keeps the clip");

    ref.setAudition(true);
    const auto heard = process(48000);
    check(heard.getMagnitude(0, 48000) > 0.01f, "PLAY CLIP plays it with nothing coming in (peak " + String(heard.getMagnitude(0, 48000), 3) + ")");
    ref.setAudition(false);
    check(process(4800).getMagnitude(0, 4800) < 1.0e-3f, "stopped: silence in, silence out");

    const auto file = File::getSpecialLocation(File::tempDirectory).getChildFile("bitvibe_clip_test.wav");
    String message;
    const bool saved = p->saveCapturedClip(file, message);
    check(saved, "SAVE CLIP writes a WAV: " + message);
    AudioFormatManager formats;
    formats.registerBasicFormats();
    if (auto reader = std::unique_ptr<AudioFormatReader>(formats.createReaderFor(file)))
        check(reader->lengthInSamples == n && reader->numChannels == 2 && reader->sampleRate == 48000.0 && reader->bitsPerSample == 24,
              "and it reads back: " + String(reader->lengthInSamples) + " samples, " + String(reader->numChannels) + " channels, 24 bit");
    else
        check(false, "the saved clip could not be read back");
    file.deleteFile();
    {
        // the EQ panel's spectrum: a 1 kHz tone shows up as a peak behind the curve
        auto q = makeProcessor();
        setValue(*q, ids::seqPlay, 0.0f);
        setValue(*q, ids::eqOn, 1.0f);
        q->prepareToPlay(48000.0, 512);
        PedalLookAndFeel lookAndFeel;
        EqPanel quiet(*q, lookAndFeel);
        quiet.refresh();
        const auto silent = snapshot(quiet, 2.0f);
        AudioBuffer<float> tone(2, 8192);
        for (int i = 0; i < tone.getNumSamples(); ++i)
            tone.setSample(0, i, 0.25f * std::sin(MathConstants<float>::twoPi * 1000.0f * (float) i / 48000.0f));
        tone.copyFrom(1, 0, tone, 0, 0, tone.getNumSamples());
        MidiBuffer none;
        for (int pos = 0; pos < tone.getNumSamples(); pos += 512)
        {
            AudioBuffer<float> block(tone.getArrayOfWritePointers(), 2, pos, 512);
            q->processBlock(block, none);
        }
        EqPanel panel(*q, lookAndFeel);
        panel.refresh();
        const auto shown = snapshot(panel, 2.0f);
        int changed = 0;
        for (int y = 0; y < shown.getHeight(); ++y)
            for (int x = 0; x < shown.getWidth(); ++x)
                changed += shown.getPixelAt(x, y) != silent.getPixelAt(x, y) ? 1 : 0;
        check(changed > 200, "the EQ panel draws the spectrum of what reaches the EQ (" + String(changed) + " pixels change with a tone)");
        const auto spectrumFile = File::getSpecialLocation(File::tempDirectory).getChildFile("bitvibe_eq_spectrum.png");
        spectrumFile.deleteFile();
        if (auto out = std::unique_ptr<FileOutputStream>(spectrumFile.createOutputStream()))
            PNGImageFormat().writeImageToStream(shown, *out);
    }
    auto fresh = makeProcessor();
    check(!fresh->saveCapturedClip(file, message) && message.isNotEmpty() && !file.exists(), "with no clip there is nothing to save");
}

// The loop section's sounds through the processor: a list of its own (the main one until then), remembered, saved with
// the project and in preset files; SAME AS MAIN goes back to the main list; its own levels start off.
void testLoopSectionSounds()
{
    std::puts("loop section sounds (processor level)");
    const auto library = SampleBank::libraryFiles();
    if (library.size() < 2)
    {
        std::puts("  (skipped: fewer than two sounds in the library)");
        return;
    }
    auto p = makeProcessor();
    auto& bank = p->getSampleBank();
    bank.setLoopOwnSounds(false);
    bank.setSelected({ library[0].getFileName(), library[1].getFileName() }, false);
    check(!bank.loopHasOwnSounds() && bank.getSelected(SampleBank::kLoop) == bank.getSelected(), "the loop section starts with the main list");
    check(getValue(*p, ids::sampleLoopOwn) < 0.5f, "and with the main levels");
    bank.setSelected({ library[1].getFileName() }, true, SampleBank::kLoop);
    check(bank.loopHasOwnSounds() && bank.getSelected(SampleBank::kLoop) == StringArray { library[1].getFileName() } && bank.getSelected().size() == 2
              && bank.getNumSelected(SampleBank::kLoop) == 1,
          "it can have a list of its own; the main one stays");
    {
        SampleBank fresh;
        check(fresh.loopHasOwnSounds() && fresh.getSelected(SampleBank::kLoop) == bank.getSelected(SampleBank::kLoop), "the loop list is remembered for next time");
    }
    MemoryBlock state;
    p->getStateInformation(state);
    auto q = makeProcessor();
    q->getSampleBank().setLoopOwnSounds(false, false);
    q->setStateInformation(state.getData(), (int) state.getSize());
    check(q->getSampleBank().loopHasOwnSounds() && q->getSampleBank().getSelected(SampleBank::kLoop) == bank.getSelected(SampleBank::kLoop),
          "the loop list is saved with the project");
    const auto text = p->exportPreset()->toString();
    check(text.contains("LOOP_SOUNDS") && text.contains("sampleLoopOwn"), "a preset file holds the loop list and levels");
    auto r = makeProcessor();
    r->getSampleBank().setLoopOwnSounds(false, false);
    String message;
    if (auto parsed = XmlDocument::parse(text))
        check(r->importPreset(*parsed, message) && r->getSampleBank().loopHasOwnSounds()
                  && r->getSampleBank().getSelected(SampleBank::kLoop) == bank.getSelected(SampleBank::kLoop),
              "and importing it brings the loop list along");
    {
        // the panel's LOOP section, with its own list and levels
        setValue(*p, ids::loopOn, 1.0f);
        setValue(*p, ids::sampleLoopOwn, 1.0f);
        setValue(*p, ids::sampleLoopMix, 0.75f);
        PedalLookAndFeel lookAndFeel;
        SamplePanel panel(*p, lookAndFeel);
        panel.refresh();
        panel.showSection(SampleBank::kLoop);
        const auto image = snapshot(panel, 2.0f);
        const auto file = File::getSpecialLocation(File::tempDirectory).getChildFile("bitvibe_sample_loop.png");
        file.deleteFile();
        if (auto out = std::unique_ptr<FileOutputStream>(file.createOutputStream()))
            PNGImageFormat().writeImageToStream(image, *out);
    }
    bank.setLoopOwnSounds(false);
    check(!bank.loopHasOwnSounds() && bank.getSelected(SampleBank::kLoop) == bank.getSelected(), "SAME AS MAIN goes back to the main list");
}

// Each sequencer type remembers its own step knobs: switching with the SEQ TYPE buttons stores the knobs of the type being
// left and brings back the ones the new type was left with; a type not used yet keeps the knobs as they are; steps with a
// type of their own stay put; the memory is part of the preset.
void testStepMemory()
{
    std::puts("step memory per sequencer type");
    auto p = makeProcessor();
    p->setCurrentProgram(0);
    auto steps = [&]
    {
        std::array<float, 6> v {};
        for (int i = 0; i < 6; ++i)
            v[(size_t) i] = getValue(*p, ids::step(i).toRawUTF8());
        return v;
    };
    auto setSteps = [&](const std::array<float, 6>& v)
    {
        for (int i = 0; i < 6; ++i)
            setValue(*p, ids::step(i).toRawUTF8(), v[(size_t) i]);
    };
    auto same = [](const std::array<float, 6>& a, const std::array<float, 6>& b)
    {
        for (size_t i = 0; i < 6; ++i)
            if (std::abs(a[i] - b[i]) > 0.002f)
                return false;
        return true;
    };
    const std::array<float, 6> A { 0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f }, B { 0.1f, 0.2f, 0.3f, 0.35f, 0.45f, 0.55f }, C { 0.25f, 0.5f, 0.75f, 0.85f, 0.95f, 0.15f };

    setValue(*p, ids::seqType, 0.0f);
    setSteps(A);
    p->switchSequencerType(1);
    check(roundToInt(getValue(*p, ids::seqType)) == 1 && same(steps(), A), "a type used for the first time keeps the knobs as they are");
    setSteps(B);
    p->switchSequencerType(2);
    check(roundToInt(getValue(*p, ids::seqType)) == 2 && same(steps(), B), "and so does the next one");
    setSteps(C);
    p->switchSequencerType(0);
    check(same(steps(), A), "back on pitch: the knobs are where pitch left them");
    p->switchSequencerType(1);
    check(same(steps(), B), "filter: its own again");
    p->switchSequencerType(2);
    check(same(steps(), C), "sample rate: its own again");
    p->switchSequencerType(2);
    check(same(steps(), C), "pressing the type that is already in use changes nothing");

    // a step with a type of its own keeps its knob through every switch
    setValue(*p, ids::stepType(2).toRawUTF8(), 2.0f); // step 3: filter on its own
    setValue(*p, ids::step(2).toRawUTF8(), 0.33f);
    p->switchSequencerType(0);
    check(std::abs(getValue(*p, ids::step(2).toRawUTF8()) - 0.33f) < 0.002f && std::abs(getValue(*p, ids::step(0).toRawUTF8()) - A[0]) < 0.002f,
          "a step with its own type is left alone; the others swap");
    p->switchSequencerType(1);
    check(std::abs(getValue(*p, ids::step(2).toRawUTF8()) - 0.33f) < 0.002f, "and again");
    setValue(*p, ids::stepType(2).toRawUTF8(), 0.0f);

    // the memory is saved with the preset and travels in preset files
    p->saveCurrentPreset();
    p->setCurrentProgram(1);
    check(getValue(*p, ids::stepMem(0, 0).toRawUTF8()) < 0.0f, "another preset has no memory yet");
    p->setCurrentProgram(0);
    const auto afterRecall = steps();
    p->switchSequencerType(2);
    check(same(steps(), C) && !same(afterRecall, C), "the memory comes back with its preset (sample rate still has its knobs)");
    const auto text = p->exportPreset()->toString();
    check(text.contains("stepMem0_1") && text.contains("stepMem2_6"), "a preset file holds the memory");
    auto q = makeProcessor();
    q->setCurrentProgram(4);
    String message;
    if (auto parsed = XmlDocument::parse(text))
        check(q->importPreset(*parsed, message) && std::abs(getValue(*q, ids::stepMem(2, 5).toRawUTF8()) - getValue(*p, ids::stepMem(2, 5).toRawUTF8())) < 0.002f,
              "and importing it brings the memory along");
}

// The delay through the processor: off by default, echoes the sequence when on, travels in preset files.
void testDelayParams()
{
    std::puts("delay (processor level)");
    const auto in = makeInput(48000);
    auto p = makeProcessor();
    setValue(*p, ids::seqPlay, 5.0f);
    const auto plain = run(*p, in);
    check(getValue(*p, ids::delayOn) < 0.5f && roundToInt(getValue(*p, ids::delayTime)) == bitvibe::kDefaultDelayDivision,
          "the delay starts off, at 1/8");
    setValue(*p, ids::delayMix, 0.8f);
    check(maxDifference(run(*p, in), plain) < 1.0e-6f, "it does nothing until it is switched on");
    setValue(*p, ids::delayOn, 1.0f);
    const auto echoed = run(*p, in);
    check(maxDifference(echoed, plain) > 0.01f && isFinite(echoed), "on, it adds echoes (max diff " + String(maxDifference(echoed, plain), 3) + ")");
    setValue(*p, ids::delayRepeats, (float) bitvibe::kDelayForever);
    check(p->getParameters().size() > 0 && getValue(*p, ids::delayRepeats) >= (float) bitvibe::kDelayForever - 0.01f
              && p->apvts.getParameter(ids::delayRepeats)->getText(1.0f, 32) == "Forever",
          "fully right, REPEATS reads Forever");
    check(isFinite(run(*p, in)), "forever stays finite");
    const auto text = p->exportPreset()->toString();
    check(text.contains("delayMix") && text.contains("delayRepeats"), "a preset file holds the delay");
}

// Preset files: export a sound from one plug-in, import it into another, and it sounds the same; the receiver's own
// rig settings stay as they were; files that are not presets are refused.
void testPresetFiles()
{
    std::puts("preset files");
    const auto in = makeInput(48000);
    auto a = makeProcessor();
    a->setCurrentProgram(3);
    a->changeProgramName(3, "Shared sound");
    setValue(*a, ids::sampleRate, 0.4f);
    setValue(*a, ids::seqPlay, 5.0f);
    setValue(*a, ids::reverbMix, 0.6f);
    setValue(*a, ids::reverbType, 3.0f);
    setValue(*a, ids::distOn, 1.0f);
    setValue(*a, ids::eqOn, 1.0f);
    setValue(*a, ids::eqBand(2, "Gain").toRawUTF8(), 9.0f);
    setValue(*a, ids::mix, 0.8f);
    a->getDrumBeat().setPattern({ bitvibe::DrumAction::RestartSequence, 0x1111u, 0x1Fu, 3 });
    const auto library = SampleBank::libraryFiles();
    if (library.size() >= 2)
        a->getSampleBank().setSelected({ library[0].getFileName(), library[1].getFileName(), "not-on-this-computer.wav" }, false);

    const auto text = a->exportPreset()->toString();
    check(text.contains("BITVIBE_PRESET") && text.contains("Shared sound") && !text.contains("\"inputGain\""), "the file holds the sound, not the rig settings");

    auto b = makeProcessor();
    b->setCurrentProgram(9);
    setValue(*b, ids::inputGain, 6.0f);
    String message;
    const auto parsed = XmlDocument::parse(text);
    check(parsed != nullptr && b->importPreset(*parsed, message), "another plug-in imports it: " + message);
    check(b->getProgramName(9) == "Shared sound" && b->getCurrentProgram() == 9, "into the slot that was showing, with its name");
    check(std::abs(getValue(*b, ids::inputGain) - 6.0f) < 0.01f, "the receiver's input level is left alone");
    check(b->getDrumBeat().getPattern() == a->getDrumBeat().getPattern(), "the beat pattern comes along");
    if (library.size() >= 2)
        check(b->getSampleBank().getSelected().size() == 2 && message.contains("1 sound"), "the sounds it has are picked; the one it lacks is reported");
    setValue(*b, ids::inputGain, 0.0f);
    check(maxDifference(run(*a, in), run(*b, in)) < 1.0e-5f, "and it sounds the same");

    String why;
    check(!b->importPreset(XmlElement("SOMETHING_ELSE"), why) && why.isNotEmpty(), "a file that is not a preset is refused");
    auto broken = XmlDocument::parse(text);
    if (auto* fx = broken->getChildByName("EFFECTS"))
        for (auto* e : fx->getChildIterator())
            e->setAttribute("value", e->getStringAttribute("id") == ids::reverbMix ? "1e9" : "nan");
    check(b->importPreset(*broken, why) && getValue(*b, ids::reverbMix) <= 1.0f && isFinite(run(*b, in)), "nonsense values are kept in range");
}

// The dry/wet mix and the "FX only on triggers" option, through the processor with an engine that is reset
// before every render (the host test cannot guarantee that).
void testMixAndTriggerOnly()
{
    std::puts("mix and trigger-only effects (processor level)");
    const auto in = makeInput(48000);
    auto p = makeProcessor();
    p->setCurrentProgram(2); // Lo-Fi Radio: crush + filter, sequencer and stutter off

    const auto full = run(*p, in);
    check(maxDifference(full, in) > 0.01, "the Lo-Fi Radio sound changes the signal (max diff " + String(maxDifference(full, in), 4) + ")");

    setValue(*p, ids::fxTriggers, 1.0f);
    const double idle = maxDifference(run(*p, in), in);
    check(idle < 1.0e-6, "trigger-only option on, nothing triggering: the signal stays clean (max diff " + String(idle, 8) + ")");
    setValue(*p, ids::fxTriggers, 0.0f);

    setValue(*p, ids::mix, 0.0f);
    const double zero = maxDifference(run(*p, in), in);
    check(zero < 1.0e-6, "Mix at 0% returns the clean input (max diff " + String(zero, 8) + ")");

    setValue(*p, ids::mix, 0.5f);
    const auto half = run(*p, in);
    double err = 0;
    for (int c = 0; c < half.getNumChannels(); ++c)
        for (int i = 0; i < half.getNumSamples(); ++i)
            err = jmax(err, (double) std::abs(half.getSample(c, i) - 0.5f * (in.getSample(c, i) + full.getSample(c, i))));
    check(err < 1.0e-5, "Mix at 50% is the average of clean and effect (max error " + String(err, 8) + ")");

    setValue(*p, ids::mix, 1.0f);
    check(maxDifference(run(*p, in), full) < 1.0e-6, "Mix back at 100% restores the full effect");

    // With the pitch sequencer running (slot 1, continuous) the effect stage counts as triggered, so the option
    // makes no difference there.
    p->setCurrentProgram(0);
    const auto seqOff = run(*p, in);
    setValue(*p, ids::fxTriggers, 1.0f);
    const auto seqOn = run(*p, in);
    check(maxDifference(seqOn, seqOff) < 1.0e-6, "a continuous sequencer keeps the effects on, so the option changes nothing");
}


// Hover descriptors. The tooltip window finds the component under the mouse and asks it for its tooltip text, so
// every control must answer that question. The step knobs' text follows the sequencer type.
void testTooltips(BitVibeEditor& ed, const File* imageFile, const String& expectedStepWord)
{
    std::puts("hover descriptors");
    int controls = 0, missing = 0, tooLong = 0;
    StringArray stepOneTips;
    auto& pane = ed.getContent();
    for (int i = 0; i < pane.getNumChildComponents(); ++i)
    {
        auto* c = pane.getChildComponent(i);
        const bool interactive = dynamic_cast<Slider*>(c) != nullptr || dynamic_cast<Button*>(c) != nullptr
                                 || dynamic_cast<ComboBox*>(c) != nullptr || dynamic_cast<Label*>(c) != nullptr
                                 || dynamic_cast<FootSwitch*>(c) != nullptr || dynamic_cast<PedalButton*>(c) != nullptr
                                 || dynamic_cast<StepTypeTags*>(c) != nullptr || dynamic_cast<StepStrip*>(c) != nullptr;
        if (!interactive)
            continue;
        ++controls;
        auto* tip = dynamic_cast<TooltipClient*>(c);
        const String text = tip != nullptr ? tip->getTooltip() : String();
        missing += text.isEmpty() ? 1 : 0;
        tooLong += text.length() > 260 ? 1 : 0;
        if (text.startsWith("STEP 1:"))
            stepOneTips.add(text);
        if (text.isEmpty())
            std::printf("    no tooltip on child %d (%s)\n", i, typeid(*c).name());
    }
    check(controls == 76 && missing == 0, String(controls) + " controls, " + String(missing) + " without a tooltip (expected 76 and 0)");
    check(tooLong == 0, "every tooltip is short enough to read at a glance (none over 260 characters)");
    check(stepOneTips.size() == 1 && stepOneTips[0].containsIgnoreCase(expectedStepWord),
          "the step knobs describe the " + expectedStepWord + " sequencer: '" + (stepOneTips.isEmpty() ? String() : stepOneTips[0]) + "'");

    // What a tooltip looks like in the editor's own style: show one for the MIX knob.
    if (imageFile != nullptr)
    {
        const Point<int> mixKnob(240, 721); // below the 36-pixel tab strip
        // The headless editor is never marked visible, which makes getComponentAt() return nothing, so find the
        // topmost visible child under the point the same way hit-testing would.
        Component* under = nullptr;
        for (int i = pane.getNumChildComponents(); --i >= 0;)
            if (auto* c = pane.getChildComponent(i); c->isVisible() && c->getBounds().contains(mixKnob))
            {
                under = c;
                break;
            }
        auto* tip = dynamic_cast<TooltipClient*>(under);
        check(tip != nullptr && tip->getTooltip().startsWith("DRY/WET:"),
              "the control under the DRY/WET knob answers with its description");
        if (tip != nullptr)
        {
            TooltipWindow window(&ed, 600);
            window.displayTip(ed.localPointToGlobal(mixKnob.translated(10, -30)), tip->getTooltip());
            save(ed, *imageFile);
        }
    }
}


// ---- drum map -----------------------------------------------------------------------------------

using bitvibe::Drum;
using bitvibe::DrumAction;
using bitvibe::DrumSettings;

struct DrumRun
{
    std::vector<int64> restarts;   // samples where the sequence (re)started
    int stepChanges = 0;           // times the step changed while running
    bool stuttered = false;
    AudioBuffer<float> out;
};

// Plays the beat over silence in 64-sample blocks and watches the sequencer.
DrumRun runDrums(BitVibeProcessor& p, double seconds)
{
    constexpr int block = 64;
    DrumRun r;
    const int total = (int) (seconds * 48000.0);
    r.out.setSize(2, total);
    r.out.clear();
    p.prepareToPlay(48000.0, 512);
    MidiBuffer midi;
    int previous = -1;
    for (int pos = 0; pos < total; pos += block)
    {
        AudioBuffer<float> b(r.out.getArrayOfWritePointers(), 2, pos, jmin(block, total - pos));
        p.processBlock(b, midi);
        const int step = p.getEngine().getCurrentStep();
        if (step == 0 && previous != 0)
            r.restarts.push_back(pos);
        if (step >= 0 && previous >= 0 && step != previous)
            ++r.stepChanges;
        r.stuttered = r.stuttered || p.getEngine().isStuttering();
        previous = step;
    }
    return r;
}

// A processor with the demo beat loaded, playing silently, and a sequencer that plays once and is idle
// again long before the next kick (100 ms steps, six of them).
std::unique_ptr<BitVibeProcessor> drumProcessor()
{
    auto p = makeProcessor();
    p->getDrumBeat().setAnalysisMode(DrumBeat::AnalysisMode::BuiltInOnly);
    p->getDrumBeat().loadDemo();
    p->getDrumBeat().waitUntilIdle(30000);
    setValue(*p, ids::seqPlay, 1.0f);
    setValue(*p, ids::tempo, 240.0f);
    setValue(*p, ids::seqMult, 2.0f); // x4: 62.5 ms steps
    setValue(*p, ids::pickTriggers, 0.0f);
    setValue(*p, ids::beatPlay, 1.0f);
    setValue(*p, ids::beatLevel, DrumBeat::kBeatOffDb);
    return p;
}

String describeHits(const DrumBeat::Model& m, Drum d)
{
    String s;
    for (const auto& h : m.hits)
        if (h.drum == d && h.confidence >= 0.1f)
            s << String(h.time, 3) << "(" << String(h.levelDb, 1) << "dB," << String(h.confidence, 2) << (h.excluded ? ",x" : "") << ") ";
    return s;
}

String describeRestarts(const std::vector<int64>& restarts)
{
    String s;
    for (auto r : restarts)
        s << String(r / 48000.0, 3) << " ";
    return s;
}

std::vector<double> demoTimes(Drum d, float minGain = 0.0f)
{
    std::vector<double> t;
    for (const auto& n : bitvibe::demo::renderDemoBeat().notes)
        if (n.drum == d && n.gain >= minGain)
            t.push_back(n.time);
    std::sort(t.begin(), t.end());
    return t;
}

// Every expected time has a restart within its block (plus 1 ms), and nothing else restarted.
bool restartsMatch(const std::vector<int64>& restarts, const std::vector<double>& times)
{
    if (restarts.size() != times.size())
        return false;
    for (size_t i = 0; i < times.size(); ++i)
    {
        const double expected = times[i] * 48000.0;
        if ((double) restarts[i] < expected - 48.0 - 64.0 || (double) restarts[i] > expected + 48.0 + 64.0)
            return false;
    }
    return true;
}

double rmsOf(const AudioBuffer<float>& b)
{
    double acc = 0.0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            acc += (double) b.getSample(c, i) * b.getSample(c, i);
    return std::sqrt(acc / jmax(1, b.getNumChannels() * b.getNumSamples()));
}

void testDrumMap()
{
    std::puts("drum map checks");
    {
        auto p = makeProcessor();
        check(p->getParameters().size() == 140, "140 parameters (" + String(p->getParameters().size()) + ")");
        const auto m = p->getDrumBeat().getModel();
        const auto s = p->getDrumBeat().getSettings();
        check(!m.loaded && s[(size_t) Drum::Kick].action == DrumAction::RestartSequence
                  && s[(size_t) Drum::Snare].action == DrumAction::Off,
              "a fresh plug-in has no beat; kick restarts the sequence, the rest is off");
        setValue(*p, ids::beatPlay, 1.0f);
        check(isFinite(run(*p, makeInput(24000))), "PLAY with no beat loaded is harmless");
    }

    // The demo beat: analysed, and every kick restarts the sequence at its sample.
    auto p = drumProcessor();
    const auto model = p->getDrumBeat().getModel();
    check(model.loaded && model.audioAvailable && model.isDemo && model.hits.size() >= 40,
          "demo beat loaded: " + String((int) model.hits.size()) + " hits, " + String(model.durationSeconds, 2) + " s, "
              + model.engine);
    check(std::abs(model.tempoBpm - 100.0) < 1.0, "demo beat tempo " + String(model.tempoBpm, 2) + " BPM");
    auto r = runDrums(*p, model.durationSeconds);
    check(restartsMatch(r.restarts, demoTimes(Drum::Kick)),
          "every kick restarts the sequence on time (" + String((int) r.restarts.size()) + " restarts)");
    check(rmsOf(r.out) == 0.0, "Beat Level off: the beat is silent but still triggers");

    // Min level: only snares measured at -3 dB or louder restart (the quiet snare of the first two bars is
    // about 6 dB down; the second one is masked by a ringing tom, so the analysis' own levels decide).
    p->getDrumBeat().setSettings(Drum::Kick, { DrumAction::Off, -12.0f, 0.3f });
    p->getDrumBeat().setSettings(Drum::Snare, { DrumAction::RestartSequence, -3.0f, 0.3f });
    std::vector<double> loudSnares;
    for (const auto& h : model.hits)
        if (bitvibe::hitFires(h, p->getDrumBeat().getSettings()[(size_t) Drum::Snare]) && h.drum == Drum::Snare)
            loudSnares.push_back(h.time);
    r = runDrums(*p, model.durationSeconds);
    check(restartsMatch(r.restarts, loudSnares) && loudSnares.size() < demoTimes(Drum::Snare).size(),
          "snare at -3 dB: only the louder snares restart (" + String((int) r.restarts.size()) + " of "
              + String((int) demoTimes(Drum::Snare).size()) + ")");
    if (!restartsMatch(r.restarts, demoTimes(Drum::Snare, 0.6f)))
        std::printf("    snares: %s\n    restarts: %s\n", describeHits(model, Drum::Snare).toRawUTF8(),
                    describeRestarts(r.restarts).toRawUTF8());
    p->getDrumBeat().setSettings(Drum::Snare, { DrumAction::RestartSequence, -12.0f, 0.3f });
    r = runDrums(*p, model.durationSeconds);
    check(restartsMatch(r.restarts, demoTimes(Drum::Snare)), "snare at -12 dB: every snare restarts (" + String((int) r.restarts.size()) + ")");

    // Stutter action.
    setValue(*p, ids::stutter, 4.0f);
    p->getDrumBeat().setSettings(Drum::Snare, { DrumAction::Stutter, -12.0f, 0.3f });
    r = runDrums(*p, 2.0);
    check(r.stuttered && r.restarts.empty(), "snare set to Stutter fires the stutter and leaves the sequence alone");
    setValue(*p, ids::stutter, 0.0f);

    // Steps follow the hi-hats: every hi-hat that fires moves the sequencer one step, nothing else does.
    p->getDrumBeat().setSettings(Drum::Snare, { DrumAction::Off, -12.0f, 0.3f });
    p->getDrumBeat().setSettings(Drum::HiHat, { DrumAction::NextStep, -48.0f, 0.3f });
    setValue(*p, ids::seqPlay, 5.0f);
    setValue(*p, ids::stepClock, 1.0f);
    int firingHats = 0;
    const auto settings = p->getDrumBeat().getSettings();
    for (const auto& h : model.hits)
        firingHats += h.time > 0.001 && bitvibe::hitFires(h, settings[(size_t) h.drum]) ? 1 : 0;
    r = runDrums(*p, model.durationSeconds);
    check(r.stepChanges == firingHats, "Step Clock = drum hits: " + String(r.stepChanges) + " step changes for " + String(firingHats)
                                           + " firing hi-hats");
    setValue(*p, ids::stepClock, 0.0f);
    setValue(*p, ids::seqPlay, 1.0f);
    p->getDrumBeat().setSettings(Drum::HiHat, { DrumAction::Off, -12.0f, 0.3f });
    p->getDrumBeat().setSettings(Drum::Kick, { DrumAction::RestartSequence, -12.0f, 0.3f });

    // Excluding a hit: the first kick no longer restarts.
    int firstKick = -1;
    for (int i = 0; i < (int) model.hits.size() && firstKick < 0; ++i)
        if (model.hits[(size_t) i].drum == Drum::Kick && model.hits[(size_t) i].confidence >= 0.3f)
            firstKick = i;
    p->getDrumBeat().setHitExcluded(firstKick, true);
    r = runDrums(*p, model.durationSeconds);
    auto kicks = demoTimes(Drum::Kick);
    kicks.erase(kicks.begin());
    check(restartsMatch(r.restarts, kicks), "an excluded kick no longer restarts the sequence");
    if (!restartsMatch(r.restarts, kicks))
        std::printf("    kicks: %s\n    restarts: %s\n", describeHits(p->getDrumBeat().getModel(), Drum::Kick).toRawUTF8(),
                    describeRestarts(r.restarts).toRawUTF8());

    // The beat is heard at its level, after the effect; bypass silences it.
    setValue(*p, ids::beatLevel, 0.0f);
    r = runDrums(*p, 2.0);
    check(rmsOf(r.out) > 0.02, "Beat Level 0 dB: the beat is heard (rms " + String(rmsOf(r.out), 3) + ")");
    setValue(*p, ids::bypass, 1.0f);
    r = runDrums(*p, 2.0);
    check(rmsOf(r.out) == 0.0 && r.restarts.empty(), "bypass silences the beat and its triggers");
    setValue(*p, ids::bypass, 0.0f);

    // State round trip: settings, hits and edits come back, and the audio is rebuilt.
    p->getDrumBeat().setSettings(Drum::Cymbal, { DrumAction::RestartAndStutter, -20.0f, 0.55f });
    MemoryBlock state;
    p->getStateInformation(state);
    auto q = makeProcessor();
    q->setStateInformation(state.getData(), (int) state.getSize());
    q->getDrumBeat().waitUntilIdle(30000);
    const auto restored = q->getDrumBeat().getModel();
    const auto original = p->getDrumBeat().getModel();
    bool sameHits = restored.hits.size() == original.hits.size();
    for (size_t i = 0; sameHits && i < restored.hits.size(); ++i)
    {
        const auto& a = restored.hits[i];
        const auto& b = original.hits[i];
        sameHits = std::abs(a.time - b.time) < 1.0e-4 && a.drum == b.drum && a.excluded == b.excluded
                   && std::abs(a.levelDb - b.levelDb) < 0.01f && std::abs(a.confidence - b.confidence) < 0.001f;
        if (!sameHits)
            std::printf("    hit %d differs: %.5f %d %.3f %.4f %d vs %.5f %d %.3f %.4f %d\n", (int) i, a.time, (int) a.drum,
                        (double) a.levelDb, (double) a.confidence, (int) a.excluded, b.time, (int) b.drum, (double) b.levelDb,
                        (double) b.confidence, (int) b.excluded);
    }
    check(sameHits && restored.loaded && restored.audioAvailable && restored.isDemo,
          "hits, edits and the demo audio survive the state (" + String((int) restored.hits.size()) + " vs "
              + String((int) p->getDrumBeat().getModel().hits.size()) + " hits, loaded " + String((int) restored.loaded)
              + ", audio " + String((int) restored.audioAvailable) + ", demo " + String((int) restored.isDemo) + ")");
    check(q->getDrumBeat().getSettings() == p->getDrumBeat().getSettings(), "per-drum settings survive the state");

    // A state from before the drum map: defaults, no beat.
    {
        auto old = makeProcessor();
        old->getDrumBeat().loadDemo();
        old->getDrumBeat().waitUntilIdle(30000);
        MemoryBlock oldState;
        if (auto xml = makeProcessor()->apvts.copyState().createXml())
            AudioProcessor::copyXmlToBinary(*xml, oldState);
        old->setStateInformation(oldState.getData(), (int) oldState.getSize());
        old->getDrumBeat().waitUntilIdle(30000);
        check(!old->getDrumBeat().getModel().loaded
                  && old->getDrumBeat().getSettings()[(size_t) Drum::Kick].action == DrumAction::RestartSequence,
              "a state without a drum map clears the beat and resets the settings");
    }

    // A real file: the demo beat written as a WAV and loaded through the decoder.
    const File wav = File::getSpecialLocation(File::tempDirectory).getChildFile("bitvibe_drum_test.wav");
    {
        const auto demoBeat = bitvibe::demo::renderDemoBeat(44100.0);
        AudioBuffer<float> b(1, (int) demoBeat.audio.size());
        b.copyFrom(0, 0, demoBeat.audio.data(), b.getNumSamples());
        wav.deleteFile();
        WavAudioFormat format;
        if (auto stream = std::unique_ptr<FileOutputStream>(wav.createOutputStream()))
            if (auto writer = std::unique_ptr<AudioFormatWriter>(format.createWriterFor(stream.get(), 44100.0, 1, 24, {}, 0)))
            {
                stream.release();
                writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
            }
    }
    auto f = drumProcessor();
    f->getDrumBeat().loadFile(wav);
    f->getDrumBeat().waitUntilIdle(30000);
    const auto fromFile = f->getDrumBeat().getModel();
    check(fromFile.loaded && fromFile.audioAvailable && !fromFile.isDemo && fromFile.name == "bitvibe_drum_test"
              && fromFile.hits.size() >= 40,
          "a 44.1 kHz WAV loads and is analysed (" + String((int) fromFile.hits.size()) + " hits): " + f->getDrumBeat().getStatus());
    r = runDrums(*f, fromFile.durationSeconds);
    check(restartsMatch(r.restarts, demoTimes(Drum::Kick)), "its kicks restart the sequence on time at 48 kHz playback");

    // With the neural helper installed (optional), files go through it.
    if (DrumBeat::isNeuralHelperInstalled())
    {
        auto n = drumProcessor();
        n->getDrumBeat().setAnalysisMode(DrumBeat::AnalysisMode::Auto);
        n->getDrumBeat().loadFile(wav);
        n->getDrumBeat().waitUntilIdle(120000);
        const auto neural = n->getDrumBeat().getModel();
        check(neural.loaded && neural.engine.contains("neural") && neural.hits.size() >= 30,
              "neural helper: " + n->getDrumBeat().getStatus());
        r = runDrums(*n, neural.durationSeconds);
        check(restartsMatch(r.restarts, demoTimes(Drum::Kick)), "neural helper: its kicks restart the sequence on time");
    }
    else
    {
        std::puts("  (neural helper not installed: its checks are skipped)");
    }

    // The file disappears: the saved hits still trigger, the beat is silent, and the status says why.
    MemoryBlock withFile;
    f->getStateInformation(withFile);
    wav.deleteFile();
    auto g = drumProcessor();
    g->setStateInformation(withFile.getData(), (int) withFile.getSize());
    g->getDrumBeat().waitUntilIdle(30000);
    setValue(*g, ids::beatPlay, 1.0f);
    setValue(*g, ids::beatLevel, 0.0f);
    const auto missing = g->getDrumBeat().getModel();
    r = runDrums(*g, fromFile.durationSeconds);
    check(missing.loaded && !missing.audioAvailable && restartsMatch(r.restarts, demoTimes(Drum::Kick)) && rmsOf(r.out) == 0.0,
          "missing file: the hits still trigger and the beat is silent - " + g->getDrumBeat().getStatus());

    // A file that is not audio.
    const File junk = File::getSpecialLocation(File::tempDirectory).getChildFile("bitvibe_not_audio.wav");
    junk.replaceWithText("not a wave file");
    auto j = makeProcessor();
    j->getDrumBeat().loadFile(junk);
    j->getDrumBeat().waitUntilIdle(30000);
    check(!j->getDrumBeat().getModel().loaded && j->getDrumBeat().getStatus().startsWith("Cannot read"),
          "a file that is not audio is refused: " + j->getDrumBeat().getStatus());
    junk.deleteFile();
}

// The DRUMS tab with the demo beat playing, part-way through the loop.
void saveDrumsTab(const File& file)
{
    auto p = drumProcessor();
    auto& beat = p->getDrumBeat();
    beat.setSettings(Drum::Kick, { DrumAction::RestartSequence, -12.0f, 0.3f });
    beat.setSettings(Drum::Snare, { DrumAction::RestartAndStutter, -3.0f, 0.3f });
    beat.setSettings(Drum::HiHat, { DrumAction::NextStep, -12.0f, 0.3f });
    setValue(*p, ids::beatLevel, -6.0f);
    setValue(*p, ids::stutter, 4.0f);
    runDrums(*p, 2.47); // just after the third kick
    std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(p->createEditor()));
    ed->showDrums(true);
    save(*ed, file);
}

// ---- the beat pattern: restarts on chosen steps of the bar, whatever the drums do ------------------------

void testBeatPatternProcessor()
{
    std::puts("beat pattern checks");
    auto p = drumProcessor();
    auto& beat = p->getDrumBeat();
    for (int d = 0; d < bitvibe::kNumDrums; ++d)
        beat.setSettings((Drum) d, { DrumAction::Off, -12.0f, 0.3f });
    const auto model = beat.getModel();
    const double barSeconds = 1.0; // a bar of 4/4 at the 240 BPM that drumProcessor() sets: 16 steps of 62.5 ms

    auto expectedBars = [&](double seconds, double every)
    {
        std::vector<double> t;
        for (double x = 0.0; x < seconds - 0.01; x += every)
            t.push_back(x);
        return t;
    };

    auto r = runDrums(*p, model.durationSeconds);
    check(r.restarts.empty() && !r.stuttered, "no pattern and every drum Off: nothing triggers");

    beat.setPattern({ DrumAction::RestartSequence, 0x0001u });
    r = runDrums(*p, model.durationSeconds);
    check(restartsMatch(r.restarts, expectedBars(model.durationSeconds, barSeconds)),
          "step 1 only: one restart per bar (" + String((int) r.restarts.size()) + "): " + describeRestarts(r.restarts));

    beat.setPattern({ DrumAction::RestartSequence, 0x1111u });
    r = runDrums(*p, model.durationSeconds);
    check(restartsMatch(r.restarts, expectedBars(model.durationSeconds, barSeconds / 4.0)),
          "beats pattern: a restart on every beat (" + String((int) r.restarts.size()) + ")");

    beat.setPattern({ DrumAction::Off, 0x1111u });
    r = runDrums(*p, model.durationSeconds);
    check(r.restarts.empty(), "pattern action Off: nothing triggers");

    // The tempo knob moves the grid, and the time signature changes the length of the bar.
    beat.setPattern({ DrumAction::RestartSequence, 0x0001u });
    setValue(*p, ids::tempo, 120.0f);
    r = runDrums(*p, model.durationSeconds);
    check(restartsMatch(r.restarts, expectedBars(model.durationSeconds, 2.0)),
          "tempo 120: the bar is 2 s (" + String((int) r.restarts.size()) + " restarts)");
    setValue(*p, ids::tempo, 240.0f);
    beat.setPattern({ DrumAction::RestartSequence, 0x0001u, 0x1Fu, 1 }); // 3/4
    r = runDrums(*p, model.durationSeconds);
    check(restartsMatch(r.restarts, expectedBars(model.durationSeconds, 0.75)),
          "3/4: the bar is three beats, 0.75 s (" + String((int) r.restarts.size()) + " restarts)");
    beat.setPattern({ DrumAction::RestartSequence, 0x0001u, 0x1Fu, 6 }); // 12/8
    r = runDrums(*p, model.durationSeconds);
    check(restartsMatch(r.restarts, expectedBars(model.durationSeconds, 1.5)),
          "12/8: the bar is 24 steps, 1.5 s (" + String((int) r.restarts.size()) + " restarts)");

    // Hits and the pattern together: kicks still restart, and the pattern adds its own.
    beat.setSettings(Drum::Kick, { DrumAction::RestartSequence, -12.0f, 0.3f });
    beat.setPattern({ DrumAction::Stutter, 0x4444u });
    setValue(*p, ids::stutter, 4.0f);
    r = runDrums(*p, model.durationSeconds);
    check(r.stuttered && restartsMatch(r.restarts, demoTimes(Drum::Kick)), "kick hits restart while the pattern stutters on its own steps");
    setValue(*p, ids::stutter, 0.0f);

    // State round trip, and an old state without a pattern.
    beat.setPattern({ DrumAction::NextStep, 0x2492u, 0x15u, 6 });
    MemoryBlock state;
    p->getStateInformation(state);
    auto q = makeProcessor();
    q->setStateInformation(state.getData(), (int) state.getSize());
    q->getDrumBeat().waitUntilIdle(30000);
    const auto got = q->getDrumBeat().getPattern();
    check(got.action == DrumAction::NextStep && got.steps == 0x2492u && got.signature == 6 && got.randomBeats == 0x15u,
          "the pattern, its time signature and the beats RANDOM may change are saved with the project");

    auto old = makeProcessor();
    old->getDrumBeat().setPattern({ DrumAction::Stutter, 0xFFFFu });
    MemoryBlock oldState;
    if (auto xml = makeProcessor()->apvts.copyState().createXml())
        AudioProcessor::copyXmlToBinary(*xml, oldState);
    old->setStateInformation(oldState.getData(), (int) oldState.getSize());
    old->getDrumBeat().waitUntilIdle(30000);
    check(old->getDrumBeat().getPattern() == bitvibe::DrumPattern {}, "a state from before the pattern existed resets it to off");
}

// ---- where the beat's sound goes: the main output or the "Beat Monitor" output -------------------------

struct Rendered
{
    AudioBuffer<float> main { 2, 0 }, aux { 2, 0 };
    bitvibe::BeatRoute route = bitvibe::BeatRoute::Silent;
};

// Plays the input through the processor in 512-sample blocks. With the monitor output switched on the host
// buffer has four channels; the two monitor channels start out filled with a loud constant so that a
// processor that forgets to clear them shows up.
Rendered runWithMonitor(BitVibeProcessor& p, const AudioBuffer<float>& in)
{
    p.prepareToPlay(48000.0, 512); // resets the engine and restarts the beat
    const int total = p.getTotalNumOutputChannels();
    const int n = in.getNumSamples();
    Rendered r;
    r.main.setSize(2, n);
    r.aux.setSize(2, n);
    r.aux.clear();
    MidiBuffer midi;
    for (int pos = 0; pos < n; pos += 512)
    {
        const int len = jmin(512, n - pos);
        AudioBuffer<float> block(total, len);
        for (int c = 0; c < total; ++c)
            for (int i = 0; i < len; ++i)
                block.setSample(c, i, c < 2 ? in.getSample(c, pos + i) : 0.9f);
        p.processBlock(block, midi);
        for (int c = 0; c < 2; ++c)
        {
            r.main.copyFrom(c, pos, block, c, 0, len);
            if (total == 4)
                r.aux.copyFrom(c, pos, block, 2 + c, 0, len);
        }
    }
    r.route = p.getBeatRoute();
    return r;
}

void setMonitor(BitVibeProcessor& p, bool connected)
{
    auto* bus = p.getBus(false, 1);
    const bool ok = bus != nullptr && bus->enable(connected) && bus->isEnabled() == connected
                    && p.getTotalNumOutputChannels() == (connected ? 4 : 2);
    if (!ok)
        check(false, String("could not ") + (connected ? "switch on" : "switch off") + " the Beat Monitor output");
}

void testBeatRouting()
{
    std::puts("beat routing (processor level)");
    using bitvibe::BeatRoute;
    constexpr int kAuto = 0, kMonitor = 1, kMain = 2;
    constexpr float kLoud = -6.0f;
    const auto in = makeInput(96000); // two seconds of guitar-like input

    auto p = makeProcessor();
    auto* monitorBus = p->getBus(false, 1);
    check(p->getBusCount(false) == 2 && monitorBus != nullptr && monitorBus->getName() == "Beat Monitor" && !monitorBus->isEnabled(),
          "a second output, 'Beat Monitor', exists and starts switched off");

    // What a host may ask for: the monitor output is either off or stereo, the main bus is unchanged.
    {
        auto layout = p->getBusesLayout();
        layout.outputBuses.set(1, AudioChannelSet::stereo());
        check(p->checkBusesLayoutSupported(layout), "a stereo Beat Monitor is accepted");
        layout.outputBuses.set(1, AudioChannelSet::mono());
        check(!p->checkBusesLayoutSupported(layout), "a mono Beat Monitor is refused");
        layout.outputBuses.set(1, AudioChannelSet::disabled());
        check(p->checkBusesLayoutSupported(layout), "a disabled Beat Monitor is accepted");
        layout.inputBuses.set(0, AudioChannelSet::stereo());
        layout.outputBuses.set(0, AudioChannelSet::mono());
        check(!p->checkBusesLayoutSupported(layout), "a stereo-in / mono-out main bus is still refused");
    }

    p->getDrumBeat().setAnalysisMode(DrumBeat::AnalysisMode::BuiltInOnly);
    p->getDrumBeat().loadDemo();
    p->getDrumBeat().waitUntilIdle(30000);
    setValue(*p, ids::beatPlay, 1.0f);
    check(p->getDrumBeat().getModel().loaded, "the demo beat is loaded (" + String((int) p->getDrumBeat().getModel().hits.size()) + " hits)");

    auto render = [&](bool monitor, int mode, float levelDb, bool bypass = false, bool play = true)
    {
        setMonitor(*p, monitor);
        setValue(*p, ids::beatOutput, (float) mode);
        setValue(*p, ids::beatLevel, levelDb);
        setValue(*p, ids::bypass, bypass ? 1.0f : 0.0f);
        setValue(*p, ids::beatPlay, play ? 1.0f : 0.0f);
        return runWithMonitor(*p, in);
    };
    auto maxAbs = [](const AudioBuffer<float>& b)
    {
        float m = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
            m = jmax(m, b.getMagnitude(c, 0, b.getNumSamples()));
        return (double) m;
    };

    // The guitar alone: the beat is silent but its hits still trigger the sequencer, exactly as when it plays.
    // Rendered twice: the first play after loading must be exactly the same as a repeat. (It was not: a pending
    // restart request sent the beat back to the top at the start of the second block, one block late from then on.)
    const auto ref = render(false, kMain, DrumBeat::kBeatOffDb);
    const auto refAgain = render(false, kMain, DrumBeat::kBeatOffDb);
    check(maxDifference(ref.main, refAgain.main) < 1.0e-6,
          "the first play after loading the beat is identical to a repeat (max diff " + String(maxDifference(ref.main, refAgain.main), 9) + ")");
    const auto beatFirst = render(false, kMain, kLoud);
    const auto beatAgain = render(false, kMain, kLoud);
    check(maxDifference(beatFirst.main, beatAgain.main) < 1.0e-6, "...and so is the audible beat (no replay of its first block)");
    // The beat in the main output, as it always was.
    const auto inMain = render(false, kMain, kLoud);
    check(inMain.route == BeatRoute::Main && maxDifference(inMain.main, ref.main) > 0.01,
          "Main: the beat is heard in the main output (" + String(maxDifference(inMain.main, ref.main), 3) + ")");

    // No monitor output connected.
    const auto autoNoBus = render(false, kAuto, kLoud);
    check(autoNoBus.route == BeatRoute::Main && maxDifference(autoNoBus.main, inMain.main) < 1.0e-6,
          "Auto without a monitor output falls back to the main output (the beat is not lost)");
    const auto monitorNoBus = render(false, kMonitor, kLoud);
    check(monitorNoBus.route == BeatRoute::Silent && maxDifference(monitorNoBus.main, ref.main) < 1.0e-6,
          "Monitor without a monitor output is silent and keeps the beat out of the main output");

    // Monitor output connected.
    const auto autoBus = render(true, kAuto, kLoud);
    check(autoBus.route == BeatRoute::Monitor, "Auto with a monitor output sends the beat to it");
    const double mainLeak = maxDifference(autoBus.main, ref.main);
    check(mainLeak < 1.0e-6, "...and the main output is exactly the guitar alone (max diff " + String(mainLeak, 9) + ")");
    check(rmsOf(autoBus.aux) > 0.01, "...and the Beat Monitor output carries the beat (rms " + String(rmsOf(autoBus.aux), 4) + ")");
    double auxErr = 0.0;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < in.getNumSamples(); ++i)
            auxErr = jmax(auxErr, (double) std::abs(autoBus.aux.getSample(c, i) - (inMain.main.getSample(c, i) - ref.main.getSample(c, i))));
    check(auxErr < 1.0e-5, "...and it is exactly the sound that Main would have added (max error " + String(auxErr, 9) + ")");

    const auto monitorBusOnly = render(true, kMonitor, kLoud);
    check(monitorBusOnly.route == BeatRoute::Monitor && maxDifference(monitorBusOnly.main, ref.main) < 1.0e-6
              && maxDifference(monitorBusOnly.aux, autoBus.aux) < 1.0e-9,
          "Monitor with a monitor output behaves the same as Auto");

    const auto mainBus = render(true, kMain, kLoud);
    check(mainBus.route == BeatRoute::Main && maxDifference(mainBus.main, inMain.main) < 1.0e-6 && maxAbs(mainBus.aux) == 0.0,
          "Main with a monitor output available keeps the beat in the main output and leaves the monitor silent");

    // Silence: the beat's level fully down, stopped, and the host's bypass.
    const auto levelOff = render(true, kAuto, DrumBeat::kBeatOffDb);
    check(maxAbs(levelOff.aux) == 0.0 && maxDifference(levelOff.main, ref.main) < 1.0e-6,
          "beat level fully down: the monitor output is silent (and the junk it arrived with is cleared)");
    const auto stopped = render(true, kAuto, kLoud, false, false);
    check(maxAbs(stopped.aux) == 0.0, "beat stopped: the monitor output is silent");
    const auto bypassed = render(true, kAuto, kLoud, true);
    check(maxAbs(bypassed.aux) == 0.0 && maxDifference(bypassed.main, in) < 1.0e-6, "bypass: the monitor is silent and the main output is the untouched input");

    // Leave the processor as it was.
    setMonitor(*p, false);
    setValue(*p, ids::bypass, 0.0f);
    setValue(*p, ids::beatOutput, 0.0f);
}

} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2)
    {
        std::puts("usage: BitVibeEditorSnapshot <output dir>");
        return 2;
    }
    ScopedJuceInitialiser_GUI init;
    // The sample settings are remembered in a file: keep the tests away from the player's real one.
    const File testSettings = File::getSpecialLocation(File::tempDirectory).getChildFile("bitvibe_test_settings");
    testSettings.deleteRecursively();
    SampleBank::setSettingsFolderOverride(testSettings);
    uisettings::markHelpSeen(); // the quick-start help would otherwise cover the screenshots
    uisettings::setAdvancedRight(true); // and the screenshots show every control
    const File outDir(argv[1]);
    outDir.createDirectory();

    auto processorOwner = makeProcessor();
    auto& processor = *processorOwner;

    {
        std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(processor.createEditor()));
        save(*ed, outDir.getChildFile("editor_front.png"));
        const auto tooltipImage = outDir.getChildFile("editor_tooltip.png");
        testTooltips(*ed, &tooltipImage, "semitones");
        ed->setAltLayer(true);
        save(*ed, outDir.getChildFile("editor_alt_pitch.png"));
    }

    // A preset with the expression morph set up and swept most of the way to toe down.
    processor.setCurrentProgram(10);
    setValue(processor, "expression", 0.6f);
    {
        std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(processor.createEditor()));
        save(*ed, outDir.getChildFile("editor_morph.png"));
    }
    processor.setCurrentProgram(0);
    setValue(processor, "expression", 0.0f);

    // A "played" state: crushed, stutter, filter sequencer, steps showing percentages.
    setValue(processor, "sample", 0.55f);
    setValue(processor, "bits", 0.3f);
    setValue(processor, "filter", 0.7f);
    setValue(processor, "stutter", 12.0f);
    setValue(processor, "seqType", 1.0f);
    setValue(processor, "hold", 1.0f);
    setValue(processor, ids::loopOn, 1.0f); // steps 4 to 6 looping forever
    {
        // the strip's mouse: a click shows a step, a drag across steps loops them, a right-click opens the step's menu
        StepStrip strip;
        strip.setSize(138, 28);
        int clicked = -1, menuFor = -1, first = -1, last = -1;
        bool ended = false;
        strip.onStepClick = [&](int s) { clicked = s; };
        strip.onStepMenu = [&](int s) { menuFor = s; };
        strip.onLoopDrag = [&](int a, int b) { first = a; last = b; };
        strip.onLoopDragEnd = [&] { ended = true; };
        auto mouse = [&](float x, bool right, bool wasDragged)
        {
            const auto now = Time::getCurrentTime();
            return MouseEvent(Desktop::getInstance().getMainMouseSource(), { x, 14.0f },
                              right ? ModifierKeys(ModifierKeys::rightButtonModifier) : ModifierKeys(ModifierKeys::leftButtonModifier),
                              1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &strip, &strip, now, { x, 14.0f }, now, 1, wasDragged);
        };
        const float cell = 138.0f / 6.0f;
        strip.mouseDown(mouse(cell * 2.5f, false, false));
        strip.mouseUp(mouse(cell * 2.5f, false, false));
        check(clicked == 2 && first < 0, "a click on step 3 of the strip shows that step");
        clicked = -1;
        strip.mouseDown(mouse(cell * 4.5f, false, false));
        strip.mouseDrag(mouse(cell * 1.5f, false, true));
        strip.mouseUp(mouse(cell * 1.5f, false, true));
        check(first == 1 && last == 4 && ended && clicked < 0, "a drag from step 5 back to step 2 loops steps 2 to 5");
        strip.mouseDown(mouse(cell * 5.5f, true, false));
        strip.mouseUp(mouse(cell * 5.5f, true, false));
        check(menuFor == 5 && clicked < 0, "a right-click on step 6 opens its menu");
        check(strip.stepAt(-5.0f) == 0 && strip.stepAt(500.0f) == 5, "outside the strip counts as its end steps");
    }
    {
        // the step strip frames the loop section
        StepStrip strip;
        strip.setSize(100, 24);
        std::array<StepStrip::Step, 6> steps {};
        strip.set(steps, 4);
        const auto without = snapshot(strip, 2.0f);
        strip.set(steps, 4, 5, 3);
        const auto with = snapshot(strip, 2.0f);
        const auto framed = with.getPixelAt(2 * 52, 3), plainPx = without.getPixelAt(2 * 52, 3), outside = with.getPixelAt(2 * 20, 3);
        check(framed != plainPx && outside == without.getPixelAt(2 * 20, 3), "the step strip frames steps 4 to 6 (given either way round) and nothing else");
        const auto stripFile = File::getSpecialLocation(File::tempDirectory).getChildFile("bitvibe_step_loop.png");
        stripFile.deleteFile();
        if (auto out = std::unique_ptr<FileOutputStream>(stripFile.createOutputStream()))
            PNGImageFormat().writeImageToStream(with, *out);
    }
    {
        std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(processor.createEditor()));
        save(*ed, outDir.getChildFile("editor_played.png"));
        testTooltips(*ed, nullptr, "filter cutoff"); // this state uses the filter sequencer
        ed->setAltLayer(true);
        save(*ed, outDir.getChildFile("editor_alt_filter.png"));
    }

    // A few blocks through the processor after the editor has come and gone.
    AudioBuffer<float> buffer(2, 512);
    MidiBuffer midi;
    for (int i = 0; i < 50; ++i)
    {
        for (int n = 0; n < 512; ++n)
            buffer.setSample(0, n, 0.4f * std::sin(0.03f * (float) (n + i * 512))), buffer.setSample(1, n, buffer.getSample(0, n));
        processor.processBlock(buffer, midi);
    }
    std::puts("processor + editor lifecycle completed");

    saveDrumsTab(outDir.getChildFile("editor_drums.png"));

    if (argc > 3 && String(argv[2]) == "--load")
    {
        auto p = makeProcessor();
        p->getDrumBeat().loadFile(File::getCurrentWorkingDirectory().getChildFile(argv[3]));
        p->getDrumBeat().waitUntilIdle(300000);
        const auto m = p->getDrumBeat().getModel();
        std::printf("loaded %s: %s\n", argv[3], p->getDrumBeat().getStatus().toRawUTF8());
        for (int d = 0; d < bitvibe::kNumDrums; ++d)
        {
            int count = 0;
            for (const auto& h : m.hits)
                count += h.drum == (Drum) d && h.confidence >= 0.3f ? 1 : 0;
            std::printf("  %-7s %d hits at confidence 0.3 or more\n", bitvibe::drumName((Drum) d), count);
        }
    }

    testPresetsAndMorph();
    testMixAndTriggerOnly();
    testDrumMap();
    testBeatPatternProcessor();
    testReverbParams();
    testSampleBlendParams();
    testSoundBanks();
    testResizing();
    testRightModes(outDir);
    testDistortionParams();
    testEqParams();
    testSequenceLoopParams();
    testDelayParams();
    testStepMemory();
    testEqReferenceClip();
    testLoopSectionSounds();
    testPresetFiles();
    testBeatRouting();
    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL OK" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
