#include "SampleBank.h"

#include <algorithm>

using namespace juce;

namespace
{
constexpr double kMaxSeconds = 20.0;     // longer files are cut: these are short sounds, not songs
constexpr float kNormalisedPeak = 0.8f;  // every sound is brought to the same peak so the mix knob means the same for all
constexpr int kMaxBanks = 64;

File& settingsOverride()
{
    static File folder;
    return folder;
}

bool isSoundFile(const File& f)
{
    static const StringArray extensions { ".wav", ".wave", ".aif", ".aiff", ".flac", ".ogg" };
    return f.existsAsFile() && !f.getFileName().startsWithChar('.') && extensions.contains(f.getFileExtension(), true);
}

// Decodes a file to mono at its own sample rate, normalised. Empty when it cannot be read or is silent.
bitvibe::SampleClip loadClip(AudioFormatManager& formats, const File& file)
{
    bitvibe::SampleClip clip;
    std::unique_ptr<AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return clip;

    const int numSamples = (int) jmin<int64>(reader->lengthInSamples, (int64) (kMaxSeconds * reader->sampleRate));
    AudioBuffer<float> buffer((int) jmax<unsigned>(1u, reader->numChannels), numSamples);
    reader->read(&buffer, 0, numSamples, 0, true, true);

    clip.rate = reader->sampleRate;
    clip.data.assign((size_t) numSamples, 0.0f);
    const int channels = buffer.getNumChannels();
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c)
            sum += buffer.getSample(c, i);
        clip.data[(size_t) i] = sum / (float) channels;
        peak = jmax(peak, std::abs(clip.data[(size_t) i]));
    }
    if (!(peak > 1.0e-5f))
    {
        clip.data.clear();
        return clip;
    }
    // Sounds saved by the sampler already sit at the level the player chose; everything else is levelled.
    const bool keepLevel = reader->metadataValues.getValue(WavAudioFormat::bwavDescription, {}) == SampleBank::kKeepLevelMark;
    const float gain = keepLevel ? jmin(1.0f, 1.0f / peak) : kNormalisedPeak / peak;
    for (auto& v : clip.data)
        v *= gain;
    return clip;
}

void addFiles(XmlElement& parent, const StringArray& files)
{
    for (const auto& f : files)
        parent.createNewChildElement("FILE")->setAttribute("name", f);
}

StringArray readFiles(const XmlElement& parent)
{
    StringArray files;
    for (auto* f : parent.getChildWithTagNameIterator("FILE"))
        if (f->getStringAttribute("name").isNotEmpty())
            files.add(f->getStringAttribute("name"));
    return files;
}
} // namespace

SampleBank::SampleBank()
{
    loadSettings();
    rebuild(kMain);
    rebuild(kLoop);
}

namespace
{
File& soundsOverride()
{
    static File folder;
    return folder;
}
} // namespace

void SampleBank::setSoundsFolderOverride(const File& folder)
{
    soundsOverride() = folder;
}

namespace
{
bool& repoSoundsEnabled()
{
    static bool enabled = true;
    return enabled;
}
} // namespace

void SampleBank::setRepoSoundsEnabled(bool enabled)
{
    repoSoundsEnabled() = enabled;
}

File SampleBank::userFolder()
{
    if (soundsOverride() != File())
        return soundsOverride();
    return File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("BitVibe").getChildFile("Sounds");
}

Array<File> SampleBank::libraryFolders()
{
    Array<File> folders;
    folders.add(userFolder());
   #ifdef BITVIBE_SOUNDS_DIR
    const File repoSounds(BITVIBE_SOUNDS_DIR); // the source tree's GoodSounds folder (local builds only)
    if (repoSounds.isDirectory() && repoSoundsEnabled())
        folders.add(repoSounds);
   #endif
    return folders;
}

Array<File> SampleBank::libraryFiles()
{
    Array<File> files;
    StringArray seen;
    for (const auto& folder : libraryFolders())
        if (folder.isDirectory())
            for (const auto& f : folder.findChildFiles(File::findFiles, true, "*"))
                if (isSoundFile(f) && !seen.contains(f.getFileName()))
                {
                    seen.add(f.getFileName());
                    files.add(f);
                }
    struct ByName
    {
        static int compareElements(const File& a, const File& b) { return a.getFileName().compareNatural(b.getFileName()); }
    };
    ByName byName;
    files.sort(byName);
    return files;
}

String SampleBank::groupOf(const File& sound)
{
    for (const auto& folder : libraryFolders())
        if (sound.isAChildOf(folder))
        {
            const auto parent = sound.getParentDirectory();
            return parent == folder ? String() : parent.getRelativePathFrom(folder).replaceCharacter('\\', '/');
        }
    return {};
}

