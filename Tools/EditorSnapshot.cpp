// Instantiates the processor and editor directly (no plugin host), renders the GUI to PNG files so the
// look can be checked without opening a DAW, and runs the processor-level checks that a host cannot
// reach (capturing the expression-morph sets, renaming and saving presets, state round trips).
//   BitVibeEditorSnapshot <output directory> [--load <audio file>]
// --load also loads that file into the DRUMS tab the way the plug-in would (neural helper included when it
// is installed) and prints what was found.
// Exit code 0 means every check passed.

#include <set>
#include "PluginEditor.h"
#include "EqPanel.h"
#include "PatternPanel.h"
#include "SamplePanel.h"
#include "SamplerPage.h"
#include "BeatDog.h"
#include "TremoloPanel.h"
#include "PresetSearch.h"
#include "StepEditor.h"
#include "PresetPack.h"
#include "EffectGraphs.h"
#include "BeatDogMesh.h"
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
    const int slotCount = bitvibe::kFirstPackSlot + BitVibeProcessor::numPackPresets();
    check(p->getNumPrograms() == slotCount && slotCount == 86 && p->getCurrentProgram() == 0,
          "one bank of " + String(p->getNumPrograms()) + " slots (12 factory, 8 empty, the pack), starts on slot 1");
    check(p->getProgramName(0) == "Pitch Sequence" && p->getProgramName(19) == "Init 20" && p->getProgramName(20) == BitVibeProcessor::packPresetName(0)
              && p->getProgramName(slotCount - 1) == BitVibeProcessor::packPresetName(BitVibeProcessor::numPackPresets() - 1)
              && p->getProgramName(slotCount).isEmpty(),
          "slot names (first '" + p->getProgramName(0) + "', 20th '" + p->getProgramName(19) + "', 21st '" + p->getProgramName(20) + "')");
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

    // A slot holds the effects and the beat pattern too: a pack slot brings its delay, a factory slot puts the
    // effects back to off, and SAVE keeps the effects of the slot it writes.
    {
        auto q = makeProcessor();
        int dub = -1, acid = -1, underwater = -1;
        for (int i = 0; i < q->getNumPrograms(); ++i)
        {
            dub = q->getProgramName(i) == "Dub Throws" ? i : dub;
            acid = q->getProgramName(i) == "Acid Walk" ? i : acid;
            underwater = q->getProgramName(i) == "Underwater" ? i : underwater;
        }
        q->setCurrentProgram(dub);
        const bool dubDelay = dub >= bitvibe::kFirstPackSlot && getValue(*q, ids::delayOn) > 0.5f && getValue(*q, ids::reverbOn) > 0.5f;
        q->setCurrentProgram(0);
        check(dubDelay && getValue(*q, ids::delayOn) < 0.5f && getValue(*q, ids::reverbOn) < 0.5f && !q->isCurrentPresetModified(),
              "a pack slot brings its effects (Dub Throws: delay and reverb), slot 1 puts them back off");
        q->setCurrentProgram(12);
        setValue(*q, ids::tremOn, 1.0f);
        setValue(*q, ids::tremRate, 5.0f);
        check(q->isCurrentPresetModified(), "switching an effect on marks the slot modified");
        q->saveCurrentPreset();
        q->setCurrentProgram(0);
        q->setCurrentProgram(12);
        check(getValue(*q, ids::tremOn) > 0.5f && std::abs(getValue(*q, ids::tremRate) - 5.0f) < 0.01f, "SAVE keeps the effects with the slot");

        // the project state keeps only the slots that changed, and a fresh plug-in gets them back
        MemoryBlock state;
        q->getStateInformation(state);
        auto xml = AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize());
        const auto* bank = xml != nullptr ? xml->getChildByName("PRESETS") : nullptr;
        check(bank != nullptr && bank->getNumChildElements() == 1 && bank->getChildElement(0)->getIntAttribute("slot", -1) == 12,
              "the state holds only the one slot that changed (" + String(bank != nullptr ? bank->getNumChildElements() : -1) + ")");
        auto r = makeProcessor();
        r->setStateInformation(state.getData(), (int) state.getSize());
        r->setCurrentProgram(0);
        r->setCurrentProgram(12);
        check(getValue(*r, ids::tremOn) > 0.5f && r->getProgramName(dub) == "Dub Throws", "and a fresh plug-in gets it back");

        // a state from before the bank grew: sixteen slots, in order, without effects; recalling one leaves the effects alone
        auto old = AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize());
        if (auto* oldBank = old != nullptr ? old->getChildByName("PRESETS") : nullptr)
        {
            oldBank->deleteAllChildElements();
            oldBank->removeAttribute("version");
            for (int i = 0; i < 16; ++i)
            {
                auto* e = oldBank->createNewChildElement("PRESET");
                e->setAttribute("name", i == 13 ? String("Old Mine") : r->getProgramName(i));
                e->setAttribute(ids::filter, i == 13 ? 0.25 : 1.0);
            }
        }
        MemoryBlock oldState;
        if (old != nullptr)
            AudioProcessor::copyXmlToBinary(*old, oldState);
        auto o = makeProcessor();
        o->setStateInformation(oldState.getData(), (int) oldState.getSize());
        setValue(*o, ids::delayOn, 1.0f);
        o->setCurrentProgram(13);
        check(o->getProgramName(13) == "Old Mine" && std::abs(getValue(*o, ids::filter) - 0.25f) < 1.0e-3f && getValue(*o, ids::delayOn) > 0.5f
                  && o->getProgramName(20) == BitVibeProcessor::packPresetName(0),
              "an older project keeps its slots in place; recalling one leaves the effects alone; the pack follows");

        // what each slot uses, for the menu's groups
        auto uses = [&](int slot, int feature) { return slot >= 0 && ((q->presetFeatures(slot) >> feature) & 1u) != 0; };
        check(uses(0, BitVibeProcessor::featPitchSteps) && uses(0, BitVibeProcessor::featAfterPicks) && !uses(0, BitVibeProcessor::featDelay)
                  && uses(acid, BitVibeProcessor::featFilterTypes) && uses(acid, BitVibeProcessor::featFilterSteps)
                  && uses(acid, BitVibeProcessor::featOnItsOwn) && uses(underwater, BitVibeProcessor::featTapeWobble)
                  && uses(underwater, BitVibeProcessor::featTremolo) && q->presetFeatures(14) == 0,
              "presets are grouped by what they use (slot 1 pitch steps after each pick; Acid Walk filter types; Underwater tape wobble; "
              "an empty slot nothing)");
    }

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
                    first->setAttribute("slot", 0);
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
            check(d->getCurrentProgram() >= 0 && d->getCurrentProgram() < d->getNumPrograms(), "an out-of-range program number is clamped");
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
    setValue(*p, ids::reverbOn, 1.0f); // off to start with
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
    setValue(*p, ids::sampleOn, 1.0f); // off to start with
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

// The page's layout: one tempo control (drag or type), SIMPLE hides the stutter and loop settings under the pedal and
// says when one is on, a shorter window in SIMPLE with DOG5 in the right-hand column, and the host sees the
// parameters in groups, named as on the pedal.
void testLayout(const File& outDir)
{
    std::puts("layout");
    auto p = makeProcessor();
    {
        // the host's groups
        StringArray groups;
        for (auto* g : p->getParameterTree().getSubgroups(false))
            groups.add(g->getName());
        check(groups.size() >= 10 && groups.contains("Pedal") && groups.contains("Input and Output") && groups.contains("Sample Blend")
                  && groups.contains("Sampler"),
              "the host sees the parameters in groups (" + groups.joinIntoString(", ") + ")");
        int outside = 0;
        for (auto* node : p->getParameterTree())
            outside += node->getParameter() != nullptr ? 1 : 0;
        check(outside == 0, "and none outside a group");
        check(p->apvts.getParameter(ids::mix)->getName(64) == "Dry/Wet" && p->apvts.getParameter(ids::seqPlay)->getName(64) == "Sequencer"
                  && p->apvts.getParameter(ids::sampleMaxBeats)->getName(64) == "Sample Max Beats",
              "named as on the pedal (Dry/Wet, Sequencer, Sample Max Beats)");
    }

    uisettings::setAdvancedRight(true);
    std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(p->createEditor()));
    const int advancedHeight = ed->getHeight();
    TempoReadout* tempo = nullptr;
    BeatDog* dog = nullptr;
    Component* holdBox = nullptr, *input = nullptr;
    int tempoKnobs = 0;
    for (auto* c : ed->getContent().getChildren())
    {
        if (tempo == nullptr)
            tempo = dynamic_cast<TempoReadout*>(c);
        if (dog == nullptr)
            dog = dynamic_cast<BeatDog*>(c);
        if (auto* box = dynamic_cast<ComboBox*>(c); box != nullptr && box->getTooltip().startsWith("STUTTER TYPE"))
            holdBox = box;
        if (auto* tip = dynamic_cast<TooltipClient*>(c); tip != nullptr && tip->getTooltip().startsWith("INPUT:"))
            input = c;
        if (auto* tip = dynamic_cast<TooltipClient*>(c); tip != nullptr && tip->getTooltip().startsWith("TEMPO"))
            ++tempoKnobs;
    }
    if (tempo == nullptr || dog == nullptr || holdBox == nullptr || input == nullptr)
    {
        check(false, "the tempo read-out, DOG5, the stutter type and the INPUT knob are on the page");
        return;
    }
    check(tempoKnobs == 1, "one tempo control on the page (the read-out on the pedal)");
    setValue(*p, ids::tempo, 120.0f);
    tempo->onDragStart();
    tempo->onDrag(20.0f);
    tempo->onDragEnd();
    check(std::abs(p->apvts.getRawParameterValue(ids::tempo)->load() - 130.0f) < 0.05f, "dragging the tempo up 20 pixels: 120 to 130 BPM");
    setValue(*p, ids::tempo, 120.0f);
    check(dog->getX() > 480 && input->getX() > 480 && input->getY() > dog->getBottom() && holdBox->isVisible(),
          "ADVANCED: DOG5 and the in / out panel under the effects in the right-hand column, the stutter type showing");
    check(ed->getHeight() <= 1040 - 90, "the window fits a 1080p screen at full size (" + String(ed->getHeight()) + " px)");

    ed->setRightMode(false);
    check(ed->getHeight() < advancedHeight && !holdBox->isVisible(),
          "SIMPLE: a shorter window (" + String(ed->getHeight()) + " instead of " + String(advancedHeight) + "), the stutter type hidden");
    check(input->getBottom() <= ed->getContent().getHeight() - 12, "and everything still fits");
    setValue(*p, ids::hold, 1.0f);
    const auto withHold = snapshot(*ed, 1.0f);
    setValue(*p, ids::hold, 0.0f);
    const auto withoutHold = snapshot(*ed, 1.0f);
    int changed = 0;
    for (int y = input->getY() + 110; y < input->getY() + 140; ++y) // the line under the SIMPLE panel's boxes
        for (int x = 500; x < 940; ++x)
            changed += withHold.getPixelAt(x, y) != withoutHold.getPixelAt(x, y) ? 1 : 0;
    check(changed > 40, "and a line says when a hidden setting is on (STUTTER HOLD: " + String(changed) + " pixels)");
    ed->setRightMode(true);
    check(ed->getHeight() == advancedHeight, "ADVANCED again: the full height");

    // HELP's second page: the chart of what triggers what; its tab switches to it, a click elsewhere closes HELP
    HelpOverlay* help = nullptr;
    for (auto* c : ed->getContent().getChildren())
        if (help == nullptr)
            help = dynamic_cast<HelpOverlay*>(c);
    if (help == nullptr)
    {
        check(false, "HELP is on the page");
        return;
    }
    help->showPage(HelpOverlay::kQuickStart);
    help->setVisible(true);
    help->toFront(false);
    const auto card = help->getLocalBounds().withSizeKeepingCentre(help->getWidth() - 120, jmin(help->getHeight() - 40, 370)); // the quick start's card
    auto click = [&](Point<float> at)
    {
        const auto now = Time::getCurrentTime();
        help->mouseUp(MouseEvent(Desktop::getInstance().getMainMouseSource(), at, ModifierKeys(ModifierKeys::leftButtonModifier), 1.0f, 0.0f,
                                 0.0f, 0.0f, 0.0f, help, help, now, at, now, 1, false));
    };
    const auto quickStart = snapshot(*ed, 1.0f);
    click({ (float) card.getRight() - 28.0f - 88.0f, (float) card.getY() + 34.0f });
    check(help->isVisible() && help->getPage() == HelpOverlay::kTriggers, "HELP: the WHAT TRIGGERS WHAT tab shows the flow chart");
    const auto chart = snapshot(*ed, 1.0f);
    int differ = 0;
    for (int yy = card.getY() + 80; yy < card.getBottom(); yy += 2)
        for (int xx = card.getX(); xx < card.getRight(); xx += 2)
            differ += chart.getPixelAt(xx, yy) != quickStart.getPixelAt(xx, yy) ? 1 : 0;
    const auto chartCard = help->getLocalBounds().withSizeKeepingCentre(help->getWidth() - 120, jmin(help->getHeight() - 40, 700));
    check(differ > 5000, "a page of its own (" + String(differ) + " pixels differ from the quick start)");
    int orange = 0;
    const auto accentNow = pedalstyle::accent;
    for (int yy = chartCard.getY() + 80; yy < chartCard.getBottom(); ++yy)
        for (int xx = chartCard.getX(); xx < chartCard.getRight(); ++xx)
        {
            const auto px = chart.getPixelAt(xx, yy);
            orange += std::abs(px.getRed() - accentNow.getRed()) < 40 && std::abs(px.getGreen() - accentNow.getGreen()) < 40
                              && std::abs(px.getBlue() - accentNow.getBlue()) < 40 ? 1 : 0;
        }
    check(orange > 1500, "drawn in the theme's accent: the arrows and the edges of what acts while a sequence plays (" + String(orange) + " pixels)");
    save(*ed, outDir.getChildFile("help_triggers.png"));
    help->showPage(HelpOverlay::kQuickStart);
    save(*ed, outDir.getChildFile("help_quick_start.png"));
    click({ 30.0f, 30.0f });
    check(!help->isVisible(), "a click outside the tabs closes HELP");
}

