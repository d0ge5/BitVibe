// Scores the built-in drum analysis against annotated recordings (development tool, no JUCE).
//
//   bitvibe_drum_eval <dataset> <audio dir> <annotation dir> [key=value ...] [--json <dir>] [--files a,b]
//                     [--from-json <dir> [--refine]]
//
// <dataset> is "mdb" (MDB Drums: <name>_Drum.wav + <name>_class.txt, labels KD SD HH CY TT) or "idmt"
// (IDMT-SMT-Drums: audio/<name>.wav + annotation_xml/<name>.xml, KD SD HH). key=value overrides an
// analysis option (see parseOption). Prints precision / recall / F-measure per drum with a 50 ms
// tolerance, at the default confidence threshold and at the best one, plus timing offsets.
// --json writes each file's hits as the same JSON the neural helper produces; --from-json scores such
// files (for example the neural helper's, see Tools/drums/transcribe_folder.py) instead of analysing.

#include "DrumAnalysis.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace bitvibe;
namespace fs = std::filesystem;

namespace
{
constexpr double kTolerance = 0.050;
constexpr float kDefaultThreshold = 0.3f;

struct Audio
{
    std::vector<float> mono;
    double sampleRate = 0.0;
};

uint32_t u32(const unsigned char* p) { return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24; }
uint16_t u16(const unsigned char* p) { return (uint16_t) (p[0] | p[1] << 8); }

// Minimal RIFF/WAVE reader: PCM 16/24/32-bit and 32-bit float, any channel count (mixed to mono).
bool readWav(const fs::path& path, Audio& out)
{
    std::ifstream f(path, std::ios::binary);
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0)
        return false;
    int format = 0, channels = 0, bits = 0;
    size_t pos = 12;
    while (pos + 8 <= d.size())
    {
        const uint32_t size = u32(&d[pos + 4]);
        const unsigned char* body = &d[pos + 8];
        if (std::memcmp(&d[pos], "fmt ", 4) == 0)
        {
            format = u16(body);
            channels = u16(body + 2);
            out.sampleRate = u32(body + 4);
            bits = u16(body + 14);
            if (format == 0xFFFE && size >= 26)
                format = u16(body + 24); // WAVE_FORMAT_EXTENSIBLE: sub-format
        }
        else if (std::memcmp(&d[pos], "data", 4) == 0 && channels > 0)
        {
            const size_t bytes = std::min<size_t>(size, d.size() - pos - 8);
            const int width = bits / 8;
            const size_t frames = bytes / (size_t) (width * channels);
            out.mono.assign(frames, 0.0f);
            for (size_t i = 0; i < frames; ++i)
            {
                double acc = 0.0;
                for (int c = 0; c < channels; ++c)
                {
                    const unsigned char* s = body + (i * (size_t) channels + (size_t) c) * (size_t) width;
                    double v = 0.0;
                    if (format == 3 && bits == 32)
                    {
                        float fv;
                        std::memcpy(&fv, s, 4);
                        v = fv;
                    }
                    else if (bits == 16)
                        v = (int16_t) u16(s) / 32768.0;
                    else if (bits == 24)
                        v = (double) ((int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 | (uint32_t) s[2] << 24) >> 8) / 8388608.0;
                    else if (bits == 32)
                        v = (int32_t) u32(s) / 2147483648.0;
                    acc += v;
                }
                out.mono[i] = (float) (acc / channels);
            }
            return out.sampleRate > 0.0;
        }
        pos += 8 + size + (size & 1);
    }
    return false;
}

using Reference = std::array<std::vector<double>, kNumDrums>;

bool labelToDrum(const std::string& label, Drum& d)
{
    static const std::map<std::string, Drum> map { { "KD", Drum::Kick },   { "SD", Drum::Snare }, { "HH", Drum::HiHat },
                                                   { "CY", Drum::Cymbal }, { "TT", Drum::Tom } };
    const auto it = map.find(label);
    if (it == map.end())
        return false;
    d = it->second;
    return true;
}