// ---- remembered settings ------------------------------------------------------------------------

void SampleBank::setSettingsFolderOverride(const File& folder)
{
    settingsOverride() = folder;
}

File SampleBank::settingsFolder()
{
    if (settingsOverride() != File())
        return settingsOverride();
    const auto env = SystemStats::getEnvironmentVariable("BITVIBE_SETTINGS_DIR", {});
    if (env.isNotEmpty())
        return File(env);
   #if JUCE_WINDOWS
    return File::getSpecialLocation(File::windowsLocalAppData).getChildFile("BitVibe");
   #elif JUCE_MAC
    return File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("Application Support/BitVibe");
   #else
    return File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("BitVibe");
   #endif
}

File SampleBank::settingsFile()
{
    return settingsFolder().getChildFile("sample_settings.xml");
}

String SampleBank::cleanName(const String& name)
{
    StringArray words;
    words.addTokens(name, " ", "");
    words.removeEmptyStrings();
    return words.joinIntoString(" ").retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-.()+&'#").substring(0, 32).trim();
}

void SampleBank::loadSettings()
{
    const ScopedLock sl(lock);
    banks.clear();
    activeBank = {};
    haveLevels = false;
    loopOwn = false;
    selected[kLoop].clear();

    std::unique_ptr<XmlElement> xml(XmlDocument::parse(settingsFile()));
    if (xml == nullptr || !xml->hasTagName("BITVIBE_SAMPLES"))
    {
        // First run: every sound found is picked.
        selected[kMain].clear();
        for (const auto& f : libraryFiles())
            selected[kMain].add(f.getFileName());
        loopOwnForAudio.store(false);
        return;
    }

    selected[kMain].clear();
    if (auto* last = xml->getChildByName("LAST"))
        selected[kMain] = readFiles(*last);
    if (auto* last = xml->getChildByName("LAST_LOOP"))
        selected[kLoop] = readFiles(*last);
    loopOwn = xml->getBoolAttribute("loopOwnSounds", false);
    activeBank[kMain] = xml->getStringAttribute("activeBank");
    activeBank[kLoop] = xml->getStringAttribute("loopActiveBank");
    if (xml->hasAttribute("mix"))
    {
        auto level = [&](const char* name, float fallback, float lo, float hi) { return jlimit(lo, hi, (float) xml->getDoubleAttribute(name, fallback)); };
        remembered.mix = level("mix", 0.0f, 0.0f, 1.0f);
        remembered.chance = level("chance", 1.0f, 0.0f, 1.0f);
        remembered.baseDb = level("baseDb", -60.0f, -60.0f, 6.0f);
        remembered.maxDb = level("maxDb", -9.0f, -60.0f, 6.0f);
        remembered.loopOwn = xml->getBoolAttribute("loopOwnLevels", false);
        remembered.loopMix = level("loopMix", remembered.mix, 0.0f, 1.0f);
        remembered.loopChance = level("loopChance", remembered.chance, 0.0f, 1.0f);
        remembered.loopBaseDb = level("loopBaseDb", remembered.baseDb, -60.0f, 6.0f);
        remembered.loopMaxDb = level("loopMaxDb", remembered.maxDb, -60.0f, 6.0f);
        haveLevels = true;
    }
    for (auto* b : xml->getChildWithTagNameIterator("BANK"))
    {
        const auto name = cleanName(b->getStringAttribute("name"));
        if (name.isNotEmpty() && (int) banks.size() < kMaxBanks)
            banks.push_back({ name, readFiles(*b) });
    }
    loopOwnForAudio.store(loopOwn);
}

void SampleBank::saveSettings() const
{
    XmlElement xml("BITVIBE_SAMPLES");
    {
        const ScopedLock sl(lock);
        xml.setAttribute("activeBank", activeBank[kMain]);
        xml.setAttribute("loopActiveBank", activeBank[kLoop]);
        xml.setAttribute("loopOwnSounds", loopOwn);
        if (haveLevels)
        {
            xml.setAttribute("mix", (double) remembered.mix);
            xml.setAttribute("chance", (double) remembered.chance);
            xml.setAttribute("baseDb", (double) remembered.baseDb);
            xml.setAttribute("maxDb", (double) remembered.maxDb);
            xml.setAttribute("loopOwnLevels", remembered.loopOwn);
            xml.setAttribute("loopMix", (double) remembered.loopMix);
            xml.setAttribute("loopChance", (double) remembered.loopChance);
            xml.setAttribute("loopBaseDb", (double) remembered.loopBaseDb);
            xml.setAttribute("loopMaxDb", (double) remembered.loopMaxDb);
        }
        addFiles(*xml.createNewChildElement("LAST"), selected[kMain]);
        addFiles(*xml.createNewChildElement("LAST_LOOP"), selected[kLoop]);
        for (const auto& b : banks)
        {
            auto* e = xml.createNewChildElement("BANK");
            e->setAttribute("name", b.name);
            addFiles(*e, b.files);
        }
    }
    const auto file = settingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(xml.toString());
}

