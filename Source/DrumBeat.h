#pragma once

#include "DemoBeat.h"
#include "DrumAnalysis.h"
#include "DrumMap.h"
#include "HandOver.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

// The drum beat behind the DRUMS tab: a file (or the built-in demo beat), the hits found in it, the
// per-drum settings that turn hits into sequencer / stutter triggers, and its looped playback.
//
// Threads: the message thread owns everything the editor sees (getModel(), settings, edits) and starts
// a background job to decode, analyse and resample. The audio thread only reads immutable snapshots
// handed over through lock-free slots, plus per-drum settings from atomics; it never locks or allocates.
class DrumBeat : private juce::Timer
{
public:
    enum class AnalysisMode
    {
        Auto,       // the neural helper when it is installed, otherwise the built-in analysis
        BuiltInOnly
    };

    // What the editor shows. A copy, so the editor never waits for a running job.
    struct Model
    {
        bool loaded = false;         // there is a beat with hits (its audio may still be missing)
        bool isDemo = false;
        bool audioAvailable = false; // the audio could be read (needed to hear the beat)
        juce::File file;             // empty for the demo beat
        juce::String name;
        std::vector<bitvibe::DrumHit> hits;
        double durationSeconds = 0.0;
        double tempoBpm = 0.0;
        juce::String engine;         // which analysis found the hits
        int generation = 0;          // changes whenever the hits or the audio change
    };

    static constexpr float kBeatOffDb = -60.0f; // Beat Level at or below this is silent
    static constexpr double kMaxSeconds = 300.0; // longer files are cut to this

    DrumBeat();
    ~DrumBeat() override;

    // ---- message thread ------------------------------------------------------------------------
    void loadFile(const juce::File&);
    void loadDemo();
    void reanalyse();
    void clear();
    void setDeviceSampleRate(double sampleRate);
    void restartPlayback(); // the next playing block starts from the top (the host restarted audio)

    Model getModel() const;
    int getGeneration() const; // cheap check for a changed model
    juce::String getStatus() const;
    void setTempoEstimate(double bpm); // the x2 / /2 buttons correct an octave error
    bool isBusy() const;

    // Edits to the hit list (by index into getModel().hits); a re-analysis replaces them.
    void setHitExcluded(int index, bool excluded);
    void setHitDrum(int index, bitvibe::Drum drum);
    void deleteHit(int index);

    bitvibe::DrumSettingsArray getSettings() const;
    void setSettings(bitvibe::Drum drum, const bitvibe::DrumSettings& settings);

    // The beat pattern: triggers at chosen sixteenth-note steps of every bar of the loop, independent of the
    // drum hits and their level.
    bitvibe::DrumPattern getPattern() const;
    void setPattern(const bitvibe::DrumPattern& pattern);

    AnalysisMode getAnalysisMode() const;
    void setAnalysisMode(AnalysisMode);

    // The optional neural helper: a Python environment with ADTOF, set up by Tools/drums/setup_neural.
    static juce::File neuralHelperFolder();
    static juce::File neuralHelperPython();
    static juce::File neuralHelperScript();
    static bool isNeuralHelperInstalled();

    // Folders listed as the beat library, and the audio files BitVibe can open.
    static juce::Array<juce::File> libraryFolders();
    static juce::Array<juce::File> libraryFiles();
    static bool isSupportedAudioFile(const juce::File&);

    juce::ValueTree toState() const;
    void fromState(const juce::ValueTree&); // an invalid tree resets everything

    // Waits (message thread) until no job is running; for tests and tools.
    bool waitUntilIdle(int timeoutMs);

    // ---- audio thread --------------------------------------------------------------------------
    // Per block: beginBlock() gives the trigger events of this block, addAudio() mixes the beat in,
    // endBlock() moves the loop on.
    int beginBlock(int numSamples, bool playing, bool triggers, bitvibe::TriggerEvent* events, int maxEvents);
    void addAudio(juce::AudioBuffer<float>& buffer, int numChannels, int numSamples, float levelDb);
    void endBlock(int numSamples);

    // Playback as the editor sees it (relaxed atomics).
    bool isPlaying() const { return playingOut.load(std::memory_order_relaxed); }
    double getPositionSeconds() const { return positionOut.load(std::memory_order_relaxed); }
    int getPlayingGeneration() const { return generationOut.load(std::memory_order_relaxed); }

private:
    struct HitData
    {
        std::vector<bitvibe::DrumHit> hits;
        std::vector<int64_t> samples; // each hit's position in the loop at sampleRate
        int64_t loopLength = 0;
        double sampleRate = 48000.0;
        int generation = 0;
        HitData* nextRetired = nullptr;
    };

    struct AudioData
    {
        juce::AudioBuffer<float> audio; // the beat at sampleRate
        double sampleRate = 48000.0;
        AudioData* nextRetired = nullptr;
    };

    template <typename T>
    using HandOver = ::HandOver<T>; // see HandOver.h

    enum class JobKind
    {
        Load,       // decode and analyse a file
        Demo,       // render and analyse the demo beat
        Reanalyse,  // analyse the audio already loaded
        Restore,    // decode (or render) the audio of hits that came from a saved state
        Resample    // the device rate changed
    };

    class Job;
    friend class Job;

    void startJob(JobKind kind, const juce::File& file = {});
    void stopJob();
    void runJob(JobKind kind, const juce::File& file, Job& job);
    bool decode(const juce::File&, juce::AudioBuffer<float>& out, double& rate, juce::String& error) const;
    std::vector<bitvibe::DrumHit> analyse(const juce::AudioBuffer<float>& audio, double rate, bool allowNeural,
                                          const juce::File& file, Job& job, juce::String& engine);
    bool runNeuralHelper(const juce::File& audioFile, std::vector<bitvibe::DrumHit>& hits, Job& job, juce::String& error);
    void publishHits();
    void publishAudio();
    void setStatus(const juce::String&);
    void timerCallback() override;

    // message-thread state (also written by the job thread, so under the lock)
    mutable juce::CriticalSection lock;
    Model model;
    juce::AudioBuffer<float> sourceAudio; // as decoded, at sourceRate
    double sourceRate = 48000.0;
    juce::String status;
    AnalysisMode mode = AnalysisMode::Auto;
    std::atomic<double> deviceRate { 48000.0 };
    juce::CriticalSection jobLock; // the host may change the sample rate from another thread
    std::unique_ptr<Job> job;

    // per-drum settings, read by the audio thread
    std::array<std::atomic<int>, bitvibe::kNumDrums> actions;
    std::array<std::atomic<float>, bitvibe::kNumDrums> minLevels, minConfidences;
    std::atomic<int> patternAction { 0 };
    std::atomic<uint32_t> patternSteps { 0 };
    std::atomic<uint32_t> patternRandomBeats { 0x1F };
    std::atomic<int> patternSignature { 0 };

    HandOver<HitData> hitSlot;
    HandOver<AudioData> audioSlot;

    // audio-thread state
    const HitData* blockHits = nullptr;
    const AudioData* blockAudio = nullptr;
    int64_t position = 0;
    bool wasPlaying = false, blockPlaying = false, justStarted = false;
    std::atomic<bool> restartRequested { false };
    juce::LinearSmoothedValue<float> gain;
    double gainRate = 0.0;
    std::atomic<bool> playingOut { false };
    std::atomic<double> positionOut { 0.0 };
    std::atomic<int> generationOut { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumBeat)
};
