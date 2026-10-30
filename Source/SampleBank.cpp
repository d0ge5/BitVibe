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
    const float gain = kNormalisedPeak / peak;
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
    rebuild();
}

File SampleBank::userFolder()
{
    return File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("BitVibe").getChildFile("Sounds");
}

Array<File> SampleBank::libraryFolders()
{
    Array<File> folders;
    folders.add(userFolder());
   #ifdef BITVIBE_SOUNDS_DIR
    const File repoSounds(BITVIBE_SOUNDS_DIR); // the source tree's GoodSounds folder (local builds only)
    if (repoSounds.isDirectory())
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
            for (const auto& f : folder.findChildFiles(File::findFiles, false, "*"))
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

    std::unique_ptr<XmlElement> xml(XmlDocument::parse(settingsFile()));
    if (xml == nullptr || !xml->hasTagName("BITVIBE_SAMPLES"))
    {
        // First run: every sound found is picked.
        selected.clear();
        for (const auto& f : libraryFiles())
            selected.add(f.getFileName());
        return;
    }

    selected.clear();
    if (auto* last = xml->getChildByName("LAST"))
        selected = readFiles(*last);
    activeBank = xml->getStringAttribute("activeBank");
    if (xml->hasAttribute("mix"))
    {
        remembered.mix = jlimit(0.0f, 1.0f, (float) xml->getDoubleAttribute("mix", 0.0));
        remembered.chance = jlimit(0.0f, 1.0f, (float) xml->getDoubleAttribute("chance", 1.0));
        remembered.baseDb = jlimit(-60.0f, 6.0f, (float) xml->getDoubleAttribute("baseDb", -60.0));
        remembered.maxDb = jlimit(-60.0f, 6.0f, (float) xml->getDoubleAttribute("maxDb", -9.0));
        haveLevels = true;
    }
    for (auto* b : xml->getChildWithTagNameIterator("BANK"))
    {
        const auto name = cleanName(b->getStringAttribute("name"));
        if (name.isNotEmpty() && (int) banks.size() < kMaxBanks)
            banks.push_back({ name, readFiles(*b) });
    }
}

void SampleBank::saveSettings() const
{
    XmlElement xml("BITVIBE_SAMPLES");
    {
        const ScopedLock sl(lock);
        xml.setAttribute("activeBank", activeBank);
        if (haveLevels)
        {
            xml.setAttribute("mix", (double) remembered.mix);
            xml.setAttribute("chance", (double) remembered.chance);
            xml.setAttribute("baseDb", (double) remembered.baseDb);
            xml.setAttribute("maxDb", (double) remembered.maxDb);
        }
        addFiles(*xml.createNewChildElement("LAST"), selected);
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

double SampleBank::getClipSeconds(std::uint32_t pick) const
{
    const ScopedLock sl(lock);
    return clipSeconds.empty() ? 0.0 : clipSeconds[pick % clipSeconds.size()];
}

// ---- the picked list ----------------------------------------------------------------------------

StringArray SampleBank::getSelected() const
{
    const ScopedLock sl(lock);
    return selected;
}

int SampleBank::getNumSelected() const
{
    const ScopedLock sl(lock);
    return numLoaded;
}

void SampleBank::setSelected(const StringArray& fileNames, bool remember)
{
    {
        const ScopedLock sl(lock);
        selected = fileNames;
    }
    rebuild();
    if (remember)
        saveSettings();
}

void SampleBank::selectAll()
{
    StringArray names;
    for (const auto& f : libraryFiles())
        names.add(f.getFileName());
    setSelected(names);
}

void SampleBank::rebuild()
{
    StringArray names;
    {
        const ScopedLock sl(lock);
        names = selected;
    }
    AudioFormatManager formats;
    formats.registerBasicFormats();

    auto holder = std::make_unique<Holder>();
    std::vector<double> seconds;
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
    {
        const ScopedLock sl(lock);
        numLoaded = (int) holder->set.clips.size();
        clipSeconds = std::move(seconds);
    }
    slot.publish(std::move(holder));
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

String SampleBank::getActiveBank() const
{
    const ScopedLock sl(lock);
    return activeBank;
}

StringArray SampleBank::getBankFiles(const String& name) const
{
    const ScopedLock sl(lock);
    for (const auto& b : banks)
        if (b.name.equalsIgnoreCase(name))
            return b.files;
    return {};
}

bool SampleBank::selectionMatchesActiveBank() const
{
    const ScopedLock sl(lock);
    for (const auto& b : banks)
        if (b.name == activeBank)
        {
            auto a = b.files, c = selected;
            a.sort(true);
            c.sort(true);
            return a == c;
        }
    return false;
}

bool SampleBank::saveBank(const String& rawName)
{
    const auto name = cleanName(rawName);
    if (name.isEmpty())
        return false;
    {
        const ScopedLock sl(lock);
        bool replaced = false;
        for (auto& b : banks)
            if (b.name.equalsIgnoreCase(name))
            {
                b.name = name;
                b.files = selected;
                replaced = true;
            }
        if (!replaced)
        {
            if ((int) banks.size() >= kMaxBanks)
                return false;
            banks.push_back({ name, selected });
        }
        activeBank = name;
    }
    saveSettings();
    return true;
}

bool SampleBank::loadBank(const String& name)
{
    StringArray files;
    String exact;
    {
        const ScopedLock sl(lock);
        bool found = false;
        for (const auto& b : banks)
            if (b.name.equalsIgnoreCase(name))
            {
                files = b.files;
                exact = b.name;
                found = true;
            }
        if (!found)
            return false;
        activeBank = exact;
    }
    setSelected(files); // also saves
    return true;
}

void SampleBank::deleteBank(const String& name)
{
    {
        const ScopedLock sl(lock);
        banks.erase(std::remove_if(banks.begin(), banks.end(), [&](const Bank& b) { return b.name.equalsIgnoreCase(name); }), banks.end());
        if (activeBank.equalsIgnoreCase(name))
            activeBank = {};
    }
    saveSettings();
}

// ---- project state ------------------------------------------------------------------------------

ValueTree SampleBank::toState() const
{
    ValueTree t("SAMPLES");
    t.setProperty("selected", getSelected().joinIntoString("|"), nullptr);
    t.setProperty("bank", getActiveBank(), nullptr);
    return t;
}

void SampleBank::fromState(const ValueTree& t)
{
    if (!t.isValid() || !t.hasProperty("selected"))
        return; // a project from before the sample blend: whatever is picked now stays
    StringArray names;
    names.addTokens(t.getProperty("selected").toString(), "|", "");
    names.removeEmptyStrings();
    {
        const ScopedLock sl(lock);
        activeBank = t.getProperty("bank").toString();
    }
    setSelected(names, false); // the project's list; the remembered one is not touched
}

const bitvibe::SampleSet* SampleBank::beginBlock()
{
    const Holder* h = slot.get();
    return h != nullptr ? &h->set : nullptr;
}
