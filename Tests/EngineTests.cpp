// Offline checks of the BitVibe engine against the behaviour described in the published documentation.
// Plain C++, no framework: build target `bitvibe_tests`, exit code 0 means everything passed.

#include "DrumAnalysis.h"
#include "DemoBeat.h"
#include "DrumMap.h"
#include "BeatRoute.h"
#include "Engine.h"
#include "Morph.h"
#include "Presets.h"
#include "SampleRender.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace bitvibe;

namespace
{
constexpr double kFs = 48000.0;
int gChecks = 0, gFailures = 0;
bool gVerbose = false; // --verbose: print details of the drum-analysis test

#define CHECK(cond, ...)                                                    \
    do                                                                      \
    {                                                                       \
        ++gChecks;                                                          \
        if (!(cond))                                                        \
        {                                                                   \
            ++gFailures;                                                    \
            std::printf("    FAIL line %d: %s  ", __LINE__, #cond);         \
            std::printf(__VA_ARGS__);                                       \
            std::printf("\n");                                              \
        }                                                                   \
    } while (0)

using Signal = std::vector<float>;

// A parameter set where every effect is neutral.
Params neutral()
{
    Params p;
    p.seqPlay = 0;
    return p;
}

Signal render(const Params& base, const Signal& in, const std::function<void(Params&, size_t)>& automate = {},
              int block = 256)
{
    Engine e;
    e.prepare(kFs);
    Signal out = in;
    Params p = base;
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t>((size_t) block, in.size() - pos);
        if (automate)
            automate(p, pos);
        float* ch = out.data() + pos;
        e.process(&ch, 1, n, p);
    }
    return out;
}

Signal sine(double f, double seconds, float amp = 0.5f)
{
    Signal s((size_t) (seconds * kFs));
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = amp * (float) std::sin(2.0 * 3.14159265358979 * f * (double) i / kFs);
    return s;
}

// Portable pseudo-random numbers in [-1, 1) with full float precision. The standard distributions
// give different values on MSVC and libc++, which made some checks depend on the platform.
struct Rng
{
    uint32_t state;
    explicit Rng(uint32_t seed) : state(seed != 0 ? seed : 1u) {}
    float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) ((double) state / 4294967296.0) * 2.0f - 1.0f;
    }
};

Signal noise(double seconds, float amp = 0.5f, unsigned seed = 1)
{
    Rng rng(seed);
    Signal s((size_t) (seconds * kFs));
    for (auto& v : s)
        v = amp * rng.next();
    return s;
}

double rms(const Signal& x, double t0, double t1)
{
    const size_t a = (size_t) (t0 * kFs), b = std::min(x.size(), (size_t) (t1 * kFs));
    double acc = 0;
    for (size_t i = a; i < b; ++i)
        acc += (double) x[i] * x[i];
    return std::sqrt(acc / (double) std::max<size_t>(1, b - a));
}

// Power of a single frequency (Goertzel), normalised by window length.
double tonePower(const Signal& x, double t0, double t1, double f)
{
    const size_t a = (size_t) (t0 * kFs), b = std::min(x.size(), (size_t) (t1 * kFs));
    const double w = 2.0 * 3.14159265358979 * f / kFs;
    double re = 0, im = 0;
    for (size_t i = a; i < b; ++i)
    {
        re += x[i] * std::cos(w * (double) i);
        im -= x[i] * std::sin(w * (double) i);
    }
    const double n = (double) (b - a);
    return (re * re + im * im) / (n * n);
}

double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1.0e-12)); }

// ---------------------------------------------------------------------------------------------

void testIdentity()
{
    std::puts("identity: all effects neutral must pass audio untouched");
    const Signal in = noise(1.0);
    const Signal out = render(neutral(), in);
    double maxDiff = 0;
    for (size_t i = 0; i < in.size(); ++i)
        maxDiff = std::max(maxDiff, (double) std::abs(out[i] - in[i]));
    CHECK(maxDiff < 1.0e-6, "max diff %g", maxDiff);

    Params p = neutral();
    p.sampleRate = 0.2f;
    p.bits = 0.1f;
    p.filter = 0.2f;
    p.bypass = true;
    const Signal byp = render(p, in);
    maxDiff = 0;
    for (size_t i = 0; i < in.size(); ++i)
        maxDiff = std::max(maxDiff, (double) std::abs(byp[i] - in[i]));
    CHECK(maxDiff < 1.0e-6, "bypass max diff %g", maxDiff);

    // Skipping every step leaves the sequencer with nothing to play.
    Params q;
    q.steps.fill(0.0f);
    const Signal skipped = render(q, in);
    maxDiff = 0;
    for (size_t i = 0; i < in.size(); ++i)
        maxDiff = std::max(maxDiff, (double) std::abs(skipped[i] - in[i]));
    CHECK(maxDiff < 1.0e-6, "all-skip max diff %g", maxDiff);
}

void testMappings()
{
    std::puts("mappings: values quoted in the manual");
    CHECK(std::abs(mapping::sampleRateHz(0.0f) - 48.0f) < 1e-3f, "");
    CHECK(std::abs(mapping::sampleRateHz(1.0f) - 48000.0f) < 1.0f, "");
    CHECK(std::abs(mapping::filterHz(0.0f) - 96.0f) < 1e-3f, "");
    CHECK(std::abs(mapping::filterHz(1.0f) - 24000.0f) < 1.0f, "");
    CHECK(mapping::bitDepth(0.0f) == 1.0f && mapping::bitDepth(1.0f) == 24.0f, "");

    // Stutter: 23 positions in total.
    CHECK(mapping::stutterMode(0).off, "");
    const auto a = mapping::stutterMode(1);
    CHECK(!a.off && a.repeats == 1 && a.speed == 1.0f, "");
    const auto b = mapping::stutterMode(7);
    CHECK(b.repeats == 16 && b.speed == 1.0f, "");
    const auto c = mapping::stutterMode(8);
    CHECK(c.repeats == 1 && c.speed == 2.0f, "");
    const auto d = mapping::stutterMode(15);
    CHECK(d.repeats == 1 && d.speed == 0.5f, "");
    const auto e = mapping::stutterMode(21);
    CHECK(e.repeats == 16 && e.speed == 0.5f, "");
    CHECK(mapping::stutterMode(22).random, "");

    // Pitch steps: 27 choices (skip, 24 semitones, dry, mute); centre of the knob is dry.
    using K = mapping::Step::Kind;
    CHECK(mapping::decodeStep(0.0f, SeqType::Pitch).kind == K::Skip, "");
    CHECK(mapping::decodeStep(1.0f, SeqType::Pitch).kind == K::Mute, "");
    CHECK(mapping::decodeStep(0.5f, SeqType::Pitch).semitones == 0.0f, "");
    CHECK(mapping::decodeStep(mapping::pitchStepKnob(-12), SeqType::Pitch).semitones == -12.0f, "");
    CHECK(mapping::decodeStep(mapping::pitchStepKnob(12), SeqType::Pitch).semitones == 12.0f, "");
    for (int s = -12; s <= 12; ++s)
        CHECK(mapping::decodeStep(mapping::pitchStepKnob(s), SeqType::Pitch).semitones == (float) s, "semi %d", s);

    // Filter / sample-rate steps: noon = 50%.
    const auto f = mapping::decodeStep(0.5f, SeqType::Filter);
    CHECK(f.kind == K::Active && std::abs(f.factor - 0.5f) < 1e-4f, "factor %g", f.factor);
    CHECK(mapping::decodeStep(0.0f, SeqType::SampleRate).kind == K::Skip, "");
    CHECK(mapping::decodeStep(1.0f, SeqType::SampleRate).kind == K::Mute, "");

    CHECK(mapping::seqRepeats(0) == 0 && mapping::seqRepeats(1) == 1 && mapping::seqRepeats(4) == 8
              && mapping::seqRepeats(5) == -1,
          "");
    CHECK(mapping::seqMultiplier(0) == 1 && mapping::seqMultiplier(7) == 128, "");
}

void testBitDepth()
{
    std::puts("bit depth: output has exactly 2^bits levels");
    for (int bits : { 2, 4, 6 })
    {
        Params p = neutral();
        p.bits = (float) (bits - 1) / 23.0f;
        const Signal out = render(p, sine(440.0, 0.5, 0.9f));
        const double levels = std::pow(2.0, bits - 1);
        std::vector<long> seen;
        bool onGrid = true;
        for (size_t i = 4000; i < out.size(); ++i)
        {
            const double k = (double) out[i] * levels - 0.5; // mid-rise grid: (k + 0.5) / levels
            if (std::abs(k - std::round(k)) > 1.0e-3)
                onGrid = false;
            const long idx = std::lround(k);
            if (std::find(seen.begin(), seen.end(), idx) == seen.end())
                seen.push_back(idx);
        }
        CHECK(onGrid, "bits=%d samples off the quantiser grid", bits);
        CHECK((double) seen.size() <= 2.0 * levels, "bits=%d saw %zu levels (max %g)", bits, seen.size(), 2.0 * levels);
        CHECK((double) seen.size() >= levels * 0.75 || bits >= 6, "bits=%d only %zu levels", bits, seen.size());
    }

    // 1 bit = a square wave (two levels).
    Params p = neutral();
    p.bits = 0.0f;
    const Signal sq = render(p, sine(220.0, 0.5, 0.9f));
    bool twoLevel = true;
    for (size_t i = 4000; i < sq.size(); ++i)
        twoLevel = twoLevel && std::abs(std::abs(sq[i]) - 0.5f) < 1.0e-3f;
    CHECK(twoLevel, "1-bit output is not a two-level square");

    // Noise gate below 8 bits: a very quiet hiss must be silenced, a normal signal must not be.
    Params g = neutral();
    g.bits = 3.0f / 23.0f; // 4 bits
    const Signal hiss = render(g, noise(1.0, 0.0005f));
    CHECK(rms(hiss, 0.3, 1.0) < 1.0e-4, "gate did not close: rms %g", rms(hiss, 0.3, 1.0));
    const Signal loud = render(g, sine(300.0, 0.5, 0.5f));
    CHECK(rms(loud, 0.2, 0.5) > 0.2, "gate ate a normal signal");
}

void testSampleRate()
{
    std::puts("sample rate: zero-order hold at the requested rate");
    Params p = neutral();
    p.sampleRate = std::log(4000.0f / 48.0f) / std::log(1000.0f); // 4 kHz -> hold ~12 samples
    const Signal out = render(p, noise(1.0));
    size_t same = 0, total = 0;
    for (size_t i = 3000; i + 1 < out.size(); ++i, ++total)
        same += std::abs(out[i] - out[i + 1]) < 1.0e-6f; // held = unchanged (up to rounding)
    const double frac = (double) same / (double) total;
    CHECK(std::abs(frac - 11.0 / 12.0) < 0.01, "hold fraction %g (expected %g)", frac, 11.0 / 12.0);
}

void testFilter()
{
    std::puts("filter: low-pass, ~24 dB/oct");
    Params p = neutral();
    p.filter = std::log(500.0f / 96.0f) / std::log(250.0f); // 500 Hz
    const double low = tonePower(render(p, sine(100.0, 1.0)), 0.3, 1.0, 100.0) / tonePower(sine(100.0, 1.0), 0.3, 1.0, 100.0);
    const double high = tonePower(render(p, sine(5000.0, 1.0)), 0.3, 1.0, 5000.0) / tonePower(sine(5000.0, 1.0), 0.3, 1.0, 5000.0);
    CHECK(std::abs(db(std::sqrt(low))) < 3.0, "100 Hz passband gain %g dB", db(std::sqrt(low)));
    CHECK(db(std::sqrt(high)) < -40.0, "5 kHz only %g dB down", db(std::sqrt(high)));

    // Filter knob fully clockwise = bypassed (manual, factory preset table).
    Params open = neutral();
    const Signal in = sine(15000.0, 0.5);
    const Signal out = render(open, in);
    CHECK(std::abs(rms(out, 0.2, 0.5) - rms(in, 0.2, 0.5)) < 1.0e-4, "");
}

// The filter types and the RESONANCE knob.
void testFilterTypes()
{
    std::puts("filter types and resonance");
    // the gain of a tone through the filter, in dB
    auto gainDb = [](const Params& p, double f)
    {
        const Signal in = sine(f, 1.0, 0.3f);
        return db(std::sqrt(tonePower(render(p, in), 0.3, 1.0, f) / tonePower(in, 0.3, 1.0, f)));
    };
    auto at = [](FilterType t, float hz, float res)
    {
        Params p = neutral();
        p.filterType = (int) t;
        p.filterRes = res;
        const float top = mapping::filterHasBandRange(t) ? mapping::kFilterBandMaxHz : mapping::kFilterMaxHz;
        p.filter = std::log(hz / mapping::kFilterMinHz) / std::log(top / mapping::kFilterMinHz);
        return p;
    };
    CHECK(std::abs(mapping::filterHzFor(FilterType::Band, 1.0f) - mapping::kFilterBandMaxHz) < 0.5f
              && std::abs(mapping::filterHzFor(FilterType::High, 0.0f) - mapping::kFilterMinHz) < 1.0e-3f
              && std::abs(mapping::filterHzFor(FilterType::Low, 1.0f) - mapping::kFilterMaxHz) < 0.5f,
          "the band-pass and high-pass run 96 Hz to 8 kHz, the others to 24 kHz");

    // LOW PASS: a 12 dB / oct low-pass
    {
        const double low = gainDb(at(FilterType::Low, 500.0f, 0.0f), 100.0), high = gainDb(at(FilterType::Low, 500.0f, 0.0f), 5000.0);
        CHECK(std::abs(low) < 3.0 && high < -30.0 && high > -60.0, "LOW PASS at 500 Hz: 100 Hz %g dB, 5 kHz %g dB (gentler than the ladder)", low, high);
    }
    // BAND PASS: the centre passes, both sides fall away
    {
        const Params p = at(FilterType::Band, 500.0f, 0.5f);
        const double centre = gainDb(p, 500.0), low = gainDb(p, 100.0), high = gainDb(p, 5000.0);
        CHECK(std::abs(centre) < 3.0 && low < -10.0 && high < -10.0, "BAND PASS at 500 Hz: centre %g dB, 100 Hz %g dB, 5 kHz %g dB", centre, low, high);
    }
    // HIGH PASS: the highs pass, the lows fall away
    {
        const Params p = at(FilterType::High, 500.0f, 0.0f);
        const double low = gainDb(p, 100.0), high = gainDb(p, 5000.0);
        CHECK(low < -20.0 && std::abs(high) < 3.0, "HIGH PASS at 500 Hz: 100 Hz %g dB, 5 kHz %g dB", low, high);
    }
    // RESONANCE: a peak at the cutoff that grows with the knob, on the ladder and on the state-variable filter
    for (FilterType t : { FilterType::Ladder, FilterType::Low })
    {
        const double none = gainDb(at(t, 500.0f, 0.0f), 500.0), some = gainDb(at(t, 500.0f, mapping::kDefaultFilterRes), 500.0),
                     lots = gainDb(at(t, 500.0f, 0.95f), 500.0);
        CHECK(lots > some + 4.0 && some > none, "type %d: the cutoff gain rises with RESONANCE (%g, %g, %g dB)", (int) t, none, some, lots);
    }
    // the default RESONANCE is the ladder as it was before the knob
    {
        Params before = neutral();
        before.filter = std::log(500.0f / 96.0f) / std::log(250.0f);
        LadderFilter a, b;
        const float G = 0.05f;
        double diff = 0.0;
        for (int n = 0; n < 2000; ++n)
        {
            const float x = n == 10 ? 1.0f : 0.0f;
            diff = std::max(diff, (double) std::abs(a.process(x, G) - b.process(x, G, mapping::ladderFeedback(mapping::kDefaultFilterRes))));
        }
        CHECK(diff < 1.0e-6, "the default RESONANCE is the old fixed one (%g)", diff);
    }
    // ENVELOPE: the filter rests low and a note opens it
    {
        const double closed = gainDb(at(FilterType::Ladder, 300.0f, 0.3f), 2000.0), open = gainDb(at(FilterType::Envelope, 300.0f, 0.3f), 2000.0);
        CHECK(open > closed + 15.0, "ENVELOPE: a note at 2 kHz comes through a filter resting at 300 Hz (%g dB, against %g)", open, closed);
    }
    // fully right every type is off: the input comes out untouched
    for (int t = 0; t < kNumFilterTypes; ++t)
    {
        Params p = neutral();
        p.filterType = t;
        p.filterRes = 0.9f;
        const Signal in = sine(3000.0, 0.5);
        const Signal out = render(p, in);
        CHECK(std::abs(rms(out, 0.2, 0.5) - rms(in, 0.2, 0.5)) < 1.0e-4, "type %d fully right: off", t);
    }
    // the most resonance there is, on a loud rough signal: it stays finite and within bounds
    for (int t = 0; t < kNumFilterTypes; ++t)
    {
        Params p = at((FilterType) t, 800.0f, 1.0f);
        Signal in((size_t) kFs);
        for (size_t n = 0; n < in.size(); ++n)
            in[n] = 0.45f * (float) (std::sin(2.0 * 3.14159265358979 * 800.0 * (double) n / kFs) + std::sin(2.0 * 3.14159265358979 * 123.0 * (double) n / kFs));
        const Signal out = render(p, in);
        float peak = 0.0f;
        bool finite = true;
        for (float v : out)
        {
            finite = finite && std::isfinite(v);
            peak = std::max(peak, std::abs(v));
        }
        CHECK(finite && peak < 4.0f, "type %d at full RESONANCE stays finite and under 12 dB (peak %g)", t, (double) peak);
    }
    // a change of type while a tone plays: no bang
    {
        const Signal in = sine(440.0, 1.0, 0.4f);
        Params p = at(FilterType::Ladder, 600.0f, 0.5f);
        const Signal out = render(p, in, [](Params& q, size_t pos) { q.filterType = pos >= (size_t) kFs / 2 ? (int) FilterType::Band : (int) FilterType::Ladder; });
        float peak = 0.0f, jump = 0.0f;
        for (size_t n = 1; n < out.size(); ++n)
        {
            peak = std::max(peak, std::abs(out[n]));
            jump = std::max(jump, std::abs(out[n] - out[n - 1]));
        }
        CHECK(peak < 1.0f && jump < 0.2f, "switching LADDER to BAND PASS mid-note: peak %g, biggest step %g", (double) peak, (double) jump);
    }
}

void testAttackDetector()
{
    std::puts("attack detector: one trigger per pick, none on sustain");
    auto count = [](const Signal& s, float sens)
    {
        AttackDetector d;
        d.prepare(kFs);
        d.setSensitivity(sens);
        int n = 0;
        for (float v : s)
            n += d.process(v);
        return n;
    };

    Signal plucks((size_t) (2.0 * kFs), 0.0f);
    for (double start : { 0.3, 1.2 })
        for (size_t i = 0; i < (size_t) (0.8 * kFs); ++i)
        {
            const double t = (double) i / kFs;
            plucks[(size_t) (start * kFs) + i] += 0.5f * (float) (std::exp(-t / 0.15) * std::sin(2 * 3.14159265358979 * 196.0 * t));
        }
    CHECK(count(plucks, 0.5f) == 2, "plucks gave %d triggers", count(plucks, 0.5f));
    CHECK(count(plucks, 1.0f) == 2, "sensitive: %d triggers", count(plucks, 1.0f));

    Signal steady = sine(330.0, 2.0, 0.4f);
    CHECK(count(steady, 0.5f) == 1, "steady tone gave %d triggers", count(steady, 0.5f));

    // How hard a note has to be hit: a pick of a given loudness (peak) after silence.
    auto pickAt = [&](float peak, float sens)
    {
        Signal s((size_t) (1.0 * kFs), 0.0f);
        Rng rng(3);
        for (size_t i = 0; i < (size_t) (0.05 * kFs); ++i)
            s[(size_t) (0.2 * kFs) + i] = peak * rng.next() * std::exp(-(float) i / (0.02f * (float) kFs));
        return count(s, sens);
    };
    CHECK(pickAt(0.2f, 0.0f) == 1 && pickAt(0.005f, 0.0f) == 0, "lowest setting: a normal pick triggers, a -46 dBFS one does not");
    CHECK(pickAt(0.02f, 0.5f) == 1, "middle setting: a soft pick (-34 dBFS) triggers");
    CHECK(pickAt(0.002f, 1.0f) == 1, "highest setting: a very soft pick (-54 dBFS) triggers");
    CHECK(AttackDetector::floorDb(0.0f) == -40.0f && AttackDetector::floorDb(1.0f) == -62.0f, "trigger floor runs from -40 to -62 dBFS");

    // What the editor shows: the loudest input since it last looked, the picks caught (even when picks may not trigger)
    // and whether the effect side is busy.
    {
        Engine e;
        e.prepare(kFs);
        Params p = neutral();
        p.pickTriggers = false;
        p.stutter = 2;
        Signal in((size_t) kFs, 0.0f);
        for (size_t i = 0; i < 2400; ++i)
            in[4800 + i] = 0.6f * std::sin((float) i * 0.2f) * std::exp(-(float) i / 800.0f);
        float* ch = in.data();
        e.process(&ch, 1, (int) in.size(), p);
        const float peak = e.takeInputPeak();
        CHECK(peak > 0.4f && e.takeInputPeak() == 0.0f, "the input meter sees the pick (%.2f) and starts again after each look", peak);
        CHECK(e.getAttackCount() == 1 && !e.isEffectSideActive(), "the pick is counted for the meter light, but with picks off nothing plays");
        p.pickTriggers = true;
        Engine f;
        f.prepare(kFs);
        Signal again(in);
        bool busy = false;
        for (size_t pos = 0; pos < again.size(); pos += 256)
        {
            float* c = again.data() + pos;
            f.process(&c, 1, (int) std::min<size_t>(256, again.size() - pos), p); // the last block is shorter
            busy = busy || f.isEffectSideActive();
        }
        CHECK(busy, "with picks on, the stutter it starts lights the effect side");
    }
}

// A short noise burst at 0.1 s in otherwise silent audio: easy to follow through the stutter.
Signal singleBurst(double seconds)
{
    Signal s((size_t) (seconds * kFs), 0.0f);
    Rng rng(7);
    for (size_t i = 0; i < (size_t) (0.02 * kFs); ++i)
        s[(size_t) (0.1 * kFs) + i] = 0.6f * rng.next() * (1.0f - (float) i / (0.02f * (float) kFs));
    return s;
}

void testStutter()
{
    std::puts("stutter: record on the pick, repeat the snippet N times, then return to live");
    Params p = neutral();
    p.tempoBpm = 120.0f; // 1/8 note = 250 ms snippet
    p.stutter = 2;       // full speed, twice
    const Signal in = singleBurst(1.6);
    const Signal out = render(p, in);

    // The original burst plus two repeats ~250 ms and ~500 ms later, then silence.
    CHECK(rms(out, 0.10, 0.125) > 0.05, "original burst missing");
    CHECK(rms(out, 0.34, 0.37) > 0.05, "first repeat missing (rms %g)", rms(out, 0.34, 0.37));
    CHECK(rms(out, 0.59, 0.62) > 0.05, "second repeat missing (rms %g)", rms(out, 0.59, 0.62));
    CHECK(rms(out, 0.70, 1.6) < 1.0e-4, "output should return to silence (rms %g)", rms(out, 0.70, 1.6));
    CHECK(rms(out, 0.20, 0.33) < 1.0e-4, "gap before the first repeat should be silent");

    // Double speed: snippet plays twice as fast, so a repeat arrives after the recording + half the time.
    Params dbl = p;
    dbl.stutter = 8; // double speed, once
    const Signal outDbl = render(dbl, in);
    CHECK(rms(outDbl, 0.34, 0.37) > 0.05, "double-speed repeat missing");
    CHECK(rms(outDbl, 0.50, 1.6) < 1.0e-4, "double-speed output should be over by ~0.5 s");

    // Stutter off leaves only the original burst.
    p.stutter = 0;
    CHECK(rms(render(p, in), 0.30, 1.6) < 1.0e-4, "stutter off still repeating");

    // Hold: pressing it before the pick makes the snippet loop until released.
    p.stutter = 1;
    const Signal held = render(p, singleBurst(2.0), [](Params& q, size_t pos)
                               { q.stutterHold = pos < (size_t) (1.6 * kFs); });
    CHECK(rms(held, 1.00, 1.05) + rms(held, 1.10, 1.15) + rms(held, 1.20, 1.25) + rms(held, 1.30, 1.35) > 0.02,
          "held stutter stopped looping");
    CHECK(rms(held, 1.8, 2.0) < 1.0e-4, "released hold should stop (rms %g)", rms(held, 1.8, 2.0));
}

// WOBBLE on the delay: off, the echoes are exact copies; on, their pitch wavers (wow and flutter) by about the amount
// a worn tape gives, without changing their level much or ever running away.
void testDelayWobble()
{
    std::puts("delay wobble");
    constexpr int fs = 48000;
    auto echoOf = [&](float wobble, int repeats, float decay, int seconds, const std::function<float(int)>& input)
    {
        Delay d;
        d.prepare(fs);
        DelaySettings s;
        s.timeSamples = 12000.0;
        s.repeats = repeats;
        s.decay = decay;
        s.wobble = wobble;
        std::vector<float> out((size_t) (fs * seconds));
        double mostWobble = 0.0;
        for (int n = 0; n < (int) out.size(); ++n)
        {
            out[(size_t) n] = d.process(input(n), s);
            mostWobble = std::max(mostWobble, std::abs(d.getWobbleSamples()));
        }
        return std::make_pair(out, mostWobble);
    };
    // off: an impulse comes back exactly a delay later, as loud as it should
    {
        const auto [out, most] = echoOf(0.0f, 2, 0.5f, 1, [](int n) { return n == 100 ? 1.0f : 0.0f; });
        CHECK(std::abs(out[12100] - 1.0f) < 1.0e-6f && std::abs(out[24100] - Delay::decayGain(0.5f)) < 1.0e-6f && most == 0.0,
              "WOBBLE off: the echoes are exact (%g, %g)", (double) out[12100], (double) out[24100]);
    }
    // on: a 1 kHz tone's echo wavers in pitch
    auto tone = [](int n) { return 0.3f * (float) std::sin(6.283185307179586 * 1000.0 * n / 48000.0); };
    auto periods = [&](const std::vector<float>& out, int from)
    {
        std::vector<double> p;
        double last = -1.0;
        for (int n = from + 1; n < (int) out.size(); ++n)
            if (out[(size_t) n - 1] < 0.0f && out[(size_t) n] >= 0.0f)
            {
                const double at = (double) (n - 1) + out[(size_t) n - 1] / (out[(size_t) n - 1] - out[(size_t) n]);
                if (last >= 0.0)
                    p.push_back(at - last);
                last = at;
            }
        return p;
    };
    auto swing = [&](const std::vector<double>& p)
    {
        double most = 0.0;
        for (double v : p)
            most = std::max(most, std::abs(v / 48.0 - 1.0));
        return most;
    };
    auto rmsFrom = [](const std::vector<float>& v, int from)
    {
        double acc = 0.0;
        for (size_t i = (size_t) from; i < v.size(); ++i)
            acc += (double) v[i] * v[i];
        return std::sqrt(acc / (double) (v.size() - (size_t) from));
    };
    const auto clean = echoOf(0.0f, 1, 0.5f, 4, tone);
    const auto worn = echoOf(1.0f, 1, 0.5f, 4, tone);
    const double cleanSwing = swing(periods(clean.first, fs)), wornSwing = swing(periods(worn.first, fs));
    CHECK(cleanSwing < 1.0e-3 && wornSwing > 2.0e-3 && wornSwing < 0.02,
          "WOBBLE on: the echo's pitch wavers (%.2f%%, clean %.3f%%)", wornSwing * 100.0, cleanSwing * 100.0);
    CHECK(worn.second > 40.0 && worn.second < 120.0, "by moving the delay up to %g samples", worn.second);
    const double level = rmsFrom(worn.first, fs) / rmsFrom(clean.first, fs);
    CHECK(level > 0.6 && level < 1.1, "at about the same level (x%.2f)", level);
    // forever, with the most feedback, it stays finite and bounded
    {
        const auto [out, most] = echoOf(1.0f, kDelayForever, 1.0f, 8, [&](int n) { return n < fs ? tone(n) : 0.0f; });
        float peak = 0.0f;
        bool finite = true;
        for (float v : out)
        {
            finite = finite && std::isfinite(v);
            peak = std::max(peak, std::abs(v));
        }
        CHECK(finite && peak < 0.6f, "FOREVER with full wobble stays bounded (peak %g)", (double) peak);
    }
}