Reference readMdb(const fs::path& path)
{
    Reference r;
    std::ifstream f(path);
    double t;
    std::string label;
    while (f >> t >> label)
    {
        Drum d;
        if (labelToDrum(label, d))
            r[(size_t) d].push_back(t);
    }
    return r;
}

// IDMT XML: <event><onsetSec>1.23</onsetSec> ... <instrument>KD</instrument></event>
Reference readIdmt(const fs::path& path)
{
    Reference r;
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string xml = ss.str();
    static const std::regex event(R"(<event>([\s\S]*?)</event>)");
    static const std::regex onset(R"(<onsetSec>\s*([0-9.eE+-]+)\s*</onsetSec>)");
    static const std::regex instrument(R"(<instrument>\s*([A-Za-z]+)\s*</instrument>)");
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), event); it != std::sregex_iterator(); ++it)
    {
        const std::string body = (*it)[1];
        std::smatch m1, m2;
        Drum d;
        if (std::regex_search(body, m1, onset) && std::regex_search(body, m2, instrument) && labelToDrum(m2[1], d))
            r[(size_t) d].push_back(std::stod(m1[1]));
    }
    for (auto& v : r)
        std::sort(v.begin(), v.end());
    return r;
}

struct Counts
{
    long tp = 0, fp = 0, fn = 0;
    double f() const { return tp == 0 ? 0.0 : 2.0 * tp / (2.0 * tp + fp + fn); }
    double p() const { return tp + fp == 0 ? 0.0 : (double) tp / (tp + fp); }
    double r() const { return tp + fn == 0 ? 0.0 : (double) tp / (tp + fn); }
};

// One-to-one matching within the tolerance, greedy in time order (both lists sorted).
Counts match(const std::vector<double>& est, const std::vector<double>& ref, std::vector<double>* offsets = nullptr)
{
    Counts c;
    std::vector<bool> used(ref.size(), false);
    size_t lo = 0;
    for (double e : est)
    {
        while (lo < ref.size() && ref[lo] < e - kTolerance)
            ++lo;
        long best = -1;
        double bestDist = kTolerance + 1e-9;
        for (size_t j = lo; j < ref.size() && ref[j] <= e + kTolerance; ++j)
            if (!used[j] && std::abs(ref[j] - e) < bestDist)
            {
                bestDist = std::abs(ref[j] - e);
                best = (long) j;
            }
        if (best >= 0)
        {
            used[(size_t) best] = true;
            ++c.tp;
            if (offsets)
                offsets->push_back(e - ref[(size_t) best]);
        }
        else
            ++c.fp;
    }
    c.fn = (long) ref.size() - c.tp;
    return c;
}

bool parseOption(drums::Options& o, const std::string& kv)
{
    const auto eq = kv.find('=');
    if (eq == std::string::npos)
        return false;
    const std::string k = kv.substr(0, eq);
    const double v = std::stod(kv.substr(eq + 1));
    if (k == "adaptation") o.adaptation = (float) v;
    else if (k == "fixed") o.fixedIterations = (int) v;
    else if (k == "adaptive") o.adaptiveIterations = (int) v;
    else if (k == "bpo") o.bandsPerOctave = (int) v;
    else if (k == "log") o.logScale = (float) v;
    else if (k == "perband") o.perBandReference = v != 0.0;
    else if (k == "lag") o.fluxLag = (int) v;
    else if (k == "onset") o.onsetThreshold = (float) v;
    else if (k == "avg") o.onsetAverageFrames = (int) v;
    else if (k == "window") o.peakWindowFrames = (int) v;
    else if (k == "gap") o.minGapSeconds = v;
    else if (k == "minrise") o.minRiseDb = (float) v;
    else if (k == "attack") o.attackFrames = (int) v;
    else if (k == "base") o.baseFrames = (int) v;
    else if (k == "share") o.shareExponent = (float) v;
    else if (k == "sw") o.sustainWeight = (float) v;
    else if (k == "sp") o.sustainPivot = (float) v;
    else if (k == "swid") o.sustainWidth = (float) v;
    else if (k == "typical") o.typicalPercentile = (float) v;
    else if (k == "mintyp") o.minTypicalFraction = (float) v;
    else if (k == "delay") o.onsetDelaySeconds = v;
    else if (k == "before") o.refineBeforeSeconds = v;
    else if (k == "after") o.refineAfterSeconds = v;
    else if (k == "half")
        o.halfConfidence.fill((float) v);
    else if (k.rfind("half.", 0) == 0)
    {
        Drum d;
        if (!drumFromId(k.substr(5), d))
            return false;
        o.halfConfidence[(size_t) d] = (float) v;
    }
    else
        return false;
    return true;
}

