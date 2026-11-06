#pragma once

#include "PluginEditor.h"

#include <vector>

// Small pictures of what an effect does to the sound, in the left of its panel. Each is worked out from the effect's
// settings by running the effect itself on a test sound, so it shows what is really heard: recomputed (by the editor's
// timer) only when a setting it shows changes.
class EffectGraph : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit EffectGraph(BitVibeProcessor& p) : proc(p) {}
    // Recomputes and repaints when a setting it shows has changed; true if it did.
    bool refresh();
    void paint(juce::Graphics&) override;

protected:
    virtual juce::String settingsKey() const = 0; // the settings it depends on, as text
    virtual void compute() = 0;
    virtual void draw(juce::Graphics&, juce::Rectangle<float> area) const = 0;
    float value(const char* id) const;
    void caption(juce::Graphics&, juce::Rectangle<float> area, const juce::String& text) const; // top right, small

    BitVibeProcessor& proc;

private:
    juce::String shownKey;
    double lastComputeMs = 0.0;
};

// DISTORTION / FUZZ: a plain tone in (grey) and what comes out (the accent), a cycle and a half.
class DistortionGraph : public EffectGraph
{
public:
    explicit DistortionGraph(BitVibeProcessor&);
    const std::vector<float>& getOutput() const { return out; } // for tests

protected:
    juce::String settingsKey() const override;
    void compute() override;
    void draw(juce::Graphics&, juce::Rectangle<float>) const override;

private:
    std::vector<float> in, out;
};

// DELAY: one note and its echoes on the beat grid: their spacing is TIME, their height MIX and DECAY, their number
// REPEATS (or on until they fade, for FOREVER).
class DelayGraph : public EffectGraph
{
public:
    explicit DelayGraph(BitVibeProcessor&);
    struct Echo
    {
        double beats; // after the note
        float level;  // 0..1 (the note is 1)
    };
    const std::vector<Echo>& getEchoes() const { return echoes; } // for tests

protected:
    juce::String settingsKey() const override;
    void compute() override;
    void draw(juce::Graphics&, juce::Rectangle<float>) const override;

private:
    float wobble = 0.0f;
    std::vector<Echo> echoes;
    double windowBeats = 4.0, echoBeats = 0.5, echoMs = 250.0;
    juce::String division;
};

// REVERB: the tail after a short note, loudness against time (quiet at the bottom, -48 dB): how long it rings, and
// with REVERSE how it swells up and stops.
class ReverbGraph : public EffectGraph
{
public:
    explicit ReverbGraph(BitVibeProcessor&);
    static constexpr double kRate = 16000.0, kBucketSeconds = 0.02, kMaxSeconds = 10.0;
    const std::vector<float>& getEnvelope() const { return envelopeDb; } // for tests: dB per bucket, 0 = the loudest
    double getTailSeconds() const { return tailSeconds; }

protected:
    juce::String settingsKey() const override;
    void compute() override;
    void draw(juce::Graphics&, juce::Rectangle<float>) const override;

private:
    std::vector<float> envelopeDb;
    double tailSeconds = 1.0, windowSeconds = 2.0;
};