// WCAG's contrast ratio between two colours (1 to 21): 4.5 is enough for small text, 3 for large text and graphics.
double contrastRatio(Colour a, Colour b)
{
    auto luminance = [](Colour c)
    {
        auto channel = [](float v) { return v <= 0.03928f ? (double) v / 12.92 : std::pow(((double) v + 0.055) / 1.055, 2.4); };
        return 0.2126 * channel(c.getFloatRed()) + 0.7152 * channel(c.getFloatGreen()) + 0.0722 * channel(c.getFloatBlue());
    };
    const double la = luminance(a), lb = luminance(b);
    return (jmax(la, lb) + 0.05) / (jmin(la, lb) + 0.05);
}

// The themes: each one keeps its text readable, the THEME button goes through them all and back, the choice is
// remembered for the next window, and a contact sheet of every theme is saved.
void testThemes(const File& outDir)
{
    std::puts("themes");
    const auto& all = pedalstyle::themes();
    StringArray names;
    for (const auto& t : all)
        names.addIfNotAlreadyThere(t.name);
    check(all.size() >= 6 && names.size() == (int) all.size() && names[0] == "NIGHT",
          String((int) all.size()) + " themes with names of their own, NIGHT first (" + names.joinIntoString(", ") + ")");
    for (const auto& t : all)
    {
        struct Pair
        {
            const char* what;
            Colour a, b;
            double needed;
        };
        const Pair pairs[] = {
            { "text on a panel", t.ink, t.panel, 7.0 },
            { "a control's name", t.inkLabel, t.panel, 4.5 },
            { "a value", t.inkDim, t.panel, 4.5 },
            { "text on the pedal (top)", t.ink, t.faceTop, 4.5 },
            { "text on the pedal (bottom)", t.ink, t.faceBottom, 4.5 },
            { "text in a read-out", t.ink, t.well, 7.0 },
            { "an unlit button's text", t.inkDim, t.buttonTop, 3.0 },
            { "a lit button's text", t.ink, t.litBottom, 4.5 },
            { "text on the accent", t.onAccent, t.accent, 4.5 },
            { "the accent on a panel", t.accent, t.panel, 3.0 },
            { "a knob's pointer", t.pointer, t.capTop, 4.5 },
            { "a menu's text", t.ink, t.popup, 7.0 },
            { "a tip's text", t.ink, t.tooltip, 7.0 },
            { "pitch on the pedal", t.steps[0], t.faceBottom, 3.0 },
            { "filter on the pedal", t.steps[1], t.faceBottom, 3.0 },
            { "rate on the pedal", t.steps[2], t.faceBottom, 3.0 },
        };
        String low;
        for (const auto& p : pairs)
            if (contrastRatio(p.a, p.b) < p.needed)
                low << " " << p.what << " " << String(contrastRatio(p.a, p.b), 1) << ":1";
        check(low.isEmpty(), String(t.name) + ": text and lights stand out from what they are on" + (low.isEmpty() ? String() : " (too faint:" + low + ")"));
    }

    auto p = makeProcessor();
    uisettings::setAdvancedRight(true);
    pedalstyle::setTheme(0);
    std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(p->createEditor()));
    PedalButton* button = nullptr;
    for (auto* c : ed->getContent().getChildren())
        if (auto* b = dynamic_cast<PedalButton*>(c); b != nullptr && b->getText() == "VIEW")
            button = b;
    check(button != nullptr && button->isVisible() && pedalstyle::themeIndex() == pedalstyle::findTheme("SYNTHWAVE")
              && String(all[(size_t) pedalstyle::themeIndex()].name) == "SYNTHWAVE",
          "a VIEW button on the page, which starts in SYNTHWAVE when no theme has been picked");
    ed->viewMenuChosen(BitVibeEditor::kViewTheme + 0); // the contact sheet goes through them from the first, NIGHT

    // every theme in turn, into a contact sheet (three across)
    constexpr float kThumb = 0.5f;
    const int thumbW = roundToInt((float) ed->getWidth() * kThumb), thumbH = roundToInt((float) ed->getHeight() * kThumb);
    constexpr int kAcross = 3, kGap = 16, kCaption = 26;
    const int rows = ((int) all.size() + kAcross - 1) / kAcross;
    Image sheet(Image::RGB, kAcross * thumbW + (kAcross + 1) * kGap, rows * (thumbH + kCaption + kGap) + kGap, true, SoftwareImageType());
    Graphics sg(sheet);
    sg.fillAll(Colour(0xff202024));
    const auto night = snapshot(*ed, 1.0f);
    bool allDiffer = true, namesShown = true;
    for (int i = 0; i < (int) all.size(); ++i)
    {
        if (i > 0)
        {
            ed->viewMenuChosen(BitVibeEditor::kViewTheme + i);
            int ticked = -1;
            const auto menu = ed->viewMenu();
            PopupMenu::MenuItemIterator item(menu);
            while (item.next())
                if (item.getItem().isTicked && item.getItem().itemID >= BitVibeEditor::kViewTheme
                    && item.getItem().itemID < BitVibeEditor::kViewSize)
                    ticked = item.getItem().itemID - BitVibeEditor::kViewTheme;
            namesShown = namesShown && pedalstyle::themeIndex() == i && ticked == i;
            const auto shot = snapshot(*ed, 1.0f);
            int differ = 0;
            for (int yy = 0; yy < shot.getHeight(); yy += 4)
                for (int xx = 0; xx < shot.getWidth(); xx += 4)
                    differ += shot.getPixelAt(xx, yy) != night.getPixelAt(xx, yy) ? 1 : 0;
            allDiffer = allDiffer && differ > shot.getWidth() * shot.getHeight() / 16 / 2; // most of the page
        }
        const int x = kGap + (i % kAcross) * (thumbW + kGap), y = kGap + (i / kAcross) * (thumbH + kCaption + kGap);
        sg.setColour(Colour(0xffe8e8e8));
        sg.setFont(pedalstyle::bold(15.0f));
        sg.drawText(all[(size_t) i].name, x, y, thumbW, kCaption - 6, Justification::left);
        sg.drawImageAt(snapshot(*ed, kThumb), x, y + kCaption);
        // each one at full size too, out of the way in the temp folder (to look a theme over)
        const File full = File::getSpecialLocation(File::tempDirectory).getChildFile("bitvibe_test_themes")
                              .getChildFile(String(all[(size_t) i].name).toLowerCase() + ".png");
        full.getParentDirectory().createDirectory();
        full.deleteFile();
        if (auto out = std::unique_ptr<FileOutputStream>(full.createOutputStream()))
            PNGImageFormat().writeImageToStream(snapshot(*ed, 1.0f), *out);
    }
    check(namesShown && pedalstyle::themeIndex() == (int) all.size() - 1, "each theme picked from VIEW shows, ticked in the menu");
    check(allDiffer, "and each theme repaints most of the page");
    check(uisettings::themeName() == String(all.back().name), "the theme picked is remembered (" + uisettings::themeName() + ")");
    {
        pedalstyle::setTheme(0); // as a new session would start, before it reads the settings
        std::unique_ptr<BitVibeEditor> next(static_cast<BitVibeEditor*>(p->createEditor()));
        check(pedalstyle::themeIndex() == (int) all.size() - 1, "a window opened later opens in it");
    }
    {
        // DOG5's screen in the theme's colour: no green left in its pre-rendered frames
        BeatDog* dog = nullptr;
        for (auto* c : ed->getContent().getChildren())
            if (dog == nullptr)
                dog = dynamic_cast<BeatDog*>(c);
        if (dog != nullptr)
        {
            pedalstyle::setTheme(pedalstyle::findTheme("SURPLUS"));
            const auto shot = snapshot(*dog, 1.0f);
            const int side = dog->getHeight() - 30; // the screen (BeatDog::screenArea)
            const Rectangle<int> screen((dog->getWidth() - side) / 2, 22, side, side);
            int green = 0, amber = 0;
            for (int yy = screen.getY(); yy < screen.getBottom(); ++yy)
                for (int xx = screen.getX(); xx < screen.getRight(); ++xx)
                {
                    const auto px = shot.getPixelAt(xx, yy);
                    green += px.getGreen() > 120 && px.getRed() < px.getGreen() / 2 ? 1 : 0;
                    amber += px.getRed() > 150 && px.getBlue() < px.getRed() / 3 ? 1 : 0;
                }
            check(green < 20 && amber > 300, "SURPLUS: DOG5 on an amber screen (" + String(amber) + " amber pixels, " + String(green) + " green)");
        }
        else
            check(false, "DOG5 is on the page");
    }
    {
        // WIREFRAME: the outlines glow, brighter right on a beat, and flush to the accent while the sequencer acts
        pedalstyle::setTheme(pedalstyle::findTheme("WIREFRAME"));
        pedalstyle::setPulse(0.0f);
        const auto dim = snapshot(*ed, 1.0f);
        pedalstyle::setPulse(1.0f);
        const auto lit = snapshot(*ed, 1.0f);
        pedalstyle::setPulse(1.0f, 1.0f);
        const auto hot = snapshot(*ed, 1.0f);
        pedalstyle::setPulse(0.0f);
        auto near = [](const Image& img, Colour target)
        {
            int n = 0;
            for (int yy = 0; yy < img.getHeight(); yy += 2)
                for (int xx = 0; xx < img.getWidth(); xx += 2)
                {
                    const auto px = img.getPixelAt(xx, yy);
                    n += std::abs(px.getRed() - target.getRed()) < 60 && std::abs(px.getGreen() - target.getGreen()) < 60
                                 && std::abs(px.getBlue() - target.getBlue()) < 60 ? 1 : 0;
                }
            return n;
        };
        const int dimGlow = near(dim, pedalstyle::theme.panelEdge), litGlow = near(lit, pedalstyle::theme.panelEdge);
        const int litHot = near(lit, pedalstyle::theme.accent), hotHot = near(hot, pedalstyle::theme.accent);
        check(litGlow > dimGlow + 200, "WIREFRAME: the outlines glow brighter on the beat (" + String(dimGlow) + " -> " + String(litGlow) + " edge pixels)");
        check(hotHot > litHot + 200, "and flush to the accent while the sequencer acts (" + String(litHot) + " -> " + String(hotHot) + " accent pixels)");
        auto paintMs = [&]
        {
            const double t0 = Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 10; ++i)
                snapshot(*ed, 1.0f);
            return (Time::getMillisecondCounterHiRes() - t0) / 10.0;
        };
        pedalstyle::setPulse(1.0f, 1.0f);
        const double wireMs = paintMs();
        pedalstyle::setTheme(0);
        const double nightMs = paintMs();
        pedalstyle::setTheme(pedalstyle::findTheme("WIREFRAME"));
        pedalstyle::setPulse(0.0f);
        std::printf("    (a full-window paint: WIREFRAME %.1f ms, NIGHT %.1f ms, software renderer)\n", wireMs, nightMs);
    }
    ed->setThemeFromPage(0);
    check(pedalstyle::themeIndex() == 0 && uisettings::themeName() == "NIGHT", "and back to NIGHT");
    uisettings::setThemeName("SYNTHWAVE"); // back to the theme a new player starts in, for the screenshots after this
    ed.reset();

    const File file = outDir.getChildFile("themes.png");
    file.deleteFile();
    PNGImageFormat png;
    if (auto out = std::unique_ptr<FileOutputStream>(file.createOutputStream()))
        png.writeImageToStream(sheet, *out);
    std::printf("wrote %s (%d bytes)\n", file.getFullPathName().toRawUTF8(), (int) file.getSize());
}