void SampleBank::rememberLevels(const Levels& levels)
{
    {
        const ScopedLock sl(lock);
        remembered = levels;
        haveLevels = true;
    }
    saveSettings();
}

bool SampleBank::getRememberedLevels(Levels& levels) const
{
    const ScopedLock sl(lock);
    levels = remembered;
    return haveLevels;
}

double SampleBank::getClipSeconds(std::uint32_t pick, int section) const
{
    const ScopedLock sl(lock);
    const auto& seconds = clipSeconds[(size_t) listFor(section)];
    return seconds.empty() ? 0.0 : seconds[pick % seconds.size()];
}

// ---- the picked lists ---------------------------------------------------------------------------

int SampleBank::listFor(int section) const
{
    return clampSection(section) == kLoop && loopOwn ? kLoop : kMain;
}

StringArray SampleBank::getSelected(int section) const
{
    const ScopedLock sl(lock);
    return selected[(size_t) listFor(section)];
}

int SampleBank::getNumSelected(int section) const
{
    const ScopedLock sl(lock);
    return numLoaded[(size_t) listFor(section)];
}

bool SampleBank::loopHasOwnSounds() const
{
    const ScopedLock sl(lock);
    return loopOwn;
}

void SampleBank::setLoopOwnSounds(bool own, bool remember)
{
    {
        const ScopedLock sl(lock);
        if (own && !loopOwn && selected[kLoop].isEmpty())
            selected[kLoop] = selected[kMain]; // starts from the main list
        loopOwn = own;
    }
    rebuild(kLoop);
    if (remember)
        saveSettings();
}

void SampleBank::setSelected(const StringArray& fileNames, bool remember, int section)
{
    section = clampSection(section);
    {
        const ScopedLock sl(lock);
        selected[(size_t) section] = fileNames;
        if (section == kLoop)
            loopOwn = true;
    }
    rebuild(section);
    if (remember)
        saveSettings();
}

void SampleBank::selectAll(int section)
{
    StringArray names;
    for (const auto& f : libraryFiles())
        names.add(f.getFileName());
    setSelected(names, true, section);
}

// Decodes a list's sounds and hands the set to the audio thread. The loop section's set is published empty-handed
// (nullptr for the audio thread) while it plays the main list.
void SampleBank::rebuild(int section)
{
    section = clampSection(section);
    StringArray names;
    bool own = true;
    {
        const ScopedLock sl(lock);
        names = selected[(size_t) section];
        if (section == kLoop)
            own = loopOwn;
    }
    auto holder = std::make_unique<Holder>();
    std::vector<double> seconds;
    if (own)
    {
        AudioFormatManager formats;
        formats.registerBasicFormats();
        for (const auto& f : libraryFiles())
        {
            if (!names.contains(f.getFileName()))
                continue;
            auto clip = loadClip(formats, f);
            if (!clip.data.empty())
            {
                seconds.push_back((double) clip.data.size() / clip.rate);
                holder->set.clips.push_back(std::move(clip));
            }
        }
    }
    {
        const ScopedLock sl(lock);
        numLoaded[(size_t) section] = (int) holder->set.clips.size();
        clipSeconds[(size_t) section] = std::move(seconds);
    }
    slot[(size_t) section].publish(std::move(holder));
    if (section == kLoop)
        loopOwnForAudio.store(own, std::memory_order_release);
}

// ---- sound banks --------------------------------------------------------------------------------

StringArray SampleBank::getBankNames() const
{
    const ScopedLock sl(lock);
    StringArray names;
    for (const auto& b : banks)
        names.add(b.name);
    return names;
}

String SampleBank::getActiveBank(int section) const
{
    const ScopedLock sl(lock);
    return activeBank[(size_t) listFor(section)];
}

StringArray SampleBank::getBankFiles(const String& name) const
{
    const ScopedLock sl(lock);
    for (const auto& b : banks)
        if (b.name.equalsIgnoreCase(name))
            return b.files;
    return {};
}