// The stutter types, on one channel: a 100 ms slice of a rising tone (so forwards and backwards differ), four repeats.
void testStutterKinds()
{
    std::puts("stutter types");
    constexpr int len = 4800, repeats = 4;
    struct Run
    {
        Signal in, out;
        std::vector<float> speed;
        int activeFor = 0;
    };
    auto run = [&](StutterKind kind, bool hold = false, int samples = 48000)
    {
        StutterChannel st;
        st.prepare(kFs, 48000);
        Run r;
        r.in.resize((size_t) samples);
        double phase = 0.0;
        for (int n = 0; n < samples; ++n)
        {
            phase += (200.0 + 4.0 * (double) n / 48.0) / kFs; // a sweep, 200 Hz up
            r.in[(size_t) n] = 0.5f * (float) std::sin(6.283185307179586 * phase);
        }
        for (int n = 0; n < samples; ++n)
        {
            if (n == 1000)
                st.trigger(len, 0, 1.0f, repeats, false, hold, kind);
            r.out.push_back(st.process(r.in[(size_t) n]));
            r.speed.push_back(st.isPlaying() ? st.getSpeed() : 0.0f);
            r.activeFor += st.isActive() ? 1 : 0;
        }
        return r;
    };
    const int recordEnd = 1000 + len;
    auto sliceRms = [](const Signal& s, int from, int to)
    {
        double acc = 0.0;
        for (int i = from; i < to; ++i)
            acc += (double) s[(size_t) i] * s[(size_t) i];
        return std::sqrt(acc / std::max(1, to - from));
    };
    auto maxJump = [](const Signal& s, int from, int to)
    {
        float most = 0.0f;
        for (int i = from + 1; i < to; ++i)
            most = std::max(most, std::abs(s[(size_t) i] - s[(size_t) i - 1]));
        return most;
    };

    const Run repeat = run(StutterKind::Repeat);
    CHECK(std::abs(repeat.activeFor - (len + repeats * len)) < 8, "REPEAT: the slice and four repeats (%d samples)", repeat.activeFor);
    float diff = 0.0f;
    for (int k = 200; k < len - 200; ++k)
        diff = std::max(diff, std::abs(repeat.out[(size_t) (recordEnd + k)] - repeat.in[(size_t) (1000 + k)]));
    CHECK(diff < 1.0e-4f, "REPEAT plays the slice as it was (%g)", (double) diff);

    const Run reverse = run(StutterKind::Reverse);
    diff = 0.0f;
    for (int k = 200; k < len - 200; ++k)
        diff = std::max(diff, std::abs(reverse.out[(size_t) (recordEnd + k)] - reverse.in[(size_t) (1000 + len - 1 - k)]));
    CHECK(diff < 0.02f && std::abs(reverse.activeFor - repeat.activeFor) < 8, "REVERSE plays it backwards, as many times (%g)", (double) diff);

    const Run rise = run(StutterKind::Rise);
    const float lastSpeed = rise.speed[(size_t) (1000 + rise.activeFor - 2)];
    CHECK(std::abs(lastSpeed - std::pow(StutterChannel::kRiseStep, 3.0f)) < 1.0e-3f && rise.activeFor < repeat.activeFor,
          "RISE: each repeat a tone higher (the fourth at %gx), so it is over sooner", (double) lastSpeed);

    const Run stop = run(StutterKind::TapeStop);
    bool slowing = true;
    for (int n = recordEnd + 1; n < recordEnd + repeats * len - 1; ++n)
        slowing = slowing && stop.speed[(size_t) n] <= stop.speed[(size_t) n - 1] + 1.0e-6f;
    CHECK(slowing && stop.speed[(size_t) (recordEnd + 10)] > 0.99f && stop.speed[(size_t) (recordEnd + repeats * len - 10)] < 0.01f,
          "TAPE STOP slows from full speed to a stop");
    CHECK(std::abs(stop.activeFor - repeat.activeFor) < 8 && sliceRms(stop.out, recordEnd + repeats * len - 300, recordEnd + repeats * len) < 0.02,
          "over four slices, fading as it stops (%g)", sliceRms(stop.out, recordEnd + repeats * len - 300, recordEnd + repeats * len));
    const Run stopHeld = run(StutterKind::TapeStop, true);
    CHECK(stopHeld.activeFor > 40000, "held, it stops and starts again for as long as the hold lasts");

    const Run scratch = run(StutterKind::Scratch);
    float lowest = 1.0f, highest = 0.0f;
    for (int n = recordEnd; n < recordEnd + repeats * len; ++n)
    {
        lowest = std::min(lowest, scratch.speed[(size_t) n]);
        highest = std::max(highest, scratch.speed[(size_t) n]);
    }
    CHECK(lowest < -1.5f && highest > 1.5f && std::abs(scratch.activeFor - repeat.activeFor) < 8,
          "SCRATCH pushes the slice forwards and pulls it back (%g to %g)", (double) lowest, (double) highest);
    CHECK(maxJump(scratch.out, recordEnd, recordEnd + repeats * len) < 0.2f, "without clicks (%g)",
          (double) maxJump(scratch.out, recordEnd, recordEnd + repeats * len));

    const Run freeze = run(StutterKind::Freeze);
    const double q1 = sliceRms(freeze.out, recordEnd + 1000, recordEnd + 1000 + len), q2 = sliceRms(freeze.out, recordEnd + 2 * len, recordEnd + 3 * len);
    CHECK(q1 > 0.15 && std::abs(q1 / q2 - 1.0) < 0.25 && std::abs(freeze.activeFor - repeat.activeFor) < 8,
          "FREEZE holds a steady sound for four slices (%g, %g)", q1, q2);
    CHECK(maxJump(freeze.out, recordEnd, recordEnd + repeats * len) < 0.15f, "without clicks (%g)",
          (double) maxJump(freeze.out, recordEnd, recordEnd + repeats * len));

    const Run fall = run(StutterKind::Fall);
    const float fallLast = fall.speed[(size_t) (1000 + fall.activeFor - 2)];
    CHECK(std::abs(fallLast - std::pow(1.0f / StutterChannel::kRiseStep, 3.0f)) < 1.0e-3f && fall.activeFor > repeat.activeFor + len / 2,
          "FALL: each repeat a tone lower (the fourth at %gx), so it lasts longer", (double) fallLast);

    const Run shuffle = run(StutterKind::Shuffle);
    const double shufRms = sliceRms(shuffle.out, recordEnd, recordEnd + repeats * len);
    const double repRms = sliceRms(repeat.out, recordEnd, recordEnd + repeats * len);
    float moved = 0.0f;
    for (int k = 200; k < repeats * len - 200; ++k)
        moved = std::max(moved, std::abs(shuffle.out[(size_t) (recordEnd + k)] - repeat.out[(size_t) (recordEnd + k)]));
    CHECK(std::abs(shuffle.activeFor - repeat.activeFor) < 8 && std::abs(shufRms / repRms - 1.0) < 0.3 && moved > 0.1f,
          "SHUFFLE: the same slice, as loud, in another order (rms %g vs %g, moved %g)", shufRms, repRms, (double) moved);
    CHECK(maxJump(shuffle.out, recordEnd, recordEnd + repeats * len) < 0.2f, "without clicks (%g)",
          (double) maxJump(shuffle.out, recordEnd, recordEnd + repeats * len));

    const Run half = run(StutterKind::Half);
    CHECK(std::abs(half.speed[(size_t) (recordEnd + 10)] - 0.5f) < 1.0e-3f && std::abs(half.activeFor - (len + 2 * repeats * len)) < 8,
          "HALF plays an octave down, so the repeats take twice as long (%d samples)", half.activeFor);

    const Run gate = run(StutterKind::Gate);
    int gateQuiet = 0;
    for (int n = recordEnd; n < recordEnd + repeats * len; ++n)
        gateQuiet += std::abs(gate.out[(size_t) n]) < 1.0e-4f ? 1 : 0;
    CHECK(std::abs(gate.activeFor - repeat.activeFor) < 8 && gateQuiet > repeats * len / 4,
          "GATE chops the repeats (%d of %d samples silent)", gateQuiet, repeats * len);
    CHECK(maxJump(gate.out, recordEnd, recordEnd + repeats * len) < 0.2f, "without clicks (%g)",
          (double) maxJump(gate.out, recordEnd, recordEnd + repeats * len));

    for (const Run* r : { &stop, &scratch, &freeze })
        CHECK(maxJump(r->out, 1000 + r->activeFor - 50, 1000 + r->activeFor + 400) < 0.2f, "and back to the live sound smoothly");

    // through the engine: the type is what Params says
    Params p = neutral();
    p.tempoBpm = 120.0f;
    p.stutter = 2;
    const Signal in = singleBurst(1.0);
    Params rev = p;
    rev.stutterKind = StutterKind::Reverse;
    auto differ = [](const Signal& a, const Signal& b, double t0, double t1)
    {
        double acc = 0.0;
        const size_t from = (size_t) (t0 * kFs), to = std::min(a.size(), (size_t) (t1 * kFs));
        for (size_t i = from; i < to; ++i)
            acc += (double) (a[i] - b[i]) * (a[i] - b[i]);
        return std::sqrt(acc / (double) std::max<size_t>(1, to - from));
    };
    const Signal plainOut = render(p, in), revOut = render(rev, in);
    CHECK(differ(plainOut, revOut, 0.33, 0.62) > 0.01 && differ(plainOut, revOut, 0.0, 0.3) < 1.0e-6,
          "the engine plays the stutter type it is given");
}

double dominantRatio(const Signal& s, double t0, double t1, double lo, double hi)
{
    // power at hi / power at lo
    return tonePower(s, t0, t1, hi) / std::max(1.0e-18, tonePower(s, t0, t1, lo));
}

void testSequencer()
{
    std::puts("sequencer: pitch / filter / sample-rate steps, skip, mute, repeat modes");
    const Signal tone = sine(440.0, 3.0, 0.5f);

    // Pitch: every step an octave up, continuous.
    Params p;
    p.seqType = SeqType::Pitch;
    p.seqPlay = 5;
    p.tempoBpm = 600.0f; // 100 ms per step at x1
    p.seqMult = 0;
    p.steps.fill(mapping::pitchStepKnob(12));
    Signal out = render(p, tone);
    CHECK(dominantRatio(out, 0.5, 2.5, 440.0, 880.0) > 10.0, "octave-up not heard (ratio %g)", dominantRatio(out, 0.5, 2.5, 440.0, 880.0));

    // Pitch: octave down.
    p.steps.fill(mapping::pitchStepKnob(-12));
    out = render(p, tone);
    CHECK(dominantRatio(out, 0.5, 2.5, 440.0, 220.0) > 10.0, "octave-down not heard");

    // Mute: every step muted -> silence.
    p.steps.fill(1.0f);
    out = render(p, tone);
    CHECK(rms(out, 0.3, 2.5) < 1.0e-4, "muted sequence not silent (rms %g)", rms(out, 0.3, 2.5));

    // Skip: only step 3 is active, everything else skipped; sequence length = 1 step.
    p.steps.fill(0.0f);
    p.steps[2] = mapping::pitchStepKnob(12);
    out = render(p, tone);
    CHECK(dominantRatio(out, 0.5, 2.5, 440.0, 880.0) > 10.0, "single active step not heard");

    // Play once: after the sequence has run (6 x 100 ms) the signal returns to dry.
    p.steps.fill(mapping::pitchStepKnob(12));
    p.seqPlay = 1;
    out = render(p, tone);
    CHECK(dominantRatio(out, 0.2, 0.5, 440.0, 880.0) > 10.0, "once: sequence not heard during playback");
    CHECK(dominantRatio(out, 1.2, 2.5, 880.0, 440.0) > 100.0, "once: should be back to dry afterwards");

    // Filter sequencer: each step sets the cutoff to ~10% of the (fully open) front-panel cutoff.
    Params f;
    f.seqType = SeqType::Filter;
    f.seqPlay = 5;
    f.tempoBpm = 600.0f;
    f.seqMult = 0;
    f.steps.fill(0.03f + 0.94f * 0.1f);
    const Signal hi = sine(10000.0, 2.0, 0.5f);
    out = render(f, hi);
    CHECK(db(rms(out, 0.5, 2.0) / rms(hi, 0.5, 2.0)) < -30.0, "filter steps did not close the filter (%g dB)",
          db(rms(out, 0.5, 2.0) / rms(hi, 0.5, 2.0)));

    // Sample-rate sequencer: 10% of 48 kHz = 4.8 kHz -> hold of 10 samples.
    Params s;
    s.seqType = SeqType::SampleRate;
    s.seqPlay = 5;
    s.tempoBpm = 600.0f;
    s.seqMult = 0;
    s.steps.fill(0.03f + 0.94f * 0.1f);
    out = render(s, noise(1.0));
    size_t same = 0, total = 0;
    for (size_t i = 6000; i + 1 < out.size(); ++i, ++total)
        same += std::abs(out[i] - out[i + 1]) < 1.0e-6f; // held = unchanged (up to rounding)
    CHECK(std::abs((double) same / (double) total - 0.9) < 0.02, "hold fraction %g", (double) same / (double) total);

    // Audio-band sequencing (x128 at 6000 BPM) must still be stable, and gates the signal (ring-mod-like).
    Params fast;
    fast.seqType = SeqType::Pitch;
    fast.seqPlay = 5;
    fast.tempoBpm = 6000.0f;
    fast.seqMult = 7;
    fast.steps = { 0.5f, 1.0f, 0.5f, 1.0f, 0.5f, 1.0f }; // dry / mute alternating
    out = render(fast, tone);
    bool finite = true;
    for (float v : out)
        finite = finite && std::isfinite(v) && std::abs(v) < 4.0f;
    CHECK(finite, "audio-band sequencer produced bad samples");
    CHECK(rms(out, 0.5, 2.5) < 0.7 * rms(tone, 0.5, 2.5), "audio-rate muting should reduce level");
}

void testSwing()
{
    std::puts("swing");
    const Signal tone = sine(440.0, 3.0, 0.5f);
    // A dry / mute / dry / mute sequence: how long each loud stretch lasts, and how far apart they start.
    auto measure = [&](float swing)
    {
        Params p;
        p.seqPlay = 5;
        p.tempoBpm = 600.0f; // 100 ms per step at x1
        p.seqMult = 0;
        p.swing = swing;
        p.steps.fill(1.0f); // mute
        for (size_t i = 0; i < p.steps.size(); i += 2)
            p.steps[i] = 0.5f; // dry on the odd steps
        const Signal out = render(p, tone);
        // loud while a sample above the threshold was heard in the last 3 ms (the tone crosses zero every ~1 ms)
        const int hold = (int) (0.003 * kFs);
        int sinceLoud = 1000000;
        bool loud = false;
        size_t start = 0, lastStart = 0;
        std::vector<double> lengths, spacings;
        for (size_t i = (size_t) (0.25 * kFs); i < out.size(); ++i)
        {
            sinceLoud = std::abs(out[i]) > 0.2f ? 0 : sinceLoud + 1;
            if (!loud && sinceLoud == 0)
            {
                loud = true;
                if (lastStart > 0)
                    spacings.push_back((double) (i - lastStart) / kFs);
                lastStart = start = i;
            }
            else if (loud && sinceLoud > hold)
            {
                loud = false;
                lengths.push_back((double) (i - sinceLoud - start) / kFs);
            }
        }
        auto mean = [](const std::vector<double>& v)
        {
            double acc = 0;
            for (double d : v)
                acc += d;
            return v.empty() ? 0.0 : acc / (double) v.size();
        };
        return std::pair { mean(lengths), mean(spacings) };
    };
    const auto straight = measure(0.0f), swung = measure(1.0f), half = measure(0.5f);
    CHECK(std::abs(straight.first - 0.100) < 0.012, "no swing: each dry step lasts ~100 ms (got %g s)", straight.first);
    CHECK(std::abs(swung.first - 0.1333) < 0.012, "full swing: the odd steps stretch to ~133 ms (got %g s)", swung.first);
    CHECK(std::abs(half.first - 0.1167) < 0.012, "half swing sits in between (~117 ms, got %g s)", half.first);
    CHECK(std::abs(straight.second - 0.200) < 0.012 && std::abs(swung.second - 0.200) < 0.012,
          "each pair of steps keeps the tempo (spacing %g and %g s)", straight.second, swung.second);
}

void testRingMod()
{
    std::puts("ring mod");
    const Signal tone = sine(440.0, 3.0, 0.5f);
    Params p; // a continuous sequence of dry steps, so the effect side is active
    p.seqPlay = 5;
    p.tempoBpm = 600.0f;
    p.seqMult = 0;
    p.steps.fill(0.5f);
    p.ringOn = true;
    p.ringHz = 1000.0f;
    p.ringMix = 1.0f;
    const Signal out = render(p, tone);
    // y * sin(1 kHz) moves a 440 Hz tone to 560 and 1440 Hz
    const double side = tonePower(out, 0.5, 2.5, 560.0) + tonePower(out, 0.5, 2.5, 1440.0);
    const double carrier = tonePower(out, 0.5, 2.5, 440.0);
    CHECK(side > 100.0 * carrier, "full ring mod: sidebands at 560 / 1440 Hz, the 440 Hz tone gone (ratio %g)", side / std::max(carrier, 1.0e-12));

    // at MIX 0 the sound passes as it is
    p.ringMix = 0.0f;
    const Signal dry = render(p, tone);
    double diff = 0.0;
    for (size_t i = (size_t) (0.5 * kFs); i < dry.size(); ++i)
        diff = std::max(diff, (double) std::abs(dry[i] - tone[i]));
    CHECK(diff < 0.02, "ring mod at MIX 0 leaves the sound alone (diff %g)", diff);

    // while nothing plays, the clean signal is never rung
    Params idle = neutral();
    idle.ringOn = true;
    idle.ringHz = 1000.0f;
    idle.ringMix = 1.0f;
    const Signal clean = render(idle, tone);
    diff = 0.0;
    for (size_t i = (size_t) (0.5 * kFs); i < clean.size(); ++i)
        diff = std::max(diff, (double) std::abs(clean[i] - tone[i]));
    CHECK(diff < 0.02, "ring mod never touches the signal while no sequence or stutter plays (diff %g)", diff);
}

void testPing()
{
    std::puts("filter ping");
    const Signal silence((size_t) (2.0 * kFs), 0.0f);
    Params p;
    p.seqPlay = 5; // continuous: the sequence runs with no input
    p.tempoBpm = 600.0f;
    p.seqMult = 0;
    p.steps.fill(0.5f); // dry steps
    p.filterType = (int) FilterType::Ladder;
    p.filterRes = 0.93f;
    p.filter = std::log(800.0f / mapping::kFilterMinHz) / std::log(mapping::kFilterMaxHz / mapping::kFilterMinHz);
    p.pingOn = true;
    p.pingLevel = 0.8f;
    const Signal rung = render(p, silence);
    const double at = tonePower(rung, 0.3, 1.8, 800.0), below = tonePower(rung, 0.3, 1.8, 150.0), above = tonePower(rung, 0.3, 1.8, 5000.0);
    CHECK(rms(rung, 0.3, 1.8) > 1.0e-3, "the ping sounds with no input at all (rms %g)", rms(rung, 0.3, 1.8));
    CHECK(at > 3.0 * below && at > 3.0 * above, "and rings at the cutoff (800 Hz %g, 150 Hz %g, 5 kHz %g)", at, below, above);

    Params off = p;
    off.pingOn = false;
    CHECK(rms(render(off, silence), 0.3, 1.8) < 1.0e-5, "off, silence stays silence");

    Params muted = p;
    muted.steps.fill(1.0f); // every step muted
    CHECK(rms(render(muted, silence), 0.3, 1.8) < 1.0e-5, "muted steps are not pinged");
}

void testBloom()
{
    std::puts("res bloom");
    // the gain of a tone at the cutoff: RES BLOOM should peak harder the louder the note
    auto gainAt = [](FilterType t, float amp)
    {
        Params p = neutral();
        p.filterType = (int) t;
        p.filterRes = 0.1f;
        p.filter = std::log(500.0f / mapping::kFilterMinHz) / std::log(mapping::kFilterMaxHz / mapping::kFilterMinHz);
        const Signal in = sine(500.0, 2.0, amp);
        return db(std::sqrt(tonePower(render(p, in), 0.5, 1.8, 500.0) / tonePower(in, 0.5, 1.8, 500.0)));
    };
    const double ladder = gainAt(FilterType::Ladder, 0.4f), bloom = gainAt(FilterType::Bloom, 0.4f), gentle = gainAt(FilterType::Bloom, 0.015f);
    CHECK(bloom > ladder + 4.0, "a held note pushes RES BLOOM's resonance up (ladder %g dB, bloom %g dB)", ladder, bloom);
    CHECK(bloom > gentle + 3.0, "and a quiet note blooms less (quiet %g dB)", gentle);
}

void testFormant()
{
    std::puts("formant filter");
    // the gain of a tone through the FORMANT type, with the FILTER knob on a vowel (0 = U .. 4 = I)
    auto gainAt = [](float position, double f)
    {
        Params p = neutral();
        p.filterType = (int) FilterType::Formant;
        p.filterRes = 0.5f;
        const float hz = mapping::kFilterMinHz * std::pow(mapping::kFormantTopHz / mapping::kFilterMinHz, position / 4.0f);
        p.filter = std::log(hz / mapping::kFilterMinHz) / std::log(mapping::kFilterMaxHz / mapping::kFilterMinHz);
        const Signal in = sine(f, 1.5, 0.3f);
        return db(std::sqrt(tonePower(render(p, in), 0.5, 1.4, f) / tonePower(in, 0.5, 1.4, f)));
    };
    const double a730 = gainAt(2.0f, 730.0), i730 = gainAt(4.0f, 730.0), a2290 = gainAt(2.0f, 2290.0), i2290 = gainAt(4.0f, 2290.0);
    CHECK(a730 > i730 + 6.0, "on A the first formant (730 Hz) passes, on I it does not (%g dB against %g)", a730, i730);
    CHECK(i2290 > a2290 + 6.0, "on I the second formant (2290 Hz) passes, on A it does not (%g dB against %g)", i2290, a2290);

    const auto u = mapping::vowelAt(0.0f), i = mapping::vowelAt(4.0f), between = mapping::vowelAt(2.5f);
    CHECK(sameFloat(u.f1, 300.0f) && sameFloat(i.f2, 2290.0f) && between.f2 > 1090.0f && between.f2 < 1840.0f,
          "the vowels run U to I, gliding between them (A-E F2 %g Hz)", (double) between.f2);
    CHECK(mapping::vowelPosition(mapping::kFilterMinHz) < 0.01f && mapping::vowelPosition(mapping::kFormantTopHz) > 3.99f,
          "the bottom of the knob is U and the vowel top is I");
}

void testPhaserFlanger()
{
    std::puts("phaser and flanger");
    const Signal tone = sine(1000.0, 4.5, 0.4f);
    auto run = [&](int type, float mix, bool playing)
    {
        Params p = playing ? Params {} : neutral();
        if (playing)
        {
            p.seqPlay = 5; // a continuous sequence of dry steps, so the effect side acts
            p.seqMult = 0;
            p.steps.fill(0.5f);
        }
        p.tempoBpm = 120.0f;
        p.modOn = true;
        p.modType = type;
        p.modRateBeats = 4.0f; // one bar: 2 s at 120 BPM
        p.modDepth = 0.9f;
        p.modFeedback = 0.6f;
        p.modMix = mix;
        return render(p, tone);
    };
    // the tone's level in 50 ms windows over one sweep: a moving notch makes it swing
    auto swingOf = [](const Signal& s)
    {
        double lo = 1.0e9, hi = 0.0;
        for (double t = 1.0; t < 3.0; t += 0.05)
        {
            const double r = rms(s, t, t + 0.05);
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        return lo / std::max(hi, 1.0e-12);
    };
    const Signal phased = run(0, 1.0f, true), flanged = run(1, 1.0f, true);
    CHECK(swingOf(phased) < 0.5, "PHASER: a notch sweeps across the tone (lowest / highest level %g)", swingOf(phased));
    CHECK(swingOf(flanged) < 0.5, "FLANGER: its comb sweeps across the tone (lowest / highest level %g)", swingOf(flanged));

    // the sweep keeps time: at 120 BPM a one-bar sweep comes round again every 2 s
    double drift = 0.0, level = 0.0;
    for (double t = 1.0; t < 2.0; t += 0.05)
    {
        drift = std::max(drift, std::abs(rms(phased, t, t + 0.05) - rms(phased, t + 2.0, t + 2.05)));
        level = std::max(level, rms(phased, t, t + 0.05));
    }
    CHECK(drift < 0.1 * level, "and the sweep is locked to the bar (it repeats every 2 s at 120 BPM, drift %g)", drift);

    float peak = 0.0f;
    bool finite = true;
    for (const Signal* s : { &phased, &flanged })
        for (float v : *s)
        {
            finite = finite && std::isfinite(v);
            peak = std::max(peak, std::abs(v));
        }
    CHECK(finite && peak < 0.8f, "both stay finite and near the input's level with feedback (peak %g)", (double) peak);

    auto untouched = [&](const Signal& s)
    {
        double diff = 0.0;
        for (size_t n = (size_t) (0.5 * kFs); n < s.size(); ++n)
            diff = std::max(diff, (double) std::abs(s[n] - tone[n]));
        return diff;
    };
    CHECK(untouched(run(0, 0.0f, true)) < 0.02, "at MIX 0 the sound passes as it is");
    CHECK(untouched(run(1, 1.0f, false)) < 0.02, "and while no sequence or stutter plays, the clean signal is never swept");
}

void testRobustness()
{
    std::puts("robustness: random parameters and input must never produce NaN/Inf or runaway levels");
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (int trial = 0; trial < 40; ++trial)
    {
        Params p;
        p.sampleRate = u(rng);
        p.filter = u(rng);
        p.bits = u(rng);
        p.stutter = (int) (u(rng) * 23.0f) % 23;
        p.seqPlay = (int) (u(rng) * 6.0f) % 6;
        p.seqMult = (int) (u(rng) * 8.0f) % 8;
        for (auto& s : p.steps)
            s = u(rng);
        p.seqType = (SeqType) ((int) (u(rng) * 3.0f) % 3);
        p.tempoBpm = 23.4f + u(rng) * 5976.0f;
        p.inputGainDb = -12.0f + 24.0f * u(rng);
        p.lineHeadroom = u(rng) > 0.5f;
        p.stutterDivision = (int) (u(rng) * 3.0f) % 3;
        p.mix = u(rng);
        p.fxOnlyOnTriggers = u(rng) > 0.5f;
        Signal in = noise(1.5, 1.5f, (unsigned) trial);
        const Signal burst = singleBurst(1.5);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = (i / 24000) % 2 ? in[i] : burst[i] * 2.0f;
        const Signal out = render(p, in, [&](Params& q, size_t pos) { q.stutterHold = (pos / 12000) % 3 == 0; },
                                  1 + (int) (u(rng) * 700.0f));
        bool ok = true;
        float peak = 0;
        for (float v : out)
        {
            ok = ok && std::isfinite(v);
            peak = std::max(peak, std::abs(v));
        }
        CHECK(ok && peak < 12.0f, "trial %d: finite=%d peak=%g", trial, (int) ok, peak);
    }

    // Stereo + mono-sized buffers, with block sizes that do not divide evenly.
    Engine e;
    e.prepare(44100.0);
    Params p;
    p.stutter = 5;
    p.seqPlay = 5;
    Signal l = noise(0.5, 0.6f, 3), r = noise(0.5, 0.6f, 4);
    for (size_t pos = 0; pos < l.size(); pos += 333)
    {
        float* ch[2] = { l.data() + pos, r.data() + pos };
        e.process(ch, 2, (int) std::min<size_t>(333, l.size() - pos), p);
    }
    bool ok = true;
    for (size_t i = 0; i < l.size(); ++i)
        ok = ok && std::isfinite(l[i]) && std::isfinite(r[i]);
    CHECK(ok, "stereo path produced bad samples");
}

// Two knob sets that differ in every field, so a mix-up between them shows.
KnobSet morphSetA()
{
    KnobSet k;
    k.sampleRate = 0.2f;
    k.filter = 0.9f;
    k.bits = 0.4f;
    k.stutter = 3;
    k.seqPlay = 1;
    k.seqMult = 2;
    k.steps = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f };
    k.seqType = SeqType::Pitch;
    return k;
}