// The VIEW menu (window size, DOG5), the preset list, undo and redo, Shift for fine settings, the output meter, and
// the names a screen reader hears.
void testPageTools()
{
    std::puts("page tools");
    auto p = makeProcessor();
    uisettings::setAdvancedRight(true);
    std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(p->createEditor()));
    struct Item
    {
        int id;
        String text;
        bool ticked;
    };
    auto items = [](const PopupMenu& menu)
    {
        std::vector<Item> v;
        PopupMenu::MenuItemIterator it(menu);
        while (it.next())
            if (it.getItem().itemID != 0)
                v.push_back({ it.getItem().itemID, it.getItem().text, it.getItem().isTicked });
        return v;
    };
    auto find = [&ed](const String& tipStart) -> Component*
    {
        for (auto* c : ed->getContent().getChildren())
            if (auto* tip = dynamic_cast<TooltipClient*>(c); tip != nullptr && tip->getTooltip().startsWith(tipStart))
                return c;
        return nullptr;
    };

    // VIEW
    {
        int themes = 0, ticked = 0;
        bool hundred = false, dog = false;
        for (const auto& i : items(ed->viewMenu()))
        {
            if (i.id >= BitVibeEditor::kViewTheme && i.id < BitVibeEditor::kViewSize)
            {
                ++themes;
                ticked += i.ticked ? 1 : 0;
            }
            hundred = hundred || (i.text == "100%" && i.ticked);
            dog = dog || (i.id == BitVibeEditor::kViewDog && i.ticked);
        }
        check(themes == (int) pedalstyle::themes().size() && ticked == 1 && hundred && dog,
              "VIEW lists the themes (the one showing ticked), the window sizes (100% ticked) and Show DOG5 (ticked)");
    }
    ed->viewMenuChosen(BitVibeEditor::kViewSize + 125);
    check(ed->getWidth() == roundToInt(948.0f * 1.25f) && std::abs(uisettings::loadWindowScale() - 1.25f) < 0.01f,
          "VIEW > 125%: the window is a quarter bigger (" + String(ed->getWidth()) + " wide), and remembered");
    ed->setWindowScale(1.0f);
    auto* dog = dynamic_cast<BeatDog*>(find("DOG5"));
    auto* input = find("INPUT:");
    if (dog == nullptr || input == nullptr)
    {
        check(false, "DOG5 and the INPUT knob are on the page");
        return;
    }
    const int fullHeight = ed->getHeight(), inputY = input->getY();
    ed->viewMenuChosen(BitVibeEditor::kViewDog);
    check(!dog->isVisible() && !uisettings::beatDogShown() && ed->getHeight() < fullHeight && input->getY() < inputY,
          "VIEW > Show DOG5 off: it is hidden (remembered), the in / out panel moves up and the window gets shorter");
    ed->setRightMode(false);
    const int simpleWithout = ed->getHeight();
    ed->setBeatDogShown(true);
    const int simpleWith = ed->getHeight();
    check(simpleWithout < simpleWith - 40, "in SIMPLE too: " + String(simpleWith - simpleWithout) + " px shorter without it");
    ed->setRightMode(true);
    check(dog->isVisible() && ed->getHeight() == fullHeight && input->getY() == inputY, "and on again: as it was");

    // the preset list: groups by feature (as submenus), all of them, your slots
    {
        std::set<int> listed;
        bool currentTicked = false;
        StringArray groups;
        const auto menu = ed->presetMenu();
        PopupMenu::MenuItemIterator it(menu, true);
        while (it.next())
        {
            const auto& i = it.getItem();
            if (i.subMenu != nullptr)
                groups.add(i.text);
            if (i.itemID >= BitVibeEditor::kMenuSlot && i.itemID < BitVibeEditor::kMenuSlot + p->getNumPrograms())
            {
                listed.insert(i.itemID - BitVibeEditor::kMenuSlot);
                currentTicked = currentTicked || (i.isTicked && i.itemID == BitVibeEditor::kMenuSlot + p->getCurrentProgram());
            }
        }
        auto hasGroup = [&](const String& start)
        {
            for (const auto& g : groups)
                if (g.startsWith(start))
                    return true;
            return false;
        };
        check((int) listed.size() == p->getNumPrograms() && currentTicked, "the preset menu lists all " + String((int) listed.size()) + " slots, the one showing ticked");
        check(hasGroup("All presets") && hasGroup("Your slots") && hasGroup("Pitch steps") && hasGroup("Filter types and resonance")
                  && hasGroup("Tape wobble") && hasGroup("Stutter types") && hasGroup("Expression morph"),
              "and groups them by feature (" + String(groups.size()) + " groups: " + groups.joinIntoString(", ").substring(0, 120) + "...)");
    }

    // undo and redo
    auto& undo = p->getUndo();
    setValue(*p, ids::mix, 0.9f); // as host automation does it: no gesture
    undo.endStep();
    check(!undo.canUndo(), "host automation is not recorded for undo");
    auto turn = [&](const char* id, float plain)
    {
        auto* prm = p->apvts.getParameter(id);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(plain * 0.5f));
        prm->setValueNotifyingHost(prm->convertTo0to1(plain)); // a drag: several values, one step
        prm->endChangeGesture();
        undo.endStep();
    };
    turn(ids::mix, 0.3f);
    turn(ids::outputGain, -6.0f);
    const bool keyUndo = ed->keyPressed(KeyPress('z', ModifierKeys::commandModifier, 0));
    check(keyUndo && std::abs(getValue(*p, ids::outputGain)) < 0.05f && std::abs(getValue(*p, ids::mix) - 0.3f) < 0.01f,
          "Ctrl+Z undoes the last change (OUTPUT back to 0 dB, DRY/WET left at 30%)");
    ed->presetMenuChosen(BitVibeEditor::kMenuUndo);
    check(std::abs(getValue(*p, ids::mix) - 0.9f) < 0.01f, "Undo in the preset menu undoes the one before (DRY/WET back to 90%)");
    ed->keyPressed(KeyPress('z', ModifierKeys::commandModifier | ModifierKeys::shiftModifier, 0));
    ed->keyPressed(KeyPress('y', ModifierKeys::commandModifier, 0));
    check(std::abs(getValue(*p, ids::mix) - 0.3f) < 0.01f && std::abs(getValue(*p, ids::outputGain) + 6.0f) < 0.05f,
          "Ctrl+Shift+Z and Ctrl+Y redo both");
    const auto beforePreset = allValues(*p);
    const int program = p->getCurrentProgram();
    ed->presetMenuChosen(BitVibeEditor::kMenuSlot + (program + 4) % 12);
    undo.endStep();
    auto sameValues = [](const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!bitvibe::sameFloat(a[i], b[i]))
                return false;
        return true;
    };
    check(p->getCurrentProgram() == (program + 4) % 12 && !sameValues(allValues(*p), beforePreset), "picking a slot from the preset menu recalls it");
    ed->presetMenuChosen(BitVibeEditor::kMenuUndo);
    check(sameValues(allValues(*p), beforePreset), "and one undo brings back the sound from before it");
    ed->keyPressed(KeyPress('z', ModifierKeys::commandModifier, 0));
    ed->keyPressed(KeyPress('z', ModifierKeys::commandModifier, 0));
    ed->keyPressed(KeyPress('z', ModifierKeys::commandModifier, 0)); // more than there is: nothing happens
    check(!undo.canUndo() && undo.canRedo() && std::abs(getValue(*p, ids::mix) - 0.9f) < 0.01f,
          "undoing everything stops at the start, with redo still there");
    check(!ed->keyPressed(KeyPress(KeyPress::spaceKey)), "other keys go on to the host (space still starts and stops it)");

    // Shift for fine settings: the same drag turns the knob a fifth as far
    if (auto* knob = dynamic_cast<KnobSlider*>(find("TRIGGER")))
    {
        auto drag = [&](bool shift)
        {
            setValue(*p, ids::sensitivity, 0.5f);
            const double before = knob->getValue();
            const Point<float> start(knob->getWidth() * 0.5f, knob->getHeight() * 0.5f);
            const auto mods = ModifierKeys(ModifierKeys::leftButtonModifier | (shift ? ModifierKeys::shiftModifier : 0));
            auto event = [&](Point<float> at)
            {
                const auto now = Time::getCurrentTime();
                return MouseEvent(Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, knob, knob, now,
                                  start, now, 1, false);
            };
            knob->mouseDown(event(start));
            for (int i = 1; i <= 10; ++i)
                knob->mouseDrag(event(start.translated(0.0f, -4.0f * (float) i)));
            knob->mouseUp(event(start.translated(0.0f, -40.0f)));
            return knob->getValue() - before;
        };
        const double normal = drag(false), fine = drag(true);
        check(normal > 0.05 && std::abs(fine - normal * 0.2) < 0.02,
              "Shift-drag turns a knob a fifth as far (" + String(normal, 3) + " without Shift, " + String(fine, 3) + " with)");
    }
    else
        check(false, "the TRIGGER knob is on the page");

    // the output meter and its clip light
    {
        setValue(*p, ids::outputGain, 12.0f);
        run(*p, makeInput(24000));
        const float peak = p->takeOutputPeak();
        check(peak > 1.0f && p->takeOutputPeak() < 1.0e-6f, "the processor reports the output's peak (" + String(peak, 2) + " at +12 dB), once");
        setValue(*p, ids::outputGain, 0.0f);
        auto* meter = dynamic_cast<OutputMeter*>(find("OUTPUT METER"));
        if (meter != nullptr)
        {
            meter->set(3.0f, true);
            const bool lit = meter->isClipLit();
            const auto now = Time::getCurrentTime();
            meter->mouseUp(MouseEvent(Desktop::getInstance().getMainMouseSource(), {}, ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, meter,
                                      meter, now, {}, now, 1, false));
            check(lit && !meter->isClipLit(), "over 0 dB the clip light comes on; a click puts it out");
        }
        else
            check(false, "the output meter is beside OUTPUT");
    }

    // what a screen reader hears: every knob, box, button and footswitch has a name
    {
        StringArray unnamed;
        int named = 0;
        std::function<void(Component&)> walk = [&](Component& parent)
        {
            for (auto* c : parent.getChildren())
            {
                if (c->isVisible())
                {
                    String name;
                    bool control = true;
                    if (auto* b = dynamic_cast<PedalButton*>(c))
                        name = b->getText().isNotEmpty() ? b->getText() : b->getTitle();
                    else if (auto* tb = dynamic_cast<Button*>(c))
                        name = tb->getTitle().isNotEmpty() ? tb->getTitle() : tb->getButtonText();
                    else if (dynamic_cast<Slider*>(c) != nullptr || dynamic_cast<ComboBox*>(c) != nullptr || dynamic_cast<FootSwitch*>(c) != nullptr)
                        name = c->getTitle();
                    else
                        control = false;
                    if (control)
                    {
                        named += name.isNotEmpty() ? 1 : 0;
                        if (name.isEmpty())
                            unnamed.add(String(typeid(*c).name()) + " in " + typeid(parent).name());
                    }
                    walk(*c); // what is hidden (the DRUMS page) is not read out
                }
            }
        };
        walk(ed->getContent());
        check(unnamed.isEmpty() && named > 40, String(named) + " controls have a name a screen reader can read"
                                                  + (unnamed.isEmpty() ? String() : " (no name: " + unnamed.joinIntoString(", ") + ")"));
        PedalButton* view = nullptr;
        for (auto* c : ed->getContent().getChildren())
            if (auto* b = dynamic_cast<PedalButton*>(c); b != nullptr && b->getText() == "VIEW")
                view = b;
        if (view != nullptr)
        {
            const auto handler = view->createAccessibilityHandler();
            check(handler != nullptr && handler->getTitle() == "VIEW" && handler->getRole() == AccessibilityRole::button
                      && handler->getActions().contains(AccessibilityActionType::press) && handler->getHelp().startsWith("VIEW:"),
                  "a hand-drawn button tells a screen reader its name, that it is a button, how to press it and its tooltip");
        }
    }
    uisettings::setBeatDogShown(true);
}

// The TREMOLO panel's curve: drag the end point to speed it up, the middle to bend it, the start to move only the start;
// double-clicks put them back. Saves a close-up of the panel.
void testTremoloPanel(const File& outDir)
{
    std::puts("tremolo panel");
    auto p = makeProcessor();
    uisettings::setAdvancedRight(true);
    std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(p->createEditor()));
    ed->setEffectTab(BitVibeEditor::kTabTremolo);
    TremoloPanel* panel = nullptr;
    for (auto* c : ed->getContent().getChildren())
        if (panel == nullptr)
            panel = dynamic_cast<TremoloPanel*>(c);
    if (panel == nullptr || !panel->isVisible())
    {
        check(false, "a TREMOLO panel on the right-hand side");
        return;
    }
    auto& curve = panel->getCurve();
    setValue(*p, ids::tremOn, 1.0f);
    setValue(*p, ids::tremRate, 6.0f);
    auto mouse = [&curve](Point<float> at, Point<float> down)
    {
        const auto now = Time::getCurrentTime();
        return MouseEvent(Desktop::getInstance().getMainMouseSource(), at, ModifierKeys(ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, &curve, &curve, now, down, now, 1, false);
    };
    auto drag = [&](int handle, float toHz)
    {
        const auto from = curve.handlePosition(handle);
        const Point<float> to(from.x, curve.yForHz(toHz));
        curve.mouseDown(mouse(from, from));
        curve.mouseDrag(mouse((from + to) * 0.5f, from));
        curve.mouseDrag(mouse(to, from));
        curve.mouseUp(mouse(to, from));
        p->getUndo().endStep();
    };
    check(std::abs(getValue(*p, ids::tremEnd)) < 1.0e-4f, "the curve starts flat: a constant rate");
    drag(TremoloCurve::end, 12.0f);
    check(std::abs(getValue(*p, ids::tremEnd) - 1.0f) < 0.05f, "dragging the right point up to 12 Hz: it speeds up an octave (" + String(getValue(*p, ids::tremEnd), 2) + ")");
    drag(TremoloCurve::middle, 6.0f * std::exp2(0.8f));
    check(getValue(*p, ids::tremBend) > 0.3f, "dragging the middle point up bends the change earlier (" + String(getValue(*p, ids::tremBend), 2) + ")");
    drag(TremoloCurve::start, 3.0f);
    check(std::abs(getValue(*p, ids::tremRate) - 3.0f) < 0.15f && std::abs(getValue(*p, ids::tremRate) * std::exp2(getValue(*p, ids::tremEnd)) - 12.0f) < 0.6f,
          "dragging the left point down to 3 Hz moves only the start (it still ends at "
              + String(getValue(*p, ids::tremRate) * std::exp2(getValue(*p, ids::tremEnd)), 1) + " Hz)");
    drag(TremoloCurve::end, 0.75f);
    check(getValue(*p, ids::tremEnd) < -1.9f, "and the right point below the left: it slows down (" + String(getValue(*p, ids::tremEnd), 2) + ")");
    drag(TremoloCurve::end, 3.05f);
    check(std::abs(getValue(*p, ids::tremEnd)) < 1.0e-4f, "level with the left point: it snaps to constant");
    ed->keyPressed(KeyPress('z', ModifierKeys::commandModifier, 0));
    check(getValue(*p, ids::tremEnd) < -1.9f, "a drag on the curve is one undo step");
    const auto middle = curve.handlePosition(TremoloCurve::middle);
    curve.mouseDoubleClick(mouse(middle, middle));
    check(std::abs(getValue(*p, ids::tremBend)) < 1.0e-4f, "a double-click on the middle point straightens the curve");
    curve.mouseDoubleClick(mouse({ 2.0f, 2.0f }, { 2.0f, 2.0f }));
    check(std::abs(getValue(*p, ids::tremEnd)) < 1.0e-4f, "a double-click away from the points makes the rate constant");

    // a close-up for the README: speeding up, a little bent, fairly hard
    setValue(*p, ids::tremRate, 3.0f);
    setValue(*p, ids::tremEnd, 1.6f);
    setValue(*p, ids::tremBend, 0.4f);
    setValue(*p, ids::tremDepth, 0.9f);
    setValue(*p, ids::tremEdge, 0.45f);
    panel->refresh();
    const auto image = snapshot(*panel, 2.0f);
    const File file = outDir.getChildFile("tremolo.png");
    file.deleteFile();
    if (auto out = std::unique_ptr<FileOutputStream>(file.createOutputStream()))
        PNGImageFormat().writeImageToStream(image, *out);
    std::printf("wrote %s (%d bytes)\n", file.getFullPathName().toRawUTF8(), (int) file.getSize());
}

