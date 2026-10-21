#include "DrumBeat.h"

using namespace juce;
using namespace bitvibe;

namespace
{
// Kick restarts the sequence (what a pick attack does to it); everything else starts switched off.
DrumSettingsArray defaultSettings()
{
    DrumSettingsArray s {};
    s[(size_t) Drum::Kick].action = DrumAction::RestartSequence;
    return s;
}

// Hits as compact text for the saved state: "time drum level confidence excluded;" per hit.
String hitsToText(const std::vector<DrumHit>& hits)
{
    String out;
    out.preallocateBytes(hits.size() * 28);
    for (const auto& h : hits)
        out << String(h.time, 4) << ' ' << drumId(h.drum) << ' ' << String(h.levelDb, 2) << ' '
            << String(h.confidence, 3) << ' ' << (h.excluded ? 1 : 0) << ';';
    return out;
}

std::vector<DrumHit> hitsFromText(const String& text)
{
    std::vector<DrumHit> hits;
    for (const auto& item : StringArray::fromTokens(text, ";", {}))
    {
        const auto parts = StringArray::fromTokens(item.trim(), " ", {});
        if (parts.size() < 5)
            continue;
        DrumHit h;
        if (!drumFromId(parts[1].toStdString(), h.drum))
            continue;
        h.time = parts[0].getDoubleValue();
        h.levelDb = parts[2].getFloatValue();
        h.confidence = parts[3].getFloatValue();
        h.excluded = parts[4].getIntValue() != 0;
        if (!std::isfinite(h.time) || h.time < 0.0 || h.time > DrumBeat::kMaxSeconds)
            continue;
        h.levelDb = std::isfinite(h.levelDb) ? jlimit(kMinLevelFloorDb, 0.0f, h.levelDb) : kMinLevelFloorDb;
        h.confidence = std::isfinite(h.confidence) ? jlimit(0.0f, 1.0f, h.confidence) : 0.0f;
        hits.push_back(h);
    }
    std::stable_sort(hits.begin(), hits.end(), [](const DrumHit& a, const DrumHit& b) { return a.time < b.time; });
    return hits;
}

std::vector<float> mixToMono(const AudioBuffer<float>& in)
{
    std::vector<float> mono((size_t) in.getNumSamples(), 0.0f);
    const int ch = in.getNumChannels();
    for (int c = 0; c < ch; ++c)
    {
        const float* src = in.getReadPointer(c);
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] += src[i] / (float) ch;
    }
    return mono;
}

String describe(const DrumBeat::Model& m)
{
    String s = String((int) m.hits.size()) + " hits in " + String(m.durationSeconds, 1) + " s";
    if (m.tempoBpm > 0.0)
        s << ", about " << String(m.tempoBpm, 1) << " BPM";
    s << " - " << m.engine;
    return s;
}
} // namespace

// ------------------------------------------------------------------------------------------------

class DrumBeat::Job : public Thread
{
public:
    Job(DrumBeat& o, JobKind k, File f) : Thread("BitVibe drum beat"), owner(o), kind(k), file(std::move(f)) {}
    void run() override { owner.runJob(kind, file, *this); }

private:
    DrumBeat& owner;
    JobKind kind;
    File file;
};

DrumBeat::DrumBeat()
{
    const auto defaults = defaultSettings();
    for (int d = 0; d < kNumDrums; ++d)
    {
        actions[(size_t) d] = (int) defaults[(size_t) d].action;
        minLevels[(size_t) d] = defaults[(size_t) d].minLevelDb;
        minConfidences[(size_t) d] = defaults[(size_t) d].minConfidence;
    }
    startTimer(500);
}

DrumBeat::~DrumBeat()
{
    stopTimer();
    stopJob();
}

void DrumBeat::timerCallback()
{
    hitSlot.collect();
    audioSlot.collect();
}

// ---- jobs ---------------------------------------------------------------------------------------

void DrumBeat::stopJob()
{
    const ScopedLock sl(jobLock);
    if (job != nullptr)
    {
        job->signalThreadShouldExit();
        job->stopThread(10000);
        job.reset();
    }
}