KnobSet morphSetB()
{
    KnobSet k;
    k.sampleRate = 0.8f;
    k.filter = 0.1f;
    k.bits = 1.0f;
    k.stutter = 20;
    k.seqPlay = 4;
    k.seqMult = 7;
    k.steps = { 0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.0f };
    k.seqType = SeqType::SampleRate;
    return k;
}

bool near(float a, float b, float tol = 1.0e-6f) { return std::abs(a - b) <= tol; }

void testMorph()
{
    std::puts("morph: toe-up / toe-down knob sets blend per block");
    const KnobSet up = morphSetA(), down = morphSetB();

    // The defaults of a knob set match the defaults of the engine parameters.
    CHECK(KnobSet {} == knobsOf(Params {}), "KnobSet defaults differ from Params defaults");

    // End points are exact.
    CHECK(morph::blend(up, down, 0.0f) == up, "expression 0 must give the toe-up set");
    CHECK(morph::blend(up, down, 1.0f) == down, "expression 1 must give the toe-down set");

    // Out-of-range and non-finite expression values are clamped (NaN and infinity count as 0).
    CHECK(morph::blend(up, down, -3.0f) == up, "negative expression clamps to toe up");
    CHECK(morph::blend(up, down, 7.5f) == down, "expression above 1 clamps to toe down");
    CHECK(morph::blend(up, down, std::nanf("")) == up, "NaN expression gives toe up");
    CHECK(morph::blend(up, down, INFINITY) == up, "infinite expression is rejected");

    // Continuous knobs: linear in knob position, the six steps independently.
    const KnobSet mid = morph::blend(up, down, 0.5f);
    CHECK(near(mid.sampleRate, 0.5f) && near(mid.filter, 0.5f) && near(mid.bits, 0.7f), "midpoint %g %g %g",
          (double) mid.sampleRate, (double) mid.filter, (double) mid.bits);
    const std::array<float, kNumSteps> midSteps { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.3f };
    for (size_t i = 0; i < midSteps.size(); ++i)
        CHECK(near(mid.steps[i], midSteps[i]), "step %d at midpoint is %g", (int) i, (double) mid.steps[i]);

    const KnobSet quarter = morph::blend(up, down, 0.25f);
    CHECK(near(quarter.sampleRate, 0.35f) && near(quarter.filter, 0.7f), "quarter point %g %g",
          (double) quarter.sampleRate, (double) quarter.filter);
    CHECK(near(quarter.steps[0], 0.3f) && near(quarter.steps[5], 0.45f), "quarter steps %g %g",
          (double) quarter.steps[0], (double) quarter.steps[5]);

    // A sweep never leaves the range spanned by the two sets and moves monotonically
    // (sample rate and bits rise from set A to set B, the filter falls).
    bool inRange = true, monotone = true;
    float prev[3] = { up.sampleRate, up.filter, up.bits };
    const float lo[3] = { std::min(up.sampleRate, down.sampleRate), std::min(up.filter, down.filter),
                          std::min(up.bits, down.bits) };
    const float hi[3] = { std::max(up.sampleRate, down.sampleRate), std::max(up.filter, down.filter),
                          std::max(up.bits, down.bits) };
    const bool expectRise[3] = { true, false, true };
    for (int i = 0; i <= 200; ++i)
    {
        const KnobSet k = morph::blend(up, down, (float) i / 200.0f);
        const float v[3] = { k.sampleRate, k.filter, k.bits };
        for (int j = 0; j < 3; ++j)
        {
            inRange = inRange && v[j] >= lo[j] - 1.0e-6f && v[j] <= hi[j] + 1.0e-6f;
            monotone = monotone && (expectRise[j] ? v[j] >= prev[j] - 1.0e-6f : v[j] <= prev[j] + 1.0e-6f);
            prev[j] = v[j];
        }
    }
    CHECK(inRange, "swept knob left the range between the two sets");
    CHECK(monotone, "swept knob was not monotonic");

    // Discrete knobs hop at the midpoint, all together.
    const KnobSet justBelow = morph::blend(up, down, 0.4999f);
    const KnobSet atMid = morph::blend(up, down, 0.5f);
    CHECK(justBelow.stutter == up.stutter && justBelow.seqPlay == up.seqPlay && justBelow.seqMult == up.seqMult &&
              justBelow.seqType == up.seqType,
          "discrete knobs must still be toe-up just below the midpoint");
    CHECK(atMid.stutter == down.stutter && atMid.seqPlay == down.seqPlay && atMid.seqMult == down.seqMult &&
              atMid.seqType == down.seqType,
          "discrete knobs must be toe-down from the midpoint on");
    bool onlyTwoValues = true;
    for (int i = 0; i <= 100; ++i)
    {
        const int stutter = morph::blend(up, down, (float) i / 100.0f).stutter;
        onlyTwoValues = onlyTwoValues && (stutter == up.stutter || stutter == down.stutter);
    }
    CHECK(onlyTwoValues, "a discrete knob must never take a value in between");

    // Two identical sets stay identical at every position (no drift from rounding).
    bool identical = true;
    const KnobSet def {};
    for (int i = 0; i <= 100; ++i)
        identical = identical && morph::blend(def, def, (float) i / 100.0f) == def;
    CHECK(identical, "blending a set with itself changed it");

    // applyKnobs only touches the knob fields.
    Params p;
    p.tempoBpm = 333.0f;
    p.bypass = true;
    p.stutterHold = true;
    p.inputGainDb = 5.0f;
    p.outputGainDb = -3.0f;
    p.lineHeadroom = true;
    p.triggerSensitivity = 0.9f;
    p.stutterDivision = 2;
    applyKnobs(p, down);
    CHECK(knobsOf(p) == down, "applyKnobs / knobsOf do not round-trip");
    CHECK(p.tempoBpm == 333.0f && p.bypass && p.stutterHold && p.inputGainDb == 5.0f && p.outputGainDb == -3.0f &&
              p.lineHeadroom && p.triggerSensitivity == 0.9f && p.stutterDivision == 2,
          "applyKnobs changed a setting that is not a knob");

    // sanitise() pulls everything into range.
    KnobSet bad;
    bad.sampleRate = 4.0f;
    bad.filter = -1.0f;
    bad.bits = std::nanf("");
    bad.stutter = 99;
    bad.seqPlay = -4;
    bad.seqMult = 12;
    bad.steps = { 2.0f, -2.0f, 0.5f, INFINITY, 0.25f, 1.0f };
    bad.seqType = (SeqType) 9;
    const KnobSet fixed = morph::sanitise(bad);
    CHECK(fixed.sampleRate == 1.0f && fixed.filter == 0.0f && fixed.bits == 0.0f, "sanitise knobs %g %g %g",
          (double) fixed.sampleRate, (double) fixed.filter, (double) fixed.bits);
    CHECK(fixed.stutter == 22 && fixed.seqPlay == 0 && fixed.seqMult == 7 && fixed.seqType == SeqType::SampleRate,
          "sanitise discrete knobs");
    CHECK(fixed.steps[0] == 1.0f && fixed.steps[1] == 0.0f && fixed.steps[2] == 0.5f && fixed.steps[3] == 0.0f &&
              fixed.steps[4] == 0.25f && fixed.steps[5] == 1.0f,
          "sanitise steps");
    CHECK(morph::sanitise(up) == up && morph::sanitise(down) == down, "sanitise changed a valid set");
}

void testMorphThroughEngine()
{
    std::puts("morph: sweeping the expression every block drives the engine smoothly");

    // Toe up is clean, toe down crushes to a few bits: the quantisation error must grow as the
    // expression moves, starting from nothing.
    KnobSet up;
    up.seqPlay = 0;
    KnobSet down = up;
    down.bits = 0.05f;

    const Signal in = sine(220.0, 2.0, 0.5f);
    const Params base;
    const Signal out = render(base, in,
                              [&](Params& p, size_t pos)
                              {
                                  const float expression = (float) ((double) pos / (double) in.size());
                                  applyKnobs(p, morph::blend(up, down, expression));
                              });

    Signal error(in.size());
    for (size_t i = 0; i < in.size(); ++i)
        error[i] = out[i] - in[i];
    const double e0 = rms(error, 0.0, 0.2), e1 = rms(error, 0.7, 0.9), e2 = rms(error, 1.2, 1.4),
                 e3 = rms(error, 1.8, 2.0);
    CHECK(e0 < 1.0e-6, "toe-up end must be clean, error %g", e0);
    CHECK(e1 > e0 && e2 > e1 && e3 > e2, "error should grow with the expression: %g %g %g %g", e0, e1, e2, e3);
    CHECK(e3 > 0.02, "toe-down end should be heavily crushed, error %g", e3);

    // Jumping between a clean set and a running sequencer every few blocks must stay well behaved.
    KnobSet a = up, b = up;
    b.seqPlay = 5;
    const Signal tone = sine(196.0, 1.5, 0.5f);
    const Signal hop = render(base, tone,
                              [&](Params& p, size_t pos)
                              { applyKnobs(p, morph::blend(a, b, (pos / 4096) % 2 ? 1.0f : 0.0f)); });
    bool finite = true;
    float peak = 0.0f;
    for (float v : hop)
    {
        finite = finite && std::isfinite(v);
        peak = std::max(peak, std::abs(v));
    }
    CHECK(finite && peak < 2.0f, "toggling between the two sets produced bad samples (peak %g)", (double) peak);
}

void testPresets()
{
    std::puts("presets: twelve factory sounds and eight empty slots, then the pack");
    CHECK(kNumFactoryPresets == 12 && kNumUserSlots == 8 && kFirstPackSlot == 20, "expected 12 + 8 slots before the pack");

    // Slot 1 is the sound the plug-in has always started with.
    const FactoryPreset first = factoryPreset(0);
    const Params def;
    Params fourTimes = def;
    fourTimes.seqPlay = 3;
    CHECK(first.knobs == knobsOf(fourTimes), "slot 1 must be the default knob set, played four times a pick");
    CHECK(first.knobs.seqPlay == 3 && first.knobs.seqType == SeqType::Pitch, "slot 1 must run the pitch sequencer on each pick");
    const float expected[kNumSteps] = { -7, -3, -12, 7, 4, 0 };
    for (int i = 0; i < kNumSteps; ++i)
    {
        const auto step = mapping::decodeStep(first.knobs.steps[(size_t) i], SeqType::Pitch);
        CHECK(step.kind == mapping::Step::Kind::Active && step.semitones == expected[i], "slot 1 step %d is %g", i,
              (double) step.semitones);
    }
    CHECK(first.tempoBpm == def.tempoBpm && first.stutterDivision == def.stutterDivision && !first.morphOn,
          "slot 1 tempo / stutter length / morph must be the defaults");
    CHECK(std::string(first.name) == "Pitch Sequence", "slot 1 name is %s", first.name);

    // Every slot is valid, named, and (unless it uses the morph) has matching toe snapshots.
    bool valid = true, named = true, unique = true;
    for (int i = 0; i < kFirstPackSlot; ++i)
    {
        const FactoryPreset f = factoryPreset(i);
        valid = valid && morph::sanitise(f.knobs) == f.knobs && morph::sanitise(f.toeUp) == f.toeUp &&
                morph::sanitise(f.toeDown) == f.toeDown && f.tempoBpm >= kMinTempoBpm && f.tempoBpm <= kMaxTempoBpm &&
                f.stutterDivision >= 0 && f.stutterDivision <= 2;
        named = named && f.name != nullptr && f.name[0] != '\0';
        if (!f.morphOn)
            valid = valid && f.toeUp == f.knobs && f.toeDown == f.knobs;
        for (int j = 0; j < i; ++j)
            unique = unique && std::string(f.name) != std::string(factoryPreset(j).name);
    }
    CHECK(valid, "a factory preset has a value outside its range");
    CHECK(named, "a factory preset has no name");
    CHECK(unique, "factory preset names must be unique");

    // The morph presets really use two different sets, starting at toe up.
    int morphing = 0;
    for (int i = 0; i < kFirstPackSlot; ++i)
    {
        const FactoryPreset f = factoryPreset(i);
        if (f.morphOn)
        {
            ++morphing;
            CHECK(!(f.toeUp == f.toeDown) && f.knobs == f.toeUp,
                  "morph preset %d must differ between toe up and toe down", i + 1);
        }
    }
    CHECK(morphing >= 1, "expected at least one factory preset that demonstrates the morph");

    // Out-of-range slot numbers are clamped, not undefined.
    CHECK(std::string(factoryPreset(-4).name) == "Pitch Sequence" &&
              std::string(factoryPreset(99).name) == std::string(factoryPreset(kFirstPackSlot - 1).name)
                  && std::string(factoryPreset(kFirstPackSlot - 1).name) == "Init 20",
          "slot numbers must be clamped");
}
} // namespace

// ---- dry/wet mix and "effects only on triggers" ----------------------------------------------

// Sample-rate, bit-depth and filter settings that are easy to hear; sequencer and stutter off.
Params crushed()
{
    Params p = neutral();
    p.sampleRate = std::log(3000.0f / 48.0f) / std::log(1000.0f); // 3 kHz
    p.bits = 4.0f / 23.0f;                                       // 5 bit
    p.filter = 0.55f;
    return p;
}

double maxAbsDiff(const Signal& a, const Signal& b, double t0 = 0.0, double t1 = 1.0e9)
{
    const size_t i0 = (size_t) (t0 * kFs), i1 = std::min({ a.size(), b.size(), (size_t) (t1 * kFs) });
    double d = 0;
    for (size_t i = i0; i < i1; ++i)
        d = std::max(d, (double) std::abs(a[i] - b[i]));
    return d;
}

double rmsDiff(const Signal& a, const Signal& b, double t0, double t1)
{
    const size_t i0 = (size_t) (t0 * kFs), i1 = std::min({ a.size(), b.size(), (size_t) (t1 * kFs) });
    double acc = 0;
    for (size_t i = i0; i < i1; ++i)
        acc += (double) (a[i] - b[i]) * (double) (a[i] - b[i]);
    return std::sqrt(acc / (double) std::max<size_t>(1, i1 - i0));
}

void testMix()
{
    std::puts("mix: blends the clean input with the effect");
    const Signal in = noise(1.0, 0.4f, 11);
    const Params wetParams = crushed();
    const Signal wet = render(wetParams, in); // mix defaults to 100 % effect

    CHECK(maxAbsDiff(wet, in) > 0.05, "the crushed signal should differ from the input");

    Params p = wetParams;
    p.mix = 0.0f;
    CHECK(maxAbsDiff(render(p, in), in) < 1.0e-7, "mix 0 gives back the clean input");

    for (float m : { 0.25f, 0.5f, 0.8f })
    {
        p.mix = m;
        const Signal out = render(p, in);
        double worst = 0;
        for (size_t i = 0; i < in.size(); ++i)
            worst = std::max(worst, (double) std::abs(out[i] - (in[i] + (wet[i] - in[i]) * m)));
        CHECK(worst < 2.0e-6, "mix %.2f: max deviation from dry + (wet - dry) * mix is %g", m, worst);
    }

    // The output gain applies to the blend, so at mix 0 it simply scales the clean signal (+6.02 dB = x2).
    p.mix = 0.0f;
    p.outputGainDb = 6.0206f;
    const Signal loud = render(p, in);
    double gainErr = 0;
    for (size_t i = 0; i < in.size(); ++i)
        gainErr = std::max(gainErr, (double) std::abs(loud[i] - 2.0f * in[i]));
    CHECK(gainErr < 1.0e-4, "output gain applies after the mix (error %g)", gainErr);

    // Bypass always returns the untouched input, whatever the mix says.
    p = wetParams;
    p.mix = 0.3f;
    p.bypass = true;
    CHECK(maxAbsDiff(render(p, in), in) < 1.0e-6, "bypass is clean at any mix");

    // Automating the mix settles exactly on the clean input.
    p = wetParams;
    const Signal swept = render(p, in, [](Params& q, size_t pos) { q.mix = pos < 24000 ? 1.0f : 0.0f; });
    // The mix is smoothed over ~10 ms so it never clicks; it has settled well within 200 ms.
    CHECK(maxAbsDiff(swept, in, 0.7, 1.0) < 1.0e-6, "mix automated to 0 ends on the clean input");
    CHECK(maxAbsDiff(swept, in, 0.5, 0.52) > 0.01 && maxAbsDiff(swept, in, 0.5, 0.52) < maxAbsDiff(wet, in, 0.5, 0.52),
          "the change is smooth, not an instant jump");
    CHECK(maxAbsDiff(swept, wet, 0.0, 0.49) < 1.0e-6, "before the change the full effect is heard");

    // Out-of-range values are clamped rather than amplifying.
    p = wetParams;
    p.mix = 5.0f;
    CHECK(maxAbsDiff(render(p, in), wet) < 1.0e-7, "mix above 1 is treated as 1");
    p.mix = -3.0f;
    CHECK(maxAbsDiff(render(p, in), in) < 1.0e-7, "mix below 0 is treated as 0");
}

// A quiet 440 Hz tone (too quiet to trigger the pick detector) with one loud pick at 0.5 s. Comparing the
// output with the input on the quiet part shows whether the sample-rate stage is acting at that moment.
Signal quietToneWithPick()
{
    Signal s = sine(440.0, 2.2, 0.0015f);
    Rng rng(3);
    const size_t start = (size_t) (0.5 * kFs), len = (size_t) (0.1 * kFs);
    for (size_t i = 0; i < len; ++i)
        s[start + i] += 0.5f * rng.next() * (1.0f - (float) i / (float) len);
    return s;
}

Params rateOnly()
{
    Params p = neutral();
    p.sampleRate = std::log(3000.0f / 48.0f) / std::log(1000.0f); // 3 kHz hold: easy to see on a tone
    return p;
}

void testEffectsOnTriggers()
{
    std::puts("effects only on triggers: crush / sample rate / filter act only while a trigger effect plays");
    const Signal in = quietToneWithPick();
    constexpr double kAudible = 1.0e-4; // rms difference that proves the hold is acting on the quiet tone

    // Option off (default): the sample-rate stage is always on.
    Params always = rateOnly();
    CHECK(rmsDiff(render(always, in), in, 0.1, 0.45) > kAudible, "option off: the effect is on with nothing playing");

    // Option on, sequencer and stutter both off: nothing ever triggers, so the signal stays clean.
    Params idle = rateOnly();
    idle.fxOnlyOnTriggers = true;
    CHECK(maxAbsDiff(render(idle, in), in) < 1.0e-6, "option on, sequencer and stutter off: fully clean");

    // Sequencer set to play once, six dry steps of 100 ms (so only the effect stage can change the sound).
    Params seq = rateOnly();
    seq.fxOnlyOnTriggers = true;
    seq.seqPlay = 1;
    seq.seqMult = 0;
    seq.tempoBpm = 600.0f;
    seq.steps.fill(mapping::pitchStepKnob(0));
    const Signal seqOut = render(seq, in);
    CHECK(maxAbsDiff(seqOut, in, 0.05, 0.45) < 1.0e-6, "sequencer: clean before the pick (max diff %g)", maxAbsDiff(seqOut, in, 0.05, 0.45));
    CHECK(rmsDiff(seqOut, in, 0.7, 1.0) > kAudible, "sequencer: effect on while the sequence plays (rms diff %g)", rmsDiff(seqOut, in, 0.7, 1.0));
    CHECK(maxAbsDiff(seqOut, in, 1.4, 2.1) < 1.0e-6, "sequencer: clean again after it finishes (max diff %g)", maxAbsDiff(seqOut, in, 1.4, 2.1));

    // A sequencer on "continuous" never stops, so the effect stays on.
    Params loop = seq;
    loop.seqPlay = 5;
    CHECK(rmsDiff(render(loop, in), in, 0.1, 0.45) > kAudible, "continuous sequencer keeps the effect on");

    // Stutter: full speed, two repeats of a 1/8 note at 120 BPM (recording 0.25 s, then two repeats).
    Params stut = rateOnly();
    stut.fxOnlyOnTriggers = true;
    stut.stutter = 2;
    const Signal stutOut = render(stut, in);
    CHECK(maxAbsDiff(stutOut, in, 0.05, 0.45) < 1.0e-6, "stutter: clean before the pick (max diff %g)", maxAbsDiff(stutOut, in, 0.05, 0.45));
    CHECK(rmsDiff(stutOut, in, 0.65, 0.74) > kAudible, "stutter: effect on while it records (rms diff %g)", rmsDiff(stutOut, in, 0.65, 0.74));
    CHECK(maxAbsDiff(stutOut, in, 1.5, 2.1) < 1.0e-6, "stutter: clean again after the repeats (max diff %g)", maxAbsDiff(stutOut, in, 1.5, 2.1));

    // Switching the option off again brings the always-on behaviour back.
    const Signal toggled = render(idle, in, [](Params& q, size_t pos) { q.fxOnlyOnTriggers = pos < 24000; });
    CHECK(maxAbsDiff(toggled, in, 0.05, 0.45) < 1.0e-6, "option on: clean");
    CHECK(rmsDiff(toggled, in, 0.7, 1.0) > kAudible, "option switched off: the effect comes back");
}

// ---------------------------------------------------------------------------------------------
// Reverb on the sequenced sound