// Reads the hits of a helper JSON file ({"t": .., "drum": "..", "p": ..} objects).
bool readJson(const fs::path& path, drums::Result& r)
{
    std::ifstream f(path);
    if (!f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    static const std::regex hit(R"re(\{\s*"t":\s*([0-9.eE+-]+),\s*"drum":\s*"(\w+)",\s*"p":\s*([0-9.eE+-]+))re");
    r.hits.clear();
    for (auto it = std::sregex_iterator(text.begin(), text.end(), hit); it != std::sregex_iterator(); ++it)
    {
        DrumHit h;
        if (!drumFromId((*it)[2].str(), h.drum))
            continue;
        h.time = std::stod((*it)[1].str());
        h.confidence = std::stof((*it)[3].str());
        r.hits.push_back(h);
    }
    std::sort(r.hits.begin(), r.hits.end(), [](const DrumHit& a, const DrumHit& b) { return a.time < b.time; });
    return true;
}

void writeJson(const fs::path& path, const drums::Result& r)
{
    std::ofstream f(path);
    f << "{\"engine\":\"builtin\",\"version\":1,\"duration\":" << r.durationSeconds << ",\"tempo\":" << r.tempoBpm << ",\"hits\":[";
    for (size_t i = 0; i < r.hits.size(); ++i)
    {
        const auto& h = r.hits[i];
        f << (i ? "," : "") << "{\"t\":" << h.time << ",\"drum\":\"" << drumId(h.drum) << "\",\"p\":" << h.confidence
          << ",\"level\":" << h.levelDb << "}";
    }
    f << "]}\n";
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::puts("usage: bitvibe_drum_eval mdb|idmt <audio dir> <annotation dir> [key=value ...] [--json dir] [--files a,b]");
        return 2;
    }
    const std::string dataset = argv[1];
    const fs::path audioDir = argv[2], annDir = argv[3];
    drums::Options options;
    fs::path jsonDir, fromJson;
    std::vector<std::string> only;
    bool profile = false, refineJson = false;
    for (int i = 4; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--profile")
            profile = true;
        else if (a == "--json" && i + 1 < argc)
            jsonDir = argv[++i];
        else if (a == "--from-json" && i + 1 < argc)
            fromJson = argv[++i];
        else if (a == "--refine")
            refineJson = true;
        else if (a == "--files" && i + 1 < argc)
        {
            std::stringstream ss(argv[++i]);
            for (std::string s; std::getline(ss, s, ',');)
                only.push_back(s);
        }
        else if (!parseOption(options, a))
        {
            std::printf("unknown option %s\n", a.c_str());
            return 2;
        }
    }

    // Collect (audio, annotation) pairs.
    std::vector<std::pair<fs::path, fs::path>> items;
    for (const auto& e : fs::directory_iterator(annDir))
    {
        const std::string stem = e.path().stem().string();
        fs::path wav;
        if (dataset == "mdb")
        {
            const auto pos = stem.rfind("_class");
            if (pos == std::string::npos)
                continue;
            wav = audioDir / (stem.substr(0, pos) + "_Drum.wav");
        }
        else
        {
            if (e.path().extension() != ".xml")
                continue;
            wav = audioDir / (stem + ".wav");
        }
        if (!only.empty() && std::find_if(only.begin(), only.end(), [&](const std::string& s) { return stem.find(s) != std::string::npos; }) == only.end())
            continue;
        if (fs::exists(wav))
            items.push_back({ wav, e.path() });
    }
    std::sort(items.begin(), items.end());
    if (items.empty())
    {
        std::puts("no annotated audio found");
        return 1;
    }

    if (profile)
    {
        // Average normalised attack spectrum and high-band sustain of onsets where exactly one drum plays.
        std::array<std::vector<double>, kNumDrums> sum;
        std::array<int, kNumDrums> count {};
        std::array<std::vector<float>, kNumDrums> sustain;
        std::vector<double> centres;
        for (const auto& [wav, ann] : items)
        {
            Audio audio;
            if (!readWav(wav, audio))
                continue;
            const Reference ref = dataset == "mdb" ? readMdb(ann) : readIdmt(ann);
            const auto s = drums::computeSpectrogram(audio.mono.data(), audio.mono.size(), audio.sampleRate, options);
            const auto onsets = drums::detectOnsets(s, options);
            const auto A = drums::attackSpectra(s, onsets, options);
            const auto sus = drums::highSustain(s, onsets);
            centres = s.bandCentres;
            for (size_t n = 0; n < onsets.size(); ++n)
            {
                const double t = s.frameTime(onsets[n]);
                int solo = -1, present = 0;
                for (int d = 0; d < kNumDrums; ++d)
                    for (double r : ref[(size_t) d])
                        if (std::abs(r - t) < 0.03)
                        {
                            solo = d;
                            ++present;
                            break;
                        }
                if (present != 1)
                    continue;
                double total = 1e-12;
                for (int b = 0; b < s.numBands; ++b)
                    total += A[n * (size_t) s.numBands + (size_t) b];
                auto& acc = sum[(size_t) solo];
                acc.resize((size_t) s.numBands, 0.0);
                for (int b = 0; b < s.numBands; ++b)
                    acc[(size_t) b] += A[n * (size_t) s.numBands + (size_t) b] / total;
                ++count[(size_t) solo];
                sustain[(size_t) solo].push_back(sus[n]);
            }
        }
        std::printf("%8s", "Hz");
        for (int d = 0; d < kNumDrums; ++d)
            std::printf(" %7s", drumId((Drum) d));
        std::printf("\n");
        for (size_t b = 0; b < centres.size(); ++b)
        {
            std::printf("%8.0f", centres[b]);
            for (int d = 0; d < kNumDrums; ++d)
            {
                const auto& acc = sum[(size_t) d];
                const double peak = acc.empty() ? 1.0 : *std::max_element(acc.begin(), acc.end());
                std::printf(" %7.1f", acc.empty() ? 0.0 : 20.0 * std::log10(acc[b] / peak + 1e-9));
            }
            std::printf("\n");
        }
        for (int d = 0; d < kNumDrums; ++d)
        {
            auto v = sustain[(size_t) d];
            std::sort(v.begin(), v.end());
            if (!v.empty())
                std::printf("%-7s solo onsets %5d  sustain q10 %.2f q25 %.2f median %.2f q75 %.2f q90 %.2f\n", drumId((Drum) d),
                            count[(size_t) d], v[v.size() / 10], v[v.size() / 4], v[v.size() / 2], v[v.size() * 3 / 4],
                            v[v.size() * 9 / 10]);
        }
        return 0;
    }

    static const std::vector<float> thresholds = [] {
        std::vector<float> t;
        for (float v = 0.05f; v < 0.96f; v += 0.025f)
            t.push_back(v);
        return t;
    }();
    std::array<std::vector<Counts>, kNumDrums> sweep;
    for (auto& s : sweep)
        s.assign(thresholds.size(), {});
    std::array<std::vector<double>, kNumDrums> offsets;
    double audioSeconds = 0.0;

    for (const auto& [wav, ann] : items)
    {
        Audio audio;
        if (!readWav(wav, audio))
        {
            std::printf("  cannot read %s\n", wav.string().c_str());
            continue;
        }
        audioSeconds += audio.mono.size() / audio.sampleRate;
        const Reference ref = dataset == "mdb" ? readMdb(ann) : readIdmt(ann);
        drums::Result result;
        if (fromJson.empty())
            result = drums::analyse(audio.mono.data(), audio.mono.size(), audio.sampleRate, options);
        else if (readJson(fromJson / (wav.stem().string() + ".json"), result))
        {
            if (refineJson) // what the plug-in does with the neural helper's hits
                drums::refineOnsets(result.hits, audio.mono.data(), audio.mono.size(), audio.sampleRate, options);
        }
        else
        {
            std::printf("  no JSON for %s\n", wav.stem().string().c_str());
            continue;
        }
        if (!jsonDir.empty())
        {
            fs::create_directories(jsonDir);
            writeJson(jsonDir / (wav.stem().string() + ".json"), result);
        }

        std::array<Counts, kNumDrums> fileCounts {};
        for (int d = 0; d < kNumDrums; ++d)
        {
            for (size_t ti = 0; ti < thresholds.size(); ++ti)
            {
                std::vector<double> est;
                for (const auto& h : result.hits)
                    if ((int) h.drum == d && h.confidence >= thresholds[ti])
                        est.push_back(h.time);
                const bool isDefault = std::abs(thresholds[ti] - kDefaultThreshold) < 1e-4f;
                const Counts c = match(est, ref[(size_t) d], isDefault ? &offsets[(size_t) d] : nullptr);
                sweep[(size_t) d][ti].tp += c.tp;
                sweep[(size_t) d][ti].fp += c.fp;
                sweep[(size_t) d][ti].fn += c.fn;
                if (isDefault)
                    fileCounts[(size_t) d] = c;
            }
        }
        std::printf("  %-34s", wav.stem().string().c_str());
        for (int d = 0; d < kNumDrums; ++d)
            if (!ref[(size_t) d].empty() || fileCounts[(size_t) d].fp > 0)
                std::printf(" %s %.2f(%ld/%zu,+%ld)", drumId((Drum) d), fileCounts[(size_t) d].f(), fileCounts[(size_t) d].tp,
                            ref[(size_t) d].size(), fileCounts[(size_t) d].fp);
        std::printf("\n");
    }

    size_t defaultIndex = 0;
    for (size_t i = 0; i < thresholds.size(); ++i)
        if (std::abs(thresholds[i] - kDefaultThreshold) < 1e-4f)
            defaultIndex = i;

    std::printf("\n%zu files, %.1f min of audio\n", items.size(), audioSeconds / 60.0);
    std::printf("%-8s %6s %6s %6s %6s | %8s %6s | %8s %8s\n", "drum", "refs", "P", "R", "F", "best F", "@thr", "offs ms", "median");
    Counts all {}, allBest {};
    for (int d = 0; d < kNumDrums; ++d)
    {
        const Counts& c = sweep[(size_t) d][defaultIndex];
        size_t best = defaultIndex;
        for (size_t i = 0; i < thresholds.size(); ++i)
            if (sweep[(size_t) d][i].f() > sweep[(size_t) d][best].f())
                best = i;
        auto& off = offsets[(size_t) d];
        double mean = 0.0, median = 0.0;
        if (!off.empty())
        {
            for (double v : off)
                mean += v;
            mean /= (double) off.size();
            std::sort(off.begin(), off.end());
            median = off[off.size() / 2];
        }
        std::printf("%-8s %6ld %6.3f %6.3f %6.3f | %8.3f %6.3f | %8.1f %8.1f\n", drumId((Drum) d), c.tp + c.fn, c.p(), c.r(),
                    c.f(), sweep[(size_t) d][best].f(), thresholds[best], mean * 1000.0, median * 1000.0);
        all.tp += c.tp;
        all.fp += c.fp;
        all.fn += c.fn;
        allBest.tp += sweep[(size_t) d][best].tp;
        allBest.fp += sweep[(size_t) d][best].fp;
        allBest.fn += sweep[(size_t) d][best].fn;
    }
    std::printf("%-8s %6ld %6.3f %6.3f %6.3f | %8.3f\n", "all", all.tp + all.fn, all.p(), all.r(), all.f(), allBest.f());
    return 0;
}
