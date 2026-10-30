#pragma once

#include "HandOver.h"
#include "dsp/Samples.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

// The sounds the sample blend chooses from: the audio files found in the sounds folders and the list of them
// the player has picked. Message-thread side: scanning folders, decoding, the picked list and its state. The
// audio thread only calls beginBlock() to get the newest immutable SampleSet.
class SampleBank
{
public:
    SampleBank();

    // Documents/BitVibe/Sounds, and (local builds only) the source tree's GoodSounds folder.
    static juce::Array<juce::File> libraryFolders();
    static juce::File userFolder();
    // Every usable audio file in those folders, sorted by name (a name found twice is listed once).
    static juce::Array<juce::File> libraryFiles();

    juce::StringArray getSelected() const;
    int getNumSelected() const;          // picked and found in the folders
    void setSelected(const juce::StringArray& fileNames);
    void selectAll();
    void selectNone() { setSelected({}); }

    juce::ValueTree toState() const;
    void fromState(const juce::ValueTree&); // no list saved (an older project): everything is picked

    // Audio thread: the set to play from this block (nullptr until the first one is published).
    const bitvibe::SampleSet* beginBlock();

private:
    struct Holder
    {
        bitvibe::SampleSet set;
        Holder* nextRetired = nullptr;
    };

    void rebuild();

    mutable juce::CriticalSection lock;
    juce::StringArray selected;
    int numLoaded = 0;
    HandOver<Holder> slot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleBank)
};
