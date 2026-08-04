#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <set>
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

template <typename IntArray, typename GateArray>
uint64_t hashPhrase(const IntArray& notes, const GateArray& gates) {
    uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        h = mix64(h, static_cast<uint32_t>(notes[i] + 128));
        h = mix64(h, static_cast<uint32_t>(std::lround(gates[i] * 4096.0f)));
    }
    return h;
}

template <typename IntArray>
uint64_t hashInts(const IntArray& values) {
    uint64_t h = 1469598103934665603ull;
    for (auto value : values) h = mix64(h, static_cast<uint32_t>(value + 128));
    return h;
}

uint32_t parseUnsigned(const std::string& data, const char* key, uint32_t fallback = 0u) {
    const std::string needle = std::string(key) + "=";
    const std::size_t start0 = data.find(needle);
    if (start0 == std::string::npos) return fallback;
    const std::size_t start = start0 + needle.size();
    const std::size_t end = data.find(';', start);
    try {
        return static_cast<uint32_t>(std::stoull(data.substr(start, end - start)));
    } catch (...) {
        return fallback;
    }
}

int32_t parseSigned(const std::string& data, const char* key, int32_t fallback = 0) {
    const std::string needle = std::string(key) + "=";
    const std::size_t start0 = data.find(needle);
    if (start0 == std::string::npos) return fallback;
    const std::size_t start = start0 + needle.size();
    const std::size_t end = data.find(';', start);
    try {
        return static_cast<int32_t>(std::stoll(data.substr(start, end - start)));
    } catch (...) {
        return fallback;
    }
}

float profileDistance(const rb::MusicEngine::StyleProfile& a,
                      const rb::MusicEngine::StyleProfile& b) {
    const float* av[] = {&a.bpmMin, &a.bpmMax, &a.swingMin, &a.swingMax, &a.density,
                         &a.drum, &a.bass, &a.melody, &a.chord, &a.texture, &a.rough,
                         &a.space, &a.sync, &a.hatRoll, &a.melodyRun, &a.drama,
                         &a.palette, &a.brightness};
    const float* bv[] = {&b.bpmMin, &b.bpmMax, &b.swingMin, &b.swingMax, &b.density,
                         &b.drum, &b.bass, &b.melody, &b.chord, &b.texture, &b.rough,
                         &b.space, &b.sync, &b.hatRoll, &b.melodyRun, &b.drama,
                         &b.palette, &b.brightness};
    float sum = 0.0f;
    for (std::size_t i = 0; i < sizeof(av) / sizeof(av[0]); ++i) {
        const float scale = i < 2 ? 100.0f : 1.0f;
        const float d = (*av[i] - *bv[i]) / scale;
        sum += d * d;
    }
    return std::sqrt(sum);
}

} // namespace