// The pictures of the sound: the input history beside TRIGGER, the SCOPE on DOG5's screen and the graphs of the
// distortion, the delay and the reverb.
void testVisuals(const File& outDir)
{
    std::puts("visuals");
    auto p = makeProcessor();
    uisettings::setAdvancedRight(true);
    std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(p->createEditor()));
    LevelMeter* history = nullptr;
    BeatDog* dog = nullptr;
    DistortionGraph* distGraph = nullptr;
    DelayGraph* delayGraph = nullptr;
    ReverbGraph* reverbGraph = nullptr;
    for (auto* c : ed->getContent().getChildren())
    {
        history = history != nullptr ? history : dynamic_cast<LevelMeter*>(c);
        dog = dog != nullptr ? dog : dynamic_cast<BeatDog*>(c);
        distGraph = distGraph != nullptr ? distGraph : dynamic_cast<DistortionGraph*>(c);
        delayGraph = delayGraph != nullptr ? delayGraph : dynamic_cast<DelayGraph*>(c);
        reverbGraph = reverbGraph != nullptr ? reverbGraph : dynamic_cast<ReverbGraph*>(c);
    }
    if (history == nullptr || dog == nullptr || distGraph == nullptr || delayGraph == nullptr || reverbGraph == nullptr)
    {
        check(false, "the input history, DOG5 and the three effect graphs are on the page");
        return;
    }

    // the input history: a frame a column, picks marked
    int pushedPicks = 0;
    for (int i = 0; i < LevelMeter::kColumns + 10; ++i)
    {
        const bool pick = i % 15 == 0;
        history->push(pick ? -6.0f : -40.0f, -30.0f, pick);
        pushedPicks += pick && i >= 10 ? 1 : 0; // the first ten have scrolled out
    }
    check(history->countPicks() == pushedPicks,
          "the input history keeps the last " + String(LevelMeter::kColumns) + " frames and their picks (" + String(history->countPicks()) + ")");
    {
        const auto shot = snapshot(*history, 1.0f);
        int lit = 0;
        const auto accentNow = pedalstyle::accent; // the picks are lit in the theme's accent
        for (int y = 0; y < shot.getHeight(); ++y)
            for (int x = 0; x < shot.getWidth(); ++x)
            {
                const auto px = shot.getPixelAt(x, y);
                lit += std::abs(px.getRed() - accentNow.getRed()) < 40 && std::abs(px.getGreen() - accentNow.getGreen()) < 40
                               && std::abs(px.getBlue() - accentNow.getBlue()) < 40 ? 1 : 0;
            }
        check(lit > 40 && history->getWidth() > 60,
              "and draws them lit, in a strip " + String(history->getWidth()) + " px wide (" + String(lit) + " lit pixels)");
    }

    // the scope: the engine keeps the input and the output on the beat clock, the screen draws it
    run(*p, makeInput(48000 * 2));
    const auto& feed = p->getEngine().getScope();
    {
        const auto newest = feed.newest();
        int buckets = 0, withSound = 0, withStep = 0, differ = 0;
        for (auto i = newest - 127; i <= newest; ++i) // two beats
            if (feed.has(i))
            {
                const auto b = feed.get(i);
                ++buckets;
                withSound += b.inHi - b.inLo > 0.01f ? 1 : 0;
                withStep += b.step >= 0 ? 1 : 0;
                differ += std::abs((b.outHi - b.outLo) - (b.inHi - b.inLo)) > 0.01f ? 1 : 0;
            }
        check(newest > 128 && buckets == 128 && withSound > 60 && withStep > 30 && differ > 10,
              "the scope feed holds the last beats bucket by bucket (" + String(buckets) + " buckets, " + String(withSound) + " with sound, "
                  + String(withStep) + " with a step playing, " + String(differ) + " where the output differs)");
    }
    dog->setScope(true);
    check(dog->isScope() && dog->isTimerRunning() && uisettings::beatDogScope(), "SCOPE on: the screen redraws by itself, and it is remembered");
    {
        const auto shot = snapshot(*dog, 2.0f);
        const int side = dog->getHeight() - 30;
        const Rectangle<int> screen(((dog->getWidth() - side) / 2) * 2, 22 * 2, side * 2, side * 2);
        int bright = 0;
        for (int y = screen.getY(); y < screen.getBottom(); ++y)
            for (int x = screen.getX(); x < screen.getRight(); ++x)
                bright += shot.getPixelAt(x, y).getBrightness() > 0.35f ? 1 : 0;
        check(bright > 2000, "and draws the two beats in and out (" + String(bright) + " bright pixels)");
        const File file = outDir.getChildFile("scope.png");
        file.deleteFile();
        if (auto out = std::unique_ptr<FileOutputStream>(file.createOutputStream()))
            PNGImageFormat().writeImageToStream(shot, *out);
        std::printf("wrote %s (%d bytes)\n", file.getFullPathName().toRawUTF8(), (int) file.getSize());
    }
    dog->setScope(false);
    check(!dog->isScope() && !dog->isTimerRunning() && !uisettings::beatDogScope(), "SCOPE off: the dog again, and the timer stops");

    // the distortion graph: more drive, a squarer wave (its peak nearer its average level)
    {
        auto crest = [&](float drive, float type)
        {
            setValue(*p, ids::distType, type);
            setValue(*p, ids::distDrive, drive);
            setValue(*p, ids::distLevel, 0.8f);
            Thread::sleep(100); // past the graph's limit of ten updates a second
            distGraph->refresh();
            const auto& out = distGraph->getOutput();
            double peak = 0.0, sum = 0.0;
            for (float v : out)
            {
                peak = jmax(peak, (double) std::abs(v));
                sum += (double) v * v;
            }
            return peak / jmax(1.0e-9, std::sqrt(sum / (double) jmax<size_t>(1, out.size())));
        };
        const double clean = crest(0.0f, 0.0f), driven = crest(1.0f, 0.0f), fuzz = crest(1.0f, 1.0f);
        check(driven < clean - 0.1 && std::abs(fuzz - driven) > 0.05, "the distortion graph runs the effect: more DRIVE, a squarer wave, the fuzz its own (crest " + String(clean, 2)
                                                         + " clean, " + String(driven, 2) + " driven, " + String(fuzz, 2) + " fuzz)");
    }

    // the delay graph: the echoes where and as loud as the delay plays them
    {
        setValue(*p, ids::delayTime, 8.0f); // 1/4: one beat
        setValue(*p, ids::delayRepeats, 4.0f);
        setValue(*p, ids::delayMix, 0.5f);
        setValue(*p, ids::delayDecay, 0.5f);
        Thread::sleep(100);
        delayGraph->refresh();
        const auto& e = delayGraph->getEchoes();
        const float g = bitvibe::Delay::decayGain(0.5f);
        check(e.size() == 4 && std::abs(e[0].beats - 1.0) < 1.0e-9 && std::abs(e[3].beats - 4.0) < 1.0e-9 && std::abs(e[0].level - 0.5f) < 1.0e-4f
                  && std::abs(e[1].level - 0.5f * g) < 1.0e-4f,
              "the delay graph: 4 REPEATS at 1/4 are 4 echoes a beat apart, each DECAY quieter");
        setValue(*p, ids::delayRepeats, (float) bitvibe::kDelayForever);
        Thread::sleep(100);
        delayGraph->refresh();
        check(delayGraph->getEchoes().size() > 4, "FOREVER: echoes until they fade (" + String((int) delayGraph->getEchoes().size()) + ")");
    }

    // the reverb graph: the tail of the reverb itself
    {
        auto tail = [&](int type, float decay, bool reverse)
        {
            setValue(*p, ids::reverbType, (float) type);
            setValue(*p, ids::reverbDecay, decay);
            setValue(*p, ids::reverbReverseHall, reverse ? 1.0f : 0.0f);
            Thread::sleep(100);
            reverbGraph->refresh();
            const auto& env = reverbGraph->getEnvelope();
            const auto loudest = std::max_element(env.begin(), env.end()) - env.begin();
            return std::make_pair(reverbGraph->getTailSeconds(), (double) loudest / (double) jmax<size_t>(1, env.size()));
        };
        const auto room = tail(0, 0.0f, false), hall = tail(1, 1.0f, false), reversed = tail(1, 0.5f, true);
        check(hall.first > 2.0 * room.first && room.second < 0.2,
              "the reverb graph: a long Hall rings longer than a short Room (" + String(hall.first, 1) + " s, " + String(room.first, 1)
                  + " s), loudest at the start");
        check(reversed.second > 0.3, "REVERSE swells up: it is loudest " + String(roundToInt(reversed.second * 100.0)) + "% of the way along");
    }
}