void DrumBeat::startJob(JobKind kind, const File& file)
{
    const ScopedLock sl(jobLock);
    if (job != nullptr)
    {
        job->signalThreadShouldExit();
        job->stopThread(10000);
    }
    job = std::make_unique<Job>(*this, kind, file);
    job->startThread(Thread::Priority::background);
}

void DrumBeat::loadFile(const File& file) { startJob(JobKind::Load, file); }
void DrumBeat::loadDemo() { startJob(JobKind::Demo); }
void DrumBeat::reanalyse() { startJob(JobKind::Reanalyse); }

void DrumBeat::clear()
{
    stopJob();
    {
        const ScopedLock sl(lock);
        const int generation = model.generation + 1;
        model = Model();
        model.generation = generation;
        sourceAudio.setSize(0, 0);
    }
    setStatus("Drop a drum beat (WAV, MP3, AIFF, FLAC) here, or pick one from the library.");
    publishHits();
    publishAudio();
}

void DrumBeat::setDeviceSampleRate(double sampleRate)
{
    if (sampleRate <= 0.0 || exactlyEqual(sampleRate, deviceRate.load()))
        return;
    deviceRate = sampleRate;
    bool loaded = false;
    {
        const ScopedLock sl(lock);
        loaded = model.loaded;
    }
    if (loaded)
        startJob(JobKind::Resample);
}

void DrumBeat::restartPlayback() { restartRequested = true; }

bool DrumBeat::isBusy() const
{
    const ScopedLock sl(jobLock);
    return job != nullptr && job->isThreadRunning();
}

bool DrumBeat::waitUntilIdle(int timeoutMs)
{
    const auto end = Time::getMillisecondCounter() + (uint32) timeoutMs;
    while (isBusy())
    {
        if (Time::getMillisecondCounter() > end)
            return false;
        Thread::sleep(10);
    }
    return true;
}

void DrumBeat::runJob(JobKind kind, const File& file, Job& j)
{
    AudioBuffer<float> audio;
    double rate = 48000.0;
    String error, name, engine;
    bool isDemo = false, audioOk = true, analyseIt = true;
    File source = file;

    switch (kind)
    {
        case JobKind::Load:
            setStatus("Reading " + file.getFileName() + "...");
            if (!decode(file, audio, rate, error))
            {
                setStatus(error);
                return;
            }
            name = file.getFileNameWithoutExtension();
            break;

        case JobKind::Demo:
        {
            const auto demo = demo::renderDemoBeat(48000.0);
            audio.setSize(1, (int) demo.audio.size());
            audio.copyFrom(0, 0, demo.audio.data(), (int) demo.audio.size());
            rate = demo.sampleRate;
            isDemo = true;
            name = "Demo beat";
            break;
        }

        case JobKind::Reanalyse:
        {
            const ScopedLock sl(lock);
            if (!model.loaded || !model.audioAvailable)
            {
                status = "There is no audio to analyse.";
                return;
            }
            audio.makeCopyOf(sourceAudio);
            rate = sourceRate;
            isDemo = model.isDemo;
            source = model.file;
            name = model.name;
            break;
        }

        case JobKind::Restore:
        {
            {
                const ScopedLock sl(lock);
                isDemo = model.isDemo;
                source = model.file;
            }
            analyseIt = false;
            if (isDemo)
            {
                const auto demo = demo::renderDemoBeat(48000.0);
                audio.setSize(1, (int) demo.audio.size());
                audio.copyFrom(0, 0, demo.audio.data(), (int) demo.audio.size());
                rate = demo.sampleRate;
            }
            else if (!decode(source, audio, rate, error))
            {
                audioOk = false;
            }
            break;
        }

        case JobKind::Resample:
            publishHits();
            publishAudio();
            return;
    }

    if (j.threadShouldExit())
        return;

    std::vector<DrumHit> hits;
    if (analyseIt)
    {
        hits = analyse(audio, rate, !isDemo, source, j, engine);
        if (j.threadShouldExit())
            return;
    }

    Model snapshot;
    {
        const ScopedLock sl(lock);
        if (analyseIt)
        {
            model.hits = std::move(hits);
            model.engine = engine;
            model.durationSeconds = audio.getNumSamples() / rate;
            model.tempoBpm = estimateTempo(model.hits, model.durationSeconds);
            model.name = name;
        }
        else if (audioOk)
        {
            model.durationSeconds = audio.getNumSamples() / rate;
        }
        model.loaded = true;
        model.isDemo = isDemo;
        model.file = isDemo ? File() : source;
        model.audioAvailable = audioOk;
        ++model.generation;
        if (audioOk)
        {
            sourceAudio = std::move(audio);
            sourceRate = rate;
        }
        else
        {
            sourceAudio.setSize(0, 0);
        }
        snapshot = model;
    }
    publishHits();
    publishAudio();

    if (!audioOk)
        setStatus(error + " - the hits still trigger, but the beat is silent.");
    else
        setStatus(describe(snapshot) + (error.isNotEmpty() ? " (" + error + ")" : String()));
}

