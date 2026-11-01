// Portable host observer for the v30.1 output-only change. It writes the actual
// unclipped float output, not a normalized or codec-processed audition.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifndef TECHNOMATIC_ENGINE_HEADER
#define TECHNOMATIC_ENGINE_HEADER "../app/src/main/cpp/MusicEngine.h"
#endif
#define private public
#include TECHNOMATIC_ENGINE_HEADER
#undef private

namespace {
constexpr int rate = 48000;
constexpr int channels = 2;
void le16(FILE* f, uint16_t value) {
    unsigned char bytes[2] = {static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8)};
    std::fwrite(bytes, 1, 2, f);
}
void le32(FILE* f, uint32_t value) {
    unsigned char bytes[4] = {static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16), static_cast<unsigned char>(value >> 24)};
    std::fwrite(bytes, 1, 4, f);
}
void header(FILE* f, int64_t frames) {
    static_assert(sizeof(float) == 4, "float WAV requires 32-bit float");
    const uint32_t size = static_cast<uint32_t>(frames * channels * sizeof(float));
    std::fwrite("RIFF", 1, 4, f); le32(f, 36 + size); std::fwrite("WAVEfmt ", 1, 8, f);
    le32(f, 16); le16(f, 3); le16(f, channels); le32(f, rate);
    le32(f, rate * channels * sizeof(float)); le16(f, channels * sizeof(float)); le16(f, 32);
    std::fwrite("data", 1, 4, f); le32(f, size);
}
std::string quote(const std::string& value) {
    std::ostringstream out; out << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c == '\n') out << "\\n";
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
        else out << c;
    }
    out << '"'; return out.str();
}
struct Hash {
    uint64_t value = 14695981039346656037ull;
    void bytes(const void* data, size_t length) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < length; ++i) { value ^= p[i]; value *= 1099511628211ull; }
    }
    template<class T> void add(const T& item) { bytes(&item, sizeof(item)); }
    std::string str() const { std::ostringstream out; out << std::hex << std::setfill('0') << std::setw(16) << value; return out.str(); }
};
// Hash explicit actual scheduled fields, never object padding. This bounded
// scheduler check is independent of mixed PCM and is not a listening test.
struct Signatures { std::string full, score, amplitude; int count; };
Signatures noteSignature(uint32_t seed) {
    rb::MusicEngine e; e.prepare(rate); e.reset(seed); Hash hash, score, amplitude; int count = 0;
    for (int step = 0; step < 64 * 16; ++step) {
        e.clearVoicesAndEvents(); e.onStep(); hash.add(step); score.add(step); amplitude.add(step);
        for (const auto& event : e.mEvents) {
            if (!event.active) continue;
            ++count;
            hash.add(event.samples); hash.add(event.kind); hash.add(event.drumType);
            hash.add(event.amp); hash.add(event.pan); hash.add(event.dur); hash.add(event.freq);
            hash.add(event.aux); hash.add(event.degree); hash.add(event.octave);
            hash.add(event.instrumentSlot); hash.add(event.foreground);
            // A separate signature excludes only velocity/amplitude so an
            // authorized removal of phrase-level volume macros cannot mask a
            // pitch, timing, articulation, panning or instrument-owner change.
            score.add(event.samples); score.add(event.kind); score.add(event.drumType);
            score.add(event.pan); score.add(event.dur); score.add(event.freq);
            score.add(event.aux); score.add(event.degree); score.add(event.octave);
            score.add(event.instrumentSlot); score.add(event.foreground);
            amplitude.add(event.amp);
        }
        score.add(e.mRng.state); score.add(e.mDevelopmentRng.state);
    }
    return {hash.str(), score.str(), amplitude.str(), count};
}
struct Level {
    double energy = 0; double peak = 0; int64_t samples = 0; int64_t nonFinite = 0; int64_t overFull = 0;
    void add(float sample) {
        ++samples;
        if (!std::isfinite(sample)) { ++nonFinite; return; }
        energy += static_cast<double>(sample) * sample;
        peak = std::max(peak, std::fabs(static_cast<double>(sample)));
        if (std::fabs(sample) >= 1.0f) ++overFull;
    }
    double rms() const { return std::sqrt(energy / std::max<int64_t>(1, samples)); }
};
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--identity") {
        const uint32_t seed = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 0));
        const auto signature = noteSignature(seed);
        std::cout << "{\"seed\":" << seed << ",\"scheduler_fnv1a\":" << quote(signature.full)
            << ",\"score_fnv1a\":" << quote(signature.score) << ",\"amplitude_fnv1a\":" << quote(signature.amplitude)
            << ",\"scheduled_events\":" << signature.count << "}\n";
        return 0;
    }
    if (argc < 4) {
        std::cerr << "usage: loudness_probe output.wav seed duration_seconds [start_seconds=0] [next_at_seconds=-1] [block_frames=256]\n";
        return 2;
    }
    const std::string path = argv[1];
    const uint32_t seed = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 0));
    const double seconds = std::max(0.01, std::atof(argv[3]));
    const double start = argc > 4 ? std::max(0.0, std::atof(argv[4])) : 0;
    const double nextAt = argc > 5 ? std::atof(argv[5]) : -1;
    const int blockFrames = argc > 6 ? std::max(1, std::min(65536, std::atoi(argv[6]))) : 256;
    const int64_t startFrame = std::llround(start * rate);
    const int64_t frames = std::llround(seconds * rate);
    const int64_t lastFrame = startFrame + frames;
    const int64_t nextFrame = nextAt < 0 ? -1 : std::llround(nextAt * rate);
    if (frames * channels * sizeof(float) > std::numeric_limits<uint32_t>::max() - 36u) return 2;
    const uint16_t endian = 1;
    if (*reinterpret_cast<const unsigned char*>(&endian) != 1) { std::cerr << "float writer requires little-endian host\n"; return 2; }
    FILE* wav = std::fopen(path.c_str(), "wb");
    FILE* levels = std::fopen((path + ".levels.csv").c_str(), "w");
    if (!wav || !levels) { std::cerr << "cannot open output\n"; if (wav) std::fclose(wav); if (levels) std::fclose(levels); return 2; }
    header(wav, frames);
    std::fprintf(levels, "start_seconds,end_seconds,rms,peak\n");
    rb::MusicEngine engine; engine.prepare(rate); engine.reset(seed);
    const std::string initialRecipe = engine.currentSongData();
    const int intent = engine.mPattern.intent; const double bpm = engine.mBpmTarget;
    const float patternDrive = engine.mPattern.drive;
    const auto signature = noteSignature(seed);
    std::vector<float> block(static_cast<size_t>(blockFrames * channels));
    Level all, bin; Hash hash; int64_t binStart = startFrame;
    bool calledNext = false;
    for (int64_t cursor = 0; cursor < lastFrame;) {
        if (cursor == nextFrame) { engine.next(); calledNext = true; }
        int n = static_cast<int>(std::min<int64_t>(blockFrames, lastFrame - cursor));
        if (cursor < startFrame) n = static_cast<int>(std::min<int64_t>(n, startFrame - cursor));
        if (nextFrame > cursor) n = static_cast<int>(std::min<int64_t>(n, nextFrame - cursor));
        engine.render(block.data(), n, channels);
        if (cursor >= startFrame) {
            if (std::fwrite(block.data(), sizeof(float), static_cast<size_t>(n * channels), wav) != static_cast<size_t>(n * channels)) return 2;
            hash.bytes(block.data(), static_cast<size_t>(n * channels) * sizeof(float));
            for (int i = 0; i < n; ++i) {
                for (int ch = 0; ch < channels; ++ch) { const float x = block[i * channels + ch]; all.add(x); bin.add(x); }
                const int64_t now = cursor + i + 1;
                if (now - binStart == rate / 10 || now == lastFrame) {
                    std::fprintf(levels, "%.6f,%.6f,%.9f,%.9f\n", static_cast<double>(binStart) / rate,
                        static_cast<double>(now) / rate, bin.rms(), bin.peak);
                    bin = Level{}; binStart = now;
                }
            }
        }
        cursor += n;
    }
    const int closeWav = std::fclose(wav); const int closeLevels = std::fclose(levels);
    std::cout << std::setprecision(12) << "{\"seed\":" << seed << ",\"intent\":" << intent << ",\"bpm\":" << bpm
        << ",\"pattern_drive\":" << patternDrive << ",\"sample_rate\":" << rate << ",\"channels\":" << channels << ",\"start_seconds\":" << start
        << ",\"duration_seconds\":" << seconds << ",\"frames\":" << frames << ",\"next_at_seconds\":" << nextAt
        << ",\"called_next\":" << (calledNext ? "true" : "false") << ",\"sample_peak\":" << all.peak
        << ",\"rms\":" << all.rms() << ",\"nonfinite_samples\":" << all.nonFinite << ",\"fullscale_samples\":" << all.overFull
        << ",\"pcm_fnv1a\":" << quote(hash.str()) << ",\"scheduler_fnv1a\":" << quote(signature.full)
        << ",\"score_fnv1a\":" << quote(signature.score) << ",\"amplitude_fnv1a\":" << quote(signature.amplitude)
        << ",\"scheduled_events\":" << signature.count << ",\"initial_recipe\":" << quote(initialRecipe)
        << ",\"final_recipe\":" << quote(engine.currentSongData()) << "}\n";
    // Raw pre-master diagnostic mixes can exceed 1.0. Preserve and count those
    // values; the final output validation, not the float writer, rejects them.
    return closeWav || closeLevels || all.nonFinite ? 1 : 0;
}