// The step editor on the Alt layer: LENGTH, drawing the steps, what each step controls, the lanes, and old preset slots
// (saved when the sequence had six steps) keeping their loop section.
void testStepEditor(const File& outDir)
{
    std::puts("step editor");
    auto p = makeProcessor();
    std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(p->createEditor()));
    ed->setAltLayer(true);
    auto& se = ed->getStepEditor();
    check(se.isVisible() && se.getWidth() == StepEditor::kWidth && se.getHeight() == StepEditor::kHeight, "ALT shows the step editor");
    ed->setAltLayer(false);
    check(!se.isVisible(), "and the front knobs hide it again");
    ed->setAltLayer(true);
    auto& undo = p->getUndo();
    undo.endStep();

    const ModifierKeys left(ModifierKeys::leftButtonModifier), right(ModifierKeys::rightButtonModifier);
    auto mouse = [&](Point<float> at, ModifierKeys mods, bool wasDragged)
    {
        const auto now = Time::getCurrentTime();
        return MouseEvent(Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &se, &se, now, at, now, 1,
                          wasDragged);
    };
    auto click = [&](Point<float> at, ModifierKeys mods)
    {
        se.mouseDown(mouse(at, mods, false));
        se.mouseUp(mouse(at, mods, false));
        undo.endStep();
    };
    auto dragFromTo = [&](Point<float> from, Point<float> to, ModifierKeys mods)
    {
        se.mouseDown(mouse(from, mods, false));
        for (int k = 1; k <= 4; ++k)
            se.mouseDrag(mouse(from + (to - from) * ((float) k / 4.0f), mods, true));
        se.mouseUp(mouse(to, mods, true));
        undo.endStep();
    };
    auto length = [&] { return roundToInt(getValue(*p, ids::seqLength)); };
    auto at = [](int step, Rectangle<float> row, float yFraction = 0.5f)
    {
        const auto c = StepEditor::column(step, row);
        return Point<float>(c.getCentreX(), c.getY() + c.getHeight() * yFraction);
    };

    // LENGTH
    check(length() == 6, "the sequence starts six steps long");
    click(StepEditor::plusButton().getCentre(), left);
    check(length() == 7, "+ makes it seven");
    click(StepEditor::minusButton().getCentre(), left);
    click(StepEditor::minusButton().getCentre(), left);
    check(length() == 5, "- twice makes it five");
    dragFromTo(at(4, StepEditor::numbersArea()), at(11, StepEditor::numbersArea()), left);
    check(length() == 12, "dragging the END marker to step 12 makes it twelve (" + String(length()) + ")");

    // drawing the bars: from high on step 1 down to the middle of step 8
    const auto bars = StepEditor::barsArea();
    const std::array<float, 8> before { getValue(*p, ids::step(0).toRawUTF8()), getValue(*p, ids::step(3).toRawUTF8()),
                                        getValue(*p, ids::step(7).toRawUTF8()) };
    dragFromTo(at(0, bars, 0.15f), at(7, bars, 0.5f), left);
    auto semis = [&](int step)
    {
        return bitvibe::mapping::decodeStep(getValue(*p, ids::step(step).toRawUTF8()), bitvibe::SeqType::Pitch).semitones;
    };
    check(semis(0) >= 7.0f && semis(7) == 0.0f && semis(3) > 0.0f && semis(3) < semis(0),
          "a drag across the bars draws the steps it passes: step 1 " + String(semis(0)) + " st, step 4 " + String(semis(3)) + " st, step 8 "
              + String(semis(7)) + " st");
    check(undo.canUndo(), "and can be undone");
    undo.undo();
    check(std::abs(getValue(*p, ids::step(0).toRawUTF8()) - before[0]) < 0.002f && std::abs(getValue(*p, ids::step(3).toRawUTF8()) - before[1]) < 0.002f
              && std::abs(getValue(*p, ids::step(7).toRawUTF8()) - before[2]) < 0.002f && length() == 12,
          "in one go: undo puts every step of the drag back, and nothing before it");
    undo.redo();
    check(semis(0) >= 7.0f, "redo draws them again");

    // the top mutes, the bottom skips
    click(at(9, bars, 0.0f) + Point<float>(0.0f, 1.0f), left);
    click(at(10, bars, 1.0f) - Point<float>(0.0f, 1.0f), left);
    check(bitvibe::mapping::decodeStep(getValue(*p, ids::step(9).toRawUTF8()), bitvibe::SeqType::Pitch).kind == bitvibe::mapping::Step::Kind::Mute
              && bitvibe::mapping::decodeStep(getValue(*p, ids::step(10).toRawUTF8()), bitvibe::SeqType::Pitch).kind == bitvibe::mapping::Step::Kind::Skip,
          "a click at the very top mutes a step, at the very bottom skips it");

    // Shift: fine control of the step it began on
    {
        const float start = getValue(*p, ids::step(5).toRawUTF8()), other = getValue(*p, ids::step(8).toRawUTF8());
        const auto shift = left.withFlags(ModifierKeys::shiftModifier);
        dragFromTo(at(5, bars, 0.5f), at(8, bars, 0.25f), shift);
        const float moved = getValue(*p, ids::step(5).toRawUTF8()) - start;
        check(moved > 0.02f && moved < 0.08f && std::abs(getValue(*p, ids::step(8).toRawUTF8()) - other) < 1.0e-5f,
              "a Shift-drag moves only its own step, a fifth as far (" + String(moved, 3) + ")");
    }

    // double-click puts a step back
    se.mouseDoubleClick(mouse(at(0, bars), left, false));
    check(std::abs(getValue(*p, ids::step(0).toRawUTF8()) - bitvibe::mapping::pitchStepKnob(-7)) < 0.002f,
          "a double-click puts step 1 back as it started (-7 st)");

    // what each step controls: a click goes round follow, pitch, filter, rate
    click(at(1, StepEditor::typesArea()), left);
    check(roundToInt(getValue(*p, ids::stepType(1).toRawUTF8())) == 1, "a click on step 2's letter gives it its own type (pitch)");
    click(at(1, StepEditor::typesArea()), left);
    click(at(1, StepEditor::typesArea()), left);
    check(roundToInt(getValue(*p, ids::stepType(1).toRawUTF8())) == 3, "two more make it rate");
    click(at(1, StepEditor::typesArea()), left);
    check(roundToInt(getValue(*p, ids::stepType(1).toRawUTF8())) == 0, "and one more follows SEQ TYPE again");

    // right-click asks for the step's menu
    {
        int menuFor = -1;
        auto saved = se.onStepMenu;
        se.onStepMenu = [&](int s) { menuFor = s; };
        click(at(6, bars), right);
        check(menuFor == 6, "a right-click on step 7 asks for its menu");
        se.onStepMenu = saved;
    }

    // the lanes
    const auto tabs = StepEditor::tabsArea();
    const float tabW = tabs.getWidth() / (float) bitvibe::kNumLanes;
    for (int l = 0; l < bitvibe::kNumLanes; ++l)
    {
        click({ tabs.getX() + tabW * ((float) l + 0.5f), tabs.getCentreY() }, left);
        check(se.getLane() == l, String("the ") + StepEditor::laneTitle(l) + " tab shows its lane");
    }
    click({ tabs.getX() + tabW * 1.5f, tabs.getCentreY() }, left); // LEVEL
    const auto laneRow = StepEditor::laneArea();
    dragFromTo(at(2, laneRow, 0.95f), at(4, laneRow, 0.5f), left);
    auto lane = [&](int l, int step) { return getValue(*p, ids::lane(l, step).toRawUTF8()); };
    check(lane(1, 2) < 0.15f && lane(1, 4) > 0.35f && lane(1, 4) < 0.65f && std::abs(lane(1, 5) - 1.0f) < 1.0e-4f,
          "a drag in the LEVEL lane sets the steps it crosses (" + String(lane(1, 2), 2) + ", " + String(lane(1, 4), 2) + ")");
    se.mouseDoubleClick(mouse(at(2, laneRow), left, false));
    check(std::abs(lane(1, 2) - 1.0f) < 1.0e-4f, "a double-click puts a lane step back to neutral");
    click({ tabs.getX() + tabW * 5.5f, tabs.getCentreY() }, left); // STUTTER
    dragFromTo(at(3, laneRow), at(5, laneRow), left);
    check(lane(5, 3) > 0.5f && lane(5, 4) > 0.5f && lane(5, 5) > 0.5f && lane(5, 2) < 0.5f, "a drag across the STUTTER lane switches steps 4 to 6 on");
    click(at(4, laneRow), left);
    check(lane(5, 4) < 0.5f && lane(5, 3) > 0.5f, "a click switches one off again");

    // what the engine makes of it: twelve steps play
    {
        setValue(*p, ids::seqPlay, 5.0f);
        setValue(*p, ids::seqMult, 3.0f);
        setValue(*p, ids::pickTriggers, 0.0f);
        AudioBuffer<float> buffer(2, 512);
        MidiBuffer midi;
        int most = -1;
        for (int i = 0; i < 200; ++i)
        {
            buffer.clear();
            p->processBlock(buffer, midi);
            most = jmax(most, p->getEngine().getCurrentStep());
        }
        check(most == 11, "the engine plays the twelve steps (the last played is step " + String(most + 1) + ")");
    }

    // a picture for the README: sixteen steps, some of their own type, the LEVEL lane in use
    {
        setValue(*p, ids::seqLength, 16.0f);
        const int notes[16] { 0, 7, 12, 7, 3, 7, 12, 19, 0, -5, 0, 5, 7, 12, -12, 0 };
        for (int i = 0; i < 16; ++i)
        {
            setValue(*p, ids::step(i).toRawUTF8(), bitvibe::mapping::pitchStepKnob(jlimit(-12, 12, notes[i])));
            setValue(*p, ids::stepType(i).toRawUTF8(), 0.0f);
            setValue(*p, ids::lane(1, i).toRawUTF8(), i % 4 == 0 ? 1.0f : 0.45f + 0.04f * (float) (i % 4));
            setValue(*p, ids::lane(5, i).toRawUTF8(), i == 7 || i == 15 ? 1.0f : 0.0f);
        }
        setValue(*p, ids::stepType(7).toRawUTF8(), 2.0f);
        setValue(*p, ids::step(7).toRawUTF8(), 0.35f);
        setValue(*p, ids::stepType(13).toRawUTF8(), 3.0f);
        setValue(*p, ids::step(13).toRawUTF8(), 0.2f);
        setValue(*p, ids::step(14).toRawUTF8(), 1.0f); // muted
        setValue(*p, ids::loopOn, 1.0f);
        setValue(*p, ids::loopFrom, 12.0f);
        setValue(*p, ids::loopTo, 15.0f);
        se.setLane(1);
        se.mouseExit(mouse({ -10.0f, -10.0f }, left, false));
        se.refresh(-1);
        const auto shot = snapshot(*ed, 1.5f);
        const auto b = se.getBounds().expanded(16, 0).withTop(se.getY() - 52).withBottom(se.getBottom() + 8);
        const auto crop = shot.getClippedImage(Rectangle<int>(roundToInt(b.getX() * 1.5f), roundToInt(b.getY() * 1.5f), roundToInt(b.getWidth() * 1.5f),
                                                              roundToInt(b.getHeight() * 1.5f)));
        int lit = 0;
        for (int y = 0; y < crop.getHeight(); y += 2)
            for (int x = 0; x < crop.getWidth(); x += 2)
                lit += crop.getPixelAt(x, y).getBrightness() > 0.5f ? 1 : 0;
        check(lit > 1500, "the step editor draws its steps and lanes (" + String(lit) + " bright pixels)");
        const File file = outDir.getChildFile("steps.png");
        file.deleteFile();
        if (auto out = std::unique_ptr<FileOutputStream>(file.createOutputStream()))
            PNGImageFormat().writeImageToStream(crop, *out);
        std::printf("wrote %s (%d bytes)\n", file.getFullPathName().toRawUTF8(), (int) file.getSize());
    }
    ed.reset();

    // A preset slot saved when the sequence had six steps: its loop steps were stored as a fraction of six choices.
    {
        auto q = makeProcessor();
        setValue(*q, ids::loopOn, 1.0f);
        setValue(*q, ids::loopFrom, 2.0f);
        setValue(*q, ids::loopTo, 4.0f);
        q->saveCurrentPreset();
        MemoryBlock state;
        q->getStateInformation(state);
        auto xml = AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize());
        bool edited = false;
        if (xml != nullptr)
            if (auto* bank = xml->getChildByName("PRESETS"))
                if (auto* slot = bank->getFirstChildElement())
                {
                    slot->removeAttribute(ids::seqLength);
                    for (int l = 0; l < bitvibe::kNumLanes; ++l)
                        for (int i = 0; i < bitvibe::kNumSteps; ++i)
                            slot->removeAttribute(ids::lane(l, i));
                    slot->setAttribute(ids::loopFrom, 2.0 / 5.0); // how six choices stored step 3
                    slot->setAttribute(ids::loopTo, 4.0 / 5.0);   // and step 5
                    edited = true;
                }
        MemoryBlock oldState;
        if (xml != nullptr)
            AudioProcessor::copyXmlToBinary(*xml, oldState);
        auto r = makeProcessor();
        r->setStateInformation(oldState.getData(), (int) oldState.getSize());
        r->setCurrentProgram(1);
        r->setCurrentProgram(0);
        check(edited && roundToInt(getValue(*r, ids::loopFrom)) == 2 && roundToInt(getValue(*r, ids::loopTo)) == 4
                  && roundToInt(getValue(*r, ids::seqLength)) == 6,
              "an old slot keeps its loop section (steps " + String(roundToInt(getValue(*r, ids::loopFrom)) + 1) + " to "
                  + String(roundToInt(getValue(*r, ids::loopTo)) + 1) + ") and plays six steps");
        // and a slot saved now keeps its steps as they are
        auto s = makeProcessor();
        s->setStateInformation(state.getData(), (int) state.getSize());
        s->setCurrentProgram(1);
        s->setCurrentProgram(0);
        check(roundToInt(getValue(*s, ids::loopFrom)) == 2 && roundToInt(getValue(*s, ids::loopTo)) == 4, "a slot saved now loads as it was saved");
    }
}

// Writes the preset pack's files (BitVibeEditorSnapshot --write-presets <folder>).
int writePresetPack(const File& folder)
{
    folder.createDirectory();
    for (const auto& old : folder.findChildFiles(File::findFiles, false, "*.xml"))
        old.deleteFile();
    const auto& list = presetpack::presets();
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto file = folder.getChildFile(presetpack::fileName((int) i, list[i]));
        presetpack::build(list[i])->writeTo(file);
        std::printf("wrote %s\n", file.getFullPathName().toRawUTF8());
    }
    return 0;
}