bool DrumBeat::decode(const File& file, AudioBuffer<float>& out, double& rate, String& error) const
{
    if (!file.existsAsFile())
    {
        error = "File not found: " + file.getFullPathName();
        return false;
    }
    AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        error = "Cannot read " + file.getFileName() + " (not a supported audio file?)";
        return false;
    }
    rate = reader->sampleRate;
    const auto length = (int) jmin<int64>(reader->lengthInSamples, (int64) (kMaxSeconds * rate));
    const int channels = jlimit(1, 2, (int) reader->numChannels);
    out.setSize(channels, length);
    out.clear();
    if (!reader->read(&out, 0, length, 0, true, channels > 1))
    {
        error = "Cannot decode " + file.getFileName();
        return false;
    }
    if (reader->lengthInSamples > (int64) (kMaxSeconds * rate))
        error = "cut to the first " + String((int) kMaxSeconds / 60) + " minutes";
    return true;
}

std::vector<DrumHit> DrumBeat::analyse(const AudioBuffer<float>& audio, double rate, bool allowNeural, const File& file,
                                       Job& j, String& engine)
{
    const std::vector<float> mono = mixToMono(audio);
    String neuralError;
    if (allowNeural && getAnalysisMode() == AnalysisMode::Auto && isNeuralHelperInstalled() && file.existsAsFile())
    {
        setStatus("Analysing with the neural helper (this can take a while)...");
        std::vector<DrumHit> hits;
        if (runNeuralHelper(file, hits, j, neuralError))
        {
            drums::refineOnsets(hits, mono.data(), mono.size(), rate);
            drums::measureLevels(hits, mono.data(), mono.size(), rate);
            normaliseLevels(hits);
            engine = "neural helper (ADTOF)";
            return hits;
        }
        if (j.threadShouldExit())
            return {};
    }
    setStatus("Analysing...");
    auto result = drums::analyse(mono.data(), mono.size(), rate);
    engine = neuralError.isEmpty() ? String("built-in analysis") : "built-in analysis (neural helper failed: " + neuralError + ")";
    return std::move(result.hits);
}

