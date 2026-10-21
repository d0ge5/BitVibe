// Offline checks of the BitVibe engine against the behaviour described in the published documentation.
// Plain C++, no framework: build target `bitvibe_tests`, exit code 0 means everything passed.

#include "Engine.h"
#include "Morph.h"
#include "Presets.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace bitvibe;

namespace
{
constexpr double kFs = 48000.0;
int gChecks = 0, gFailures = 0;

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
    std::puts("presets: sixteen factory slots");
    CHECK(kNumPresets == 16, "expected 16 preset slots");

    // Slot 1 is the sound the plug-in has always started with.
    const FactoryPreset first = factoryPreset(0);
    const Params def;
    CHECK(first.knobs == knobsOf(def), "slot 1 must be the default knob set");
    CHECK(first.knobs.seqPlay == 5 && first.knobs.seqType == SeqType::Pitch, "slot 1 must run the pitch sequencer");
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
    for (int i = 0; i < kNumPresets; ++i)
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
    for (int i = 0; i < kNumPresets; ++i)
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
              std::string(factoryPreset(99).name) == std::string(factoryPreset(kNumPresets - 1).name),
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

int main()
{
    testMappings();
    testIdentity();
    testBitDepth();
    testSampleRate();
    testFilter();
    testAttackDetector();
    testStutter();
    testSequencer();
    testRobustness();
    testMorph();
    testMorphThroughEngine();
    testPresets();
    testMix();
    testEffectsOnTriggers();

    std::printf("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