// The preset pack: the files match the table they were written from, the plug-in carries all of them, and each one
// loads, uses something new (a length other than six, a lane, a stutter type or the tremolo) and makes a sound that is
// clearly the effect's, without clipping.
// FIND A PRESET: Ctrl+F opens it on every preset; a name or what a preset uses finds it, every word typed must match;
// typing plays nothing, Down plays the next result, Escape goes back, Return keeps the one highlighted.
void testPresetSearch(const File& outDir)
{
    std::puts("preset search");
    auto p = makeProcessor();
    std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(p->createEditor()));
    const int start = p->getCurrentProgram();
    const bool opened = ed->keyPressed(KeyPress('f', ModifierKeys::commandModifier, 0));
    auto* s = ed->getPresetSearch();
    check(opened && s != nullptr && s->isVisible(), "Ctrl+F opens FIND A PRESET");
    if (s == nullptr)
        return;
    check((int) s->results().size() == p->getNumPrograms(), "listing every preset (" + String((int) s->results().size()) + ")");
    auto names = [&]
    {
        StringArray n;
        for (int slot : s->results())
            n.add(p->getProgramName(slot));
        return n;
    };
    s->setQuery("talk box");
    check(names() == StringArray { "Talk Box" }, "a name finds its preset ('talk box': " + names().joinIntoString(", ") + ")");
    s->setQuery("flanger");
    check(names().contains("Jet Plane") && names().contains("Metal Flange") && !names().contains("Talk Box") && !names().contains("Phase Walk"),
          "what a preset uses finds every one that uses it ('flanger': " + names().joinIntoString(", ") + ")");
    save(*ed, outDir.getChildFile("preset_search.png"));
    s->setQuery("ping swing");
    check(names().contains("Kick the Filter") && names().contains("Vowel Ping") && !names().contains("Ping Pong"),
          "every word typed must match ('ping swing': " + names().joinIntoString(", ") + ")");
    check(p->getCurrentProgram() == start, "typing plays nothing yet");
    s->handleKey(KeyPress(KeyPress::downKey));
    check(s->results().size() > 1 && p->getCurrentProgram() == s->results()[1], "Down plays the next result");
    s->handleKey(KeyPress(KeyPress::escapeKey));
    check(!s->isVisible() && p->getCurrentProgram() == start, "Escape closes it and goes back to the preset it started on");
    ed->openPresetSearch();
    s->setQuery("jet");
    s->handleKey(KeyPress(KeyPress::returnKey));
    check(!s->isVisible() && p->getProgramName(p->getCurrentProgram()) == "Jet Plane", "Return keeps the one highlighted");
    bool inMenu = false;
    const auto menu = ed->presetMenu();
    PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        inMenu = inMenu || it.getItem().itemID == BitVibeEditor::kMenuSearch;
    check(inMenu, "and the preset menu opens it too");
}

void testPresetPack(const File& outDir)
{
    std::puts("preset pack");
    const auto& list = presetpack::presets();
    const File folder = outDir.getParentDirectory().getChildFile("Presets");
    int matching = 0;
    for (size_t i = 0; i < list.size(); ++i)
        if (auto saved = XmlDocument::parse(folder.getChildFile(presetpack::fileName((int) i, list[i]))))
            matching += saved->toString() == presetpack::build(list[i])->toString() ? 1 : 0;
    const int count = (int) list.size();
    check(count >= 20 && matching == count, "the " + String(count) + " files in Presets/ match the table in PresetPack.h (" + String(matching)
                                                   + "; run BitVibeEditorSnapshot --write-presets Presets after changing it)");
    check(BitVibeProcessor::numPackPresets() == count, "the plug-in carries all " + String(count) + " (" + String(BitVibeProcessor::numPackPresets()) + ")");

    // a guitar-like pluck every second, the one the pack is levelled on
    const auto dry = presetpack::pluckTestSignal();
    const double dryRms = dry.getRMSLevel(0, 0, dry.getNumSamples());

    int good = 0, matched = 0;
    double quietest = 1.0e9, loudest = 0.0;
    for (int i = 0; i < BitVibeProcessor::numPackPresets(); ++i)
    {
        auto p = makeProcessor();
        p->setCurrentProgram(bitvibe::kFirstPackSlot + i);
        const bool loaded = p->getCurrentProgram() == bitvibe::kFirstPackSlot + i;
        const bool named = p->getProgramName(p->getCurrentProgram()) == BitVibeProcessor::packPresetName(i);
        bool usesNew = roundToInt(getValue(*p, ids::seqLength)) != bitvibe::kDefaultSeqLength || roundToInt(getValue(*p, ids::stutterType)) != 0
                       || getValue(*p, ids::tremOn) > 0.5f || getValue(*p, ids::delayWobble) > 0.005f
                       || roundToInt(getValue(*p, ids::filterType)) != 0 || std::abs(getValue(*p, ids::filterRes) - bitvibe::mapping::kDefaultFilterRes) > 0.05f
                       || getValue(*p, ids::swing) > 0.005f || getValue(*p, ids::ringOn) > 0.5f || getValue(*p, ids::pingOn) > 0.5f
                       || getValue(*p, ids::modOn) > 0.5f;
        for (int l = 0; l < bitvibe::kNumLanes && !usesNew; ++l)
            for (int s = 0; s < bitvibe::kNumSteps; ++s)
                usesNew = usesNew || std::abs(getValue(*p, ids::lane(l, s).toRawUTF8()) - bitvibe::kLaneNeutral[(size_t) l]) > 0.005f;

        const auto wet = presetpack::playThrough(*p, dry);
        bool finite = true;
        double diff = 0.0;
        for (int n = 0; n < wet.getNumSamples(); ++n)
        {
            const float v = wet.getSample(0, n);
            finite = finite && std::isfinite(v);
            diff += (double) (v - dry.getSample(0, n)) * (v - dry.getSample(0, n));
        }
        diff = std::sqrt(diff / wet.getNumSamples());
        const float peak = wet.getMagnitude(0, wet.getNumSamples());
        const double rmsRatio = wet.getRMSLevel(0, 0, wet.getNumSamples()) / dryRms;
        const bool ok = loaded && named && usesNew && finite && peak < 1.0f && rmsRatio > 0.25 && rmsRatio < 3.0 && diff > 0.1 * dryRms;
        good += ok ? 1 : 0;
        // levelled: as loud as the dry pluck, within 1.5 dB, unless its peaks hold it down
        matched += (rmsRatio > 0.84 && rmsRatio < 1.19) || (rmsRatio <= 0.84 && peak > 0.8f * presetpack::kLevelPeakCeiling) ? 1 : 0;
        quietest = std::min(quietest, rmsRatio);
        loudest = std::max(loudest, rmsRatio);
        std::printf("    %-20s %s  peak %.2f  level x%.2f  change %.2f%s\n", BitVibeProcessor::packPresetName(i).toRawUTF8(), ok ? "ok  " : "FAIL", peak,
                    rmsRatio, diff / dryRms, usesNew ? "" : "  (nothing new)");
    }
    check(good == count, "every preset loads, uses a new feature and sounds without clipping (" + String(good) + " of " + String(count) + ")");
    check(matched == count, "and every one plays at the dry pluck's loudness, or as near as its peaks allow (" + String(matched) + " of "
                                + String(count) + "; levels x" + String(quietest, 2) + " to x" + String(loudest, 2) + ")");
}

// What makes a first run go right: the window fits the screen, effects that are on can be heard, the beat pattern
// looks off while it is off, and the sample blend says when there are no sounds to blend.
void testEasierStart()
{
    std::puts("first run");
    // the window fits the screen it opens on
    const Rectangle<int> fullHd(0, 0, 1920, 1040), laptop(0, 0, 1366, 728), big(0, 0, 3840, 2120);
    const float onFullHd = uisettings::fittingScale(1.0f, 948, 1122, fullHd);
    check(onFullHd < 1.0f && 1122.0f * onFullHd <= 1040.0f - 90.0f && onFullHd > 0.8f,
          "on a 1080p screen the window opens at " + String(roundToInt(onFullHd * 100.0f)) + "%, so all of it is on screen");
    check(std::abs(uisettings::fittingScale(1.0f, 948, 1122, laptop) - uisettings::kMinScale) < 1.0e-4f, "on a small laptop screen: the smallest size");
    check(std::abs(uisettings::fittingScale(1.0f, 948, 1122, big) - 1.0f) < 1.0e-4f
              && std::abs(uisettings::fittingScale(1.5f, 948, 1122, big) - 1.5f) < 1.0e-4f,
          "on a big screen: the size chosen");
    {
        auto p = makeProcessor();
        const auto keep = uisettings::screenAreaOverride();
        uisettings::screenAreaOverride() = fullHd;
        uisettings::saveWindowScale(1.25f);
        std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(p->createEditor()));
        check(ed->getHeight() <= 1040 - 90, "a new editor on that screen fits (" + String(ed->getWidth()) + " x " + String(ed->getHeight()) + ")");
        check(std::abs(uisettings::loadWindowScale() - 1.25f) < 1.0e-4f, "and the size chosen on a bigger screen is not forgotten");
        ed.reset();
        uisettings::screenAreaOverride() = keep;
        uisettings::saveWindowScale(1.0f);
    }

    // the reverb and the sample blend start off, and switching one on brings its mix up from silence
    auto p = makeProcessor();
    check(p->apvts.getRawParameterValue(ids::reverbOn)->load() < 0.5f && p->apvts.getRawParameterValue(ids::sampleOn)->load() < 0.5f,
          "the reverb and the sample blend start off, like the other effects");
    std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(p->createEditor()));
    ToggleButton* reverbOn = nullptr;
    SamplePanel* samples = nullptr;
    PatternPanel* pattern = nullptr;
    for (auto* c : ed->getContent().getChildren())
    {
        if (auto* b = dynamic_cast<ToggleButton*>(c); b != nullptr && b->getTooltip().startsWith("REVERB ON"))
            reverbOn = b;
        if (samples == nullptr)
            samples = dynamic_cast<SamplePanel*>(c);
        if (pattern == nullptr)
            pattern = dynamic_cast<PatternPanel*>(c);
    }
    if (reverbOn == nullptr || samples == nullptr || pattern == nullptr)
    {
        check(false, "the reverb ON button, the sample panel and the pattern panel are on the page");
        return;
    }
    check(p->apvts.getRawParameterValue(ids::reverbMix)->load() < 1.0e-6f, "the reverb mix starts at 0%");
    reverbOn->setToggleState(true, sendNotification);
    check(p->apvts.getRawParameterValue(ids::reverbOn)->load() > 0.5f && std::abs(p->apvts.getRawParameterValue(ids::reverbMix)->load() - 0.3f) < 0.01f,
          "REVERB ON with the mix at 0%: the mix comes up to 30%, so it can be heard");
    reverbOn->setToggleState(false, sendNotification);
    setValue(*p, ids::reverbMix, 0.6f);
    reverbOn->setToggleState(true, sendNotification);
    check(std::abs(p->apvts.getRawParameterValue(ids::reverbMix)->load() - 0.6f) < 0.01f, "a mix that is already up is left alone");
    ToggleButton* samplesOn = nullptr;
    for (auto* c : samples->getChildren())
        if (auto* b = dynamic_cast<ToggleButton*>(c); b != nullptr && b->getTooltip().startsWith("SAMPLES ON"))
            samplesOn = b;
    setValue(*p, ids::sampleMix, 0.0f);
    if (samplesOn != nullptr)
        samplesOn->setToggleState(true, sendNotification);
    check(samplesOn != nullptr && p->apvts.getRawParameterValue(ids::sampleOn)->load() > 0.5f
              && std::abs(p->apvts.getRawParameterValue(ids::sampleMix)->load() - 0.75f) < 0.01f,
          "SAMPLES ON with the mix at 0%: the mix comes up to 75%");

    // the beat pattern looks off while its ACTION is Off
    const auto allButtons = [&](auto test) {
        bool all = true;
        for (auto* c : pattern->getChildren())
            if (dynamic_cast<PedalButton*>(c) != nullptr)
                all = all && test(c->getAlpha());
        return all;
    };
    check(pattern->isDimmed() && allButtons([](float a) { return a < 0.99f; }),
          "the beat pattern starts with its ACTION at Off, its grid and buttons dimmed like a switched-off panel");
    auto beatPattern = p->getDrumBeat().getPattern();
    beatPattern.action = bitvibe::DrumAction::RestartSequence;
    p->getDrumBeat().setPattern(beatPattern);
    pattern->refresh();
    check(!pattern->isDimmed() && allButtons([](float a) { return a > 0.99f; }), "with an ACTION chosen it lights up");
    beatPattern.action = bitvibe::DrumAction::Off;
    p->getDrumBeat().setPattern(beatPattern);
    ed.reset();

    // no sounds at all: the sample blend says so and shows the sounds folder, in SIMPLE too
    const auto library = SampleBank::libraryFiles();
    const File soundsFolder = SampleBank::userFolder();
    const File empty = File::getSpecialLocation(File::tempDirectory).getChildFile("bitvibe_test_no_sounds");
    empty.deleteRecursively();
    empty.createDirectory();
    SampleBank::setSoundsFolderOverride(empty);
    SampleBank::setRepoSoundsEnabled(false);
    {
        PedalLookAndFeel lookAndFeel;
        auto q = makeProcessor();
        SamplePanel panel(*q, lookAndFeel);
        panel.setAdvanced(false);
        PedalButton* choose = nullptr;
        PedalButton* folder = nullptr;
        for (auto* c : panel.getChildren())
            if (auto* b = dynamic_cast<PedalButton*>(c))
            {
                if (b->getText().startsWith("NO SOUNDS") || b->getText().startsWith("CHOOSE"))
                    choose = b;
                if (b->getText().contains("FOLDER"))
                    folder = b;
            }
        check(panel.hasNoSounds() && choose != nullptr && choose->getText() == "NO SOUNDS YET", "with no sound files the panel says NO SOUNDS YET");
        check(folder != nullptr && folder->isVisible() && folder->getText().startsWith("ADD SOUNDS"),
              "and shows ADD SOUNDS: OPEN THE FOLDER, in SIMPLE too");
        if (!library.isEmpty())
        {
            library[0].copyFileTo(empty.getChildFile(library[0].getFileName()));
            panel.lookForNewSounds(true);
            panel.refresh();
            check(!panel.hasNoSounds() && q->getSampleBank().getNumSelected() == 1 && choose != nullptr
                      && choose->getText().startsWith("CHOOSE SOUNDS (1 OF 1)"),
                  "a sound added to the folder is found and picked (" + (choose != nullptr ? choose->getText() : String()) + ")");
            check(folder != nullptr && !folder->isVisible(), "and the folder button goes back to ADVANCED");
        }
    }
    SampleBank::setRepoSoundsEnabled(true);
    SampleBank::setSoundsFolderOverride(soundsFolder);
    empty.deleteRecursively();
    makeProcessor()->getSampleBank().selectAll();
}

