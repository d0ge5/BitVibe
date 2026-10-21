// Instantiates the processor and editor directly (no plugin host), renders the GUI to PNG files so the
// look can be checked without opening a DAW, and runs the processor-level checks that a host cannot
// reach (capturing the expression-morph sets, renaming and saving presets, state round trips).
//   BitVibeEditorSnapshot <output directory>
// Exit code 0 means every check passed.

#include "PluginEditor.h"
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

void save(BitVibeEditor& ed, const File& file)
{
    auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.5f);
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
    for (int i = 0; i < ed.getNumChildComponents(); ++i)
    {
        auto* c = ed.getChildComponent(i);
        const bool interactive = dynamic_cast<Slider*>(c) != nullptr || dynamic_cast<Button*>(c) != nullptr
                                 || dynamic_cast<ComboBox*>(c) != nullptr || dynamic_cast<Label*>(c) != nullptr
                                 || dynamic_cast<FootSwitch*>(c) != nullptr || dynamic_cast<PedalButton*>(c) != nullptr;
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
    check(controls == 34 && missing == 0, String(controls) + " controls, " + String(missing) + " without a tooltip (expected 34 and 0)");
    check(tooLong == 0, "every tooltip is short enough to read at a glance (none over 260 characters)");
    check(stepOneTips.size() == 1 && stepOneTips[0].containsIgnoreCase(expectedStepWord),
          "the step knobs describe the " + expectedStepWord + " sequencer: '" + (stepOneTips.isEmpty() ? String() : stepOneTips[0]) + "'");

    // What a tooltip looks like in the editor's own style: show one for the MIX knob.
    if (imageFile != nullptr)
    {
        const Point<int> mixKnob(240, 685);
        // The headless editor is never marked visible, which makes getComponentAt() return nothing, so find the
        // topmost visible child under the point the same way hit-testing would.
        Component* under = nullptr;
        for (int i = ed.getNumChildComponents(); --i >= 0;)
            if (auto* c = ed.getChildComponent(i); c->isVisible() && c->getBounds().contains(mixKnob))
            {
                under = c;
                break;
            }
        auto* tip = dynamic_cast<TooltipClient*>(under);
        check(tip != nullptr && tip->getTooltip().startsWith("MIX:"),
              "the control under the MIX knob answers with the MIX description");
        if (tip != nullptr)
        {
            TooltipWindow window(&ed, 600);
            window.displayTip(ed.localPointToGlobal(mixKnob.translated(10, -30)), tip->getTooltip());
            save(ed, *imageFile);
        }
    }
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

    testPresetsAndMorph();
    testMixAndTriggerOnly();
    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL OK" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