bool SampleBank::selectionMatchesActiveBank(int section) const
{
    const ScopedLock sl(lock);
    const auto list = (size_t) listFor(section);
    for (const auto& b : banks)
        if (b.name == activeBank[list])
        {
            auto a = b.files, c = selected[list];
            a.sort(true);
            c.sort(true);
            return a == c;
        }
    return false;
}

bool SampleBank::saveBank(const String& rawName, int section)
{
    const auto name = cleanName(rawName);
    if (name.isEmpty())
        return false;
    {
        const ScopedLock sl(lock);
        const auto list = (size_t) listFor(section);
        bool replaced = false;
        for (auto& b : banks)
            if (b.name.equalsIgnoreCase(name))
            {
                b.name = name;
                b.files = selected[list];
                replaced = true;
            }
        if (!replaced)
        {
            if ((int) banks.size() >= kMaxBanks)
                return false;
            banks.push_back({ name, selected[list] });
        }
        activeBank[list] = name;
    }
    saveSettings();
    return true;
}

bool SampleBank::loadBank(const String& name, int section)
{
    section = clampSection(section);
    StringArray files;
    {
        const ScopedLock sl(lock);
        bool found = false;
        for (const auto& b : banks)
            if (b.name.equalsIgnoreCase(name))
            {
                files = b.files;
                activeBank[(size_t) section] = b.name;
                found = true;
            }
        if (!found)
            return false;
    }
    setSelected(files, true, section); // also saves (and gives the loop section its own sounds)
    return true;
}

bool SampleBank::addToBank(const String& bankName, const String& fileName)
{
    std::array<bool, 2> showing { false, false };
    {
        const ScopedLock sl(lock);
        auto it = std::find_if(banks.begin(), banks.end(), [&](const Bank& b) { return b.name.equalsIgnoreCase(bankName); });
        if (it == banks.end())
            return false;
        for (int s = 0; s < 2; ++s)
        {
            const auto list = (size_t) s;
            if (activeBank[list] == it->name && (s == kMain || loopOwn))
            {
                auto a = it->files, c = selected[list];
                a.sort(true);
                c.sort(true);
                showing[list] = a == c; // the list in use is that bank, unchanged
            }
        }
        it->files.addIfNotAlreadyThere(fileName);
        for (int s = 0; s < 2; ++s)
            if (showing[(size_t) s])
                selected[(size_t) s].addIfNotAlreadyThere(fileName);
    }
    for (int s = 0; s < 2; ++s)
        if (showing[(size_t) s])
            rebuild(s);
    saveSettings();
    return true;
}

void SampleBank::deleteBank(const String& name)
{
    {
        const ScopedLock sl(lock);
        banks.erase(std::remove_if(banks.begin(), banks.end(), [&](const Bank& b) { return b.name.equalsIgnoreCase(name); }), banks.end());
        for (auto& active : activeBank)
            if (active.equalsIgnoreCase(name))
                active = {};
    }
    saveSettings();
}

// ---- project state ------------------------------------------------------------------------------

ValueTree SampleBank::toState() const
{
    ValueTree t("SAMPLES");
    const ScopedLock sl(lock);
    t.setProperty("selected", selected[kMain].joinIntoString("|"), nullptr);
    t.setProperty("bank", activeBank[kMain], nullptr);
    t.setProperty("loopOwn", loopOwn, nullptr);
    t.setProperty("loopSelected", selected[kLoop].joinIntoString("|"), nullptr);
    t.setProperty("loopBank", activeBank[kLoop], nullptr);
    return t;
}

void SampleBank::fromState(const ValueTree& t)
{
    if (!t.isValid() || !t.hasProperty("selected"))
        return; // a project from before the sample blend: whatever is picked now stays
    auto list = [&](const char* key)
    {
        StringArray names;
        names.addTokens(t.getProperty(key).toString(), "|", "");
        names.removeEmptyStrings();
        return names;
    };
    {
        const ScopedLock sl(lock);
        activeBank[kMain] = t.getProperty("bank").toString();
        activeBank[kLoop] = t.getProperty("loopBank").toString();
        selected[kMain] = list("selected");
        selected[kLoop] = list("loopSelected");
        loopOwn = (bool) t.getProperty("loopOwn", false); // a project from before the loop section: the main list
    }
    // the project's lists; the remembered ones are not touched
    rebuild(kMain);
    rebuild(kLoop);
}

const bitvibe::SampleSet* SampleBank::beginBlock(int section)
{
    if (clampSection(section) == kLoop && !loopOwnForAudio.load(std::memory_order_acquire))
        return nullptr;
    const Holder* h = slot[(size_t) clampSection(section)].get();
    return h != nullptr ? &h->set : nullptr;
}