// The window can be resized: the content is scaled to the window, the shape stays, and the size is remembered.
void testResizing()
{
    std::puts("resizable window");
    auto p = makeProcessor();
    std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(p->createEditor()));
    const int w = ed->getWidth(), h = ed->getHeight();
    check(w == 948 && h == 818, "the editor opens at its designed size (" + String(w) + " x " + String(h) + ")");
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
    check(advanced == simple + 8, "ADVANCED shows the stutter's length, type and hold, the filter's type and resonance, SWING and the PING (" + String(simple) + " then " + String(advanced) + ")");
    check(ed->getEffectTab() == BitVibeEditor::kTabSamples, "the sample blend is the effect panel showing first");
    {
        // the effect tabs: one panel at a time, ADVANCED adds the extra controls of the one showing
        int extra = 0;
        for (const auto& [tab, more] : { std::pair { BitVibeEditor::kTabDist, 2 }, std::pair { BitVibeEditor::kTabDelay, 2 }, std::pair { BitVibeEditor::kTabReverb, 4 } })
        {
            ed->setRightMode(false);
            ed->setEffectTab(tab);
            const int few = countRight(*ed);
            ed->setRightMode(true);
            extra += countRight(*ed) - few == more + 8 ? 1 : 0;
        }
        check(extra == 3, "each of the distortion, delay and reverb tabs shows its extra controls in ADVANCED (" + String(extra) + " of 3)");
        ed->setEffectTab(BitVibeEditor::kTabEq);
        check(ed->getEffectTab() == BitVibeEditor::kTabEq && uisettings::effectTab() == BitVibeEditor::kTabEq, "the EQ tab shows in ADVANCED, and the tab is remembered");
        ed->setRightMode(false);
        check(ed->getEffectTab() == BitVibeEditor::kTabSamples, "SIMPLE has no EQ tab: back to the sample blend");
        ed->setRightMode(true);
        ed->setEffectTab(BitVibeEditor::kTabReverb);
        save(*ed, outDir.getChildFile("editor_effects.png")); // the reverb tab: the graph and six knobs in two rows
        ed->setEffectTab(BitVibeEditor::kTabMod);
        save(*ed, outDir.getChildFile("editor_mod.png")); // the MOD tab: the ring mod beside the phaser / flanger
        ed->setEffectTab(BitVibeEditor::kTabSamples);
    }
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

// Sounds in subfolders of the sounds folder are found, and the chooser groups them by their folder.
void testSoundSubfolders()
{
    std::puts("sound subfolders");
    const auto files = SampleBank::libraryFiles();
    int inSubfolders = 0, wrongGroup = 0;
    for (const auto& f : files)
    {
        const auto group = SampleBank::groupOf(f);
        if (group.isNotEmpty())
        {
            ++inSubfolders;
            wrongGroup += f.getParentDirectory().getFileName() == group.fromLastOccurrenceOf("/", false, false) ? 0 : 1;
        }
    }
    StringArray names;
    for (const auto& f : files)
        names.add(f.getFileName());
    names.removeDuplicates(false);
    check(names.size() == files.size(), "every sound is listed once (" + String(files.size()) + " sounds)");
    check(inSubfolders >= 1 && wrongGroup == 0, String(inSubfolders) + " sound(s) in subfolders are found and grouped by their folder");
    bool foundTestTone = false;
    for (const auto& f : files)
        foundTestTone = foundTestTone || (f.getFileName() == "zz_test_tone.wav" && SampleBank::groupOf(f) == "Test Folder");
    check(foundTestTone, "the test sound in \"Test Folder\" is found, in that group");
}

// The sampler: a recording, a picked part, the effects, PLAY (the preview player) and SAVE (a WAV in Sounds/Sampled,
// marked to keep its level, added to a bank); and the program recorder starting and stopping (Windows).
void testSampler(const File& outDir)
{
    std::puts("sampler");
    auto p = makeProcessor();
    std::unique_ptr<BitVibeEditor> ed(static_cast<BitVibeEditor*>(p->createEditor()));
    ed->showSampler(true);
    auto* page = ed->getSamplerPage();
    check(page != nullptr && page->isVisible(), "the SAMPLER opens over the window");

    // two seconds: a quiet second, then a second of tone
    std::vector<float> l(96000, 0.0f), r(96000, 0.0f);
    for (size_t i = 48000; i < 96000; ++i)
        l[i] = r[i] = 0.25f * std::sin(6.2831853f * 220.0f * (float) i / 48000.0f);
    page->setRecording(l, r);
    check(page->getSelection() == Range<int64>(0, 96000), "a new recording is picked whole");
    page->setSelection(60000, 84000);
    std::vector<float> outL, outR;
    page->renderSelection(outL, outR);
    float peak = 0.0f;
    for (float v : outL)
        peak = jmax(peak, std::abs(v));
    check(outL.size() == 24000 && std::abs(peak - bitvibe::kSoundPeak) < 0.01f, "the picked half second comes out levelled (" + String(peak, 3) + ")");
    setValue(*p, ids::smpVolume, -6.0f);
    setValue(*p, ids::smpBits, 0.0f);
    setValue(*p, ids::smpDistType, 2.0f);
    std::vector<float> fxL, fxR;
    page->renderSelection(fxL, fxR);
    float fxPeak = 0.0f, diff = 0.0f;
    for (size_t i = 0; i < fxL.size(); ++i)
    {
        fxPeak = jmax(fxPeak, std::abs(fxL[i]));
        diff = jmax(diff, std::abs(fxL[i] - outL[i] * 0.5f));
    }
    check(std::abs(fxPeak - bitvibe::kSoundPeak * 0.501f) < 0.01f && diff > 0.05f, "fuzz, 1 bit and -6 dB change it (peak " + String(fxPeak, 3) + ")");
    setValue(*p, "smpEqOn", 1.0f);
    setValue(*p, "smpEq4Gain", -15.0f);
    check(p->getSamplerFx().eqOn && std::abs(p->getSamplerFx().eqBands[3].gainDb + 15.0f) < 0.01f && getValue(*p, ids::eqOn) < 0.5f,
          "the sampler's EQ has settings of its own (the main EQ stays off)");
    save(*ed, outDir.getChildFile("editor_sampler.png"));

    // PLAY: the preview player adds the sound to the output, STOP ends it
    p->prepareToPlay(48000.0, 512);
    p->setSamplerPreview(std::vector<float>(48000, 0.5f), std::vector<float>(48000, -0.5f), 48000.0);
    p->playSamplerPreview();
    MidiBuffer midi;
    AudioBuffer<float> block(2, 512);
    block.clear();
    setValue(*p, ids::seqPlay, 0.0f);
    p->processBlock(block, midi);
    check(std::abs(block.getSample(0, 300) - 0.5f) < 0.05f && std::abs(block.getSample(1, 300) + 0.5f) < 0.05f && p->getSamplerPreviewSeconds() > 0.0f,
          "PLAY: the preview is heard, left and right as they are (" + String(block.getSample(0, 300), 3) + ", " + String(block.getSample(1, 300), 3)
              + ", at " + String(p->getSamplerPreviewSeconds(), 3) + " s)");
    p->stopSamplerPreview();
    block.clear();
    p->processBlock(block, midi);
    check(block.getMagnitude(0, 512) < 1.0e-4f && p->getSamplerPreviewSeconds() < 0.0f, "STOP: silence again");
    {
        // LOOP: a 5 ms sound keeps going round; the sampler EQ's spectrum feed follows it
        std::vector<float> beforeEq(240, 0.25f);
        p->setSamplerPreview(std::vector<float>(240, 0.5f), std::vector<float>(240, 0.5f), 48000.0, beforeEq);
        p->setSamplerPreviewLoop(true);
        p->playSamplerPreview();
        for (int i = 0; i < 4; ++i)
        {
            block.clear();
            p->processBlock(block, midi);
        }
        std::vector<float> b(1024), a(1024);
        const bool feeding = p->readSamplerScope(b.data(), a.data(), 1024);
        check(block.getMagnitude(0, 512) > 0.4f && p->getSamplerPreviewSeconds() >= 0.0f, "LOOP: a 5 ms sound is still playing after 2048 samples");
        check(feeding && std::abs(b[1000] - 0.25f) < 0.01f && std::abs(a[1000] - 0.5f) < 0.01f, "the spectrum feed has the sound before and after the EQ");
        p->setSamplerPreviewLoop(false);
        p->stopSamplerPreview();
        block.clear();
        p->processBlock(block, midi);
        check(!p->readSamplerScope(b.data(), a.data(), 1024) && b[1000] == 0.0f, "stopped: the feed is silent");
    }

    // SAVE: into Sounds/Sampled, in a bank, keeping its level
    auto& bank = p->getSampleBank();
    bank.setSelected({ "zz_test_tone.wav" }, true);
    bank.saveBank("Sampler test bank");
    String message;
    const bool savedOk = page->save("My Test Sound", "Sampler test bank", message);
    check(savedOk, "SAVE SOUND: " + message);
    const auto saved = BitVibeProcessor::sampledFolder().getChildFile("My Test Sound.wav");
    check(saved.existsAsFile() && bank.getBankFiles("Sampler test bank").contains("My Test Sound.wav")
              && bank.getSelected().contains("My Test Sound.wav"),
          "it is in Sounds/Sampled, in the bank, and in the list in use (that bank is showing)");
    AudioFormatManager formats;
    formats.registerBasicFormats();
    if (auto reader = std::unique_ptr<AudioFormatReader>(formats.createReaderFor(saved)))
        check(reader->numChannels == 2 && reader->lengthInSamples == 24000
                  && reader->metadataValues.getValue(WavAudioFormat::bwavDescription, {}) == SampleBank::kKeepLevelMark,
              "the file: stereo, the picked length, marked so the sample blend keeps its level");
    else
        check(false, "the saved sound could not be read back");
    check(page->save("My Test Sound", {}, message) && BitVibeProcessor::sampledFolder().getChildFile("My Test Sound 2.wav").existsAsFile(),
          "saving under a name that is taken adds a number: " + message);
    page->setRecording({}, {});
    check(!page->save("Nothing", {}, message) && message.isNotEmpty(), "with nothing recorded there is nothing to save");

    // the recorder (Windows): the list, and starting and stopping a recording of everything but this program
    const auto sources = AppAudioCapture::listSources();
    if (AppAudioCapture::isAvailable())
    {
        check(sources.size() >= 1 && sources[0].exclude, "the program list starts with \"" + (sources.isEmpty() ? String() : sources[0].name) + "\"");
        String error;
        auto& capture = p->getAppCapture();
        const bool started = capture.start(sources[0], error);
        check(started && capture.isRecording(), "a recording starts" + (error.isEmpty() ? String() : ": " + error));
        Thread::sleep(400);
        capture.stop();
        std::vector<float> rl, rr;
        capture.takeRecording(rl, rr);
        check(!capture.isRecording() && rl.size() == rr.size(), "and stops (" + String((int) rl.size()) + " samples while nothing may have been playing)");
    }
    else
    {
        std::puts("  (recording programs is not available on this system: recorder not checked)");
    }
    ed->showSampler(false);
}

