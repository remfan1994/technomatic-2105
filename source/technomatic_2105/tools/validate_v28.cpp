#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#define private public
#include "../app/src/main/cpp/MusicEngine.h"
#undef private

namespace {

uint64_t mix64(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6u) + (h >> 2u);
    h *= 1099511628211ull;
    return h;
}

template <typename Array>
uint64_t hashArray(const Array& values) {
    uint64_t h = 1469598103934665603ull;
    for (const auto& value : values) {
        h = mix64(h, static_cast<uint64_t>(static_cast<int64_t>(value) + 4096));
    }
    return h;
}

template <typename Array>
uint64_t hashFloatArray(const Array& values) {
    uint64_t h = 1469598103934665603ull;
    for (float value : values) {
        h = mix64(h, static_cast<uint32_t>(std::lround(value * 65536.0f)));
    }
    return h;
}

uint64_t coreIdentityHash(const rb::MusicEngine& engine) {
    const auto& c = engine.mComposition;
    const auto& p = engine.mPattern;
    uint64_t h = 1469598103934665603ull;
    h = mix64(h, static_cast<uint32_t>(p.style));
    h = mix64(h, static_cast<uint32_t>(p.rootMidi));
    h = mix64(h, static_cast<uint32_t>(p.scaleMode));
    h = mix64(h, hashArray(c.chordRoot));
    h = mix64(h, hashArray(c.motifA));
    h = mix64(h, hashArray(c.motifB));
    h = mix64(h, hashArray(c.motifC));
    h = mix64(h, hashArray(c.motifD));
    h = mix64(h, hashArray(c.motifE));
    h = mix64(h, hashArray(c.motifF));
    h = mix64(h, hashArray(c.motifG));
    h = mix64(h, hashFloatArray(c.gateA));
    h = mix64(h, hashFloatArray(c.gateF));
    h = mix64(h, hashFloatArray(c.gateG));
    h = mix64(h, hashArray(c.bassRel));
    h = mix64(h, hashArray(c.bassVerseRel));
    h = mix64(h, hashArray(c.bassAnswerRel));
    h = mix64(h, hashFloatArray(c.bassGate));
    h = mix64(h, hashArray(c.counterRel));
    h = mix64(h, hashArray(c.arpRel));
    h = mix64(h, hashArray(c.pulseRel));
    h = mix64(h, hashArray(c.ornamentRel));
    h = mix64(h, hashArray(c.form));
    h = mix64(h, hashArray(c.introForm));
    h = mix64(h, hashArray(c.outroForm));
    h = mix64(h, c.leadGrammarSeed);
    h = mix64(h, c.bassGrammarSeed);
    h = mix64(h, c.counterGrammarSeed);
    h = mix64(h, c.arpGrammarSeed);
    h = mix64(h, c.pulseGrammarSeed);
    h = mix64(h, c.ornamentGrammarSeed);
    h = mix64(h, c.boundaryGrammarSeed);
    h = mix64(h, static_cast<uint32_t>(c.introPhrases));
    h = mix64(h, static_cast<uint32_t>(c.introShape));
    h = mix64(h, static_cast<uint32_t>(c.outroPhrases));
    h = mix64(h, static_cast<uint32_t>(c.outroShape));
    h = mix64(h, static_cast<uint32_t>(c.outroCadencePos));
    h = mix64(h, static_cast<uint32_t>(c.conclusiveOutro ? 1 : 0));
    return h;
}

uint32_t parseUnsigned(const std::string& data, const char* key, uint32_t fallback = 0u) {
    const std::string needle = std::string(key) + "=";
    const std::size_t p = data.find(needle);
    if (p == std::string::npos) return fallback;
    const std::size_t start = p + needle.size();
    const std::size_t end = data.find(';', start);
    try { return static_cast<uint32_t>(std::stoull(data.substr(start, end - start))); }
    catch (...) { return fallback; }
}

int32_t parseSigned(const std::string& data, const char* key, int32_t fallback = 0) {
    const std::string needle = std::string(key) + "=";
    const std::size_t p = data.find(needle);
    if (p == std::string::npos) return fallback;
    const std::size_t start = p + needle.size();
    const std::size_t end = data.find(';', start);
    try { return static_cast<int32_t>(std::stoll(data.substr(start, end - start))); }
    catch (...) { return fallback; }
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) if (!line.empty()) out.push_back(line);
    return out;
}

void renderSeconds(rb::MusicEngine& engine, int seconds) {
    constexpr int rate = 48000;
    constexpr int block = 1024;
    std::vector<float> samples(block * 2);
    int frames = seconds * rate;
    while (frames > 0) {
        const int n = std::min(block, frames);
        std::fill(samples.begin(), samples.end(), 0.0f);
        engine.render(samples.data(), n, 2);
        frames -= n;
    }
}

