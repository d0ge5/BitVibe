#pragma once

// The preset pack: sounds built around the 16-step sequencer, its lanes, the stutter types, the tremolo and the delay's
// tape wobble.
// BitVibeEditorSnapshot --write-presets <folder> writes them as preset files (Presets/ in the repository, which the
// plug-in embeds as slots 21 on); the snapshot tests check that the files still match this table and
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
    // The phaser / flanger: type 0 = phaser, 1 = flanger; rate an index of kModRateBeats (0 = 4 bars .. 7 = 1/32).
    void sweep(int type, int rate, float depth, float feedback, float mix = 1.0f)
    {
        on(ids::modOn);
        set(ids::modType, (float) type);
        set(ids::modRate, (float) rate);
        set(ids::modDepth, depth);
        set(ids::modFeedback, feedback);
        set(ids::modMix, mix);
    }
    // REPLAY (the LOOPER section): what you just played loops through the sequence when you stop. Seconds of LOOK
    // BACK and WAIT, the LENGTH (kReplayForever = until you play again), DRY 0..1 and the THRESHOLD in dB.
    void replay(float lookBack, float wait, float length = bitvibe::kReplayForever, float dry = 0.0f, float thresholdDb = -45.0f)
    {
        on(ids::replayOn);
        set(ids::replayBack, lookBack);
        set(ids::replayWait, wait);
        set(ids::replayLength, length);
        set(ids::replayDry, dry);
        set(ids::replayThreshold, thresholdDb);
    }
    // How REPLAY's loop plays back: speed 0 = normal, 1 = half (an octave down), 2 = quarter; direction 0 = forward,
    // 1 = reverse, 2 = back and forth; stretch 1..4 (times slower, same pitch); the swell in seconds; wear 0..1.
    void replayPlayback(int speed, int direction, float stretch = 1.0f, float swell = 0.0f, float wear = 0.0f)
    {
        set(ids::replaySpeed, (float) speed);
        set(ids::replayDirection, (float) direction);
        set(ids::replayStretch, stretch);
        set(ids::replaySwell, swell);
        set(ids::replayWear, wear);
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
constexpr int kRepeat = 0, kReverse = 1, kRise = 2, kTapeStop = 3, kScratch = 4, kFreeze = 5, kFall = 6, kShuffle = 7,
              kHalf = 8, kGate = 9;
// filter types
constexpr int kLadder = 0, kLowPass = 1, kBandPass = 2, kHighPass = 3, kEnvelope = 4, kBloom = 5, kFormant = 6;
// phaser / flanger types and rates
constexpr int kPhaser = 0, kFlanger = 1;
constexpr int kRate4Bars = 0, kRate2Bars = 1, kRateBar = 2, kRateHalf = 3, kRateQuarter = 4, kRateEighth = 5;
// delay times (kDelayDivisions)
constexpr int k32nd = 0, k16th = 2, k16thDotted = 3, k8th = 5, k8thDotted = 6, kQuarter = 8, kQuarterDotted = 9;
// reverb types
constexpr int kRoom = 0, kHall = 1, kSpring = 2, kPlate = 3, kShimmer = 4;

struct Preset
{
    const char* name;
    std::function<void(Builder&)> build;
    const char* section = nullptr; // the section of the bank it is listed under (nullptr = the pack itself)
};
// The sections after the pack, as the preset menu shows them: LOOPER (presets built around REPLAY) and DREAMY LOOPS
// (REPLAY's playback: speed, direction, stretch, swell and wear, for atmospheres)
constexpr const char* kLooper = "Looper", *kDreamy = "Dreamy Loops";
constexpr int kForward = 0, kBackwards = 1, kBackAndForth = 2; // REPLAY directions
constexpr int kNormalSpeed = 0, kHalfSpeed = 1, kQuarterSpeed = 2;

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
        { "Worn Cassette", [](Builder& b) {
             // no sequence at all: a tired cassette, crunchy and dark, its echoes sagging and wandering
             b.sequence(6, 0, 1);
             b.set(ids::sampleRate, 0.62f);
             b.set(ids::bits, 0.5f);
             b.set(ids::filter, 0.78f);
             b.delay(k8thDotted, 0.45f, 0.6f, bitvibe::kDelayForever);
             b.set(ids::delayWobble, 1.0f);
         } },
        { "Seasick Arp", [](Builder& b) {
             // a minor arpeggio whose echoes slide out of tune and back
             b.set(ids::tempo, 104.0f);
             b.sequence(8, 5, 2);
             b.freeRunning();
             b.steps({ P(0), P(3), P(7), P(12), P(7), P(3), P(-5), P(-2) });
             b.lane(Lane::Level, { 1.0f, 0.65f, 0.75f, 0.65f });
             b.delay(k8th, 0.4f, 0.55f, bitvibe::kDelayForever);
             b.set(ids::delayWobble, 0.8f);
             b.reverb(kPlate, 0.15f, 0.5f);
         } },
        { "Half-Speed Scratch", [](Builder& b) {
             // each pick dragged back and forth at half speed, an octave down, into a wobbly echo
             b.sequence(6, 0, 1);
             b.stutter(2, 2, kScratch, 0);
             b.delay(k8th, 0.2f, 0.45f, 3);
             b.set(ids::delayWobble, 0.4f);
             b.dist(0, 0.3f, 0.45f, 0.5f);
         } },
        { "Reverse Climb", [](Builder& b) {
             // sixteen steps climbing an octave, flipping backwards at the top of each half
             b.sequence(16, 5, 2);
             b.freeRunning();
             b.steps({ P(0), P(2), P(3), P(5), P(7), P(8), P(10), P(12), P(0), P(2), P(3), P(5), P(7), P(8), P(10), P(12) });
             b.stutter(0, 2, kReverse, 2);
             b.set(ids::stutter, 0.0f);
             b.lane(Lane::Stutter, { 0, 0, 0, 0, 0, 0, 0, 1 });
             b.reverb(kRoom, 0.2f, 0.5f);
         } },
        { "Tape Stop Fill", [](Builder& b) {
             // a straight sixteenth groove that brakes to a stop at the end of every bar
             b.sequence(16, 5, 2);
             b.freeRunning();
             b.steps({ P(0), P(0), P(12), P(0), P(0), P(12), P(0), P(0), P(0), P(0), P(12), P(0), P(0), P(0), P(0), P(0) });
             b.lane(Lane::Level, { 1.0f, 0.6f, 0.8f, 0.6f });
             b.stutter(0, 2, kTapeStop, 1);
             b.set(ids::stutter, 0.0f);
             b.lane(Lane::Stutter, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 });
         } },
        { "Pulse Width", [](Builder& b) {
             // a hard square tremolo that thins out step by step, then comes back
             b.sequence(8, 5, 2);
             b.steps({ P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0) });
             b.tremolo(8.0f, 1.0f, 1.0f);
             b.lane(Lane::Trem, { 0.0f, 0.3f, 0.6f, 1.0f, 1.0f, 0.6f, 0.3f, 0.0f });
             b.dist(0, 0.45f, 0.5f, 0.7f);
         } },
        { "Slow Motion", [](Builder& b) {
             // a falling line under a tremolo that slows to a crawl over two bars
             b.set(ids::tempo, 90.0f);
             b.sequence(4, 5, 0);
             b.steps({ P(0), P(-5), P(-12), P(-7) });
             b.tremolo(10.0f, 0.9f, 0.4f, -2.5f, -0.3f, 3);
             b.reverb(kHall, 0.3f, 0.7f);
         } },
        { "Bitstorm", [](Builder& b) {
             // the sample rate rattling at thirty-second notes, crushed hard on random steps, random stutters on top
             b.set(ids::tempo, 140.0f);
             b.set(ids::seqType, 2.0f);
             b.sequence(16, 5, 3);
             b.freeRunning();
             b.steps({ pct(0.2f), pct(0.05f), pct(0.12f), pct(0.03f), pct(0.3f), pct(0.08f), pct(0.02f), pct(0.15f),
                       pct(0.06f), pct(0.25f), pct(0.04f), pct(0.1f), pct(0.02f), pct(0.18f), pct(0.07f), pct(0.35f) });
             b.lane(Lane::Crush, { 0.9f, 0.0f, 0.5f, 0.0f, 0.0f, 0.8f, 0.0f, 0.3f, 0.95f, 0.0f, 0.0f, 0.6f, 0.0f, 0.4f, 0.0f, 0.85f });
             b.set(ids::stutter, 22.0f); // random
             b.set(ids::stutterLen, 2.0f);
             b.delay(k32nd, 0.2f, 0.4f, 4);
         } },
        { "Ghost Notes", [](Builder& b) {
             // quiet ghost steps around the accents, with a lightly worn sixteenth echo
             b.sequence(16, 5, 2);
             b.freeRunning();
             b.steps({ P(0), P(0), P(0), P(12), P(0), P(0), P(0), P(0), P(0), P(0), P(12), P(0), P(0), P(7), P(0), P(0) });
             b.lane(Lane::Level, { 1.0f, 0.2f, 0.25f, 0.9f, 0.2f, 0.3f, 1.0f, 0.2f, 0.85f, 0.2f, 0.9f, 0.25f, 0.2f, 0.8f, 0.3f, 0.2f });
             b.delay(k16thDotted, 0.25f, 0.4f, 3);
             b.set(ids::delayWobble, 0.3f);
         } },
        { "Frozen Steps", [](Builder& b) {
             // a rising figure that freezes into a shimmering cloud on every beat
             b.sequence(16, 5, 2);
             b.freeRunning();
             b.steps({ P(0), P(7), P(12), P(7), P(3), P(10), P(12), P(10), P(5), P(12), P(7), P(12), P(0), P(7), P(12), P(-5) });
             b.stutter(0, 2, kFreeze, 2);
             b.set(ids::stutter, 0.0f);
             b.lane(Lane::Stutter, { 1, 0, 0, 0 });
             b.reverb(kShimmer, 0.35f, 0.7f);
         } },
        { "Echo Ladder", [](Builder& b) {
             // each step sends more of itself to the echoes than the one before, the line climbing with it
             b.sequence(16, 5, 1);
             b.freeRunning();
             b.steps({ P(0), P(0), P(2), P(2), P(3), P(3), P(5), P(5), P(7), P(7), P(8), P(8), P(10), P(10), P(12), P(12) });
             b.lane(Lane::Delay, { 0.0f, 0.07f, 0.13f, 0.2f, 0.27f, 0.33f, 0.4f, 0.47f, 0.53f, 0.6f, 0.67f, 0.73f, 0.8f, 0.87f, 0.93f, 1.0f });
             b.delay(k8th, 0.5f, 0.6f, bitvibe::kDelayForever);
             b.set(ids::delayWobble, 0.2f);
         } },
        { "Underwater", [](Builder& b) {
             // a filter swaying low, a slow tremolo and a drowned tape echo in a dark hall
             b.set(ids::seqType, 1.0f);
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.steps({ pct(0.03f), pct(0.05f), pct(0.08f), pct(0.06f), pct(0.04f), pct(0.07f), pct(0.1f), pct(0.05f) });
             b.tremolo(1.5f, 0.4f, 0.0f);
             b.delay(kQuarter, 0.5f, 0.6f, 6);
             b.set(ids::delayWobble, 1.0f);
             b.reverb(kHall, 0.35f, 0.75f);
             b.set(ids::reverbTone, 0.2f);
         } },
        { "Acid Walk", [](Builder& b) {
             // a resonant band-pass walking up and down sixteen steps, with a sixteenth echo: the acid line
             b.set(ids::tempo, 126.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(16, 5, 2);
             b.freeRunning();
             b.set(ids::filterType, (float) kBandPass);
             b.set(ids::filterRes, 0.85f);
             b.set(ids::filter, 0.8f);
             b.steps({ pct(0.1f), pct(0.3f), pct(0.15f), pct(0.5f), pct(0.2f), pct(0.7f), pct(0.25f), pct(1.0f),
                       pct(0.6f), pct(0.35f), pct(0.8f), pct(0.2f), pct(0.45f), pct(0.12f), pct(0.3f), pct(0.08f) });
             b.lane(Lane::Level, { 1.0f, 0.7f, 0.9f, 0.7f, 1.0f, 0.7f, 0.9f, 0.7f });
             b.dist(0, 0.3f, 0.55f, 0.7f);
             b.delay(k16th, 0.25f, 0.5f, 3);
         } },
        { "Wah Picks", [](Builder& b) {
             // an auto-wah: each pick opens the filter over a four-note figure that plays four times, into a spring
             b.set(ids::tempo, 100.0f);
             b.sequence(4, 3, 1);
             b.steps({ P(0), P(7), P(12), P(7) });
             b.set(ids::filterType, (float) kEnvelope);
             b.set(ids::filterRes, 0.6f);
             b.set(ids::filter, 0.28f);
             b.reverb(kSpring, 0.25f, 0.5f);
         } },
        { "Squelch Ladder", [](Builder& b) {
             // the ladder near to whistling, swept by eight steps and a little crush on the off-beats, with dotted echoes
             b.set(ids::tempo, 118.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.set(ids::filterType, (float) kLadder);
             b.set(ids::filterRes, 0.92f);
             b.set(ids::filter, 0.7f);
             b.steps({ pct(0.08f), pct(0.4f), pct(0.15f), pct(0.9f), pct(0.1f), pct(0.6f), pct(0.25f), pct(0.35f) });
             b.lane(Lane::Crush, { 0.0f, 0.3f });
             b.delay(k8thDotted, 0.3f, 0.5f, 4);
         } },
        { "Thin Air", [](Builder& b) {
             // a high-pass climbing over a bar and falling back, under a slow tremolo, in a hall
             b.set(ids::tempo, 92.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(16, 5, 1);
             b.freeRunning();
             b.set(ids::filterType, (float) kHighPass);
             b.set(ids::filterRes, 0.55f);
             b.set(ids::filter, 0.85f);
             b.steps({ pct(0.05f), pct(0.1f), pct(0.18f), pct(0.28f), pct(0.4f), pct(0.55f), pct(0.72f), pct(0.9f),
                       pct(0.9f), pct(0.72f), pct(0.55f), pct(0.4f), pct(0.28f), pct(0.18f), pct(0.1f), pct(0.05f) });
             b.tremolo(3.0f, 0.5f, 0.2f);
             b.reverb(kHall, 0.3f, 0.6f);
         } },
        { "Wah Pedal", [](Builder& b) {
             // no sequence: just the auto-wah on whatever you play, a little overdrive behind it
             b.sequence(6, 0, 1);
             b.set(ids::filterType, (float) kEnvelope);
             b.set(ids::filterRes, 0.75f);
             b.set(ids::filter, 0.2f);
             b.dist(0, 0.2f, 0.55f, 0.8f);
         } },
        { "Vowel Steps", [](Builder& b) {
             // a band-pass at a sharp peak jumping between vowel-like places, four times after each pick, overdriven
             b.set(ids::tempo, 112.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(8, 3, 1);
             b.set(ids::filterType, (float) kBandPass);
             b.set(ids::filterRes, 0.9f);
             b.set(ids::filter, 0.75f);
             b.steps({ pct(0.12f), pct(0.3f), pct(0.12f), pct(0.55f), pct(0.2f), pct(0.3f), pct(0.8f), pct(0.45f) });
             b.dist(0, 0.4f, 0.5f, 0.75f);
             b.reverb(kRoom, 0.15f, 0.4f);
         } },
        { "Resonant Dive", [](Builder& b) {
             // each pick: the ladder, nearly whistling, falls from open to shut over a bar, into wobbling echoes
             b.set(ids::tempo, 120.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(16, 1, 2);
             b.set(ids::filterType, (float) kLadder);
             b.set(ids::filterRes, 0.9f);
             b.set(ids::filter, 0.9f);
             b.steps({ pct(1.0f), pct(0.8f), pct(0.64f), pct(0.5f), pct(0.4f), pct(0.32f), pct(0.25f), pct(0.2f),
                       pct(0.16f), pct(0.13f), pct(0.1f), pct(0.08f), pct(0.06f), pct(0.05f), pct(0.04f), pct(0.03f) });
             b.delay(k8th, 0.4f, 0.55f, 6);
             b.set(ids::delayWobble, 0.3f);
         } },
        { "Acid Bass Loop", [](Builder& b) {
             // an octave-jumping bass line with filter steps between the notes, a resonant low-pass and a fuzz: a 303
             b.set(ids::tempo, 132.0f);
             b.sequence(16, 5, 2);
             b.freeRunning();
             b.set(ids::filterType, (float) kLowPass);
             b.set(ids::filterRes, 0.85f);
             b.set(ids::filter, 0.5f);
             b.types({ 1, 2, 1, 1, 2, 1, 2, 1, 1, 2, 1, 1, 2, 1, 2, 2 });
             b.steps({ P(-12), pct(0.3f), P(-12), P(0), pct(0.6f), P(-12), pct(0.9f), P(-5), P(-12), pct(0.4f), P(-12), P(3),
                       pct(0.7f), P(-12), pct(1.0f), pct(0.2f) });
             b.lane(Lane::Level, { 1.0f, 0.6f, 0.9f, 0.7f });
             b.lane(Lane::Crush, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.4f, 0.0f });
             b.dist(1, 0.3f, 0.5f, 0.5f);
         } },
        { "Radio Dial", [](Builder& b) {
             // a band-pass hopping about sixteen places, crushed more and less as it goes, like a dial being turned
             b.set(ids::tempo, 100.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(16, 5, 2);
             b.freeRunning();
             b.set(ids::filterType, (float) kBandPass);
             b.set(ids::filterRes, 0.7f);
             b.set(ids::filter, 0.9f);
             b.steps({ pct(0.3f), pct(0.9f), pct(0.1f), pct(0.6f), pct(0.45f), pct(0.05f), pct(0.8f), pct(0.25f),
                       pct(0.7f), pct(0.15f), pct(1.0f), pct(0.4f), pct(0.2f), pct(0.55f), pct(0.08f), pct(0.65f) });
             b.lane(Lane::Crush, { 0.1f, 0.3f, 0.0f, 0.2f, 0.4f, 0.05f, 0.25f, 0.15f, 0.0f, 0.35f, 0.1f, 0.45f, 0.05f, 0.2f, 0.3f, 0.0f });
             b.set(ids::sampleRate, 0.85f);
             b.dist(0, 0.4f, 0.6f, 0.72f); // the band-pass leaves little: the overdrive brings it back up
             b.delay(k16th, 0.2f, 0.4f, bitvibe::kDelayForever);
         } },
        { "Thin Reverse", [](Builder& b) {
             // a high-pass climbing eight steps while reversed stutters fire on the odd ones, in a plate
             b.set(ids::tempo, 108.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.set(ids::filterType, (float) kHighPass);
             b.set(ids::filterRes, 0.6f);
             b.set(ids::filter, 0.8f);
             b.steps({ pct(0.05f), pct(0.12f), pct(0.22f), pct(0.35f), pct(0.5f), pct(0.68f), pct(0.85f), pct(1.0f) });
             b.stutter(0, 2, kReverse, 1);
             b.lane(Lane::Stutter, { 1, 0, 1, 0, 1, 0, 1, 0 });
             b.reverb(kPlate, 0.3f, 0.55f);
         } },
        { "Peak Pulse", [](Builder& b) {
             // a resonant low-pass snapping between shut and open under a hard square tremolo
             b.set(ids::tempo, 124.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(16, 5, 2);
             b.freeRunning();
             b.set(ids::filterType, (float) kLowPass);
             b.set(ids::filterRes, 0.9f);
             b.set(ids::filter, 0.6f);
             b.steps({ pct(0.1f), pct(0.1f), pct(0.8f), pct(0.1f), pct(0.1f), pct(0.6f), pct(0.1f), pct(1.0f),
                       pct(0.1f), pct(0.1f), pct(0.7f), pct(0.1f), pct(0.5f), pct(0.1f), pct(0.9f), pct(0.3f) });
             b.tremolo(8.0f, 0.8f, 0.9f);
             b.delay(k8thDotted, 0.25f, 0.5f, 3);
         } },
        { "Whistle Freeze", [](Builder& b) {
             // the ladder at full resonance, barely not whistling, swept slowly and frozen into a shimmer on each bar
             b.set(ids::tempo, 80.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(8, 5, 0);
             b.freeRunning();
             b.set(ids::filterType, (float) kLadder);
             b.set(ids::filterRes, 1.0f);
             b.set(ids::filter, 0.55f);
             b.steps({ pct(0.2f), pct(0.3f), pct(0.45f), pct(0.6f), pct(0.8f), pct(0.6f), pct(0.45f), pct(0.3f) });
             b.stutter(0, 4, kFreeze, 0);
             b.lane(Lane::Stutter, { 1, 0, 0, 0, 0, 0, 0, 0 });
             b.reverb(kShimmer, 0.4f, 0.7f);
         } },
        { "Swing Thing", [](Builder& b) {
             // a swung pitch line on eighth notes, every second step late, the backbeats a little softer
             b.set(ids::tempo, 104.0f);
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.set(ids::swing, 1.0f);
             b.steps({ P(0), P(12), P(3), P(12), P(5), P(15), P(3), P(10) });
             b.lane(Lane::Level, { 1.0f, 0.7f });
             b.reverb(kRoom, 0.2f, 0.4f);
         } },
        { "Bell Ringer", [](Builder& b) {
             // octaves rung against an 850 Hz sine: metal bells from any guitar, with a plate behind them
             b.set(ids::tempo, 96.0f);
             b.sequence(4, 5, 0);
             b.freeRunning();
             b.steps({ P(0), P(12), P(7), P(12) });
             b.on(ids::ringOn);
             b.set(ids::ringHz, 850.0f);
             b.set(ids::ringMix, 0.85f);
             b.reverb(kPlate, 0.35f, 0.6f);
         } },
        { "Kick the Filter", [](Builder& b) {
             // the PING plays the filter as a drum synth: filter steps tune each ring, swing gives it bounce
             b.set(ids::tempo, 112.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.on(ids::pingOn);
             b.set(ids::pingLevel, 0.8f);
             b.set(ids::swing, 0.6f);
             b.set(ids::filterType, (float) kLadder);
             b.set(ids::filterRes, 0.93f);
             b.set(ids::filter, 0.35f);
             b.steps({ pct(0.12f), pct(0.5f), pct(0.3f), pct(0.5f), pct(0.12f), pct(0.6f), pct(0.3f), pct(0.8f) });
             b.reverb(kRoom, 0.15f, 0.35f);
         } },
        { "Blooming Lead", [](Builder& b) {
             // RES BLOOM: every pick starts vocal and relaxes as the note dies, over a slow spring
             b.set(ids::tempo, 92.0f);
             b.sequence(4, 2, 0);
             b.steps({ P(0), P(7), P(12), P(7) });
             b.set(ids::filterType, (float) kBloom);
             b.set(ids::filterRes, 0.2f);
             b.set(ids::filter, 0.5f);
             b.reverb(kSpring, 0.3f, 0.5f);
         } },
        { "Robot Voice", [](Builder& b) {
             // a 32 Hz ring mod, fully wet, on whatever you play: the classic metal voice, dropping an octave to close
             b.set(ids::tempo, 110.0f);
             b.sequence(4, 3, 1);
             b.steps({ P(0), P(0), P(-5), P(-12) });
             b.on(ids::ringOn);
             b.set(ids::ringHz, 32.0f);
             b.set(ids::ringMix, 1.0f);
             b.reverb(kRoom, 0.15f, 0.35f);
         } },
        { "Bell Tower", [](Builder& b) {
             // slow octaves rung at 1.3 kHz into a shimmer: church bells from single notes
             b.set(ids::tempo, 70.0f);
             b.sequence(4, 5, 0);
             b.freeRunning();
             b.steps({ P(0), P(5), P(7), P(12) });
             b.lane(Lane::Level, { 1.0f, 0.8f, 0.85f, 0.9f });
             b.on(ids::ringOn);
             b.set(ids::ringHz, 1318.0f);
             b.set(ids::ringMix, 0.85f);
             b.reverb(kShimmer, 0.45f, 0.8f);
         } },
        { "Ring Shuffle", [](Builder& b) {
             // a swung funk line with a 220 Hz ring blended halfway in and a touch of drive
             b.set(ids::tempo, 116.0f);
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.set(ids::swing, 1.0f);
             b.steps({ P(0), P(12), P(0), P(7), P(3), P(12), P(5), P(10) });
             b.lane(Lane::Level, { 1.0f, 0.7f });
             b.on(ids::ringOn);
             b.set(ids::ringHz, 220.0f);
             b.set(ids::ringMix, 0.55f);
             b.dist(0, 0.35f, 0.5f, 0.7f);
             b.delay(k16th, 0.2f, 0.45f, 2);
         } },
        { "Tin Can Kit", [](Builder& b) {
             // a swung sixteenth-note drum kit from the PING alone: low pings as kicks, high ones as hats
             b.set(ids::tempo, 108.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(16, 5, 2);
             b.freeRunning();
             b.on(ids::pingOn);
             b.set(ids::pingLevel, 0.85f);
             b.set(ids::swing, 0.5f);
             b.set(ids::filterRes, 0.9f);
             b.set(ids::filter, 0.3f);
             b.steps({ pct(0.05f), pct(0.9f), pct(0.45f), pct(0.9f), pct(0.05f), pct(0.9f), pct(0.45f), pct(0.95f),
                       pct(0.05f), pct(0.85f), pct(0.45f), pct(0.9f), pct(0.1f), pct(0.9f), pct(0.5f), pct(0.95f) });
             b.lane(Lane::Level, { 1.0f, 0.6f, 0.85f, 0.6f });
             b.reverb(kRoom, 0.12f, 0.3f);
         } },
        { "Ping Pong", [](Builder& b) {
             // two pings a bar, their dotted-eighth echoes bouncing between the beats
             b.set(ids::tempo, 120.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(4, 5, 1);
             b.freeRunning();
             b.on(ids::pingOn);
             b.set(ids::pingLevel, 0.8f);
             b.set(ids::filterRes, 0.95f);
             b.set(ids::filter, 0.28f);
             b.steps({ pct(0.2f), kMute, pct(0.55f), kMute });
             b.delay(k8thDotted, 0.45f, 0.6f, 6);
         } },
        { "Bloom Swells", [](Builder& b) {
             // RES BLOOM under a slowing tremolo and a hall: every pick swells open, then settles
             b.set(ids::tempo, 90.0f);
             b.sequence(4, 2, 0);
             b.steps({ P(0), P(5), P(7), P(12) });
             b.set(ids::filterType, (float) kBloom);
             b.set(ids::filterRes, 0.15f);
             b.set(ids::filter, 0.55f);
             b.tremolo(3.5f, 0.45f, 0.1f, -1.0f, 0.0f, 3);
             b.reverb(kHall, 0.4f, 0.75f);
         } },
        { "Tape Bells", [](Builder& b) {
             // bells rung at 880 Hz, their quarter-note echoes wobbling like a worn tape loop
             b.set(ids::tempo, 84.0f);
             b.sequence(4, 5, 0);
             b.freeRunning();
             b.steps({ P(0), P(12), P(7), P(12) });
             b.on(ids::ringOn);
             b.set(ids::ringHz, 880.0f);
             b.set(ids::ringMix, 0.8f);
             b.delay(kQuarter, 0.4f, 0.5f, 8);
             b.set(ids::delayWobble, 0.7f);
             b.reverb(kPlate, 0.25f, 0.5f);
         } },
        { "Shuffle Strut", [](Builder& b) {
             // a hard-swung strut with soft backbeats, a scratch thrown on step five and a light fuzz
             b.set(ids::tempo, 100.0f);
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.set(ids::swing, 0.75f);
             b.steps({ P(0), P(10), P(12), P(7), P(3), P(10), P(12), P(5) });
             b.lane(Lane::Level, { 1.0f, 0.65f });
             b.stutter(0, 2, kScratch, 2);
             b.lane(Lane::Stutter, { 0, 0, 0, 0, 1, 0, 0, 0 });
             b.dist(1, 0.4f, 0.5f, 0.6f);
         } },
        { "Falling Down", [](Builder& b) {
             // every pick tumbles a whole tone at a time, two octaves down a hall
             b.set(ids::tempo, 110.0f);
             b.sequence(6, 0, 1);
             b.stutter(0, 8, kFall, 1);
             b.reverb(kHall, 0.3f, 0.6f);
         } },
        { "Cut Up Kid", [](Builder& b) {
             // a running line diced by the SHUFFLE: steps one and five throw the slice into a new order
             b.set(ids::tempo, 118.0f);
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.steps({ P(0), P(3), P(7), P(3), P(12), P(7), P(3), P(10) });
             b.stutter(1, 4, kShuffle, 2);
             b.lane(Lane::Stutter, { 1, 0, 0, 0, 1, 0, 0, 0 });
             b.lane(Lane::Crush, { 0.0f, 0.1f, 0.0f, 0.2f });
             b.delay(k16th, 0.2f, 0.45f, 2);
         } },
        { "Molasses Tape", [](Builder& b) {
             // HALF SPEED on the slow setting: every pick a quarter-speed sludge, the echoes wobbling behind it
             b.set(ids::tempo, 80.0f);
             b.sequence(6, 0, 1);
             b.stutter(2, 4, kHalf, 0);
             b.delay(kQuarter, 0.35f, 0.5f, 6);
             b.set(ids::delayWobble, 0.5f);
             b.reverb(kHall, 0.35f, 0.7f);
         } },
        { "Gate Crasher", [](Builder& b) {
             // a quarter-note slice chopped by the GATE, sixteen slices long, with a little grit and a plate
             b.set(ids::tempo, 124.0f);
             b.sequence(6, 0, 1);
             b.stutter(0, 16, kGate, 0);
             b.dist(0, 0.3f, 0.5f, 0.7f);
             b.reverb(kPlate, 0.2f, 0.45f);
         } },
        { "Talk Box", [](Builder& b) {
             // the FORMANT filter on filter steps: the line says "oo-oh-ah-ee" as it walks, over a light drive
             b.set(ids::tempo, 100.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.set(ids::filterType, (float) kFormant);
             b.set(ids::filterRes, 0.5f);
             b.set(ids::filter, 0.85f);
             b.steps({ pct(0.02f), pct(0.15f), pct(0.4f), pct(1.0f), pct(0.4f), pct(0.15f), pct(0.05f), pct(1.0f) });
             b.dist(0, 0.35f, 0.6f, 0.75f);
             b.reverb(kRoom, 0.15f, 0.35f);
         } },
        { "Vowel Ping", [](Builder& b) {
             // PING through the FORMANT filter: every step a sung syllable, swung, with no input needed
             b.set(ids::tempo, 96.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.on(ids::pingOn);
             b.set(ids::pingLevel, 1.0f);
             b.set(ids::swing, 0.5f);
             b.set(ids::filterType, (float) kFormant);
             b.set(ids::filterRes, 0.45f);
             b.dist(0, 0.3f, 0.55f, 0.5f);
             b.set(ids::filter, 0.85f);
             b.steps({ pct(0.03f), pct(0.4f), pct(0.15f), pct(1.0f), pct(0.03f), pct(0.4f), pct(0.6f), pct(0.15f) });
             b.reverb(kPlate, 0.25f, 0.5f);
         } },
        { "Phase Walk", [](Builder& b) {
             // an arpeggio under a one-bar PHASER sweep with plenty of feedback, into a hall
             b.set(ids::tempo, 108.0f);
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.steps({ P(0), P(7), P(12), P(7), P(3), P(10), P(15), P(10) });
             b.sweep(kPhaser, kRateBar, 0.85f, 0.6f);
             b.reverb(kHall, 0.25f, 0.5f);
         } },
        { "Phased Shuffle", [](Builder& b) {
             // a swung funk line with a quick quarter-note PHASER and a little drive
             b.set(ids::tempo, 112.0f);
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.set(ids::swing, 0.9f);
             b.steps({ P(0), P(12), P(5), P(12), P(3), P(10), P(7), P(12) });
             b.lane(Lane::Level, { 1.0f, 0.7f });
             b.sweep(kPhaser, kRateQuarter, 0.6f, 0.3f);
             b.dist(0, 0.3f, 0.5f, 0.7f);
         } },
        { "Jet Plane", [](Builder& b) {
             // octaves through a slow two-bar FLANGER with heavy feedback: the jet taking off, echoed
             b.set(ids::tempo, 90.0f);
             b.sequence(4, 5, 0);
             b.freeRunning();
             b.steps({ P(0), P(12), P(0), P(7) });
             b.sweep(kFlanger, kRate2Bars, 0.9f, 0.75f);
             b.delay(k8thDotted, 0.3f, 0.5f, 4);
         } },
        { "Metal Flange", [](Builder& b) {
             // a quarter-note FLANGER over a low ring mod and the fuzz: a grinding, clanging riff
             b.set(ids::tempo, 120.0f);
             b.sequence(8, 5, 1);
             b.freeRunning();
             b.steps({ P(0), P(0), P(12), P(0), P(7), P(0), P(10), P(12) });
             b.on(ids::ringOn);
             b.set(ids::ringHz, 110.0f);
             b.set(ids::ringMix, 0.45f);
             b.sweep(kFlanger, kRateQuarter, 0.6f, 0.6f);
             b.dist(1, 0.45f, 0.5f, 0.5f);
         } },
        // ---- LOOPER: REPLAY on, so each of these answers what you play once you stop. A pick cuts the loop rather than
        // restarting the sequence, so none of them needs PATTERN LOCK; the sequence starts with the loop. ----
        { "Loop Octaves", [](Builder& b) {
             // the plainest answer: your phrase back in octaves and fifths, in eighths, with a little plate
             b.set(ids::tempo, 120.0f);
             b.sequence(4, 5, 1);
             b.steps({ P(0), P(12), P(0), P(7) });
             b.reverb(kPlate, 0.2f, 0.5f);
             b.replay(2.0f, 0.5f);
         }, kLooper },
        { "Ghost Phrase", [](Builder& b) {
             // the last four seconds come back half clean, half through a slow resonant sweep into a hall
             b.set(ids::tempo, 70.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(8, 5, 0);
             b.set(ids::filter, 0.7f);
             b.set(ids::filterRes, 0.6f);
             b.steps({ pct(0.1f), pct(0.2f), pct(0.35f), pct(0.5f), pct(0.6f), pct(0.45f), pct(0.3f), pct(0.15f) });
             b.reverb(kHall, 0.4f, 0.75f);
             b.replay(4.0f, 0.8f, bitvibe::kReplayForever, 0.6f);
         }, kLooper },
        { "Chop Shop", [](Builder& b) {
             // the last second of your playing diced into a sixteenth-note gate, with short echoes
             b.set(ids::tempo, 128.0f);
             b.sequence(16, 5, 2);
             b.steps({ P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0) });
             b.lane(Lane::Level, { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1 });
             b.delay(k16th, 0.25f, 0.45f, 3);
             b.replay(1.0f, 0.3f);
         }, kLooper },
        { "Riff Grinder", [](Builder& b) {
             // your riff an octave down through the fuzz and a low ring mod, grinding on for eight seconds
             b.set(ids::tempo, 100.0f);
             b.sequence(8, 5, 1);
             b.steps({ P(-12), P(0), P(-12), P(7), P(-12), P(0), P(-5), P(0) });
             b.dist(1, 0.5f, 0.45f, 0.5f);
             b.on(ids::ringOn);
             b.set(ids::ringHz, 80.0f);
             b.set(ids::ringMix, 0.35f);
             b.replay(1.5f, 0.4f, 8.0f);
         }, kLooper },
        { "Half Time Dream", [](Builder& b) {
             // every other beat of the loop drops to HALF SPEED, under a shimmer
             b.set(ids::tempo, 90.0f);
             b.sequence(4, 5, 0);
             b.steps({ P(0), P(0), P(0), P(0) });
             b.stutter(0, 4, kHalf, 0);
             b.lane(Lane::Stutter, { 1, 0, 1, 0 });
             b.reverb(kShimmer, 0.4f, 0.75f);
             b.replay(3.0f, 0.6f);
         }, kLooper },
        { "Reverse Memory", [](Builder& b) {
             // a long pause, then the phrase back with slices REVERSED on beats one and three, in a reversed hall
             b.set(ids::tempo, 110.0f);
             b.sequence(8, 5, 1);
             b.steps({ P(0), P(0), P(12), P(0), P(0), P(0), P(7), P(0) });
             b.stutter(0, 2, kReverse, 1);
             b.lane(Lane::Stutter, { 1, 0, 0, 0, 1, 0, 0, 0 });
             b.reverb(kHall, 0.3f, 0.6f, true);
             b.replay(2.0f, 1.0f);
         }, kLooper },
        { "Talk Back", [](Builder& b) {
             // the FORMANT filter answers you: your phrase mouthed as vowels over a light drive, a third of it clean
             b.set(ids::tempo, 100.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(8, 5, 1);
             b.set(ids::filterType, (float) kFormant);
             b.set(ids::filterRes, 0.5f);
             b.set(ids::filter, 0.85f);
             b.steps({ pct(0.02f), pct(0.15f), pct(0.4f), pct(1.0f), pct(0.4f), pct(0.15f), pct(0.05f), pct(1.0f) });
             b.dist(0, 0.3f, 0.6f, 0.75f);
             b.replay(2.0f, 0.5f, bitvibe::kReplayForever, 0.3f);
         }, kLooper },
        { "Bit Loop", [](Builder& b) {
             // a four-second glitch reply: the last second at sixteenth-note sample rates, crushed in between
             b.set(ids::tempo, 120.0f);
             b.set(ids::seqType, 2.0f);
             b.sequence(8, 5, 2);
             b.steps({ pct(1.0f), pct(0.3f), pct(0.1f), pct(0.5f), pct(1.0f), pct(0.05f), pct(0.2f), pct(0.08f) });
             b.lane(Lane::Crush, { 0.0f, 0.4f, 0.8f, 0.2f });
             b.set(ids::bits, 0.6f);
             b.replay(1.0f, 0.3f, 4.0f);
         }, kLooper },
        { "Climb Then Rest", [](Builder& b) {
             // the loop climbs a major scale four times, then settles down to play on as it was
             b.set(ids::tempo, 120.0f);
             b.sequence(8, 3, 1);
             b.steps({ P(0), P(2), P(4), P(5), P(7), P(9), P(11), P(12) });
             b.delay(k8th, 0.3f, 0.5f, 4);
             b.reverb(kPlate, 0.25f, 0.5f);
             b.replay(2.0f, 0.5f);
         }, kLooper },
        { "Swing Looper", [](Builder& b) {
             // your phrase swung hard in eighths, bouncing between octaves, with dotted echoes
             b.set(ids::tempo, 96.0f);
             b.sequence(8, 5, 1);
             b.steps({ P(0), P(12), P(7), P(12), P(0), P(10), P(7), P(5) });
             b.set(ids::swing, 0.6f);
             b.lane(Lane::Level, { 1.0f, 0.6f });
             b.delay(k8thDotted, 0.3f, 0.45f, 4);
             b.replay(2.0f, 0.5f);
         }, kLooper },
        { "Tremolo Pulse", [](Builder& b) {
             // the loop under a tremolo that speeds up over a bar, swept by a slow phaser
             b.set(ids::tempo, 110.0f);
             b.sequence(4, 5, 0);
             b.steps({ P(0), P(0), P(0), P(0) });
             b.tremolo(4.0f, 0.9f, 0.5f, 2.0f, 0.3f, 4);
             b.sweep(kPhaser, kRateBar, 0.7f, 0.4f);
             b.replay(2.5f, 0.5f);
         }, kLooper },
        { "Shuffle Deck", [](Builder& b) {
             // the SHUFFLE deals your phrase out in a new order every other eighth
             b.set(ids::tempo, 118.0f);
             b.sequence(8, 5, 1);
             b.steps({ P(0), P(0), P(3), P(0), P(0), P(0), P(10), P(0) });
             b.stutter(1, 4, kShuffle, 2);
             b.lane(Lane::Stutter, { 1, 0, 1, 0, 1, 0, 1, 0 });
             b.delay(k16th, 0.2f, 0.4f, 2);
             b.replay(1.5f, 0.4f);
         }, kLooper },
        { "Long Memory", [](Builder& b) {
             // the last eight seconds, half clean, swelling slowly through a low-pass into a long hall and wobbling echoes
             b.set(ids::tempo, 60.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(16, 5, 0);
             b.set(ids::filterType, (float) kLowPass);
             b.set(ids::filter, 0.6f);
             b.set(ids::filterRes, 0.5f);
             b.steps({ pct(0.1f), pct(0.15f), pct(0.2f), pct(0.3f), pct(0.4f), pct(0.5f), pct(0.6f), pct(0.7f),
                       pct(0.7f), pct(0.6f), pct(0.5f), pct(0.4f), pct(0.3f), pct(0.2f), pct(0.15f), pct(0.1f) });
             b.reverb(kHall, 0.5f, 0.9f);
             b.delay(kQuarter, 0.3f, 0.6f, bitvibe::kDelayForever);
             b.set(ids::delayWobble, 0.3f);
             b.replay(8.0f, 1.5f, bitvibe::kReplayForever, 0.5f, -50.0f);
         }, kLooper },
        { "Quick Answer", [](Builder& b) {
             // the shortest reply: the last half second, an octave up and gated, the moment you stop, for two seconds
             b.set(ids::tempo, 140.0f);
             b.sequence(4, 5, 2);
             b.steps({ P(12), P(12), P(0), P(12) });
             b.lane(Lane::Level, { 1, 0, 1, 0 });
             b.reverb(kRoom, 0.15f, 0.4f);
             b.replay(0.5f, 0.15f, 2.0f);
         }, kLooper },
        { "Fading Ghost", [](Builder& b) {
             // an octave below, swelling into a reversed hall over wobbling echoes, and gone after six seconds
             b.set(ids::tempo, 80.0f);
             b.sequence(4, 5, 0);
             b.steps({ P(-12), P(-12), P(-5), P(-12) });
             b.reverb(kHall, 0.4f, 0.7f, true);
             b.delay(kQuarter, 0.3f, 0.55f, 6);
             b.set(ids::delayWobble, 0.4f);
             b.replay(3.0f, 0.7f, 6.0f, 0.4f);
         }, kLooper },
        // ---- DREAMY LOOPS: REPLAY's playback (speed, direction, stretch, swell and wear) for atmospheres ----
        { "Slow Tide", [](Builder& b) {
             // the last six seconds at half speed, swelling in over three, under a slow low-pass swell into a long hall
             b.set(ids::tempo, 60.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(8, 5, 0);
             b.set(ids::filterType, (float) kLowPass);
             b.set(ids::filter, 0.5f);
             b.set(ids::filterRes, 0.45f);
             b.steps({ pct(0.15f), pct(0.25f), pct(0.4f), pct(0.6f), pct(0.75f), pct(0.6f), pct(0.4f), pct(0.25f) });
             b.reverb(kHall, 0.5f, 0.9f);
             b.replay(6.0f, 1.0f, bitvibe::kReplayForever, 0.3f);
             b.replayPlayback(kHalfSpeed, kForward, 1.0f, 3.0f);
         }, kDreamy },
        { "Backwards Lullaby", [](Builder& b) {
             // your phrase played backwards, an octave and a fifth above, swelling into a shimmer
             b.set(ids::tempo, 72.0f);
             b.sequence(4, 5, 0);
             b.steps({ P(12), P(7), P(12), P(0) });
             b.reverb(kShimmer, 0.45f, 0.8f);
             b.replay(3.0f, 0.8f, bitvibe::kReplayForever, 0.2f);
             b.replayPlayback(kNormalSpeed, kBackwards, 1.0f, 1.5f);
         }, kDreamy },
        { "Frozen Lake", [](Builder& b) {
             // STRETCHed four times: two seconds of playing become eight of glassy pad, filtered low into a huge hall
             b.set(ids::tempo, 80.0f);
             b.set(ids::seqType, 1.0f);
             b.sequence(4, 5, 0);
             b.set(ids::filterType, (float) kLowPass);
             b.set(ids::filter, 0.45f);
             b.steps({ pct(0.3f), pct(0.5f), pct(0.4f), pct(0.6f) });
             b.reverb(kHall, 0.6f, 0.95f);
             b.replay(2.0f, 0.6f);
             b.replayPlayback(kNormalSpeed, kForward, 4.0f, 2.0f);
         }, kDreamy },
        { "Tape Memory", [](Builder& b) {
             // a half-speed loop that wears out: darker and quieter every pass, over wobbling echoes, until it is dust
             b.set(ids::tempo, 90.0f);
             b.sequence(4, 5, 0);
             b.steps({ P(0), P(0), P(0), P(0) });
             b.delay(kQuarter, 0.3f, 0.55f, 6);
             b.set(ids::delayWobble, 0.5f);
             b.reverb(kRoom, 0.2f, 0.5f);
             b.replay(4.0f, 0.8f, bitvibe::kReplayForever, 0.5f);
             b.replayPlayback(kHalfSpeed, kForward, 1.0f, 0.5f, 0.8f);
         }, kDreamy },
        { "Pendulum", [](Builder& b) {
             // back and forth, each swing an octave apart, under a slow smooth tremolo and a plate
             b.set(ids::tempo, 70.0f);
             b.sequence(4, 5, 0);
             b.steps({ P(0), P(12), P(0), P(12) });
             b.tremolo(1.5f, 0.6f, 0.0f);
             b.reverb(kPlate, 0.35f, 0.6f);
             b.replay(2.5f, 0.6f, bitvibe::kReplayForever, 0.25f);
             b.replayPlayback(kNormalSpeed, kBackAndForth, 1.0f, 1.0f);
         }, kDreamy },
        { "Quarter Light", [](Builder& b) {
             // two octaves down and half as fast again: a subterranean drone under a shimmer, a little of the loop clean
             b.set(ids::tempo, 60.0f);
             b.sequence(4, 5, 0);
             b.steps({ P(0), P(0), P(7), P(0) });
             b.reverb(kShimmer, 0.5f, 0.85f);
             b.replay(4.0f, 1.0f, bitvibe::kReplayForever, 0.2f);
             b.replayPlayback(kQuarterSpeed, kForward, 1.5f, 2.0f);
         }, kDreamy },
        { "Breathing Room", [](Builder& b) {
             // stretched to twice its length, breathing on a sixteen-step level swell, with a slow tremolo and a hall
             b.set(ids::tempo, 100.0f);
             b.sequence(16, 5, 1);
             b.steps({ P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0), P(0) });
             b.lane(Lane::Level, { 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.35f, 0.35f });
             b.tremolo(2.0f, 0.3f, 0.0f);
             b.reverb(kHall, 0.45f, 0.8f);
             b.replay(3.0f, 0.7f, bitvibe::kReplayForever, 0.3f);
             b.replayPlayback(kNormalSpeed, kForward, 2.0f, 1.5f);
         }, kDreamy },
        { "Mirror Pool", [](Builder& b) {
             // back and forth through the RES BLOOM filter and a four-bar phaser, into a reversed hall
             b.set(ids::tempo, 80.0f);
             b.sequence(4, 5, 0);
             b.steps({ P(0), P(5), P(7), P(0) });
             b.set(ids::filterType, (float) kBloom);
             b.set(ids::filter, 0.6f);
             b.set(ids::filterRes, 0.55f);
             b.sweep(kPhaser, kRate4Bars, 0.7f, 0.3f);
             b.reverb(kHall, 0.4f, 0.75f, true);
             b.replay(3.0f, 0.8f, bitvibe::kReplayForever, 0.2f);
             b.replayPlayback(kNormalSpeed, kBackAndForth, 1.0f, 1.0f);
         }, kDreamy },
        { "Dust and Echo", [](Builder& b) {
             // the loop wearing away under dotted echoes that never stop, each repeat older than the last
             b.set(ids::tempo, 84.0f);
             b.sequence(8, 5, 1);
             b.steps({ P(0), P(0), P(12), P(0), P(0), P(7), P(0), P(0) });
             b.lane(Lane::Level, { 1.0f, 0.5f });
             b.delay(k8thDotted, 0.4f, 0.7f, bitvibe::kDelayForever);
             b.set(ids::delayWobble, 0.5f);
             b.reverb(kRoom, 0.25f, 0.5f);
             b.replay(3.0f, 0.6f, bitvibe::kReplayForever, 0.4f);
             b.replayPlayback(kNormalSpeed, kForward, 1.0f, 0.8f, 0.5f);
         }, kDreamy },
        { "Dream Sequence", [](Builder& b) {
             // all of it: the last eight seconds backwards at half speed, stretched, swelling in over three seconds, in
             // a shimmer with quarter-note echoes, half of it clean
             b.set(ids::tempo, 66.0f);
             b.sequence(4, 5, 0);
             b.steps({ P(12), P(7), P(5), P(12) });
             b.delay(kQuarter, 0.3f, 0.6f, 6);
             b.reverb(kShimmer, 0.5f, 0.85f);
             b.replay(8.0f, 1.2f, bitvibe::kReplayForever, 0.4f);
             b.replayPlayback(kHalfSpeed, kBackwards, 1.5f, 3.0f, 0.15f);
         }, kDreamy },
    };
    return list;
}

// The preset as a file: what Export writes, without the picked sounds (loading it leaves the player's own).
// The pack's level check: a guitar-like pluck every second for four seconds (a few harmonics of G3, dying away).
// The pack is levelled on it, and the snapshot tests play it.
inline juce::AudioBuffer<float> pluckTestSignal()
{
    constexpr int fs = 48000, seconds = 4;
    juce::AudioBuffer<float> dry(2, fs * seconds);
    dry.clear();
    for (int start = fs / 4; start < fs * seconds; start += fs)
        for (int n = 0; start + n < fs * seconds; ++n)
        {
            const double t = (double) n / fs, env = std::exp(-3.0 * t);
            const float v = (float) (0.35 * env * (std::sin(juce::MathConstants<double>::twoPi * 196.0 * t)
                                                   + 0.5 * std::sin(juce::MathConstants<double>::twoPi * 392.0 * t)
                                                   + 0.25 * std::sin(juce::MathConstants<double>::twoPi * 588.0 * t)));
            dry.setSample(0, start + n, v);
            dry.setSample(1, start + n, v);
        }
    return dry;
}

// Plays a signal through the processor in 512-sample blocks, as a host would, and returns what comes out.
inline juce::AudioBuffer<float> playThrough(BitVibeProcessor& p, const juce::AudioBuffer<float>& in)
{
    juce::AudioBuffer<float> wet(in);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < wet.getNumSamples(); pos += 512)
    {
        juce::AudioBuffer<float> block(wet.getArrayOfWritePointers(), 2, pos, juce::jmin(512, wet.getNumSamples() - pos));
        p.processBlock(block, midi);
    }
    return wet;
}

// The SOUND LEVEL that brings what the processor makes of the test pluck to the pluck's own loudness (its RMS),
// without letting its peak above kLevelPeakCeiling, in half-dB steps.
inline constexpr float kLevelPeakCeiling = 0.9f;
inline float matchedLevelDb(BitVibeProcessor& p)
{
    const auto dry = pluckTestSignal();
    const auto wet = playThrough(p, dry);
    const int n = dry.getNumSamples();
    const double ratio = wet.getRMSLevel(0, 0, n) / dry.getRMSLevel(0, 0, n);
    const float peak = wet.getMagnitude(0, n); // both channels: the stereo effects peak differently on each side
    if (!(ratio > 1.0e-6))
        return 0.0f;
    double db = -20.0 * std::log10(ratio);
    if (peak > 1.0e-6f)
        db = std::min(db, 20.0 * std::log10((double) kLevelPeakCeiling / (double) peak));
    db = juce::jlimit(-18.0, 12.0, db);
    return (float) (std::floor(db * 2.0) / 2.0); // rounded down, so the peak ceiling holds
}

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
    // every pack preset plays at the loudness of the dry pluck (or as near as its peaks allow); playing the pluck
    // changes no parameter, so the file is written as the table says, with its level. A LOOPER preset is levelled
    // with REPLAY off: the plucks never stop for long enough to start a loop, and the loop goes through the same chain.
    const bool replayOn = p.apvts.getRawParameterValue(ids::replayOn)->load() > 0.5f;
    if (replayOn)
        b.set(ids::replayOn, 0.0f);
    b.set(ids::soundLevel, matchedLevelDb(p));
    if (replayOn)
        b.set(ids::replayOn, 1.0f);
    p.changeProgramName(p.getCurrentProgram(), preset.name);
    auto xml = p.exportPreset();
    if (preset.section != nullptr)
        xml->setAttribute("section", preset.section);
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
