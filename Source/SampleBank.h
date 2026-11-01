#pragma once

#include "HandOver.h"
#include "dsp/Samples.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

#include <array>

// The sounds the sample blend chooses from: the audio files found in the sounds folders, the list of them the
// player has picked, named sound banks (saved lists) and what the player last set, remembered across sessions.
//
// There are two lists. The MAIN one plays on every step; the LOOP one, on the steps of the sequence's loop section,
// when the player gives the loop section sounds of its own (until then it plays the main list). Calls that take a
// section use 0 for main and 1 for the loop section.
//
// Remembered settings live in one small XML file in the user's application-data folder (see settingsFile()): the
// last picked lists, the banks, and the sample knobs the player last set. A new instance of the plug-in starts from
// them; a saved project brings its own lists back instead (and does not overwrite them).
//
// Message-thread side: scanning folders, decoding, the lists and the file. The audio thread only calls
// beginBlock() to get the newest immutable SampleSet of each list.
class SampleBank
{
public:
    static constexpr int kMain = 0, kLoop = 1;

    SampleBank();

    // Documents/BitVibe/Sounds, and (local builds only) the source tree's GoodSounds folder.
    static juce::Array<juce::File> libraryFolders();
    static juce::File userFolder();
    // Every usable audio file in those folders and their subfolders, sorted by name (a name found twice is listed once:
    // lists and banks refer to sounds by file name).
    static juce::Array<juce::File> libraryFiles();
    // The subfolder a sound sits in, relative to its sounds folder ("Hits/Metal"), or "" for one at the top.
    static juce::String groupOf(const juce::File& sound);

    // Where the remembered settings are kept. Tests point this somewhere else so they never touch the real file
    // (the environment variable BITVIBE_SETTINGS_DIR does the same for a hosted plug-in).
    static void setSettingsFolderOverride(const juce::File& folder);
    // The same for the player's sounds folder (the sampler saves into it).
    static void setSoundsFolderOverride(const juce::File& folder);
    // Tests: leave out the source tree's sounds folder that local builds also read, so the library can be empty.
    static void setRepoSoundsEnabled(bool enabled);
    static juce::File settingsFolder();
    static juce::File settingsFile();

    // ---- the picked lists ------------------------------------------------------------------------
    // The loop section's list is the main one until it gets its own (picking sounds or loading a bank for it).
    juce::StringArray getSelected(int section = kMain) const;
    int getNumSelected(int section = kMain) const; // picked and found in the folders
    // remember = false for a list that comes from a project (the remembered one stays as the player left it).
    // Setting the loop section's list gives it its own sounds.
    void setSelected(const juce::StringArray& fileNames, bool remember = true, int section = kMain);
    void selectAll(int section = kMain);
    void selectNone(int section = kMain) { setSelected({}, true, section); }
    bool loopHasOwnSounds() const;
    void setLoopOwnSounds(bool own, bool remember = true); // false: the loop section plays the main list again

    // ---- sound banks: named, saved lists ---------------------------------------------------------
    juce::StringArray getBankNames() const;
    juce::String getActiveBank(int section = kMain) const;  // the bank last saved or loaded for that list ("" = none)
    juce::StringArray getBankFiles(const juce::String& name) const;
    bool selectionMatchesActiveBank(int section = kMain) const; // false once the list has been changed after loading it
    bool saveBank(const juce::String& name, int section = kMain); // that list under this name (replaces one of that name)
    bool loadBank(const juce::String& name, int section = kMain); // makes that bank the list
    void deleteBank(const juce::String& name);
    // Adds a sound (by file name) to a saved bank, and to the list in use when that bank is the one showing. False
    // when there is no bank of that name.
    bool addToBank(const juce::String& bankName, const juce::String& fileName);

    // The sampler marks the WAV files it saves with this description: the sample blend then keeps their level (the
    // sampler has already set it) instead of levelling them like any other sound.
    static constexpr const char* kKeepLevelMark = "BitVibe sampler: keep level";

    // ---- the knobs ------------------------------------------------------------------------------
    struct Levels
    {
        float mix = 0.0f, chance = 1.0f, baseDb = -60.0f, maxDb = -9.0f;
        // the loop section's own knobs, used when loopOwn is set (otherwise it follows the ones above)
        bool loopOwn = false;
        float loopMix = 0.0f, loopChance = 1.0f, loopBaseDb = -60.0f, loopMaxDb = -9.0f;
    };
    void rememberLevels(const Levels&);
    bool getRememberedLevels(Levels&) const;

    // How long the sound that a preview pick selects lasts, in seconds (0 if nothing is picked).
    double getClipSeconds(std::uint32_t pick, int section = kMain) const;

    // ---- project state --------------------------------------------------------------------------
    juce::ValueTree toState() const;
    void fromState(const juce::ValueTree&); // no list in it (an older project): the current lists are kept

    // Audio thread: the set to play from this block (nullptr until the first one is published). For the loop
    // section, nullptr while it plays the main list.
    const bitvibe::SampleSet* beginBlock(int section = kMain);

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

    static int clampSection(int s) { return s == kLoop ? kLoop : kMain; }
    int listFor(int section) const; // the list a section plays: the loop section without its own sounds plays the main one
    void rebuild(int section);
    void loadSettings();
    void saveSettings() const;
    static juce::String cleanName(const juce::String& name);

    mutable juce::CriticalSection lock;
    std::array<juce::StringArray, 2> selected;
    bool loopOwn = false;
    std::vector<Bank> banks;
    std::array<juce::String, 2> activeBank;
    Levels remembered;
    bool haveLevels = false;
    std::array<std::vector<double>, 2> clipSeconds; // of the loaded sounds, in the order of the set the audio thread plays from
    std::array<int, 2> numLoaded {};
    std::array<HandOver<Holder>, 2> slot;
    std::atomic<bool> loopOwnForAudio { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleBank)
};