bool DrumBeat::runNeuralHelper(const File& audioFile, std::vector<DrumHit>& hits, Job& j, String& error)
{
    const File json = File::createTempFile(".json");
    ChildProcess process;
    const StringArray args { neuralHelperPython().getFullPathName(), neuralHelperScript().getFullPathName(),
                             audioFile.getFullPathName(), json.getFullPathName() };
    if (!process.start(args, ChildProcess::wantStdOut | ChildProcess::wantStdErr))
    {
        error = "could not start Python";
        return false;
    }
    const auto started = Time::getMillisecondCounter();
    while (!process.waitForProcessToFinish(100))
    {
        if (j.threadShouldExit() || Time::getMillisecondCounter() - started > 300000u)
        {
            process.kill();
            error = j.threadShouldExit() ? "cancelled" : "took longer than 5 minutes";
            json.deleteFile();
            return false;
        }
    }
    const auto output = process.readAllProcessOutput().trim();
    if (process.getExitCode() != 0 || !json.existsAsFile())
    {
        const auto lines = StringArray::fromLines(output);
        error = lines.isEmpty() ? "exit code " + String(process.getExitCode()) : lines[lines.size() - 1].substring(0, 160);
        json.deleteFile();
        return false;
    }

    const var parsed = JSON::parse(json);
    json.deleteFile();
    const auto* list = parsed["hits"].getArray();
    if (list == nullptr)
    {
        error = "unreadable result";
        return false;
    }
    hits.clear();
    for (const auto& item : *list)
    {
        DrumHit h;
        if (!drumFromId(item["drum"].toString().toStdString(), h.drum))
            continue;
        h.time = (double) item["t"];
        h.confidence = jlimit(0.0f, 1.0f, (float) (double) item["p"]);
        if (std::isfinite(h.time) && h.time >= 0.0)
            hits.push_back(h);
    }
    std::stable_sort(hits.begin(), hits.end(), [](const DrumHit& a, const DrumHit& b) { return a.time < b.time; });
    return true;
}

// ---- hand-over to the audio thread ------------------------------------------------------------------

void DrumBeat::publishHits()
{
    auto d = std::make_unique<HitData>();
    d->sampleRate = deviceRate.load();
    {
        const ScopedLock sl(lock);
        if (model.loaded)
        {
            d->hits = model.hits;
            d->generation = model.generation;
            d->loopLength = model.audioAvailable && sourceAudio.getNumSamples() > 0
                                ? (int64_t) std::llround(sourceAudio.getNumSamples() * d->sampleRate / sourceRate)
                                : (int64_t) std::llround(model.durationSeconds * d->sampleRate);
        }
    }
    d->samples.reserve(d->hits.size());
    for (const auto& h : d->hits)
        d->samples.push_back(jlimit<int64_t>(0, std::max<int64_t>(0, d->loopLength - 1), (int64_t) std::llround(h.time * d->sampleRate)));
    hitSlot.publish(std::move(d));
}

void DrumBeat::publishAudio()
{
    auto d = std::make_unique<AudioData>();
    d->sampleRate = deviceRate.load();
    {
        const ScopedLock sl(lock);
        const int length = sourceAudio.getNumSamples();
        if (model.loaded && model.audioAvailable && length > 0)
        {
            const double ratio = sourceRate / d->sampleRate; // input samples per output sample
            const int outLength = (int) std::llround(length * d->sampleRate / sourceRate);
            d->audio.setSize(sourceAudio.getNumChannels(), outLength);
            if (std::abs(ratio - 1.0) < 1.0e-9)
            {
                d->audio.makeCopyOf(sourceAudio);
            }
            else
            {
                WindowedSincInterpolator interpolator;
                const int skip = (int) std::lround(interpolator.getBaseLatency() / ratio); // compensate its delay
                const int padding = (int) (ratio * (skip + 64)) + 64;
                std::vector<float> in((size_t) (length + padding)), out((size_t) (outLength + skip));
                for (int c = 0; c < sourceAudio.getNumChannels(); ++c)
                {
                    std::fill(in.begin(), in.end(), 0.0f);
                    std::copy_n(sourceAudio.getReadPointer(c), length, in.begin());
                    interpolator.reset();
                    interpolator.process(ratio, in.data(), out.data(), (int) out.size());
                    d->audio.copyFrom(c, 0, out.data() + skip, outLength);
                }
            }
        }
        else
        {
            d->audio.setSize(0, 0);
        }
    }
    audioSlot.publish(std::move(d));
}

// ---- model and settings (message thread) -----------------------------------------------------------

DrumBeat::Model DrumBeat::getModel() const
{
    const ScopedLock sl(lock);
    return model;
}