long fileSize(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return -1;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fclose(f);
    return size;
}

} // namespace

int main() {
    bool ok = true;
    auto require = [&](bool condition, const char* label) {
        std::cout << (condition ? "PASS " : "FAIL ") << label << "\n";
        ok = ok && condition;
    };

    constexpr uint32_t seed = 3290437499u;
    constexpr int channelA = 1 << 0;  // Chrome Pulse
    constexpr int channelB = 1 << 9;  // Soft Voltage

    rb::MusicEngine open, chrome, hybrid;
    open.prepare(48000.0);
    chrome.prepare(48000.0);
    hybrid.prepare(48000.0);

    open.setGenreMask(0);
    open.setGenreBlendMode(0);
    open.setGenrePrimary(0);
    open.reset(seed);

    chrome.setGenreMask(channelA);
    chrome.setGenreBlendMode(0);
    chrome.setGenrePrimary(1);
    chrome.reset(seed);

    hybrid.setGenreMask(channelA | channelB);
    hybrid.setGenreBlendMode(1);
    hybrid.setGenrePrimary(1);
    hybrid.reset(seed);

    const uint64_t openCore = coreIdentityHash(open);
    require(open.mCurrentCandidateIndex == chrome.mCurrentCandidateIndex,
            "single Channel retains the seed's winning candidate");
    require(open.mCurrentCandidateIndex == hybrid.mCurrentCandidateIndex,
            "Hybrid Channels retain the seed's winning candidate");
    require(openCore == coreIdentityHash(chrome),
            "single Channel preserves motifs, harmony, bass grammar, form, and boundaries");
    require(openCore == coreIdentityHash(hybrid),
            "Hybrid Channels preserve the same composition core");

    const bool renditionChanged = std::fabs(open.mBpmTarget - chrome.mBpmTarget) > 0.01f ||
        open.mComposition.leadModel != chrome.mComposition.leadModel ||
        open.mComposition.bassModel != chrome.mComposition.bassModel ||
        std::fabs(open.mPattern.density - chrome.mPattern.density) > 0.01f ||
        std::fabs(open.mPattern.roughness - chrome.mPattern.roughness) > 0.01f;
    require(renditionChanged, "Channel changes the rendition rather than doing nothing");

    std::vector<float> openAudio(48000 * 2), channelAudio(48000 * 2);
    open.render(openAudio.data(), 48000, 2);
    chrome.render(channelAudio.data(), 48000, 2);
    float audioDifference = 0.0f;
    for (std::size_t i = 0; i < openAudio.size(); ++i) {
        audioDifference = std::max(audioDifference, std::fabs(openAudio[i] - channelAudio[i]));
    }
    require(audioDifference > 1.0e-5f, "same seed is audibly reinterpreted by a Channel");

    rb::MusicEngine live;
    live.prepare(48000.0);
    live.clearHistory();
    live.setGenreMask(0);
    live.setGenreBlendMode(0);
    live.setGenrePrimary(0);
    live.reset(seed);
    const int candidateBefore = live.mCurrentCandidateIndex;
    const uint64_t coreBefore = coreIdentityHash(live);
    renderSeconds(live, 2);
    live.rerenderCurrentWithChannel(channelB, 0, 10);
    require(parseUnsigned(live.currentSongData(), "seed") == seed,
            "Channel selection keeps the visible seed");
    require(live.mCurrentCandidateIndex == candidateBefore,
            "Channel selection keeps the candidate");
    require(coreIdentityHash(live) == coreBefore,
            "Channel selection reloads the same musical identity");
    require(live.currentElapsedSeconds() < 0.05,
            "Channel selection restarts the same composition at zero");
    renderSeconds(live, 3);

    const auto history = lines(live.historyData());
    bool oldRendition = false;
    bool newRendition = false;
    for (const auto& entry : history) {
        if (parseUnsigned(entry, "seed") != seed) continue;
        const int mask = parseSigned(entry, "gmask", -1);
        const int listened = parseSigned(entry, "listened", -1);
        if (mask == 0 && listened >= 1) oldRendition = true;
        if (mask == channelB && listened >= 2) newRendition = true;
    }
    require(history.size() == 2, "history keeps separate same-seed Channel renditions");
    require(oldRendition, "history freezes listened duration for the old rendition");
    require(newRendition, "history updates listened duration for the current rendition");

    rb::MusicEngine cappedHistory;
    cappedHistory.prepare(48000.0);
    cappedHistory.clearHistory();
    cappedHistory.reset(77u);
    for (int i = 0; i < 25; ++i) cappedHistory.forceNewPiece();
    require(lines(cappedHistory.historyData()).size() == 20,
            "native history remains capped at the latest 20 renditions");

    std::set<int> introShapes, outroShapes, introLengths, outroLengths;
    std::set<uint64_t> boundaryIds;
    int conclusiveCount = 0;
    bool boundaryLengthsInBounds = true;
    for (uint32_t s = 1; s <= 256; ++s) {
        rb::MusicEngine e;
        e.prepare(48000.0);
        e.reset(s * 2654435761u);
        introShapes.insert(e.mComposition.introShape);
        outroShapes.insert(e.mComposition.outroShape);
        introLengths.insert(e.mComposition.introPhrases);
        outroLengths.insert(e.mComposition.outroPhrases);
        boundaryIds.insert(coreIdentityHash(e) ^ static_cast<uint64_t>(e.mComposition.boundaryGrammarSeed));
        if (e.mComposition.conclusiveOutro) ++conclusiveCount;
        boundaryLengthsInBounds = boundaryLengthsInBounds &&
            e.mComposition.introPhrases >= 3 && e.mComposition.introPhrases <= 8 &&
            e.mComposition.outroPhrases >= 3 && e.mComposition.outroPhrases <= 8;
    }
    require(boundaryLengthsInBounds, "generated opening and ending lengths stay in bounds");
    require(introShapes.size() == 6, "all generated opening geometries are reachable");
    require(outroShapes.size() == 5, "all generated ending geometries are reachable");
    require(introLengths.size() >= 5, "opening lengths have broad phrase diversity");
    require(outroLengths.size() >= 5, "ending lengths have broad phrase diversity");
    require(boundaryIds.size() >= 250, "opening and ending grammars are seed-specific");
    require(conclusiveCount > 40 && conclusiveCount < 245,
            "conclusive and dissolving endings are both common enough to matter");

    rb::MusicEngine exactA, exactB;
    exactA.prepare(48000.0);
    exactB.prepare(48000.0);
    exactA.setGenreMask(channelA | channelB);
    exactA.setGenreBlendMode(1);
    exactA.setGenrePrimary(1);
    exactA.reset(0x12345678u);
    const std::string snapshot = exactA.currentSongData();
    require(exactB.loadSongData(snapshot), "same-seed Channel snapshot reloads");
    std::vector<float> a(48000 * 2), b(48000 * 2);
    exactA.render(a.data(), 48000, 2);
    exactB.render(b.data(), 48000, 2);
    require(a == b, "Channel snapshot reconstruction is sample exact");

    const char* rangePath = "/tmp/technomatic_v28_range.pcm";
    std::remove(rangePath);
    require(rb::MusicEngine::exportPcm16RangeFile(snapshot, 10, 40, rangePath),
            "start-end offline export completes");
    const long expected = 30L * 48000L * 2L * static_cast<long>(sizeof(int16_t));
    require(fileSize(rangePath) == expected, "start-end export writes exact requested duration");
    std::remove(rangePath);

    // Exercise a finite export ending and verify bounded output.
    rb::MusicEngine bounded;
    bounded.prepare(48000.0);
    require(bounded.loadSongData(snapshot), "finite boundary test snapshot loads");
    bounded.mExportSinglePieceMode = true;
    bounded.mExportStopSamples = 45LL * 48000LL;
    bounded.mComposition.pieceSteps = bounded.pieceStepsFromSeconds(45, bounded.mBpmTarget);
    std::vector<float> block(1024 * 2);
    float peak = 0.0f;
    bool finite = true;
    int frames = 45 * 48000;
    while (frames > 0) {
        const int n = std::min(1024, frames);
        std::fill(block.begin(), block.end(), 0.0f);
        bounded.render(block.data(), n, 2);
        for (int i = 0; i < n * 2; ++i) {
            finite = finite && std::isfinite(block[static_cast<std::size_t>(i)]);
            peak = std::max(peak, std::fabs(block[static_cast<std::size_t>(i)]));
        }
        frames -= n;
    }
    require(finite, "generated opening and ending remain finite");
    require(peak <= 1.0f, "generated opening and ending remain unclipped");

    // Source-level invariant: the full quality search remains 48 candidates.
    std::ifstream source("app/src/main/cpp/MusicEngine.cpp");
    std::string sourceText((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    require(sourceText.find("for (int32_t i = 0; i < 48; ++i)") != std::string::npos,
            "all 48 composition candidates remain in the search");

    return ok ? 0 : 1;
}