void testReverb()
{
    std::puts("reverb: room, hall and spring, added to the sequenced sound only, with tone, mix, level and cuts");

    // Each character on its own: a unit click rings out and dies away, noise passes at about unity.
    for (int type = 0; type < kNumReverbTypes; ++type)
    {
        ReverbSettings rs;
        rs.type = (ReverbType) type;
        rs.tone = 1.0f;
        {
            Reverb r;
            r.prepare(kFs, 0);
            for (int i = 0; i < 600; ++i) // let a change of type finish (it fades out and back in over ~16 ms)
                r.process(0.0f, rs);
            double early = 0.0, late = 0.0;
            bool finite = true;
            for (int i = 0; i < (int) (8.0 * kFs); ++i)
            {
                const float y = r.process(i == 0 ? 1.0f : 0.0f, rs);
                finite = finite && std::isfinite(y);
                if (i < (int) (0.5 * kFs))
                    early += (double) y * y;
                else if (i >= (int) (7.0 * kFs))
                    late += (double) y * y;
            }
            CHECK(finite && early > 1.0e-6, "%s: an impulse produces a tail (energy %g)", reverbTypeName(rs.type), early);
            CHECK(late < early * 1.0e-3, "%s: the tail decays (late %g vs early %g)", reverbTypeName(rs.type), late, early);
        }
        {
            Reverb r;
            r.prepare(kFs, 0);
            Rng rng(5);
            double in2 = 0.0, out2 = 0.0;
            for (int i = 0; i < (int) (4.0 * kFs); ++i)
            {
                const float x = 0.3f * rng.next();
                const float y = r.process(x, rs);
                if (i > (int) (2.0 * kFs))
                {
                    in2 += (double) x * x;
                    out2 += (double) y * y;
                }
            }
            const double gainDb = db(std::sqrt(out2 / in2));
            CHECK(gainDb > -6.0 && gainDb < 6.0, "%s: steady noise passes at about unity (%.1f dB)", reverbTypeName(rs.type), gainDb);
        }
    }

    // The three characters sound different, and Hall rings longer than Room.
    {
        auto tailEnergy = [](ReverbType type, double t0, double t1)
        {
            ReverbSettings rs;
            rs.type = type;
            rs.tone = 1.0f;
            Reverb r;
            r.prepare(kFs, 0);
            for (int i = 0; i < 600; ++i)
                r.process(0.0f, rs);
            double e = 0.0;
            for (int i = 0; i < (int) (t1 * kFs); ++i)
            {
                const float y = r.process(i == 0 ? 1.0f : 0.0f, rs);
                if (i >= (int) (t0 * kFs))
                    e += (double) y * y;
            }
            return e;
        };
        const double roomLate = tailEnergy(ReverbType::Room, 1.0, 3.0), hallLate = tailEnergy(ReverbType::Hall, 1.0, 3.0);
        CHECK(hallLate > roomLate * 3.0, "hall rings longer than room (%g vs %g)", hallLate, roomLate);
        CHECK(tailEnergy(ReverbType::Spring, 0.0, 1.0) != tailEnergy(ReverbType::Room, 0.0, 1.0), "spring is its own sound");
    }

    // Low cut and high cut shape the reverb: a 1 kHz sine passes with the filters open, but a high low-cut
    // removes the lows and a low high-cut removes the highs.
    {
        auto level = [](double hz, float lowCut, float highCut)
        {
            ReverbSettings rs;
            rs.type = ReverbType::Room;
            rs.tone = 1.0f;
            rs.lowCutHz = lowCut;
            rs.highCutHz = highCut;
            Reverb r;
            r.prepare(kFs, 0);
            double e = 0.0;
            for (int i = 0; i < (int) (2.0 * kFs); ++i)
            {
                const float y = r.process(0.3f * (float) std::sin(2.0 * 3.14159265358979 * hz * (double) i / kFs), rs);
                if (i > (int) kFs)
                    e += (double) y * y;
            }
            return std::sqrt(e / kFs);
        };
        const double open = level(100.0, 20.0f, 20000.0f);
        CHECK(level(100.0, 1000.0f, 20000.0f) < open * 0.2, "low cut at 1 kHz removes a 100 Hz tone");
        const double openHigh = level(8000.0, 20.0f, 20000.0f);
        CHECK(level(8000.0, 20.0f, 1000.0f) < openHigh * 0.1, "high cut at 1 kHz removes an 8 kHz tone");
        CHECK(level(1000.0, 100.0f, 10000.0f) > level(1000.0, 20.0f, 20000.0f) * 0.7, "a tone between the cuts passes");
    }

    // Changing the type does not click: the output stays smooth through the switch.
    {
        Reverb r;
        r.prepare(kFs, 0);
        ReverbSettings rs;
        rs.tone = 1.0f;
        float last = 0.0f, worstStep = 0.0f;
        for (int i = 0; i < (int) kFs; ++i)
        {
            rs.type = i < 24000 ? ReverbType::Room : ReverbType::Spring;
            const float y = r.process(0.3f * (float) std::sin(2.0 * 3.14159265358979 * 200.0 * (double) i / kFs), rs); // a smooth input
            if (i > 1000)
                worstStep = std::max(worstStep, std::abs(y - last));
            last = y;
        }
        CHECK(worstStep < 0.1f && std::isfinite(last), "switching type stays smooth (largest step %.3f)", worstStep);
    }

    // DECAY stretches the tail (0 = a quarter of the normal length, 1 = four times) for every type.
    auto impulseResponse = [](ReverbType type, bool reverse, float decay, double seconds)
    {
        ReverbSettings rs;
        rs.type = type;
        rs.reverse = reverse;
        rs.decay = decay;
        rs.tone = 1.0f;
        Reverb r;
        r.prepare(kFs, 0);
        for (int i = 0; i < 600; ++i) // let the type and direction settle (they fade over ~16 ms)
            r.process(0.0f, rs);
        Signal out((size_t) (seconds * kFs));
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = r.process(i == 0 ? 1.0f : 0.0f, rs);
        return out;
    };
    auto windowEnergy = [](const Signal& x, double t0, double t1)
    {
        double e = 0.0;
        for (size_t i = (size_t) (t0 * kFs); i < std::min(x.size(), (size_t) (t1 * kFs)); ++i)
            e += (double) x[i] * x[i];
        return e;
    };
    for (int type = 0; type < kNumReverbTypes; ++type)
    {
        const auto name = reverbTypeName((ReverbType) type);
        // (for the gated reverb DECAY sets how long the gate holds, so look earlier in its tail)
        const double t0 = (ReverbType) type == ReverbType::Gated ? 0.3 : 1.0;
        const double short1 = windowEnergy(impulseResponse((ReverbType) type, false, 0.0f, 3.0), t0, 3.0);
        const double normal = windowEnergy(impulseResponse((ReverbType) type, false, 0.5f, 3.0), t0, 3.0);
        const double longest = windowEnergy(impulseResponse((ReverbType) type, false, 1.0f, 3.0), t0, 3.0);
        CHECK(short1 < normal && normal < longest && longest > 50.0 * std::max(short1, 1.0e-20), "%s: more decay, longer tail (%g, %g, %g)", name, short1, normal, longest);
    }

    // Plate: a long, smooth, dense tail (it rings on well past a second and is still decaying).
    {
        const auto ir = impulseResponse(ReverbType::Plate, false, 0.5f, 4.0);
        const double a = windowEnergy(ir, 0.1, 0.6), b = windowEnergy(ir, 1.0, 1.5), c = windowEnergy(ir, 2.5, 3.0);
        CHECK(a > b && b > c && b > 1.0e-7, "plate: a smooth tail that is still audible after a second and keeps decaying (%g, %g, %g)", a, b, c);
    }

    // Shimmer: the tail climbs an octave. A 500 Hz burst leaves much more 1 kHz in the Shimmer tail than in the Hall's.
    {
        auto octaveShare = [](ReverbType type)
        {
            ReverbSettings rs;
            rs.type = type;
            rs.tone = 1.0f;
            Reverb r;
            r.prepare(kFs, 0);
            for (int i = 0; i < 600; ++i)
                r.process(0.0f, rs);
            Signal out((size_t) (2.0 * kFs));
            for (size_t i = 0; i < out.size(); ++i)
                out[i] = r.process(i < (size_t) (0.3 * kFs) ? 0.3f * (float) std::sin(2.0 * 3.14159265358979 * 500.0 * (double) i / kFs) : 0.0f, rs);
            return tonePower(out, 0.6, 1.8, 1000.0) / std::max(1.0e-18, tonePower(out, 0.6, 1.8, 500.0));
        };
        const double shimmer = octaveShare(ReverbType::Shimmer), hall = octaveShare(ReverbType::Hall);
        CHECK(shimmer > 10.0 * hall, "shimmer: the tail climbs an octave (1k/500 Hz %.3f against %.4f for the hall)", shimmer, hall);
    }

    // Gated: the tail is chopped off after the hold (about 0.25 s at the normal DECAY, plus the time the gate takes to notice the sound has gone), and what is left is silence.
    {
        const auto ir = impulseResponse(ReverbType::Gated, false, 0.5f, 1.5);
        const double inside = windowEnergy(ir, 0.05, 0.2), after = windowEnergy(ir, 0.55, 1.4);
        CHECK(inside > 1.0e-6 && after < 1.0e-3 * inside, "gated: loud inside the hold, silent after it (%g then %g)", inside, after);
    }

    // REVERSE: the tail swells up and stops. Echoes are quiet at the start and loud at the end of the swell, and
    // nothing is left after it. Room (0.7 s), Hall (2.2 s) and Spring (1.2 s) at the normal decay.
    for (auto [type, length] : { std::pair<ReverbType, double> { ReverbType::Room, 0.7 }, { ReverbType::Hall, 2.2 }, { ReverbType::Spring, 1.2 },
                                 { ReverbType::Plate, 1.6 }, { ReverbType::Shimmer, 2.6 }, { ReverbType::Gated, 0.45 } })
    {
        const auto ir = impulseResponse(type, true, 0.5f, length + 1.5);
        const double early = windowEnergy(ir, 0.03 * length, 0.3 * length), late = windowEnergy(ir, 0.6 * length, length - 0.02);
        const double after = windowEnergy(ir, length + 0.15, length + 1.4);
        CHECK(late > 8.0 * early && late > 1.0e-5, "reverse %s: the swell builds up (early %g, late %g)", reverbTypeName(type), early, late);
        CHECK(after < 1.0e-3 * late, "reverse %s: and stops at the end of its length (%g left)", reverbTypeName(type), after);
        const auto longer = impulseResponse(type, true, 1.0f, length * 4.0 + 1.0);
        CHECK(windowEnergy(longer, length * 2.5, length * 3.9) > 1.0e-6 && windowEnergy(longer, length * 4.3, length * 4.9) < 1.0e-3 * windowEnergy(longer, length * 2.5, length * 3.9),
              "reverse %s: DECAY makes the swell longer (four times at the top)", reverbTypeName(type));
    }
    {
        ReverbSettings rs;
        rs.reverse = true;
        rs.tone = 1.0f;
        Reverb r;
        r.prepare(kFs, 0);
        Rng rng(11);
        double in2 = 0.0, out2 = 0.0;
        bool finite = true;
        for (int i = 0; i < (int) (4.0 * kFs); ++i)
        {
            const float x = 0.3f * rng.next();
            const float y = r.process(x, rs);
            finite = finite && std::isfinite(y);
            if (i > (int) (2.0 * kFs))
            {
                in2 += (double) x * x;
                out2 += (double) y * y;
            }
        }
        const double gainDb = db(std::sqrt(out2 / in2));
        CHECK(finite && gainDb > -6.0 && gainDb < 6.0, "reverse: steady noise passes at about unity (%.1f dB)", gainDb);
    }
    {
        // turning REVERSE on and off while sound plays does not click
        Reverb r;
        r.prepare(kFs, 0);
        ReverbSettings rs;
        rs.tone = 1.0f;
        float last = 0.0f, worstStep = 0.0f;
        for (int i = 0; i < (int) kFs; ++i)
        {
            rs.reverse = (i / 12000) % 2 == 1;
            const float y = r.process(0.3f * (float) std::sin(2.0 * 3.14159265358979 * 200.0 * (double) i / kFs), rs);
            if (i > 1000)
                worstStep = std::max(worstStep, std::abs(y - last));
            last = y;
        }
        CHECK(worstStep < 0.1f && std::isfinite(last), "switching REVERSE stays smooth (largest step %.3f)", worstStep);
    }

    // Stress: changing the type, direction, decay, tone and cuts at random while loud noise plays never blows up.
    {
        Reverb r;
        r.prepare(kFs, 1);
        Rng rng(99);
        ReverbSettings rs;
        bool finite = true;
        float peak = 0.0f;
        for (int i = 0; i < (int) (8.0 * kFs); ++i)
        {
            if (i % 4000 == 0)
            {
                rs.type = (ReverbType) (std::abs((int) (rng.next() * 100.0f)) % kNumReverbTypes);
                rs.reverse = rng.next() > 0.3f;
                rs.decay = 0.5f + 0.5f * rng.next();
                rs.tone = 0.5f + 0.5f * rng.next();
                rs.lowCutHz = rng.next() > 0.5f ? 20.0f : 400.0f;
            }
            const float y = r.process(0.8f * rng.next(), rs);
            finite = finite && std::isfinite(y);
            peak = std::max(peak, std::abs(y));
        }
        CHECK(finite && peak < 20.0f, "random type, direction, decay and tone changes stay finite and bounded (peak %.2f)", peak);
    }

    // A sequence plays once from a pick; steps are dry, so only the reverb can change the sound.
    const Signal in = quietToneWithPick();
    Params seq = neutral();
    seq.seqPlay = 1;
    seq.seqMult = 0;
    seq.tempoBpm = 600.0f;
    seq.steps.fill(mapping::pitchStepKnob(0));
    const Signal plain = render(seq, in);

    Params wet = seq;
    wet.reverbMix = 1.0f;
    CHECK(maxAbsDiff(render(seq, in), plain) == 0.0 && maxAbsDiff(render(wet, in), plain, 0.0, 0.09) < 1.0e-6,
          "reverb mix 0 changes nothing, and nothing is heard before the pick");
    const Signal withReverb = render(wet, in);
    CHECK(rmsDiff(withReverb, plain, 0.7, 1.0) > 1.0e-4, "reverb mix 1: the sequenced sound gets a reverb (%g)", rmsDiff(withReverb, plain, 0.7, 1.0));
    CHECK(rmsDiff(withReverb, plain, 1.4, 2.1) > 1.0e-5, "the tail rings on after the sequence has finished (%g)", rmsDiff(withReverb, plain, 1.4, 2.1));

    {
        Params rev = wet;
        rev.reverbReverse = true;
        const Signal reversed = render(rev, in);
        CHECK(rmsDiff(reversed, withReverb, 0.7, 1.6) > 1.0e-4 && rmsDiff(reversed, plain, 0.7, 1.6) > 1.0e-4,
              "reverse reverb differs from the normal one and is audible (%g)", rmsDiff(reversed, plain, 0.7, 1.6));
        rev.reverbMix = 0.0f;
        CHECK(maxAbsDiff(render(rev, in), plain) == 0.0, "reverse with the reverb mix at 0 adds nothing");
    }

    {
        Params off = wet;
        off.reverbOn = false;
        CHECK(maxAbsDiff(render(off, in), plain) == 0.0, "reverb switched off: nothing is added, whatever the mix says");
        Params later = wet;
        const Signal switched = render(later, in, [](Params& q, size_t pos) { q.reverbOn = pos < 50000; });
        CHECK(rmsDiff(switched, plain, 0.7, 1.0) > 1.0e-4 && maxAbsDiff(switched, plain, 1.4, 2.1) < 1.0e-5,
              "switching the reverb off mid-way stops the tail (%g)", maxAbsDiff(switched, plain, 1.4, 2.1));
    }

    // Nothing is sequenced: the reverb hears nothing, so the signal is untouched (the clean signal is never reverbed).
    Params idle = neutral();
    idle.reverbMix = 1.0f;
    CHECK(maxAbsDiff(render(idle, in), in) < 1.0e-6, "with no sequence or stutter running the signal is untouched");

    // Mix 0 on the blend (clean input only) leaves the reverb out of the output: the reverb is part of the effect side.
    Params dryOnly = wet;
    dryOnly.mix = 0.0f;
    CHECK(maxAbsDiff(render(dryOnly, in), in) < 1.0e-6, "dry/wet mix 0 returns the clean input, with no reverb in it");

    // Level: -6.02 dB halves the reverb, +0 dB is the full one (the reverb part is the difference to the plain sound).
    Params half = wet;
    half.reverbLevelDb = -6.0206f;
    double worst = 0.0;
    const Signal halfOut = render(half, in);
    for (size_t i = 0; i < plain.size(); ++i)
        worst = std::max(worst, (double) std::abs((halfOut[i] - plain[i]) - 0.5f * (withReverb[i] - plain[i])));
    CHECK(worst < 1.0e-4, "level -6 dB halves the reverb (deviation %g)", worst);

    // Mix is the amount added: 0.5 gives half the reverb.
    Params mid = wet;
    mid.reverbMix = 0.5f;
    const Signal midOut = render(mid, in);
    worst = 0.0;
    for (size_t i = 0; i < plain.size(); ++i)
        worst = std::max(worst, (double) std::abs((midOut[i] - plain[i]) - 0.5f * (withReverb[i] - plain[i])));
    CHECK(worst < 1.0e-4, "mix 0.5 adds half the reverb (deviation %g)", worst);

    // Tone: the dark setting leaves less high-frequency energy in the reverb than the bright one.
    // A looping dry sequence keeps the noise "sequenced" the whole time.
    {
        Params loop = seq;
        loop.seqPlay = 5;
        const Signal src = noise(2.0, 0.3f, 7);
        const Signal base = render(loop, src);
        auto highShare = [&](float tone)
        {
            Params q = loop;
            q.reverbMix = 1.0f;
            q.reverbTone = tone;
            const Signal o = render(q, src);
            double hf = 0.0, all = 0.0;
            for (size_t i = (size_t) (0.5 * kFs) + 1; i < o.size(); ++i)
            {
                const double v = (double) o[i] - base[i], v0 = (double) o[i - 1] - base[i - 1];
                hf += (v - v0) * (v - v0);
                all += v * v;
            }
            return hf / std::max(all, 1.0e-12);
        };
        const double dark = highShare(0.0f), bright = highShare(1.0f);
        CHECK(dark < 0.25 * bright, "tone: dark has far less treble than bright (%g vs %g)", dark, bright);
    }

    // Bypass is still clean, and the engine copes with a reset mid-tail.
    Params byp = wet;
    byp.bypass = true;
    CHECK(maxAbsDiff(render(byp, in), in) < 1.0e-6, "bypass is clean with the reverb on");
}

// ---------------------------------------------------------------------------------------------
// Sample blend: a random picked sound joins the sequence at every step

void testSampleBlend()
{
    std::puts("sample blend: a random sound from the picked list at each sequencer step");

    // The volume: MIX moves between a base and a max volume, in dB; the bottom of a volume knob is silence.
    CHECK(Engine::sampleGain(0.0f, -60.0f, -6.0f) == 0.0f && std::abs(db(Engine::sampleGain(1.0f, -60.0f, -6.0f)) + 6.0) < 0.01 &&
              std::abs(db(Engine::sampleGain(0.5f, -20.0f, -6.0f)) + 13.0) < 0.01 && std::abs(db(Engine::sampleGain(0.0f, -20.0f, -6.0f)) + 20.0) < 0.01 &&
              Engine::sampleGain(0.5f, -60.0f, -60.0f) == 0.0f,
          "the sample volume: base at mix 0, max at mix 1, even steps in dB in between, silence at the bottom");
    const float full = 1.0f; // the tests below use base Off and max 0 dB, so mix 1 plays the clips at their own level

    // Two clips of constant level, 2000 samples long (at the engine's rate): 0.2 and 0.4 after the knob.
    SampleSet set;
    for (float level : { 0.2f, 0.4f })
    {
        SampleClip c;
        c.data.assign(2000, level / full);
        c.rate = kFs;
        set.clips.push_back(std::move(c));
    }

    // Six dry steps of 100 ms, played once after the pick at 0.1 s (see testEffectsOnTriggers).
    const Signal in(24000 * 4, 0.0f);
    auto sequence = [&]
    {
        Params p = neutral();
        p.seqPlay = 1;
        p.seqMult = 0;
        p.tempoBpm = 600.0f;
        p.steps.fill(mapping::pitchStepKnob(0));
        p.pickTriggers = false;
        p.sampleBaseDb = -60.0f; // Off at mix 0, 0 dB (the clip's own level) at mix 1
        p.sampleMaxDb = 0.0f;
        return p;
    };
    // Runs the engine in 256-sample blocks with one restart event at sample 1000.
    auto renderWithTrigger = [&](const Params& params)
    {
        constexpr int block = 256, eventAt = 1000;
        Engine e;
        e.prepare(kFs);
        Signal out = in;
        for (size_t pos = 0; pos < out.size(); pos += (size_t) block)
        {
            float* ch = out.data() + pos;
            const int offset = eventAt - (int) pos;
            if (offset >= 0 && offset < block)
            {
                const TriggerEvent ev { offset, TriggerAction::RestartSequence };
                e.process(&ch, 1, block, params, &ev, 1);
            }
            else
            {
                e.process(&ch, 1, block, params);
            }
        }
        return out;
    };

    Params p = sequence();
    p.sampleSet = &set;
    p.sampleBaseDb = -60.0f; // Off
    p.sampleMaxDb = 0.0f;
    CHECK(maxAbsDiff(renderWithTrigger(p), in) == 0.0, "sample mix 0 with the base Off: nothing is added");

    p.sampleMix = 1.0f;
    const Signal out = renderWithTrigger(p);
    // Each of the six steps (every 4800 samples from 1000) starts a clip: 0.2 or 0.4 for the first ~2000 samples, silence after.
    int clipsHeard = 0;
    bool silentBetween = true, onlyKnownLevels = true;
    for (int step = 0; step < 6; ++step)
    {
        const size_t at = 1000 + (size_t) step * 4800;
        const float level = out[at + 500];
        onlyKnownLevels = onlyKnownLevels && (std::abs(level - 0.2f) < 1.0e-3f || std::abs(level - 0.4f) < 1.0e-3f);
        clipsHeard += level > 0.1f ? 1 : 0;
        silentBetween = silentBetween && std::abs(out[at + 3500]) < 1.0e-6f;
    }
    CHECK(clipsHeard == 6 && onlyKnownLevels, "every step brings in one of the two sounds (heard %d of 6)", clipsHeard);
    CHECK(silentBetween && std::abs(out[500]) < 1.0e-6f && std::abs(out[1000 + 6 * 4800 + 500]) < 1.0e-6f,
          "and only at the steps: silence before the sequence and after it");

    // Over many steps both sounds turn up.
    {
        Params loop = sequence();
        loop.seqPlay = 5; // loops
        loop.sampleSet = &set;
        loop.sampleMix = 1.0f;
        const Signal r = render(loop, Signal(48000 * 4, 0.0f));
        bool low = false, high = false;
        for (size_t i = 0; i < r.size(); i += 100)
        {
            low = low || std::abs(r[i] - 0.2f) < 1.0e-3f;
            high = high || std::abs(r[i] - 0.4f) < 1.0e-3f;
        }
        CHECK(low && high, "over a looping sequence both sounds are picked");
    }

    // Mix follows the curve, chance 0 plays nothing, a muted step stays silent.
    p.sampleMix = 0.5f;
    {
        const Signal half = renderWithTrigger(p);
        const float a = half[1000 + 500];
        CHECK(std::abs(a - 0.2f * 0.0316228f) < 1.0e-4f || std::abs(a - 0.4f * 0.0316228f) < 1.0e-4f, "mix 0.5 with the base Off is 30 dB down (%.4f)", a);
    }
    p.sampleMix = 1.0f;
    p.sampleChance = 0.0f;
    CHECK(maxAbsDiff(renderWithTrigger(p), in) == 0.0, "chance 0: no sound is played");
    p.sampleChance = 1.0f;
    p.steps.fill(1.0f); // fully right = mute
    CHECK(maxAbsDiff(renderWithTrigger(p), in) < 1.0e-6, "muted steps bring in no sound");

    {
        Params off = sequence();
        off.sampleSet = &set;
        off.sampleMix = 1.0f;
        off.sampleOn = false;
        CHECK(maxAbsDiff(renderWithTrigger(off), in) == 0.0, "sample blend switched off: no sound is added");
        Params on = off;
        on.sampleOn = true;
        CHECK(maxAbsDiff(renderWithTrigger(on), in) > 0.05, "and switched on it plays again");
    }

    // No set, or an empty one: nothing happens.
    p = sequence();
    p.sampleMix = 1.0f;
    CHECK(maxAbsDiff(renderWithTrigger(p), in) == 0.0, "no sounds picked: nothing is added");
    SampleSet empty;
    p.sampleSet = &empty;
    CHECK(maxAbsDiff(renderWithTrigger(p), in) == 0.0, "an empty list adds nothing");

    // The sounds belong to the sequence: a long clip is cut when the sequence ends (it would last 20000 samples).
    {
        SampleSet longSet;
        SampleClip c;
        c.data.assign(40000, 0.3f / full);
        c.rate = kFs;
        longSet.clips.push_back(std::move(c));
        Params q = sequence();
        q.sampleMix = 1.0f;
        q.sampleSet = &longSet;
        const Signal r = renderWithTrigger(q);
        const size_t end = 1000 + 6 * 4800;
        CHECK(std::abs(r[end - 1000]) > 0.25f && std::abs(r[end + 600]) < 1.0e-6f && std::abs(r[end + 5000]) < 1.0e-6f,
              "the sound stops when the sequence stops (%.3f before, %.3f after)", r[end - 1000], r[end + 600]);
    }

    // They are crushed with the rest of the sequence: at a low bit depth the sound is no longer its clean level.
    {
        Params q = sequence();
        q.sampleMix = 1.0f;
        q.sampleSet = &set;
        q.bits = 0.05f;
        const Signal r = renderWithTrigger(q);
        const float v = r[1000 + 500];
        CHECK(std::abs(v) > 0.05f && std::abs(v - 0.2f) > 0.01f && std::abs(v - 0.4f) > 0.01f, "the sound goes through the bit crusher (%.3f)", v);
        q.bits = 1.0f;
        const float clean = renderWithTrigger(q)[1000 + 500];
        CHECK(std::abs(clean - 0.2f) < 1.0e-3f || std::abs(clean - 0.4f) < 1.0e-3f, "with the crusher off it is clean again (%.3f)", clean);
    }

    // They are pitch-shifted by the sequence: a step +12 semitones plays the sound an octave up (twice as fast).
    {
        SampleSet ramp;
        SampleClip c;
        for (int i = 0; i < 4000; ++i)
            c.data.push_back(0.4f / full * std::sin(2.0f * 3.14159265f * 400.0f * (float) i / (float) kFs));
        c.rate = kFs;
        ramp.clips.push_back(std::move(c));
        Params q = sequence();
        q.sampleMix = 1.0f;
        q.sampleSet = &ramp;
        const Signal flat = renderWithTrigger(q);
        q.steps.fill(mapping::pitchStepKnob(12));
        const Signal up = renderWithTrigger(q);
        CHECK(tonePower(flat, 0.03, 0.09, 400.0) > 4.0 * tonePower(flat, 0.03, 0.09, 800.0) &&
                  tonePower(up, 0.03, 0.09, 800.0) > 4.0 * tonePower(up, 0.03, 0.09, 400.0),
              "the sequence's +12 step moves the sound from 400 Hz up to 800 Hz");
    }

    // A preview: one picked sound at a volume of its own, with no sequence running and whatever the MIX knob says.
    {
        Params q = neutral();
        q.sampleSet = &set;
        q.sampleMix = 0.0f;
        Engine e;
        e.prepare(kFs);
        Signal heard(4000, 0.0f);
        float* ch = heard.data();
        e.previewSample(&set, 1, 0.5f); // pick 1 of 2 clips: the 0.4 one, at half volume
        e.process(&ch, 1, 4000, q);
        CHECK(std::abs(heard[500] - 0.2f) < 1.0e-3f && std::abs(heard[3500]) < 1.0e-6f, "a preview plays the picked sound at its own volume (%.3f), then stops", heard[500]);
        Signal silent(500, 0.0f);
        float* sc = silent.data();
        e.previewSample(&set, 0, 0.0f);
        e.process(&sc, 1, 500, q);
        CHECK(std::abs(silent[400]) < 1.0e-9f, "a preview at silence plays nothing");
    }

    // A clip at half the engine's rate lasts twice as long.
    SampleSet slow;
    {
        SampleClip c;
        c.data.assign(1000, 0.3f / full);
        c.rate = kFs / 2.0;
        slow.clips.push_back(std::move(c));
    }
    p.sampleSet = &slow;
    const Signal s = renderWithTrigger(p);
    CHECK(std::abs(s[1000 + 1500] - 0.3f) < 1.0e-3f && std::abs(s[1000 + 2500]) < 1.0e-6f, "a lower-rate clip plays at the right speed");
}

// ---------------------------------------------------------------------------------------------
// Distortion (the green-pedal overdrive) on the sequenced sound

// Runs a sine through the distortion and returns the output.
Signal distort(double hz, float amp, float drive, float tone, float level, double seconds = 0.4)
{
    Distortion d;
    d.prepare(kFs);
    d.setParams(drive, tone, level);
    Signal in = sine(hz, seconds, amp), out(in.size());
    for (size_t i = 0; i < in.size(); ++i)
        out[i] = d.process(in[i]);
    return out;
}

void testDistortion()
{
    std::puts("distortion: drive, tone and level (the 808-style mid-hump overdrive), only on the sequenced sound");

    // Silence in, silence out; a loud input stays bounded and finite.
    {
        const Signal quiet = distort(440.0, 0.0f, 1.0f, 1.0f, 1.0f);
        const Signal loud = distort(440.0, 1.0f, 1.0f, 1.0f, 1.0f);
        double peak = 0.0;
        bool finite = true;
        for (float v : loud)
        {
            peak = std::max(peak, (double) std::abs(v));
            finite = finite && std::isfinite(v);
        }
        CHECK(rms(quiet, 0.0, 0.4) < 1.0e-9 && finite && peak < 1.5, "silence stays silent, a full-scale sine stays bounded (peak %.2f)", peak);
    }

    // More drive: more harmonics (the third harmonic of a 1 kHz sine against its fundamental).
    {
        auto thirdOverFundamental = [](float drive)
        {
            const Signal o = distort(1000.0, 0.2f, drive, 1.0f, 0.8f, 0.5);
            return tonePower(o, 0.1, 0.5, 3000.0) / std::max(1.0e-18, tonePower(o, 0.1, 0.5, 1000.0));
        };
        const double low = thirdOverFundamental(0.0f), high = thirdOverFundamental(1.0f);
        CHECK(high > 4.0 * low && high > 0.01, "drive adds harmonics (3rd/1st %.4f at 0, %.4f at 1)", low, high);
    }

    // The mid hump: the lows are not pushed into the clipper as hard as the mids, so a 100 Hz sine is distorted
    // less than a 1 kHz one of the same size.
    {
        auto thirdOverFundamental = [](double hz)
        {
            const Signal o = distort(hz, 0.2f, 0.7f, 1.0f, 0.8f, 0.6);
            return tonePower(o, 0.2, 0.6, 3.0 * hz) / std::max(1.0e-18, tonePower(o, 0.2, 0.6, hz));
        };
        const double lows = thirdOverFundamental(100.0), mids = thirdOverFundamental(1000.0);
        CHECK(mids > 2.5 * lows, "mid hump: a 1 kHz tone is distorted much more than a 100 Hz one (%.4f vs %.4f)", mids, lows);
    }

    // Tone: a bright setting keeps more of the highs than a dark one (a 5 kHz sine through it).
    {
        const double dark = rms(distort(5000.0, 0.05f, 0.2f, 0.0f, 0.8f), 0.2, 0.4), bright = rms(distort(5000.0, 0.05f, 0.2f, 1.0f, 0.8f), 0.2, 0.4);
        CHECK(bright > 4.0 * dark, "tone: bright keeps the highs, dark takes them away (%.4f vs %.4f)", bright, dark);
    }

    // Level: -30 dB .. +6 dB, exactly (small input, so the circuit is linear).
    {
        const double lo = rms(distort(300.0, 0.002f, 0.0f, 0.5f, 0.0f), 0.2, 0.4), hi = rms(distort(300.0, 0.002f, 0.0f, 0.5f, 1.0f), 0.2, 0.4);
        CHECK(std::abs(db(hi / lo) - 36.0) < 0.5, "level spans 36 dB (%.2f)", db(hi / lo));
    }

    // Through the engine: sequenced sound only. Six dry steps played once; the distortion changes the sound while
    // they play and leaves the clean signal alone before and after.
    {
        const Signal in = quietToneWithPick();
        Params seq = neutral();
        seq.seqPlay = 1;
        seq.seqMult = 0;
        seq.tempoBpm = 600.0f;
        seq.steps.fill(mapping::pitchStepKnob(0));
        const Signal plain = render(seq, in);
        Params dist = seq;
        dist.distOn = true;
        dist.distDrive = 1.0f;
        const Signal with = render(dist, in);
        CHECK(maxAbsDiff(with, plain, 0.0, 0.09) < 1.0e-6, "before the sequence starts the signal is untouched");
        CHECK(rmsDiff(with, plain, 0.7, 1.0) > 1.0e-3, "while the sequence plays it is distorted (%g)", rmsDiff(with, plain, 0.7, 1.0));
        CHECK(maxAbsDiff(with, plain, 1.4, 2.1) < 1.0e-6, "after the sequence the signal is clean again (%g)", maxAbsDiff(with, plain, 1.4, 2.1));
        Params off = seq;
        off.distDrive = 1.0f;
        CHECK(maxAbsDiff(render(off, in), plain) == 0.0, "switched off: nothing changes");
        Params byp = dist;
        byp.bypass = true;
        CHECK(maxAbsDiff(render(byp, in), in) < 1.0e-6, "bypass is clean");
        Params mixDry = dist;
        mixDry.mix = 0.0f;
        CHECK(maxAbsDiff(render(mixDry, in), in) < 1.0e-6, "dry/wet mix 0 brings back the clean input");

        // The reverb: left alone by default, distorted when the player asks for it.
        Params rev = dist;
        rev.reverbMix = 1.0f;
        const Signal cleanReverb = render(rev, in);
        rev.distReverb = true;
        const Signal dirtyReverb = render(rev, in);
        CHECK(rmsDiff(dirtyReverb, cleanReverb, 0.7, 1.0) > 1.0e-4, "the reverb is distorted too when asked (%g)", rmsDiff(dirtyReverb, cleanReverb, 0.7, 1.0));
        Params plainReverb = seq;
        plainReverb.reverbMix = 1.0f;
        const Signal reverbOnly = render(plainReverb, in);
        CHECK(rmsDiff(cleanReverb, reverbOnly, 1.5, 2.1) < 1.0e-6, "with the reverb box off the reverb tail is not distorted (%g)", rmsDiff(cleanReverb, reverbOnly, 1.5, 2.1));
    }
}

