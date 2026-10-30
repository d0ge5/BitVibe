#pragma once

#include "HandOver.h"
#include "dsp/Samples.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

// The sounds the sample blend chooses from: the audio files found in the sounds folders, the list of them the
// player has picked, named sound banks (saved lists) and what the player last set, remembered across sessions.
//
// Remembered settings live in one small XML file in the user's application-data folder (see settingsFile()): the
// last picked list, the banks, and the sample MIX and CHANCE the player last set with the knobs. A new instance
// of the plug-in starts from them; a saved project brings its own list back instead (and does not overwrite them).
//
// Message-thread side: scanning folders, decoding, the lists and the file. The audio thread only calls
// beginBlock() to get the newest immutable SampleSet.
class SampleBank
{
public:
    SampleBank();

    // Documents/BitVibe/Sounds, and (local builds only) the source tree's GoodSounds folder.
    static juce::Array<juce::File> libraryFolders();
    static juce::File userFolder();
    // Every usable audio file in those folders, sorted by name (a name found twice is listed once).
    static juce::Array<juce::File> libraryFiles();

    // Where the remembered settings are kept. Tests point this somewhere else so they never touch the real file
    // (the environment variable BITVIBE_SETTINGS_DIR does the same for a hosted plug-in).
    static void setSettingsFolderOverride(const juce::File& folder);
    static juce::File settingsFolder();
    static juce::File settingsFile();

    // ---- the picked list -------------------------------------------------------------------------
    juce::StringArray getSelected() const;
    int getNumSelected() const; // picked and found in the folders
    // remember = false for a list that comes from a project (the remembered one stays as the player left it).
    void setSelected(const juce::StringArray& fileNames, bool remember = true);
    void selectAll();
    void selectNone() { setSelected({}); }

    // ---- sound banks: named, saved lists ---------------------------------------------------------
    juce::StringArray getBankNames() const;
    juce::String getActiveBank() const;                  // the bank last saved or loaded ("" = none)
    juce::StringArray getBankFiles(const juce::String& name) const;
    bool selectionMatchesActiveBank() const;             // false once the list has been changed after loading it
    bool saveBank(const juce::String& name);             // the current list under this name (replaces one of that name)
    bool loadBank(const juce::String& name);             // makes that list the current one
    void deleteBank(const juce::String& name);

    // ---- the knobs ------------------------------------------------------------------------------
    void rememberLevels(float mix, float chance);
    bool getRememberedLevels(float& mix, float& chance) const;

    // ---- project state --------------------------------------------------------------------------
    juce::ValueTree toState() const;
    void fromState(const juce::ValueTree&); // no list in it (an older project): the current list is kept

    // Audio thread: the set to play from this block (nullptr until the first one is published).
    const bitvibe::SampleSet* beginBlock();

private:
    struct Holder
    {
        bitvibe::SampleSet set;
        Holder* nextRetired = nullptr;
    };
    struct Bank
    {
        juce::String name;
        juce::StringArray files;
    };

    void rebuild();
    void loadSettings();
    void saveSettings() const;
    static juce::String cleanName(const juce::String& name);

    mutable juce::CriticalSection lock;
    juce::StringArray selected;
    std::vector<Bank> banks;
    juce::String activeBank;
    float rememberedMix = 0.0f, rememberedChance = 1.0f;
    bool haveLevels = false;
    int numLoaded = 0;
    HandOver<Holder> slot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleBank)
};
