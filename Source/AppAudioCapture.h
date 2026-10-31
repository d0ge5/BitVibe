#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <vector>

// Records the sound one program on this computer is playing (or everything except the host), for the sampler. Uses
// Windows' per-process loopback capture (Windows 10 version 2004 / Windows 11 and later); on other systems
// isAvailable() is false and nothing records.
//
// Threads: listSources, start and stop on the message thread; the recording runs on its own thread. The recorded audio
// is only read (takeRecording) once the recording has stopped.
class AppAudioCapture
{
public:
    static constexpr double kSampleRate = 48000.0;
    static constexpr double kMaxSeconds = 60.0;

    struct Source
    {
        juce::String name;      // what the list shows
        juce::uint32 pid = 0;   // the program (its child processes are recorded too)
        bool exclude = false;   // true: everything except this program
    };

    AppAudioCapture();
    ~AppAudioCapture();

    static bool isAvailable();
    // The programs that have sound open on the default output, by name, plus "everything except this host".
    static juce::Array<Source> listSources();

    bool start(const Source&, juce::String& error);
    void stop();
    bool isRecording() const { return recording.load(std::memory_order_acquire); }
    double getRecordedSeconds() const { return (double) frames.load(std::memory_order_relaxed) / kSampleRate; }
    float takePeak() { return peak.exchange(0.0f, std::memory_order_relaxed); }
    // Why the last recording ended early, if it did ("" otherwise).
    juce::String getLastError() const;

    // Hands over what was recorded (stereo, kSampleRate). Only after stop().
    void takeRecording(std::vector<float>& left, std::vector<float>& right);

private:
    class Worker;
    std::unique_ptr<Worker> worker;
    std::vector<float> recordedL, recordedR;
    std::atomic<bool> recording { false };
    std::atomic<juce::int64> frames { 0 };
    std::atomic<float> peak { 0.0f };
    juce::String lastError;
    juce::CriticalSection errorLock;

    friend class Worker;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppAudioCapture)
};