void testTremolo()
{
    std::puts("tremolo: rate, depth, edge and the rate curve, only on the sequenced sound");
    TremoloSettings s;
    s.rateHz = 6.0f;
    CHECK(std::abs(tremoloRateHz(s, 0.0f) - 6.0f) < 1e-4f && std::abs(tremoloRateHz(s, 1.0f) - 6.0f) < 1e-4f, "no change: constant");
    s.endOctaves = 1.0f;
    CHECK(std::abs(tremoloRateHz(s, 0.0f) - 6.0f) < 1e-4f && std::abs(tremoloRateHz(s, 1.0f) - 12.0f) < 1e-3f
              && std::abs(tremoloRateHz(s, 3.0f) - 12.0f) < 1e-3f,
          "one octave up: 6 Hz to 12 Hz, and it stays there after the ramp");
    CHECK(tremoloRampShape(0.5f, 1.0f) > tremoloRampShape(0.5f, 0.0f) && tremoloRampShape(0.5f, 0.0f) > tremoloRampShape(0.5f, -1.0f)
              && std::abs(tremoloRampShape(0.5f, 0.0f) - 0.5f) < 1e-5f && tremoloRampShape(0.0f, 1.0f) < 1e-6f
              && std::abs(tremoloRampShape(1.0f, -1.0f) - 1.0f) < 1e-6f,
          "bend: the change comes early (+), evenly (0) or late (-), always from start to end");

    // depth and edge
    {
        float lo = 1.0f, hi = 0.0f, none = 0.0f;
        int between0 = 0, between1 = 0;
        for (int i = 0; i < 1000; ++i)
        {
            const float ph = (float) i / 1000.0f;
            const float g = tremoloGain(ph, 1.0f, 0.0f);
            lo = std::min(lo, g);
            hi = std::max(hi, g);
            none = std::max(none, std::abs(1.0f - tremoloGain(ph, 0.0f, 0.5f)));
            between0 += g > 0.1f && g < 0.9f ? 1 : 0;
            const float hard = tremoloGain(ph, 1.0f, 1.0f);
            between1 += hard > 0.1f && hard < 0.9f ? 1 : 0;
        }
        CHECK(lo < 1e-4f && hi > 0.9999f && none < 1e-6f, "full depth cuts right out and comes back to full volume; no depth, no change");
        CHECK(between1 * 8 < between0, "a hard EDGE spends far less time in between (%d of 1000 vs %d)", between1, between0);
    }

    // the cycles in a 4-beat ramp at 120 BPM (2 seconds): constant 6 Hz = 12, speeding up = more, slowing down = fewer
    {
        auto cycles = [](float endOctaves)
        {
            TremoloSettings t;
            t.rateHz = 6.0f;
            t.endOctaves = endOctaves;
            t.rampBeats = 4;
            Tremolo trem;
            trem.restart();
            int wraps = 0;
            double last = 0.0;
            for (int i = 0; i < (int) (2.0 * kFs); ++i)
            {
                trem.next(t, kFs, kFs * 0.5);
                wraps += trem.getPhase() < last ? 1 : 0;
                last = trem.getPhase();
            }
            return wraps;
        };
        const int constant = cycles(0.0f), faster = cycles(1.0f), slower = cycles(-1.0f);
        CHECK(constant == 12 && faster >= 16 && faster <= 18 && slower >= 8 && slower <= 9,
              "cycles over the ramp: %d constant, %d speeding up (period shrinking), %d slowing down", constant, faster, slower);
    }

    // Through the engine: on the sequenced sound only, like the distortion
    {
        const Signal in = quietToneWithPick();
        Params seq = neutral();
        seq.seqPlay = 1;
        seq.seqMult = 0;
        seq.tempoBpm = 600.0f;
        seq.steps.fill(mapping::pitchStepKnob(0));
        const Signal plain = render(seq, in);
        Params trem = seq;
        trem.tremOn = true;
        trem.trem.rateHz = 8.0f;
        trem.trem.depth = 1.0f;
        const Signal with = render(trem, in);
        CHECK(maxAbsDiff(with, plain, 0.0, 0.09) < 1.0e-6, "before the sequence starts the signal is untouched");
        CHECK(rms(with, 0.7, 1.0) < 0.8 * rms(plain, 0.7, 1.0) && rms(plain, 0.7, 1.0) > 1.0e-4,
              "while the sequence plays it is cut in and out (%.1f%% of the level)", 100.0 * rms(with, 0.7, 1.0) / rms(plain, 0.7, 1.0));
        CHECK(maxAbsDiff(with, plain, 1.4, 2.1) < 1.0e-6, "after the sequence the signal is clean again (%g)", maxAbsDiff(with, plain, 1.4, 2.1));
        Params off = seq;
        off.trem.depth = 1.0f;
        CHECK(maxAbsDiff(render(off, in), plain) == 0.0, "switched off: nothing changes");
        Params byp = trem;
        byp.bypass = true;
        CHECK(maxAbsDiff(render(byp, in), in) < 1.0e-6, "bypass is clean");
    }
}

// Runs a sine through the fuzz.
Signal fuzzed(double hz, float amp, float sustain, float tone, float volume, double seconds = 0.4)
{
    Fuzz f;
    f.prepare(kFs);
    f.setParams(sustain, tone, volume);
    Signal in = sine(hz, seconds, amp), out(in.size());
    for (size_t i = 0; i < in.size(); ++i)
        out[i] = f.process(in[i]);
    return out;
}

void testFuzz()
{
    std::puts("fuzz: sustain, tone and volume (the big-fuzz style pedal)");

    {
        const Signal quiet = fuzzed(440.0, 0.0f, 1.0f, 0.5f, 1.0f);
        const Signal loud = fuzzed(440.0, 1.0f, 1.0f, 0.5f, 1.0f);
        double peak = 0.0;
        bool finite = true;
        for (float v : loud)
        {
            peak = std::max(peak, (double) std::abs(v));
            finite = finite && std::isfinite(v);
        }
        CHECK(rms(quiet, 0.0, 0.4) < 1.0e-9 && finite && peak < 2.5, "silence stays silent, a full-scale sine stays bounded (peak %.2f)", peak);
    }

    // Sustain: at full sustain a quiet and a loud note come out nearly the same size (heavy compression),
    // at no sustain the louder one is clearly louder.
    {
        auto spread = [](float sustain)
        {
            const double a = rms(fuzzed(440.0, 0.02f, sustain, 0.5f, 0.8f, 0.5), 0.2, 0.5), b = rms(fuzzed(440.0, 0.4f, sustain, 0.5f, 0.8f, 0.5), 0.2, 0.5);
            return db(b / a);
        };
        const double low = spread(0.0f), high = spread(1.0f);
        CHECK(high < low - 6.0 && high < 12.0, "sustain compresses: a 26 dB louder note is only %.1f dB louder at full sustain (%.1f dB at none)", high, low);
    }

    // Square-ish: more sustain, more odd harmonics.
    {
        auto thirdOverFundamental = [](float sustain)
        {
            const Signal o = fuzzed(500.0, 0.2f, sustain, 0.5f, 0.8f, 0.5);
            return tonePower(o, 0.1, 0.5, 1500.0) / std::max(1.0e-18, tonePower(o, 0.1, 0.5, 500.0));
        };
        const double low = thirdOverFundamental(0.0f), high = thirdOverFundamental(1.0f);
        CHECK(high > low && high > 0.02, "sustain adds harmonics (3rd/1st %.4f at 0, %.4f at 1)", low, high);
    }

    // Tone: dark keeps the lows, bright keeps the highs; the middle is scooped.
    {
        const double darkLow = rms(fuzzed(150.0, 0.05f, 0.3f, 0.0f, 0.8f), 0.2, 0.4), brightLow = rms(fuzzed(150.0, 0.05f, 0.3f, 1.0f, 0.8f), 0.2, 0.4);
        const double darkHigh = rms(fuzzed(4000.0, 0.05f, 0.3f, 0.0f, 0.8f), 0.2, 0.4), brightHigh = rms(fuzzed(4000.0, 0.05f, 0.3f, 1.0f, 0.8f), 0.2, 0.4);
        CHECK(darkLow > 2.0 * brightLow && brightHigh > 2.0 * darkHigh, "tone: left keeps the lows, right keeps the highs");
    }

    // Volume: 36 dB range (small input, linear region: sustain 0 gives 0.6 x 3 x 6 = 11 x before the clippers,
    // so keep the input tiny).
    {
        const double lo = rms(fuzzed(300.0, 0.0002f, 0.0f, 0.5f, 0.0f), 0.2, 0.4), hi = rms(fuzzed(300.0, 0.0002f, 0.0f, 0.5f, 1.0f), 0.2, 0.4);
        CHECK(std::abs(db(hi / lo) - 36.0) < 0.5, "volume spans 36 dB (%.2f)", db(hi / lo));
    }

    // Through the engine, in place of the overdrive: sequenced sound only, and different from the overdrive.
    {
        const Signal in = quietToneWithPick();
        Params seq = neutral();
        seq.seqPlay = 1;
        seq.seqMult = 0;
        seq.tempoBpm = 600.0f;
        seq.steps.fill(mapping::pitchStepKnob(0));
        const Signal plain = render(seq, in);
        Params od = seq;
        od.distOn = true;
        od.distDrive = 1.0f;
        Params fz = od;
        fz.distType = 1;
        const Signal withFuzz = render(fz, in), withOd = render(od, in);
        CHECK(maxAbsDiff(withFuzz, plain, 0.0, 0.09) < 1.0e-6 && maxAbsDiff(withFuzz, plain, 1.4, 2.1) < 1.0e-5,
              "fuzz leaves the signal clean before and after the sequence (%g, %g)", maxAbsDiff(withFuzz, plain, 0.0, 0.09), maxAbsDiff(withFuzz, plain, 1.4, 2.1));
        CHECK(rmsDiff(withFuzz, plain, 0.7, 1.0) > 1.0e-3 && rmsDiff(withFuzz, withOd, 0.7, 1.0) > 1.0e-3,
              "while the sequence plays it is fuzzed, differently from the overdrive");
        Params byp = fz;
        byp.bypass = true;
        CHECK(maxAbsDiff(render(byp, in), in) < 1.0e-6, "bypass is clean");
    }
}

// ---------------------------------------------------------------------------------------------
// The EQ: eight bands on the sequenced sound, last in its chain

// The level (dB) of a sine through one Equalizer with these bands, after it has settled.
double eqGainAt(const std::array<EqBand, kNumEqBands>& bands, double hz, float outputDb = 0.0f)
{
    Equalizer eq;
    eq.prepare(kFs);
    eq.setBands(bands, outputDb, 512);
    const Signal in = sine(hz, 0.5, 0.25f);
    Signal out(in.size());
    for (size_t i = 0; i < in.size(); ++i)
        out[i] = eq.process(in[i]);
    return db(rms(out, 0.25, 0.5) / rms(in, 0.25, 0.5));
}

void testEqualizer()
{
    std::puts("eq: eight bands, flat by default, each kind of band, the curve, last on the sequenced sound");
    auto flat = defaultEqBands();
    CHECK(std::abs(eqGainAt(flat, 100.0)) < 0.05 && std::abs(eqGainAt(flat, 1000.0)) < 0.05 && std::abs(eqGainAt(flat, 8000.0)) < 0.05,
          "the default bands are flat");

    auto one = [](EqType type, float hz, float gainDb, float q = 0.71f)
    {
        auto bands = defaultEqBands();
        for (auto& b : bands)
            b.on = false;
        bands[0] = { true, type, hz, gainDb, q };
        return bands;
    };
    CHECK(std::abs(eqGainAt(one(EqType::Bell, 1000.0f, 12.0f, 1.0f), 1000.0) - 12.0) < 0.3 && std::abs(eqGainAt(one(EqType::Bell, 1000.0f, 12.0f, 1.0f), 100.0)) < 1.0,
          "a bell: +12 dB at its frequency, little change far away");
    CHECK(eqGainAt(one(EqType::Bell, 1000.0f, -12.0f, 1.0f), 1000.0) < -11.7, "a bell can cut too");
    CHECK(eqGainAt(one(EqType::LowCut, 400.0f, 0.0f), 50.0) < -30.0 && std::abs(eqGainAt(one(EqType::LowCut, 400.0f, 0.0f), 5000.0)) < 0.2,
          "a low cut removes the lows and leaves the highs");
    CHECK(eqGainAt(one(EqType::HighCut, 1000.0f, 0.0f), 8000.0) < -30.0 && std::abs(eqGainAt(one(EqType::HighCut, 1000.0f, 0.0f), 100.0)) < 0.2,
          "a high cut removes the highs and leaves the lows");
    CHECK(eqGainAt(one(EqType::Notch, 1000.0f, 0.0f, 4.0f), 1000.0) < -30.0 && std::abs(eqGainAt(one(EqType::Notch, 1000.0f, 0.0f, 4.0f), 3000.0)) < 1.0,
          "a notch takes out one frequency");
    CHECK(std::abs(eqGainAt(one(EqType::LowShelf, 200.0f, 9.0f), 40.0) - 9.0) < 0.6 && std::abs(eqGainAt(one(EqType::LowShelf, 200.0f, 9.0f), 5000.0)) < 0.3,
          "a low shelf lifts everything below it");
    CHECK(std::abs(eqGainAt(one(EqType::HighShelf, 4000.0f, -9.0f), 16000.0) + 9.0) < 0.8 && std::abs(eqGainAt(one(EqType::HighShelf, 4000.0f, -9.0f), 200.0)) < 0.3,
          "a high shelf lowers everything above it");
    CHECK(std::abs(eqGainAt(flat, 1000.0, 6.0f) - 6.0) < 0.05, "the output gain");

    // The curve the editor draws is the response that is heard.
    {
        auto bands = defaultEqBands();
        bands[2] = { true, EqType::Bell, 300.0f, 8.0f, 2.0f };
        bands[6] = { true, EqType::HighShelf, 6000.0f, -6.0f, 0.71f };
        bands[0] = { true, EqType::LowCut, 60.0f, 0.0f, 0.71f };
        double worst = 0.0;
        for (double hz : { 40.0, 100.0, 300.0, 1000.0, 3000.0, 10000.0 })
            worst = std::max(worst, std::abs(eqResponseDb(bands, 2.0f, hz, kFs) - eqGainAt(bands, hz, 2.0f)));
        CHECK(worst < 0.3, "the drawn curve matches what is heard (worst %.2f dB)", worst);
    }

    // Stays stable while every setting is thrown around.
    {
        Equalizer eq;
        eq.prepare(kFs);
        Rng rng(5);
        bool finite = true;
        for (int block = 0; block < 400; ++block)
        {
            auto bands = defaultEqBands();
            for (auto& b : bands)
            {
                b.on = rng.next() > 0.0f;
                b.type = (EqType) (std::abs((int) (rng.next() * 100.0f)) % kNumEqTypes);
                b.freq = 20.0f * std::pow(1000.0f, 0.5f + 0.5f * rng.next());
                b.gainDb = 15.0f * rng.next();
                b.q = 0.1f + 9.0f * (0.5f + 0.5f * rng.next());
            }
            eq.setBands(bands, 0.0f, 256);
            for (int i = 0; i < 256; ++i)
                finite = finite && std::isfinite(eq.process(0.3f * rng.next()));
        }
        CHECK(finite, "random settings changed every block stay finite");
    }

    // In the engine: last on the sequenced sound, off by default, never on the clean signal.
    {
        const Signal in = quietToneWithPick();
        Params seq = neutral();
        seq.seqPlay = 5;
        seq.tempoBpm = 120.0f;
        seq.steps.fill(mapping::pitchStepKnob(0));
        const Signal plain = render(seq, in);
        Params withEq = seq;
        withEq.eqBands = one(EqType::HighCut, 300.0f, 0.0f);
        CHECK(maxAbsDiff(render(withEq, in), plain) == 0.0, "the EQ is off until it is switched on");
        withEq.eqOn = true;
        const Signal eqd = render(withEq, in);
        CHECK(rmsDiff(eqd, plain, 0.6, 1.0) > 1.0e-4, "switched on it changes the sequenced sound (%g)", rmsDiff(eqd, plain, 0.6, 1.0));
        Params dry = withEq;
        dry.mix = 0.0f;
        CHECK(maxAbsDiff(render(dry, in), in) < 1.0e-6, "dry/wet at 0 is still the clean signal");
        Params byp = withEq;
        byp.bypass = true;
        CHECK(maxAbsDiff(render(byp, in), in) < 1.0e-6, "bypass is clean");
    }
}

// ---------------------------------------------------------------------------------------------
// Drum map: trigger events into the engine, the loop scheduler, the tempo estimate

// Runs an engine sample by sample (so the step can be read after every sample) and returns the step
// shown after each one. events[k] is applied at absolute sample eventsAt[k].
std::vector<int> stepTrace(const Params& p, const Signal& in, const std::vector<std::pair<size_t, TriggerAction>>& events)
{
    Engine e;
    e.prepare(kFs);
    Signal buf = in;
    std::vector<int> steps(in.size(), -1);
    size_t next = 0;
    for (size_t i = 0; i < in.size(); ++i)
    {
        TriggerEvent ev[4];
        int n = 0;
        while (next < events.size() && events[next].first == i && n < 4)
            ev[n++] = { 0, events[next++].second };
        float* ch = buf.data() + i;
        e.process(&ch, 1, 1, p, ev, n);
        steps[i] = e.getCurrentStep();
    }
    return steps;
}

size_t firstIndexOf(const std::vector<int>& v, int value, size_t from = 0)
{
    for (size_t i = from; i < v.size(); ++i)
        if (v[i] == value)
            return i;
    return v.size();
}

// The loop section: the chosen steps repeat (a number of times or forever) before the sequence goes on, and a
// restart still begins at step 1.
// Each step can control its own thing: with the sequencer on pitch, a step set to FILTER changes the filter instead of
// the pitch, and a step set to SAMPLE changes the sample rate.
void testStepTypes()
{
    std::puts("step types");
    Params p = neutral();
    p.seqPlay = 5;           // continuous
    p.seqMult = 0;
    p.tempoBpm = 60.0f;      // one step per second
    p.pickTriggers = false;
    p.seqType = SeqType::Pitch;
    p.steps = { mapping::pitchStepKnob(0), 0.1f, 0.1f, mapping::pitchStepKnob(0), mapping::pitchStepKnob(0), mapping::pitchStepKnob(0) };
    p.stepTypes = { -1, 1, 2, -1, -1, -1 };  // step 2 the filter at 10%, step 3 the sample rate at 10%
    const Signal in = sine(3000.0, 3.0, 0.3f);
    const Signal out = render(p, in);
    // compared with the same steps all on pitch (a 0.1 knob is about -9 semitones there)
    Params allPitch = p;
    allPitch.stepTypes = { -1, -1, -1, -1, -1, -1 };
    const Signal pitchOnly = render(allPitch, in);
    CHECK(rmsDiff(out, pitchOnly, 0.1, 0.9) < 1.0e-3, "a step that follows the sequencer type is unchanged");
    CHECK(rmsDiff(out, pitchOnly, 1.1, 1.9) > 0.01, "a FILTER step does something else than the pitch step it was");
    Params filterSeq = p;
    filterSeq.seqType = SeqType::Filter;
    filterSeq.stepTypes = { 0, -1, 2, 0, 0, 0 }; // the same steps, written the other way round
    const Signal swapped = render(filterSeq, in);
    CHECK(rmsDiff(out, swapped, 0.1, 2.9) < 1.0e-4, "the same per-step choice gives the same sound whatever the sequencer type");
    Params rateStep = p;
    rateStep.stepTypes = { -1, -1, 2, -1, -1, -1 };
    CHECK(rmsDiff(render(rateStep, in), pitchOnly, 2.1, 2.9) > 0.01, "a SAMPLE step crushes the sample rate");
}

// LENGTH: the sequence plays its first seqLength steps (up to sixteen). The lanes: what else each step does while it
// plays (fewer bits, its level, its share of the tremolo, the delay and the reverb, and firing the stutter).
void testSeqLengthAndLanes()
{
    std::puts("sequence length and step lanes");
    Params p = neutral();
    p.seqPlay = 5; // continuous
    p.seqMult = 0;
    p.tempoBpm = 600.0f; // a step every 0.1 s
    p.pickTriggers = false;
    p.steps.fill(mapping::pitchStepKnob(0));
    const Signal silence((size_t) (kFs * 2.0), 0.0f);
    auto stepsSeen = [&](const Params& q)
    {
        const auto trace = stepTrace(q, silence, {});
        int most = -1;
        for (int s : trace)
            most = std::max(most, s);
        return most;
    };
    CHECK(stepsSeen(p) == kDefaultSeqLength - 1, "six steps by default: the last one played is step %d", stepsSeen(p) + 1);
    Params ten = p;
    ten.seqLength = 10;
    CHECK(stepsSeen(ten) == 9, "LENGTH 10 plays ten steps (%d)", stepsSeen(ten) + 1);
    Params sixteen = p;
    sixteen.seqLength = 99; // clamped
    CHECK(stepsSeen(sixteen) == kNumSteps - 1, "a length past sixteen plays sixteen (%d)", stepsSeen(sixteen) + 1);
    {
        const auto trace = stepTrace(ten, silence, {});
        const size_t first10 = firstIndexOf(trace, 9), back = firstIndexOf(trace, 0, first10);
        CHECK(first10 < trace.size() && back < trace.size() && back - first10 > 4000 && back - first10 < 5600,
              "after step 10 it goes back to step 1 (%d samples later)", (int) (back - first10));
    }
    Params looped = ten;
    looped.loopOn = true;
    looped.loopFrom = 8;
    looped.loopTo = 12; // past the end: the loop stops at step 10
    {
        const auto trace = stepTrace(looped, silence, {});
        int afterLoopStart = -1;
        for (size_t i = firstIndexOf(trace, 9); i < trace.size(); ++i)
            if (trace[i] != 9)
            {
                afterLoopStart = trace[i];
                break;
            }
        CHECK(afterLoopStart == 8, "a loop section reaching past the end loops up to the last step (back to step %d)", afterLoopStart + 1);
    }

    // the lanes, on a steady tone (step n plays from 0.1 n s)
    const Signal in = sine(220.0, 1.2, 0.3f);
    const Signal plain = render(p, in);
    CHECK(rmsDiff(render(p, in), plain, 0.05, 1.1) < 1.0e-6, "neutral lanes change nothing");
    {
        Params q = p;
        q.lanes[(size_t) Lane::Level][2] = 0.0f; // step 3 silent
        q.lanes[(size_t) Lane::Level][4] = 0.5f; // step 5 half
        const Signal out = render(q, in);
        CHECK(rms(out, 0.215, 0.29) < 0.02 * rms(plain, 0.215, 0.29), "a step at LEVEL 0 is silent (%g)", rms(out, 0.215, 0.29));
        const double half = rms(out, 0.415, 0.49) / rms(plain, 0.415, 0.49);
        CHECK(half > 0.45 && half < 0.55, "a step at LEVEL 50%% is half as loud (%g)", half);
        CHECK(rmsDiff(out, plain, 0.315, 0.39) < 1.0e-4, "the steps between are as they were");
    }
    {
        Params q = p;
        q.lanes[(size_t) Lane::Crush][1] = 1.0f; // step 2 crushed to one bit
        const Signal out = render(q, in);
        CHECK(rmsDiff(out, plain, 0.115, 0.19) > 0.03 && rmsDiff(out, plain, 0.215, 0.29) < 1.0e-4,
              "CRUSH crushes its step only (%g)", rmsDiff(out, plain, 0.115, 0.19));
    }
    {
        Params q = p;
        q.tremOn = true;
        q.trem.rateHz = 20.0f;
        q.trem.depth = 1.0f;
        Params calm = q;
        calm.lanes[(size_t) Lane::Trem][3] = 0.0f; // no tremolo on step 4
        const Signal trem = render(q, in), stepWithout = render(calm, in);
        CHECK(rmsDiff(stepWithout, plain, 0.32, 0.39) < 0.01 && rmsDiff(trem, plain, 0.32, 0.39) > 0.03,
              "TREM 0 takes the tremolo off its step (%g)", rmsDiff(stepWithout, plain, 0.32, 0.39));
    }
    {
        Params q = p;
        q.delayOn = true;
        q.delayMix = 1.0f;
        q.delayDivision = 0;
        Params noSend = q;
        for (auto& v : noSend.lanes[(size_t) Lane::Delay])
            v = 0.0f;
        Params q2 = q, noSend2 = noSend;
        q2.reverbMix = noSend2.reverbMix = 0.0f;
        CHECK(rmsDiff(render(noSend2, in), plain, 0.1, 1.1) < 1.0e-4 && rmsDiff(render(q2, in), plain, 0.1, 1.1) > 0.01,
              "DELAY 0 on every step sends nothing to the delay");
        Params rev = p;
        rev.reverbMix = 1.0f;
        Params revNone = rev;
        for (auto& v : revNone.lanes[(size_t) Lane::Reverb])
            v = 0.0f;
        CHECK(rmsDiff(render(revNone, in), plain, 0.1, 1.1) < 1.0e-4 && rmsDiff(render(rev, in), plain, 0.1, 1.1) > 0.01,
              "REVERB 0 on every step sends nothing to the reverb (%g)", rmsDiff(render(revNone, in), plain, 0.1, 1.1));
    }
    {
        // the STUTTER lane fires the stutter as its step begins, even with the STUTTER knob off
        Params q = p;
        q.stutter = 0;
        q.lanes[(size_t) Lane::Stutter][3] = 1.0f;
        Engine e;
        e.prepare(kFs);
        Signal buf = in;
        bool before = false, during = false;
        for (size_t i = 0; i + 64 <= buf.size(); i += 64)
        {
            float* ch = buf.data() + i;
            e.process(&ch, 1, 64, q);
            const double t = (double) i / kFs;
            before = before || (t < 0.29 && e.isStuttering());
            during = during || (t > 0.3 && t < 0.4 && e.isStuttering());
        }
        CHECK(!before && during, "a STUTTER step fires the stutter as it begins (before %d, during %d)", (int) before, (int) during);
    }
}

// SEQ ONLY: the clean signal is cut and the effect side only sounds while a sequence or stutter plays; the reverb
// still rings out after it.
void testSeqOnly()
{
    std::puts("sequence only");
    const Signal in = sine(330.0, 2.0, 0.3f);
    Params idle = neutral(); // no sequence, no stutter
    idle.seqOnly = true;
    CHECK(rms(render(idle, in), 0.2, 1.9) < 1.0e-6, "nothing playing: silence (%g)", rms(render(idle, in), 0.2, 1.9));
    idle.mix = 0.0f;
    CHECK(rms(render(idle, in), 0.2, 1.9) < 1.0e-6, "the clean signal is cut even with DRY/WET fully dry");

    Params running = neutral();
    running.seqPlay = 5; // continuous
    running.pickTriggers = false;
    Params runningOnly = running;
    runningOnly.seqOnly = true;
    runningOnly.mix = 0.3f; // no effect while SEQ ONLY is on
    CHECK(rmsDiff(render(runningOnly, in), render(running, in), 0.2, 1.9) < 1.0e-4, "while the sequence plays, the sequenced sound as usual (fully wet)");

    // one pass of six 100 ms steps from the start, then the sequence stops: silence after, except the reverb's tail
    Params once = neutral();
    once.seqPlay = 1;
    once.seqMult = 0;
    once.tempoBpm = 600.0f;
    once.steps.fill(mapping::pitchStepKnob(0));
    once.pickTriggers = false;
    once.seqOnly = true;
    auto play = [&](const Params& q)
    {
        Engine e;
        e.prepare(kFs);
        Signal out = in;
        for (size_t pos = 0; pos < out.size(); pos += 256)
        {
            float* ch = out.data() + pos;
            const TriggerEvent start { 0, TriggerAction::RestartSequence };
            e.process(&ch, 1, (int) std::min<size_t>(256, out.size() - pos), q, pos == 0 ? &start : nullptr, pos == 0 ? 1 : 0);
        }
        return out;
    };
    const auto dryTail = play(once);
    CHECK(rms(dryTail, 0.1, 0.5) > 0.1 && rms(dryTail, 0.8, 1.9) < 1.0e-5,
          "the sequence sounds (%.3f) and then the plug-in goes silent (%g)", rms(dryTail, 0.1, 0.5), rms(dryTail, 0.8, 1.9));
    Params withReverb = once;
    withReverb.reverbMix = 0.8f;
    const auto wetTail = play(withReverb);
    CHECK(rms(wetTail, 0.65, 0.9) > 1.0e-3, "the reverb still rings out after the sequence (%g)", rms(wetTail, 0.65, 0.9));
    Params bypassed = idle;
    bypassed.bypass = true;
    CHECK(rmsDiff(render(bypassed, in), in, 0.2, 1.9) < 1.0e-6, "bypass still gives back the clean input");
}