int DrumBeat::getGeneration() const
{
    const ScopedLock sl(lock);
    return model.generation;
}

void DrumBeat::setTempoEstimate(double bpm)
{
    const ScopedLock sl(lock);
    if (model.loaded && std::isfinite(bpm))
    {
        model.tempoBpm = jlimit(0.0, (double) kMaxTempoBpm, bpm);
        ++model.generation;
    }
}

String DrumBeat::getStatus() const
{
    const ScopedLock sl(lock);
    return status;
}

void DrumBeat::setStatus(const String& s)
{
    const ScopedLock sl(lock);
    status = s;
}

void DrumBeat::setHitExcluded(int index, bool excluded)
{
    {
        const ScopedLock sl(lock);
        if (!isPositiveAndBelow(index, (int) model.hits.size()))
            return;
        model.hits[(size_t) index].excluded = excluded;
        ++model.generation;
    }
    publishHits();
}

void DrumBeat::setHitDrum(int index, Drum drum)
{
    {
        const ScopedLock sl(lock);
        if (!isPositiveAndBelow(index, (int) model.hits.size()))
            return;
        model.hits[(size_t) index].drum = drum;
        ++model.generation;
    }
    publishHits();
}

void DrumBeat::deleteHit(int index)
{
    {
        const ScopedLock sl(lock);
        if (!isPositiveAndBelow(index, (int) model.hits.size()))
            return;
        model.hits.erase(model.hits.begin() + index);
        ++model.generation;
    }
    publishHits();
}

DrumSettingsArray DrumBeat::getSettings() const
{
    DrumSettingsArray s {};
    for (int d = 0; d < kNumDrums; ++d)
        s[(size_t) d] = { (DrumAction) actions[(size_t) d].load(), minLevels[(size_t) d].load(), minConfidences[(size_t) d].load() };
    return s;
}

void DrumBeat::setSettings(Drum drum, const DrumSettings& s)
{
    const auto d = (size_t) drum;
    actions[d] = jlimit(0, kNumDrumActions - 1, (int) s.action);
    minLevels[d] = std::isfinite(s.minLevelDb) ? jlimit(kMinLevelFloorDb, 0.0f, s.minLevelDb) : -12.0f;
    minConfidences[d] = std::isfinite(s.minConfidence) ? jlimit(0.0f, 1.0f, s.minConfidence) : 0.3f;
}

DrumBeat::AnalysisMode DrumBeat::getAnalysisMode() const
{
    const ScopedLock sl(lock);
    return mode;
}

void DrumBeat::setAnalysisMode(AnalysisMode m)
{
    const ScopedLock sl(lock);
    mode = m;
}

// ---- neural helper and library ------------------------------------------------------------------

File DrumBeat::neuralHelperFolder()
{
   #if JUCE_WINDOWS
    return File::getSpecialLocation(File::windowsLocalAppData).getChildFile("BitVibe").getChildFile("neural");
   #elif JUCE_MAC
    return File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("Application Support/BitVibe/neural");
   #else
    return File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("BitVibe/neural");
   #endif
}

File DrumBeat::neuralHelperPython()
{
   #if JUCE_WINDOWS
    return neuralHelperFolder().getChildFile("Scripts").getChildFile("python.exe");
   #else
    return neuralHelperFolder().getChildFile("bin").getChildFile("python3");
   #endif
}

File DrumBeat::neuralHelperScript() { return neuralHelperFolder().getChildFile("bitvibe_adtof.py"); }

bool DrumBeat::isNeuralHelperInstalled()
{
    return neuralHelperPython().existsAsFile() && neuralHelperScript().existsAsFile();
}

Array<File> DrumBeat::libraryFolders()
{
    Array<File> folders;
    folders.add(File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("BitVibe").getChildFile("Beats"));
   #ifdef BITVIBE_BEATS_DIR
    const File repoBeats(BITVIBE_BEATS_DIR); // the source tree's beats/ folder (local builds only)
    if (repoBeats.isDirectory())
        folders.add(repoBeats);
   #endif
    return folders;
}

