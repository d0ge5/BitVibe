#pragma once

// The preset pack: twenty sounds built around the 16-step sequencer, its lanes, the stutter types and the tremolo.
// BitVibeEditorSnapshot --write-presets <folder> writes them as preset files (Presets/ in the repository, which the
// plug-in embeds and lists under Preset pack); the snapshot tests check that the files still match this table and
// that every preset loads and sounds.
//
// Each preset starts from a fresh plug-in (every setting at its default) and says only what it changes. A picked note
// restarts the sequence, so the long patterns switch PATTERN LOCK on: with the beat pattern off they then run freely
// (a pick still fires the stutter) instead of starting over before they reach their later steps.

#include "PluginProcessor.h"

#include <functional>
#include <initializer_list>
#include <vector>

namespace presetpack
{
using bitvibe::Lane;

// Plain values, by parameter.
struct Builder
{
    BitVibeProcessor& p;

    void set(const juce::String& id, float plain)
    {
        if (auto* prm = p.apvts.getParameter(id))
            prm->setValueNotifyingHost(prm->convertTo0to1(plain));
    }
    void on(const char* id) { set(id, 1.0f); }
    void freeRunning() { on(ids::patternLock); }

    // The step knobs, from step 1 (the rest are left as they are).
    void steps(std::initializer_list<float> knobs)
    {
        int i = 0;
        for (float k : knobs)
            set(ids::step(i++), k);
    }
    // A step's own type: 1 = pitch, 2 = filter, 3 = sample rate (0 = follow SEQ TYPE).
    void types(std::initializer_list<int> own)
    {
        int i = 0;
        for (int t : own)
            set(ids::stepType(i++), (float) t);
    }
    // One lane's values, from step 1 (repeated to fill sixteen steps when fewer are given).
    void lane(Lane lane, std::initializer_list<float> values)
    {
        const std::vector<float> v(values);
        for (int i = 0; i < bitvibe::kNumSteps; ++i)
            set(ids::lane((int) lane, i), v[(size_t) i % v.size()]);
    }
    void sequence(int length, int play, int mult) // play: 0 off .. 5 continuous; mult: 0 = x1, 1 = x2, 2 = x4 ...
    {
        set(ids::seqLength, (float) length);
        set(ids::seqPlay, (float) play);
        set(ids::seqMult, (float) mult);
    }
    void reverb(int type, float mix, float decay, bool reverse = false)
    {
        static const char* reverseIds[6] { ids::reverbReverseRoom, ids::reverbReverseHall, ids::reverbReverseSpring,
                                           ids::reverbReversePlate, ids::reverbReverseShimmer, ids::reverbReverseGated };
        on(ids::reverbOn);
        set(ids::reverbType, (float) type);
        set(ids::reverbMix, mix);
        set(ids::reverbDecay, decay);
        if (reverse)
            on(reverseIds[type]);
    }
    void delay(int division, float mix, float decay, int repeats)
    {
        on(ids::delayOn);
        set(ids::delayTime, (float) division);
        set(ids::delayMix, mix);
        set(ids::delayDecay, decay);
        set(ids::delayRepeats, (float) repeats);
    }
    void dist(int type, float drive, float tone, float level)
    {
        on(ids::distOn);
        set(ids::distType, (float) type);
        set(ids::distDrive, drive);
        set(ids::distTone, tone);
        set(ids::distLevel, level);
    }
    void tremolo(float rateHz, float depth, float edge, float endOctaves = 0.0f, float bend = 0.0f, int ramp = 2)
    {
        on(ids::tremOn);
        set(ids::tremRate, rateHz);
        set(ids::tremDepth, depth);
        set(ids::tremEdge, edge);
        set(ids::tremEnd, endOctaves);
        set(ids::tremBend, bend);
        set(ids::tremRamp, (float) ramp);
    }
    // The STUTTER knob: speed 0 = normal, 1 = fast, 2 = slow; repeats 1, 2, 3, 4, 6, 8 or 16.
    void stutter(int speed, int repeats, int type, int length)
    {
        static constexpr int counts[7] { 1, 2, 3, 4, 6, 8, 16 };
        int r = 0;
        for (int i = 0; i < 7; ++i)
            if (counts[i] == repeats)
                r = i;
        set(ids::stutter, (float) (1 + speed * 7 + r));
        set(ids::stutterType, (float) type);
        set(ids::stutterLen, (float) length); // 0 = 1/4 note, 1 = 1/8, 2 = 1/16
    }
};

inline float P(int semitones) { return bitvibe::mapping::pitchStepKnob(semitones); }
inline float pct(float fraction) { return 0.03f + 0.94f * fraction; } // a filter or rate step: this much of its knob
constexpr float kSkip = 0.0f, kMute = 1.0f;
// stutter types
constexpr int kRepeat = 0, kReverse = 1, kRise = 2, kTapeStop = 3, kScratch = 4, kFreeze = 5;
// delay times (kDelayDivisions)
constexpr int k16th = 2, k16thDotted = 3, k8th = 5, k8thDotted = 6, kQuarter = 8, kQuarterDotted = 9;
// reverb types
constexpr int kRoom = 0, kHall = 1, kSpring = 2, kPlate = 3, kShimmer = 4;

struct Preset
{
    const char* name;
    std::function<void(Builder&)> build;
};

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list {
        { "Arp Sixteen", [](Builder& b) {
             // a two-bar arpeggio in sixteenths, accented on the beat, with dotted echoes
             b.set(ids::tempo, 110.0f);
             b.sequence(16, 5, 2);
             b.steps({ P(0), P(7), P(12), P(7), P(3), P(7), P(10), P(12), P(0), P(5), P(12), P(5), P(-2), P(5), P(10), P(12) });
             b.lane(Lane::Level, { 1.0f, 0.55f, 0.7f, 0.55f });
             b.freeRunning();
             b.delay(k8thDotted, 0.3f, 0.45f, 4);
             b.reverb(kPlate, 0.2f, 0.5f);
         } },
        { "Glass Stairs", [](Builder& b) {
             // every pick climbs a staircase of whole tones into a shimmer
             b.sequence(6, 0, 1);
             b.stutter(0, 8, kRise, 2);
             b.reverb(kShimmer, 0.45f, 0.7f);
         } },
        { "Tape Brake", [](Builder& b) {
             // each pick grinds to a halt over a bar, like a tape machine switched off
             b.sequence(6, 0, 1);
             b.stutter(0, 4, kTapeStop, 0);
             b.set(ids::bits, 0.7f);
             b.reverb(kRoom, 0.15f, 0.4f);
         } },
        { "Scratch Cut", [](Builder& b) {
             // scratched picks, and the sequence throws in a scratch of its own on beats 2 and 4
             b.set(ids::tempo, 96.0f);
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.steps({ P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0) });
             b.stutter(0, 4, kScratch, 1);
             b.lane(Lane::Stutter, { 0.0f, 0.0f, 0.0f, 1.0f });
             b.dist(0, 0.4f, 0.55f, 0.7f);
         } },
        { "Frozen Pad", [](Builder& b) {
             // a pick hangs in the air as a cloud, breathing slowly into a long hall
             b.sequence(6, 0, 1);
             b.stutter(0, 16, kFreeze, 0);
             b.tremolo(3.0f, 0.5f, 0.0f);
             b.reverb(kHall, 0.5f, 0.85f);
         } },
        { "Backwards Rain", [](Builder& b) {
             // picks played backwards, falling into quarter-note echoes and a reversed hall
             b.sequence(6, 0, 1);
             b.stutter(0, 3, kReverse, 1);
             b.delay(kQuarter, 0.35f, 0.55f, bitvibe::kDelayForever);
             b.set(ids::delayWobble, 0.3f);
             b.reverb(kHall, 0.3f, 0.6f, true);
         } },
        { "Gate Grid", [](Builder& b) {
             // a sixteen-step gate carved out of a fuzzed chord
             b.sequence(16, 5, 2);
             b.steps({ P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0) });
             b.lane(Lane::Level, { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1 });
             b.freeRunning();
             b.dist(1, 0.5f, 0.5f, 0.6f);
         } },
        { "Crush Accents", [](Builder& b) {
             // clean on the beat, crushed in between
             b.sequence(8, 5, 2);
             b.steps({ P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0) });
             b.set(ids::filter, 0.85f);
             b.lane(Lane::Crush, { 0.0f, 0.85f, 0.0f, 0.6f, 0.0f, 0.85f, 0.3f, 0.95f });
         } },
        { "Dub Throws", [](Builder& b) {
             // the off-beats are thrown into the echoes and the spring, the last one an octave up
             b.set(ids::tempo, 84.0f);
             b.sequence(4, 5, 1);
             b.steps({ P(0), P(0), P(0), P(12) });
             b.delay(kQuarterDotted, 0.6f, 0.7f, 8);
             b.set(ids::delayLowCut, 300.0f);
             b.set(ids::delayHighCut, 3000.0f);
             b.set(ids::delayWobble, 0.6f); // a worn tape echo
             b.lane(Lane::Delay, { 0.0f, 1.0f });
             b.reverb(kSpring, 0.25f, 0.5f);
             b.lane(Lane::Reverb, { 0.0f, 1.0f });
         } },
        { "Stutter Machine", [](Builder& b) {
             // a filter sequence that stutters on its own, off the beat
             b.set(ids::seqType, 1.0f);
             b.sequence(16, 5, 1);
             b.steps({ pct(0.3f), pct(0.08f), pct(0.15f), pct(0.5f), pct(0.05f), pct(0.2f), pct(0.6f), pct(0.1f),
                       pct(0.3f), pct(0.04f), pct(0.25f), pct(0.45f), pct(0.08f), pct(0.6f), pct(0.12f), pct(0.2f) });
             b.stutter(0, 2, kRepeat, 2);
             b.set(ids::stutter, 0.0f); // the knob off: only the lane stutters (twice)
             b.lane(Lane::Stutter, { 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0 });
             b.freeRunning();
         } },
        { "Filter Walk 12", [](Builder& b) {
             // twelve filter steps against a bar of sixteen: the sweep lands somewhere new each bar
             b.set(ids::seqType, 1.0f);
             b.sequence(12, 5, 2);
             b.freeRunning();
             b.steps({ pct(0.02f), pct(0.04f), pct(0.07f), pct(0.11f), pct(0.16f), pct(0.24f), pct(0.35f), pct(0.24f), pct(0.16f),
                       pct(0.11f), pct(0.07f), pct(0.04f) });
             b.reverb(kRoom, 0.15f, 0.5f);
         } },
        { "Five Against Four", [](Builder& b) {
             // a five-note figure turning over a four-beat bar
             b.set(ids::tempo, 100.0f);
             b.sequence(5, 5, 2);
             b.steps({ P(0), P(7), P(12), P(3), P(10) });
             b.lane(Lane::Level, { 1.0f, 0.6f, 0.6f, 0.6f, 0.6f });
             b.delay(k8th, 0.25f, 0.4f, 3);
         } },
        { "Rate Ramp", [](Builder& b) {
             // a pick dives through the sample rate, crushing deeper, then the last four steps loop
             b.set(ids::seqType, 2.0f);
             b.sequence(16, 1, 3);
             b.steps({ pct(0.25f), pct(0.18f), pct(0.13f), pct(0.1f), pct(0.08f), pct(0.065f), pct(0.05f), pct(0.04f),
                       pct(0.033f), pct(0.028f), pct(0.024f), pct(0.02f), pct(0.017f), pct(0.03f), pct(0.014f), pct(0.025f) });
             b.lane(Lane::Crush, { 0.0f, 0.04f, 0.08f, 0.12f, 0.16f, 0.2f, 0.24f, 0.28f, 0.32f, 0.36f, 0.4f, 0.44f, 0.5f, 0.5f, 0.6f, 0.6f });
             b.on(ids::loopOn);
             b.set(ids::loopFrom, 12.0f);
             b.set(ids::loopTo, 15.0f);
             b.set(ids::loopRepeats, 0.0f);
         } },
        { "Tremolo Rush", [](Builder& b) {
             // a tremolo that speeds up over two bars after each pick, softer on the off-beats
             b.sequence(4, 5, 0);
             b.steps({ P(0), P(0), P(0), P(0) });
             b.tremolo(3.0f, 1.0f, 0.6f, 2.5f, 0.4f, 3);
             b.lane(Lane::Trem, { 1.0f, 0.5f });
             b.reverb(kPlate, 0.15f, 0.4f);
         } },
        { "Mixed Signals", [](Builder& b) {
             // every step its own job: pitch, filter, sample rate in turn
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.types({ 1, 2, 3, 1, 2, 3, 1, 2 });
             b.steps({ P(12), pct(0.05f), pct(0.1f), P(-12), pct(0.15f), pct(0.04f), P(7), pct(0.3f) });
             b.delay(k16thDotted, 0.2f, 0.4f, 3);
         } },
        { "Octave Bounce", [](Builder& b) {
             // octaves bouncing in sixteenths, a rising stutter at the end of each bar
             b.sequence(8, 5, 2);
             b.steps({ P(12), P(0), P(-12), P(0), P(12), P(0), P(-12), P(12) });
             b.stutter(0, 2, kRise, 2);
             b.set(ids::stutter, 0.0f);
             b.lane(Lane::Stutter, { 0, 0, 0, 0, 0, 0, 0, 1 });
             b.reverb(kPlate, 0.2f, 0.5f);
         } },
        { "Lo-Fi Shuffle", [](Builder& b) {
             // a dusty shuffle: twelve steps gated long-short across three beats
             b.set(ids::tempo, 92.0f);
             b.sequence(12, 5, 2);
             b.freeRunning();
             b.steps({ P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0) });
             b.lane(Lane::Level, { 1.0f, 0.0f, 0.7f });
             b.set(ids::sampleRate, 0.55f);
             b.set(ids::bits, 0.45f);
             b.set(ids::filter, 0.7f);
         } },
        { "Reverb Swells", [](Builder& b) {
             // the fourth and eighth steps swell backwards into a long hall
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.steps({ P(0), P(0), P(0), P(7), P(0), P(0), P(0), P(12) });
             b.lane(Lane::Level, { 0.8f, 0.6f, 0.6f, 1.0f });
             b.lane(Lane::Reverb, { 0.0f, 0.0f, 0.0f, 1.0f });
             b.reverb(kHall, 0.8f, 0.8f, true);
         } },
        { "Freeze and Glide", [](Builder& b) {
             // a melody that freezes on the first step of each bar
             b.sequence(16, 5, 1);
             b.steps({ P(0), P(3), P(7), P(10), P(12), P(10), P(7), P(3), P(5), P(8), P(12), P(8), P(5), P(3), P(0), P(-5) });
             b.stutter(0, 2, kFreeze, 1);
             b.set(ids::stutter, 0.0f);
             b.lane(Lane::Stutter, { 1, 0, 0, 0, 0, 0, 0, 0 });
             b.freeRunning();
             b.reverb(kHall, 0.3f, 0.6f);
         } },
        { "Chaos Engine", [](Builder& b) {
             // random scratches, steps of every kind, crushed and fuzzed, with sixteenth echoes
             b.set(ids::tempo, 128.0f);
             b.sequence(16, 5, 2);
             b.types({ 1, 2, 1, 3, 1, 1, 2, 3, 1, 2, 1, 1, 3, 1, 2, 1 });
             b.steps({ P(12), pct(0.1f), P(-5), pct(0.06f), P(7), kMute, pct(0.3f), pct(0.12f), P(-12), pct(0.04f), P(3), kSkip,
                       pct(0.2f), P(10), pct(0.5f), P(0) });
             b.set(ids::stutter, 22.0f); // random
             b.set(ids::stutterType, (float) kScratch);
             b.set(ids::stutterLen, 2.0f);
             b.lane(Lane::Crush, { 0.0f, 0.5f, 0.0f, 0.9f, 0.2f, 0.0f, 0.7f, 0.0f, 0.4f, 0.0f, 0.8f, 0.0f, 0.3f, 0.6f, 0.0f, 0.95f });
             b.lane(Lane::Stutter, { 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0 });
             b.freeRunning();
             b.dist(1, 0.35f, 0.6f, 0.55f);
             b.delay(k16thDotted, 0.3f, 0.5f, bitvibe::kDelayForever);
         } },
    };
    return list;
}

// The preset as a file: what Export writes, without the picked sounds (loading it leaves the player's own).
inline std::unique_ptr<juce::XmlElement> build(const Preset& preset)
{
    BitVibeProcessor p;
    p.setPlayConfigDetails(2, 2, 48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    Builder b { p };
    // the sample blend's levels start from the player's remembered ones: from their defaults here, so the files never
    // depend on the machine that wrote them
    for (auto* prm : p.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(prm))
            if (ranged->paramID.startsWith("sample") && ranged->paramID != ids::sampleRate)
                ranged->setValueNotifyingHost(ranged->getDefaultValue());
    preset.build(b);
    p.changeProgramName(p.getCurrentProgram(), preset.name);
    auto xml = p.exportPreset();
    for (const char* tag : { "SOUNDS", "LOOP_SOUNDS" })
        if (auto* e = xml->getChildByName(tag))
            xml->removeChildElement(e, true);
    return xml;
}

inline juce::String fileName(int index, const Preset& preset)
{
    return juce::String(index + 1).paddedLeft('0', 2) + " " + preset.name + ".xml";
}
} // namespace presetpack