int main() {
    bool ok = true;
    auto require = [&](bool condition, const std::string& label) {
        std::cout << (condition ? "PASS " : "FAIL ") << label << "\n";
        ok = ok && condition;
    };

    // Every seed should create its own generated melodic language.
    constexpr uint32_t kSeeds = 256;
    std::set<uint64_t> leadIds, hookIds, verseIds, bassIds, bassVerseIds, bassAnswerIds;
    std::set<uint64_t> counterIds, arpIds, pulseIds, ornamentIds, harmonyIds;
    rb::MusicEngine diversity;
    diversity.prepare(48000.0);
    for (uint32_t seed = 1; seed <= kSeeds; ++seed) {
        diversity.reset(seed);
        leadIds.insert(hashPhrase(diversity.mComposition.motifA, diversity.mComposition.gateA));
        hookIds.insert(hashPhrase(diversity.mComposition.motifF, diversity.mComposition.gateF));
        verseIds.insert(hashPhrase(diversity.mComposition.motifG, diversity.mComposition.gateG));
        bassIds.insert(hashPhrase(diversity.mComposition.bassRel, diversity.mComposition.bassGate));
        bassVerseIds.insert(hashPhrase(diversity.mComposition.bassVerseRel, diversity.mComposition.bassVerseGate));
        bassAnswerIds.insert(hashPhrase(diversity.mComposition.bassAnswerRel, diversity.mComposition.bassAnswerGate));
        counterIds.insert(hashPhrase(diversity.mComposition.counterRel, diversity.mComposition.counterGate));
        arpIds.insert(hashPhrase(diversity.mComposition.arpRel, diversity.mComposition.arpGate));
        pulseIds.insert(hashPhrase(diversity.mComposition.pulseRel, diversity.mComposition.pulseGate));
        ornamentIds.insert(hashPhrase(diversity.mComposition.ornamentRel, diversity.mComposition.ornamentGate));
        harmonyIds.insert(hashInts(diversity.mComposition.chordRoot));
    }
    require(leadIds.size() >= kSeeds * 995 / 1000, "generated lead identities are effectively unique");
    require(hookIds.size() >= kSeeds * 995 / 1000, "generated hook identities are effectively unique");
    require(verseIds.size() >= kSeeds * 995 / 1000, "generated verse identities are effectively unique");
    require(bassIds.size() >= kSeeds * 995 / 1000, "generated bass identities are effectively unique");
    require(bassVerseIds.size() >= kSeeds * 990 / 1000, "generated bass verse identities are effectively unique");
    require(bassAnswerIds.size() >= kSeeds * 990 / 1000, "generated bass answer identities are effectively unique");
    require(counterIds.size() >= kSeeds * 990 / 1000, "generated counter identities are effectively unique");
    require(arpIds.size() >= kSeeds * 990 / 1000, "generated arp identities are effectively unique");
    require(pulseIds.size() >= kSeeds * 985 / 1000, "generated pulse identities are effectively unique");
    require(ornamentIds.size() >= kSeeds * 990 / 1000, "generated ornament identities are effectively unique");
    require(harmonyIds.size() > kSeeds / 2, "generated harmonic identities have broad diversity");

    // A saved seed/candidate/channel snapshot must reconstruct exactly.
    rb::MusicEngine original, reconstructed;
    original.prepare(48000.0);
    reconstructed.prepare(48000.0);
    original.reset(0x12345678u);
    const std::string snapshot = original.currentSongData();
    require(reconstructed.loadSongData(snapshot), "saved sound data reloads");
    std::vector<float> a(48000 * 2), b(48000 * 2);
    original.render(a.data(), 48000, 2);
    reconstructed.render(b.data(), 48000, 2);
    float maxDifference = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        maxDifference = std::max(maxDifference, std::fabs(a[i] - b[i]));
    }
    require(maxDifference == 0.0f, "seed and candidate reconstruction is sample exact");

    // Live playback has exactly one policy: keep the same seed indefinitely.
    rb::MusicEngine indefinite;
    indefinite.prepare(48000.0);
    indefinite.reset(0x33445566u);
    const uint32_t indefiniteSeed = parseUnsigned(indefinite.currentSongData(), "seed");
    std::vector<float> block(2048 * 2);
    constexpr int32_t kLongRenderSeconds = 600;
    const int32_t longBlocks = kLongRenderSeconds * 48000 / 2048;
    for (int32_t i = 0; i < longBlocks; ++i) indefinite.render(block.data(), 2048, 2);
    require(parseUnsigned(indefinite.currentSongData(), "seed") == indefiniteSeed,
            "live playback never replaces its seed automatically");
    require(indefinite.mTransitionStage == rb::MusicEngine::TransitionStage::None,
            "live playback never enters an automatic track transition");
    require(indefinite.currentSectionType(indefinite.mStyleAgeSteps) != rb::MusicEngine::SectionType::Outro,
            "indefinite live playback never falls into an export outro");
    require(indefinite.currentElapsedSeconds() > 590.0,
            "elapsed time continues across an indefinite sound");

    // Long-form evolution must remain active while the seed stays fixed.
    rb::MusicEngine evolution;
    evolution.prepare(48000.0);
    evolution.reset(0x55aa7711u);
    const auto frame0 = evolution.evolutionFrameForStep(0);
    const float zeroMagnitude = std::fabs(frame0.leadMutation) + std::fabs(frame0.bassMutation) +
                                std::fabs(frame0.rhythmMigration) + std::fabs(frame0.densityDrift) +
                                std::fabs(frame0.registerTide) + std::fabs(frame0.harmonyLens) +
                                std::fabs(frame0.paletteDrift);
    require(zeroMagnitude < 1.0e-6f, "evolution begins at the generated identity");

    float maximumEvolution = 0.0f;
    float maximumAdjacentDelta = 0.0f;
    int32_t strongestStep = 0;
    auto previousFrame = frame0;
    for (int32_t phrase = 1; phrase <= 240; ++phrase) {
        const int32_t step = phrase * rb::MusicEngine::kPhraseSteps;
        const auto frame = evolution.evolutionFrameForStep(step);
        const float magnitude = std::fabs(frame.leadMutation) + std::fabs(frame.bassMutation) +
                                std::fabs(frame.rhythmMigration) + std::fabs(frame.densityDrift) +
                                std::fabs(frame.registerTide) + std::fabs(frame.harmonyLens) +
                                std::fabs(frame.paletteDrift);
        if (magnitude > maximumEvolution) {
            maximumEvolution = magnitude;
            strongestStep = step;
        }
        const float delta = std::fabs(frame.leadMutation - previousFrame.leadMutation) +
                            std::fabs(frame.bassMutation - previousFrame.bassMutation) +
                            std::fabs(frame.rhythmMigration - previousFrame.rhythmMigration) +
                            std::fabs(frame.densityDrift - previousFrame.densityDrift) +
                            std::fabs(frame.registerTide - previousFrame.registerTide) +
                            std::fabs(frame.harmonyLens - previousFrame.harmonyLens) +
                            std::fabs(frame.paletteDrift - previousFrame.paletteDrift);
        maximumAdjacentDelta = std::max(maximumAdjacentDelta, delta);
        previousFrame = frame;
    }
    require(maximumEvolution > 0.35f, "indefinite sound develops materially over time");
    require(maximumAdjacentDelta < 1.40f, "development follows bounded interpolated steps");

    float hookDifference = 0.0f;
    float verseDifference = 0.0f;
    float bassDifference = 0.0f;
    int hookCount = 0, verseCount = 0, bassCount = 0;
    for (int32_t pos = 0; pos < 16; ++pos) {
        const int32_t hook0 = evolution.evolvedDegree(evolution.mComposition.motifF[pos], 0, pos, 0,
                rb::MusicEngine::PhraseType::Hook, rb::MusicEngine::SectionType::Hook,
                rb::MusicEngine::MelodyLayer::Lead);
        const int32_t hook1 = evolution.evolvedDegree(evolution.mComposition.motifF[pos], strongestStep, pos, 0,
                rb::MusicEngine::PhraseType::Hook, rb::MusicEngine::SectionType::Hook,
                rb::MusicEngine::MelodyLayer::Lead);
        if (evolution.mComposition.gateF[pos] > 0.02f) {
            hookDifference += std::abs(hook1 - hook0);
            ++hookCount;
        }
        const int32_t verse0 = evolution.evolvedDegree(evolution.mComposition.motifG[pos], 0, pos, 0,
                rb::MusicEngine::PhraseType::Variation, rb::MusicEngine::SectionType::Tide,
                rb::MusicEngine::MelodyLayer::Lead);
        const int32_t verse1 = evolution.evolvedDegree(evolution.mComposition.motifG[pos], strongestStep, pos, 0,
                rb::MusicEngine::PhraseType::Variation, rb::MusicEngine::SectionType::Tide,
                rb::MusicEngine::MelodyLayer::Lead);
        if (evolution.mComposition.gateG[pos] > 0.02f) {
            verseDifference += std::abs(verse1 - verse0);
            ++verseCount;
        }
        const int32_t bass0 = evolution.generatedLayerDegree(rb::MusicEngine::MelodyLayer::Bass, 0, pos, 0,
                rb::MusicEngine::PhraseType::Variation, rb::MusicEngine::SectionType::Variation);
        const int32_t bass1 = evolution.generatedLayerDegree(rb::MusicEngine::MelodyLayer::Bass, strongestStep, pos, 0,
                rb::MusicEngine::PhraseType::Variation, rb::MusicEngine::SectionType::Variation);
        if (evolution.mComposition.bassVerseGate[pos] > 0.02f) {
            bassDifference += std::abs(bass1 - bass0);
            ++bassCount;
        }
    }
    hookDifference /= std::max(1, hookCount);
    verseDifference /= std::max(1, verseCount);
    bassDifference /= std::max(1, bassCount);
    require(hookDifference <= 0.75f, "hook remains recognizable during long-form development");
    require(verseDifference >= hookDifference, "verse admits at least as much variation as the hook");
    require(bassDifference <= 1.25f, "bass development remains bounded to its identity");

    // Manual Next is now the only normal way to create another generated seed.
    rb::MusicEngine manual;
    manual.prepare(48000.0);
    manual.reset(0x77889900u);
    const uint32_t manualSeed = parseUnsigned(manual.currentSongData(), "seed");
    manual.next();
    for (int32_t i = 0; i < 150; ++i) manual.render(block.data(), 2048, 2);
    require(parseUnsigned(manual.currentSongData(), "seed") != manualSeed,
            "manual Next creates a new generated sound");

    // Channel state remains frozen into the current sound and hybrid dominance remains intact.
    rb::MusicEngine channels;
    channels.prepare(48000.0);
    channels.setGenreMask(0);
    channels.setGenreBlendMode(0);
    channels.setGenrePrimary(0);
    channels.reset(100u);
    require(channels.mActiveGenreMask == 0 && channels.mActiveGenrePrimary == 0,
            "No Channel leaves generation unrestricted");

    const int32_t primaryMode = 1;
    const int32_t secondaryMode = 10;
    const int32_t hybridMask = (1 << (primaryMode - 1)) | (1 << (secondaryMode - 1));
    channels.setGenreMask(hybridMask);
    channels.setGenreBlendMode(1);
    channels.setGenrePrimary(primaryMode);
    channels.reset(101u);
    const std::string hybridData = channels.currentSongData();
    require(channels.mActiveGenreBlendMode == 1 && channels.mActiveGenrePrimary == primaryMode,
            "Hybrid Channel freezes the first selected channel as dominant");
    require(parseSigned(hybridData, "gblend") == 1 && parseSigned(hybridData, "gprimary") == primaryMode,
            "Hybrid dominance is serialized with the sound");

    rb::MusicEngine hybridReload;
    hybridReload.prepare(48000.0);
    hybridReload.setGenreMask(1 << (5 - 1));
    hybridReload.setGenreBlendMode(0);
    hybridReload.setGenrePrimary(5);
    require(hybridReload.loadSongData(hybridData), "hybrid sound reloads under a different selector");
    std::vector<float> hybridA(48000 * 2), hybridB(48000 * 2);
    channels.render(hybridA.data(), 48000, 2);
    hybridReload.render(hybridB.data(), 48000, 2);
    float hybridDifference = 0.0f;
    for (std::size_t i = 0; i < hybridA.size(); ++i) {
        hybridDifference = std::max(hybridDifference, std::fabs(hybridA[i] - hybridB[i]));
    }
    require(hybridDifference == 0.0f, "loaded hybrid sound is isolated from the current selector");

    rb::MusicEngine algebra;
    algebra.mCurrentSongSeed = 1234u;
    algebra.mComposition.arcSeed = 5678u;
    const auto base = algebra.profile(rb::MusicEngine::StyleType::ConcretePulse);
    const auto primaryTarget = algebra.channelProfile(primaryMode, base);
    const auto secondaryTarget = algebra.channelProfile(secondaryMode, base);
    algebra.mActiveGenreMask = hybridMask;
    algebra.mActiveGenreBlendMode = 1;
    algebra.mActiveGenrePrimary = primaryMode;
    auto hybridProfile = base;
    algebra.applyChannelBias(hybridProfile);
    require(profileDistance(hybridProfile, primaryTarget) < profileDistance(hybridProfile, secondaryTarget),
            "hybrid character remains closer to the dominant channel");

    // History remains bounded even when users manually request many new sounds.
    rb::MusicEngine history;
    history.prepare(48000.0);
    history.reset(1u);
    for (int32_t i = 0; i < 28; ++i) history.forceNewPiece();
    int32_t historyLines = 0;
    const std::string historyData = history.historyData();
    if (!historyData.empty()) {
        historyLines = 1;
        for (char c : historyData) if (c == '\n') ++historyLines;
    }
    require(historyLines == 20, "native history remains capped at 20 entries");

    // Export is the only finite-duration path and must write the exact requested frame count.
    const std::string pcmPath = "/tmp/technomatic_v25_export_test.pcm";
    std::remove(pcmPath.c_str());
    require(rb::MusicEngine::exportPcm16File(original.currentSongData(), 30, pcmPath),
            "finite PCM export completes");
    std::FILE* pcm = std::fopen(pcmPath.c_str(), "rb");
    long pcmSize = -1;
    if (pcm) {
        std::fseek(pcm, 0, SEEK_END);
        pcmSize = std::ftell(pcm);
        std::fclose(pcm);
    }
    std::remove(pcmPath.c_str());
    require(pcmSize == 30L * 48000L * 2L * static_cast<long>(sizeof(int16_t)),
            "finite export writes exactly the requested duration");

    // Basic render performance and numeric safety.
    rb::MusicEngine performance;
    performance.prepare(48000.0);
    performance.reset(0xabcdef01u);
    std::vector<float> perfBlock(1024 * 2);
    const auto begin = std::chrono::steady_clock::now();
    float peak = 0.0f;
    bool finite = true;
    constexpr int32_t kPerfSeconds = 30;
    for (int32_t i = 0; i < kPerfSeconds * 48000 / 1024; ++i) {
        performance.render(perfBlock.data(), 1024, 2);
        for (float sample : perfBlock) {
            finite = finite && std::isfinite(sample);
            peak = std::max(peak, std::fabs(sample));
        }
    }
    const double renderSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::cout << "INFO 30-second render wall time: " << renderSeconds << " seconds\n";
    std::cout << "INFO rendered peak: " << peak << "\n";
    require(finite && peak <= 1.0f, "render output is finite and bounded");
    require(renderSeconds < 30.0, "native engine renders faster than realtime on validation host");

    std::cout << "INFO unique lead/hook/verse: " << leadIds.size() << "/" << hookIds.size() << "/" << verseIds.size() << " of " << kSeeds << "\n";
    std::cout << "INFO unique bass/base/verse/answer: " << bassIds.size() << "/" << bassVerseIds.size() << "/" << bassAnswerIds.size() << " of " << kSeeds << "\n";
    std::cout << "INFO unique counter/arp/pulse/ornament: " << counterIds.size() << "/" << arpIds.size() << "/" << pulseIds.size() << "/" << ornamentIds.size() << " of " << kSeeds << "\n";
    std::cout << "INFO unique harmony: " << harmonyIds.size() << " of " << kSeeds << "\n";
    std::cout << "INFO strongest evolution step: " << strongestStep << ", magnitude: " << maximumEvolution << ", adjacent delta max: " << maximumAdjacentDelta << "\n";
    std::cout << "INFO hook/verse/bass mean degree change: " << hookDifference << "/" << verseDifference << "/" << bassDifference << "\n";
    std::cout << (ok ? "PASS v25 validation\n" : "FAIL v25 validation\n");
    return ok ? 0 : 1;
}