void testSequenceLoop()
{
    std::puts("sequence loop");
    Params p = neutral();
    p.seqPlay = 1;            // once
    p.seqMult = 7;            // x128
    p.tempoBpm = 240.0f;      // a step is about 94 samples
    p.pickTriggers = false;
    const Signal quiet(6000, 0.0f);
    auto order = [&](const Params& q, const std::vector<std::pair<size_t, TriggerAction>>& events, size_t upTo)
    {
        const auto trace = stepTrace(q, quiet, events);
        std::vector<int> seen;
        for (size_t i = 0; i < upTo && i < trace.size(); ++i)
            if (seen.empty() || seen.back() != trace[i])
                seen.push_back(trace[i]);
        return seen;
    };
    const std::vector<std::pair<size_t, TriggerAction>> start { { 0, TriggerAction::RestartSequence } };

    CHECK((order(p, start, 6000) == std::vector<int> { 0, 1, 2, 3, 4, 5, -1 }), "loop off: one pass of six steps");

    Params twice = p;
    twice.loopOn = true;
    twice.loopFrom = 3;
    twice.loopTo = 5;
    twice.loopPlays = 2;
    CHECK((order(twice, start, 6000) == std::vector<int> { 0, 1, 2, 3, 4, 5, 3, 4, 5, -1 }), "steps 4 to 6 twice, then the pass ends");

    Params middle = twice;
    middle.loopFrom = 3; // either order works
    middle.loopTo = 1;
    middle.loopPlays = 3;
    CHECK((order(middle, start, 6000) == std::vector<int> { 0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 4, 5, -1 }),
          "steps 2 to 4 three times, then steps 5 and 6");

    Params forever = twice;
    forever.loopPlays = 0;
    const auto loops = order(forever, start, 6000);
    CHECK(loops.size() > 20 && loops.back() != -1 && std::find(loops.begin() + 3, loops.end(), 0) == loops.end()
              && std::find(loops.begin() + 3, loops.end(), 2) == loops.end(),
          "forever: steps 4 to 6 keep going and never go back to the start on their own (%d changes)", (int) loops.size());
    const auto restarted = order(forever, { { 0, TriggerAction::RestartSequence }, { 3000, TriggerAction::RestartSequence } }, 6000);
    const auto firstAgain = std::find(restarted.begin() + 1, restarted.end(), 0);
    CHECK(firstAgain != restarted.end() && firstAgain + 3 < restarted.end() && firstAgain[1] == 1 && firstAgain[2] == 2 && firstAgain[3] == 3,
          "forever: a restart plays steps 1 to 3 again, then the loop");

    Params skipped = twice;
    skipped.steps[4] = 0.0f; // step 5 skipped
    CHECK((order(skipped, start, 6000) == std::vector<int> { 0, 1, 2, 3, 5, 3, 5, -1 }), "a skipped step inside the loop stays skipped");

    Params allSkipped = twice;
    allSkipped.steps[3] = allSkipped.steps[4] = allSkipped.steps[5] = 0.0f;
    CHECK((order(allSkipped, start, 6000) == std::vector<int> { 0, 1, 2, -1 }), "a loop of skipped steps does nothing");

    Params single = forever;
    single.loopFrom = single.loopTo = 2;
    const auto held = order(single, start, 6000);
    CHECK((held == std::vector<int> { 0, 1, 2 }), "a one-step loop holds that step (%d changes)", (int) held.size());

    // Steps moved by trigger events (steps on hits) follow the loop too.
    Params hits = twice;
    hits.stepsOnHits = true;
    std::vector<std::pair<size_t, TriggerAction>> nexts;
    for (size_t k = 0; k < 12; ++k)
        nexts.push_back({ k * 100, TriggerAction::NextStep });
    CHECK((order(hits, nexts, 6000) == std::vector<int> { 0, 1, 2, 3, 4, 5, 3, 4, 5, -1, 0, 1 }), "steps on hits follow the loop");
}

// The EQ's reference: the sound reaching the EQ can be captured as a clip (without the silence around it), played in
// a loop through the EQ instead of the live sound, copied out, and its spectrum feed follows what the EQ hears.
void testEqReference()
{
    std::puts("eq reference");
    Engine e;
    e.prepare(kFs);
    auto& ref = e.getEqReference();
    Params p = neutral(); // a clean signal: what reaches the EQ is the input
    auto feed = [&](Signal& buf, const Params& q)
    {
        for (size_t pos = 0; pos < buf.size(); pos += 256)
        {
            float* ch = buf.data() + pos;
            e.process(&ch, 1, (int) std::min<size_t>(256, buf.size() - pos), q);
        }
    };

    Signal silence((size_t) (0.5 * kFs), 0.0f);
    feed(silence, p);
    ref.requestCapture();
    CHECK(ref.isCapturePending(), "a capture waits for the audio thread");
    Signal more = silence;
    feed(more, p);
    CHECK(!ref.isCapturePending() && ref.getClipSamples() == 0, "capturing silence gives no clip");

    // half a second of silence, then one second of tone, then silence: the clip is the tone
    Signal played((size_t) (2.0 * kFs), 0.0f);
    const Signal tone = sine(440.0, 1.0, 0.4f);
    std::copy(tone.begin(), tone.end(), played.begin() + (long) (0.5 * kFs));
    feed(played, p);
    ref.requestCapture();
    Signal tick(256, 0.0f);
    feed(tick, p);
    const int n = ref.getClipSamples();
    CHECK(std::abs(n - (int) tone.size()) < 64, "the clip is the second of tone without the silence (%d samples)", n);
    std::vector<std::vector<float>> clip;
    CHECK(ref.copyClip(clip) && clip.size() == 1 && clip[0].size() == (size_t) n, "the clip can be copied out");
    // the clip length: only the last part is played and copied, and it can be changed after capturing
    ref.setClipSeconds(0.5f);
    std::vector<std::vector<float>> part;
    CHECK(ref.getUsedSamples() == (int) (0.5 * kFs) && ref.copyClip(part) && part[0].size() == (size_t) (0.5 * kFs)
              && std::equal(part[0].begin(), part[0].end(), clip[0].end() - (long) part[0].size()),
          "a 0.5 s length uses the last half second of the clip");
    ref.setClipSeconds(4.0f);
    CHECK(ref.getUsedSamples() == n, "a length longer than the clip uses all of it");
    float diff = 0.0f;
    const size_t offset = (size_t) std::distance(tone.begin(), std::find_if(tone.begin(), tone.end(), [](float v) { return std::abs(v) > 1.0e-3f; }));
    for (size_t i = 0; i < clip[0].size() && i + offset < tone.size(); ++i)
        diff = std::max(diff, std::abs(clip[0][i] - tone[i + offset]));
    CHECK(diff < 1.0e-5f, "and holds what reached the EQ (max diff %g)", (double) diff);

    // playing the clip: it loops on its own, whatever comes in, and ignores the dry/wet mix
    ref.setAudition(true);
    Params dryOnly = p;
    dryOnly.mix = 0.0f;
    Signal heard((size_t) (2.5 * kFs), 0.0f);
    feed(heard, dryOnly);
    CHECK(rms(heard, 0.1, 0.9) > 0.2 && rms(heard, 1.1, 1.9) > 0.2, "the clip plays in a loop with no input (rms %.3f, %.3f)", rms(heard, 0.1, 0.9), rms(heard, 1.1, 1.9));
    Params boosted = p;
    boosted.eqOn = true;
    boosted.eqBands[3].on = true;
    boosted.eqBands[3].type = EqType::Bell;
    boosted.eqBands[3].freq = 440.0f;
    boosted.eqBands[3].gainDb = 12.0f;
    Signal louder((size_t) kFs, 0.0f);
    feed(louder, boosted);
    CHECK(rms(louder, 0.3, 0.9) > 2.5 * rms(heard, 0.1, 0.9), "and through the EQ: +12 dB at 440 Hz makes it louder (%.3f)", rms(louder, 0.3, 0.9));
    float before[1024], after[1024];
    ref.readScope(before, after, 1024);
    float peakBefore = 0.0f, peakAfter = 0.0f;
    for (int i = 0; i < 1024; ++i)
    {
        peakBefore = std::max(peakBefore, std::abs(before[i]));
        peakAfter = std::max(peakAfter, std::abs(after[i]));
    }
    CHECK(peakBefore > 0.3f && peakAfter > 2.5f * peakBefore, "the spectrum feed has the sound before and after the EQ (%.2f, %.2f)", (double) peakBefore, (double) peakAfter);
    Params bypassed = boosted;
    bypassed.bypass = true;
    Signal input = sine(220.0, 0.5, 0.3f), bypassOut = input;
    feed(bypassOut, bypassed);
    CHECK(rmsDiff(bypassOut, input, 0.2, 0.5) < 1.0e-4, "bypass still gives back the input");

    // stopping it gives the live sound back, and the clip stays for next time
    ref.setAudition(false);
    Signal live = sine(330.0, 0.5, 0.3f), liveOut = live;
    feed(liveOut, p);
    CHECK(rmsDiff(liveOut, live, 0.1, 0.5) < 1.0e-4 && ref.getClipSamples() == n, "stopped: the live sound again, clip kept");
    e.reset();
    CHECK(ref.getClipSamples() == n, "a reset keeps the clip");
    e.prepare(kFs);
    CHECK(ref.getClipSamples() == n, "preparing again at the same rate keeps it");
    e.prepare(44100.0);
    CHECK(ref.getClipSamples() == 0, "a new sample rate forgets it");
}

// The loop section's sounds: its steps play the loop section's own sounds (or the main ones when it has none), with
// its own volume and chance; the other steps keep the main ones.
void testLoopSectionSamples()
{
    std::puts("loop section samples");
    auto makeSet = [](float level)
    {
        SampleSet s;
        SampleClip c;
        c.data.assign(6000, level); // longer than a step, so a step always sounds
        c.rate = kFs;
        s.clips.push_back(std::move(c));
        return s;
    };
    const SampleSet mainSet = makeSet(0.2f), loopSet = makeSet(-0.4f);
    Params p = neutral();
    p.seqPlay = 1;
    p.seqMult = 0;
    p.tempoBpm = 600.0f; // 100 ms steps
    p.steps.fill(mapping::pitchStepKnob(0));
    p.pickTriggers = false;
    p.sampleSet = &mainSet;
    p.sampleMix = p.loopSampleMix = 1.0f;
    p.sampleBaseDb = p.loopSampleBaseDb = -60.0f;
    p.sampleMaxDb = p.loopSampleMaxDb = 0.0f;
    p.loopOn = true;
    p.loopFrom = 3;
    p.loopTo = 5;
    p.loopPlays = 2; // steps 1-3 (0..0.3 s), then 4-6 twice (0.3..0.9 s)
    auto play = [&](const Params& q)
    {
        Engine e;
        e.prepare(kFs);
        Signal out((size_t) kFs, 0.0f);
        for (size_t pos = 0; pos < out.size(); pos += 256)
        {
            float* ch = out.data() + pos;
            const TriggerEvent start { 0, TriggerAction::RestartSequence };
            e.process(&ch, 1, (int) std::min<size_t>(256, out.size() - pos), q, pos == 0 ? &start : nullptr, pos == 0 ? 1 : 0);
        }
        return out;
    };
    auto mean = [](const Signal& s, double t0, double t1)
    {
        double sum = 0.0;
        for (size_t i = (size_t) (t0 * kFs); i < (size_t) (t1 * kFs); ++i)
            sum += s[i];
        return sum / ((t1 - t0) * kFs);
    };

    Params own = p;
    own.loopSampleSet = &loopSet;
    const auto a = play(own);
    CHECK(std::abs(mean(a, 0.05, 0.28) - 0.2) < 0.01 && std::abs(mean(a, 0.4, 0.88) + 0.4) < 0.01,
          "the main steps play the main sounds (%.3f), the loop steps their own (%.3f)", mean(a, 0.05, 0.28), mean(a, 0.4, 0.88));
    const auto b = play(p);
    CHECK(std::abs(mean(b, 0.4, 0.88) - 0.2) < 0.01, "a loop section without sounds of its own plays the main ones (%.3f)", mean(b, 0.4, 0.88));
    Params quietLoop = own;
    quietLoop.loopSampleMaxDb = -60.0f;
    const auto c = play(quietLoop);
    CHECK(std::abs(mean(c, 0.4, 0.88)) < 0.005 && std::abs(mean(c, 0.05, 0.28) - 0.2) < 0.01, "the loop section's own volume (silent here) leaves the main steps alone");
    Params louderLoop = own;
    louderLoop.loopSampleMaxDb = -6.0f;
    CHECK(std::abs(mean(play(louderLoop), 0.4, 0.88) + 0.4 * 0.501) < 0.01, "the loop section's max volume sets its level");
    Params never = own;
    never.loopSampleChance = 0.0f;
    CHECK(std::abs(mean(play(never), 0.45, 0.88)) < 0.005, "a loop chance of 0 gives the loop steps no sounds");
    Params noLoop = own;
    noLoop.loopOn = false;
    CHECK(std::abs(mean(play(noLoop), 0.05, 0.58) - 0.2) < 0.01, "with the loop off every step plays the main sounds");
}

// The delay: echoes at the delay time, an exact number of repeats (or forever), each DECAY times the one before, the
// cuts on the echoes only; in the engine its time follows the tempo and it only hears the sequenced sound.
void testDelay()
{
    std::puts("delay");
    {
        Delay d;
        d.prepare(kFs);
        DelaySettings s;
        s.timeSamples = 100.0;
        s.decay = 1.0f; // each echo 0.95 of the one before
        s.repeats = 3;
        std::vector<float> out(600);
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = d.process(i == 0 ? 1.0f : 0.0f, s);
        const float g = Delay::decayGain(1.0f);
        CHECK(std::abs(out[100] - 1.0f) < 1.0e-4f && std::abs(out[200] - g) < 1.0e-4f && std::abs(out[300] - g * g) < 1.0e-4f && std::abs(out[400]) < 1.0e-6f,
              "three repeats: echoes at 100, 200 and 300 samples (%.3f %.3f %.3f), none at 400", (double) out[100], (double) out[200], (double) out[300]);
        float elsewhere = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
            if (i != 100 && i != 200 && i != 300)
                elsewhere = std::max(elsewhere, std::abs(out[i]));
        CHECK(elsewhere < 1.0e-6f, "and nothing in between (the dry sound is not in the output)");
    }
    {
        Delay d;
        d.prepare(kFs);
        DelaySettings s;
        s.timeSamples = 100.0;
        s.decay = 0.5f;
        s.repeats = kDelayForever;
        std::vector<float> out(2100);
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = d.process(i == 0 ? 1.0f : 0.0f, s);
        const float g = Delay::decayGain(0.5f);
        CHECK(std::abs(out[100] - 1.0f) < 1.0e-4f && std::abs(out[1000] - std::pow(g, 9.0f)) < 1.0e-5f && out[2000] > 0.0f,
              "forever: the echoes keep coming, each %.3f of the one before", (double) g);
    }
    {
        Delay d;
        d.prepare(kFs);
        DelaySettings s;
        s.timeSamples = 100.0;
        s.decay = 0.0f;
        s.repeats = 8;
        float later = 0.0f, first = 0.0f;
        for (int i = 0; i < 1000; ++i)
        {
            const float v = d.process(i == 0 ? 1.0f : 0.0f, s);
            if (i == 100)
                first = v;
            else if (i > 150)
                later = std::max(later, std::abs(v));
        }
        CHECK(std::abs(first - 1.0f) < 1.0e-4f && later < 1.0e-6f, "decay at 0: one echo only");
    }
    {
        // the cuts act on the echoes: a high cut takes the top off white noise's echoes
        auto echoEnergy = [](float highCut, float lowCut)
        {
            Delay d;
            d.prepare(kFs);
            DelaySettings s;
            s.timeSamples = 2400.0;
            s.repeats = 1;
            s.highCutHz = highCut;
            s.lowCutHz = lowCut;
            Rng rng(5);
            double hf = 0.0, total = 0.0;
            float prev = 0.0f;
            for (int i = 0; i < 24000; ++i)
            {
                const float v = d.process(rng.next(), s);
                if (i > 4800)
                {
                    hf += (double) ((v - prev) * (v - prev));
                    total += (double) (v * v);
                }
                prev = v;
            }
            return std::make_pair(hf, total);
        };
        const auto open = echoEnergy(20000.0f, 20.0f), dark = echoEnergy(1000.0f, 20.0f);
        CHECK(dark.first < 0.1 * open.first, "a 1 kHz high cut darkens the echoes");
        // a low cut: a 100 Hz tone's echo nearly goes, a 6 kHz tone's stays
        auto echoLevel = [](float hz, float lowCut)
        {
            Delay d;
            d.prepare(kFs);
            DelaySettings s;
            s.timeSamples = 2400.0;
            s.repeats = 1;
            s.lowCutHz = lowCut;
            double sum = 0.0;
            for (int i = 0; i < 24000; ++i)
            {
                const float v = d.process(std::sin(6.2831853f * hz * (float) i / (float) kFs), s);
                if (i > 9600)
                    sum += (double) (v * v);
            }
            return std::sqrt(sum / 14400.0);
        };
        CHECK(echoLevel(100.0f, 1500.0f) < 0.05 * echoLevel(100.0f, 20.0f) && echoLevel(6000.0f, 1500.0f) > 0.7 * echoLevel(6000.0f, 20.0f),
              "a 1.5 kHz low cut takes the lows out of the echoes (%.4f of %.4f) and keeps the highs (%.4f of %.4f)",
              echoLevel(100.0f, 1500.0f), echoLevel(100.0f, 20.0f), echoLevel(6000.0f, 1500.0f), echoLevel(6000.0f, 20.0f));
    }

    // in the engine: the time is a note length of the tempo, and only the sequenced sound is echoed
    auto burstThrough = [&](Params p)
    {
        Signal in((size_t) (2.0 * kFs), 0.0f);
        for (size_t i = 0; i < 240; ++i)
            in[(size_t) (0.1 * kFs) + i] = 0.5f * std::sin(6.2831853f * 1000.0f * (float) i / (float) kFs);
        return render(p, in);
    };
    Params p = neutral();
    p.seqPlay = 5; // continuous, so the sound is the sequenced sound
    p.steps.fill(mapping::pitchStepKnob(0));
    p.pickTriggers = false;
    p.tempoBpm = 120.0f;
    p.delayOn = true;
    p.delayMix = 1.0f;
    p.delayDecay = 0.5f;
    p.delayRepeats = 2;
    p.delayDivision = 5; // 1/8 = 0.25 s at 120 BPM
    const auto echoed = burstThrough(p);
    CHECK(rms(echoed, 0.345, 0.36) > 0.05 && rms(echoed, 0.595, 0.61) > 0.02 && rms(echoed, 0.75, 0.9) < 1.0e-4 && rms(echoed, 0.2, 0.34) < 1.0e-4,
          "1/8 at 120 BPM: echoes 0.25 s and 0.5 s after the sound, then quiet");
    Params slow = p;
    slow.tempoBpm = 60.0f;
    const auto slower = burstThrough(slow);
    CHECK(rms(slower, 0.595, 0.61) > 0.05 && rms(slower, 0.345, 0.36) < 1.0e-4, "at 60 BPM the same 1/8 is 0.5 s");
    Params dotted = p;
    dotted.delayDivision = 9; // 1/4 D = 0.75 s
    CHECK(rms(burstThrough(dotted), 0.845, 0.86) > 0.05, "a dotted quarter at 120 BPM is 0.75 s");
    Params idle = p;
    idle.seqPlay = 0;
    const auto notSequenced = burstThrough(idle);
    CHECK(rms(notSequenced, 0.2, 1.9) < 1.0e-5, "with no sequence playing, nothing is echoed");
    Params off = p;
    off.delayOn = false;
    CHECK(rms(burstThrough(off), 0.2, 1.9) < 1.0e-5, "off: no echoes");
    Params dry = p;
    dry.delayMix = 0.0f;
    CHECK(rms(burstThrough(dry), 0.2, 1.9) < 1.0e-5, "mix at 0: no echoes");
}

// The sampler's chain: with nothing on, the clip only comes out levelled to the sound peak; VOLUME sets that level; the
// distortion, the sample-rate reducer, the bit crusher and the EQ each change it the way the pedal's own do.
// MAX BEATS: a random sound plays at most that many beats of the tempo, then fades out.
void testSampleMaxBeats()
{
    std::puts("sample max beats");
    SampleSet set;
    SampleClip c;
    c.data.assign((size_t) (2.0 * kFs), 0.5f); // two seconds
    c.rate = kFs;
    set.clips.push_back(std::move(c));
    Params p = neutral();
    p.seqPlay = 5;
    p.seqMult = 0;          // one step per beat
    p.tempoBpm = 120.0f;    // a beat is 0.5 s
    p.steps.fill(mapping::pitchStepKnob(0));
    p.pickTriggers = false;
    p.sampleSet = &set;
    p.sampleMix = 1.0f;
    p.sampleBaseDb = -60.0f;
    p.sampleMaxDb = 0.0f;
    const Signal quiet((size_t) (1.5 * kFs), 0.0f);
    auto mean = [](const Signal& s, double t0, double t1)
    {
        double sum = 0.0;
        for (size_t i = (size_t) (t0 * kFs); i < (size_t) (t1 * kFs); ++i)
            sum += s[i];
        return sum / ((t1 - t0) * kFs);
    };
    const auto whole = render(p, quiet);
    CHECK(mean(whole, 0.3, 0.45) > 0.4, "the whole sound: it plays through the step (%.3f)", mean(whole, 0.3, 0.45));
    Params quarter = p;
    quarter.sampleMaxBeats = 0.25f; // 0.125 s
    const auto cut = render(quarter, quiet);
    CHECK(mean(cut, 0.03, 0.1) > 0.4 && std::abs(mean(cut, 0.16, 0.45)) < 1.0e-3,
          "a quarter beat: it plays 0.125 s, then fades out (%.3f, then %.4f)", mean(cut, 0.03, 0.1), mean(cut, 0.16, 0.45));
    CHECK(mean(cut, 0.53, 0.6) > 0.4, "the next step's sound plays again");
}

void testSampleRender()
{
    std::puts("sampler render");
    Signal tone = sine(440.0, 0.5, 0.3f);
    std::vector<float> L(tone.begin(), tone.end()), R = L, outL, outR;
    auto peakOf = [](const std::vector<float>& v) { float p = 0.0f; for (float x : v) p = std::max(p, std::abs(x)); return p; };
    SampleFx plain;
    renderSample(L, R, kFs, plain, outL, outR);
    float shape = 0.0f;
    for (size_t i = 0; i < L.size(); ++i)
        shape = std::max(shape, std::abs(outL[i] - L[i] * (kSoundPeak / 0.3f)));
    CHECK(outL.size() == L.size() && std::abs(peakOf(outL) - kSoundPeak) < 1.0e-3f && shape < 1.0e-3f,
          "nothing on: the same clip, levelled to the sound peak (%.3f)", (double) peakOf(outL));
    SampleFx quieter = plain;
    quieter.volumeDb = -6.0f;
    renderSample(L, R, kFs, quieter, outL, outR);
    CHECK(std::abs(peakOf(outL) - kSoundPeak * 0.501f) < 2.0e-3f, "VOLUME -6 dB: half as loud as other sounds (%.3f)", (double) peakOf(outL));

    SampleFx crushed = plain;
    crushed.bitsKnob = 0.0f; // 1 bit
    renderSample(L, R, kFs, crushed, outL, outR);
    std::vector<float> distinct;
    for (float v : outL)
        if (std::none_of(distinct.begin(), distinct.end(), [v](float d) { return std::abs(d - v) < 1.0e-5f; }))
            distinct.push_back(v);
    CHECK(distinct.size() <= 2, "1 bit: two levels (%d)", (int) distinct.size());

    SampleFx held = plain;
    held.sampleRateKnob = 0.3f; // a few hundred Hz
    renderSample(L, R, kFs, held, outL, outR);
    int runs = 0;
    for (size_t i = 1; i < outL.size(); ++i)
        runs += outL[i] != outL[i - 1] ? 1 : 0;
    CHECK(runs < (int) outL.size() / 20, "a low sample rate holds the samples (%d changes)", runs);

    SampleFx driven = plain;
    driven.distType = 1;
    driven.drive = 1.0f;
    renderSample(L, R, kFs, driven, outL, outR);
    double crest = 0.0, sum = 0.0;
    for (float v : outL)
        sum += (double) v * v;
    crest = (double) peakOf(outL) / std::sqrt(sum / (double) outL.size());
    CHECK(crest < 1.3, "the overdrive squares the tone off (crest %.2f, a sine is 1.41)", crest);
    SampleFx fuzzed = driven;
    fuzzed.distType = 2;
    renderSample(L, R, kFs, fuzzed, outL, outR);
    CHECK(std::all_of(outL.begin(), outL.end(), [](float v) { return std::isfinite(v); }) && peakOf(outL) > 0.7f, "the fuzz works too");

    // the EQ: cutting at 440 Hz makes the tone quieter than a level-only render of a 2-tone clip would be
    Signal mix = sine(440.0, 0.5, 0.2f), other = sine(3000.0, 0.5, 0.2f);
    std::vector<float> M(mix.size());
    for (size_t i = 0; i < M.size(); ++i)
        M[i] = mix[i] + other[i];
    SampleFx eq = plain;
    eq.eqOn = true;
    eq.eqBands[3] = { true, EqType::Bell, 440.0f, -15.0f, 2.0f };
    std::vector<float> eqL, eqR, flatL, flatR;
    renderSample(M, M, kFs, eq, eqL, eqR);
    renderSample(M, M, kFs, plain, flatL, flatR);
    auto toneLevel = [](const std::vector<float>& v, double hz)
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 4800; i < v.size(); ++i)
        {
            re += v[i] * std::cos(6.283185307 * hz * (double) i / kFs);
            im += v[i] * std::sin(6.283185307 * hz * (double) i / kFs);
        }
        return std::sqrt(re * re + im * im);
    };
    const double ratioEq = toneLevel(eqL, 440.0) / toneLevel(eqL, 3000.0), ratioFlat = toneLevel(flatL, 440.0) / toneLevel(flatL, 3000.0);
    CHECK(ratioEq < 0.3 * ratioFlat, "a -15 dB bell at 440 Hz takes that tone down against the other (%.2f vs %.2f)", ratioEq, ratioFlat);

    // the sound before the EQ, for the sampler EQ's spectrum: with the EQ off it is the output itself (mono)
    std::vector<float> before;
    renderSample(M, M, kFs, plain, flatL, flatR, &before);
    float apart = 0.0f;
    for (size_t i = 0; i < before.size(); ++i)
        apart = std::max(apart, std::abs(before[i] - flatL[i]));
    CHECK(before.size() == flatL.size() && apart < 1.0e-5f, "the sound before the EQ is the output when the EQ is off");
    renderSample(M, M, kFs, eq, eqL, eqR, &before);
    CHECK(toneLevel(before, 440.0) > 3.0 * toneLevel(eqL, 440.0), "with the bell, the sound before the EQ still has the 440 Hz tone");
    std::vector<float> empty, eL, eR;
    renderSample(empty, empty, kFs, plain, eL, eR);
    CHECK(eL.empty() && eR.empty(), "an empty clip stays empty");
}

