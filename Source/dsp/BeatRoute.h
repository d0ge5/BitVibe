#pragma once

// Where the drum beat's sound is sent.
//
// The beat is something to play along with, not part of the guitar signal. The processed guitar goes to
// the main output (and from there to the next plug-in, the amp, the recording). The beat's sound can go to a
// separate "Beat Monitor" output instead, which the player routes to their headphones, so it never reaches
// anything that comes after the plug-in. Not every host or interface provides a second output, so the
// default (Auto) falls back to the main output rather than leaving the beat silent.
// Header-only and free of JUCE so the rule can be checked by the offline tests.

#include <algorithm>

namespace bitvibe
{
// The "Beat Output" setting.
enum class BeatOutput
{
    Auto = 0,    // the Beat Monitor output when it is connected, otherwise the main output
    Monitor = 1, // only the Beat Monitor output (silent when it is not connected)
    Main = 2     // only the main output
};

// Where the sound goes right now.
enum class BeatRoute
{
    Silent,
    Main,
    Monitor
};

inline BeatOutput beatOutputFromIndex(int index)
{
    return (BeatOutput) std::clamp(index, 0, 2);
}

inline BeatRoute chooseBeatRoute(BeatOutput mode, bool monitorConnected)
{
    switch (mode)
    {
        case BeatOutput::Main:
            return BeatRoute::Main;
        case BeatOutput::Monitor:
            return monitorConnected ? BeatRoute::Monitor : BeatRoute::Silent;
        case BeatOutput::Auto:
            break;
    }
    return monitorConnected ? BeatRoute::Monitor : BeatRoute::Main;
}
} // namespace bitvibe