// DOG5: the sprite sheet is built in, and the frame follows the beat phase (the head nods forward on the beat).
void testBeatDog()
{
    std::puts("beat dog");
    PedalLookAndFeel lookAndFeel;
    BeatDog dog(lookAndFeel);
    dog.setLive(false);
    dog.setSize(456, 164);
    check(dog.hasSheet(), "the headbang frames are built into the plug-in");
    dog.setBeat(0.0f, 0);
    const auto onBeat = snapshot(dog, 1.0f);
    check(dog.getFrame() <= 1 || dog.getFrame() >= 28, "on the beat: the head pulled back, about to nod (frame " + String(dog.getFrame()) + ")");
    dog.setBeat(0.15f, 1);
    check(dog.getFrame() >= 16 && dog.getFrame() <= 19, "a moment later: nodded forward (frame " + String(dog.getFrame()) + ")");
    dog.setBeat(0.5f, 2);
    const auto between = snapshot(dog, 1.0f);
    check(dog.getFrame() > 19 && dog.getFrame() < 31, "halfway to the next beat: pulling back (frame " + String(dog.getFrame()) + ")");
    int changed = 0;
    for (int y = 0; y < onBeat.getHeight(); y += 2)
        for (int x = 0; x < onBeat.getWidth(); x += 2)
            changed += onBeat.getPixelAt(x, y) != between.getPixelAt(x, y) ? 1 : 0;
    check(changed > 200, "the picture moves (" + String(changed) + " pixels differ)");
    dog.setBeat(0.9999f, 3);
    check(dog.getFrame() >= 28, "the last moment before a beat: pulled back");

    // LIVE 3D: drawn from the mesh at the frame rate chosen, both remembered
    check(beatdog::kNumVerts > 100 && beatdog::kNumEdges > 300 && beatdog::kNumFaces > 200,
          "the head's mesh is built in (" + String(beatdog::kNumVerts) + " points, " + String(beatdog::kNumEdges) + " lines)");
    dog.setLive(true);
    check(dog.isLive() && dog.isTimerRunning() && dog.getTimerInterval() <= 17, "LIVE 3D on: drawn 60 times a second by default");
    dog.renderLiveFrame(0.0f);
    const auto down = dog.getLiveImage().createCopy();
    dog.renderLiveFrame(0.5f);
    const auto up = dog.getLiveImage().createCopy();
    int lit = 0, moved = 0;
    for (int y = 0; y < down.getHeight(); ++y)
        for (int x = 0; x < down.getWidth(); ++x)
        {
            lit += down.getPixelAt(x, y).getGreen() > 120 ? 1 : 0;
            moved += down.getPixelAt(x, y) != up.getPixelAt(x, y) ? 1 : 0;
        }
    check(lit > 1500, "the live frame draws the wireframe (" + String(lit) + " bright pixels)");
    check(moved > 1000, "and the head moves between the beat and halfway (" + String(moved) + " pixels differ)");
    {
        const auto start = Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 120; ++i)
            dog.renderLiveFrame((float) i / 120.0f);
        const double ms = (Time::getMillisecondCounterHiRes() - start) / 120.0;
        check(ms < 8.0, "a live frame takes " + String(ms, 2) + " ms to draw (" + String(ms * 60.0 / 10.0, 1) + "% of one core at 60 FPS)");
    }
    dog.setBeat(0.0f, 0, 128.0f);
    dog.renderLiveFrame(0.0f);
    const auto liveFile = File::getSpecialLocation(File::tempDirectory).getChildFile("bitvibe_beat_dog_live.png");
    liveFile.deleteFile();
    if (auto out = std::unique_ptr<FileOutputStream>(liveFile.createOutputStream()))
        PNGImageFormat().writeImageToStream(snapshot(dog, 2.0f), *out);
    dog.setFrameRate(30);
    check(dog.getFrameRate() == 30 && dog.getTimerInterval() >= 32 && dog.getTimerInterval() <= 34, "30 FPS: the timer runs at 30");
    dog.setFrameRate(15);
    check(dog.getFrameRate() == 15 && dog.getTimerInterval() >= 65 && dog.getTimerInterval() <= 68, "15 FPS");
    check(uisettings::beatDogLive() && uisettings::beatDogFps() == 15, "both remembered for next time");
    {
        BeatDog another(lookAndFeel);
        check(another.isLive() && another.getFrameRate() == 15, "a new editor opens with them");
    }
    dog.setLive(false);
    check(!dog.isTimerRunning() && !uisettings::beatDogLive(), "LIVE 3D off: the timer stops, back to the pre-rendered frames");
    dog.setFrameRate(60);

    // the picked beats: the head nods on those only, pulling back in between
    const auto near = [](float a, float b) { return std::abs(a - b) < 0.01f; };
    dog.setBeatMask(0x1);
    check(near(dog.motionAt(0.0f, 0, 16).since, 0.0f) && near(dog.motionAt(0.0f, 0, 16).gap, 4.0f)
              && near(dog.motionAt(0.0f, 2, 16).between, 0.5f) && near(dog.motionAt(0.0f, 2, 16).since, 2.0f),
          "beat 1 only: one headbang a bar");
    dog.setBeat(0.0f, 2);
    check(dog.getFrame() > 19 && dog.getFrame() < 31, "the pre-rendered frames follow it (frame " + String(dog.getFrame()) + " halfway through the bar)");
    dog.setBeatMask(0x5);
    check(near(dog.motionAt(0.0f, 2, 16).since, 0.0f) && near(dog.motionAt(0.0f, 1, 16).between, 0.5f), "beats 1 and 3: two a bar");
    dog.setBeatMask(0x9);
    check(near(dog.motionAt(0.0f, 0, 12).since, 0.0f) && near(dog.motionAt(0.5f, 1, 12).between, 0.5f),
          "a beat the bar does not have (beat 4 of 3/4) is left out");
    dog.setBeatMask(0);
    check(dog.motionAt(0.0f, 0, 16).gap == 0.0f && near(dog.headAngle(dog.motionAt(0.3f, 2, 16)), beatdog::kHeadAngle[0]),
          "no beats picked: the head waits, pulled back");
    check(uisettings::beatDogBeats() == 0, "the picked beats are remembered");
    dog.setBeatMask(0x3f);

    // the sway lands on one side at a picked beat and on the other at the next
    const float firstSide = dog.motionAt(0.0f, 0, 16).side;
    const float midway = dog.motionAt(0.5f, 0, 16).side;
    dog.motionAt(0.9f, 0, 16);
    const float nextSide = dog.motionAt(0.0f, 1, 16).side;
    check(std::abs(firstSide) > 0.99f && std::abs(midway) < 0.01f && near(nextSide, -firstSide) && dog.motionAt(0.5f, 1, 16).lift > 0.99f,
          "the sway swings from one side to the other between picked beats, floating up in the middle");

    // both move the live picture
    const auto liveAt = [&](float smooth, float swayAmount, float phase) {
        dog.setSmoothness(smooth);
        dog.setSway(swayAmount);
        dog.renderLiveFrame(phase, 0, 16);
        return dog.getLiveImage().createCopy();
    };
    const auto differ = [](const Image& a, const Image& b) {
        int n = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                n += a.getPixelAt(x, y) != b.getPixelAt(x, y) ? 1 : 0;
        return n;
    };
    const int bySmooth = differ(liveAt(0.0f, 0.0f, 0.3f), liveAt(1.0f, 0.0f, 0.3f));
    const int bySway = differ(liveAt(0.0f, 0.0f, 0.1f), liveAt(0.0f, 1.0f, 0.1f));
    check(bySmooth > 500 && bySway > 500, "SMOOTH and SWAY move the live head (" + String(bySmooth) + " and " + String(bySway) + " pixels)");
    dog.setSmoothness(0.3f);
    dog.setSway(0.8f);
    dog.saveSettings();
    {
        BeatDog another(lookAndFeel);
        check(near(another.getSmoothness(), 0.3f) && near(another.getSway(), 0.8f), "the knobs are remembered");
    }
    dog.setLive(true);
    dog.setBeatMask(0x5);
    dog.setBeat(0.0f, 0, 128.0f);
    dog.renderLiveFrame(0.6f, 1, 16);
    const auto controlsFile = File::getSpecialLocation(File::tempDirectory).getChildFile("bitvibe_beat_dog_controls.png");
    controlsFile.deleteFile();
    if (auto out = std::unique_ptr<FileOutputStream>(controlsFile.createOutputStream()))
        PNGImageFormat().writeImageToStream(snapshot(dog, 2.0f), *out);
    dog.setLive(false);
    dog.setBeatMask(0x3f);

    // the headbang: on the beat the head nods forward (the snout down: the angle goes negative), then pulls back
    // smoothly; SMOOTH sets how much quicker the nod is, down to none
    dog.setSway(0.0f);
    const auto angleAt = [&](float since) {
        BeatDog::Motion m;
        m.since = m.between = since;
        return dog.headAngle(m);
    };
    const auto fastest = [&](float from, float to, float sign) { // the quickest move one way, in radians a beat
        float most = 0.0f;
        for (float p = from; p < to - 0.005f; p += 0.005f)
            most = jmax(most, sign * (angleAt(p + 0.005f) - angleAt(p)) / 0.005f);
        return most;
    };
    float nodSpeed[3], backSpeed[3];
    for (int i = 0; i < 3; ++i)
    {
        dog.setSmoothness(0.5f * (float) i);
        nodSpeed[i] = fastest(0.0f, 1.0f, -1.0f);
        backSpeed[i] = fastest(0.0f, 1.0f, 1.0f);
    }
    dog.setSmoothness(0.0f);
    check(nodSpeed[0] > 3.0f * backSpeed[0], "SMOOTH left: the nod forward is sudden and the pull back smooth (fastest nod "
              + String(nodSpeed[0], 1) + ", fastest pull back " + String(backSpeed[0], 1) + " a beat)");
    check(angleAt(0.02f) < angleAt(0.0f) && angleAt(0.15f) < angleAt(0.5f) && angleAt(0.5f) < angleAt(0.0f),
          "on the beat the head is pulled back and starts forward, nodded right forward a moment later");
    check(std::abs(angleAt(0.0f) - angleAt(0.9999f)) < 0.02f, "and is pulled back again for the next beat");
    check(nodSpeed[1] < nodSpeed[0] && nodSpeed[1] > nodSpeed[2], "SMOOTH halfway: a gentler nod (fastest " + String(nodSpeed[1], 1) + ")");
    check(std::abs(nodSpeed[2] - backSpeed[2]) < 0.05f * backSpeed[2], "SMOOTH right: forward and back alike (fastest "
              + String(nodSpeed[2], 2) + " and " + String(backSpeed[2], 2) + ")");
    BeatDog::Motion nodding, pulling;
    nodding.since = 0.08f;
    pulling.since = 0.5f;
    const int noddingFrame = dog.frameFor(nodding), pullingFrame = dog.frameFor(pulling);
    check(noddingFrame <= 19 && pullingFrame >= 19, "the pre-rendered frames follow it, going the right way (frames "
              + String(noddingFrame) + " nodding, " + String(pullingFrame) + " pulling back)");
    dog.setSmoothness(1.0f);
    const int evenFrame = dog.frameFor(pulling);
    dog.setSmoothness(0.0f);
    check(evenFrame != dog.frameFor(pulling), "and SMOOTH moves them too (frame " + String(evenFrame) + " for an even bob)");
    dog.setSmoothness(0.0f);
    dog.setSway(0.5f);
    dog.saveSettings();
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
                                 || dynamic_cast<StepEditor*>(c) != nullptr || dynamic_cast<StepStrip*>(c) != nullptr
                                 || dynamic_cast<OutputMeter*>(c) != nullptr;
        if (!interactive)
            continue;
        ++controls;
        auto* tip = dynamic_cast<TooltipClient*>(c);
        const String text = tip != nullptr ? tip->getTooltip() : String();
        missing += text.isEmpty() ? 1 : 0;
        tooLong += text.length() > 260 ? 1 : 0;
        if (text.startsWith("THE STEPS:"))
            stepOneTips.add(text);
        if (text.isEmpty())
            std::printf("    no tooltip on child %d (%s)\n", i, typeid(*c).name());
    }
    check(controls == 77 && missing == 0, String(controls) + " controls, " + String(missing) + " without a tooltip (expected 77 and 0)");
    check(tooLong == 0, "every tooltip is short enough to read at a glance (none over 260 characters)");
    check(stepOneTips.size() == 1 && stepOneTips[0].containsIgnoreCase(expectedStepWord),
          "the step editor describes the " + expectedStepWord + " sequencer: '" + (stepOneTips.isEmpty() ? String() : stepOneTips[0]) + "'");

    // What a tooltip looks like in the editor's own style: show one for the MIX knob.
    if (imageFile != nullptr)
    {
        Point<int> mixKnob;
        for (int i = 0; i < pane.getNumChildComponents(); ++i)
            if (auto* tip = dynamic_cast<TooltipClient*>(pane.getChildComponent(i)); tip != nullptr && tip->getTooltip().startsWith("DRY/WET:"))
                mixKnob = pane.getChildComponent(i)->getBounds().getCentre();
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
        check(p->getParameters().size() == 361, "361 parameters (" + String(p->getParameters().size()) + ")");
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
    // and away from the player's sounds folder (the sampler writes into it): a test folder with one sound in a subfolder
    const File testSounds = File::getSpecialLocation(File::tempDirectory).getChildFile("bitvibe_test_sounds");
    testSounds.deleteRecursively();
    testSounds.getChildFile("Test Folder").createDirectory();
    SampleBank::setSoundsFolderOverride(testSounds);
    {
        // a short tone as a WAV, in the subfolder
        AudioBuffer<float> tone(1, 4800);
        for (int i = 0; i < 4800; ++i)
            tone.setSample(0, i, 0.5f * std::sin(MathConstants<float>::twoPi * 440.0f * (float) i / 48000.0f));
        std::unique_ptr<OutputStream> out = std::make_unique<FileOutputStream>(testSounds.getChildFile("Test Folder").getChildFile("zz_test_tone.wav"));
        if (auto writer = WavAudioFormat().createWriterFor(out, AudioFormatWriterOptions().withSampleRate(48000.0).withNumChannels(1).withBitsPerSample(16)))
            writer->writeFromAudioSampleBuffer(tone, 0, 4800);
    }
    uisettings::screenAreaOverride() = Rectangle<int>(0, 0, 3840, 2160); // a big screen: the editor opens at its designed size
    uisettings::markHelpSeen(); // the quick-start help would otherwise cover the screenshots
    uisettings::setAdvancedRight(true); // and the screenshots show every control
    if (String(argv[1]) == "--write-presets")
        return writePresetPack(File::getCurrentWorkingDirectory().getChildFile(argc > 2 ? argv[2] : "Presets"));
    const File outDir(File::getCurrentWorkingDirectory().getChildFile(argv[1]));
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
        std::array<StepStrip::Step, bitvibe::kNumSteps> steps {};
        strip.set(steps, 6, 4);
        const auto without = snapshot(strip, 2.0f);
        strip.set(steps, 6, 4, 5, 3);
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
    testSoundSubfolders();
    testSampler(outDir);
    testBeatDog();
    testPresetFiles();
    testBeatRouting();
    testEasierStart();
    testLayout(outDir);
    testThemes(outDir);
    testPageTools();
    testTremoloPanel(outDir);
    testVisuals(outDir);
    testStepEditor(outDir);
    testPresetPack(outDir);
    testPresetSearch(outDir);
    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL OK" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
