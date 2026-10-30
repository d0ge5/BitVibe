#include "SampleBank.h"

using namespace juce;

namespace
{
constexpr double kMaxSeconds = 20.0;     // longer files are cut: these are short sounds, not songs
constexpr float kNormalisedPeak = 0.8f;  // every sound is brought to the same peak so the mix knob means the same for all

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
} // namespace

SampleBank::SampleBank()
{
    selected = [] { StringArray names; for (const auto& f : libraryFiles()) names.add(f.getFileName()); return names; }();
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

void SampleBank::setSelected(const StringArray& fileNames)
{
    {
        const ScopedLock sl(lock);
        selected = fileNames;
    }
    rebuild();
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
    for (const auto& f : libraryFiles())
    {
        if (!names.contains(f.getFileName()))
            continue;
        auto clip = loadClip(formats, f);
        if (!clip.data.empty())
            holder->set.clips.push_back(std::move(clip));
    }
    {
        const ScopedLock sl(lock);
        numLoaded = (int) holder->set.clips.size();
    }
    slot.publish(std::move(holder));
}

ValueTree SampleBank::toState() const
{
    ValueTree t("SAMPLES");
    t.setProperty("selected", getSelected().joinIntoString("|"), nullptr);
    return t;
}

void SampleBank::fromState(const ValueTree& t)
{
    if (!t.isValid() || !t.hasProperty("selected"))
    {
        selectAll();
        return;
    }
    StringArray names;
    names.addTokens(t.getProperty("selected").toString(), "|", "");
    names.removeEmptyStrings();
    setSelected(names);
}

const bitvibe::SampleSet* SampleBank::beginBlock()
{
    const Holder* h = slot.get();
    return h != nullptr ? &h->set : nullptr;
}