void testTriggerEvents()
{
    std::puts("trigger events: restart, next step, stutter, pick triggers off, steps on hits");
    const Signal silence(48000, 0.0f);

    Params once;
    once.seqPlay = 1;     // play once, so nothing runs until something triggers it
    once.seqMult = 0;
    once.tempoBpm = 600.0f; // 100 ms = 4800 samples per step
    once.steps.fill(mapping::pitchStepKnob(0));

    // Restart: the sequence starts exactly at the event and steps on with the tempo from there.
    auto trace = stepTrace(once, silence, { { 2000, TriggerAction::RestartSequence } });
    CHECK(trace[1999] == -1 && trace[2000] == 0, "restart starts the sequence at its sample (%d %d)", trace[1999], trace[2000]);
    const size_t toStep1 = firstIndexOf(trace, 1);
    CHECK(toStep1 == 2000 + 4799, "step 2 begins one step length after the restart (sample %zu)", toStep1);

    // Next step while idle starts the sequence; while running it moves on and restarts the step timer.
    trace = stepTrace(once, silence, { { 1000, TriggerAction::NextStep }, { 3000, TriggerAction::NextStep } });
    CHECK(trace[1000] == 0 && trace[2999] == 0 && trace[3000] == 1, "next step: start, then one step on (%d %d %d)",
          trace[1000], trace[2999], trace[3000]);
    CHECK(firstIndexOf(trace, 2) == 3000 + 4799, "next step restarts the step timer (step 3 at %zu)", firstIndexOf(trace, 2));

    // Steps on hits: the tempo no longer moves the sequence, only NextStep events do.
    Params hits = once;
    hits.seqPlay = 5;
    hits.stepsOnHits = true;
    trace = stepTrace(hits, silence, { { 10000, TriggerAction::NextStep }, { 30000, TriggerAction::NextStep } });
    CHECK(trace[9999] == 0 && trace[10000] == 1 && trace[29999] == 1 && trace[30000] == 2 && trace.back() == 2,
          "steps on hits: steps move only on events (%d %d %d %d %d)", trace[9999], trace[10000], trace[29999],
          trace[30000], trace.back());
    std::vector<std::pair<size_t, TriggerAction>> six;
    for (size_t k = 0; k < 6; ++k)
        six.push_back({ 1000 + k * 1000, TriggerAction::NextStep });
    trace = stepTrace(hits, silence, six);
    CHECK(trace[5999] == 5 && trace[6000] == 0, "steps on hits: wraps after the sixth step (%d %d)", trace[5999], trace[6000]);
    Params hitsOnce = hits;
    hitsOnce.seqPlay = 1;
    std::vector<std::pair<size_t, TriggerAction>> seven = six;
    seven.insert(seven.begin(), std::make_pair((size_t) 500, TriggerAction::NextStep)); // starts it, then six more steps
    trace = stepTrace(hitsOnce, silence, seven);
    CHECK(trace[5999] == 5 && trace[6000] == -1, "steps on hits, play once: stops after one pass (%d %d)", trace[5999], trace[6000]);

    // Sequencer off: trigger events do nothing to it.
    Params off = once;
    off.seqPlay = 0;
    trace = stepTrace(off, silence, { { 100, TriggerAction::RestartSequence }, { 200, TriggerAction::NextStep } });
    CHECK(firstIndexOf(trace, 0) == trace.size(), "sequencer off ignores trigger events");

    // Pick triggers off: a pick on the input no longer starts the sequence; events still do.
    const Signal pick = singleBurst(0.5);
    trace = stepTrace(once, pick, {});
    CHECK(firstIndexOf(trace, 0) < (size_t) (0.13 * kFs), "a pick starts the sequence by default");
    Params noPick = once;
    noPick.pickTriggers = false;
    trace = stepTrace(noPick, pick, {});
    CHECK(firstIndexOf(trace, 0) == trace.size(), "pick triggers off: the pick is ignored");
    trace = stepTrace(noPick, pick, { { 20000, TriggerAction::RestartSequence } });
    CHECK(firstIndexOf(trace, 0) == 20000, "pick triggers off: events still start the sequence");

    // Stutter events fire the stutter only; restart events never do.
    Params stut = once;
    stut.seqPlay = 0;
    stut.stutter = 4;           // full speed, four repeats
    stut.pickTriggers = false;  // the tone's own onset must not fire it
    const Signal tone = sine(220.0, 1.0, 0.3f);
    auto stutters = [&](const Params& p, TriggerAction a)
    {
        Engine e;
        e.prepare(kFs);
        Signal buf = tone;
        bool seen = false;
        for (size_t pos = 0; pos < buf.size(); pos += 480)
        {
            const TriggerEvent ev { 100, a };
            float* ch = buf.data() + pos;
            e.process(&ch, 1, 480, p, pos == 9600 ? &ev : nullptr, pos == 9600 ? 1 : 0);
            seen = seen || e.isStuttering();
        }
        return seen;
    };
    CHECK(stutters(stut, TriggerAction::Stutter), "a stutter event fires the stutter");
    CHECK(stutters(stut, TriggerAction::RestartAndStutter), "restart + stutter fires the stutter");
    CHECK(!stutters(stut, TriggerAction::RestartSequence), "a restart event does not stutter");

    // Events at the block edges: offset 0, the last sample, and past the end (applied at the end).
    for (int offset : { 0, 255, 400 })
    {
        Engine e;
        e.prepare(kFs);
        Signal buf(256, 0.0f);
        float* ch = buf.data();
        const TriggerEvent ev { offset, TriggerAction::RestartSequence };
        e.process(&ch, 1, 256, once, &ev, 1);
        CHECK(e.getCurrentStep() == 0, "event at offset %d runs within the block", offset);
    }

    // No events and pick triggers on: exactly the behaviour without the drum map.
    const Signal burst = singleBurst(1.0);
    Params def = once;
    def.stutter = 3;
    Engine a, b;
    a.prepare(kFs);
    b.prepare(kFs);
    Signal outA = burst, outB = burst;
    for (size_t pos = 0; pos < burst.size(); pos += 256)
    {
        const int n = (int) std::min<size_t>(256, burst.size() - pos);
        float* ca = outA.data() + pos;
        float* cb = outB.data() + pos;
        a.process(&ca, 1, n, def);
        b.process(&cb, 1, n, def, nullptr, 0);
    }
    CHECK(maxAbsDiff(outA, outB) == 0.0, "no events changes nothing");
}

DrumHit makeHit(double t, Drum d, float level = 0.0f, float confidence = 1.0f)
{
    DrumHit h;
    h.time = t;
    h.drum = d;
    h.levelDb = level;
    h.confidence = confidence;
    return h;
}

void testDrumScheduler()
{
    std::puts("drum map: levels, thresholds and the loop scheduler");

    // normaliseLevels: relative to the loudest hit of each drum, sorted by time.
    std::vector<DrumHit> raw { makeHit(0.5, Drum::Snare, -10.0f), makeHit(0.1, Drum::Kick, -3.0f),
                               makeHit(0.3, Drum::Kick, -9.0f), makeHit(0.7, Drum::Snare, -4.0f),
                               makeHit(0.9, Drum::HiHat, -80.0f, 2.0f) };
    normaliseLevels(raw);
    CHECK(raw[0].time == 0.1 && raw[0].levelDb == 0.0f && raw[1].levelDb == -6.0f, "kick levels %g %g",
          (double) raw[0].levelDb, (double) raw[1].levelDb);
    CHECK(raw[2].levelDb == -6.0f && raw[3].levelDb == 0.0f, "snare levels %g %g", (double) raw[2].levelDb, (double) raw[3].levelDb);
    CHECK(raw[4].levelDb == 0.0f && raw[4].confidence == 1.0f, "a lone hit is its drum's loudest; confidence clamped");

    // Thresholds.
    DrumSettings s;
    s.action = DrumAction::RestartSequence;
    s.minLevelDb = -6.0f;
    s.minConfidence = 0.5f;
    CHECK(hitFires(makeHit(0, Drum::Kick, -6.0f, 0.5f), s), "a hit exactly at both thresholds fires");
    CHECK(!hitFires(makeHit(0, Drum::Kick, -6.1f, 0.9f), s), "a quieter hit does not fire");
    CHECK(!hitFires(makeHit(0, Drum::Kick, 0.0f, 0.49f), s), "a less certain hit does not fire");
    DrumHit excluded = makeHit(0, Drum::Kick);
    excluded.excluded = true;
    CHECK(!hitFires(excluded, s) && hitDetected(excluded, s), "an excluded hit is shown but never fires");
    s.action = DrumAction::Off;
    CHECK(!hitFires(makeHit(0, Drum::Kick), s), "a drum set to Off never fires");

    // The scheduler over three loops, with block sizes from 1 sample to more than a loop.
    constexpr int64_t loop = 10000;
    const std::vector<DrumHit> hits { makeHit(0, Drum::Kick, 0.0f),      makeHit(0, Drum::Snare, -20.0f),
                                      makeHit(0, Drum::Snare, 0.0f),     makeHit(0, Drum::HiHat, 0.0f),
                                      makeHit(0, Drum::Kick, -3.0f, 0.2f), makeHit(0, Drum::Cymbal, -1.0f) };
    const std::vector<int64_t> at { 0, 1000, 1000, 5000, 7000, 9999 };
    DrumSettingsArray settings {};
    settings[(size_t) Drum::Kick] = { DrumAction::RestartSequence, -12.0f, 0.3f };
    settings[(size_t) Drum::Snare] = { DrumAction::Stutter, -12.0f, 0.3f };
    settings[(size_t) Drum::Cymbal] = { DrumAction::NextStep, -12.0f, 0.3f };
    // hi-hat stays Off. Should fire: 0 (kick), 2 (loud snare), 5 (cymbal). Not: 1 (quiet), 3 (off), 4 (unsure).
    const std::vector<int> shouldFire { 0, 2, 5 };

    for (int block : { 1, 7, 64, 333, 4096, 25000 })
    {
        std::vector<std::pair<int, int64_t>> fired; // hit, absolute sample
        bool eventsOk = true;
        int64_t position = 0, absolute = 0;
        while (absolute < 3 * loop)
        {
            const int n = (int) std::min<int64_t>(block, 3 * loop - absolute);
            TriggerEvent events[16];
            std::vector<std::pair<int, int>> calls;
            const int count = DrumScheduler::collect(hits.data(), at.data(), (int) hits.size(), loop, position, n,
                                                     settings, events, 16,
                                                     [&](int hit, int offset) { calls.push_back({ hit, offset }); });
            eventsOk = eventsOk && count == (int) calls.size();
            for (int k = 0; k < count; ++k)
            {
                const auto& h = hits[(size_t) calls[(size_t) k].first];
                eventsOk = eventsOk && events[k].offset == calls[(size_t) k].second && events[k].offset >= 0 &&
                           events[k].offset < n && (k == 0 || events[k].offset >= events[k - 1].offset) &&
                           events[k].action == toTriggerAction(settings[(size_t) h.drum].action);
            }
            for (const auto& c : calls)
                fired.push_back({ c.first, absolute + c.second });
            absolute += n;
            position = (position + n) % loop;
        }
        std::vector<std::pair<int, int64_t>> expected;
        for (int64_t l = 0; l < 3; ++l)
            for (int h : shouldFire)
                expected.push_back({ h, l * loop + at[(size_t) h] });
        CHECK(fired == expected, "block %d: %d hits fired, expected %d at their exact samples", block, (int) fired.size(),
              (int) expected.size());
        CHECK(eventsOk, "block %d: events match the fired hits, in order, with the right actions", block);
    }

    // A full event buffer still reports every fired hit.
    int calls = 0;
    TriggerEvent one[1];
    const int written = DrumScheduler::collect(hits.data(), at.data(), (int) hits.size(), loop, 0, 2000, settings, one, 1,
                                               [&](int, int) { ++calls; });
    CHECK(written == 1 && calls == 2, "full event buffer: %d written, %d reported", written, calls);

    // Nothing to do: no hits, empty block, zero-length loop.
    TriggerEvent buf[4];
    CHECK(DrumScheduler::collect(hits.data(), at.data(), 0, loop, 0, 512, settings, buf, 4) == 0 &&
              DrumScheduler::collect(hits.data(), at.data(), (int) hits.size(), loop, 0, 0, settings, buf, 4) == 0 &&
              DrumScheduler::collect(hits.data(), at.data(), (int) hits.size(), 0, 0, 512, settings, buf, 4) == 0,
          "degenerate inputs produce no events");

    // Drum ids round-trip.
    bool idsOk = true;
    for (int i = 0; i < kNumDrums; ++i)
    {
        Drum d = Drum::Kick;
        idsOk = idsOk && drumFromId(drumId((Drum) i), d) && d == (Drum) i;
    }
    Drum unused = Drum::Kick;
    CHECK(idsOk && !drumFromId("cowbell", unused), "drum ids round-trip");
}

void testTempoEstimate()
{
    std::puts("drum map: tempo of a loop from its hits");
    auto groove = [](double bpm, int bars, double jitterMs, bool halfTimeFeel)
    {
        std::vector<DrumHit> h;
        const double beat = 60.0 / bpm;
        Rng rng(11);
        auto j = [&] { return rng.next() * jitterMs * 0.001; };
        for (int b = 0; b < bars * 4; ++b)
        {
            const double t = b * beat;
            if (b % 4 == 0 || (!halfTimeFeel && b % 4 == 2))
                h.push_back(makeHit(t + j(), Drum::Kick, -1.0f));
            if (halfTimeFeel ? b % 4 == 2 : b % 2 == 1)
                h.push_back(makeHit(t + j(), Drum::Snare, 0.0f));
            h.push_back(makeHit(t + j() + (t == 0 ? 0.001 : 0.0), Drum::HiHat, -4.0f));
            h.push_back(makeHit(t + beat / 2 + j(), Drum::HiHat, -10.0f));
        }
        return std::make_pair(h, bars * 4 * beat);
    };
    for (auto [bpm, bars, jitter, half] : { std::tuple { 97.0, 2, 0.0, false }, std::tuple { 140.0, 4, 6.0, false },
                                            std::tuple { 72.0, 2, 4.0, false }, std::tuple { 174.0, 8, 3.0, true },
                                            std::tuple { 120.0, 1, 0.0, false } })
    {
        auto [h, duration] = groove(bpm, bars, jitter, half);
        std::sort(h.begin(), h.end(), [](const DrumHit& a, const DrumHit& b) { return a.time < b.time; });
        const double est = estimateTempo(h, duration);
        // A half-time groove is honestly ambiguous (174 is often counted as 87): either octave is fine
        // there, and the DRUMS tab has x2 / /2 buttons for it.
        const bool ok = std::abs(est - bpm) < 0.5 || (half && std::abs(est - bpm / 2.0) < 0.5);
        CHECK(ok, "%g BPM, %d bars, %g ms jitter: estimated %g", bpm, bars, jitter, est);
    }
    CHECK(estimateTempo({}, 4.0) == 0.0 && estimateTempo({ makeHit(0.1, Drum::Kick) }, 4.0) == 0.0,
          "too few hits gives no tempo");
}

void testFft()
{
    std::puts("fft: matches a direct DFT");
    for (size_t n : { (size_t) 8, (size_t) 64, (size_t) 2048 })
    {
        Fft fft(n);
        Rng rng(5);
        std::vector<std::complex<double>> x(n), X;
        for (auto& v : x)
            v = { rng.next(), rng.next() };
        X = x;
        fft.forward(X);
        double worst = 0.0;
        for (size_t k = 0; k < n; k += std::max<size_t>(1, n / 16))
        {
            std::complex<double> acc;
            for (size_t i = 0; i < n; ++i)
                acc += x[i] * std::polar(1.0, -2.0 * 3.14159265358979323846 * (double) (k * i % n) / (double) n);
            worst = std::max(worst, std::abs(acc - X[k]));
        }
        CHECK(worst < 1.0e-9 * (double) n, "size %zu: max error %g", n, worst);
    }
}

void testDrumAnalysis()
{
    std::puts("drum analysis: the synthetic demo beat is transcribed, timed and levelled");
    constexpr double bpm = 100.0, beat = 60.0 / bpm, start = 0.05;
    const demo::Beat demoBeat = demo::renderDemoBeat(kFs, bpm, 4);
    const Signal& audio = demoBeat.audio;
    std::array<std::vector<double>, kNumDrums> truth;
    for (const auto& n : demoBeat.notes)
        truth[(size_t) n.drum].push_back(n.time);
    for (auto& v : truth)
        std::sort(v.begin(), v.end());

    const auto result = drums::analyse(audio.data(), audio.size(), kFs);
    if (gVerbose)
        for (int d = 0; d < kNumDrums; ++d)
        {
            std::printf("  %s truth:", drumId((Drum) d));
            for (double t : truth[(size_t) d])
                std::printf(" %.3f", t);
            std::printf("\n  %s found:", drumId((Drum) d));
            for (const auto& h : result.hits)
                if ((int) h.drum == d && h.confidence >= 0.1f)
                    std::printf(" %.3f(%.2f,%.0f)", h.time, (double) h.confidence, (double) h.levelDb);
            std::printf("\n");
        }
    CHECK(std::abs(result.durationSeconds - (double) audio.size() / kFs) < 1e-9, "duration");

    // One-to-one matching within 30 ms: true positives, reported hits, worst timing error.
    struct Score
    {
        size_t tp = 0, reported = 0;
        double worst = 0.0;
    };
    auto score = [&](Drum d, const std::vector<double>& ref)
    {
        Score sc;
        std::vector<double> est;
        for (const auto& h : result.hits)
            if (h.drum == d && h.confidence >= 0.3f)
                est.push_back(h.time);
        sc.reported = est.size();
        std::vector<bool> used(est.size(), false);
        for (double r : ref)
        {
            long best = -1;
            for (size_t i = 0; i < est.size(); ++i)
                if (!used[i] && std::abs(est[i] - r) < 0.03 && (best < 0 || std::abs(est[i] - r) < std::abs(est[(size_t) best] - r)))
                    best = (long) i;
            if (best >= 0)
            {
                used[(size_t) best] = true;
                ++sc.tp;
                sc.worst = std::max(sc.worst, std::abs(est[(size_t) best] - r));
            }
        }
        return sc;
    };

    // A regression floor for the built-in analysis on this kit (its accuracy on real recordings is
    // measured with bitvibe_drum_eval; see the README).
    // Kick, snare and tom: every hit found within 6 ms, at most one extra.
    for (Drum d : { Drum::Kick, Drum::Snare, Drum::Tom })
    {
        const Score sc = score(d, truth[(size_t) d]);
        CHECK(sc.tp == truth[(size_t) d].size() && sc.reported <= sc.tp + 1, "%s: %zu of %zu found, %zu reported",
              drumName(d), sc.tp, truth[(size_t) d].size(), sc.reported);
        CHECK(sc.worst < 0.006, "%s: worst timing error %.1f ms", drumName(d), sc.worst * 1000.0);
    }

    // Hi-hats: at least half of the loud ones. Known weak spots of the built-in analysis (the neural
    // helper does better): a hat on the same beat as a kick, quiet hats right after louder hits, a crash
    // taken for a hat and a snare leaking into the hat (allowed below as up to four "false" hats).
    std::vector<double> loudHats;
    for (const auto& n : demoBeat.notes)
        if (n.drum == Drum::HiHat && n.gain >= 0.4f)
            loudHats.push_back(n.time);
    std::sort(loudHats.begin(), loudHats.end());
    const Score loud = score(Drum::HiHat, loudHats), all = score(Drum::HiHat, truth[(size_t) Drum::HiHat]);
    CHECK((double) loud.tp >= 0.5 * (double) loudHats.size(), "hi-hat: %zu of %zu loud hats found", loud.tp, loudHats.size());
    CHECK(all.reported <= all.tp + 4 && all.worst < 0.006, "hi-hat: %zu reported, %zu of them real, worst %.1f ms",
          all.reported, all.tp, all.worst * 1000.0);

    // Cymbals: nothing reported that is not a cymbal.
    const Score cym = score(Drum::Cymbal, truth[(size_t) Drum::Cymbal]);
    CHECK(cym.reported == cym.tp, "cymbal: %zu reported, %zu real", cym.reported, cym.tp);

    // Levels: the loud snares come out louder than the quiet one; loud hi-hats louder than the off-beats.
    auto levelAt = [&](double t, Drum d)
    {
        float best = -1000.0f, dist = 1.0f;
        for (const auto& h : result.hits)
            if (h.drum == d && std::abs(h.time - t) < dist)
            {
                dist = (float) std::abs(h.time - t);
                best = h.levelDb;
            }
        return dist < 0.03f ? best : -1000.0f;
    };
    const float loudSnare = levelAt(start + 1 * beat, Drum::Snare), quietSnare = levelAt(start + 7 * beat, Drum::Snare);
    CHECK(loudSnare > quietSnare + 3.0f, "accented snare %.1f dB vs quiet %.1f dB", (double) loudSnare, (double) quietSnare);
    const float loudHat = levelAt(start + 4.5 * beat, Drum::HiHat), quietHat = levelAt(start + 5.5 * beat, Drum::HiHat);
    CHECK(loudHat > quietHat + 6.0f, "loud off-beat hi-hat %.1f dB vs quiet %.1f dB", (double) loudHat, (double) quietHat);
    bool levelsInRange = true;
    for (const auto& h : result.hits)
        levelsInRange = levelsInRange && h.levelDb <= 0.0f && h.levelDb >= kMinLevelFloorDb && h.confidence >= 0.0f && h.confidence <= 1.0f;
    CHECK(levelsInRange, "levels are 0 dB or below and confidences within 0..1");
    CHECK(std::abs(result.tempoBpm - bpm) < 1.0 || std::abs(result.tempoBpm - 2.0 * bpm) < 2.0,
          "tempo %.2f BPM (expected %g)", result.tempoBpm, bpm);

    // measureLevels gives hits from another detector the same scale.
    std::vector<DrumHit> external;
    for (const auto& n : demoBeat.notes)
        if (n.drum == Drum::Snare)
            external.push_back(makeHit(n.time, Drum::Snare, 0.0f, 0.9f));
    drums::measureLevels(external, audio.data(), audio.size(), kFs);
    normaliseLevels(external);
    float quietest = 0.0f;
    for (const auto& h : external)
        quietest = std::min(quietest, h.levelDb);
    CHECK(quietest < -3.0f, "measured levels separate the quiet snare (%.1f dB)", (double) quietest);

    // Silence, a very short file and pure noise do not crash or invent a beat.
    const Signal quiet(48000, 0.0f);
    CHECK(drums::analyse(quiet.data(), quiet.size(), kFs).hits.empty(), "silence has no hits");
    CHECK(drums::analyse(quiet.data(), 100, kFs).hits.empty(), "a tiny file has no hits");

    // A noise floor 40 dB down does not add hits or lose the kick and snare.
    Signal noisy = audio;
    const Signal floorNoise = noise((double) audio.size() / kFs, 0.008f, 9);
    for (size_t i = 0; i < noisy.size(); ++i)
        noisy[i] += floorNoise[i];
    const auto noisyResult = drums::analyse(noisy.data(), noisy.size(), kFs);
    for (Drum d : { Drum::Kick, Drum::Snare })
    {
        size_t found = 0, reported = 0;
        for (const auto& h : noisyResult.hits)
            if (h.drum == d && h.confidence >= 0.3f)
            {
                ++reported;
                for (double r : truth[(size_t) d])
                    found += std::abs(h.time - r) < 0.03;
            }
        CHECK(found == truth[(size_t) d].size() && reported == found, "with a noise floor, %s: %zu of %zu found, %zu reported",
              drumName(d), found, truth[(size_t) d].size(), reported);
    }
}

// ---- where the beat's sound goes ---------------------------------------------------------------------

void testBeatRoute()
{
    std::puts("beat routing: which output carries the beat's sound");
    using M = BeatOutput;
    using R = BeatRoute;

    // Auto: the Beat Monitor output when it is connected, otherwise the main output (never silent).
    CHECK(chooseBeatRoute(M::Auto, true) == R::Monitor, "auto, monitor connected -> monitor");
    CHECK(chooseBeatRoute(M::Auto, false) == R::Main, "auto, no monitor -> main (not silent)");
    // Monitor: only that output, so silent when it is not connected.
    CHECK(chooseBeatRoute(M::Monitor, true) == R::Monitor, "monitor, connected -> monitor");
    CHECK(chooseBeatRoute(M::Monitor, false) == R::Silent, "monitor, not connected -> silent");
    // Main: always the main output, even with a monitor output available.
    CHECK(chooseBeatRoute(M::Main, true) == R::Main, "main, monitor connected -> still main");
    CHECK(chooseBeatRoute(M::Main, false) == R::Main, "main, no monitor -> main");

    // The parameter index is clamped, so a damaged or future value cannot select something undefined.
    CHECK(beatOutputFromIndex(0) == M::Auto && beatOutputFromIndex(1) == M::Monitor && beatOutputFromIndex(2) == M::Main,
          "indices 0, 1, 2 are Auto, Monitor, Main");
    CHECK(beatOutputFromIndex(-4) == M::Auto && beatOutputFromIndex(99) == M::Main, "out-of-range indices are clamped");
}

// ---- beat pattern: fires an action at chosen steps of a bar, from the plug-in's own tempo --------------

// Times (in samples) at which the sequencer restarts at step 0, running the engine one sample at a time.
std::vector<size_t> patternRestarts(const Params& base, double seconds, const std::function<void(Params&, size_t)>& automate = {})
{
    Engine e;
    e.prepare(kFs);
    Params p = base;
    std::vector<size_t> times;
    int previous = -1;
    float sample = 0.0f;
    float* channel = &sample;
    const size_t total = (size_t) (seconds * kFs);
    for (size_t i = 0; i < total; ++i)
    {
        sample = 0.0f;
        if (automate)
            automate(p, i);
        e.process(&channel, 1, 1, p);
        const int step = e.getCurrentStep();
        if (step == 0 && previous != 0)
            times.push_back(i);
        previous = step;
    }
    return times;
}

