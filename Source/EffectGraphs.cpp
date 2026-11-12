#include "EffectGraphs.h"

using namespace juce;
using namespace bitvibe;
using namespace pedalstyle;

// ------------------------------------------------------------------------------------------
// The common part

float EffectGraph::value(const char* id) const { return proc.apvts.getRawParameterValue(id)->load(); }

bool EffectGraph::refresh()
{
    const auto key = settingsKey();
    if (key == shownKey)
        return false;
    // while a knob is being turned, at most about ten times a second (the reverb's tail takes a few milliseconds)
    const double now = Time::getMillisecondCounterHiRes();
    if (shownKey.isNotEmpty() && now - lastComputeMs < 90.0)
        return false;
    compute();
    shownKey = key;
    lastComputeMs = now;
    repaint();
    return true;
}

void EffectGraph::paint(Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(theme.well);
    g.fillRoundedRectangle(bounds, 4.0f);
    draw(g, bounds.reduced(4.0f, 3.0f));
    g.setColour(theme.outline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
}

void EffectGraph::caption(Graphics& g, Rectangle<float> area, const String& text) const
{
    g.setColour(inkDim);
    g.setFont(plain(11.0f));
    g.drawText(text, area.withHeight(13.0f).toNearestInt(), Justification::topRight);
}

// ------------------------------------------------------------------------------------------
// Distortion / fuzz

DistortionGraph::DistortionGraph(BitVibeProcessor& p) : EffectGraph(p)
{
    setTooltip("DISTORTION GRAPH: what it does to a plain tone. The grey wave goes in, the coloured one comes out: more DRIVE "
               "flattens its tops (more crunch), the fuzz squares them off, LEVEL sets how big it comes out.");
}

String DistortionGraph::settingsKey() const
{
    return String(roundToInt(value(ids::distType))) + "|" + String(value(ids::distDrive), 3) + "|" + String(value(ids::distTone), 3) + "|"
           + String(value(ids::distLevel), 3);
}

void DistortionGraph::compute()
{
    // a 220 Hz tone at -6 dB through a fresh copy of the effect, settled for a tenth of a second, then a cycle and a half
    constexpr double fs = 48000.0, hz = 220.0;
    constexpr int settle = 4800, shown = (int) (1.5 * fs / hz);
    const bool fuzz = roundToInt(value(ids::distType)) == 1;
    bitvibe::Distortion overdrive;
    bitvibe::Fuzz bigFuzz;
    overdrive.prepare(fs);
    bigFuzz.prepare(fs);
    overdrive.setParams(value(ids::distDrive), value(ids::distTone), value(ids::distLevel));
    bigFuzz.setParams(value(ids::distDrive), value(ids::distTone), value(ids::distLevel));
    in.assign((size_t) shown, 0.0f);
    out.assign((size_t) shown, 0.0f);
    for (int n = 0; n < settle + shown; ++n)
    {
        const float x = 0.5f * (float) std::sin(MathConstants<double>::twoPi * hz * (double) n / fs);
        const float y = fuzz ? bigFuzz.process(x) : overdrive.process(x);
        if (n >= settle)
        {
            in[(size_t) (n - settle)] = x;
            out[(size_t) (n - settle)] = y;
        }
    }
}

void DistortionGraph::draw(Graphics& g, Rectangle<float> area) const
{
    if (in.empty())
        return;
    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;
    g.setColour(theme.gridLine);
    g.fillRect(area.getX(), mid, area.getWidth(), 1.0f);
    auto wave = [&](const std::vector<float>& v)
    {
        Path p;
        for (size_t i = 0; i < v.size(); ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (v.size() - 1);
            const float y = mid - jlimit(-1.0f, 1.0f, v[i] / 0.55f) * half; // the tone in reaches nine tenths of the way
            if (i == 0)
                p.startNewSubPath(x, y);
            else
                p.lineTo(x, y);
        }
        return p;
    };
    g.setColour(inkDim.withAlpha(0.55f));
    g.strokePath(wave(in), PathStrokeType(1.2f));
    g.setColour(accent);
    g.strokePath(wave(out), PathStrokeType(1.8f, PathStrokeType::curved, PathStrokeType::rounded));
}

// ------------------------------------------------------------------------------------------
// Delay

DelayGraph::DelayGraph(BitVibeProcessor& p) : EffectGraph(p)
{
    setTooltip("DELAY GRAPH: one note (grey) and its echoes, with a line on each beat. Their spacing is TIME, their height "
               "MIX and DECAY, their number REPEATS; FOREVER goes on until they fade. WOBBLE sets them drifting, like tape.");
}

String DelayGraph::settingsKey() const
{
    return String(roundToInt(value(ids::delayTime))) + "|" + String(proc.getEffectiveBpm(), 1) + "|" + String(value(ids::delayMix), 3) + "|"
           + String(value(ids::delayDecay), 3) + "|" + String(roundToInt(value(ids::delayRepeats))) + "|" + String(value(ids::delayWobble), 2);
}

void DelayGraph::compute()
{
    const auto& d = kDelayDivisions[(size_t) jlimit(0, (int) kDelayDivisions.size() - 1, roundToInt(value(ids::delayTime)))];
    const double bpm = jmax(20.0, (double) proc.getEffectiveBpm());
    division = d.name;
    echoBeats = d.beats;
    echoMs = d.beats * 60000.0 / bpm;
    wobble = value(ids::delayWobble);
    const int repeats = roundToInt(value(ids::delayRepeats));
    const bool forever = repeats >= kDelayForever;
    const float g = Delay::decayGain(value(ids::delayDecay));
    constexpr double kMostBeats = 16.0;
    echoes.clear();
    float level = value(ids::delayMix);
    for (int k = 1; forever ? (level > 0.02f && (double) k * d.beats <= kMostBeats) : k <= repeats; ++k)
    {
        echoes.push_back({ (double) k * d.beats, level });
        level *= g;
    }
    const double last = echoes.empty() ? d.beats : echoes.back().beats;
    windowBeats = jlimit(1.0, kMostBeats, std::ceil(last + d.beats * 0.6));
}

void DelayGraph::draw(Graphics& g, Rectangle<float> area) const
{
    auto xFor = [&](double beats) { return area.getX() + 3.0f + (area.getWidth() - 6.0f) * (float) (beats / windowBeats); };
    g.setColour(theme.gridLine);
    for (int beat = 1; beat < (int) windowBeats; ++beat)
        g.fillRect(xFor((double) beat), area.getY(), 1.0f, area.getHeight());
    const float bottom = area.getBottom(), height = area.getHeight() - 14.0f; // the caption sits above the tallest bar
    const float barW = jlimit(2.0f, 5.0f, (area.getWidth() - 6.0f) * (float) (echoBeats / windowBeats) * 0.45f);
    g.setColour(inkDim.withAlpha(0.7f));
    g.fillRect(xFor(0.0) - barW * 0.5f, bottom - height, barW, height);
    g.setColour(accent);
    int k = 0;
    for (const auto& e : echoes)
    {
        // with WOBBLE the echoes drift off the beat, each later one further, as on a worn tape
        ++k;
        const float drift = wobble * 2.5f * std::sqrt((float) k) * (float) std::sin(1.7 * k + 0.5);
        const float h = jmax(1.0f, height * jlimit(0.0f, 1.0f, e.level));
        g.fillRect(xFor(e.beats) - barW * 0.5f + drift, bottom - h, barW, h);
    }
    caption(g, area, division + "  " + String(roundToInt(echoMs)) + " ms" + (wobble >= 0.005f ? "  TAPE" : ""));
}

// ------------------------------------------------------------------------------------------
// Reverb

ReverbGraph::ReverbGraph(BitVibeProcessor& p) : EffectGraph(p)
{
    setTooltip("REVERB GRAPH: the reverb after a short note, loud at the top and quiet (-48 dB) at the bottom, and how long "
               "it rings. DECAY and the type set its length; REVERSE makes it swell up and stop.");
}

String ReverbGraph::settingsKey() const
{
    const int type = roundToInt(value(ids::reverbType));
    return String(type) + "|" + String(value(ids::reverbDecay), 3) + "|" + String(value(ids::reverbReverse) > 0.5f ? 1 : 0);
}

void ReverbGraph::compute()
{
    // a short burst of noise through a fresh reverb (at a low rate: the shape is what matters), its loudness every 20 ms
    const int type = roundToInt(value(ids::reverbType));
    ReverbSettings s;
    s.type = (ReverbType) jlimit(0, kNumReverbTypes - 1, type);
    s.decay = value(ids::reverbDecay);
    s.reverse = value(ids::reverbReverse) > 0.5f;
    bitvibe::Reverb reverb;
    reverb.prepare(kRate, 0);
    for (int n = 0; n < (int) (0.1 * kRate); ++n) // settles its type first, so the burst is not faded in
        reverb.process(0.0f, s);
    Random noise(17);
    const int bucket = (int) (kBucketSeconds * kRate), burst = (int) (0.01 * kRate), most = (int) (kMaxSeconds / kBucketSeconds);
    std::vector<float> rms;
    float peak = 0.0f;
    int quietSince = -1;
    for (int b = 0; b < most; ++b)
    {
        double sum = 0.0;
        for (int i = 0; i < bucket; ++i)
        {
            const int n = b * bucket + i;
            const float x = n < burst ? 0.5f * (noise.nextFloat() * 2.0f - 1.0f) : 0.0f;
            const float y = reverb.process(x, s);
            sum += (double) y * y;
        }
        const float level = (float) std::sqrt(sum / bucket);
        rms.push_back(level);
        peak = jmax(peak, level);
        // stop half a second after it has fallen 60 dB below its loudest
        const bool quiet = peak > 0.0f && level < peak * 0.001f;
        quietSince = quiet ? (quietSince < 0 ? b : quietSince) : -1;
        if (quietSince >= 0 && (double) (b - quietSince) * kBucketSeconds > 0.5)
            break;
    }
    envelopeDb.clear();
    int last = 0;
    for (size_t b = 0; b < rms.size(); ++b)
    {
        const float db = peak > 0.0f && rms[b] > 0.0f ? 20.0f * std::log10(rms[b] / peak) : -100.0f;
        envelopeDb.push_back(db);
        if (db > -48.0f)
            last = (int) b;
    }
    tailSeconds = (double) (last + 1) * kBucketSeconds;
    windowSeconds = 10.0;
    for (const double w : { 0.5, 1.0, 2.0, 3.0, 4.0, 6.0, 8.0, 10.0 })
        if (w >= tailSeconds * 1.15)
        {
            windowSeconds = w;
            break;
        }
}

void ReverbGraph::draw(Graphics& g, Rectangle<float> area) const
{
    if (envelopeDb.empty())
        return;
    const auto plot = area.withTrimmedTop(12.0f);
    auto xFor = [&](double seconds) { return plot.getX() + plot.getWidth() * (float) (seconds / windowSeconds); };
    auto yFor = [&](float db) { return plot.getBottom() - plot.getHeight() * jlimit(0.0f, 1.0f, (db + 48.0f) / 48.0f); };
    g.setColour(theme.gridLine);
    for (int second = 1; second < (int) windowSeconds; ++second)
        g.fillRect(xFor((double) second), area.getY(), 1.0f, area.getHeight());
    Path tail;
    tail.startNewSubPath(plot.getX(), plot.getBottom());
    for (size_t b = 0; b < envelopeDb.size(); ++b)
    {
        const double t = ((double) b + 0.5) * kBucketSeconds;
        if (t > windowSeconds)
            break;
        tail.lineTo(xFor(t), yFor(envelopeDb[b]));
    }
    tail.lineTo(xFor(jmin(windowSeconds, (double) envelopeDb.size() * kBucketSeconds)), plot.getBottom());
    tail.closeSubPath();
    g.setColour(accent.withAlpha(0.25f));
    g.fillPath(tail);
    g.setColour(accent);
    g.strokePath(tail, PathStrokeType(1.4f));
    caption(g, area, String(tailSeconds, tailSeconds < 10.0 ? 1 : 0) + " s");
}