Array<File> DrumBeat::libraryFiles()
{
    Array<File> files;
    for (const auto& folder : libraryFolders())
        if (folder.isDirectory())
            for (const auto& f : folder.findChildFiles(File::findFiles, false, "*"))
                if (isSupportedAudioFile(f))
                    files.add(f);
    files.sort();
    return files;
}

bool DrumBeat::isSupportedAudioFile(const File& f)
{
    static const StringArray extensions { ".wav", ".wave", ".aif", ".aiff", ".flac", ".ogg", ".mp3", ".m4a", ".caf" };
    return extensions.contains(f.getFileExtension(), true);
}

// ---- state ---------------------------------------------------------------------------------------

ValueTree DrumBeat::toState() const
{
    ValueTree t("DRUMS");
    {
        const ScopedLock sl(lock);
        t.setProperty("mode", mode == AnalysisMode::Auto ? "auto" : "builtin", nullptr);
        if (model.loaded)
        {
            t.setProperty("demo", model.isDemo, nullptr);
            t.setProperty("file", model.file.getFullPathName(), nullptr);
            t.setProperty("name", model.name, nullptr);
            t.setProperty("engine", model.engine, nullptr);
            t.setProperty("duration", model.durationSeconds, nullptr);
            t.setProperty("tempo", model.tempoBpm, nullptr);
            ValueTree hitsNode("HITS");
            hitsNode.setProperty("data", hitsToText(model.hits), nullptr);
            t.appendChild(hitsNode, nullptr);
        }
    }
    const auto settings = getSettings();
    for (int d = 0; d < kNumDrums; ++d)
    {
        ValueTree node("DRUM");
        node.setProperty("id", drumId((Drum) d), nullptr);
        node.setProperty("action", (int) settings[(size_t) d].action, nullptr);
        node.setProperty("minLevel", (double) settings[(size_t) d].minLevelDb, nullptr);
        node.setProperty("minConfidence", (double) settings[(size_t) d].minConfidence, nullptr);
        t.appendChild(node, nullptr);
    }
    return t;
}

void DrumBeat::fromState(const ValueTree& t)
{
    const auto defaults = defaultSettings();
    for (int d = 0; d < kNumDrums; ++d)
        setSettings((Drum) d, defaults[(size_t) d]);
    setAnalysisMode(AnalysisMode::Auto);
    if (!t.isValid())
    {
        clear();
        return;
    }

    setAnalysisMode(t["mode"].toString() == "builtin" ? AnalysisMode::BuiltInOnly : AnalysisMode::Auto);
    for (const auto& node : t)
    {
        Drum d = Drum::Kick;
        if (!node.hasType("DRUM") || !drumFromId(node["id"].toString().toStdString(), d))
            continue;
        DrumSettings s = defaults[(size_t) d];
        s.action = (DrumAction) jlimit(0, kNumDrumActions - 1, (int) node.getProperty("action", (int) s.action));
        s.minLevelDb = (float) (double) node.getProperty("minLevel", (double) s.minLevelDb);
        s.minConfidence = (float) (double) node.getProperty("minConfidence", (double) s.minConfidence);
        setSettings(d, s);
    }

    const bool isDemo = (bool) t.getProperty("demo", false);
    const File file(t["file"].toString());
    const auto hitsNode = t.getChildWithName("HITS");
    if (!isDemo && t["file"].toString().isEmpty())
    {
        clear();
        return;
    }
    if (!hitsNode.isValid())
    {
        // Settings saved before any analysis finished: analyse again.
        if (isDemo)
            loadDemo();
        else
            loadFile(file);
        return;
    }

    stopJob();
    {
        const ScopedLock sl(lock);
        const int generation = model.generation + 1;
        model = Model();
        model.generation = generation;
        model.loaded = true;
        model.isDemo = isDemo;
        model.file = isDemo ? File() : file;
        model.name = t["name"].toString();
        model.engine = t["engine"].toString();
        model.durationSeconds = jlimit(0.0, kMaxSeconds, (double) t.getProperty("duration", 0.0));
        model.tempoBpm = (double) t.getProperty("tempo", 0.0);
        model.hits = hitsFromText(hitsNode["data"].toString());
        sourceAudio.setSize(0, 0);
        status = "Loading the beat...";
    }
    publishHits(); // triggers work straight away; the audio follows when it has been read
    startJob(JobKind::Restore);
}