void testBeatPattern()
{
    std::puts("beat pattern: time signatures, presets, random, and the pattern clock");

    // The ten time signatures: their bars add up, and fit the biggest grid.
    {
        bool consistent = true;
        for (int i = 0; i < kNumTimeSignatures; ++i)
        {
            const auto& ts = timeSignature(i);
            int sum = 0;
            for (int g = 0; g < ts.numGroups; ++g)
                sum += ts.groupSteps[(size_t) g];
            consistent = consistent && sum == ts.totalSteps && ts.totalSteps <= kMaxPatternSteps && ts.numGroups >= 1 && ts.numGroups <= 5;
        }
        CHECK(consistent, "every time signature's beats add up to its bar, and fit the grid");
        CHECK(timeSignature(0).totalSteps == 16 && timeSignature(1).totalSteps == 12 && timeSignature(3).totalSteps == 12
                  && timeSignature(5).totalSteps == 14 && timeSignature(6).totalSteps == 24 && timeSignature(9).totalSteps == 6,
              "4/4 has 16 steps, 3/4 has 12, 6/8 has 12, 7/8 has 14, 12/8 has 24, 3/8 has 6");
    }

    // The preset buttons follow the signature: first step of every beat, the middle of every beat, the backbeat.
    CHECK(patternPreset(0) == 0u && patternPreset(1) == 0x1111u && patternPreset(2) == 0x4444u && patternPreset(3) == 0x1010u,
          "4/4: clear, beats, the 'and' of every beat, the backbeat");
    CHECK(patternPreset(1, 1) == 0x111u && patternPreset(2, 1) == 0x444u && patternPreset(3, 1) == 0x010u, "3/4 presets");
    CHECK(patternPreset(1, 3) == 0x041u && patternPreset(2, 3) == 0x208u && patternPreset(3, 3) == 0x040u, "6/8 presets (two beats of six steps)");
    CHECK(patternPreset(1, 5) == 0x111u && patternPreset(3, 5) == 0x010u, "7/8 counts 2 + 2 + 3 eighths: beats at steps 0, 4, 8");
    CHECK(patternPreset(3, 9) == 0x1u, "a one-beat bar has its backbeat on the beat");

    // RANDOM: only the chosen beats change, the rest keep their steps, and the same seed gives the same pattern.
    for (int signature : { 0, 3, 6 })
    {
        const auto& ts = timeSignature(signature);
        bool onlyChosen = true, deterministic = true, neverEmpty = true, varied = false, inRange = true;
        const uint32_t base = 0xA5A5A5u & ((1u << ts.totalSteps) - 1u);
        const uint32_t chosen = 0x2u; // beat 2
        const uint32_t beatBits = ((1u << ts.groupSteps[1]) - 1u) << patternGroupStart(ts, 1);
        const uint32_t first = patternRandom(base, chosen, 1u, signature);
        for (uint32_t seed = 1; seed <= 200; ++seed)
        {
            const uint32_t r = patternRandom(base, chosen, seed, signature);
            onlyChosen = onlyChosen && (r & ~beatBits) == (base & ~beatBits);
            deterministic = deterministic && r == patternRandom(base, chosen, seed, signature);
            neverEmpty = neverEmpty && (r & beatBits) != 0u;
            inRange = inRange && r < (1u << ts.totalSteps);
            varied = varied || r != first;
        }
        CHECK(onlyChosen && inRange, "%s: random leaves the beats that are not chosen alone", ts.name);
        CHECK(deterministic && neverEmpty && varied, "%s: random is repeatable per seed, varied between seeds, never empty", ts.name);
    }
    CHECK(patternRandom(0x1234u, 0u, 7u) == 0x1234u, "random with no beat chosen changes nothing");
    {
        int firstStepLit = 0, inBetweenLit = 0;
        for (uint32_t seed = 1; seed <= 400; ++seed)
        {
            const uint32_t r = patternRandom(0u, 0x1Fu, seed);
            firstStepLit += (int) (r & 1u);
            inBetweenLit += (int) ((r >> 1) & 1u);
        }
        CHECK(firstStepLit > inBetweenLit, "random: the first step of a beat fires more often than the second (%d vs %d)", firstStepLit, inBetweenLit);
    }

    // The clock in the engine: 120 BPM is a 6000-sample step. A one-bar pattern lighting only step 0 of 4/4 restarts
    // the sequence at the start and every 2 s after (96000 samples).
    auto pattern = [](uint32_t steps, int barSteps)
    {
        Params p = neutral();
        p.seqPlay = 5; // loops, so a restart is visible as the step going back to 0
        p.seqMult = 0;
        p.tempoBpm = 120.0f;
        p.steps.fill(mapping::pitchStepKnob(0));
        p.pickTriggers = false;
        p.patternOn = true;
        p.patternAction = TriggerAction::RestartSequence;
        p.patternSteps = steps;
        p.patternBarSteps = barSteps;
        return p;
    };
    {
        // seqPlay "loop" keeps stepping 0..5 every 0.5 s, so detect restarts as steps going to 0 off the beat of the loop:
        // use a once-only sequence instead (it stops after 3 s) and look at a 5 s window.
        Params p = pattern(0x0001u, 16);
        p.seqPlay = 1;
        const auto t = patternRestarts(p, 5.0);
        CHECK(t.size() == 3 && t[0] == 0 && t[1] == 96000 && t[2] == 192000, "4/4, step 1 only: a restart at the start and every bar (%zu found)", t.size());

        Params q = pattern(0x0010u, 16); // step 5 (the second beat's first step)
        q.seqPlay = 1;
        const auto u = patternRestarts(q, 5.0);
        CHECK(u.size() == 3 && u[0] == 24000 && u[1] == 24000 + 96000 && u[2] == 24000 + 192000, "4/4, the second beat: 0.5 s into every bar");

        Params w = pattern(0x0001u, 12); // 3/4: a bar is 12 steps = 72000 samples
        w.seqPlay = 1;
        const auto v = patternRestarts(w, 4.0);
        CHECK(v.size() == 3 && v[1] == 72000 && v[2] == 144000, "3/4: the bar is 1.5 s long");

        Params x = pattern(0x0001u, 14); // 7/8: 14 steps
        x.seqPlay = 1;
        const auto y = patternRestarts(x, 4.0);
        CHECK(y.size() == 3 && y[1] == 84000 && y[2] == 168000, "7/8: the bar is 1.75 s long");

        Params off = pattern(0x0001u, 16);
        off.seqPlay = 1;
        off.patternOn = false;
        CHECK(patternRestarts(off, 3.0).empty(), "pattern off: nothing restarts");
        Params none = pattern(0x0000u, 16);
        none.seqPlay = 1;
        CHECK(patternRestarts(none, 3.0).empty(), "no step lit: nothing restarts");

        // The tempo moves the grid: doubling it halves the bar.
        Params fast = pattern(0x0001u, 16);
        fast.seqPlay = 1;
        fast.tempoBpm = 240.0f;
        const auto f = patternRestarts(fast, 3.0);
        CHECK(f.size() >= 3 && f[1] == 48000 && f[2] == 96000, "twice the tempo, half the bar");
    }

    // The clock runs whether or not the pattern is on, and the engine tells the editor where it is (for the highlight).
    {
        Params p = pattern(0u, 16);
        p.patternOn = false;
        Engine e;
        e.prepare(kFs);
        std::vector<float> block(50000, 0.0f);
        float* ch = block.data();
        e.process(&ch, 1, 50000, p);
        CHECK(e.getPatternStep() == 8, "after 50000 samples at 120 BPM the bar is at step 8 (%d)", e.getPatternStep());
        p.patternBarSteps = 6; // a shorter bar: the position wraps into it
        float* ch2 = block.data();
        e.process(&ch2, 1, 6000, p);
        CHECK(e.getPatternStep() >= 0 && e.getPatternStep() < 6, "a shorter bar wraps the position (%d)", e.getPatternStep());
    }

    // Pattern lock: the level of the input (a pick) and drum hits no longer restart the sequence; only the pattern does.
    {
        const Signal in = quietToneWithPick(); // a pick at 0.5 s
        auto firstStep = [&](const Params& base, const std::vector<TriggerEvent>& events = {})
        {
            Engine e;
            e.prepare(kFs);
            Params p = base;
            Signal out = in;
            size_t first = out.size();
            bool stuttered = false;
            for (size_t i = 0; i < out.size(); ++i)
            {
                float* ch = out.data() + i;
                std::vector<TriggerEvent> local;
                for (const auto& ev : events)
                    if (i == 7000)
                        local.push_back({ 0, ev.action });
                e.process(&ch, 1, 1, p, local.data(), (int) local.size());
                if (e.getCurrentStep() >= 0 && first == out.size())
                    first = i;
                stuttered = stuttered || e.isStuttering();
            }
            return std::pair<size_t, bool> { first, stuttered };
        };
        Params base = pattern(0u, 16);
        base.seqPlay = 1;
        base.patternOn = false;
        base.pickTriggers = true;
        base.stutter = 2;
        const auto picked = firstStep(base);
        CHECK(picked.first >= 24000 && picked.first < 26000 && picked.second, "no lock: a picked note starts the sequence and the stutter (sequence at %zu)", picked.first);

        Params locked = base;
        locked.patternLock = true;
        const auto lockedPick = firstStep(locked);
        CHECK(lockedPick.first == in.size() && lockedPick.second, "lock on: the pick no longer starts the sequence, but still fires the stutter");

        Params lockedPattern = pattern(0x0002u, 16); // the pattern lights step 2 (0.125 s into the bar)
        lockedPattern.seqPlay = 1;
        lockedPattern.pickTriggers = true;
        lockedPattern.patternLock = true;
        const auto fromPattern = firstStep(lockedPattern);
        CHECK(fromPattern.first >= 5990 && fromPattern.first <= 6010, "lock on: the sequence starts when the pattern says so, not at the pick (%zu)", fromPattern.first);

        const auto restartEvent = firstStep(locked, { { 0, TriggerAction::RestartSequence } });
        CHECK(restartEvent.first == in.size(), "lock on: a restart from a drum hit is ignored");
        Params unlocked = base;
        unlocked.pickTriggers = false;
        const auto eventOnly = firstStep(unlocked, { { 0, TriggerAction::RestartSequence } });
        CHECK(eventOnly.first >= 7000 && eventOnly.first < 7010, "lock off: the same drum hit restarts the sequence (%zu)", eventOnly.first);
    }

    // Restart the bar: the clock goes back to the first step at once, and fires it.
    {
        Params p = pattern(0x0001u, 16);
        p.seqPlay = 1;
        Engine e;
        e.prepare(kFs);
        float sample = 0.0f;
        float* ch = &sample;
        for (int i = 0; i < 30001; ++i)
        {
            sample = 0.0f;
            e.process(&ch, 1, 1, p);
        }
        const int before = e.getPatternStep(), sequenceStep = e.getCurrentStep();
        e.restartPatternBar();
        sample = 0.0f;
        e.process(&ch, 1, 1, p);
        CHECK(before == 5 && sequenceStep > 0 && e.getPatternStep() == 0 && e.getCurrentStep() == 0,
              "restart bar: the clock goes from step %d back to step 1 and the sequence restarts (%d -> %d)", before + 1, sequenceStep, e.getCurrentStep());
    }

    // Following the host: at 1 quarter note (4 steps) in, a pattern with step 6 lit (bit 5) fires one step later (6000 samples).
    {
        Params p = pattern(0x0020u, 16);
        p.seqPlay = 1;
        p.hostPpq = 1.0;
        Engine e;
        e.prepare(kFs);
        int previous = -1;
        std::vector<size_t> restarts;
        float sample = 0.0f;
        float* ch = &sample;
        for (size_t i = 0; i < 20000; ++i)
        {
            sample = 0.0f;
            // the host reports its position at each block start: here every 100 samples
            p.hostPpq = i % 100 == 0 ? 1.0 + (double) i / 24000.0 : -1.0;
            e.process(&ch, 1, 1, p);
            const int step = e.getCurrentStep();
            if (step == 0 && previous != 0)
                restarts.push_back(i);
            previous = step;
        }
        CHECK(restarts.size() == 1 && restarts[0] >= 5990 && restarts[0] <= 6010, "following the host: the lit step arrives when the host reaches it (%zu at %zu)",
              restarts.size(), restarts.empty() ? (size_t) 0 : restarts[0]);
    }
}

// REPLAY: the clean signal alone while the player plays; when they stop, the last LOOK BACK of what they played loops
// (after the WAIT) through the sequencer until they play again or LENGTH runs out.
void testReplay()
{
    std::puts("replay");
    // a phrase: 440 Hz for 1 s, silence, 660 Hz from 4 to 5 s, silence
    Signal in((size_t) (7.0 * kFs), 0.0f);
    for (size_t i = 0; i < in.size(); ++i)
    {
        const double t = (double) i / kFs;
        if (t < 1.0)
            in[i] = 0.3f * (float) std::sin(2.0 * 3.14159265358979 * 440.0 * t);
        else if (t >= 4.0 && t < 5.0)
            in[i] = 0.3f * (float) std::sin(2.0 * 3.14159265358979 * 660.0 * (t - 4.0));
    }
    Params p = neutral();
    p.replay.on = true;
    p.replay.lookBackSeconds = 0.4567f; // not a whole number of cycles: the seam has to be crossfaded
    p.replay.waitSeconds = 0.3f;
    auto maxDiff = [&](const Signal& out, double t0, double t1)
    {
        double d = 0.0;
        for (size_t i = (size_t) (t0 * kFs); i < (size_t) (t1 * kFs); ++i)
            d = std::max(d, (double) std::abs(out[i] - in[i]));
        return d;
    };
    Signal out = render(p, in);
    CHECK(maxDiff(out, 0.0, 1.0) < 1.0e-6, "while the player plays, only the clean signal comes out (diff %g)", maxDiff(out, 0.0, 1.0));
    CHECK(rms(out, 1.01, 1.29) < 1.0e-5, "during the WAIT, silence (rms %g)", rms(out, 1.01, 1.29));
    CHECK(std::abs(db(rms(out, 1.4, 3.9) / rms(in, 0.2, 0.9))) < 1.5, "then the loop plays at the level it was played (%g dB)",
          db(rms(out, 1.4, 3.9) / rms(in, 0.2, 0.9)));
    CHECK(tonePower(out, 1.4, 3.9, 440.0) > 1000.0 * tonePower(out, 1.4, 3.9, 660.0), "the loop is the phrase just played (440 Hz)");
    double step = 0.0;
    for (size_t i = (size_t) (1.35 * kFs); i < (size_t) (3.9 * kFs); ++i)
        step = std::max(step, (double) std::abs(out[i] - out[i - 1]));
    CHECK(step < 0.03, "the loop wraps without a click (largest step %g, a 440 Hz sine moves up to 0.017)", step);
    CHECK(maxDiff(out, 4.03, 5.0) < 1.0e-3, "playing again cuts the loop at once: the clean signal only (diff %g)", maxDiff(out, 4.03, 5.0));
    CHECK(tonePower(out, 5.4, 6.9, 660.0) > 1000.0 * tonePower(out, 5.4, 6.9, 440.0), "and the next stop loops the new phrase (660 Hz)");

    // LENGTH: the loop fades out after it, and stays quiet until the player plays again
    Params timed = p;
    timed.replay.lengthSeconds = 1.0f;
    out = render(timed, in);
    CHECK(rms(out, 1.4, 2.2) > 0.1 && rms(out, 2.9, 3.9) < 1.0e-5, "LENGTH 1 s: the loop plays, then stops (rms %g then %g)", rms(out, 1.4, 2.2),
          rms(out, 2.9, 3.9));

    // the sequence plays with the loop, and only with it; DRY brings the clean loop back
    Params seq = p;
    seq.seqType = SeqType::Pitch;
    seq.seqPlay = 5; // continuous
    seq.tempoBpm = 600.0f;
    seq.seqMult = 0;
    seq.steps.fill(mapping::pitchStepKnob(12));
    {
        Engine e;
        e.prepare(kFs);
        Signal o = in;
        int stepWhilePlaying = -2, stepWhileLooping = -2;
        ReplayLooper::State whilePlaying = ReplayLooper::State::Off, whileLooping = ReplayLooper::State::Off;
        for (size_t pos = 0; pos < in.size(); pos += 256)
        {
            const int n = (int) std::min<size_t>(256, in.size() - pos);
            float* ch = o.data() + pos;
            e.process(&ch, 1, n, seq);
            if (pos <= (size_t) (0.5 * kFs) && (size_t) (0.5 * kFs) < pos + (size_t) n)
            {
                stepWhilePlaying = e.getCurrentStep();
                whilePlaying = e.getReplayState();
            }
            if (pos <= (size_t) (2.0 * kFs) && (size_t) (2.0 * kFs) < pos + (size_t) n)
            {
                stepWhileLooping = e.getCurrentStep();
                whileLooping = e.getReplayState();
            }
        }
        out = o;
        CHECK(stepWhilePlaying == -1 && whilePlaying == ReplayLooper::State::Live, "while the player plays the sequence waits (step %d)", stepWhilePlaying);
        CHECK(stepWhileLooping >= 0 && whileLooping == ReplayLooper::State::Looping, "it runs while the loop plays (step %d)", stepWhileLooping);
    }
    CHECK(maxDiff(out, 0.0, 1.0) < 1.0e-6, "a continuous sequence leaves the playing clean (diff %g)", maxDiff(out, 0.0, 1.0));
    CHECK(dominantRatio(out, 1.5, 3.9, 440.0, 880.0) > 10.0, "the loop goes through the sequence (an octave up, ratio %g)",
          dominantRatio(out, 1.5, 3.9, 440.0, 880.0));
    seq.replay.dry = 1.0f;
    out = render(seq, in);
    CHECK(dominantRatio(out, 1.5, 3.9, 880.0, 440.0) > 10.0, "DRY all the way: the loop as it was played (ratio %g)",
          dominantRatio(out, 1.5, 3.9, 880.0, 440.0));

    // picks never restart the sequence or fire the stutter while the player plays
    Params picks = seq;
    picks.replay.dry = 0.0f;
    picks.stutter = 5;
    picks.seqPlay = 1;
    const Signal plucks = [&]
    {
        Signal x((size_t) (1.0 * kFs), 0.0f);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = std::fmod((double) i / kFs, 0.25);
            x[i] = (float) (0.4 * std::exp(-8.0 * t) * std::sin(2.0 * 3.14159265358979 * 330.0 * (double) i / kFs));
        }
        return x;
    }();
    const Signal heard = render(picks, plucks);
    double d = 0.0;
    for (size_t i = 0; i < plucks.size(); ++i)
        d = std::max(d, (double) std::abs(heard[i] - plucks[i]));
    CHECK(d < 1.0e-6, "picked notes pass clean: no stutter, no sequence (diff %g)", d);

    // switched off, the plug-in is as before
    Params off = p;
    off.replay.on = false;
    out = render(off, in);
    CHECK(maxDiff(out, 0.0, 7.0) < 1.0e-6, "REPLAY off: the neutral settings pass the input (diff %g)", maxDiff(out, 0.0, 7.0));
}

// REPLAY's playback: speed (tape style), direction, the granular stretch, the swell and the wear.
void testReplayPlayback()
{
    std::puts("replay playback");
    // a phrase whose pitch changes half way: 440 Hz for half a second, then 660 Hz for half a second, then silence
    Signal in((size_t) (4.0 * kFs), 0.0f);
    for (size_t i = 0; i < in.size(); ++i)
    {
        const double t = (double) i / kFs;
        if (t < 0.5)
            in[i] = 0.3f * (float) std::sin(2.0 * 3.14159265358979 * 440.0 * t);
        else if (t < 1.0)
            in[i] = 0.3f * (float) std::sin(2.0 * 3.14159265358979 * 660.0 * t);
    }
    Params p = neutral();
    p.replay.on = true;
    p.replay.lookBackSeconds = 1.0f; // the whole phrase: 440 then 660
    p.replay.waitSeconds = 0.3f;
    // the loop starts at 1.3 s: forward, the first 0.4 s of it are the 440 Hz half
    Signal out = render(p, in);
    CHECK(tonePower(out, 1.35, 1.7, 440.0) > 100.0 * tonePower(out, 1.35, 1.7, 660.0), "forward: the loop begins with the 440 Hz half");
    CHECK(tonePower(out, 1.85, 2.2, 660.0) > 100.0 * tonePower(out, 1.85, 2.2, 440.0), "and goes on to the 660 Hz half");

    p.replay.direction = ReplayDirection::Reverse;
    out = render(p, in);
    CHECK(tonePower(out, 1.35, 1.7, 660.0) > 100.0 * tonePower(out, 1.35, 1.7, 440.0), "REVERSE: it begins with the 660 Hz half (from the end)");

    p.replay.direction = ReplayDirection::BackAndForth;
    out = render(p, in);
    // forward for a second (440 then 660), then back (660 then 440): 2.3 to 2.8 s is the backward 660 half, 2.8 on the 440
    CHECK(tonePower(out, 1.35, 1.7, 440.0) > 100.0 * tonePower(out, 1.35, 1.7, 660.0)
              && tonePower(out, 2.35, 2.7, 660.0) > 100.0 * tonePower(out, 2.35, 2.7, 440.0)
              && tonePower(out, 2.85, 3.2, 440.0) > 100.0 * tonePower(out, 2.85, 3.2, 660.0),
          "BACK AND FORTH: forward through the loop, then backwards from its end");

    p.replay.direction = ReplayDirection::Forward;
    p.replay.speed = 0.5f;
    out = render(p, in);
    CHECK(tonePower(out, 1.35, 2.2, 220.0) > 100.0 * tonePower(out, 1.35, 2.2, 440.0), "HALF SPEED: an octave down (220 Hz)");
    CHECK(tonePower(out, 2.4, 3.2, 330.0) > 100.0 * tonePower(out, 2.4, 3.2, 220.0), "and the 660 Hz half arrives a second later than at full speed, at 330 Hz");

    p.replay.speed = 1.0f;
    p.replay.stretch = 2.0f;
    out = render(p, in);
    CHECK(tonePower(out, 1.35, 2.2, 440.0) > 30.0 * tonePower(out, 1.35, 2.2, 660.0), "STRETCH 2x: the 440 Hz half lasts a second, still at 440 Hz (ratio %g)",
          tonePower(out, 1.35, 2.2, 440.0) / std::max(1.0e-18, tonePower(out, 1.35, 2.2, 660.0)));
    CHECK(tonePower(out, 2.4, 3.2, 660.0) > 30.0 * tonePower(out, 2.4, 3.2, 440.0), "then the 660 Hz half, at 660 Hz");
    CHECK(rms(out, 1.4, 3.9) > 0.5 * rms(in, 0.1, 0.9) && rms(out, 1.4, 3.9) < 1.5 * rms(in, 0.1, 0.9), "the grains add up to about the loop's own level (rms %g vs %g)",
          rms(out, 1.4, 3.9), rms(in, 0.1, 0.9));

    p.replay.stretch = 1.0f;
    p.replay.swellSeconds = 2.0f;
    out = render(p, in);
    CHECK(rms(out, 1.3, 1.5) < 0.35 * rms(out, 3.3, 3.5) && rms(out, 3.3, 3.5) > 0.7 * rms(in, 0.1, 0.9),
          "SWELL 2 s: the loop starts quiet and is up to level two seconds later (rms %g then %g)", rms(out, 1.3, 1.5), rms(out, 3.3, 3.5));

    p.replay.swellSeconds = 0.0f;
    p.replay.wear = 1.0f;
    Signal longer(in);
    longer.resize((size_t) (8.0 * kFs), 0.0f);
    out = render(p, longer);
    // the loop is a second long: pass one at 1.3 s, pass six at 6.3 s
    const double first660 = tonePower(out, 1.85, 2.25, 660.0), later660 = tonePower(out, 6.85, 7.25, 660.0);
    const double first440 = tonePower(out, 1.35, 1.75, 440.0), later440 = tonePower(out, 6.35, 6.75, 440.0);
    CHECK(later660 < 0.2 * first660 && later440 < 0.4 * first440 && later660 / first660 < later440 / first440,
          "WEAR: six passes on, the loop is quieter and the 660 Hz has gone more than the 440 Hz (%g and %g of the first pass)",
          later660 / first660, later440 / first440);
}

// SPLICE: clips butted together with no fades. The edits are single-sample jumps in the waveform (and dropouts with a
// GAP), only while a sequence plays, as often as the CHANCE says, and the sound returns to itself without a click.
void testSplice()
{
    std::puts("splice");
    const Signal tone = sine(440.0, 3.0, 0.5f);
    // the biggest step a 440 Hz sine at 0.5 takes between samples
    const double sineStep = 0.5 * 2.0 * 3.14159265358979 * 440.0 / kFs;
    auto clicks = [&](const Signal& out, double t0, double t1)
    {
        int n = 0;
        for (size_t i = (size_t) (t0 * kFs) + 1; i < (size_t) (t1 * kFs); ++i)
            n += std::abs(out[i] - out[i - 1]) > 2.0 * sineStep ? 1 : 0;
        return n;
    };
    Params p; // a continuous sequence of dry steps, 100 ms each
    p.seqPlay = 5;
    p.tempoBpm = 600.0f;
    p.seqMult = 0;
    p.steps.fill(mapping::pitchStepKnob(0));
    const Signal dry = render(p, tone);
    CHECK(clicks(dry, 0.2, 2.9) == 0, "without SPLICE the dry sequence has no clicks (%d)", clicks(dry, 0.2, 2.9));

    p.splice.on = true;
    p.splice.chance = 1.0f;
    p.splice.jumpMs = 50.0f;
    p.splice.gapMs = 0.0f;
    Signal out = render(p, tone);
    // an edit at every step: 27 steps from 0.2 to 2.9 s (a jump can land near the same phase now and then)
    const int everyStep = clicks(out, 0.2, 2.9);
    CHECK(everyStep >= 15 && everyStep <= 30, "CHANCE 100%%, on steps: a click at (nearly) every step (%d in 27 steps)", everyStep);
    double peak = 0.0;
    for (size_t i = (size_t) (0.2 * kFs); i < out.size(); ++i)
        peak = std::max(peak, (double) std::abs(out[i]));
    CHECK(peak <= 0.5001, "the edits never make the sound louder: they only jump within it (peak %g)", peak);

    p.splice.chance = 0.25f;
    const int someSteps = clicks(render(p, tone), 0.2, 2.9);
    CHECK(someSteps >= 2 && someSteps < everyStep, "CHANCE 25%%: only some of the steps (%d)", someSteps);

    p.splice.chance = 1.0f;
    p.splice.when = SpliceWhen::Sixteenths; // at 600 BPM a sixteenth is 25 ms: four times the steps
    const int sixteenths = clicks(render(p, tone), 0.2, 2.9);
    CHECK(sixteenths > 2 * everyStep, "on 1/16 notes: more edits than on the steps (%d vs %d)", sixteenths, everyStep);

    // GAP with no JUMP: dropouts of digital silence between the clips, between a quarter of the GAP and all of it
    p.splice.when = SpliceWhen::Steps;
    p.splice.jumpMs = 0.0f;
    p.splice.gapMs = 10.0f;
    out = render(p, tone);
    int gaps = 0, longest = 0, run = 0;
    for (size_t i = (size_t) (0.2 * kFs); i < (size_t) (2.9 * kFs); ++i)
    {
        if (out[i] == 0.0f)
            ++run;
        else
        {
            if (run >= 100)
                ++gaps;
            longest = std::max(longest, run);
            run = 0;
        }
    }
    CHECK(gaps >= 20 && longest <= (int) (0.010 * kFs) + 1, "GAP 10 ms: a dropout at each step, never longer than the GAP (%d, longest %d samples)",
          gaps, longest);

    // only while the sequence plays: a sequence played once, then the sound must be itself again, with no click
    p.splice.gapMs = 0.0f;
    p.splice.jumpMs = 50.0f;
    p.seqPlay = 1;
    out = render(p, tone);
    double after = 0.0;
    for (size_t i = (size_t) (1.2 * kFs); i < out.size(); ++i)
        after = std::max(after, (double) std::abs(out[i] - tone[i]));
    CHECK(after < 1.0e-3, "once the sequence has stopped the sound is back in time with the input (diff %g)", after);
    CHECK(clicks(out, 0.7, 2.9) == 0, "and it glided back without a click (%d)", clicks(out, 0.7, 2.9));

    // never on the clean signal: no sequence, no edits
    Params idle = neutral();
    idle.splice = p.splice;
    idle.splice.when = SpliceWhen::Anywhere;
    out = render(idle, tone);
    double diff = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
        diff = std::max(diff, (double) std::abs(out[i] - tone[i]));
    CHECK(diff < 1.0e-6, "with no sequence or stutter playing, SPLICE leaves the sound alone (diff %g)", diff);

    // ANYWHERE: random moments, about eight chances a beat (100 ms here) times the CHANCE
    p.seqPlay = 5;
    p.splice.when = SpliceWhen::Anywhere;
    p.splice.chance = 0.5f;
    const int anywhere = clicks(render(p, tone), 0.2, 2.9);
    CHECK(anywhere > 40 && anywhere < 160, "ANYWHERE at 50%%: about four edits a beat (%d in 27 beats)", anywhere);
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0); // so a crash still shows how far the checks got
    gVerbose = argc > 1 && std::string(argv[1]) == "--verbose";
    testMappings();
    testIdentity();
    testBitDepth();
    testSampleRate();
    testFilter();
    testFilterTypes();
    testAttackDetector();
    testStutter();
    testSequencer();
    testRobustness();
    testMorph();
    testMorphThroughEngine();
    testPresets();
    testMix();
    testEffectsOnTriggers();
    testBeatRoute();
    testBeatPattern();
    testStepTypes();
    testSeqOnly();
    testDelay();
    testSampleMaxBeats();
    testSampleRender();
    testSequenceLoop();
    testLoopSectionSamples();
    testEqReference();
    testReverb();
    testSampleBlend();
    testDistortion();
    testTremolo();
    testSwing();
    testRingMod();
    testPing();
    testBloom();
    testFormant();
    testPhaserFlanger();
    testReplay();
    testReplayPlayback();
    testSplice();
    testSeqLengthAndLanes();
    testStutterKinds();
    testDelayWobble();
    testFuzz();
    testEqualizer();
    testTriggerEvents();
    testDrumScheduler();
    testTempoEstimate();
    testFft();
    testDrumAnalysis();

    std::printf("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
