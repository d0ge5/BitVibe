// Offline checks of the Ottobit Jr. replica engine against the behaviour documented in the manual.
// Plain C++, no framework: build target `ottobit_tests`, exit code 0 means everything passed.

#include "Engine.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

using namespace ottobit;

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
} // namespace

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

    std::printf("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