// ---- audio thread ------------------------------------------------------------------------------------

int DrumBeat::beginBlock(int numSamples, bool playing, bool triggers, TriggerEvent* events, int maxEvents)
{
    blockHits = hitSlot.get();
    blockAudio = audioSlot.get();
    blockPlaying = playing && blockHits != nullptr && blockHits->loopLength > 0 && numSamples > 0;
    if (!blockPlaying)
    {
        wasPlaying = false;
        playingOut.store(false, std::memory_order_relaxed);
        return 0;
    }
    // Read the request before testing wasPlaying: with `!wasPlaying || exchange(false)` the exchange is skipped on
    // the first block, so the request made by prepareToPlay stayed pending and sent the beat back to the top
    // once more at the start of the second block (a ~10 ms replay and everything after it one block late).
    const bool restart = restartRequested.exchange(false);
    justStarted = !wasPlaying || restart;
    if (justStarted)
    {
        position = 0; // PLAY always starts the beat from the top
        wasPlaying = true;
    }
    position %= blockHits->loopLength; // a new, shorter beat may have arrived
    if (!triggers)
        return 0;

    DrumSettingsArray settings {};
    for (int d = 0; d < kNumDrums; ++d)
        settings[(size_t) d] = { (DrumAction) actions[(size_t) d].load(std::memory_order_relaxed),
                                 minLevels[(size_t) d].load(std::memory_order_relaxed),
                                 minConfidences[(size_t) d].load(std::memory_order_relaxed) };
    return DrumScheduler::collect(blockHits->hits.data(), blockHits->samples.data(), (int) blockHits->hits.size(),
                                  blockHits->loopLength, position, numSamples, settings, events, maxEvents);
}

void DrumBeat::addAudio(AudioBuffer<float>& buffer, int numChannels, int numSamples, float levelDb)
{
    if (!blockPlaying || blockAudio == nullptr || !exactlyEqual(blockAudio->sampleRate, blockHits->sampleRate)
        || blockAudio->audio.getNumSamples() != blockHits->loopLength || blockAudio->audio.getNumChannels() == 0)
        return;

    if (!exactlyEqual(gainRate, blockHits->sampleRate))
    {
        gainRate = blockHits->sampleRate;
        gain.reset(gainRate, 0.03);
    }
    const float target = levelDb <= kBeatOffDb ? 0.0f : Decibels::decibelsToGain(levelDb);
    if (justStarted)
        gain.setCurrentAndTargetValue(target); // the first hit keeps its attack
    else
        gain.setTargetValue(target);
    if (!gain.isSmoothing() && gain.getCurrentValue() == 0.0f)
        return;

    const auto& audio = blockAudio->audio;
    const int64_t length = audio.getNumSamples();
    const int beatChannels = audio.getNumChannels();
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = gain.getNextValue();
        const int idx = (int) ((position + i) % length);
        if (numChannels == 1 && beatChannels == 2)
        {
            buffer.addSample(0, i, 0.5f * g * (audio.getSample(0, idx) + audio.getSample(1, idx)));
            continue;
        }
        for (int c = 0; c < numChannels; ++c)
            buffer.addSample(c, i, g * audio.getSample(std::min(c, beatChannels - 1), idx));
    }
}

void DrumBeat::endBlock(int numSamples)
{
    if (!blockPlaying)
        return;
    position = (position + numSamples) % blockHits->loopLength;
    positionOut.store((double) position / blockHits->sampleRate, std::memory_order_relaxed);
    generationOut.store(blockHits->generation, std::memory_order_relaxed);
    playingOut.store(true, std::memory_order_relaxed);
}
