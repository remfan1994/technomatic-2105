#include "MusicEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cctype>
#include <cstdio>

namespace rb {

uint32_t MusicEngine::Rng::nextU32() {
    uint32_t x = state;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    state = x ? x : 0x9e3779b9u;
    return state;
}

float MusicEngine::Rng::uni() {
    return static_cast<float>(nextU32() >> 8u) * (1.0f / 16777216.0f);
}

float MusicEngine::Rng::bipolar() {
    return uni() * 2.0f - 1.0f;
}

bool MusicEngine::Rng::chance(float probability) {
    if (probability <= 0.0f) return false;
    if (probability >= 1.0f) return true;
    return uni() < probability;
}

int32_t MusicEngine::Rng::rangeInt(int32_t loInclusive, int32_t hiInclusive) {
    if (hiInclusive <= loInclusive) return loInclusive;
    const uint32_t span = static_cast<uint32_t>(hiInclusive - loInclusive + 1);
    return loInclusive + static_cast<int32_t>(nextU32() % span);
}

MusicEngine::MusicEngine() : mRng(0x52423934u) {
    mSuppressHistoryRecord = true;
    reset(0x52423934u);
    mSuppressHistoryRecord = false;
    clearHistory();
}

void MusicEngine::prepare(double sampleRate) {
    if (sampleRate < 8000.0) sampleRate = 48000.0;
    mSampleRate = sampleRate;

    const int32_t delaySize = std::max(4096, static_cast<int32_t>(mSampleRate * 1.70));
    mDelayL.assign(static_cast<size_t>(delaySize), 0.0f);
    mDelayR.assign(static_cast<size_t>(delaySize), 0.0f);
    mDelayWrite = 0;
    mDelaySamples = std::max(1, static_cast<int32_t>(mSampleRate * 0.315));
    mPrepared = true;
}

void MusicEngine::reset(uint32_t seed) {
    mRng = Rng(seed ? seed : 0x52423934u);
    clearVoicesAndEvents();

    mStepIndex = -1;
    mSamplesUntilNextStep = 0.0;
    mStyleAgeSteps = 0;
    mPhraseSeed = mRng.rangeInt(0, 4095);
    mLeadRunSteps = 0;
    mLastKickStep = -1000;
    mLastSnareStep = -1000;
    mLastBassStep = -1000;
    mLastLeadStep = -1000;
    mSilentSteps = 0;
    mTransitionStage = TransitionStage::None;
    mTransitionGain = 1.0f;
    mSidechain = 1.0f;
    mMaster = 0.82f;
    mAgcRms = 0.010f;
    mAgcGain = 1.0f;
    mNovelty = 0.0f;
    mPocketLate = 0.0015f + mRng.uni() * 0.0120f;
    mTexturePhaseA = 0.0f;
    mTexturePhaseB = 0.0f;
    mTextureLp = 0.0f;
    mTextureHp = 0.0f;
    mTextureNoise = mRng.nextU32();
    mDcInL = mDcInR = mDcOutL = mDcOutR = 0.0f;
    mRecentHash.fill(0u);
    mRecentHashWrite = 0;
    mRecentMotifHash.fill(0u);
    mRecentMotifHashWrite = 0;
    mComposition = Composition{};

    mCurrentSongSeed = seed ? seed : 0x52423934u;
    mPendingSongSeed = mCurrentSongSeed;
    generateSeededSong(mCurrentSongSeed);
    for (auto& slot : mMemory) slot = mPattern;
    mMemoryWrite = 0;

    if (!mDelayL.empty()) std::fill(mDelayL.begin(), mDelayL.end(), 0.0f);
    if (!mDelayR.empty()) std::fill(mDelayR.begin(), mDelayR.end(), 0.0f);
}

void MusicEngine::next() {
    if (mTransitionStage != TransitionStage::None) {
        mPendingSongSeed = mRng.nextU32();
        mPendingStyle = randomDifferentStyle(mPattern.style);
        return;
    }
    storeMemory();
    mPendingSongSeed = mRng.nextU32();
    mPendingStyle = randomDifferentStyle(mPattern.style);
    const float fadeSeconds = 0.12f + 0.20f * mRng.uni();
    mTransitionSamplesTotal = std::max(1, static_cast<int32_t>(fadeSeconds * static_cast<float>(mSampleRate)));
    mTransitionSamplesLeft = mTransitionSamplesTotal;
    mDeadAirSamples = static_cast<int32_t>((0.08f + 0.28f * mRng.uni()) * static_cast<float>(mSampleRate));
    mTransitionStage = TransitionStage::FadeOut;
}

void MusicEngine::forceNewPiece() {
    clearVoicesAndEvents();
    mStepIndex = -1;
    mSamplesUntilNextStep = 0.0;
    mStyleAgeSteps = 0;
    mLeadRunSteps = 0;
    mPhraseSeed = mRng.rangeInt(0, 4095);
    mLastKickStep = -1000;
    mLastSnareStep = -1000;
    mLastBassStep = -1000;
    mLastLeadStep = -1000;
    mSilentSteps = 0;
    mTransitionStage = TransitionStage::None;
    mTransitionGain = 1.0f;
    mSidechain = 1.0f;
    mAgcRms = 0.010f;
    mAgcGain = 1.0f;
    mNovelty = 0.0f;
    mBpm = 0.0f;
    mBpmTarget = 92.0f;
    // Keep recent symbolic hashes across manual Next/genre changes.
    // These hashes are anti-repetition memory, not audio state.
    if (!mDelayL.empty()) std::fill(mDelayL.begin(), mDelayL.end(), 0.0f);
    if (!mDelayR.empty()) std::fill(mDelayR.begin(), mDelayR.end(), 0.0f);
    mDelayWrite = 0;
    mDcInL = mDcInR = mDcOutL = mDcOutR = 0.0f;
    mTextureLp = 0.0f;
    mTextureHp = 0.0f;
    mTexturePhaseA = 0.0f;
    mTexturePhaseB = 0.0f;
    mTextureNoise = mRng.nextU32();
    mSidechain = 1.0f;

    uint32_t seed = mRng.nextU32();
    seed ^= static_cast<uint32_t>(currentGenreMask() + 1) * 0x9e3779b9u;
    seed ^= static_cast<uint32_t>(currentGenreBlendMode() + 17) * 0x85ebca6bu;
    seed ^= static_cast<uint32_t>(mCurrentPieceSamples.load(std::memory_order_acquire) + 0x27d4eb2du);
    if (seed == 0u) seed = 0x52423934u;
    mRng = Rng(seed ^ 0xa511e9b3u);
    generateSeededSong(seed);
    for (auto& slot : mMemory) slot = mPattern;
    mMemoryWrite = 0;
    mCurrentPieceSamples.store(0, std::memory_order_release);
}

void MusicEngine::setPieceLengthSeconds(int32_t seconds) {
    if (seconds < 0) {
        mRandomPieceLength = false;
        mInfinitePieceLength = true;
        mRequestedPieceSeconds = -1;
        mComposition.pieceSteps = 1000000000;
        mStyleTargetSteps = mComposition.pieceSteps;
        updateCurrentSongData();
        return;
    }

    mInfinitePieceLength = false;
    if (seconds == 0) {
        mRandomPieceLength = true;
        mRequestedPieceSeconds = randomPieceSeconds();
    } else {
        mRandomPieceLength = false;
        if (seconds < 8) seconds = 8;
        if (seconds > 999999) seconds = 999999;
        mRequestedPieceSeconds = seconds;
    }

    const int32_t steps = pieceStepsFromSeconds(mRequestedPieceSeconds, std::max(40.0f, mBpmTarget));
    if (steps > 0) {
        mComposition.pieceSteps = steps;
        mStyleTargetSteps = steps;
        updateCurrentSongData();
    }
}


void MusicEngine::setGenreMask(int32_t mask) {
    const int32_t maxMask = (1 << kGenreModeCount) - 1;
    if (mask < 0) mask = 0;
    if (mask > maxMask) mask = maxMask;
    mGenreMask = mask;
}

int32_t MusicEngine::currentGenreMask() const {
    const int32_t maxMask = (1 << kGenreModeCount) - 1;
    int32_t mask = mGenreMask;
    if (mask < 0) mask = 0;
    if (mask > maxMask) mask = maxMask;
    return mask;
}

void MusicEngine::setGenreBlendMode(int32_t mode) {
    if (mode < 0) mode = 0;
    if (mode > 1) mode = 1;
    mGenreBlendMode = mode;
}

int32_t MusicEngine::currentGenreBlendMode() const {
    int32_t mode = mGenreBlendMode;
    if (mode < 0) mode = 0;
    if (mode > 1) mode = 1;
    return mode;
}

int32_t MusicEngine::currentGenreMode() const {
    int32_t mode = mCurrentGenreMode.load(std::memory_order_acquire);
    if (mode < 0) mode = 0;
    if (mode > kGenreModeCount) mode = kGenreModeCount;
    return mode;
}

int32_t MusicEngine::pieceStepsFromSeconds(int32_t seconds, float bpm) const {
    const double safeBpm = std::max(40.0, std::min(220.0, static_cast<double>(bpm)));
    const double rawSteps = static_cast<double>(std::max(8, seconds)) * safeBpm * 4.0 / 60.0;
    int64_t phrases = static_cast<int64_t>(std::llround(rawSteps / static_cast<double>(kPhraseSteps)));
    phrases = std::max<int64_t>(2, phrases);
    phrases = std::min<int64_t>(phrases, 60000000LL / kPhraseSteps);
    return static_cast<int32_t>(phrases * kPhraseSteps);
}

int32_t MusicEngine::randomPieceSeconds() {
    static constexpr int32_t kDurations[] = {30, 60, 180, 300, 600, 1200, 3600};
    static constexpr int32_t kWeights[] = {10, 16, 42, 20, 8, 4, 1};
    int32_t total = 0;
    for (int32_t w : kWeights) total += w;
    int32_t roll = mRng.rangeInt(1, total);
    for (int32_t i = 0; i < static_cast<int32_t>(sizeof(kDurations) / sizeof(kDurations[0])); ++i) {
        roll -= kWeights[i];
        if (roll <= 0) return kDurations[i];
    }
    return 180;
}


std::string MusicEngine::currentSongData() const {
    std::lock_guard<std::mutex> guard(mSongDataMutex);
    return mCurrentSongData.empty() ? std::string("technomatic2105-v1;seed=1379932468;seconds=180;edited=0") : mCurrentSongData;
}

std::string MusicEngine::historyData() const {
    std::lock_guard<std::mutex> guard(mHistoryMutex);
    std::string out;
    for (int32_t i = 0; i < mSongHistorySize; ++i) {
        if (mSongHistory[i].empty()) continue;
        if (!out.empty()) out += '\n';
        out += mSongHistory[i];
    }
    return out;
}

void MusicEngine::clearHistory() {
    std::lock_guard<std::mutex> guard(mHistoryMutex);
    for (auto& entry : mSongHistory) entry.clear();
    mSongHistorySize = 0;
}

double MusicEngine::currentElapsedSeconds() const {
    const double sr = mSampleRate > 1.0 ? mSampleRate : 48000.0;
    return static_cast<double>(mCurrentPieceSamples.load(std::memory_order_acquire)) / sr;
}

int32_t MusicEngine::currentPieceLengthSeconds() const {
    if (mInfinitePieceLength) return -1;
    return std::max(8, std::min(999999, mRequestedPieceSeconds));
}

static bool parseUnsignedField(const std::string& data, const char* key, uint32_t& out) {
    const std::string needle = std::string(key) + "=";
    const size_t pos = data.find(needle);
    if (pos == std::string::npos) return false;
    size_t i = pos + needle.size();
    if (i >= data.size()) return false;
    uint64_t value = 0;
    bool any = false;
    while (i < data.size() && std::isdigit(static_cast<unsigned char>(data[i]))) {
        any = true;
        value = value * 10u + static_cast<uint32_t>(data[i] - '0');
        if (value > 0xffffffffull) return false;
        ++i;
    }
    if (!any) return false;
    out = static_cast<uint32_t>(value);
    return true;
}

static bool parseSignedField(const std::string& data, const char* key, int32_t& out) {
    const std::string needle = std::string(key) + "=";
    const size_t pos = data.find(needle);
    if (pos == std::string::npos) return false;
    size_t i = pos + needle.size();
    if (i >= data.size()) return false;
    bool neg = false;
    if (data[i] == '-') {
        neg = true;
        ++i;
    }
    int64_t value = 0;
    bool any = false;
    while (i < data.size() && std::isdigit(static_cast<unsigned char>(data[i]))) {
        any = true;
        value = value * 10 + static_cast<int32_t>(data[i] - '0');
        if (value > 2147483647LL) return false;
        ++i;
    }
    if (!any) return false;
    out = static_cast<int32_t>(neg ? -value : value);
    return true;
}

static int32_t clampInt32(int32_t value, int32_t lo, int32_t hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

static int32_t q100(float value) {
    const int32_t q = static_cast<int32_t>(std::lround(value * 100.0f));
    return clampInt32(q, 0, 100);
}

static int32_t qSwing(float swing) {
    const int32_t q = static_cast<int32_t>(std::lround(swing * 500.0f));
    return clampInt32(q, 0, 100);
}

static float laneFromValue(int32_t value, float high = 1.08f) {
    value = clampInt32(value, 0, 100);
    if (value <= 0) return 0.0f;
    return 0.16f + (high - 0.16f) * (static_cast<float>(value) / 100.0f);
}

bool MusicEngine::decodeSongData(const std::string& data, uint32_t& seedOut, int32_t& secondsOut) {
    uint32_t seed = 0;
    uint32_t seconds = 0;
    if (!parseUnsignedField(data, "seed", seed)) return false;
    if (!parseUnsignedField(data, "seconds", seconds)) return false;
    if (seconds < 8u || seconds > 999999u) return false;
    seedOut = seed ? seed : 0x52423934u;
    secondsOut = static_cast<int32_t>(seconds);
    return true;
}


bool MusicEngine::exportPcm16File(const std::string& data, int32_t seconds, const std::string& path, const std::atomic<bool>* cancelFlag) {
    if (path.empty()) return false;
    if (seconds < 8 || seconds > 999999) return false;

    MusicEngine engine;
    engine.prepare(48000.0);
    if (!data.empty()) {
        if (!engine.loadSongData(data)) return false;
    }
    engine.setPieceLengthSeconds(seconds);
    // Export is a single generated sound, not the live radio stream.
    // Keep the rendered file sample-accurate and prevent any live-stream auto-advance.
    engine.mExportSinglePieceMode = true;
    engine.mExportStopSamples = static_cast<int64_t>(seconds) * 48000LL;

    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;

    static constexpr int32_t kSampleRate = 48000;
    static constexpr int32_t kChannels = 2;
    static constexpr int32_t kFramesPerChunk = 1024;
    const int64_t totalFrames = static_cast<int64_t>(seconds) * kSampleRate;
    std::vector<float> floats(static_cast<size_t>(kFramesPerChunk * kChannels), 0.0f);
    std::vector<int16_t> pcm(static_cast<size_t>(kFramesPerChunk * kChannels), 0);

    int64_t rendered = 0;
    // Final file fade only. Do not fade for a large part of short exports.
    // A 30-second export should still sound like a 30-second piece, not a 20-second piece.
    const int64_t fadeFrames = std::max<int64_t>(kSampleRate / 8,
            std::min<int64_t>(static_cast<int64_t>(kSampleRate) / 2, totalFrames / 40));
    const int64_t fadeStart = std::max<int64_t>(0, totalFrames - fadeFrames);
    bool ok = true;
    while (rendered < totalFrames) {
        if (cancelFlag && cancelFlag->load(std::memory_order_relaxed)) {
            ok = false;
            break;
        }
        const int32_t frames = static_cast<int32_t>(std::min<int64_t>(kFramesPerChunk, totalFrames - rendered));
        std::fill(floats.begin(), floats.begin() + static_cast<size_t>(frames * kChannels), 0.0f);
        engine.render(floats.data(), frames, kChannels);
        for (int32_t frame = 0; frame < frames; ++frame) {
            const int64_t absoluteFrame = rendered + frame;
            float tailGain = 1.0f;
            if (absoluteFrame >= fadeStart && fadeFrames > 0) {
                const float remain = static_cast<float>(totalFrames - absoluteFrame) / static_cast<float>(fadeFrames);
                tailGain = engine.clamp(remain * remain, 0.0f, 1.0f);
            }
            for (int32_t ch = 0; ch < kChannels; ++ch) {
                const int32_t i = frame * kChannels + ch;
                float v = engine.clamp(floats[static_cast<size_t>(i)] * tailGain, -1.0f, 1.0f);
                pcm[static_cast<size_t>(i)] = static_cast<int16_t>(std::lrint(v * 32767.0f));
            }
        }
        const size_t wrote = std::fwrite(pcm.data(), sizeof(int16_t), static_cast<size_t>(frames * kChannels), file);
        if (wrote != static_cast<size_t>(frames * kChannels)) {
            ok = false;
            break;
        }
        rendered += frames;
    }

    if (std::fclose(file) != 0) ok = false;
    return ok;
}

bool MusicEngine::loadSongData(const std::string& data) {
    uint32_t seed = 0;
    int32_t seconds = 0;
    if (!decodeSongData(data, seed, seconds)) return false;

    const int32_t oldMask = mGenreMask;
    const int32_t oldBlend = mGenreBlendMode;
    int32_t savedMask = 0;
    int32_t savedBlend = 0;
    int32_t savedCandidate = -1;
    const bool hasSavedMask = parseSignedField(data, "gmask", savedMask);
    const bool hasSavedBlend = parseSignedField(data, "gblend", savedBlend);
    const bool hasSavedCandidate = parseSignedField(data, "cand", savedCandidate);
    if (hasSavedMask) mGenreMask = clampInt32(savedMask, 0, (1 << kGenreModeCount) - 1);
    if (hasSavedBlend) mGenreBlendMode = clampInt32(savedBlend, 0, 1);
    mForcedCandidateIndex = hasSavedCandidate ? clampInt32(savedCandidate, 0, 47) : -1;

    setPieceLengthSeconds(seconds);
    const bool oldSuppressHistory = mSuppressHistoryRecord;
    mSuppressHistoryRecord = true;
    reset(seed);
    mSuppressHistoryRecord = oldSuppressHistory;
    mForcedCandidateIndex = -1;

    if (hasSavedMask) mGenreMask = oldMask;
    if (hasSavedBlend) mGenreBlendMode = oldBlend;

    int32_t edited = 0;
    const bool hasExtendedGeneratorData = data.find(";style=") != std::string::npos ||
                                          data.find(";tempo=") != std::string::npos ||
                                          data.find(";motif=") != std::string::npos;
    if ((parseSignedField(data, "edited", edited) && edited == 1) || (!hasSavedMask && hasExtendedGeneratorData)) {
        applySongDataOverrides(data);
    }
    {
        std::lock_guard<std::mutex> guard(mSongDataMutex);
        mCurrentSongData = data;
    }
    return true;
}

void MusicEngine::updateCurrentSongData() {
    auto appendField = [](std::string& out, const char* key, int32_t value) {
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), ";%s=%d", key, value);
        out += buffer;
    };

    const int32_t seconds = std::max(8, std::min(999999, mRequestedPieceSeconds));
    char header[96];
    std::snprintf(header, sizeof(header), "technomatic2105-v1;seed=%u;seconds=%d", mCurrentSongSeed, seconds);
    std::string out(header);
    appendField(out, "edited", mCurrentSongEdited ? 1 : 0);
    appendField(out, "gmask", clampInt32(mActiveGenreMask, 0, (1 << kGenreModeCount) - 1));
    appendField(out, "gblend", clampInt32(mActiveGenreBlendMode, 0, 1));
    appendField(out, "gmode", currentGenreMode());
    appendField(out, "cand", clampInt32(mCurrentCandidateIndex, 0, 47));

    appendField(out, "style", clampInt32(static_cast<int32_t>(mPattern.style), 0, static_cast<int32_t>(StyleType::Count) - 1));
    appendField(out, "tempo", clampInt32(static_cast<int32_t>(std::lround(mBpmTarget)), 40, 220));
    appendField(out, "root", clampInt32(mPattern.rootMidi, 24, 60));
    appendField(out, "scale", clampInt32(mPattern.scaleMode, 0, 4));
    appendField(out, "density", q100(mPattern.density));
    appendField(out, "swing", qSwing(mPattern.swing));
    appendField(out, "melody", q100(mPattern.melody));
    appendField(out, "motif", q100((mComposition.motifGain - 0.55f) / 1.75f));
    appendField(out, "drama", q100(mComposition.drama));
    appendField(out, "space", q100(mPattern.space));
    appendField(out, "roughness", q100(mPattern.roughness));
    appendField(out, "brightness", q100((mComposition.leadTone + mComposition.sparkTone + mComposition.sheenTone) / 3.0f));
    const float diversity = (mComposition.useArp + mComposition.useCounter + mComposition.useStab + mComposition.useTexture +
                             mComposition.useDrone + mComposition.useSpark + mComposition.useFx + mComposition.useEcho +
                             mComposition.useOrbit + mComposition.useBloom + mComposition.useGlyph + mComposition.useSub +
                             mComposition.useSheen + mComposition.usePluck + mComposition.useBell + mComposition.usePulse +
                             mComposition.useGrain + mComposition.useComet + mComposition.useRotor) / 13.8f;
    appendField(out, "diversity", q100(diversity));
    appendField(out, "kick", q100(mComposition.useKick));
    appendField(out, "snare", q100(mComposition.useSnare));
    appendField(out, "hats", q100(mComposition.useHat));
    appendField(out, "openhat", q100(mComposition.useOpenHat));
    appendField(out, "perc", q100(mComposition.usePerc));
    appendField(out, "bass", q100(mComposition.useBass));
    appendField(out, "sub", q100(mComposition.useSub));
    appendField(out, "chords", q100(mComposition.useChord));
    appendField(out, "lead", q100(mComposition.useLead));
    appendField(out, "arp", q100(mComposition.useArp));
    appendField(out, "counter", q100(mComposition.useCounter));
    appendField(out, "stab", q100(mComposition.useStab));
    appendField(out, "texture", q100(mComposition.useTexture));
    appendField(out, "drone", q100(mComposition.useDrone));
    appendField(out, "spark", q100(mComposition.useSpark));
    appendField(out, "fx", q100(mComposition.useFx));
    appendField(out, "echo", q100(mComposition.useEcho));
    appendField(out, "orbit", q100(mComposition.useOrbit));
    appendField(out, "bloom", q100(mComposition.useBloom));
    appendField(out, "glyph", q100(mComposition.useGlyph));
    appendField(out, "sheen", q100(mComposition.useSheen));
    appendField(out, "pluck", q100(mComposition.usePluck));
    appendField(out, "bell", q100(mComposition.useBell));
    appendField(out, "pulse", q100(mComposition.usePulse));
    appendField(out, "grain", q100(mComposition.useGrain));
    appendField(out, "comet", q100(mComposition.useComet));
    appendField(out, "rotor", q100(mComposition.useRotor));

    std::lock_guard<std::mutex> guard(mSongDataMutex);
    mCurrentSongData = out;
}

void MusicEngine::recordCurrentSongDataToHistory() {
    if (mSuppressHistoryRecord) return;
    std::string data;
    {
        std::lock_guard<std::mutex> guard(mSongDataMutex);
        data = mCurrentSongData;
    }
    if (data.empty()) return;

    std::lock_guard<std::mutex> guard(mHistoryMutex);
    if (mSongHistorySize > 0 && mSongHistory[mSongHistorySize - 1] == data) return;

    if (mSongHistorySize < kSongHistoryLimit) {
        mSongHistory[mSongHistorySize++] = data;
        return;
    }

    for (int32_t i = 1; i < kSongHistoryLimit; ++i) {
        mSongHistory[i - 1] = mSongHistory[i];
    }
    mSongHistory[kSongHistoryLimit - 1] = data;
    mSongHistorySize = kSongHistoryLimit;
}

void MusicEngine::applySongDataOverrides(const std::string& data) {
    mCurrentSongEdited = true;
    int32_t value = 0;
    if (parseSignedField(data, "style", value)) {
        value = clampInt32(value, 0, static_cast<int32_t>(StyleType::Count) - 1);
        const StyleType requestedStyle = static_cast<StyleType>(value);
        if (requestedStyle != mPattern.style) {
            mRng = Rng(mCurrentSongSeed ^ (0x9e3779b9u * static_cast<uint32_t>(value + 1)));
            generatePattern(requestedStyle);
        }
    }

    if (parseSignedField(data, "tempo", value)) {
        mBpmTarget = static_cast<float>(clampInt32(value, 40, 220));
        mBpm = mBpmTarget;
        mComposition.pieceSteps = pieceStepsFromSeconds(mRequestedPieceSeconds, mBpmTarget);
        mStyleTargetSteps = mComposition.pieceSteps;
    }
    if (parseSignedField(data, "root", value)) mPattern.rootMidi = clampInt32(value, 24, 60);
    if (parseSignedField(data, "scale", value)) mPattern.scaleMode = clampInt32(value, 0, 4);
    if (parseSignedField(data, "density", value)) {
        mPattern.density = clamp01(static_cast<float>(clampInt32(value, 0, 100)) / 100.0f);
        mPattern.energy = clamp(0.28f + mPattern.density * 0.62f, 0.10f, 0.98f);
    }
    if (parseSignedField(data, "swing", value)) mPattern.swing = static_cast<float>(clampInt32(value, 0, 100)) / 500.0f;
    if (parseSignedField(data, "melody", value)) {
        const float x = static_cast<float>(clampInt32(value, 0, 100)) / 100.0f;
        mPattern.melody = clamp01(x);
        mComposition.useLead = std::max(mComposition.useLead, laneFromValue(value, 1.10f));
        mComposition.useCounter = std::max(mComposition.useCounter, laneFromValue(static_cast<int32_t>(value * 0.72f), 0.88f));
        mComposition.useArp = std::max(mComposition.useArp, laneFromValue(static_cast<int32_t>(value * 0.68f), 0.88f));
    }
    if (parseSignedField(data, "motif", value)) {
        const float x = static_cast<float>(clampInt32(value, 0, 100)) / 100.0f;
        mComposition.motifGain = clamp(0.55f + 1.75f * x, 0.45f, 2.35f);
        mComposition.hookEmphasis = clamp(0.72f + 1.40f * x, 0.70f, 2.20f);
    }
    if (parseSignedField(data, "drama", value)) {
        const float x = static_cast<float>(clampInt32(value, 0, 100)) / 100.0f;
        mComposition.drama = clamp01(x);
        mComposition.deviceDepth = clamp(0.16f + 0.84f * x, 0.12f, 1.05f);
        mComposition.surgeLift = clamp(0.82f + 1.20f * x, 0.70f, 2.20f);
    }
    if (parseSignedField(data, "space", value)) mPattern.space = clamp01(static_cast<float>(clampInt32(value, 0, 100)) / 100.0f);
    if (parseSignedField(data, "roughness", value)) {
        const float x = static_cast<float>(clampInt32(value, 0, 100)) / 100.0f;
        mPattern.roughness = clamp01(x);
        mPattern.drive = clamp(0.22f + 0.72f * x, 0.12f, 1.05f);
    }
    if (parseSignedField(data, "brightness", value)) {
        const float x = static_cast<float>(clampInt32(value, 0, 100)) / 100.0f;
        mComposition.leadTone = clamp01(0.20f + 0.78f * x);
        mComposition.arpTone = clamp01(0.18f + 0.80f * x);
        mComposition.sparkTone = clamp01(0.24f + 0.74f * x);
        mComposition.sheenTone = clamp01(0.26f + 0.72f * x);
        mComposition.hatTone = clamp01(0.16f + 0.80f * x);
    }
    if (parseSignedField(data, "diversity", value)) {
        const float x = static_cast<float>(clampInt32(value, 0, 100)) / 100.0f;
        if (x < 0.35f) {
            mComposition.useArp *= 0.45f; mComposition.useCounter *= 0.45f; mComposition.useStab *= 0.55f;
            mComposition.useDrone *= 0.45f; mComposition.useSpark *= 0.45f; mComposition.useFx *= 0.45f;
            mComposition.useOrbit *= 0.45f; mComposition.useBloom *= 0.45f; mComposition.useGlyph *= 0.45f;
            mComposition.usePluck *= 0.45f; mComposition.useBell *= 0.45f; mComposition.usePulse *= 0.45f;
            mComposition.useGrain *= 0.45f; mComposition.useComet *= 0.45f; mComposition.useRotor *= 0.45f;
        } else {
            const int32_t v = clampInt32(value, 0, 100);
            mComposition.useArp = std::max(mComposition.useArp, laneFromValue(v - 8, 0.92f));
            mComposition.useCounter = std::max(mComposition.useCounter, laneFromValue(v - 14, 0.82f));
            mComposition.useStab = std::max(mComposition.useStab, laneFromValue(v - 12, 0.84f));
            mComposition.useTexture = std::max(mComposition.useTexture, laneFromValue(v - 10, 0.88f));
            mComposition.useDrone = std::max(mComposition.useDrone, laneFromValue(v - 22, 0.74f));
            mComposition.useSpark = std::max(mComposition.useSpark, laneFromValue(v - 18, 0.82f));
            mComposition.useFx = std::max(mComposition.useFx, laneFromValue(v - 24, 0.78f));
            mComposition.useEcho = std::max(mComposition.useEcho, laneFromValue(v - 16, 0.84f));
            mComposition.useOrbit = std::max(mComposition.useOrbit, laneFromValue(v - 24, 0.80f));
            mComposition.useBloom = std::max(mComposition.useBloom, laneFromValue(v - 20, 0.82f));
            mComposition.useGlyph = std::max(mComposition.useGlyph, laneFromValue(v - 28, 0.76f));
            mComposition.useSheen = std::max(mComposition.useSheen, laneFromValue(v - 24, 0.78f));
            mComposition.usePluck = std::max(mComposition.usePluck, laneFromValue(v - 18, 0.92f));
            mComposition.useBell = std::max(mComposition.useBell, laneFromValue(v - 26, 0.84f));
            mComposition.usePulse = std::max(mComposition.usePulse, laneFromValue(v - 14, 0.88f));
            mComposition.useGrain = std::max(mComposition.useGrain, laneFromValue(v - 28, 0.80f));
            mComposition.useComet = std::max(mComposition.useComet, laneFromValue(v - 30, 0.78f));
            mComposition.useRotor = std::max(mComposition.useRotor, laneFromValue(v - 24, 0.82f));
        }
    }

    auto lane = [&](const char* key, float& target, float high) {
        int32_t v = 0;
        if (parseSignedField(data, key, v)) target = laneFromValue(v, high);
    };
    lane("kick", mComposition.useKick, 1.10f);
    lane("snare", mComposition.useSnare, 1.10f);
    lane("hats", mComposition.useHat, 1.10f);
    lane("openhat", mComposition.useOpenHat, 0.98f);
    lane("perc", mComposition.usePerc, 1.05f);
    lane("bass", mComposition.useBass, 1.16f);
    lane("sub", mComposition.useSub, 1.02f);
    lane("chords", mComposition.useChord, 1.08f);
    lane("lead", mComposition.useLead, 1.14f);
    lane("arp", mComposition.useArp, 1.00f);
    lane("counter", mComposition.useCounter, 0.92f);
    lane("stab", mComposition.useStab, 0.96f);
    lane("texture", mComposition.useTexture, 1.00f);
    lane("drone", mComposition.useDrone, 0.92f);
    lane("spark", mComposition.useSpark, 0.92f);
    lane("fx", mComposition.useFx, 0.88f);
    lane("echo", mComposition.useEcho, 0.92f);
    lane("orbit", mComposition.useOrbit, 0.90f);
    lane("bloom", mComposition.useBloom, 0.96f);
    lane("glyph", mComposition.useGlyph, 0.88f);
    lane("sheen", mComposition.useSheen, 0.88f);
    lane("pluck", mComposition.usePluck, 0.94f);
    lane("bell", mComposition.useBell, 0.88f);
    lane("pulse", mComposition.usePulse, 0.92f);
    lane("grain", mComposition.useGrain, 0.86f);
    lane("comet", mComposition.useComet, 0.84f);
    lane("rotor", mComposition.useRotor, 0.88f);

    repairPattern();
    mCurrentPieceSamples.store(0, std::memory_order_release);
}

void MusicEngine::generateSeededSong(uint32_t seed) {
    const uint32_t requestedSeed = seed ? seed : 0x52423934u;
    // Freeze the selector state into this piece. The live selector can change later,
    // but the sounding piece and saved/history/export data must remain self-contained.
    mActiveGenreMask = currentGenreMask();
    mActiveGenreBlendMode = currentGenreBlendMode();
    mCurrentSongSeed = requestedSeed;
    mCurrentSongEdited = false;
    mCurrentPieceSamples.store(0, std::memory_order_release);
    if (mRandomPieceLength) {
        mRequestedPieceSeconds = randomPieceSeconds();
    }

    const auto recentCopy = mRecentHash;
    const int32_t recentWriteCopy = mRecentHashWrite;
    const auto recentMotifCopy = mRecentMotifHash;
    const int32_t recentMotifWriteCopy = mRecentMotifHashWrite;

    auto inRecentPatternCopy = [&](uint32_t hash) {
        if (hash == 0u) return false;
        for (uint32_t h : recentCopy) {
            if (h == hash) return true;
        }
        return false;
    };
    auto inRecentMotifCopy = [&](uint32_t hash) {
        if (hash == 0u) return false;
        for (uint32_t h : recentMotifCopy) {
            if (h == hash) return true;
        }
        return false;
    };

    Pattern bestPattern{};
    Composition bestComposition{};
    Rng bestRng(mCurrentSongSeed);
    float bestBpm = 92.0f;
    float bestBpmTarget = 92.0f;
    int32_t bestStyleTarget = 0;
    int32_t bestGenreMode = 0;
    int32_t bestCandidateIndex = 0;
    float bestScore = -1000000.0f;
    const int32_t forcedCandidateIndex = clampInt32(mForcedCandidateIndex, -1, 47);

    for (int32_t i = 0; i < 48; ++i) {
        if (forcedCandidateIndex >= 0 && i != forcedCandidateIndex) continue;
        const uint32_t salt = 0x9e3779b9u * static_cast<uint32_t>(i + 1) + 0x85ebca6bu;
        const uint32_t candidateSeed = (i == 0) ? mCurrentSongSeed : (mCurrentSongSeed ^ salt);
        mRecentHash = recentCopy;
        mRecentHashWrite = recentWriteCopy;
        mRng = Rng(candidateSeed);
        mBpm = 0.0f;
        mBpmTarget = 92.0f;
        const StyleType initial = randomStyle();
        const int32_t candidateGenreMode = mWorkingGenreMode;
        generatePattern(initial);
        const uint32_t candidatePatternHash = patternHash();
        const uint32_t candidateMotifHash = motifSignatureHash();
        float score = scoreCurrentComposition() + 0.010f * static_cast<float>(mRng.rangeInt(0, 100));
        if (forcedCandidateIndex < 0) {
            if (inRecentPatternCopy(candidatePatternHash)) score -= 1.10f;
            if (inRecentMotifCopy(candidateMotifHash)) score -= 1.75f;
            if ((i % 7) == 0 && i > 0) score += 0.025f * static_cast<float>(i / 7);
        }
        if (score > bestScore) {
            bestScore = score;
            bestPattern = mPattern;
            bestComposition = mComposition;
            bestRng = mRng;
            bestBpm = mBpm;
            bestBpmTarget = mBpmTarget;
            bestStyleTarget = mStyleTargetSteps;
            bestGenreMode = candidateGenreMode;
            bestCandidateIndex = i;
        }
    }

    mCurrentSongSeed = requestedSeed;
    mCurrentCandidateIndex = bestCandidateIndex;
    mPattern = bestPattern;
    mComposition = bestComposition;
    mRng = bestRng;
    mBpm = bestBpm;
    mBpmTarget = bestBpmTarget;
    mStyleTargetSteps = bestStyleTarget;
    mCurrentGenreMode.store(bestGenreMode, std::memory_order_release);
    mRecentHash = recentCopy;
    mRecentHashWrite = recentWriteCopy;
    mRecentMotifHash = recentMotifCopy;
    mRecentMotifHashWrite = recentMotifWriteCopy;
    const uint32_t h = patternHash();
    mRecentHash[mRecentHashWrite] = h;
    mRecentHashWrite = (mRecentHashWrite + 1) % kRecentHashes;
    const uint32_t mh = motifSignatureHash();
    mRecentMotifHash[mRecentMotifHashWrite] = mh;
    mRecentMotifHashWrite = (mRecentMotifHashWrite + 1) % kRecentMotifHashes;
    repairPattern();
    updateCurrentSongData();
    recordCurrentSongDataToHistory();
}

float MusicEngine::scoreCurrentComposition() const {
    int32_t leadHits = 0;
    int32_t answerHits = 0;
    int32_t bassHits = 0;
    int32_t chordHits = 0;
    int32_t uniqueMask = 0;
    int32_t minDegree = 99;
    int32_t maxDegree = -99;
    int32_t repeatedCells = 0;
    int32_t directionalMoves = 0;
    int32_t directionChanges = 0;
    int32_t largeLeaps = 0;
    int32_t lastDegree = 999;
    int32_t lastDir = 0;
    int32_t strongAnchors = 0;
    int32_t answerContrast = 0;
    int32_t rhythmicCells = 0;

    for (int32_t i = 0; i < kPhraseSteps; ++i) {
        if (mComposition.gateA[i] > 0.20f) {
            ++leadHits;
            const int32_t d = mComposition.motifA[i];
            minDegree = std::min(minDegree, d);
            maxDegree = std::max(maxDegree, d);
            uniqueMask |= (1 << ((d + 12) & 31));
            if (i == 0 || i == 8 || i == 15) {
                const int32_t ad = std::abs(d);
                if (ad == 0 || ad == 2 || ad == 4 || ad == 5 || ad == 7) ++strongAnchors;
            }
            if (lastDegree != 999) {
                const int32_t diff = d - lastDegree;
                const int32_t dir = (diff > 0) ? 1 : (diff < 0 ? -1 : 0);
                if (dir != 0) {
                    ++directionalMoves;
                    if (lastDir != 0 && dir != lastDir) ++directionChanges;
                    lastDir = dir;
                }
                if (std::abs(diff) > 5) ++largeLeaps;
            }
            lastDegree = d;
        }
        if (mComposition.gateB[i] > 0.20f || mComposition.gateC[i] > 0.20f) ++answerHits;
        if (mComposition.bassGate[i] > 0.20f) ++bassHits;
        if (mComposition.chordGate[i] > 0.20f) ++chordHits;
        if ((mComposition.gateA[i] > 0.20f) != (mComposition.gateA[(i + 8) & 15] > 0.20f)) ++rhythmicCells;
        if (mComposition.gateB[i] > 0.20f && std::abs(mComposition.motifB[i] - mComposition.motifA[i]) >= 2) ++answerContrast;
    }

    for (int32_t i = 0; i < 8; ++i) {
        if (mComposition.gateA[i] > 0.20f && mComposition.gateA[i + 8] > 0.20f) {
            if (std::abs(mComposition.motifA[i] - mComposition.motifA[i + 8]) <= 2) ++repeatedCells;
        }
    }

    int32_t uniqueDegrees = 0;
    for (int32_t i = 0; i < 32; ++i) if (uniqueMask & (1 << i)) ++uniqueDegrees;
    const int32_t span = maxDegree > minDegree ? (maxDegree - minDegree) : 0;

    const float lanes = mComposition.useKick + mComposition.useSnare + mComposition.useHat +
            mComposition.usePerc + mComposition.useBass + mComposition.useChord + mComposition.useLead +
            mComposition.useArp + mComposition.useCounter + mComposition.useTexture + mComposition.useDrone +
            mComposition.useSpark + mComposition.useEcho + mComposition.useBloom + mComposition.usePluck +
            mComposition.useBell + mComposition.usePulse + mComposition.useGrain + mComposition.useComet +
            mComposition.useRotor;

    const float melodic = clamp(static_cast<float>(leadHits) / 8.0f, 0.0f, 1.6f) +
            0.35f * clamp(static_cast<float>(answerHits) / 8.0f, 0.0f, 1.6f) +
            0.24f * clamp(static_cast<float>(uniqueDegrees) / 5.0f, 0.0f, 1.5f) +
            0.24f * clamp(static_cast<float>(span) / 7.0f, 0.0f, 1.5f);
    const float foundation = 0.60f * clamp(static_cast<float>(bassHits) / 4.0f, 0.0f, 1.4f) +
            0.28f * clamp(static_cast<float>(chordHits) / 2.0f, 0.0f, 1.2f);
    const float phraseLogic = 0.18f * clamp(static_cast<float>(repeatedCells) / 3.0f, 0.0f, 1.4f) +
            0.18f * clamp(static_cast<float>(directionChanges) / 3.0f, 0.0f, 1.4f) +
            0.14f * clamp(static_cast<float>(strongAnchors) / 3.0f, 0.0f, 1.3f) +
            0.12f * clamp(static_cast<float>(answerContrast) / 4.0f, 0.0f, 1.3f) +
            0.10f * clamp(static_cast<float>(rhythmicCells) / 5.0f, 0.0f, 1.2f);
    const float layerTarget = mPattern.space > 0.70f ? 7.0f : 11.0f;
    const float layerPenalty = std::fabs(lanes - layerTarget) * 0.055f;
    const float grammar = 0.38f * mComposition.longMemory +
            0.34f * mComposition.callResponse +
            0.26f * mComposition.counterpoint +
            0.22f * mComposition.melodicGravity;

    float score = melodic + foundation + grammar + phraseLogic - layerPenalty;
    if (leadHits < 4) score -= 1.20f;
    if (bassHits < 2) score -= 0.65f;
    if (span <= 1 && mPattern.melody > 0.45f) score -= 0.55f;
    if (uniqueDegrees < 3 && mPattern.melody > 0.45f) score -= 0.42f;
    if (directionalMoves > 0 && directionChanges == 0 && mPattern.melody > 0.50f) score -= 0.30f;
    if (largeLeaps > 3) score -= 0.26f;
    if (lanes < 5.0f) score -= 0.80f;
    if (lanes > 18.0f && mPattern.space < 0.40f) score -= 0.35f;
    return score;
}

MusicEngine::StyleProfile MusicEngine::profile(StyleType style) const {
    StyleProfile p;
    p.type = style;

    switch (style) {
        case StyleType::ConcretePulse:
            p.bpmMin = 78.0f; p.bpmMax = 98.0f;
            p.swingMin = 0.08f; p.swingMax = 0.18f;
            p.density = 0.50f; p.drum = 0.72f; p.bass = 0.64f; p.melody = 0.38f; p.chord = 0.24f;
            p.texture = 0.16f; p.rough = 0.48f; p.space = 0.38f; p.sync = 0.62f;
            p.hatRoll = 0.05f; p.melodyRun = 0.24f; p.transitionSilence = 0.35f;
            p.drama = 0.42f; p.palette = 0.52f; p.brightness = 0.38f;
            break;
        case StyleType::GlassNoir:
            p.bpmMin = 128.0f; p.bpmMax = 154.0f;
            p.swingMin = 0.01f; p.swingMax = 0.08f;
            p.density = 0.55f; p.drum = 0.66f; p.bass = 0.84f; p.melody = 0.46f; p.chord = 0.18f;
            p.texture = 0.27f; p.rough = 0.42f; p.space = 0.48f; p.sync = 0.54f;
            p.hatRoll = 0.56f; p.melodyRun = 0.22f; p.transitionSilence = 0.30f;
            p.drama = 0.58f; p.palette = 0.48f; p.brightness = 0.58f;
            p.halfTime = true; p.trapHats = true;
            break;
        case StyleType::ShardRush:
            p.bpmMin = 154.0f; p.bpmMax = 174.0f;
            p.swingMin = 0.00f; p.swingMax = 0.06f;
            p.density = 0.80f; p.drum = 0.88f; p.bass = 0.62f; p.melody = 0.34f; p.chord = 0.14f;
            p.texture = 0.20f; p.rough = 0.60f; p.space = 0.20f; p.sync = 0.76f;
            p.hatRoll = 0.24f; p.melodyRun = 0.16f; p.transitionSilence = 0.18f;
            p.drama = 0.50f; p.palette = 0.70f; p.brightness = 0.70f;
            p.breakbeat = true;
            break;
        case StyleType::NeonLatch:
            p.bpmMin = 104.0f; p.bpmMax = 128.0f;
            p.swingMin = 0.03f; p.swingMax = 0.13f;
            p.density = 0.64f; p.drum = 0.74f; p.bass = 0.76f; p.melody = 0.62f; p.chord = 0.30f;
            p.texture = 0.18f; p.rough = 0.38f; p.space = 0.30f; p.sync = 0.72f;
            p.hatRoll = 0.14f; p.melodyRun = 0.54f; p.transitionSilence = 0.24f;
            p.drama = 0.46f; p.palette = 0.62f; p.brightness = 0.62f;
            break;
        case StyleType::TinyGrid:
            p.bpmMin = 116.0f; p.bpmMax = 128.0f;
            p.swingMin = 0.00f; p.swingMax = 0.04f;
            p.density = 0.54f; p.drum = 0.62f; p.bass = 0.48f; p.melody = 0.30f; p.chord = 0.42f;
            p.texture = 0.26f; p.rough = 0.20f; p.space = 0.56f; p.sync = 0.40f;
            p.hatRoll = 0.06f; p.melodyRun = 0.14f; p.transitionSilence = 0.46f;
            p.drama = 0.30f; p.palette = 0.44f; p.brightness = 0.46f;
            p.fourOnFloor = true;
            break;
        case StyleType::PrismCruise:
            p.bpmMin = 86.0f; p.bpmMax = 112.0f;
            p.swingMin = 0.00f; p.swingMax = 0.04f;
            p.density = 0.52f; p.drum = 0.54f; p.bass = 0.70f; p.melody = 0.76f; p.chord = 0.62f;
            p.texture = 0.32f; p.rough = 0.16f; p.space = 0.40f; p.sync = 0.36f;
            p.hatRoll = 0.03f; p.melodyRun = 0.72f; p.transitionSilence = 0.32f;
            p.drama = 0.52f; p.palette = 0.56f; p.brightness = 0.78f;
            break;
        case StyleType::BrokenMagnet:
            p.bpmMin = 90.0f; p.bpmMax = 116.0f;
            p.swingMin = 0.02f; p.swingMax = 0.11f;
            p.density = 0.68f; p.drum = 0.80f; p.bass = 0.68f; p.melody = 0.44f; p.chord = 0.22f;
            p.texture = 0.30f; p.rough = 0.78f; p.space = 0.30f; p.sync = 0.84f;
            p.hatRoll = 0.20f; p.melodyRun = 0.32f; p.transitionSilence = 0.18f;
            p.drama = 0.64f; p.palette = 0.76f; p.brightness = 0.52f;
            break;
        case StyleType::VelvetDrift:
            p.bpmMin = 62.0f; p.bpmMax = 88.0f;
            p.swingMin = 0.03f; p.swingMax = 0.12f;
            p.density = 0.24f; p.drum = 0.20f; p.bass = 0.38f; p.melody = 0.64f; p.chord = 0.82f;
            p.texture = 0.76f; p.rough = 0.12f; p.space = 0.82f; p.sync = 0.30f;
            p.hatRoll = 0.01f; p.melodyRun = 0.52f; p.transitionSilence = 0.72f;
            p.drama = 0.38f; p.palette = 0.40f; p.brightness = 0.38f;
            p.ambient = true;
            break;
        case StyleType::SubOrbit:
            p.bpmMin = 122.0f; p.bpmMax = 142.0f;
            p.swingMin = 0.00f; p.swingMax = 0.08f;
            p.density = 0.50f; p.drum = 0.60f; p.bass = 0.86f; p.melody = 0.30f; p.chord = 0.36f;
            p.texture = 0.44f; p.rough = 0.34f; p.space = 0.62f; p.sync = 0.50f;
            p.hatRoll = 0.08f; p.melodyRun = 0.16f; p.transitionSilence = 0.58f;
            p.drama = 0.52f; p.palette = 0.50f; p.brightness = 0.34f;
            p.halfTime = true;
            break;
        case StyleType::SoftVoltage:
            p.bpmMin = 68.0f; p.bpmMax = 96.0f;
            p.swingMin = 0.02f; p.swingMax = 0.10f;
            p.density = 0.34f; p.drum = 0.34f; p.bass = 0.46f; p.melody = 0.82f; p.chord = 0.74f;
            p.texture = 0.56f; p.rough = 0.10f; p.space = 0.72f; p.sync = 0.32f;
            p.hatRoll = 0.02f; p.melodyRun = 0.64f; p.transitionSilence = 0.62f;
            p.drama = 0.44f; p.palette = 0.42f; p.brightness = 0.66f;
            p.ambient = true;
            break;
        case StyleType::DeepMagnet:
            p.bpmMin = 82.0f; p.bpmMax = 108.0f;
            p.swingMin = 0.01f; p.swingMax = 0.09f;
            p.density = 0.38f; p.drum = 0.48f; p.bass = 0.94f; p.melody = 0.26f; p.chord = 0.28f;
            p.texture = 0.52f; p.rough = 0.48f; p.space = 0.60f; p.sync = 0.44f;
            p.hatRoll = 0.05f; p.melodyRun = 0.10f; p.transitionSilence = 0.50f;
            p.drama = 0.72f; p.palette = 0.46f; p.brightness = 0.24f;
            p.halfTime = true;
            break;
        case StyleType::WarmCurrent:
            p.bpmMin = 86.0f; p.bpmMax = 112.0f;
            p.swingMin = 0.05f; p.swingMax = 0.16f;
            p.density = 0.58f; p.drum = 0.62f; p.bass = 0.72f; p.melody = 0.74f; p.chord = 0.64f;
            p.texture = 0.38f; p.rough = 0.18f; p.space = 0.42f; p.sync = 0.58f;
            p.hatRoll = 0.04f; p.melodyRun = 0.62f; p.transitionSilence = 0.34f;
            p.drama = 0.55f; p.palette = 0.82f; p.brightness = 0.64f;
            break;
        case StyleType::PulseGarden:
            p.bpmMin = 72.0f; p.bpmMax = 104.0f;
            p.swingMin = 0.03f; p.swingMax = 0.14f;
            p.density = 0.46f; p.drum = 0.46f; p.bass = 0.56f; p.melody = 0.78f; p.chord = 0.76f;
            p.texture = 0.58f; p.rough = 0.08f; p.space = 0.64f; p.sync = 0.42f;
            p.hatRoll = 0.02f; p.melodyRun = 0.56f; p.transitionSilence = 0.55f;
            p.drama = 0.46f; p.palette = 0.86f; p.brightness = 0.72f;
            p.ambient = true;
            break;
        case StyleType::VoidStep:
            p.bpmMin = 96.0f; p.bpmMax = 126.0f;
            p.swingMin = 0.01f; p.swingMax = 0.10f;
            p.density = 0.42f; p.drum = 0.56f; p.bass = 0.96f; p.melody = 0.48f; p.chord = 0.42f;
            p.texture = 0.62f; p.rough = 0.56f; p.space = 0.68f; p.sync = 0.52f;
            p.hatRoll = 0.08f; p.melodyRun = 0.28f; p.transitionSilence = 0.68f;
            p.drama = 0.88f; p.palette = 0.72f; p.brightness = 0.30f;
            p.halfTime = true;
            break;
        case StyleType::SolarFold:
            p.bpmMin = 124.0f; p.bpmMax = 150.0f;
            p.swingMin = 0.00f; p.swingMax = 0.07f;
            p.density = 0.66f; p.drum = 0.72f; p.bass = 0.62f; p.melody = 0.82f; p.chord = 0.66f;
            p.texture = 0.34f; p.rough = 0.22f; p.space = 0.34f; p.sync = 0.46f;
            p.hatRoll = 0.06f; p.melodyRun = 0.76f; p.transitionSilence = 0.26f;
            p.drama = 0.62f; p.palette = 0.88f; p.brightness = 0.92f;
            p.fourOnFloor = true;
            break;
        case StyleType::IonGarden:
            p.bpmMin = 70.0f; p.bpmMax = 98.0f;
            p.swingMin = 0.04f; p.swingMax = 0.16f;
            p.density = 0.50f; p.drum = 0.42f; p.bass = 0.58f; p.melody = 0.92f; p.chord = 0.88f;
            p.texture = 0.70f; p.rough = 0.06f; p.space = 0.70f; p.sync = 0.34f;
            p.hatRoll = 0.02f; p.melodyRun = 0.78f; p.transitionSilence = 0.60f;
            p.drama = 0.54f; p.palette = 0.94f; p.brightness = 0.76f;
            p.ambient = true;
            break;
        case StyleType::MarbleBass:
            p.bpmMin = 84.0f; p.bpmMax = 118.0f;
            p.swingMin = 0.02f; p.swingMax = 0.13f;
            p.density = 0.56f; p.drum = 0.58f; p.bass = 1.00f; p.melody = 0.52f; p.chord = 0.34f;
            p.texture = 0.42f; p.rough = 0.32f; p.space = 0.52f; p.sync = 0.58f;
            p.hatRoll = 0.05f; p.melodyRun = 0.26f; p.transitionSilence = 0.46f;
            p.drama = 0.78f; p.palette = 0.78f; p.brightness = 0.28f;
            p.halfTime = true;
            break;
        case StyleType::EchoCrown:
            p.bpmMin = 98.0f; p.bpmMax = 132.0f;
            p.swingMin = 0.00f; p.swingMax = 0.09f;
            p.density = 0.60f; p.drum = 0.56f; p.bass = 0.60f; p.melody = 0.96f; p.chord = 0.72f;
            p.texture = 0.50f; p.rough = 0.12f; p.space = 0.58f; p.sync = 0.48f;
            p.hatRoll = 0.04f; p.melodyRun = 0.88f; p.transitionSilence = 0.36f;
            p.drama = 0.66f; p.palette = 0.98f; p.brightness = 0.92f;
            break;
        case StyleType::BitFog:
            p.bpmMin = 112.0f; p.bpmMax = 148.0f;
            p.swingMin = 0.00f; p.swingMax = 0.06f;
            p.density = 0.70f; p.drum = 0.72f; p.bass = 0.68f; p.melody = 0.58f; p.chord = 0.32f;
            p.texture = 0.52f; p.rough = 0.74f; p.space = 0.36f; p.sync = 0.82f;
            p.hatRoll = 0.16f; p.melodyRun = 0.40f; p.transitionSilence = 0.28f;
            p.drama = 0.70f; p.palette = 0.90f; p.brightness = 0.56f;
            p.breakbeat = true;
            break;
        case StyleType::MagentaWell:
            p.bpmMin = 88.0f; p.bpmMax = 116.0f;
            p.swingMin = 0.02f; p.swingMax = 0.11f;
            p.density = 0.58f; p.drum = 0.54f; p.bass = 0.56f; p.melody = 0.98f; p.chord = 0.72f;
            p.texture = 0.44f; p.rough = 0.14f; p.space = 0.56f; p.sync = 0.42f;
            p.hatRoll = 0.03f; p.melodyRun = 0.92f; p.transitionSilence = 0.38f;
            p.drama = 0.60f; p.palette = 0.96f; p.brightness = 0.84f;
            break;
        case StyleType::CarbonRain:
            p.bpmMin = 138.0f; p.bpmMax = 168.0f;
            p.swingMin = 0.00f; p.swingMax = 0.07f;
            p.density = 0.78f; p.drum = 0.90f; p.bass = 0.70f; p.melody = 0.44f; p.chord = 0.22f;
            p.texture = 0.46f; p.rough = 0.86f; p.space = 0.28f; p.sync = 0.90f;
            p.hatRoll = 0.24f; p.melodyRun = 0.36f; p.transitionSilence = 0.18f;
            p.drama = 0.86f; p.palette = 0.98f; p.brightness = 0.50f;
            p.breakbeat = true;
            break;
        case StyleType::LatticeSun:
            p.bpmMin = 118.0f; p.bpmMax = 146.0f;
            p.swingMin = 0.00f; p.swingMax = 0.05f;
            p.density = 0.62f; p.drum = 0.62f; p.bass = 0.56f; p.melody = 0.92f; p.chord = 0.80f;
            p.texture = 0.36f; p.rough = 0.12f; p.space = 0.40f; p.sync = 0.50f;
            p.hatRoll = 0.05f; p.melodyRun = 0.96f; p.transitionSilence = 0.30f;
            p.drama = 0.58f; p.palette = 1.00f; p.brightness = 0.96f;
            p.fourOnFloor = true;
            break;
        case StyleType::StrangeHarbor:
            p.bpmMin = 54.0f; p.bpmMax = 84.0f;
            p.swingMin = 0.04f; p.swingMax = 0.17f;
            p.density = 0.30f; p.drum = 0.22f; p.bass = 0.42f; p.melody = 0.74f; p.chord = 0.90f;
            p.texture = 0.92f; p.rough = 0.10f; p.space = 0.88f; p.sync = 0.28f;
            p.hatRoll = 0.01f; p.melodyRun = 0.58f; p.transitionSilence = 0.74f;
            p.drama = 0.48f; p.palette = 0.90f; p.brightness = 0.44f;
            p.ambient = true;
            break;
        case StyleType::CopperChord:
            p.bpmMin = 86.0f; p.bpmMax = 116.0f;
            p.swingMin = 0.04f; p.swingMax = 0.16f;
            p.density = 0.52f; p.drum = 0.50f; p.bass = 0.62f; p.melody = 0.78f; p.chord = 0.86f;
            p.texture = 0.44f; p.rough = 0.20f; p.space = 0.50f; p.sync = 0.40f;
            p.hatRoll = 0.04f; p.melodyRun = 0.72f; p.transitionSilence = 0.42f;
            p.drama = 0.56f; p.palette = 0.96f; p.brightness = 0.62f;
            break;
        case StyleType::GhostMeter:
            p.bpmMin = 74.0f; p.bpmMax = 126.0f;
            p.swingMin = 0.06f; p.swingMax = 0.22f;
            p.density = 0.44f; p.drum = 0.54f; p.bass = 0.56f; p.melody = 0.66f; p.chord = 0.38f;
            p.texture = 0.56f; p.rough = 0.34f; p.space = 0.68f; p.sync = 0.72f;
            p.hatRoll = 0.10f; p.melodyRun = 0.46f; p.transitionSilence = 0.54f;
            p.drama = 0.62f; p.palette = 0.88f; p.brightness = 0.48f;
            break;
        case StyleType::ObsidianBloom:
            p.bpmMin = 62.0f; p.bpmMax = 102.0f;
            p.swingMin = 0.02f; p.swingMax = 0.12f;
            p.density = 0.36f; p.drum = 0.36f; p.bass = 0.92f; p.melody = 0.58f; p.chord = 0.76f;
            p.texture = 0.78f; p.rough = 0.24f; p.space = 0.76f; p.sync = 0.32f;
            p.hatRoll = 0.01f; p.melodyRun = 0.50f; p.transitionSilence = 0.68f;
            p.drama = 0.70f; p.palette = 0.92f; p.brightness = 0.26f;
            p.ambient = true; p.halfTime = true;
            break;
        case StyleType::VoltageMoth:
            p.bpmMin = 122.0f; p.bpmMax = 158.0f;
            p.swingMin = 0.00f; p.swingMax = 0.08f;
            p.density = 0.68f; p.drum = 0.62f; p.bass = 0.58f; p.melody = 0.94f; p.chord = 0.42f;
            p.texture = 0.38f; p.rough = 0.32f; p.space = 0.36f; p.sync = 0.78f;
            p.hatRoll = 0.14f; p.melodyRun = 0.98f; p.transitionSilence = 0.26f;
            p.drama = 0.72f; p.palette = 1.00f; p.brightness = 0.92f;
            break;
        case StyleType::QuartzTide:
            p.bpmMin = 70.0f; p.bpmMax = 98.0f;
            p.swingMin = 0.03f; p.swingMax = 0.14f;
            p.density = 0.34f; p.drum = 0.30f; p.bass = 0.46f; p.melody = 0.88f; p.chord = 0.92f;
            p.texture = 0.86f; p.rough = 0.08f; p.space = 0.84f; p.sync = 0.30f;
            p.hatRoll = 0.02f; p.melodyRun = 0.76f; p.transitionSilence = 0.72f;
            p.drama = 0.44f; p.palette = 1.00f; p.brightness = 0.74f;
            p.ambient = true;
            break;
        case StyleType::StaticCathedral:
            p.bpmMin = 96.0f; p.bpmMax = 138.0f;
            p.swingMin = 0.00f; p.swingMax = 0.10f;
            p.density = 0.50f; p.drum = 0.48f; p.bass = 0.70f; p.melody = 0.52f; p.chord = 0.94f;
            p.texture = 0.88f; p.rough = 0.62f; p.space = 0.66f; p.sync = 0.42f;
            p.hatRoll = 0.04f; p.melodyRun = 0.36f; p.transitionSilence = 0.62f;
            p.drama = 0.86f; p.palette = 0.98f; p.brightness = 0.42f;
            break;
        case StyleType::MercuryThread:
            p.bpmMin = 136.0f; p.bpmMax = 172.0f;
            p.swingMin = 0.00f; p.swingMax = 0.06f;
            p.density = 0.76f; p.drum = 0.72f; p.bass = 0.52f; p.melody = 0.86f; p.chord = 0.30f;
            p.texture = 0.38f; p.rough = 0.44f; p.space = 0.28f; p.sync = 0.94f;
            p.hatRoll = 0.34f; p.melodyRun = 0.90f; p.transitionSilence = 0.20f;
            p.drama = 0.74f; p.palette = 0.94f; p.brightness = 0.68f;
            p.breakbeat = true;
            break;
        case StyleType::NightLatch:
            p.bpmMin = 82.0f; p.bpmMax = 118.0f;
            p.swingMin = 0.02f; p.swingMax = 0.12f;
            p.density = 0.54f; p.drum = 0.62f; p.bass = 0.86f; p.melody = 0.48f; p.chord = 0.32f;
            p.texture = 0.52f; p.rough = 0.58f; p.space = 0.48f; p.sync = 0.64f;
            p.hatRoll = 0.08f; p.melodyRun = 0.34f; p.transitionSilence = 0.36f;
            p.drama = 0.82f; p.palette = 0.86f; p.brightness = 0.30f;
            p.halfTime = true;
            break;
        case StyleType::ChromeBloom:
        default:
            p.bpmMin = 118.0f; p.bpmMax = 142.0f;
            p.swingMin = 0.00f; p.swingMax = 0.05f;
            p.density = 0.58f; p.drum = 0.56f; p.bass = 0.52f; p.melody = 0.86f; p.chord = 0.52f;
            p.texture = 0.34f; p.rough = 0.18f; p.space = 0.36f; p.sync = 0.48f;
            p.hatRoll = 0.06f; p.melodyRun = 0.78f; p.transitionSilence = 0.28f;
            p.drama = 0.56f; p.palette = 0.60f; p.brightness = 0.88f;
            p.fourOnFloor = true;
            break;
    }

    applyHybridGenreBias(p);
    return p;
}

bool MusicEngine::styleAllowedForNoGenre(StyleType /*style*/) const {
    return true;
}

bool MusicEngine::styleAllowedForGenre(StyleType style, int32_t genreMode) const {
    if (genreMode <= 0) return true;
    if (genreMode > kGenreModeCount) genreMode = kGenreModeCount;

    switch (genreMode) {
        // Chrome Pulse: dry mechanized pulse, metallic motion, firm grid.
        case 1: return style == StyleType::ConcretePulse || style == StyleType::ChromeBloom || style == StyleType::LatticeSun || style == StyleType::MercuryThread || style == StyleType::CopperChord;
        // Velvet Circuit: smoother low-pressure circuitry, softer harmonic glow.
        case 2: return style == StyleType::VelvetDrift || style == StyleType::SoftVoltage || style == StyleType::WarmCurrent || style == StyleType::IonGarden || style == StyleType::CopperChord || style == StyleType::QuartzTide;
        // Glass Trap: crystalline high motion with half-time gravity and snapped hats.
        case 3: return style == StyleType::GlassNoir || style == StyleType::NeonLatch || style == StyleType::EchoCrown;
        // Dust Machine: rougher oxidized rhythm, noise edges, unstable machinery.
        case 4: return style == StyleType::BrokenMagnet || style == StyleType::BitFog || style == StyleType::CarbonRain || style == StyleType::StaticCathedral || style == StyleType::GhostMeter;
        // Liquid Grid: melodic grid movement with smoother current and chord memory.
        case 5: return style == StyleType::PrismCruise || style == StyleType::PulseGarden || style == StyleType::LatticeSun || style == StyleType::WarmCurrent || style == StyleType::QuartzTide || style == StyleType::CopperChord;
        // Neon Drift: brighter drifting tones with long hooks and glowing motion.
        case 6: return style == StyleType::NeonLatch || style == StyleType::SolarFold || style == StyleType::MagentaWell || style == StyleType::EchoCrown || style == StyleType::VoltageMoth || style == StyleType::MercuryThread;
        // Broken Speaker: damaged pressure, fractured percussion, bit-fogged edges.
        case 7: return style == StyleType::BrokenMagnet || style == StyleType::BitFog || style == StyleType::CarbonRain || style == StyleType::ShardRush || style == StyleType::StaticCathedral || style == StyleType::MercuryThread;
        // Deep Magnet: bass-centered gravity, dark low movement, heavy tonal pull.
        case 8: return style == StyleType::DeepMagnet || style == StyleType::MarbleBass || style == StyleType::SubOrbit || style == StyleType::VoidStep || style == StyleType::ObsidianBloom || style == StyleType::NightLatch;
        // Pixel Ritual: small-grid figures, bright square logic, repeated glyph behavior.
        case 9: return style == StyleType::TinyGrid || style == StyleType::BitFog || style == StyleType::LatticeSun || style == StyleType::ConcretePulse || style == StyleType::VoltageMoth || style == StyleType::GhostMeter;
        // Soft Voltage: gentle electronic charge, more air, slower melodic pressure.
        case 10: return style == StyleType::SoftVoltage || style == StyleType::IonGarden || style == StyleType::StrangeHarbor || style == StyleType::VelvetDrift || style == StyleType::QuartzTide || style == StyleType::CopperChord;
        // Heavy Orbit: lower-register orbit, surge, sub pressure, large circular movement.
        case 11: return style == StyleType::SubOrbit || style == StyleType::VoidStep || style == StyleType::MarbleBass || style == StyleType::DeepMagnet || style == StyleType::ObsidianBloom || style == StyleType::NightLatch;
        // Cold Arcade: hard bright grid, colder pulse language, precise synthetic hooks.
        case 12: return style == StyleType::TinyGrid || style == StyleType::ConcretePulse || style == StyleType::ChromeBloom || style == StyleType::LatticeSun || style == StyleType::VoltageMoth || style == StyleType::MercuryThread;
        // No Genre: raw engine selection with no named style-family restraint.
        case 13: return styleAllowedForNoGenre(style);
        default: return true;
    }
}

bool MusicEngine::styleAllowedForGenreMask(StyleType style, int32_t genreMask) const {
    genreMask = genreMask & ((1 << kGenreModeCount) - 1);
    if (genreMask == 0) return true;
    for (int32_t i = 0; i < kGenreModeCount; ++i) {
        if ((genreMask & (1 << i)) != 0 && styleAllowedForGenre(style, i + 1)) return true;
    }
    return false;
}

int32_t MusicEngine::chooseGenreModeFromMask(int32_t genreMask) {
    genreMask = genreMask & ((1 << kGenreModeCount) - 1);
    if (genreMask == 0) return mRng.rangeInt(1, kGenreModeCount);
    int32_t selected[kGenreModeCount] = {};
    int32_t count = 0;
    for (int32_t i = 0; i < kGenreModeCount; ++i) {
        if ((genreMask & (1 << i)) != 0) selected[count++] = i + 1;
    }
    return count > 0 ? selected[mRng.rangeInt(0, count - 1)] : mRng.rangeInt(1, kGenreModeCount);
}

void MusicEngine::applyHybridGenreBias(StyleProfile& p) const {
    const int32_t mask = mActiveGenreMask & ((1 << kGenreModeCount) - 1);
    if (mActiveGenreBlendMode != 1 || mask == 0) return;
    int32_t count = 0;
    float density = 0.0f, drum = 0.0f, bass = 0.0f, melody = 0.0f, chord = 0.0f;
    float texture = 0.0f, rough = 0.0f, space = 0.0f, sync = 0.0f, drama = 0.0f;
    float brightness = 0.0f, bpmMin = 0.0f, bpmMax = 0.0f, swingMin = 0.0f, swingMax = 0.0f;

    auto add = [&](float lo, float hi, float swLo, float swHi,
                   float den, float dr, float ba, float mel, float ch,
                   float tex, float ro, float sp, float sy, float dra, float bright) {
        bpmMin += lo; bpmMax += hi; swingMin += swLo; swingMax += swHi;
        density += den; drum += dr; bass += ba; melody += mel; chord += ch;
        texture += tex; rough += ro; space += sp; sync += sy; drama += dra; brightness += bright;
        ++count;
    };

    for (int32_t i = 0; i < kGenreModeCount; ++i) {
        if ((mask & (1 << i)) == 0) continue;
        switch (i + 1) {
            case 1: add(80, 124, .02f, .12f, .58f,.70f,.62f,.44f,.28f,.22f,.44f,.34f,.60f,.44f,.42f); break;
            case 2: add(62, 106, .03f, .14f, .38f,.38f,.52f,.72f,.70f,.62f,.12f,.72f,.34f,.42f,.50f); break;
            case 3: add(120,156, .00f, .08f, .60f,.66f,.78f,.55f,.26f,.32f,.42f,.44f,.58f,.58f,.62f); break;
            case 4: add(88,150, .00f, .10f, .70f,.80f,.66f,.44f,.24f,.42f,.78f,.30f,.84f,.70f,.46f); break;
            case 5: add(84,136, .00f, .08f, .56f,.58f,.62f,.78f,.68f,.38f,.16f,.44f,.44f,.54f,.76f); break;
            case 6: add(96,148, .00f, .08f, .58f,.58f,.56f,.86f,.62f,.38f,.16f,.42f,.48f,.60f,.86f); break;
            case 7: add(90,168, .00f, .08f, .76f,.88f,.68f,.44f,.20f,.46f,.86f,.28f,.90f,.76f,.52f); break;
            case 8: add(76,128, .01f, .10f, .42f,.48f,.98f,.34f,.28f,.48f,.46f,.62f,.44f,.74f,.28f); break;
            case 9: add(104,146, .00f, .05f, .58f,.64f,.48f,.42f,.38f,.34f,.36f,.44f,.42f,.42f,.72f); break;
            case 10: add(58,98, .02f, .13f, .34f,.30f,.44f,.84f,.78f,.68f,.10f,.78f,.30f,.44f,.66f); break;
            case 11: add(88,142, .00f, .09f, .54f,.60f,.96f,.36f,.32f,.50f,.42f,.62f,.48f,.70f,.32f); break;
            case 12: add(104,148, .00f, .05f, .60f,.66f,.54f,.54f,.42f,.30f,.22f,.38f,.46f,.50f,.86f); break;
            case 13: add(70,150, .00f, .14f, .56f,.62f,.62f,.62f,.48f,.44f,.44f,.46f,.56f,.58f,.56f); break;
            default: break;
        }
    }
    if (count <= 1) return;

    const float inv = 1.0f / static_cast<float>(count);
    const float influence = 0.34f;
    auto mix = [&](float& dst, float src) {
        dst = clamp(dst * (1.0f - influence) + src * inv * influence, 0.0f, 1.20f);
    };
    p.bpmMin = p.bpmMin * (1.0f - influence) + bpmMin * inv * influence;
    p.bpmMax = p.bpmMax * (1.0f - influence) + bpmMax * inv * influence;
    p.swingMin = p.swingMin * (1.0f - influence) + swingMin * inv * influence;
    p.swingMax = p.swingMax * (1.0f - influence) + swingMax * inv * influence;
    mix(p.density, density); mix(p.drum, drum); mix(p.bass, bass); mix(p.melody, melody); mix(p.chord, chord);
    mix(p.texture, texture); mix(p.rough, rough); mix(p.space, space); mix(p.sync, sync); mix(p.drama, drama); mix(p.brightness, brightness);
    p.ambient = p.ambient || (space * inv > 0.68f && drum * inv < 0.48f);
    p.breakbeat = p.breakbeat || (rough * inv > 0.72f && drum * inv > 0.72f);
    p.trapHats = p.trapHats || (sync * inv > 0.62f && p.bpmMax > 132.0f);
}

MusicEngine::StyleType MusicEngine::randomStyle() {
    const int32_t mask = mActiveGenreMask & ((1 << kGenreModeCount) - 1);
    int32_t mode = (mask == 0) ? mRng.rangeInt(1, kGenreModeCount) : chooseGenreModeFromMask(mask);
    if (mode <= 0) mode = mRng.rangeInt(1, kGenreModeCount);
    mWorkingGenreMode = mode;

    StyleType s = StyleType::ConcretePulse;
    for (int32_t i = 0; i < 52; ++i) {
        s = static_cast<StyleType>(mRng.rangeInt(0, static_cast<int32_t>(StyleType::Count) - 1));
        if (styleAllowedForGenre(s, mode)) return s;
    }
    for (int32_t i = 0; i < static_cast<int32_t>(StyleType::Count); ++i) {
        s = static_cast<StyleType>(i);
        if (styleAllowedForGenre(s, mode)) return s;
    }

    mWorkingGenreMode = 1;
    return StyleType::ConcretePulse;
}

MusicEngine::StyleType MusicEngine::randomDifferentStyle(StyleType current) {
    StyleType s = current;
    for (int32_t i = 0; i < 16 && s == current; ++i) {
        s = randomStyle();
    }
    if (s == current) {
        const int32_t n = static_cast<int32_t>(StyleType::Count);
        for (int32_t i = 1; i < n; ++i) {
            StyleType candidate = static_cast<StyleType>((static_cast<int32_t>(current) + i) % n);
            if (styleAllowedForGenreMask(candidate, currentGenreMask())) return candidate;
        }
    }
    return s;
}

void MusicEngine::scheduleNextStyleTarget() {
    const float u = std::max(0.001f, 1.0f - mRng.uni());
    const float mean = 260.0f + 520.0f * mRng.uni();
    const int32_t expPart = static_cast<int32_t>(-std::log(u) * mean);
    mStyleTargetSteps = 120 + expPart + mRng.rangeInt(0, 180);
    if (mRng.chance(0.18f)) mStyleTargetSteps = mRng.rangeInt(80, 260);
    if (mRng.chance(0.10f)) mStyleTargetSteps += mRng.rangeInt(520, 1500);
}

void MusicEngine::generatePattern(StyleType style) {
    mPattern = Pattern{};
    mPattern.style = style;
    const StyleProfile p = profile(style);
    mPattern.profileTexture = p.texture;
    mPattern.profileAmbient = p.ambient;
    mPattern.profileBreakbeat = p.breakbeat;

    const int32_t roots[] = {31, 33, 34, 36, 38, 39, 41, 43, 46};
    mPattern.rootMidi = roots[mRng.rangeInt(0, 8)];
    if (style == StyleType::GlassNoir || style == StyleType::SubOrbit || style == StyleType::DeepMagnet || style == StyleType::MarbleBass || style == StyleType::StrangeHarbor || style == StyleType::ObsidianBloom || style == StyleType::NightLatch || style == StyleType::StaticCathedral) mPattern.rootMidi -= 2;
    if (style == StyleType::PrismCruise || style == StyleType::ChromeBloom || style == StyleType::SolarFold || style == StyleType::WarmCurrent || style == StyleType::IonGarden || style == StyleType::EchoCrown || style == StyleType::MagentaWell || style == StyleType::LatticeSun || style == StyleType::VoltageMoth || style == StyleType::QuartzTide || style == StyleType::CopperChord || style == StyleType::MercuryThread) mPattern.rootMidi += 2;

    if (style == StyleType::VelvetDrift || style == StyleType::SoftVoltage || style == StyleType::PulseGarden || style == StyleType::IonGarden || style == StyleType::StrangeHarbor || style == StyleType::QuartzTide || style == StyleType::CopperChord) mPattern.scaleMode = mRng.chance(0.55f) ? 1 : 4;
    else if (style == StyleType::GlassNoir || style == StyleType::DeepMagnet || style == StyleType::MarbleBass || style == StyleType::BitFog || style == StyleType::CarbonRain || style == StyleType::ObsidianBloom || style == StyleType::NightLatch || style == StyleType::StaticCathedral) mPattern.scaleMode = mRng.chance(0.55f) ? 2 : 0;
    else if (style == StyleType::PrismCruise || style == StyleType::ChromeBloom || style == StyleType::SolarFold || style == StyleType::WarmCurrent || style == StyleType::EchoCrown || style == StyleType::MagentaWell || style == StyleType::LatticeSun || style == StyleType::VoltageMoth || style == StyleType::MercuryThread || style == StyleType::GhostMeter) mPattern.scaleMode = mRng.chance(0.50f) ? 0 : 3;
    else mPattern.scaleMode = mRng.rangeInt(0, 4);

    mPattern.swing = p.swingMin + mRng.uni() * (p.swingMax - p.swingMin);
    mPattern.humanize = clamp(0.12f + mRng.uni() * 0.62f + (p.swingMax * 1.1f), 0.06f, 0.95f);
    mPattern.energy = clamp(0.32f + p.density * 0.44f + mRng.bipolar() * 0.16f, 0.12f, 0.96f);
    mPattern.density = clamp(p.density + mRng.bipolar() * 0.12f, 0.10f, 0.96f);
    mPattern.syncopation = clamp(p.sync + mRng.bipolar() * 0.13f, 0.05f, 0.96f);
    mPattern.texture = clamp(p.texture + mRng.bipolar() * 0.11f, 0.00f, 0.95f);
    mPattern.roughness = clamp(p.rough + mRng.bipolar() * 0.12f, 0.00f, 0.98f);
    mPattern.space = clamp(p.space + mRng.bipolar() * 0.13f, 0.02f, 0.92f);
    mPattern.melody = clamp(p.melody + mRng.bipolar() * 0.14f, 0.15f, 0.98f);
    mPattern.delay = clamp(0.06f + p.space * 0.22f + p.texture * 0.12f + mRng.bipolar() * 0.06f, 0.02f, 0.42f);
    mPattern.drive = clamp(0.36f + p.rough * 0.48f + mRng.bipolar() * 0.08f, 0.18f, 0.92f);

    mBpmTarget = p.bpmMin + mRng.uni() * (p.bpmMax - p.bpmMin);
    if (mBpm <= 10.0f) mBpm = mBpmTarget;
    else mBpm = 0.45f * mBpm + 0.55f * mBpmTarget;

    static constexpr uint16_t kickDNA[24] = {
        0x1101,0x1049,0x1481,0x9009,0x1189,0x8109,0x5421,0x1249,
        0x9081,0x1141,0x8049,0x5085,0x2209,0x102d,0x2409,0x8901,
        0x1501,0x4481,0x0189,0xa101,0x2105,0x1881,0x4029,0x9301
    };
    static constexpr uint16_t snareDNA[20] = {
        0x1010,0x1100,0x0110,0x9010,0x1090,0x1018,0x1810,0x0018,
        0x1091,0x1110,0x0410,0x1004,0x2010,0x1080,0x0181,0x1040,
        0x5010,0x0118,0x1910,0x1050
    };
    static constexpr uint16_t hatDNA[18] = {
        0x5555,0xffff,0xaaaa,0x3333,0xcccc,0x5d75,0xd575,0x7777,
        0xeeee,0xbbbb,0x6db6,0xb6db,0x0f0f,0xf0f0,0x9696,0x6996,
        0xdddd,0x7bde
    };
    static constexpr uint16_t percDNA[24] = {
        0x0208,0x2080,0x4042,0x8400,0x0220,0x8840,0x2004,0x4280,
        0x0802,0x2288,0x4410,0x8021,0x1204,0x0482,0x2810,0x8120,
        0x0448,0x4804,0x1028,0x8044,0x2402,0x4208,0x9004,0x0490
    };
    const int32_t kickVariant = mRng.rangeInt(0, 23);
    const int32_t snareVariant = mRng.rangeInt(0, 19);
    const int32_t hatVariant = mRng.rangeInt(0, 17);
    const int32_t percVariant = mRng.rangeInt(0, 23);
    const float dnaStrength = clamp(0.20f + 0.42f * p.palette + 0.16f * mRng.uni(), 0.16f, 0.76f);

    for (int32_t i = 0; i < kPatternSteps; ++i) {
        const int32_t p16 = i & 15;
        const bool down = p16 == 0;
        const bool back = p16 == 4 || p16 == 12;
        const bool halfBack = p16 == 8;
        const bool eighth = (p16 & 1) == 0;
        const bool offEighth = p16 == 2 || p16 == 6 || p16 == 10 || p16 == 14;
        const bool barTwo = (i & 31) >= 16;
        const float lift = barTwo ? 0.045f : 0.0f;

        float kick = 0.004f;
        float snare = 0.004f;
        float hat = eighth ? 0.18f : 0.045f;
        float openHat = 0.004f;
        float perc = 0.010f;
        float bass = 0.020f;
        float chord = 0.004f;
        float lead = 0.006f;
        float accent = down ? 0.95f : (back || halfBack ? 0.74f : (eighth ? 0.42f : 0.22f));

        switch (style) {
            case StyleType::ConcretePulse:
                if (down) kick = 0.94f;
                if (p16 == 7 || p16 == 10 || p16 == 14 || (barTwo && p16 == 3)) kick = 0.20f + lift;
                if (p16 == 8) kick = 0.42f + lift;
                if (back) snare = 0.93f;
                if (p16 == 11 || p16 == 15) snare = 0.12f;
                hat = eighth ? 0.52f : 0.20f;
                if (p16 == 6 || p16 == 13) perc = 0.11f;
                if (p16 == 2 || p16 == 10) chord = 0.035f;
                if (p16 == 3 || p16 == 7 || p16 == 15) lead = 0.055f;
                bass = (kick > 0.30f ? 0.34f : 0.050f) + (p16 == 6 || p16 == 13 ? 0.14f : 0.0f);
                break;

            case StyleType::GlassNoir:
                if (p16 == 0 || p16 == 3 || p16 == 11 || (barTwo && p16 == 14)) kick = 0.60f;
                if (p16 == 8) snare = 0.92f;
                if (p16 == 7 || p16 == 15) snare = 0.10f;
                hat = 0.42f + (eighth ? 0.14f : 0.08f);
                if (p16 == 2 || p16 == 6 || p16 == 10 || p16 == 14) hat += 0.18f;
                openHat = (p16 == 6 || p16 == 14) ? 0.10f : 0.004f;
                bass = (kick > 0.35f || p16 == 1 || p16 == 12) ? 0.48f : 0.040f;
                chord = (p16 == 0 || p16 == 12) ? 0.022f : 0.002f;
                lead = (p16 == 5 || p16 == 10 || p16 == 15) ? 0.040f : 0.006f;
                perc = (p16 == 4 || p16 == 13) ? 0.07f : 0.012f;
                accent = halfBack ? 0.88f : accent;
                break;

            case StyleType::ShardRush:
                if (p16 == 0 || p16 == 10 || (barTwo && p16 == 3)) kick = 0.86f;
                if (p16 == 4 || p16 == 12) snare = 0.94f;
                if (p16 == 7 || p16 == 15) snare = 0.16f;
                hat = 0.54f + (eighth ? 0.12f : 0.20f);
                openHat = offEighth ? 0.07f : 0.004f;
                perc = (p16 == 1 || p16 == 6 || p16 == 9 || p16 == 14) ? 0.18f : 0.030f;
                bass = (p16 == 0 || p16 == 5 || p16 == 10 || p16 == 13) ? 0.38f : 0.050f;
                lead = (p16 == 3 || p16 == 11) ? 0.030f : 0.004f;
                chord = 0.006f;
                break;

            case StyleType::NeonLatch:
                if (p16 == 0 || p16 == 6 || p16 == 10) kick = 0.74f;
                if (p16 == 3 || p16 == 13) kick = 0.24f;
                if (back) snare = 0.88f;
                if (p16 == 2 || p16 == 15) snare = 0.10f;
                hat = eighth ? 0.56f : 0.24f;
                openHat = offEighth ? 0.06f : 0.004f;
                perc = (p16 == 1 || p16 == 5 || p16 == 9 || p16 == 14) ? 0.16f : 0.026f;
                bass = (p16 == 0 || p16 == 2 || p16 == 6 || p16 == 9 || p16 == 13) ? 0.42f : 0.050f;
                chord = (p16 == 2 || p16 == 6 || p16 == 10 || p16 == 14) ? 0.034f : 0.004f;
                lead = (p16 == 1 || p16 == 5 || p16 == 7 || p16 == 13 || p16 == 15) ? 0.090f : 0.012f;
                break;

            case StyleType::TinyGrid:
                if (p16 == 0 || p16 == 4 || p16 == 8 || p16 == 12) kick = 0.86f;
                if (p16 == 4 || p16 == 12) snare = 0.30f;
                hat = offEighth ? 0.72f : (eighth ? 0.16f : 0.07f);
                openHat = offEighth ? 0.18f : 0.002f;
                perc = (p16 == 3 || p16 == 7 || p16 == 11 || p16 == 15) ? 0.10f : 0.018f;
                bass = (p16 == 0 || p16 == 7 || p16 == 10 || p16 == 15) ? 0.24f : 0.034f;
                chord = (p16 == 2 || p16 == 10) ? 0.040f : 0.006f;
                lead = (p16 == 5 || p16 == 13) ? 0.026f : 0.003f;
                break;

            case StyleType::PrismCruise:
                if (p16 == 0 || p16 == 8) kick = 0.82f;
                if (back) snare = 0.86f;
                hat = eighth ? 0.32f : 0.08f;
                openHat = offEighth ? 0.06f : 0.003f;
                bass = (p16 == 0 || p16 == 2 || p16 == 4 || p16 == 6 || p16 == 8 || p16 == 10 || p16 == 12 || p16 == 14) ? 0.34f : 0.018f;
                chord = (p16 == 0 || p16 == 8) ? 0.080f : ((p16 == 4 || p16 == 12) ? 0.028f : 0.004f);
                lead = (p16 == 1 || p16 == 3 || p16 == 6 || p16 == 9 || p16 == 11 || p16 == 14) ? 0.115f : 0.010f;
                perc = (p16 == 15) ? 0.070f : 0.010f;
                break;

            case StyleType::BrokenMagnet:
                if (p16 == 0 || p16 == 5 || p16 == 11) kick = 0.72f;
                if (p16 == 4 || p16 == 12) snare = 0.86f;
                if (p16 == 2 || p16 == 9 || p16 == 15) snare = 0.14f;
                hat = eighth ? 0.42f : 0.26f;
                openHat = (p16 == 7 || p16 == 14) ? 0.06f : 0.003f;
                perc = (p16 == 1 || p16 == 3 || p16 == 6 || p16 == 10 || p16 == 13 || p16 == 15) ? 0.24f : 0.030f;
                bass = (p16 == 0 || p16 == 5 || p16 == 8 || p16 == 11 || p16 == 15) ? 0.42f : 0.040f;
                chord = (p16 == 6 || p16 == 14) ? 0.026f : 0.003f;
                lead = (p16 == 2 || p16 == 7 || p16 == 10 || p16 == 13) ? 0.065f : 0.006f;
                break;

            case StyleType::VelvetDrift:
                kick = down && !barTwo ? 0.30f : ((p16 == 8 && mRng.chance(0.35f)) ? 0.16f : 0.002f);
                snare = (p16 == 12 && mRng.chance(0.45f)) ? 0.18f : 0.002f;
                hat = eighth ? 0.08f : 0.018f;
                openHat = (p16 == 14) ? 0.035f : 0.002f;
                perc = (p16 == 5 || p16 == 11) ? 0.045f : 0.008f;
                bass = (p16 == 0 || p16 == 10) ? 0.26f : 0.020f;
                chord = (p16 == 0 || p16 == 8) ? 0.14f : 0.012f;
                lead = (p16 == 3 || p16 == 6 || p16 == 13) ? 0.085f : 0.018f;
                accent *= 0.74f;
                break;

            case StyleType::SubOrbit:
                if (p16 == 0 || p16 == 10) kick = 0.82f;
                if (p16 == 8) snare = 0.82f;
                if (p16 == 4 || p16 == 12) snare = 0.12f;
                hat = offEighth ? 0.42f : (eighth ? 0.20f : 0.06f);
                openHat = (p16 == 6 || p16 == 14) ? 0.09f : 0.003f;
                perc = (p16 == 3 || p16 == 11 || p16 == 15) ? 0.12f : 0.018f;
                bass = (p16 == 0 || p16 == 3 || p16 == 10 || p16 == 13) ? 0.48f : 0.034f;
                chord = (p16 == 2 || p16 == 10) ? 0.052f : 0.006f;
                lead = (p16 == 7 || p16 == 15) ? 0.035f : 0.004f;
                accent = halfBack ? 0.84f : accent;
                break;

            case StyleType::SoftVoltage:
                kick = (down && mRng.chance(0.55f)) ? 0.26f : ((p16 == 10 && barTwo) ? 0.12f : 0.002f);
                snare = (p16 == 8 && mRng.chance(0.42f)) ? 0.20f : 0.002f;
                hat = eighth ? 0.07f : 0.014f;
                openHat = (p16 == 6 || p16 == 14) ? 0.025f : 0.002f;
                perc = (p16 == 5 || p16 == 9 || p16 == 15) ? 0.040f : 0.006f;
                bass = (p16 == 0 || p16 == 8 || p16 == 14) ? 0.24f : 0.018f;
                chord = (p16 == 0 || p16 == 8) ? 0.16f : ((p16 == 4 || p16 == 12) ? 0.040f : 0.010f);
                lead = (p16 == 1 || p16 == 3 || p16 == 6 || p16 == 10 || p16 == 13) ? 0.115f : 0.014f;
                accent *= 0.78f;
                break;

            case StyleType::DeepMagnet:
                if (p16 == 0 || p16 == 9 || (barTwo && p16 == 14)) kick = 0.72f;
                if (p16 == 8) snare = 0.58f;
                if (p16 == 15) snare = 0.11f;
                hat = eighth ? 0.13f : 0.034f;
                openHat = (p16 == 6 || p16 == 14) ? 0.052f : 0.002f;
                perc = (p16 == 3 || p16 == 11) ? 0.070f : 0.012f;
                bass = (p16 == 0 || p16 == 3 || p16 == 9 || p16 == 12 || p16 == 15) ? 0.52f : 0.030f;
                chord = (p16 == 0 || p16 == 10) ? 0.050f : 0.004f;
                lead = (p16 == 6 || p16 == 13) ? 0.026f : 0.003f;
                accent = halfBack ? 0.78f : accent * 0.92f;
                break;

            case StyleType::WarmCurrent:
                if (p16 == 0 || p16 == 6 || p16 == 10) kick = 0.72f;
                if (p16 == 3 || p16 == 14) kick = 0.22f;
                if (back) snare = 0.78f;
                if (p16 == 7 || p16 == 15) snare = 0.13f;
                hat = eighth ? 0.36f : 0.16f;
                openHat = offEighth ? 0.06f : 0.004f;
                perc = (p16 == 1 || p16 == 5 || p16 == 9 || p16 == 13) ? 0.14f : 0.018f;
                bass = (p16 == 0 || p16 == 3 || p16 == 6 || p16 == 10 || p16 == 13) ? 0.42f : 0.050f;
                chord = (p16 == 0 || p16 == 8 || p16 == 12) ? 0.085f : 0.012f;
                lead = (p16 == 1 || p16 == 4 || p16 == 6 || p16 == 10 || p16 == 14) ? 0.125f : 0.016f;
                break;

            case StyleType::PulseGarden:
                kick = (down || p16 == 10) ? 0.42f : ((p16 == 6 || p16 == 14) ? 0.11f : 0.004f);
                snare = (p16 == 8 || p16 == 12) ? 0.24f : 0.004f;
                hat = eighth ? 0.12f : 0.030f;
                openHat = (p16 == 6 || p16 == 14) ? 0.040f : 0.003f;
                perc = (p16 == 2 || p16 == 5 || p16 == 11 || p16 == 15) ? 0.075f : 0.010f;
                bass = (p16 == 0 || p16 == 8 || p16 == 10 || p16 == 15) ? 0.30f : 0.026f;
                chord = (p16 == 0 || p16 == 4 || p16 == 8 || p16 == 12) ? 0.125f : 0.018f;
                lead = (p16 == 1 || p16 == 3 || p16 == 6 || p16 == 9 || p16 == 13) ? 0.120f : 0.018f;
                accent *= 0.82f;
                break;

            case StyleType::VoidStep:
                if (p16 == 0 || p16 == 9 || p16 == 14) kick = 0.78f;
                if (p16 == 8) snare = 0.72f;
                if (p16 == 3 || p16 == 15) snare = 0.13f;
                hat = offEighth ? 0.20f : (eighth ? 0.10f : 0.035f);
                openHat = (p16 == 6 || p16 == 14) ? 0.075f : 0.003f;
                perc = (p16 == 1 || p16 == 6 || p16 == 11 || p16 == 13) ? 0.13f : 0.020f;
                bass = (p16 == 0 || p16 == 3 || p16 == 9 || p16 == 10 || p16 == 14) ? 0.56f : 0.038f;
                chord = (p16 == 0 || p16 == 8) ? 0.070f : 0.007f;
                lead = (p16 == 5 || p16 == 11 || p16 == 15) ? 0.060f : 0.008f;
                accent = halfBack ? 0.88f : accent * 0.95f;
                break;

            case StyleType::SolarFold:
                if (p16 == 0 || p16 == 4 || p16 == 8 || p16 == 12) kick = 0.74f;
                if (p16 == 4 || p16 == 12) snare = 0.52f;
                if (p16 == 15) snare = 0.11f;
                hat = offEighth ? 0.58f : (eighth ? 0.26f : 0.08f);
                openHat = offEighth ? 0.15f : 0.004f;
                perc = (p16 == 2 || p16 == 5 || p16 == 7 || p16 == 11 || p16 == 15) ? 0.13f : 0.018f;
                bass = (p16 == 0 || p16 == 2 || p16 == 4 || p16 == 7 || p16 == 10 || p16 == 14) ? 0.34f : 0.032f;
                chord = (p16 == 0 || p16 == 4 || p16 == 8 || p16 == 12) ? 0.090f : 0.009f;
                lead = (p16 == 1 || p16 == 3 || p16 == 5 || p16 == 9 || p16 == 11 || p16 == 13 || p16 == 15) ? 0.140f : 0.014f;
                accent = offEighth ? accent * 1.08f : accent;
                break;

            case StyleType::IonGarden:
                kick = (down || p16 == 9) ? 0.34f : ((p16 == 6 || p16 == 14) ? 0.08f : 0.003f);
                snare = (p16 == 8) ? 0.18f : 0.003f;
                hat = eighth ? 0.10f : 0.025f;
                openHat = offEighth ? 0.035f : 0.003f;
                perc = (p16 == 2 || p16 == 7 || p16 == 11 || p16 == 15) ? 0.070f : 0.012f;
                bass = (p16 == 0 || p16 == 8 || p16 == 13) ? 0.28f : 0.022f;
                chord = (p16 == 0 || p16 == 4 || p16 == 8 || p16 == 12) ? 0.140f : 0.020f;
                lead = (p16 == 1 || p16 == 3 || p16 == 6 || p16 == 10 || p16 == 13 || p16 == 15) ? 0.150f : 0.020f;
                accent *= 0.80f;
                break;

            case StyleType::MarbleBass:
                if (p16 == 0 || p16 == 3 || p16 == 9 || p16 == 14) kick = 0.76f;
                if (p16 == 8) snare = 0.66f;
                if (p16 == 5 || p16 == 15) snare = 0.11f;
                hat = eighth ? 0.22f : 0.070f;
                openHat = (p16 == 6 || p16 == 14) ? 0.065f : 0.003f;
                perc = (p16 == 1 || p16 == 6 || p16 == 10 || p16 == 13) ? 0.110f : 0.020f;
                bass = (p16 == 0 || p16 == 3 || p16 == 8 || p16 == 9 || p16 == 12 || p16 == 14) ? 0.58f : 0.045f;
                chord = (p16 == 0 || p16 == 8) ? 0.050f : 0.006f;
                lead = (p16 == 5 || p16 == 11 || p16 == 15) ? 0.075f : 0.010f;
                accent = halfBack ? 0.88f : accent * 0.96f;
                break;

            case StyleType::EchoCrown:
                if (p16 == 0 || p16 == 8) kick = 0.58f;
                if (p16 == 4 || p16 == 12) snare = 0.40f;
                hat = offEighth ? 0.36f : (eighth ? 0.17f : 0.055f);
                openHat = offEighth ? 0.100f : 0.004f;
                perc = (p16 == 2 || p16 == 5 || p16 == 9 || p16 == 14) ? 0.105f : 0.016f;
                bass = (p16 == 0 || p16 == 4 || p16 == 8 || p16 == 10 || p16 == 14) ? 0.32f : 0.032f;
                chord = (p16 == 0 || p16 == 6 || p16 == 8 || p16 == 14) ? 0.105f : 0.014f;
                lead = (p16 == 1 || p16 == 2 || p16 == 5 || p16 == 7 || p16 == 9 || p16 == 11 || p16 == 13 || p16 == 15) ? 0.165f : 0.020f;
                accent = offEighth ? accent * 1.04f : accent;
                break;

            case StyleType::BitFog:
                if (p16 == 0 || p16 == 5 || p16 == 10 || (barTwo && p16 == 3)) kick = 0.82f;
                if (p16 == 4 || p16 == 12) snare = 0.88f;
                if (p16 == 7 || p16 == 15) snare = 0.18f;
                hat = 0.42f + (eighth ? 0.10f : 0.18f);
                openHat = offEighth ? 0.095f : 0.004f;
                perc = (p16 == 1 || p16 == 3 || p16 == 6 || p16 == 9 || p16 == 11 || p16 == 14) ? 0.220f : 0.034f;
                bass = (p16 == 0 || p16 == 5 || p16 == 9 || p16 == 10 || p16 == 13) ? 0.46f : 0.046f;
                chord = (p16 == 2 || p16 == 10) ? 0.040f : 0.006f;
                lead = (p16 == 3 || p16 == 6 || p16 == 11 || p16 == 14) ? 0.090f : 0.010f;
                break;

            case StyleType::MagentaWell:
                if (p16 == 0 || p16 == 8 || (barTwo && p16 == 14)) kick = 0.62f;
                if (p16 == 4 || p16 == 12) snare = 0.46f;
                hat = eighth ? 0.36f : 0.14f;
                openHat = offEighth ? 0.08f : 0.004f;
                perc = (p16 == 3 || p16 == 10 || p16 == 15) ? 0.08f : 0.018f;
                bass = (p16 == 0 || p16 == 5 || p16 == 8 || p16 == 13) ? 0.32f : 0.040f;
                chord = (p16 == 0 || p16 == 8) ? 0.080f : 0.010f;
                lead = (p16 == 0 || p16 == 2 || p16 == 5 || p16 == 7 || p16 == 10 || p16 == 14) ? 0.160f : 0.030f;
                accent = (p16 == 0 || p16 == 8) ? 0.90f : accent;
                break;

            case StyleType::CarbonRain:
                if (p16 == 0 || p16 == 3 || p16 == 10 || (barTwo && p16 == 14)) kick = 0.90f;
                if (p16 == 4 || p16 == 12) snare = 0.96f;
                if (p16 == 7 || p16 == 15) snare = 0.22f;
                hat = 0.50f + (eighth ? 0.16f : 0.22f);
                openHat = offEighth ? 0.07f : 0.004f;
                perc = (p16 == 1 || p16 == 5 || p16 == 9 || p16 == 13 || p16 == 15) ? 0.24f : 0.040f;
                bass = (kick > 0.50f || p16 == 6 || p16 == 11) ? 0.44f : 0.060f;
                chord = (p16 == 0 || p16 == 12) ? 0.018f : 0.004f;
                lead = (p16 == 2 || p16 == 6 || p16 == 14) ? 0.054f : 0.008f;
                break;

            case StyleType::LatticeSun:
                if (p16 == 0 || p16 == 4 || p16 == 8 || p16 == 12) kick = 0.76f;
                if (p16 == 4 || p16 == 12) snare = 0.34f;
                hat = offEighth ? 0.62f : (eighth ? 0.28f : 0.12f);
                openHat = offEighth ? 0.16f : 0.004f;
                perc = (p16 == 3 || p16 == 7 || p16 == 11 || p16 == 15) ? 0.12f : 0.020f;
                bass = (p16 == 0 || p16 == 4 || p16 == 7 || p16 == 12 || p16 == 15) ? 0.30f : 0.040f;
                chord = (p16 == 0 || p16 == 4 || p16 == 8 || p16 == 12) ? 0.052f : 0.008f;
                lead = (p16 == 1 || p16 == 3 || p16 == 5 || p16 == 9 || p16 == 11 || p16 == 13) ? 0.120f : 0.020f;
                break;

            case StyleType::StrangeHarbor:
                if (p16 == 0 && !barTwo) kick = 0.26f;
                if (p16 == 8 && barTwo) snare = 0.18f;
                hat = (p16 == 2 || p16 == 10) ? 0.10f : 0.020f;
                openHat = (p16 == 14) ? 0.06f : 0.002f;
                perc = (p16 == 5 || p16 == 13) ? 0.07f : 0.006f;
                bass = (p16 == 0 || p16 == 10) ? 0.20f : 0.018f;
                chord = (p16 == 0 || p16 == 8) ? 0.110f : 0.020f;
                lead = (p16 == 4 || p16 == 11 || p16 == 15) ? 0.070f : 0.008f;
                accent = down ? 0.72f : accent * 0.72f;
                break;

            case StyleType::CopperChord:
                if (p16 == 0 || p16 == 8 || p16 == 13) kick = 0.54f;
                if (p16 == 4 || p16 == 12) snare = 0.36f;
                hat = eighth ? 0.28f : 0.10f;
                openHat = (p16 == 6 || p16 == 14) ? 0.070f : 0.004f;
                perc = (p16 == 2 || p16 == 9 || p16 == 15) ? 0.095f : 0.014f;
                bass = (p16 == 0 || p16 == 5 || p16 == 8 || p16 == 12) ? 0.36f : 0.032f;
                chord = (p16 == 0 || p16 == 3 || p16 == 8 || p16 == 11) ? 0.130f : 0.020f;
                lead = (p16 == 1 || p16 == 5 || p16 == 7 || p16 == 10 || p16 == 14) ? 0.120f : 0.018f;
                accent = (p16 == 0 || p16 == 8) ? 0.88f : accent * 0.92f;
                break;

            case StyleType::GhostMeter:
                if (p16 == 0 || p16 == 5 || p16 == 11 || (barTwo && p16 == 14)) kick = 0.58f;
                if (p16 == 8 || (barTwo && p16 == 3)) snare = 0.42f;
                hat = (p16 == 2 || p16 == 7 || p16 == 10 || p16 == 15) ? 0.32f : (eighth ? 0.12f : 0.044f);
                openHat = (p16 == 7 || p16 == 15) ? 0.080f : 0.003f;
                perc = (p16 == 1 || p16 == 6 || p16 == 9 || p16 == 13) ? 0.145f : 0.018f;
                bass = (p16 == 0 || p16 == 5 || p16 == 8 || p16 == 11) ? 0.36f : 0.030f;
                chord = (p16 == 0 || p16 == 10) ? 0.075f : 0.012f;
                lead = (p16 == 3 || p16 == 7 || p16 == 12 || p16 == 15) ? 0.090f : 0.010f;
                accent = offEighth ? accent * 1.08f : accent * 0.90f;
                break;

            case StyleType::ObsidianBloom:
                if (p16 == 0 || (barTwo && p16 == 10)) kick = 0.34f;
                if (p16 == 8 && barTwo) snare = 0.20f;
                hat = (p16 == 2 || p16 == 10) ? 0.090f : 0.018f;
                openHat = (p16 == 14) ? 0.040f : 0.002f;
                perc = (p16 == 5 || p16 == 13) ? 0.060f : 0.008f;
                bass = (p16 == 0 || p16 == 8 || p16 == 11) ? 0.44f : 0.024f;
                chord = (p16 == 0 || p16 == 8) ? 0.150f : 0.026f;
                lead = (p16 == 4 || p16 == 7 || p16 == 11 || p16 == 15) ? 0.080f : 0.010f;
                accent = down ? 0.74f : accent * 0.70f;
                break;

            case StyleType::VoltageMoth:
                if (p16 == 0 || p16 == 4 || p16 == 8 || p16 == 12) kick = 0.58f;
                if (p16 == 4 || p16 == 12) snare = 0.42f;
                hat = offEighth ? 0.54f : (eighth ? 0.24f : 0.12f);
                openHat = offEighth ? 0.110f : 0.004f;
                perc = (p16 == 1 || p16 == 3 || p16 == 7 || p16 == 11 || p16 == 14) ? 0.155f : 0.024f;
                bass = (p16 == 0 || p16 == 4 || p16 == 7 || p16 == 10 || p16 == 13) ? 0.32f : 0.036f;
                chord = (p16 == 0 || p16 == 8) ? 0.060f : 0.010f;
                lead = (p16 == 0 || p16 == 2 || p16 == 5 || p16 == 7 || p16 == 9 || p16 == 11 || p16 == 14) ? 0.170f : 0.024f;
                accent = offEighth ? accent * 1.08f : accent;
                break;

            case StyleType::QuartzTide:
                kick = (p16 == 0 && !barTwo) ? 0.24f : ((p16 == 8 && barTwo) ? 0.18f : 0.003f);
                snare = (p16 == 12 && barTwo) ? 0.14f : 0.003f;
                hat = (p16 == 2 || p16 == 10 || p16 == 14) ? 0.075f : 0.012f;
                openHat = (p16 == 14) ? 0.050f : 0.002f;
                perc = (p16 == 3 || p16 == 9 || p16 == 15) ? 0.060f : 0.006f;
                bass = (p16 == 0 || p16 == 10) ? 0.24f : 0.016f;
                chord = (p16 == 0 || p16 == 4 || p16 == 8 || p16 == 12) ? 0.140f : 0.024f;
                lead = (p16 == 1 || p16 == 6 || p16 == 11 || p16 == 15) ? 0.110f : 0.014f;
                accent *= 0.76f;
                break;

            case StyleType::StaticCathedral:
                if (p16 == 0 || p16 == 8) kick = 0.52f;
                if (p16 == 4 || p16 == 12) snare = 0.28f;
                hat = eighth ? 0.18f : 0.060f;
                openHat = (p16 == 6 || p16 == 14) ? 0.060f : 0.003f;
                perc = (p16 == 2 || p16 == 5 || p16 == 11 || p16 == 15) ? 0.120f : 0.018f;
                bass = (p16 == 0 || p16 == 7 || p16 == 8 || p16 == 14) ? 0.42f : 0.032f;
                chord = (p16 == 0 || p16 == 8 || p16 == 12) ? 0.155f : 0.024f;
                lead = (p16 == 3 || p16 == 10 || p16 == 15) ? 0.075f : 0.010f;
                accent = (p16 == 0 || p16 == 8) ? 0.86f : accent * 0.82f;
                break;

            case StyleType::MercuryThread:
                if (p16 == 0 || p16 == 3 || p16 == 10 || (barTwo && p16 == 14)) kick = 0.84f;
                if (p16 == 4 || p16 == 12) snare = 0.88f;
                if (p16 == 7 || p16 == 15) snare = 0.16f;
                hat = 0.48f + (eighth ? 0.14f : 0.24f);
                openHat = offEighth ? 0.100f : 0.004f;
                perc = (p16 == 1 || p16 == 3 || p16 == 6 || p16 == 9 || p16 == 11 || p16 == 14) ? 0.210f : 0.034f;
                bass = (p16 == 0 || p16 == 5 || p16 == 9 || p16 == 10 || p16 == 13) ? 0.36f : 0.046f;
                chord = (p16 == 2 || p16 == 10) ? 0.034f : 0.006f;
                lead = (p16 == 1 || p16 == 4 || p16 == 7 || p16 == 11 || p16 == 15) ? 0.115f : 0.012f;
                break;

            case StyleType::NightLatch:
                if (p16 == 0 || p16 == 3 || p16 == 9 || p16 == 14) kick = 0.72f;
                if (p16 == 8) snare = 0.70f;
                if (p16 == 5 || p16 == 15) snare = 0.12f;
                hat = eighth ? 0.24f : 0.075f;
                openHat = (p16 == 6 || p16 == 14) ? 0.065f : 0.003f;
                perc = (p16 == 1 || p16 == 6 || p16 == 10 || p16 == 13) ? 0.130f : 0.020f;
                bass = (p16 == 0 || p16 == 3 || p16 == 8 || p16 == 9 || p16 == 12 || p16 == 14) ? 0.54f : 0.045f;
                chord = (p16 == 0 || p16 == 8) ? 0.058f : 0.006f;
                lead = (p16 == 5 || p16 == 11 || p16 == 15) ? 0.070f : 0.010f;
                accent = halfBack ? 0.88f : accent * 0.94f;
                break;

            case StyleType::ChromeBloom:
            default:
                if (p16 == 0 || p16 == 4 || p16 == 8 || p16 == 12) kick = 0.64f;
                if (p16 == 4 || p16 == 12) snare = 0.44f;
                hat = offEighth ? 0.48f : (eighth ? 0.20f : 0.06f);
                openHat = offEighth ? 0.12f : 0.004f;
                perc = (p16 == 2 || p16 == 7 || p16 == 11 || p16 == 15) ? 0.12f : 0.018f;
                bass = (p16 == 0 || p16 == 4 || p16 == 7 || p16 == 10 || p16 == 14) ? 0.28f : 0.030f;
                chord = (p16 == 0 || p16 == 4 || p16 == 8 || p16 == 12) ? 0.062f : 0.006f;
                lead = (p16 == 1 || p16 == 3 || p16 == 5 || p16 == 9 || p16 == 11 || p16 == 14) ? 0.135f : 0.012f;
                accent = offEighth ? accent * 1.05f : accent;
                break;
        }

        const bool kickDNAHit = (kickDNA[kickVariant] & static_cast<uint16_t>(1u << p16)) != 0;
        const bool snareDNAHit = (snareDNA[snareVariant] & static_cast<uint16_t>(1u << p16)) != 0;
        const bool hatDNAHit = (hatDNA[hatVariant] & static_cast<uint16_t>(1u << p16)) != 0;
        const bool percDNAHit = (percDNA[percVariant] & static_cast<uint16_t>(1u << p16)) != 0;
        const float dnaBar = ((i >> 4) & 1) ? 1.08f : 0.92f;
        if (kickDNAHit && !p.ambient) kick = std::max(kick, (0.13f + 0.34f * p.drum) * dnaStrength * dnaBar);
        if (snareDNAHit && !p.ambient) snare = std::max(snare, (0.10f + 0.30f * p.drum) * dnaStrength * (back ? 1.35f : 0.85f));
        if (hatDNAHit) hat = std::max(hat, (0.20f + 0.36f * p.density) * (0.52f + 0.48f * dnaStrength));
        if (percDNAHit) perc = std::max(perc, (0.045f + 0.24f * p.sync + 0.12f * p.rough) * dnaStrength);
        if (((kickVariant + p16) % 11) == 0 && p.sync > 0.52f) bass = std::max(bass, 0.050f + 0.20f * p.bass * dnaStrength);
        if (((hatVariant ^ p16) & 7) == 3 && p.hatRoll > 0.08f) openHat = std::max(openHat, 0.024f + 0.11f * p.hatRoll);

        const float randomScale = 0.95f + mRng.uni() * 0.10f;
        mPattern.kick[i] = clamp(kick * randomScale + mRng.bipolar() * 0.035f, 0.0f, 0.98f);
        mPattern.snare[i] = clamp(snare + mRng.bipolar() * 0.035f, 0.0f, 0.98f);
        mPattern.hat[i] = clamp(hat + mRng.bipolar() * 0.095f, 0.0f, 0.98f);
        mPattern.openHat[i] = clamp(openHat + mRng.bipolar() * 0.035f, 0.0f, 0.65f);
        mPattern.perc[i] = clamp(perc + mRng.bipolar() * 0.055f, 0.0f, 0.88f);
        mPattern.bass[i] = clamp(bass + mRng.bipolar() * 0.055f, 0.0f, 0.95f);
        mPattern.chord[i] = clamp(chord + mRng.bipolar() * 0.030f, 0.0f, 0.72f);
        mPattern.lead[i] = clamp(lead + mRng.bipolar() * 0.050f, 0.0f, 0.85f);
        mPattern.accent[i] = clamp(accent + mRng.bipolar() * 0.14f, 0.0f, 1.0f);
    }

    generateComposition(p);
    repairPattern();

    uint32_t h = patternHash();
    if (isHashRecent(h)) {
        mPattern.rootMidi += mRng.rangeInt(-2, 2);
        generateComposition(p);
        h = patternHash();
    }
    mRecentHash[mRecentHashWrite] = h;
    mRecentHashWrite = (mRecentHashWrite + 1) % kRecentHashes;
    updateCurrentSongData();
}

void MusicEngine::fillMotifFromTemplate(std::array<int32_t, kPhraseSteps>& motif,
                                        std::array<float, kPhraseSteps>& gate,
                                        std::array<float, kPhraseSteps>& dur,
                                        int32_t templateId,
                                        int32_t contourOffset) {
    motif.fill(0);
    gate.fill(0.0f);
    dur.fill(0.70f);

    // These are not genre licks. They are abstract grammatical shapes.
    // v19 expands the symbolic melody space again.  The table below contains
    // abstract grammatical shapes; higher template numbers transform these
    // shapes by rotation, inversion, anchor motion, fragmentation, and reply
    // pressure so long sessions do not keep recycling the same lead grammar.
    static constexpr int32_t degrees[20][16] = {
        {0, 0, 2, 0, 3, 0, 4, 0, 5, 0, 4, 0, 2, 0, 0, 0},
        {0, 0, 0, 2, 3, 0, 5, 0, 4, 0, 3, 0, 2, 0, 0, 0},
        {0, 0, 2, 3, 0, 0, 5, 4, 0, 3, 0, 2, 0, 0, -1, 0},
        {2, 0, 3, 0, 5, 0, 4, 0, 3, 0, 2, 0, 0, 0, 0, 0},
        {0, 2, 0, 4, 0, 5, 4, 0, 3, 0, 2, 0, 0, -1, 0, 0},
        {0, 0, -1, 0, 0, 2, 0, 3, 4, 0, 3, 0, 2, 0, 0, 0},
        {0, 2, 3, 0, 5, 0, 7, 0, 5, 0, 4, 3, 2, 0, 0, 0},
        {0, 0, 4, 0, 3, 0, 2, 0, 0, 0, -1, 0, -2, 0, 0, 0},
        {0, 0, 2, 4, 5, 4, 2, 0, -1, 0, 2, 3, 2, 0, -1, 0},
        {0, 3, 5, 3, 2, 0, -1, 0, 4, 5, 7, 5, 4, 2, 0, 0},
        {0, -1, 0, 2, 0, 3, 4, 3, 0, -1, -2, -1, 0, 2, 0, 0},
        {0, 0, 0, -1, 0, 0, 2, 0, 3, 0, 2, 0, 0, -1, 0, 0},
        {0, 2, 4, 5, 4, 2, 0, -1, 0, 2, 4, 7, 5, 4, 2, 0},
        {0, -2, 0, 3, 2, 0, -1, 0, 5, 4, 2, 0, -1, 0, 2, 0},
        {0, 5, 4, 2, 0, -1, 0, 2, 3, 5, 7, 5, 4, 2, 0, 0},
        {0, 0, 3, 5, 7, 5, 3, 0, 2, 0, -1, 0, 2, 4, 2, 0},
        {0, 2, 0, -1, 0, 4, 0, 2, 0, 5, 0, 4, 2, 0, -1, 0},
        {0, 4, 7, 5, 4, 2, 0, 2, 5, 7, 9, 7, 5, 4, 2, 0},
        {0, 0, -2, 0, -1, 2, 0, 3, 5, 3, 2, 0, -1, -2, 0, 0},
        {0, 2, 5, 4, 2, 4, 7, 5, 4, 2, 0, -1, 0, 3, 2, 0}
    };
    static constexpr float gates[20][16] = {
        {0.98f,0,0.70f,0,0.86f,0,0.72f,0,0.92f,0,0.64f,0,0.70f,0,0.82f,0},
        {0.92f,0,0,0.82f,0.78f,0,0.88f,0,0.62f,0,0.72f,0,0.66f,0,0.92f,0},
        {0.96f,0,0.64f,0.72f,0,0,0.86f,0.70f,0,0.58f,0,0.70f,0,0,0.55f,0.88f},
        {0.88f,0,0.68f,0,0.92f,0,0.70f,0,0.74f,0,0.62f,0,0.90f,0,0,0},
        {0.92f,0.54f,0,0.74f,0,0.88f,0.62f,0,0.74f,0,0.60f,0,0.82f,0.46f,0,0},
        {0.90f,0,0.54f,0,0.86f,0.58f,0,0.74f,0.60f,0,0.64f,0,0.56f,0,0.92f,0},
        {0.84f,0.48f,0.64f,0,0.86f,0,0.92f,0,0.66f,0,0.62f,0.54f,0.70f,0,0.90f,0},
        {0.88f,0,0.72f,0,0.64f,0,0.72f,0,0.88f,0,0.52f,0,0.62f,0,0.88f,0},
        {0.92f,0,0.64f,0.58f,0.74f,0.48f,0.56f,0,0.72f,0,0.62f,0.50f,0.66f,0,0.58f,0.88f},
        {0.78f,0.52f,0.70f,0.46f,0.62f,0,0.50f,0,0.72f,0.62f,0.86f,0.50f,0.66f,0.46f,0.82f,0},
        {0.90f,0.44f,0.62f,0.50f,0.82f,0,0.66f,0.46f,0.78f,0.42f,0.54f,0.44f,0.88f,0.50f,0.84f,0},
        {0.88f,0,0,0.50f,0.74f,0,0.58f,0,0.66f,0,0.52f,0,0.82f,0.38f,0.72f,0},
        {0.96f,0.58f,0.76f,0.62f,0.84f,0.50f,0.68f,0.44f,0.92f,0.52f,0.72f,0.50f,0.82f,0.44f,0.64f,0.88f},
        {0.92f,0.40f,0.72f,0.54f,0.66f,0.42f,0.58f,0.72f,0.82f,0.48f,0.66f,0.42f,0.58f,0.56f,0.68f,0.88f},
        {0.88f,0.64f,0.72f,0.48f,0.82f,0.44f,0.58f,0.36f,0.78f,0.54f,0.84f,0.48f,0.68f,0.42f,0.74f,0.86f},
        {0.94f,0,0.60f,0.76f,0.86f,0.64f,0.52f,0,0.80f,0,0.54f,0,0.70f,0.46f,0.62f,0.90f},
        {0.96f,0.50f,0,0.58f,0.82f,0.44f,0,0.70f,0.86f,0.46f,0,0.62f,0.76f,0,0.54f,0.88f},
        {0.82f,0.54f,0.74f,0.52f,0.68f,0.48f,0.86f,0.44f,0.72f,0.50f,0.94f,0.56f,0.76f,0.48f,0.62f,0.90f},
        {0.90f,0,0.56f,0,0.66f,0.50f,0.72f,0,0.84f,0.46f,0.64f,0,0.58f,0.44f,0.70f,0.88f},
        {0.94f,0.58f,0.76f,0.48f,0.68f,0.54f,0.88f,0.52f,0.82f,0.48f,0.70f,0.42f,0.80f,0.54f,0.66f,0.92f}
    };

    const int32_t t = std::max(0, std::min(255, templateId));
    const int32_t base = t % 20;
    for (int32_t i = 0; i < kPhraseSteps; ++i) {
        int32_t sourceBase = base;
        int32_t sourcePos = i;
        if (t >= 128) {
            const int32_t rotate = 1 + ((t >> 3) & 7);
            sourcePos = (i + rotate + ((i >= 8 && (t & 1)) ? 2 : 0)) & 15;
            sourceBase = (base + ((t >> 5) & 7)) % 20;
        }
        if (t >= 192 && ((i + t) & 3) == 0) {
            sourcePos = (15 - sourcePos) & 15;
            sourceBase = (sourceBase + 11) % 20;
        }
        int32_t d = degrees[sourceBase][sourcePos] + contourOffset;
        float g = gates[sourceBase][sourcePos];
        if (t >= 12 && (i == 2 || i == 6 || i == 10 || i == 14)) {
            d += (t & 1) ? 1 : -1;
            g = std::max(g, 0.38f + 0.10f * static_cast<float>((i >> 1) & 1));
        }
        if (t >= 16 && (i == 1 || i == 5 || i == 9 || i == 13 || i == 15)) {
            d += (i < 8) ? 2 : -2;
            g = std::max(g, 0.42f);
        }
        if ((t == 14 || t == 18) && (i & 3) == 3) {
            d = contourOffset;
            g = std::max(g, 0.62f);
        }
        if (t >= 20 && (i == 4 || i == 12)) {
            d += (t & 2) ? 3 : -2;
            g = std::max(g, 0.56f);
        }
        if (t >= 24 && (i == 3 || i == 7 || i == 11)) {
            d += (i < 8) ? 1 : -1;
            g = std::max(g, 0.50f);
        }
        if (t >= 28 && (i == 0 || i == 8 || i == 15)) {
            d = contourOffset + (i == 8 ? ((t & 1) ? 4 : 2) : 0);
            g = std::max(g, 0.78f);
        }
        if (t >= 36 && (i == 2 || i == 5 || i == 10 || i == 13)) {
            d += ((t + i) & 2) ? 2 : -1;
            g = std::max(g, 0.46f);
        }
        if (t >= 44 && (i == 6 || i == 7 || i == 14)) {
            d = contourOffset + ((i == 7) ? 5 : ((t & 1) ? 3 : -2));
            g = std::max(g, 0.58f);
        }
        if (t >= 52 && (i & 3) == 1) {
            d += ((i < 8) ? 1 : -1) * (1 + (t & 1));
            g = std::max(g, 0.40f);
        }
        if (t >= 60 && (i == 3 || i == 11 || i == 15)) {
            d = (i == 15) ? contourOffset : contourOffset + ((i == 3) ? 4 : -1);
            g = std::max(g, 0.62f);
        }
        if (t >= 64 && (i == 2 || i == 6 || i == 9 || i == 14)) {
            d = contourOffset + (((t + i) & 4) ? 6 : -3);
            g = std::max(g, (i == 14) ? 0.68f : 0.48f);
        }
        if (t >= 70 && (i == 1 || i == 4 || i == 10 || i == 13)) {
            d += ((i < 8) ? 1 : -1);
            g = std::max(g, 0.46f);
        }
        if (t >= 74 && (i == 5 || i == 7 || i == 12)) {
            d = contourOffset + ((i == 12) ? 0 : ((t & 1) ? 7 : 4));
            g = std::max(g, 0.60f);
        }
        if (t >= 80 && (i == 1 || i == 6 || i == 9 || i == 14)) {
            d += ((t + i) & 1) ? 2 : -2;
            g = std::max(g, (i == 14) ? 0.66f : 0.44f);
        }
        if (t >= 88 && (i == 2 || i == 3 || i == 10 || i == 11)) {
            d = contourOffset + ((i < 8) ? (3 + ((t >> 1) & 1)) : (-1 - (t & 1)));
            g = std::max(g, 0.50f + 0.08f * static_cast<float>((i >> 1) & 1));
        }
        if (t >= 96 && (i == 4 || i == 5 || i == 12 || i == 13)) {
            d += ((i == 5 || i == 12) ? 5 : -3);
            g = std::max(g, 0.52f);
        }
        if (t >= 104 && ((i + t) & 3) == 2) {
            d = contourOffset + ((i < 8) ? ((i & 4) ? 6 : 2) : ((i & 4) ? -2 : 4));
            g = std::max(g, 0.46f);
        }
        if (t >= 112 && (i == 0 || i == 4 || i == 8 || i == 15)) {
            d = contourOffset + (i == 4 ? ((t & 1) ? 5 : 3) : (i == 8 ? ((t & 2) ? 4 : 2) : 0));
            g = std::max(g, i == 15 ? 0.84f : 0.74f);
        }
        if (t >= 120 && (i == 6 || i == 7 || i == 13 || i == 14)) {
            d = contourOffset + ((i == 14) ? -1 : ((t & 1) ? 8 : 5));
            g = std::max(g, 0.54f + (i == 7 ? 0.12f : 0.0f));
        }
        if (t >= 128) {
            const int32_t group = (t - 128) >> 4;
            const int32_t lane = t & 15;
            if ((group & 1) != 0) d = contourOffset - (d - contourOffset) + ((i >= 8) ? 1 : -1);
            if ((group & 2) != 0 && (i == 2 || i == 6 || i == 10 || i == 14)) {
                d += ((lane + i) & 2) ? 3 : -2;
                g = std::max(g, 0.42f + 0.10f * static_cast<float>((lane >> 1) & 1));
            }
            if ((group & 4) != 0 && (i == 1 || i == 5 || i == 9 || i == 13)) {
                d = contourOffset + ((i < 8) ? (2 + (lane & 3)) : (5 - (lane & 3)));
                g = std::max(g, 0.38f + 0.10f * static_cast<float>(lane & 1));
            }
            if (t >= 160 && (i == 0 || i == 8 || i == 15)) {
                d = contourOffset + (i == 8 ? ((lane & 1) ? 5 : 3) : 0);
                g = std::max(g, i == 15 ? 0.86f : 0.80f);
            }
            if (t >= 176 && ((i + lane) % 5) == 0) {
                d += ((i & 8) ? -1 : 1) * (1 + ((lane >> 2) & 1));
                g = std::max(g, 0.44f);
            }
            if (t >= 208 && (i == 3 || i == 4 || i == 11 || i == 12)) {
                d = contourOffset + ((i < 8) ? ((lane & 1) ? 7 : 4) : ((lane & 2) ? -2 : 2));
                g = std::max(g, 0.50f + (i == 4 || i == 12 ? 0.12f : 0.0f));
            }
            if (t >= 224 && (i == 6 || i == 10 || i == 14)) {
                d = contourOffset + ((i == 14) ? 0 : ((lane & 1) ? -3 : 6));
                g = std::max(g, i == 14 ? 0.80f : 0.54f);
            }
            if (t >= 240 && (i == 2 || i == 7 || i == 9 || i == 13)) {
                d += ((i == 7 || i == 9) ? 4 : -1);
                g = std::max(g, 0.56f);
            }
        }
        motif[i] = d;
        gate[i] = clamp01(g);
        const bool anchor = (i == 0 || i == 8 || i == 15);
        dur[i] = gate[i] > 0.0f ? (anchor ? 1.08f : 0.58f + 0.28f * ((i & 3) == 0 ? 1.0f : 0.0f)) : 0.0f;
    }

    motif[0] = contourOffset;
    gate[0] = std::max(gate[0], 0.88f);
    motif[15] = contourOffset;
    gate[15] = std::max(gate[15], 0.58f);
}

void MusicEngine::deriveAnswerMotif() {
    for (int32_t i = 0; i < kPhraseSteps; ++i) {
        const int32_t src = (kPhraseSteps - 1 - i);
        int32_t inv = -mComposition.motifA[src];
        if (i == 0) inv = 4;
        if (i == 7) inv = 2;
        if (i == 15) inv = 0;
        mComposition.motifB[i] = inv + mComposition.answerOffset;
        mComposition.gateB[i] = std::max(0.0f, mComposition.gateA[src] * (0.72f + 0.18f * ((i & 3) == 0 ? 1.0f : 0.0f)));
        mComposition.durB[i] = mComposition.durA[src] * 0.88f;

        int32_t var = mComposition.motifA[i];
        if ((i & 7) == 6 || (i & 7) == 7) var += (i < 8 ? 2 : -1);
        if (i == 12) var = 4;
        if (i == 15) var = 0;
        mComposition.motifC[i] = var;
        mComposition.gateC[i] = std::min(1.0f, mComposition.gateA[i] * 1.08f + ((i == 1 || i == 9) ? 0.42f : 0.0f));
        mComposition.durC[i] = std::max(0.45f, mComposition.durA[i] * 0.76f);

        int32_t call = mComposition.motifA[(i + 4) & 15];
        if ((i & 3) == 0) call = mComposition.motifA[i];
        mComposition.motifD[i] = call + ((i >= 8) ? mComposition.answerOffset : 0);
        mComposition.gateD[i] = std::min(1.0f, mComposition.gateA[(i + 4) & 15] * (0.70f + 0.18f * ((i & 3) == 0 ? 1.0f : 0.0f)));
        mComposition.durD[i] = std::max(0.25f, mComposition.durA[i] * 0.60f);

        int32_t recall = mComposition.motifA[i];
        if (i == 4 || i == 12) recall += (i == 4 ? 2 : -1);
        if (i == 15) recall = 0;
        mComposition.motifE[i] = recall;
        mComposition.gateE[i] = std::min(1.0f, mComposition.gateA[i] * 0.92f + ((i == 0 || i == 8 || i == 15) ? 0.24f : 0.0f));
        mComposition.durE[i] = std::max(0.35f, mComposition.durA[i] * 1.10f);

        // v18.1: additional abstract grammar passes.  These make the derived motifs
        // feel less like copies and more like related answers, recalls, and shadows.
        if ((mComposition.themeShapeId & 1) == 0 && (i == 6 || i == 14)) {
            mComposition.motifB[i] = (i == 14) ? 0 : (mComposition.hookOffset + 5);
            mComposition.gateB[i] = std::max(mComposition.gateB[i], 0.66f);
        }
        if ((mComposition.counterShape % 3) == 1 && (i == 2 || i == 10)) {
            mComposition.motifC[i] += (i == 2) ? 2 : -2;
            mComposition.gateC[i] = std::max(mComposition.gateC[i], 0.62f);
        }
        if ((mComposition.counterShape % 5) == 2 && (i == 3 || i == 11 || i == 15)) {
            mComposition.motifD[i] = (i == 15) ? 0 : mComposition.motifA[(i + 12) & 15] + ((i < 8) ? 1 : -1);
            mComposition.gateD[i] = std::max(mComposition.gateD[i], 0.54f);
        }
        if (mComposition.longMemory > 0.70f && (i == 0 || i == 8 || i == 15)) {
            mComposition.motifE[i] = (i == 8) ? mComposition.answerOffset : 0;
            mComposition.gateE[i] = std::max(mComposition.gateE[i], i == 15 ? 0.82f : 0.68f);
            mComposition.durE[i] = std::max(mComposition.durE[i], i == 15 ? 1.30f : 0.95f);
        }
    }
}

void MusicEngine::chooseInstrumentPalette(const StyleProfile& p) {
    auto toneBucket = [&](int32_t bucket, int32_t count) {
        const float w = 1.0f / static_cast<float>(std::max(1, count));
        return clamp((static_cast<float>(bucket) + 0.12f + 0.76f * mRng.uni()) * w, 0.01f, 0.99f);
    };
    auto lane = [&](float probability, float lo, float hi) {
        if (!mRng.chance(clamp01(probability))) return 0.0f;
        return clamp(lo + (hi - lo) * mRng.uni(), 0.0f, 1.25f);
    };

    mComposition.drama = clamp(0.10f + p.drama * 0.86f + mRng.bipolar() * 0.18f, 0.08f, 0.98f);
    if (mRng.chance(0.24f)) mComposition.drama = clamp(mComposition.drama + 0.18f + 0.22f * mRng.uni(), 0.08f, 0.99f);
    mComposition.deviceDepth = clamp(0.20f + mComposition.drama * 0.78f + mRng.bipolar() * 0.16f, 0.16f, 0.98f);
    if (mRng.chance(0.20f + 0.20f * p.drama)) mComposition.deviceDepth = clamp(mComposition.deviceDepth + 0.20f, 0.18f, 0.99f);
    mComposition.hookEmphasis = clamp(1.02f + p.melody * 0.68f + mComposition.drama * 0.44f, 0.92f, 2.18f);
    mComposition.surgeLift = clamp(0.98f + mComposition.drama * 0.66f + p.density * 0.28f, 0.92f, 1.92f);

    // v10 keeps the wider per-piece instrument window. Lanes are intensities, not simple on/off gates.
    mComposition.useKick = lane(0.88f + p.drum * 0.22f - (p.ambient ? 0.20f : 0.0f), 0.62f, 1.00f);
    mComposition.useSnare = lane(0.76f + p.drum * 0.24f - (p.ambient ? 0.20f : 0.0f), 0.54f, 1.00f);
    mComposition.useHat = lane(0.74f + p.density * 0.22f - p.space * 0.08f, 0.48f, 1.00f);
    mComposition.useOpenHat = lane(0.50f + p.density * 0.22f - p.space * 0.08f, 0.34f, 0.88f);
    mComposition.usePerc = lane(0.64f + p.sync * 0.28f + p.rough * 0.12f, 0.44f, 1.00f);
    mComposition.useBass = lane(0.92f + p.bass * 0.14f, 0.72f, 1.08f);
    mComposition.useChord = lane(0.60f + p.chord * 0.36f + p.texture * 0.12f, 0.42f, 1.00f);
    mComposition.useLead = lane(0.76f + p.melody * 0.32f, 0.58f, 1.14f);
    mComposition.useTexture = lane(0.58f + p.texture * 0.38f + p.space * 0.10f, 0.36f, 1.00f);
    mComposition.useArp = lane(0.44f + p.melodyRun * 0.50f + p.brightness * 0.10f, 0.32f, 0.94f);
    mComposition.useCounter = lane(0.42f + p.melody * 0.42f + p.sync * 0.14f, 0.30f, 0.88f);
    mComposition.useStab = lane(0.34f + p.chord * 0.42f + p.density * 0.10f, 0.30f, 0.88f);
    mComposition.useDrone = lane(0.28f + p.texture * 0.42f + p.space * 0.18f, 0.24f, 0.82f);
    mComposition.useSpark = lane(0.30f + p.brightness * 0.32f + p.sync * 0.12f, 0.22f, 0.76f);
    mComposition.useFx = lane(0.26f + p.rough * 0.34f + p.drama * 0.20f, 0.20f, 0.72f);
    mComposition.useEcho = lane(0.34f + p.space * 0.30f + p.melody * 0.20f, 0.24f, 0.82f);
    mComposition.useOrbit = lane(0.32f + p.melodyRun * 0.34f + p.texture * 0.18f, 0.22f, 0.80f);
    mComposition.useBloom = lane(0.30f + p.chord * 0.34f + p.drama * 0.18f, 0.24f, 0.88f);
    mComposition.useGlyph = lane(0.34f + p.brightness * 0.24f + p.sync * 0.24f, 0.22f, 0.78f);
    mComposition.useSub = lane(0.42f + p.bass * 0.34f, 0.28f, 0.92f);
    mComposition.useSheen = lane(0.28f + p.brightness * 0.38f + p.space * 0.12f, 0.20f, 0.76f);
    mComposition.usePluck = lane(0.36f + p.melodyRun * 0.44f + p.sync * 0.14f, 0.22f, 0.88f);
    mComposition.useBell = lane(0.28f + p.brightness * 0.36f + p.chord * 0.18f, 0.18f, 0.78f);
    mComposition.usePulse = lane(0.30f + p.sync * 0.38f + p.density * 0.16f, 0.24f, 0.86f);
    mComposition.useGrain = lane(0.24f + p.rough * 0.42f + p.texture * 0.18f, 0.18f, 0.72f);
    mComposition.useComet = lane(0.20f + p.drama * 0.36f + p.melody * 0.14f, 0.16f, 0.70f);
    mComposition.useRotor = lane(0.28f + p.chord * 0.36f + p.space * 0.18f, 0.22f, 0.84f);

    switch (mPattern.style) {
        case StyleType::VelvetDrift:
        case StyleType::SoftVoltage:
        case StyleType::PulseGarden:
        case StyleType::IonGarden:
            mComposition.useKick *= lane(0.72f, 0.58f, 1.0f);
            mComposition.useSnare *= lane(0.68f, 0.50f, 1.0f);
            mComposition.useChord = std::max(mComposition.useChord, 0.76f);
            mComposition.useLead = std::max(mComposition.useLead, 0.72f);
            mComposition.useTexture = std::max(mComposition.useTexture, 0.70f);
            mComposition.useDrone = std::max(mComposition.useDrone, 0.48f);
            mComposition.useBloom = std::max(mComposition.useBloom, 0.48f);
            mComposition.useEcho = std::max(mComposition.useEcho, 0.40f);
            break;
        case StyleType::DeepMagnet:
        case StyleType::VoidStep:
        case StyleType::SubOrbit:
        case StyleType::MarbleBass:
            mComposition.useBass = std::max(mComposition.useBass, 0.96f);
            mComposition.useTexture = std::max(mComposition.useTexture, 0.58f);
            mComposition.useFx = std::max(mComposition.useFx, 0.36f);
            mComposition.useSub = std::max(mComposition.useSub, 0.60f);
            mComposition.useOrbit = std::max(mComposition.useOrbit, 0.34f);
            break;
        case StyleType::ChromeBloom:
        case StyleType::SolarFold:
        case StyleType::PrismCruise:
        case StyleType::EchoCrown:
            mComposition.useLead = std::max(mComposition.useLead, 0.90f);
            mComposition.useArp = std::max(mComposition.useArp, 0.46f);
            mComposition.useChord = std::max(mComposition.useChord, 0.56f);
            mComposition.useSpark = std::max(mComposition.useSpark, 0.40f);
            mComposition.useSheen = std::max(mComposition.useSheen, 0.44f);
            mComposition.useEcho = std::max(mComposition.useEcho, 0.48f);
            break;
        case StyleType::MagentaWell:
            mComposition.useLead = std::max(mComposition.useLead, 0.92f);
            mComposition.useCounter = std::max(mComposition.useCounter, 0.54f);
            mComposition.usePluck = std::max(mComposition.usePluck, 0.62f);
            mComposition.useBell = std::max(mComposition.useBell, 0.48f);
            mComposition.useRotor = std::max(mComposition.useRotor, 0.42f);
            break;
        case StyleType::CarbonRain:
            mComposition.useKick = std::max(mComposition.useKick, 0.88f);
            mComposition.usePerc = std::max(mComposition.usePerc, 0.78f);
            mComposition.usePulse = std::max(mComposition.usePulse, 0.58f);
            mComposition.useGrain = std::max(mComposition.useGrain, 0.56f);
            mComposition.useFx = std::max(mComposition.useFx, 0.48f);
            break;
        case StyleType::LatticeSun:
            mComposition.useArp = std::max(mComposition.useArp, 0.72f);
            mComposition.usePluck = std::max(mComposition.usePluck, 0.58f);
            mComposition.usePulse = std::max(mComposition.usePulse, 0.52f);
            mComposition.useSheen = std::max(mComposition.useSheen, 0.52f);
            mComposition.useBell = std::max(mComposition.useBell, 0.42f);
            break;
        case StyleType::StrangeHarbor:
            mComposition.useChord = std::max(mComposition.useChord, 0.82f);
            mComposition.useTexture = std::max(mComposition.useTexture, 0.82f);
            mComposition.useDrone = std::max(mComposition.useDrone, 0.64f);
            mComposition.useRotor = std::max(mComposition.useRotor, 0.50f);
            mComposition.useComet = std::max(mComposition.useComet, 0.34f);
            mComposition.useBell = std::max(mComposition.useBell, 0.34f);
            break;
        case StyleType::CopperChord:
            mComposition.useChord = std::max(mComposition.useChord, 0.86f);
            mComposition.useLead = std::max(mComposition.useLead, 0.78f);
            mComposition.useCounter = std::max(mComposition.useCounter, 0.52f);
            mComposition.useRotor = std::max(mComposition.useRotor, 0.46f);
            mComposition.useBell = std::max(mComposition.useBell, 0.42f);
            break;
        case StyleType::GhostMeter:
            mComposition.usePerc = std::max(mComposition.usePerc, 0.66f);
            mComposition.useEcho = std::max(mComposition.useEcho, 0.58f);
            mComposition.useOrbit = std::max(mComposition.useOrbit, 0.54f);
            mComposition.useCounter = std::max(mComposition.useCounter, 0.44f);
            mComposition.useGlyph = std::max(mComposition.useGlyph, 0.38f);
            break;
        case StyleType::ObsidianBloom:
            mComposition.useBass = std::max(mComposition.useBass, 0.92f);
            mComposition.useSub = std::max(mComposition.useSub, 0.72f);
            mComposition.useChord = std::max(mComposition.useChord, 0.76f);
            mComposition.useDrone = std::max(mComposition.useDrone, 0.68f);
            mComposition.useBloom = std::max(mComposition.useBloom, 0.54f);
            break;
        case StyleType::VoltageMoth:
            mComposition.useLead = std::max(mComposition.useLead, 0.94f);
            mComposition.useArp = std::max(mComposition.useArp, 0.72f);
            mComposition.useSheen = std::max(mComposition.useSheen, 0.60f);
            mComposition.usePluck = std::max(mComposition.usePluck, 0.56f);
            mComposition.useSpark = std::max(mComposition.useSpark, 0.52f);
            break;
        case StyleType::QuartzTide:
            mComposition.useChord = std::max(mComposition.useChord, 0.86f);
            mComposition.useTexture = std::max(mComposition.useTexture, 0.82f);
            mComposition.useLead = std::max(mComposition.useLead, 0.74f);
            mComposition.useBell = std::max(mComposition.useBell, 0.54f);
            mComposition.useComet = std::max(mComposition.useComet, 0.40f);
            break;
        case StyleType::StaticCathedral:
            mComposition.useChord = std::max(mComposition.useChord, 0.88f);
            mComposition.useTexture = std::max(mComposition.useTexture, 0.78f);
            mComposition.useDrone = std::max(mComposition.useDrone, 0.62f);
            mComposition.useFx = std::max(mComposition.useFx, 0.46f);
            mComposition.useRotor = std::max(mComposition.useRotor, 0.52f);
            break;
        case StyleType::MercuryThread:
            mComposition.useHat = std::max(mComposition.useHat, 0.80f);
            mComposition.usePerc = std::max(mComposition.usePerc, 0.76f);
            mComposition.usePulse = std::max(mComposition.usePulse, 0.64f);
            mComposition.useGrain = std::max(mComposition.useGrain, 0.52f);
            mComposition.useArp = std::max(mComposition.useArp, 0.54f);
            break;
        case StyleType::NightLatch:
            mComposition.useBass = std::max(mComposition.useBass, 0.86f);
            mComposition.useSub = std::max(mComposition.useSub, 0.62f);
            mComposition.usePerc = std::max(mComposition.usePerc, 0.56f);
            mComposition.useEcho = std::max(mComposition.useEcho, 0.46f);
            mComposition.useBloom = std::max(mComposition.useBloom, 0.42f);
            break;
        case StyleType::TinyGrid:
            mComposition.useKick = std::max(mComposition.useKick, 0.82f);
            mComposition.useHat = std::max(mComposition.useHat, 0.62f);
            mComposition.useArp = std::max(mComposition.useArp, 0.36f);
            break;
        case StyleType::BitFog:
            mComposition.usePerc = std::max(mComposition.usePerc, 0.72f);
            mComposition.useGlyph = std::max(mComposition.useGlyph, 0.54f);
            mComposition.useFx = std::max(mComposition.useFx, 0.46f);
            break;
        case StyleType::WarmCurrent:
            mComposition.useBass = std::max(mComposition.useBass, 0.82f);
            mComposition.useChord = std::max(mComposition.useChord, 0.62f);
            mComposition.useLead = std::max(mComposition.useLead, 0.74f);
            mComposition.useCounter = std::max(mComposition.useCounter, 0.38f);
            break;
        default:
            break;
    }

    if (mComposition.useBass + mComposition.useChord + mComposition.useLead < 2.30f) {
        mComposition.useBass = std::max(mComposition.useBass, 0.84f);
        if (p.melody >= 0.42f) mComposition.useLead = std::max(mComposition.useLead, 0.72f);
        if (p.chord >= 0.32f || p.texture >= 0.40f) mComposition.useChord = std::max(mComposition.useChord, 0.58f);
    }
    if (mComposition.useKick + mComposition.useSnare + mComposition.useHat + mComposition.usePerc < 2.10f && !p.ambient) {
        mComposition.useKick = std::max(mComposition.useKick, 0.78f);
        mComposition.useSnare = std::max(mComposition.useSnare, 0.58f);
        mComposition.useHat = std::max(mComposition.useHat, 0.52f);
    }

    int32_t active = 0;
    auto on = [&](float v) { return v > 0.05f; };
    active += on(mComposition.useKick); active += on(mComposition.useSnare);
    active += on(mComposition.useHat); active += on(mComposition.useOpenHat);
    active += on(mComposition.usePerc); active += on(mComposition.useBass);
    active += on(mComposition.useChord); active += on(mComposition.useLead);
    active += on(mComposition.useTexture); active += on(mComposition.useArp);
    active += on(mComposition.useCounter); active += on(mComposition.useStab);
    active += on(mComposition.useDrone); active += on(mComposition.useSpark);
    active += on(mComposition.useFx); active += on(mComposition.useEcho);
    active += on(mComposition.useOrbit); active += on(mComposition.useBloom);
    active += on(mComposition.useGlyph); active += on(mComposition.useSub);
    active += on(mComposition.useSheen); active += on(mComposition.usePluck);
    active += on(mComposition.useBell); active += on(mComposition.usePulse);
    active += on(mComposition.useGrain); active += on(mComposition.useComet);
    active += on(mComposition.useRotor);
    while (active < (p.ambient ? 12 : 15)) {
        switch (mRng.rangeInt(0, 19)) {
            case 0: if (!on(mComposition.useOpenHat)) { mComposition.useOpenHat = 0.42f; ++active; } break;
            case 1: if (!on(mComposition.usePerc)) { mComposition.usePerc = 0.50f; ++active; } break;
            case 2: if (!on(mComposition.useChord)) { mComposition.useChord = 0.54f; ++active; } break;
            case 3: if (!on(mComposition.useLead)) { mComposition.useLead = 0.60f; ++active; } break;
            case 4: if (!on(mComposition.useTexture)) { mComposition.useTexture = 0.46f; ++active; } break;
            case 5: if (!on(mComposition.useArp)) { mComposition.useArp = 0.38f; ++active; } break;
            case 6: if (!on(mComposition.useCounter)) { mComposition.useCounter = 0.40f; ++active; } break;
            case 7: if (!on(mComposition.useStab)) { mComposition.useStab = 0.40f; ++active; } break;
            case 8: if (!on(mComposition.useEcho)) { mComposition.useEcho = 0.38f; ++active; } break;
            case 9: if (!on(mComposition.useOrbit)) { mComposition.useOrbit = 0.34f; ++active; } break;
            case 10: if (!on(mComposition.useBloom)) { mComposition.useBloom = 0.40f; ++active; } break;
            case 11: if (!on(mComposition.useGlyph)) { mComposition.useGlyph = 0.34f; ++active; } break;
            case 12: if (!on(mComposition.useSub)) { mComposition.useSub = 0.38f; ++active; } break;
            case 13: if (!on(mComposition.useSheen)) { mComposition.useSheen = 0.32f; ++active; } break;
            case 14: if (!on(mComposition.usePluck)) { mComposition.usePluck = 0.36f; ++active; } break;
            case 15: if (!on(mComposition.useBell)) { mComposition.useBell = 0.30f; ++active; } break;
            case 16: if (!on(mComposition.usePulse)) { mComposition.usePulse = 0.34f; ++active; } break;
            case 17: if (!on(mComposition.useGrain)) { mComposition.useGrain = 0.28f; ++active; } break;
            case 18: if (!on(mComposition.useComet)) { mComposition.useComet = 0.24f; ++active; } break;
            default: if (!on(mComposition.useRotor)) { mComposition.useRotor = 0.34f; ++active; } break;
        }
    }

    int32_t bassBucket = 2, leadBucket = 2, padBucket = 2, kickBucket = 2, snareBucket = 2, hatBucket = 2, percBucket = 2, textureBucket = 2;
    switch (mPattern.style) {
        case StyleType::ConcretePulse: bassBucket = 2; leadBucket = 1; padBucket = 1; kickBucket = 3; snareBucket = 4; hatBucket = 3; percBucket = 4; textureBucket = 1; break;
        case StyleType::GlassNoir: bassBucket = 4; leadBucket = 3; padBucket = 5; kickBucket = 1; snareBucket = 2; hatBucket = 5; percBucket = 5; textureBucket = 4; break;
        case StyleType::ShardRush: bassBucket = 3; leadBucket = 5; padBucket = 2; kickBucket = 4; snareBucket = 5; hatBucket = 6; percBucket = 6; textureBucket = 5; break;
        case StyleType::NeonLatch: bassBucket = 1; leadBucket = 4; padBucket = 3; kickBucket = 2; snareBucket = 1; hatBucket = 4; percBucket = 3; textureBucket = 2; break;
        case StyleType::TinyGrid: bassBucket = 0; leadBucket = 0; padBucket = 4; kickBucket = 0; snareBucket = 0; hatBucket = 1; percBucket = 1; textureBucket = 3; break;
        case StyleType::PrismCruise: bassBucket = 2; leadBucket = 6; padBucket = 5; kickBucket = 1; snareBucket = 2; hatBucket = 2; percBucket = 2; textureBucket = 3; break;
        case StyleType::BrokenMagnet: bassBucket = 5; leadBucket = 5; padBucket = 2; kickBucket = 4; snareBucket = 6; hatBucket = 5; percBucket = 6; textureBucket = 6; break;
        case StyleType::VelvetDrift: bassBucket = 0; leadBucket = 2; padBucket = 6; kickBucket = 0; snareBucket = 1; hatBucket = 0; percBucket = 2; textureBucket = 6; break;
        case StyleType::SubOrbit: bassBucket = 5; leadBucket = 1; padBucket = 4; kickBucket = 2; snareBucket = 3; hatBucket = 2; percBucket = 3; textureBucket = 5; break;
        case StyleType::SoftVoltage: bassBucket = 0; leadBucket = 2; padBucket = 6; kickBucket = 0; snareBucket = 0; hatBucket = 1; percBucket = 1; textureBucket = 4; break;
        case StyleType::DeepMagnet: bassBucket = 5; leadBucket = 0; padBucket = 3; kickBucket = 5; snareBucket = 3; hatBucket = 0; percBucket = 5; textureBucket = 6; break;
        case StyleType::WarmCurrent: bassBucket = 2; leadBucket = 4; padBucket = 5; kickBucket = 2; snareBucket = 2; hatBucket = 3; percBucket = 2; textureBucket = 3; break;
        case StyleType::PulseGarden: bassBucket = 0; leadBucket = 6; padBucket = 6; kickBucket = 0; snareBucket = 1; hatBucket = 1; percBucket = 2; textureBucket = 6; break;
        case StyleType::VoidStep: bassBucket = 5; leadBucket = 3; padBucket = 4; kickBucket = 5; snareBucket = 4; hatBucket = 2; percBucket = 6; textureBucket = 6; break;
        case StyleType::SolarFold: bassBucket = 1; leadBucket = 6; padBucket = 5; kickBucket = 1; snareBucket = 2; hatBucket = 3; percBucket = 3; textureBucket = 2; break;
        case StyleType::IonGarden: bassBucket = 0; leadBucket = 8; padBucket = 8; kickBucket = 0; snareBucket = 0; hatBucket = 1; percBucket = 2; textureBucket = 8; break;
        case StyleType::MarbleBass: bassBucket = 8; leadBucket = 2; padBucket = 3; kickBucket = 5; snareBucket = 4; hatBucket = 2; percBucket = 5; textureBucket = 5; break;
        case StyleType::EchoCrown: bassBucket = 2; leadBucket = 9; padBucket = 7; kickBucket = 1; snareBucket = 2; hatBucket = 4; percBucket = 4; textureBucket = 4; break;
        case StyleType::BitFog: bassBucket = 6; leadBucket = 5; padBucket = 2; kickBucket = 4; snareBucket = 7; hatBucket = 7; percBucket = 8; textureBucket = 7; break;
        case StyleType::MagentaWell: bassBucket = 2; leadBucket = 9; padBucket = 7; kickBucket = 1; snareBucket = 2; hatBucket = 3; percBucket = 3; textureBucket = 5; break;
        case StyleType::CarbonRain: bassBucket = 7; leadBucket = 4; padBucket = 2; kickBucket = 6; snareBucket = 8; hatBucket = 8; percBucket = 9; textureBucket = 8; break;
        case StyleType::LatticeSun: bassBucket = 1; leadBucket = 10; padBucket = 6; kickBucket = 1; snareBucket = 2; hatBucket = 5; percBucket = 4; textureBucket = 3; break;
        case StyleType::StrangeHarbor: bassBucket = 4; leadBucket = 3; padBucket = 10; kickBucket = 0; snareBucket = 1; hatBucket = 0; percBucket = 2; textureBucket = 10; break;
        case StyleType::CopperChord: bassBucket = 2; leadBucket = 7; padBucket = 8; kickBucket = 1; snareBucket = 2; hatBucket = 2; percBucket = 3; textureBucket = 5; break;
        case StyleType::GhostMeter: bassBucket = 3; leadBucket = 4; padBucket = 6; kickBucket = 2; snareBucket = 3; hatBucket = 5; percBucket = 7; textureBucket = 8; break;
        case StyleType::ObsidianBloom: bassBucket = 9; leadBucket = 3; padBucket = 9; kickBucket = 5; snareBucket = 4; hatBucket = 0; percBucket = 5; textureBucket = 10; break;
        case StyleType::VoltageMoth: bassBucket = 1; leadBucket = 10; padBucket = 6; kickBucket = 1; snareBucket = 2; hatBucket = 6; percBucket = 5; textureBucket = 4; break;
        case StyleType::QuartzTide: bassBucket = 0; leadBucket = 9; padBucket = 10; kickBucket = 0; snareBucket = 1; hatBucket = 1; percBucket = 2; textureBucket = 10; break;
        case StyleType::StaticCathedral: bassBucket = 8; leadBucket = 4; padBucket = 10; kickBucket = 5; snareBucket = 6; hatBucket = 3; percBucket = 8; textureBucket = 10; break;
        case StyleType::MercuryThread: bassBucket = 2; leadBucket = 8; padBucket = 5; kickBucket = 3; snareBucket = 7; hatBucket = 9; percBucket = 9; textureBucket = 7; break;
        case StyleType::NightLatch: bassBucket = 8; leadBucket = 2; padBucket = 5; kickBucket = 5; snareBucket = 5; hatBucket = 2; percBucket = 6; textureBucket = 8; break;
        case StyleType::ChromeBloom:
        default: bassBucket = 1; leadBucket = 6; padBucket = 5; kickBucket = 1; snareBucket = 1; hatBucket = 3; percBucket = 2; textureBucket = 2; break;
    }
    const int32_t drift = mRng.chance(0.50f + p.palette * 0.28f) ? mRng.rangeInt(-2, 2) : 0;
    leadBucket = clamp(leadBucket + drift + (mRng.chance(0.28f) ? mRng.rangeInt(-1, 1) : 0), 0, 10);
    bassBucket = clamp(bassBucket + (mRng.chance(0.42f) ? mRng.rangeInt(-2, 2) : 0), 0, 9);
    padBucket = clamp(padBucket + (mRng.chance(0.42f) ? mRng.rangeInt(-2, 2) : 0), 0, 10);
    percBucket = clamp(percBucket + (mRng.chance(0.36f) ? mRng.rangeInt(-2, 2) : 0), 0, 10);

    mComposition.kickTone = toneBucket(kickBucket, 10);
    mComposition.snareTone = toneBucket(snareBucket, 11);
    mComposition.hatTone = toneBucket(hatBucket, 11);
    mComposition.percTone = toneBucket(percBucket, 11);
    mComposition.bassTone = toneBucket(bassBucket, 10);
    mComposition.padTone = toneBucket(padBucket, 11);
    mComposition.leadTone = toneBucket(leadBucket, 11);
    mComposition.arpTone = toneBucket(clamp(leadBucket + mRng.rangeInt(-3, 3), 0, 10), 11);
    mComposition.counterTone = toneBucket(clamp(leadBucket + mRng.rangeInt(-4, 4), 0, 10), 11);
    mComposition.stabTone = toneBucket(clamp(padBucket + mRng.rangeInt(-3, 3), 0, 10), 11);
    mComposition.droneTone = toneBucket(clamp(padBucket + mRng.rangeInt(-4, 2), 0, 10), 11);
    mComposition.sparkTone = toneBucket(clamp(percBucket + mRng.rangeInt(-2, 4), 0, 10), 11);
    mComposition.fxTone = toneBucket(clamp(textureBucket + mRng.rangeInt(-2, 4), 0, 10), 11);
    mComposition.echoTone = toneBucket(clamp(leadBucket + mRng.rangeInt(-5, 5), 0, 10), 11);
    mComposition.orbitTone = toneBucket(clamp(leadBucket + mRng.rangeInt(-4, 4), 0, 10), 11);
    mComposition.bloomTone = toneBucket(clamp(padBucket + mRng.rangeInt(-2, 4), 0, 10), 11);
    mComposition.glyphTone = toneBucket(clamp(percBucket + mRng.rangeInt(-4, 4), 0, 10), 11);
    mComposition.subTone = toneBucket(clamp(bassBucket + mRng.rangeInt(-2, 2), 0, 10), 11);
    mComposition.sheenTone = toneBucket(clamp(leadBucket + mRng.rangeInt(0, 5), 0, 10), 11);
    mComposition.pluckTone = toneBucket((leadBucket + 2) % 11, 11);
    mComposition.bellTone = toneBucket((leadBucket + 4) % 11, 11);
    mComposition.pulseTone = toneBucket((leadBucket + percBucket + 1) % 11, 11);
    mComposition.grainTone = toneBucket((textureBucket + percBucket + 3) % 11, 11);
    mComposition.cometTone = toneBucket((leadBucket + 6) % 11, 11);
    mComposition.rotorTone = toneBucket((padBucket + 5) % 11, 11);
    mComposition.textureTone = toneBucket(textureBucket, 11);

    uint32_t h = 0x811c9dc5u;
    auto mix = [&](uint32_t v) {
        h ^= v + 0x9e3779b9u + (h << 6u) + (h >> 2u);
        h *= 0x85ebca6bu;
    };
    auto q = [](float v) { return static_cast<uint32_t>(std::max(0, std::min(255, static_cast<int32_t>(std::lround(v * 255.0f))))); };
    mix(q(mComposition.useKick)); mix(q(mComposition.useSnare)); mix(q(mComposition.useHat)); mix(q(mComposition.useOpenHat));
    mix(q(mComposition.usePerc)); mix(q(mComposition.useBass)); mix(q(mComposition.useChord)); mix(q(mComposition.useLead));
    mix(q(mComposition.useTexture)); mix(q(mComposition.useArp)); mix(q(mComposition.useCounter)); mix(q(mComposition.useStab));
    mix(q(mComposition.useDrone)); mix(q(mComposition.useSpark)); mix(q(mComposition.useFx));
    mix(q(mComposition.useEcho)); mix(q(mComposition.useOrbit)); mix(q(mComposition.useBloom));
    mix(q(mComposition.useGlyph)); mix(q(mComposition.useSub)); mix(q(mComposition.useSheen));
    mix(q(mComposition.usePluck)); mix(q(mComposition.useBell)); mix(q(mComposition.usePulse));
    mix(q(mComposition.useGrain)); mix(q(mComposition.useComet)); mix(q(mComposition.useRotor));
    mix(static_cast<uint32_t>(bassBucket)); mix(static_cast<uint32_t>(leadBucket)); mix(static_cast<uint32_t>(padBucket));
    mix(static_cast<uint32_t>(kickBucket)); mix(static_cast<uint32_t>(snareBucket)); mix(static_cast<uint32_t>(hatBucket));
    mix(static_cast<uint32_t>(percBucket)); mix(static_cast<uint32_t>(textureBucket));
    mComposition.paletteHash = h;
}

void MusicEngine::generateComposition(const StyleProfile& p) {
    ++mComposition.generation;
    mComposition.chordRoot.fill(0);
    mComposition.form.fill(PhraseType::Statement);
    chooseInstrumentPalette(p);

    mComposition.pieceSteps = mInfinitePieceLength ? 1000000000 : pieceStepsFromSeconds(mRequestedPieceSeconds, mBpmTarget);
    mStyleTargetSteps = mComposition.pieceSteps;
    mComposition.conclusiveOutro = !mInfinitePieceLength && mRng.chance(0.34f + 0.22f * p.drama + 0.10f * p.chord);
    mComposition.outroFadeSteps = kPhraseSteps * mRng.rangeInt(2, p.ambient ? 9 : 6);
    const int32_t maxOutroFadeSteps = std::max(kPhraseSteps, std::min(kPhraseSteps * 6, mComposition.pieceSteps / 5));
    mComposition.outroFadeSteps = std::min(mComposition.outroFadeSteps, maxOutroFadeSteps);
    mComposition.arcSeed = mRng.nextU32();
    mComposition.themeCount = mRng.rangeInt(5, 8);
    mComposition.recallCycle = mRng.rangeInt(5, 17);
    mComposition.dialogueCycle = mRng.rangeInt(3, 10);
    mComposition.counterShape = mRng.rangeInt(0, 47);
    mComposition.themeShapeId = mRng.rangeInt(0, 15);
    mComposition.longMemory = clamp(0.44f + p.melody * 0.32f + mRng.uni() * 0.30f, 0.34f, 0.99f);
    mComposition.callResponse = clamp(0.34f + p.melody * 0.42f + p.sync * 0.18f + mRng.bipolar() * 0.07f, 0.24f, 0.98f);
    mComposition.counterpoint = clamp(0.30f + p.melody * 0.34f + p.chord * 0.18f + mRng.uni() * 0.24f, 0.18f, 0.94f);
    mComposition.melodicGravity = clamp(0.60f + p.melody * 0.26f + p.chord * 0.12f, 0.50f, 0.98f);
    mComposition.phraseArc = clamp(0.34f + p.melodyRun * 0.34f + mRng.uni() * 0.22f, 0.20f, 0.94f);
    mComposition.layerDepth = clamp(0.48f + p.palette * 0.34f + p.density * 0.18f, 0.35f, 1.00f);
    for (int32_t i = 0; i < kThemeSlots; ++i) {
        static constexpr int32_t order[8] = {0, 2, -1, 4, 1, -2, 5, 3};
        mComposition.themeOffset[i] = order[(i + mComposition.themeShapeId) & 7];
        mComposition.themeContour[i] = ((i + mComposition.counterShape) & 1) ? 1 : -1;
        mComposition.themeWeight[i] = clamp(0.56f + 0.06f * static_cast<float>(i) + 0.24f * mRng.uni(), 0.36f, 0.98f);
    }
    mComposition.themeOffset[0] = 0;
    {
        const int32_t totalPhrases = std::max(2, mComposition.pieceSteps / kPhraseSteps);
        const int32_t baseSection = totalPhrases > 160 ? 10 : (totalPhrases > 64 ? 8 : 6);
        mComposition.sectionPhraseLength = std::max(4, std::min(18, baseSection + mRng.rangeInt(-2, 4)));
        mComposition.hookCycle = mRng.rangeInt(3, 6);
    }

    mComposition.progressionLength = mRng.chance(0.25f) ? 8 : 4;
    static constexpr int32_t prog[80][8] = {
        {0, 5, 6, 0, 0, 5, 6, 4},
        {0, 3, 6, 0, 0, 3, 5, 4},
        {0, 0, 5, 6, 0, 0, 3, 4},
        {0, 6, 5, 6, 0, 6, 3, 4},
        {0, 4, 5, 3, 0, 4, 6, 5},
        {0, 2, 3, 5, 0, 2, 5, 4},
        {0, 1, 0, 6, 0, 1, 3, 0},
        {0, 0, -1, 0, 0, 3, 2, 0},
        {0, 4, 0, 6, 0, 4, 3, 2},
        {0, 5, 3, 4, 0, 6, 5, 4},
        {0, 2, 5, 4, 0, 3, 5, 4},
        {0, 6, 4, 3, 0, 5, 4, 2},
        {0, 0, 4, 5, 0, 2, 4, 6},
        {0, -1, 3, 5, 0, -1, 4, 3},
        {0, 2, 0, 4, 5, 4, 2, 0},
        {0, 7, 5, 3, 0, 6, 5, 2},
        {0, -2, 0, 2, 0, 3, 2, -1},
        {0, 4, 7, 6, 0, 4, 5, 3},
        {0, 0, 2, 5, 0, 0, 4, 6},
        {0, 5, 4, 2, 0, -1, 3, 2},
        {0, 3, 2, 0, 5, 4, 2, 0},
        {0, 6, 7, 5, 0, 4, 3, 2},
        {0, -1, 2, 0, 4, 2, -1, 0},
        {0, 5, 0, 7, 6, 4, 2, 0},
        {0, 2, 4, 2, 5, 4, 2, 0},
        {0, -2, 3, 0, 6, 5, 3, 0},
        {0, 7, 0, 5, 4, 2, 0, -1},
        {0, 0, 6, 4, 0, 3, 5, 4},
        {0, 1, 4, 5, 0, 6, 4, 1},
        {0, 5, 2, 3, 0, 4, 7, 5},
        {0, -1, -2, 0, 3, 2, 0, -1},
        {0, 4, 2, 5, 7, 5, 4, 0},
        {0, 3, 5, 7, 0, 5, 3, 2},
        {0, -1, 0, 4, 6, 4, 2, 0},
        {0, 2, 6, 5, 3, 2, 0, -1},
        {0, 7, 6, 4, 2, 4, 5, 0},
        {0, -2, 2, 5, 0, 3, 2, -1},
        {0, 5, 7, 9, 7, 5, 3, 0},
        {0, 4, 1, 5, 0, 6, 3, 2},
        {0, 0, 5, 2, 7, 5, 2, 0},
        {0, 3, 5, 3, 0, 4, 2, 0},
        {0, -2, 4, 0, 5, 3, 0, -1},
        {0, 6, 2, 5, 0, 7, 4, 2},
        {0, 0, 3, 7, 0, 5, 3, 0},
        {0, -1, 4, 2, 0, 3, 5, 2},
        {0, 5, 1, 4, 0, 6, 2, 5},
        {0, 2, 7, 5, 4, 2, 0, -1},
        {0, -2, 0, 5, 7, 4, 2, 0},
        {0, 4, 6, 3, 0, 5, 2, 0},
        {0, 7, 5, 2, 0, 4, 6, 5},
        {0, 3, 0, -1, 0, 5, 4, 2},
        {0, 2, 3, 2, 0, 5, 3, 0},
        {0, -1, 4, 5, 0, 2, 6, 4},
        {0, 6, 4, 1, 0, 5, 2, -1},
        {0, 0, 7, 5, 3, 5, 2, 0},
        {0, 4, 2, -1, 0, 6, 5, 3},
        {0, -2, 5, 4, 2, 0, 3, -1},
        {0, 3, 6, 5, 0, 4, 1, 0},
        {0, 5, 2, -2, 0, 7, 5, 2},
        {0, 1, 3, 6, 0, 4, 5, 1},
        {0, 7, 4, 6, 5, 3, 2, 0},
        {0, -1, 5, 2, 0, 4, 2, -2},
        {0, 2, 0, 6, 4, 0, 5, 3},
        {0, 5, 6, 3, 0, 7, 4, 2},
        {0, -2, 1, 4, 0, 3, 6, 5},
        {0, 4, 0, 2, 5, 7, 5, 0},
        {0, 3, -1, 2, 0, 5, 7, 4},
        {0, 6, 2, 0, 5, 3, 1, 0},
        {0, -1, 0, 5, 2, 6, 4, 0},
        {0, 7, 5, 0, 3, 5, 6, 2},
        {0, 2, 4, -1, 0, 5, 3, 2},
        {0, 5, 1, 6, 0, 4, 2, -1},
        {0, -2, 3, 6, 5, 2, 0, -1},
        {0, 4, 7, 5, 2, 0, 3, 5},
        {0, 0, 2, -1, 4, 6, 5, 0},
        {0, 6, 3, 1, 0, 5, 7, 4},
        {0, 2, 5, 3, -1, 0, 4, 2},
        {0, 7, 3, 5, 0, 6, 2, 4},
        {0, -1, 6, 4, 1, 0, 5, 3},
        {0, 4, 2, 7, 5, 1, 3, 0}
    };

    int32_t pi = mRng.rangeInt(0, 79);
    if (mPattern.style == StyleType::GlassNoir || mPattern.style == StyleType::SubOrbit ||
        mPattern.style == StyleType::DeepMagnet || mPattern.style == StyleType::VoidStep ||
        mPattern.style == StyleType::MarbleBass) {
        pi = mRng.chance(0.50f) ? 6 : 7;
    }
    if (mPattern.style == StyleType::PrismCruise || mPattern.style == StyleType::ChromeBloom ||
        mPattern.style == StyleType::SolarFold || mPattern.style == StyleType::WarmCurrent ||
        mPattern.style == StyleType::EchoCrown || mPattern.style == StyleType::IonGarden) {
        pi = mRng.chance(0.50f) ? 0 : 4;
    }
    if (mPattern.style == StyleType::TinyGrid || mPattern.style == StyleType::BitFog) pi = mRng.chance(0.50f) ? 2 : 8;
    if (mPattern.style == StyleType::SoftVoltage || mPattern.style == StyleType::VelvetDrift ||
        mPattern.style == StyleType::PulseGarden) {
        pi = mRng.chance(0.50f) ? 1 : 9;
    }
    if (mPattern.style == StyleType::MagentaWell) pi = mRng.chance(0.50f) ? 14 : 17;
    if (mPattern.style == StyleType::CarbonRain) pi = mRng.chance(0.50f) ? 11 : 6;
    if (mPattern.style == StyleType::LatticeSun) pi = mRng.chance(0.50f) ? 18 : 23;
    if (mPattern.style == StyleType::StrangeHarbor) pi = mRng.chance(0.50f) ? 16 : 22;
    if (mPattern.style == StyleType::NeonLatch || mPattern.style == StyleType::EchoCrown) pi = mRng.chance(0.35f) ? 21 : pi;
    if (mPattern.style == StyleType::ConcretePulse || mPattern.style == StyleType::TinyGrid) pi = mRng.chance(0.28f) ? 20 : pi;
    if (mPattern.style == StyleType::CopperChord) pi = mRng.chance(0.55f) ? 40 : 45;
    if (mPattern.style == StyleType::GhostMeter) pi = mRng.chance(0.50f) ? 41 : 47;
    if (mPattern.style == StyleType::ObsidianBloom || mPattern.style == StyleType::NightLatch) pi = mRng.chance(0.52f) ? 42 : 46;
    if (mPattern.style == StyleType::VoltageMoth || mPattern.style == StyleType::MercuryThread) pi = mRng.chance(0.50f) ? 43 : 48;
    if (mPattern.style == StyleType::QuartzTide) pi = mRng.chance(0.54f) ? 44 : 49;
    if (mPattern.style == StyleType::StaticCathedral) pi = mRng.chance(0.50f) ? 50 : 51;
    mComposition.progressionId = pi;
    for (int32_t i = 0; i < kMaxProgressionSlots; ++i) mComposition.chordRoot[i] = prog[pi][i];
    if (mComposition.progressionLength == 4) {
        for (int32_t i = 4; i < kMaxProgressionSlots; ++i) mComposition.chordRoot[i] = mComposition.chordRoot[i & 3];
    }

    static constexpr PhraseType formA[8] = {
        PhraseType::Statement, PhraseType::Repeat, PhraseType::Answer, PhraseType::Variation,
        PhraseType::Hook, PhraseType::Orbit, PhraseType::Answer, PhraseType::Surge
    };
    static constexpr PhraseType formB[8] = {
        PhraseType::Statement, PhraseType::Answer, PhraseType::Mirror, PhraseType::Variation,
        PhraseType::Suspension, PhraseType::Statement, PhraseType::Answer, PhraseType::Cascade
    };
    static constexpr PhraseType formC[12] = {
        PhraseType::Statement, PhraseType::Repeat, PhraseType::Answer, PhraseType::Variation,
        PhraseType::Hook, PhraseType::Mirror, PhraseType::Suspension, PhraseType::Statement,
        PhraseType::Orbit, PhraseType::Answer, PhraseType::Cascade, PhraseType::Statement
    };
    static constexpr PhraseType formD[16] = {
        PhraseType::Statement, PhraseType::Repeat, PhraseType::Answer, PhraseType::Crystallize,
        PhraseType::Hook, PhraseType::Afterimage, PhraseType::Mirror, PhraseType::Variation,
        PhraseType::Eclipse, PhraseType::Orbit, PhraseType::Answer, PhraseType::Cascade,
        PhraseType::Hook, PhraseType::Surge, PhraseType::Afterimage, PhraseType::Statement
    };
    static constexpr PhraseType formE[16] = {
        PhraseType::Statement, PhraseType::Orbit, PhraseType::Answer, PhraseType::Mirror,
        PhraseType::Hook, PhraseType::Crystallize, PhraseType::Variation, PhraseType::Afterimage,
        PhraseType::Statement, PhraseType::Eclipse, PhraseType::Answer, PhraseType::Cascade,
        PhraseType::Hook, PhraseType::Variation, PhraseType::Surge, PhraseType::Statement
    };
    static constexpr PhraseType formF[16] = {
        PhraseType::Suspension, PhraseType::Statement, PhraseType::Repeat, PhraseType::Answer,
        PhraseType::Orbit, PhraseType::Mirror, PhraseType::Hook, PhraseType::Afterimage,
        PhraseType::Crystallize, PhraseType::Statement, PhraseType::Variation, PhraseType::Answer,
        PhraseType::Eclipse, PhraseType::Cascade, PhraseType::Surge, PhraseType::Statement
    };
    if (mRng.chance(0.18f + 0.26f * mComposition.longMemory + 0.10f * p.drama)) {
        mComposition.formLength = 16;
        const PhraseType* picked = mRng.chance(0.50f) ? formE : formF;
        for (int32_t i = 0; i < 16; ++i) mComposition.form[i] = picked[i];
    } else if (mRng.chance(0.24f + 0.20f * mComposition.longMemory)) {
        mComposition.formLength = 16;
        for (int32_t i = 0; i < 16; ++i) mComposition.form[i] = formD[i];
    } else if (mRng.chance(0.38f)) {
        mComposition.formLength = 12;
        for (int32_t i = 0; i < 12; ++i) mComposition.form[i] = formC[i];
    } else if (mRng.chance(0.50f)) {
        mComposition.formLength = 8;
        for (int32_t i = 0; i < 8; ++i) mComposition.form[i] = formA[i];
    } else {
        mComposition.formLength = 8;
        for (int32_t i = 0; i < 8; ++i) mComposition.form[i] = formB[i];
    }

    int32_t motifTemplate = mRng.rangeInt(0, 255);
    if (mPattern.style == StyleType::SoftVoltage || mPattern.style == StyleType::VelvetDrift ||
        mPattern.style == StyleType::PulseGarden || mPattern.style == StyleType::IonGarden) {
        motifTemplate = mRng.rangeInt(8, 254);
    }
    if (mPattern.style == StyleType::DeepMagnet || mPattern.style == StyleType::VoidStep ||
        mPattern.style == StyleType::MarbleBass) {
        motifTemplate = mRng.chance(0.50f) ? 7 : mRng.rangeInt(11, 255);
    }
    if (mPattern.style == StyleType::ChromeBloom || mPattern.style == StyleType::PrismCruise ||
        mPattern.style == StyleType::SolarFold || mPattern.style == StyleType::EchoCrown) {
        motifTemplate = mRng.chance(0.50f) ? mRng.rangeInt(9, 255) : mRng.rangeInt(6, 236);
    }
    if (mPattern.style == StyleType::WarmCurrent) motifTemplate = mRng.chance(0.50f) ? 6 : mRng.rangeInt(8, 246);
    if (mPattern.style == StyleType::BitFog) motifTemplate = mRng.rangeInt(2, 244);
    if (mPattern.style == StyleType::MagentaWell || mPattern.style == StyleType::LatticeSun) motifTemplate = mRng.rangeInt(18, 255);
    if (mPattern.style == StyleType::CarbonRain) motifTemplate = mRng.rangeInt(2, 244);
    if (mPattern.style == StyleType::StrangeHarbor) motifTemplate = mRng.rangeInt(8, 254);
    if (mPattern.style == StyleType::CopperChord || mPattern.style == StyleType::QuartzTide) motifTemplate = mRng.rangeInt(24, 255);
    if (mPattern.style == StyleType::VoltageMoth || mPattern.style == StyleType::MercuryThread) motifTemplate = mRng.rangeInt(40, 255);
    if (mPattern.style == StyleType::ObsidianBloom || mPattern.style == StyleType::NightLatch || mPattern.style == StyleType::StaticCathedral) motifTemplate = mRng.rangeInt(8, 240);
    if (mPattern.style == StyleType::GhostMeter) motifTemplate = mRng.rangeInt(12, 248);
    mComposition.motifTemplateId = motifTemplate;
    mComposition.hookOffset = (mRng.chance(0.50f) ? 0 : (mRng.chance(0.50f) ? 2 : -1));
    mComposition.answerOffset = (mRng.chance(0.50f) ? 0 : (mRng.chance(0.50f) ? 1 : -1));
    const bool lowRegisterStyle = (mPattern.style == StyleType::GlassNoir || mPattern.style == StyleType::SubOrbit ||
                                   mPattern.style == StyleType::DeepMagnet || mPattern.style == StyleType::VoidStep ||
                                   mPattern.style == StyleType::MarbleBass || mPattern.style == StyleType::CarbonRain ||
                                   mPattern.style == StyleType::StrangeHarbor || mPattern.style == StyleType::ObsidianBloom ||
                                   mPattern.style == StyleType::NightLatch || mPattern.style == StyleType::StaticCathedral);
    const bool highRegisterStyle = (mPattern.style == StyleType::PrismCruise || mPattern.style == StyleType::ChromeBloom ||
                                    mPattern.style == StyleType::SoftVoltage || mPattern.style == StyleType::SolarFold ||
                                    mPattern.style == StyleType::WarmCurrent || mPattern.style == StyleType::IonGarden ||
                                    mPattern.style == StyleType::EchoCrown || mPattern.style == StyleType::MagentaWell ||
                                    mPattern.style == StyleType::LatticeSun || mPattern.style == StyleType::VoltageMoth ||
                                    mPattern.style == StyleType::QuartzTide || mPattern.style == StyleType::MercuryThread ||
                                    mPattern.style == StyleType::CopperChord);
    mComposition.octaveBias = lowRegisterStyle ? 2 : (highRegisterStyle ? 3 : 2);
    mComposition.motifGain = clamp(1.12f + p.melody * 1.02f + mRng.bipolar() * 0.09f, 0.82f, 2.12f);
    mComposition.bassGain = clamp(0.90f + p.bass * 0.72f + mRng.bipolar() * 0.08f, 0.64f, 1.56f);
    mComposition.chordGain = clamp(0.58f + p.chord * 0.78f + p.texture * 0.42f, 0.38f, 1.46f);
    mComposition.motifGain *= mComposition.hookEmphasis;
    mComposition.ornament = clamp(0.16f + p.melodyRun * 0.62f + p.sync * 0.22f, 0.08f, 0.86f);
    mComposition.leadSpace = clamp(p.space * 0.42f + (p.ambient ? 0.18f : 0.01f), 0.03f, 0.62f);

    fillMotifFromTemplate(mComposition.motifA, mComposition.gateA, mComposition.durA, motifTemplate, mComposition.hookOffset);
    deriveAnswerMotif();

    mComposition.bassRel.fill(0);
    mComposition.bassGate.fill(0.0f);
    mComposition.chordGate.fill(0.0f);
    auto bass = [&](int32_t step, int32_t rel, float gate) {
        if (step >= 0 && step < kPhraseSteps) {
            mComposition.bassRel[step] = rel;
            mComposition.bassGate[step] = gate;
        }
    };
    if (p.fourOnFloor) {
        bass(0, 0, 0.98f); bass(4, 0, 0.74f); bass(7, 4, 0.52f); bass(8, 0, 0.88f); bass(12, 4, 0.64f); bass(15, -1, 0.46f);
        mComposition.chordGate[0] = 0.70f; mComposition.chordGate[8] = 0.34f;
    } else if (p.halfTime) {
        bass(0, 0, 0.98f); bass(3, 0, 0.56f); bass(8, 4, 0.76f); bass(10, 0, 0.50f); bass(14, -1, 0.44f);
        mComposition.chordGate[0] = 0.52f; mComposition.chordGate[12] = 0.24f;
    } else if (p.breakbeat) {
        bass(0, 0, 0.96f); bass(5, 4, 0.62f); bass(8, 0, 0.76f); bass(11, 2, 0.48f); bass(14, -1, 0.40f);
        mComposition.chordGate[0] = 0.34f;
    } else if (p.ambient) {
        bass(0, 0, 0.72f); bass(10, 4, 0.34f);
        mComposition.chordGate[0] = 0.92f; mComposition.chordGate[8] = 0.42f;
    } else {
        bass(0, 0, 0.98f); bass(3, 0, 0.44f); bass(6, 4, 0.56f); bass(10, 0, 0.72f); bass(13, 2, 0.42f); bass(15, -1, 0.36f);
        mComposition.chordGate[0] = 0.48f; mComposition.chordGate[8] = 0.22f;
    }

    uint32_t mh = 0x811c9dc5u;
    auto mixMotif = [&](uint32_t v) {
        mh ^= v + 0x9e3779b9u + (mh << 6u) + (mh >> 2u);
        mh *= 0x85ebca6bu;
    };
    for (int32_t i = 0; i < kPhraseSteps; ++i) {
        mixMotif(static_cast<uint32_t>(mComposition.motifA[i] + 64));
        mixMotif(static_cast<uint32_t>(mComposition.motifB[i] + 64));
        mixMotif(static_cast<uint32_t>(mComposition.motifC[i] + 64));
        mixMotif(static_cast<uint32_t>(mComposition.motifD[i] + 64));
        mixMotif(static_cast<uint32_t>(mComposition.motifE[i] + 64));
        mixMotif(static_cast<uint32_t>(std::lround(mComposition.gateA[i] * 31.0f)));
        mixMotif(static_cast<uint32_t>(std::lround(mComposition.gateB[i] * 31.0f)));
        mixMotif(static_cast<uint32_t>(std::lround(mComposition.gateC[i] * 31.0f)));
        mixMotif(static_cast<uint32_t>(std::lround(mComposition.gateD[i] * 31.0f)));
        mixMotif(static_cast<uint32_t>(std::lround(mComposition.gateE[i] * 31.0f)));
        mixMotif(static_cast<uint32_t>(mComposition.bassRel[i] + 64));
        mixMotif(static_cast<uint32_t>(std::lround(mComposition.bassGate[i] * 31.0f)));
        mixMotif(static_cast<uint32_t>(std::lround(mComposition.chordGate[i] * 31.0f)));
    }
    for (int32_t i = 0; i < kMaxProgressionSlots; ++i) mixMotif(static_cast<uint32_t>(mComposition.chordRoot[i] + 64));
    mixMotif(static_cast<uint32_t>(mComposition.formLength));
    mixMotif(static_cast<uint32_t>(mComposition.progressionId));
    mixMotif(static_cast<uint32_t>(mComposition.motifTemplateId));
    for (int32_t i = 0; i < kThemeSlots; ++i) {
        mixMotif(static_cast<uint32_t>(mComposition.themeOffset[i] + 64));
        mixMotif(static_cast<uint32_t>(mComposition.themeContour[i] + 64));
        mixMotif(static_cast<uint32_t>(std::lround(mComposition.themeWeight[i] * 31.0f)));
    }
    mixMotif(static_cast<uint32_t>(mComposition.themeCount));
    mixMotif(static_cast<uint32_t>(mComposition.recallCycle));
    mixMotif(static_cast<uint32_t>(mComposition.counterShape));
    mixMotif(static_cast<uint32_t>(mComposition.themeShapeId));
    mComposition.motifHash = mh;

    writeCompositionToPattern();
}

void MusicEngine::writeCompositionToPattern() {
    for (int32_t i = 0; i < kMotifSteps; ++i) {
        const int32_t pos = i & (kPhraseSteps - 1);
        mPattern.bassMotif[i] = mComposition.bassRel[pos];
        mPattern.leadMotif[i] = mComposition.motifA[pos];
        mPattern.leadGate[i] = mComposition.gateA[pos];
    }
    for (int32_t i = 0; i < kChordSteps; ++i) {
        mPattern.chordMotif[i] = mComposition.chordRoot[i & (kMaxProgressionSlots - 1)];
    }
}

void MusicEngine::mutateDrumsOnly() {
    const StyleProfile p = profile(mPattern.style);
    const int32_t pos = mRng.rangeInt(0, kPatternSteps - 1);
    const int32_t track = mRng.rangeInt(0, 4);
    const float delta = mRng.bipolar() * (0.035f + 0.10f * mPattern.roughness + 0.04f * mNovelty);
    switch (track) {
        case 0: mPattern.kick[pos] += delta * 0.70f; break;
        case 1: mPattern.snare[pos] += delta * 0.85f; break;
        case 2: mPattern.hat[pos] += delta * (p.trapHats ? 1.35f : 0.90f); break;
        case 3: mPattern.perc[pos] += delta * 1.40f; break;
        default: mPattern.openHat[pos] += delta * 0.55f; break;
    }
    repairPattern();
}

void MusicEngine::repairPattern() {
    const StyleProfile p = profile(mPattern.style);
    for (int32_t i = 0; i < kPatternSteps; ++i) {
        mPattern.kick[i] = clamp(mPattern.kick[i], 0.0f, 0.98f);
        mPattern.snare[i] = clamp(mPattern.snare[i], 0.0f, 0.98f);
        mPattern.hat[i] = clamp(mPattern.hat[i], 0.0f, 0.98f);
        mPattern.openHat[i] = clamp(mPattern.openHat[i], 0.0f, 0.72f);
        mPattern.perc[i] = clamp(mPattern.perc[i], 0.0f, 0.90f);
        mPattern.bass[i] = clamp(mPattern.bass[i], 0.0f, 0.98f);
        mPattern.chord[i] = clamp(mPattern.chord[i], 0.0f, 0.78f);
        mPattern.lead[i] = clamp(mPattern.lead[i], 0.0f, 0.90f);
        mPattern.accent[i] = clamp(mPattern.accent[i], 0.0f, 1.0f);
    }

    if (p.fourOnFloor) {
        for (int32_t i = 0; i < kPatternSteps; i += 4) mPattern.kick[i] = std::max(mPattern.kick[i], 0.74f);
    } else if (!p.ambient) {
        for (int32_t i = 0; i < kPatternSteps; i += 16) mPattern.kick[i] = std::max(mPattern.kick[i], 0.78f);
    } else {
        mPattern.kick[0] = std::max(mPattern.kick[0], 0.12f);
    }

    if (p.halfTime) {
        for (int32_t i = 8; i < kPatternSteps; i += 16) mPattern.snare[i] = std::max(mPattern.snare[i], p.ambient ? 0.08f : 0.74f);
    } else if (!p.ambient) {
        for (int32_t bar = 0; bar < 4; ++bar) {
            mPattern.snare[bar * 16 + 4] = std::max(mPattern.snare[bar * 16 + 4], 0.70f);
            mPattern.snare[bar * 16 + 12] = std::max(mPattern.snare[bar * 16 + 12], 0.72f);
        }
    }

    if (p.trapHats) {
        for (int32_t i = 0; i < kPatternSteps; ++i) mPattern.hat[i] = std::max(mPattern.hat[i], (i & 1) ? 0.24f : 0.38f);
    } else if (!p.ambient) {
        for (int32_t i = 0; i < kPatternSteps; i += 2) mPattern.hat[i] = std::max(mPattern.hat[i], 0.16f + 0.22f * p.density);
    }

    for (int32_t i = 0; i < kPatternSteps; ++i) {
        const int32_t n = (i + 1) & (kPatternSteps - 1);
        if (mPattern.kick[i] > 0.70f && mPattern.kick[n] > 0.62f && !p.breakbeat) mPattern.kick[n] *= 0.52f;
        if (mPattern.snare[i] > 0.66f && mPattern.snare[n] > 0.62f) mPattern.snare[n] *= 0.55f;
    }

    for (int32_t i = 0; i < kMotifSteps; ++i) {
        mPattern.bassMotif[i] = static_cast<int32_t>(clamp(static_cast<float>(mPattern.bassMotif[i]), -9.0f, 12.0f));
        mPattern.leadMotif[i] = static_cast<int32_t>(clamp(static_cast<float>(mPattern.leadMotif[i]), -4.0f, 22.0f));
        mPattern.leadGate[i] = clamp(mPattern.leadGate[i], 0.0f, 1.0f);
    }
    for (int32_t i = 0; i < kChordSteps; ++i) {
        mPattern.chordMotif[i] = static_cast<int32_t>(clamp(static_cast<float>(mPattern.chordMotif[i]), -7.0f, 12.0f));
    }
}

void MusicEngine::storeMemory() {
    mMemory[mMemoryWrite] = mPattern;
    mMemoryWrite = (mMemoryWrite + 1) % kMemorySlots;
}

void MusicEngine::recallMemory(float amount) {
    const Pattern& mem = mMemory[mRng.rangeInt(0, kMemorySlots - 1)];
    const int32_t moves = mRng.rangeInt(4, 18);
    for (int32_t i = 0; i < moves; ++i) {
        const int32_t pos = mRng.rangeInt(0, kPatternSteps - 1);
        if (mRng.chance(amount)) mPattern.kick[pos] = 0.55f * mPattern.kick[pos] + 0.45f * mem.kick[pos];
        if (mRng.chance(amount)) mPattern.snare[pos] = 0.55f * mPattern.snare[pos] + 0.45f * mem.snare[pos];
        if (mRng.chance(amount)) mPattern.bass[pos] = 0.55f * mPattern.bass[pos] + 0.45f * mem.bass[pos];
        if (mRng.chance(amount)) mPattern.lead[pos] = 0.55f * mPattern.lead[pos] + 0.45f * mem.lead[pos];
    }
    if (mRng.chance(amount)) {
        const int32_t start = mRng.rangeInt(0, kMotifSteps - 8);
        for (int32_t i = 0; i < 8; ++i) {
            mPattern.leadMotif[start + i] = mem.leadMotif[start + i] + mRng.rangeInt(-1, 1);
            mPattern.leadGate[start + i] = clamp(0.65f * mPattern.leadGate[start + i] + 0.35f * mem.leadGate[start + i], 0.0f, 1.0f);
        }
    }
    repairPattern();
}

void MusicEngine::mutateSmall() {
    const StyleProfile p = profile(mPattern.style);
    const int32_t pos = mRng.rangeInt(0, kPatternSteps - 1);
    const int32_t track = mRng.rangeInt(0, 7);
    const float wild = 0.05f + 0.18f * mPattern.roughness + 0.08f * mNovelty;
    const float delta = mRng.bipolar() * wild;

    switch (track) {
        case 0: mPattern.kick[pos] += delta; break;
        case 1: mPattern.snare[pos] += delta; break;
        case 2: mPattern.hat[pos] += delta * (p.trapHats ? 1.4f : 0.9f); break;
        case 3: mPattern.perc[pos] += delta * 1.3f; break;
        case 4: mPattern.bass[pos] += delta; break;
        case 5: mPattern.chord[pos] += delta * (0.6f + p.chord); break;
        default: mPattern.lead[pos] += delta * (0.8f + p.melody); break;
    }

    if (mRng.chance(0.38f)) {
        const int32_t m = mRng.rangeInt(0, kMotifSteps - 1);
        if (mRng.chance(0.52f)) mPattern.leadMotif[m] += mRng.rangeInt(-2, 2);
        else mPattern.bassMotif[m] += mRng.rangeInt(-1, 1);
        mPattern.leadGate[m] = clamp(mPattern.leadGate[m] + mRng.bipolar() * 0.20f, 0.0f, 1.0f);
    }

    if (mRng.chance(0.08f)) {
        const int32_t a = mRng.rangeInt(0, kPatternSteps - 1);
        const int32_t b = (a + mRng.rangeInt(3, 15)) & (kPatternSteps - 1);
        std::swap(mPattern.perc[a], mPattern.perc[b]);
        if (mRng.chance(0.50f)) std::swap(mPattern.lead[a], mPattern.lead[b]);
    }

    repairPattern();
}

void MusicEngine::mutateLarge() {
    storeMemory();

    const StyleProfile p = profile(mPattern.style);
    const int32_t moves = mRng.rangeInt(8, 28);
    for (int32_t i = 0; i < moves; ++i) mutateSmall();

    if (mRng.chance(0.45f)) recallMemory(0.22f + 0.34f * mRng.uni());
    if (mRng.chance(0.30f)) {
        const int32_t shift = mRng.rangeInt(-3, 3);
        for (auto& note : mPattern.leadMotif) note += shift;
    }
    if (mRng.chance(0.25f)) {
        mPattern.energy = clamp(mPattern.energy + mRng.bipolar() * 0.24f, 0.12f, 0.98f);
        mPattern.density = clamp(mPattern.density + mRng.bipolar() * 0.20f, 0.08f, 0.98f);
        mPattern.melody = clamp(mPattern.melody + mRng.bipolar() * 0.28f + p.melody * 0.10f, 0.03f, 0.98f);
        mPattern.texture = clamp(mPattern.texture + mRng.bipolar() * 0.24f, 0.0f, 0.96f);
    }

    const uint32_t h = patternHash();
    if (!isHashRecent(h)) {
        mRecentHash[mRecentHashWrite] = h;
        mRecentHashWrite = (mRecentHashWrite + 1) % kRecentHashes;
        mNovelty = std::max(0.0f, mNovelty - 0.35f);
    } else {
        mNovelty = std::min(1.0f, mNovelty + 0.15f);
    }

    repairPattern();
}

uint32_t MusicEngine::patternHash() const {
    uint32_t h = 2166136261u;
    auto add = [&h](int32_t v) {
        h ^= static_cast<uint32_t>(v + 0x9e3779b9u);
        h *= 16777619u;
    };
    add(static_cast<int32_t>(mPattern.style));
    add(mPattern.rootMidi);
    add(mPattern.scaleMode);
    for (int32_t i = 0; i < kPatternSteps; ++i) {
        add(static_cast<int32_t>(mPattern.kick[i] * 7.0f));
        add(static_cast<int32_t>(mPattern.snare[i] * 7.0f));
        add(static_cast<int32_t>(mPattern.bass[i] * 7.0f));
        add(static_cast<int32_t>(mPattern.lead[i] * 7.0f));
        if ((i & 1) == 0) add(static_cast<int32_t>(mPattern.perc[i] * 5.0f));
    }
    for (int32_t i = 0; i < kMotifSteps; i += 2) {
        add(mPattern.bassMotif[i]);
        add(mPattern.leadMotif[i]);
        add(static_cast<int32_t>(mPattern.leadGate[i] * 11.0f));
    }
    for (int32_t i = 0; i < kChordSteps; ++i) add(mPattern.chordMotif[i]);
    add(static_cast<int32_t>(mComposition.motifHash & 0xffffu));
    add(static_cast<int32_t>((mComposition.motifHash >> 16u) & 0xffffu));
    add(static_cast<int32_t>(mComposition.paletteHash & 0xffffu));
    add(static_cast<int32_t>(mComposition.progressionId));
    add(static_cast<int32_t>(mComposition.formLength));
    return h ? h : 1u;
}

bool MusicEngine::isHashRecent(uint32_t hash) const {
    for (uint32_t h : mRecentHash) {
        if (h == hash && h != 0u) return true;
    }
    return false;
}

uint32_t MusicEngine::motifSignatureHash() const {
    uint32_t h = 2166136261u ^ 0x4d4f5449u;
    auto add = [&h](int32_t v) {
        h ^= static_cast<uint32_t>(v + 0x9e3779b9u);
        h *= 16777619u;
    };
    add(static_cast<int32_t>(mPattern.style));
    add(mPattern.scaleMode);
    add(mComposition.motifTemplateId);
    add(mComposition.progressionId);
    add(mComposition.themeShapeId);
    add(mComposition.counterShape);
    add(mComposition.formLength);
    for (int32_t i = 0; i < kPhraseSteps; ++i) {
        add(mComposition.motifA[i]);
        add(mComposition.motifB[i]);
        add(mComposition.motifC[i]);
        add(mComposition.motifD[i]);
        add(mComposition.motifE[i]);
        add(static_cast<int32_t>(std::lround(mComposition.gateA[i] * 15.0f)));
        add(static_cast<int32_t>(std::lround(mComposition.gateB[i] * 15.0f)));
        add(mComposition.bassRel[i]);
        add(static_cast<int32_t>(std::lround(mComposition.bassGate[i] * 15.0f)));
    }
    for (int32_t i = 0; i < kMaxProgressionSlots; ++i) add(mComposition.chordRoot[i]);
    for (int32_t i = 0; i < kThemeSlots; ++i) {
        add(mComposition.themeOffset[i]);
        add(mComposition.themeContour[i]);
    }
    return h ? h : 1u;
}

bool MusicEngine::isMotifHashRecent(uint32_t hash) const {
    for (uint32_t h : mRecentMotifHash) {
        if (h == hash && h != 0u) return true;
    }
    return false;
}

int32_t MusicEngine::outroGravitySteps() const {
    const int32_t totalPhrases = std::max(2, mComposition.pieceSteps / kPhraseSteps);
    if (totalPhrases <= 6) return kPhraseSteps;
    if (totalPhrases <= 16) return kPhraseSteps * 2;
    return kPhraseSteps * 3;
}

int32_t MusicEngine::currentChordRoot(int32_t step) const {
    if (mComposition.conclusiveOutro && !mInfinitePieceLength && step >= mComposition.pieceSteps - outroGravitySteps()) {
        return 0;
    }
    const int32_t progression = std::max(1, std::min(kMaxProgressionSlots, mComposition.progressionLength));
    int32_t bar = step / kPhraseSteps;
    while (bar < 0) bar += progression;
    return mComposition.chordRoot[bar % progression];
}

MusicEngine::SectionType MusicEngine::currentSectionType(int32_t step) const {
    const int32_t phrase = std::max(0, step) / kPhraseSteps;
    const int32_t totalPhrases = std::max(2, mComposition.pieceSteps / kPhraseSteps);
    const int32_t edgePhrases = (totalPhrases <= 6) ? 1 : std::max(2, std::min(8, totalPhrases / 18));

    if (phrase < edgePhrases) return SectionType::Intro;
    if (mExportSinglePieceMode && mExportStopSamples > 0) {
        const int64_t nowSamples = mCurrentPieceSamples.load(std::memory_order_relaxed);
        const int64_t remainingSamples = mExportStopSamples - nowSamples;
        const int64_t outroWindow = std::max<int64_t>(static_cast<int64_t>(mSampleRate) / 2,
                std::min<int64_t>(static_cast<int64_t>(mSampleRate) * 2, mExportStopSamples / 20));
        if (remainingSamples <= outroWindow) return SectionType::Outro;
    } else if (phrase >= totalPhrases - edgePhrases) {
        return SectionType::Outro;
    }

    const int32_t sectionLen = std::max(4, mComposition.sectionPhraseLength);
    const int32_t section = phrase / sectionLen;

    if (mComposition.hookCycle > 0 && (section % mComposition.hookCycle) == 1) {
        return SectionType::Hook;
    }

    uint32_t h = mComposition.arcSeed ^ static_cast<uint32_t>(section * 0x9e3779b9u);
    h ^= h << 13u;
    h ^= h >> 17u;
    h ^= h << 5u;
    const int32_t r = static_cast<int32_t>(h % 100u);

    const int32_t suspensionCut = 5 + static_cast<int32_t>(mComposition.deviceDepth * 9.0f);
    const int32_t mirrorCut = suspensionCut + 7 + static_cast<int32_t>(mComposition.paletteHash & 5u);
    const int32_t orbitCut = mirrorCut + 7 + static_cast<int32_t>(mPattern.texture * 8.0f);
    const int32_t variationCut = 36 + static_cast<int32_t>(mPattern.melody * 14.0f);
    const int32_t cascadeCut = 88 - static_cast<int32_t>(mComposition.drama * 8.0f);
    const int32_t surgeCut = 94 - static_cast<int32_t>(mComposition.drama * 14.0f);

    if (r < suspensionCut) return SectionType::Suspension;
    if (r < mirrorCut) return SectionType::Mirror;
    if (r < orbitCut) return SectionType::Orbit;
    if (r > surgeCut) return SectionType::Surge;
    if (r > cascadeCut) return SectionType::Cascade;
    if (r < variationCut) return SectionType::Variation;
    return SectionType::Theme;
}

MusicEngine::PhraseType MusicEngine::currentPhraseType(int32_t step) const {
    const int32_t len = std::max(1, std::min(kMaxFormSlots, mComposition.formLength));
    int32_t phrase = step / kPhraseSteps;
    while (phrase < 0) phrase += len;

    const SectionType section = currentSectionType(step);
    const int32_t local = phrase & 3;
    switch (section) {
        case SectionType::Intro:
            return local < 1 ? PhraseType::Orbit : (local == 1 ? PhraseType::Suspension : PhraseType::Statement);
        case SectionType::Hook:
            if (local == 0) return PhraseType::Hook;
            if (local == 1) return PhraseType::Repeat;
            if (local == 2) return PhraseType::Answer;
            return PhraseType::Hook;
        case SectionType::Variation:
            if (local == 0) return PhraseType::Statement;
            if (local == 1) return PhraseType::Variation;
            if (local == 2) return PhraseType::Answer;
            return PhraseType::Variation;
        case SectionType::Suspension:
            if (local == 0) return PhraseType::Suspension;
            if (local == 1) return PhraseType::Orbit;
            if (local == 2) return PhraseType::Answer;
            return PhraseType::Mirror;
        case SectionType::Mirror:
            if (local == 0) return PhraseType::Mirror;
            if (local == 1) return PhraseType::Answer;
            if (local == 2) return PhraseType::Variation;
            return PhraseType::Mirror;
        case SectionType::Orbit:
            if (local == 0 || local == 3) return PhraseType::Orbit;
            if (local == 1) return PhraseType::Statement;
            return PhraseType::Answer;
        case SectionType::Cascade:
            if (local == 0) return PhraseType::Cascade;
            if (local == 1) return PhraseType::Hook;
            if (local == 2) return PhraseType::Answer;
            return PhraseType::Surge;
        case SectionType::Surge:
            if (local == 0) return PhraseType::Hook;
            if (local == 1) return PhraseType::Surge;
            if (local == 2) return PhraseType::Answer;
            return PhraseType::Cascade;
        case SectionType::Outro:
            if (local == 0) return PhraseType::Statement;
            if (local == 1) return PhraseType::Afterimage;
            return mComposition.conclusiveOutro ? PhraseType::Crystallize : PhraseType::Eclipse;
        case SectionType::Theme:
        default:
            return mComposition.form[phrase % len];
    }
}

int32_t MusicEngine::grammarDegree(PhraseType phrase,
                                   int32_t phrasePos,
                                   int32_t chordRoot,
                                   bool& isRest,
                                   float& gate,
                                   float& dur) const {
    phrasePos &= (kPhraseSteps - 1);
    const std::array<int32_t, kPhraseSteps>* motif = &mComposition.motifA;
    const std::array<float, kPhraseSteps>* gates = &mComposition.gateA;
    const std::array<float, kPhraseSteps>* durs = &mComposition.durA;
    int32_t trans = 0;
    float gateScale = 1.0f;

    switch (phrase) {
        case PhraseType::Statement:
            motif = &mComposition.motifA; gates = &mComposition.gateA; durs = &mComposition.durA;
            break;
        case PhraseType::Repeat:
            motif = &mComposition.motifA; gates = &mComposition.gateA; durs = &mComposition.durA;
            gateScale = 0.92f;
            break;
        case PhraseType::Answer:
            motif = &mComposition.motifB; gates = &mComposition.gateB; durs = &mComposition.durB;
            break;
        case PhraseType::Variation:
            motif = &mComposition.motifC; gates = &mComposition.gateC; durs = &mComposition.durC;
            trans = ((phrasePos >= 8) ? -1 : 0);
            break;
        case PhraseType::Suspension:
            motif = &mComposition.motifA; gates = &mComposition.gateA; durs = &mComposition.durA;
            gateScale = 0.66f;
            trans = -1;
            break;
        case PhraseType::Hook:
            motif = &mComposition.motifA; gates = &mComposition.gateA; durs = &mComposition.durA;
            gateScale = 1.34f;
            trans = 0;
            break;
        case PhraseType::Mirror:
            motif = &mComposition.motifB; gates = &mComposition.gateB; durs = &mComposition.durB;
            gateScale = 1.02f;
            trans = (phrasePos < 8) ? 1 : -1;
            break;
        case PhraseType::Orbit:
            motif = &mComposition.motifA; gates = &mComposition.gateA; durs = &mComposition.durA;
            gateScale = 0.82f;
            trans = ((phrasePos / 4) & 1) ? 1 : 0;
            break;
        case PhraseType::Cascade:
            motif = &mComposition.motifC; gates = &mComposition.gateC; durs = &mComposition.durC;
            gateScale = 1.24f;
            trans = 1 + ((phrasePos / 4) & 1);
            break;
        case PhraseType::Crystallize:
            motif = &mComposition.motifD; gates = &mComposition.gateD; durs = &mComposition.durD;
            gateScale = 0.70f + 0.055f * static_cast<float>(phrasePos);
            trans = (phrasePos >= 8) ? 1 : 0;
            break;
        case PhraseType::Eclipse:
            motif = &mComposition.motifE; gates = &mComposition.gateE; durs = &mComposition.durE;
            gateScale = (phrasePos < 4 || phrasePos > 12) ? 0.54f : 0.86f;
            trans = -1;
            break;
        case PhraseType::Afterimage:
            motif = &mComposition.motifB; gates = &mComposition.gateB; durs = &mComposition.durB;
            gateScale = 0.72f;
            trans = (phrasePos >= 8) ? -2 : 2;
            break;
        case PhraseType::Surge:
        default:
            motif = &mComposition.motifC; gates = &mComposition.gateC; durs = &mComposition.durC;
            gateScale = 1.18f;
            trans = 1;
            break;
    }

    gate = clamp01((*gates)[phrasePos] * gateScale * mComposition.motifGain);
    dur = std::max(0.25f, (*durs)[phrasePos]);
    isRest = gate <= 0.02f;

    int32_t degree = chordRoot + (*motif)[phrasePos] + trans;
    if (mComposition.conclusiveOutro && !mInfinitePieceLength &&
        mStyleAgeSteps >= mComposition.pieceSteps - outroGravitySteps() &&
        (phrasePos == 0 || phrasePos == 8 || phrasePos == 14 || phrasePos == 15)) {
        degree = chordRoot;
        gate = std::max(gate, phrasePos == 15 ? 0.92f : 0.72f);
        dur = std::max(dur, phrasePos == 15 ? 1.50f : 0.95f);
    }

    const bool strong = (phrasePos == 0 || phrasePos == 4 || phrasePos == 8 || phrasePos == 12 || phrasePos == 15);
    if (strong) {
        const int32_t rel = degree - chordRoot;
        const int32_t chordTones[5] = {0, 2, 4, 6, 7};
        int32_t best = chordRoot;
        int32_t bestDist = 100;
        for (int32_t tone : chordTones) {
            const int32_t cand = chordRoot + tone;
            const int32_t dist = std::abs(cand - degree);
            if (dist < bestDist) {
                bestDist = dist;
                best = cand;
            }
        }
        if (phrasePos == 15) best = chordRoot;
        degree = (bestDist <= 2 || rel == 0) ? best : degree;
    }

    return degree;
}

int32_t MusicEngine::themeIndexForStep(int32_t step) const {
    const int32_t phrase = std::max(0, step) / kPhraseSteps;
    const int32_t count = std::max(1, std::min(kThemeSlots, mComposition.themeCount));
    const int32_t sectionLen = std::max(2, mComposition.sectionPhraseLength);
    int32_t idx = (phrase / sectionLen) % count;

    const int32_t recall = std::max(0, mComposition.recallCycle);
    if (recall > 0) {
        // Large-scale identity: the original theme keeps returning, but not at a simple loop rate.
        if ((phrase % recall) == 0) idx = 0;
        if (phrase > recall && ((phrase + mComposition.dialogueCycle) % (recall * 2 + 1)) == 0) idx = 1 % count;
        if (phrase > recall * 2 && ((phrase + mComposition.themeShapeId) % (recall * 3 + 2)) == 0) idx = 0;
    }

    if (mComposition.longMemory > 0.70f && phrase > sectionLen * 3) {
        const int32_t slow = (phrase / std::max(3, sectionLen / 2)) % count;
        if (((phrase + mComposition.counterShape) & 7) == 3) idx = slow;
    }
    if (mComposition.longMemory > 0.78f && phrase > sectionLen * 6) {
        // Distant recall: not a loop, but a return of identity at phrase offsets
        // that are deliberately not aligned with the regular form cycle.
        const int32_t distant = std::max(5, mComposition.recallCycle + mComposition.dialogueCycle + 1);
        if (((phrase + mComposition.themeShapeId) % distant) == 2) idx = (count > 2) ? 2 : 0;
        if (((phrase * 3 + mComposition.counterShape) % (distant + 7)) == 4) idx = 0;
    }
    return clampInt32(idx, 0, kThemeSlots - 1);
}

int32_t MusicEngine::applyThemeTransform(int32_t degree, int32_t step, int32_t phrasePos, int32_t chordRoot, PhraseType phrase) const {
    const int32_t themeIdx = themeIndexForStep(step);
    const int32_t contour = mComposition.themeContour[themeIdx];
    int32_t rel = degree - chordRoot;
    const int32_t offset = mComposition.themeOffset[themeIdx];
    if (phrase == PhraseType::Hook) {
        rel += offset / 2;
        if (phrasePos == 0 || phrasePos == 8 || phrasePos == 15) rel = (phrasePos == 8) ? offset : 0;
    } else if (phrase == PhraseType::Answer) {
        rel += offset - contour;
    } else if (phrase == PhraseType::Variation || phrase == PhraseType::Cascade) {
        rel += offset + ((phrasePos < 8) ? contour : -contour);
    } else if (phrase == PhraseType::Mirror) {
        rel = offset - rel / 2;
    } else if (phrase == PhraseType::Orbit) {
        rel += (phrasePos < 8) ? offset : -offset / 2;
    } else {
        rel += offset;
    }
    if (mComposition.phraseArc > 0.42f) {
        const int32_t arc = (phrasePos < 8) ? phrasePos : (15 - phrasePos);
        const int32_t lift = (arc >= 5) ? contour : ((arc >= 3) ? (contour > 0 ? 1 : -1) : 0);
        if (phrase == PhraseType::Hook || phrase == PhraseType::Cascade || phrase == PhraseType::Surge) rel += lift;
        else if (phrase == PhraseType::Answer && phrasePos >= 8) rel -= lift;
    }
    if ((phrasePos == 0 || phrasePos == 15) && mComposition.melodicGravity > 0.56f) rel = 0;
    if (phrasePos == 8 && mComposition.melodicGravity > 0.74f) rel = offset;
    if (phrasePos == 14 && mComposition.melodicGravity > 0.82f && std::abs(rel) > 5) rel = (rel > 0) ? 4 : -2;
    return chordRoot + clampInt32(rel, -7, 14);
}

int32_t MusicEngine::counterpointDegree(int32_t step, int32_t phrasePos, int32_t chordRoot) const {
    static constexpr int32_t shapes[48][8] = {
        {7, 5, 4, 2, 0, 2, 4, 5},
        {0, 2, 4, 5, 7, 5, 4, 2},
        {4, 2, 0, -1, 0, 2, 5, 4},
        {5, 4, 2, 0, -2, 0, 2, 4},
        {0, 4, 7, 4, 0, -1, 2, 0},
        {2, 5, 3, 6, 4, 7, 5, 0},
        {0, -1, 2, 4, 5, 4, 2, 0},
        {5, 7, 5, 3, 2, 0, -1, 0},
        {0, 3, 2, 5, 4, 2, 0, -2},
        {7, 4, 5, 2, 3, 0, 2, 0},
        {0, 5, 2, 4, 7, 4, 2, 0},
        {3, 0, -1, 2, 5, 3, 2, 0},
        {0, 2, 0, 4, 2, 5, 4, 0},
        {7, 5, 2, 0, 3, 2, -1, 0},
        {0, 4, 2, 5, 3, 7, 4, 0},
        {5, 3, 0, 2, 4, 2, -1, 0},
        {0, -2, 0, 3, 5, 2, 4, 0},
        {7, 4, 2, 5, 0, 3, 2, 0},
        {0, 5, 7, 4, 2, 4, 3, 0},
        {3, 5, 2, 0, -1, 2, 4, 0},
        {0, 3, 5, 3, 0, -2, 0, 2},
        {7, 9, 7, 5, 4, 2, 0, -1},
        {0, -1, 2, 5, 7, 4, 2, 0},
        {5, 2, 4, 0, 3, -1, 2, 0},
        {0, 4, 6, 4, 2, 0, -1, 0},
        {7, 5, 3, 0, 2, 4, 2, 0},
        {0, -2, 3, 5, 4, 2, 0, 2},
        {5, 7, 9, 7, 4, 2, 0, -1},
        {0, 3, 0, 5, 2, 4, -1, 0},
        {4, 2, -1, 0, 5, 3, 2, 0},
        {0, 5, 4, 7, 5, 2, 4, 0},
        {7, 4, 0, -2, 0, 3, 5, 0},
        {0, 2, 5, 7, 4, 2, -1, 0},
        {5, 3, 1, 0, 4, 6, 4, 2},
        {0, -1, 3, 6, 5, 2, 0, -2},
        {7, 5, 2, 4, 6, 4, 1, 0},
        {0, 4, 1, -1, 2, 5, 3, 0},
        {5, 7, 4, 0, -1, 2, 5, 0},
        {0, 3, 6, 4, 2, 5, 1, 0},
        {7, 2, 4, 5, 3, 0, -1, 0},
        {0, -2, 2, 5, 7, 5, 3, 0},
        {4, 6, 3, 0, 2, 5, 2, -1},
        {0, 5, 1, 4, 7, 4, 2, 0},
        {7, 3, 0, 2, -1, 0, 4, 0},
        {0, 4, 6, 2, 5, 3, 1, 0},
        {5, 2, -2, 0, 3, 6, 4, 0},
        {0, 7, 5, 3, 6, 2, 4, 0},
        {4, 1, 3, 5, 2, -1, 0, 0}
    };
    const int32_t themeIdx = themeIndexForStep(step);
    const int32_t shape = clampInt32(mComposition.counterShape, 0, 47);
    const int32_t idx = ((phrasePos / 2) + (step / kPhraseSteps) + themeIdx) & 7;
    int32_t rel = shapes[shape][idx] - (mComposition.themeOffset[themeIdx] / 2);
    if (phrasePos == 14 || phrasePos == 15) rel = 0;
    return chordRoot + clampInt32(rel, -6, 12);
}

int32_t MusicEngine::bassDegreeForStep(int32_t phrasePos, int32_t chordRoot, int32_t nextChordRoot) const {
    phrasePos &= (kPhraseSteps - 1);
    int32_t rel = mComposition.bassRel[phrasePos];
    if (phrasePos == 15) {
        if (nextChordRoot > chordRoot) rel = std::max(-1, nextChordRoot - chordRoot - 1);
        else if (nextChordRoot < chordRoot) rel = std::min(1, nextChordRoot - chordRoot + 1);
    }
    return chordRoot + rel;
}

void MusicEngine::clearVoicesAndEvents() {
    for (auto& v : mDrums) v = DrumVoice{};
    for (auto& v : mBass) v = BassVoice{};
    for (auto& v : mPads) v = PadVoice{};
    for (auto& v : mLeads) v = LeadVoice{};
    for (auto& e : mEvents) e = ScheduledEvent{};
}

void MusicEngine::beginTransition() {
    if (mTransitionStage != TransitionStage::None) return;
    storeMemory();
    mPendingSongSeed = mRng.nextU32();
    mPendingStyle = randomDifferentStyle(mPattern.style);
    const StyleProfile p = profile(mPattern.style);
    const float fadeSeconds = 0.35f + mRng.uni() * (p.ambient ? 2.8f : 1.25f);
    mTransitionSamplesTotal = std::max(1, static_cast<int32_t>(fadeSeconds * static_cast<float>(mSampleRate)));
    mTransitionSamplesLeft = mTransitionSamplesTotal;
    const float silence = p.transitionSilence * (0.20f + 1.35f * mRng.uni());
    mDeadAirSamples = static_cast<int32_t>(silence * static_cast<float>(mSampleRate));
    mTransitionStage = TransitionStage::FadeOut;
}

void MusicEngine::switchToPendingStyle() {
    clearVoicesAndEvents();
    mStepIndex = -1;
    mSamplesUntilNextStep = 0.0;
    mStyleAgeSteps = 0;
    mLeadRunSteps = 0;
    mPhraseSeed = mRng.rangeInt(0, 4095);
    mLastKickStep = -1000;
    mLastSnareStep = -1000;
    mLastBassStep = -1000;
    mLastLeadStep = -1000;
    mSilentSteps = 0;
    mAgcRms = 0.010f;
    mAgcGain = 1.0f;
    mSidechain = 1.0f;
    mNovelty = 0.0f;
    mTextureLp = 0.0f;
    mTextureHp = 0.0f;
    mTexturePhaseA = 0.0f;
    mTexturePhaseB = 0.0f;
    mTextureNoise = mRng.nextU32();
    mDcInL = mDcInR = mDcOutL = mDcOutR = 0.0f;
    if (!mDelayL.empty()) std::fill(mDelayL.begin(), mDelayL.end(), 0.0f);
    if (!mDelayR.empty()) std::fill(mDelayR.begin(), mDelayR.end(), 0.0f);
    mDelayWrite = 0;
    generateSeededSong(mPendingSongSeed);
    for (auto& slot : mMemory) slot = mPattern;
    mMemoryWrite = 0;
}

void MusicEngine::updateTransition() {
    if (mTransitionStage == TransitionStage::None) {
        mTransitionGain = 1.0f;
        return;
    }

    if (mTransitionStage == TransitionStage::FadeOut) {
        const float x = static_cast<float>(mTransitionSamplesLeft) / static_cast<float>(std::max(1, mTransitionSamplesTotal));
        mTransitionGain = x * x;
        --mTransitionSamplesLeft;
        if (mTransitionSamplesLeft <= 0) {
            switchToPendingStyle();
            if (mDeadAirSamples > 0) {
                mTransitionStage = TransitionStage::DeadAir;
                mTransitionSamplesLeft = mDeadAirSamples;
                mTransitionSamplesTotal = std::max(1, mDeadAirSamples);
            } else {
                mTransitionStage = TransitionStage::FadeIn;
                mTransitionSamplesTotal = std::max(1, static_cast<int32_t>((0.15f + mRng.uni() * 0.80f) * static_cast<float>(mSampleRate)));
                mTransitionSamplesLeft = mTransitionSamplesTotal;
            }
        }
        return;
    }

    if (mTransitionStage == TransitionStage::DeadAir) {
        mTransitionGain = 0.0f;
        --mTransitionSamplesLeft;
        if (mTransitionSamplesLeft <= 0) {
            mTransitionStage = TransitionStage::FadeIn;
            mTransitionSamplesTotal = std::max(1, static_cast<int32_t>((0.12f + mRng.uni() * 0.92f) * static_cast<float>(mSampleRate)));
            mTransitionSamplesLeft = mTransitionSamplesTotal;
        }
        return;
    }

    if (mTransitionStage == TransitionStage::FadeIn) {
        const float x = 1.0f - static_cast<float>(mTransitionSamplesLeft) / static_cast<float>(std::max(1, mTransitionSamplesTotal));
        mTransitionGain = clamp01(x * x * (3.0f - 2.0f * x));
        --mTransitionSamplesLeft;
        if (mTransitionSamplesLeft <= 0) {
            mTransitionStage = TransitionStage::None;
            mTransitionGain = 1.0f;
        }
    }
}

void MusicEngine::render(float* output, int32_t frames, int32_t channelCount) {
    if (!output || frames <= 0 || channelCount <= 0) return;
    if (!mPrepared) prepare(mSampleRate);

    for (int32_t frame = 0; frame < frames; ++frame) {
        if (mSamplesUntilNextStep <= 0.0) {
            onStep();
            mSamplesUntilNextStep += stepDurationSamples();
        }
        mSamplesUntilNextStep -= 1.0;

        updateTransition();
        processEvents();

        float left = 0.0f;
        float right = 0.0f;

        for (auto& v : mDrums) {
            if (!v.active) continue;
            const float s = renderDrum(v);
            left += s * v.panL;
            right += s * v.panR;
        }
        for (auto& v : mBass) {
            if (!v.active) continue;
            const float s = renderBass(v);
            left += s * v.panL;
            right += s * v.panR;
        }
        for (auto& v : mPads) {
            if (!v.active) continue;
            const float s = renderPad(v);
            left += s * v.panL;
            right += s * v.panR;
        }
        for (auto& v : mLeads) {
            if (!v.active) continue;
            const float s = renderLead(v);
            left += s * v.panL;
            right += s * v.panR;
        }

        const float tex = renderTexture();
        left += tex * 0.75f;
        right += tex * 0.75f;

        mSidechain += (1.0f - mSidechain) * 0.0016f;
        const float duck = 0.72f + 0.28f * mSidechain;
        left *= duck;
        right *= duck;

        left *= mTransitionGain;
        right *= mTransitionGain;
        applyDelayAndMaster(left, right);

        const int32_t base = frame * channelCount;
        if (channelCount == 1) {
            output[base] = 0.5f * (left + right);
        } else {
            output[base] = left;
            output[base + 1] = right;
            for (int32_t ch = 2; ch < channelCount; ++ch) output[base + ch] = 0.0f;
        }
    }
    mCurrentPieceSamples.fetch_add(static_cast<int64_t>(frames), std::memory_order_acq_rel);
}

void MusicEngine::onStep() {
    ++mStepIndex;
    ++mStyleAgeSteps;
    ++mSilentSteps;
    mBpm += (mBpmTarget - mBpm) * 0.0012f;
    mNovelty = clamp01(mNovelty + 0.00042f);

    if (mTransitionStage == TransitionStage::DeadAir) return;

    const StyleProfile p = profile(mPattern.style);
    const int32_t pieceStep = std::max(0, mStyleAgeSteps - 1);
    const int32_t pos = static_cast<int32_t>(mStepIndex & (kPatternSteps - 1));
    const int32_t p16 = pieceStep & (kPhraseSteps - 1);
    const bool downbeat = p16 == 0;
    const bool backbeat = p.halfTime ? (p16 == 8) : (p16 == 4 || p16 == 12);
    const bool offEighth = (p16 == 2 || p16 == 6 || p16 == 10 || p16 == 14);
    const float accent = 0.55f + 0.55f * mPattern.accent[pos];
    const float stepSamples = static_cast<float>(stepDurationSamples());
    const int32_t chordRoot = currentChordRoot(pieceStep);
    const int32_t nextChordRoot = currentChordRoot(pieceStep + kPhraseSteps);
    const SectionType section = currentSectionType(pieceStep);
    const PhraseType phrase = currentPhraseType(pieceStep);

    if (!mInfinitePieceLength && !mExportSinglePieceMode) {
        const int32_t remainingSteps = mComposition.pieceSteps - pieceStep;
        if (!mComposition.conclusiveOutro && remainingSteps <= mComposition.outroFadeSteps) {
            beginTransition();
        } else if (pieceStep >= mComposition.pieceSteps &&
                   (p16 == 15 || p16 == 0 || mRng.chance(0.040f + 0.040f * mNovelty))) {
            beginTransition();
        }
    }

    if ((p16 == 0) && mRng.chance(0.18f + 0.16f * p.rough)) {
        mutateDrumsOnly();
    }
    if (mRng.chance(0.0045f + 0.0060f * p.rough + 0.0040f * mNovelty)) {
        mutateDrumsOnly();
    }

    bool eventHappened = false;

    float phraseDrumScale = 1.0f;
    float phraseBassScale = 1.0f;
    float phraseLeadScale = 1.0f;
    float phraseChordScale = 1.0f;
    switch (phrase) {
        case PhraseType::Suspension:
            phraseDrumScale = p.ambient ? 0.76f : clamp(0.88f - 0.36f * mComposition.deviceDepth, 0.38f, 0.88f);
            phraseBassScale = clamp(0.92f - 0.22f * mComposition.deviceDepth, 0.48f, 0.92f);
            phraseLeadScale = clamp(0.92f - 0.16f * mComposition.deviceDepth, 0.58f, 0.92f);
            phraseChordScale = 1.08f + 0.34f * mComposition.deviceDepth;
            break;
        case PhraseType::Hook:
            phraseDrumScale = 1.02f + 0.08f * mComposition.drama;
            phraseBassScale = 1.02f + 0.06f * mComposition.drama;
            phraseLeadScale = 1.14f + 0.36f * mComposition.hookEmphasis;
            phraseChordScale = 0.98f + 0.10f * mComposition.drama;
            break;
        case PhraseType::Mirror:
            phraseDrumScale = 0.96f;
            phraseBassScale = 1.04f;
            phraseLeadScale = 1.08f;
            phraseChordScale = 1.06f;
            break;
        case PhraseType::Orbit:
            phraseDrumScale = 0.86f;
            phraseBassScale = 1.08f;
            phraseLeadScale = 0.94f;
            phraseChordScale = 1.18f;
            break;
        case PhraseType::Cascade:
            phraseDrumScale = 1.04f + 0.12f * mComposition.drama;
            phraseBassScale = 1.02f + 0.08f * mComposition.drama;
            phraseLeadScale = 1.16f + 0.20f * mComposition.surgeLift;
            phraseChordScale = 0.94f + 0.08f * mComposition.drama;
            break;
        case PhraseType::Surge:
            phraseDrumScale = 1.06f + 0.20f * mComposition.surgeLift;
            phraseBassScale = 1.04f + 0.16f * mComposition.surgeLift;
            phraseLeadScale = 1.08f + 0.28f * mComposition.surgeLift;
            phraseChordScale = 0.88f + 0.08f * mComposition.drama;
            break;
        case PhraseType::Answer:
            phraseLeadScale = 0.98f;
            phraseBassScale = 0.92f;
            break;
        case PhraseType::Variation:
            phraseLeadScale = 1.08f;
            phraseBassScale = 1.04f;
            break;
        case PhraseType::Crystallize:
            phraseDrumScale = 0.88f;
            phraseBassScale = 0.96f;
            phraseLeadScale = 1.16f;
            phraseChordScale = 1.10f;
            break;
        case PhraseType::Eclipse:
            phraseDrumScale = 0.62f;
            phraseBassScale = 0.78f;
            phraseLeadScale = 0.68f;
            phraseChordScale = 1.24f;
            break;
        case PhraseType::Afterimage:
            phraseDrumScale = 0.78f;
            phraseBassScale = 0.86f;
            phraseLeadScale = 0.82f;
            phraseChordScale = 1.18f;
            break;
        default:
            break;
    }

    switch (section) {
        case SectionType::Intro: {
            const float ramp = clamp01(static_cast<float>(pieceStep) / static_cast<float>(std::max(1, kPhraseSteps * 4)));
            phraseDrumScale *= 0.76f + 0.24f * ramp;
            phraseBassScale *= 0.82f + 0.18f * ramp;
            phraseLeadScale *= 0.86f + 0.14f * ramp;
            phraseChordScale *= 1.08f;
            break;
        }
        case SectionType::Hook:
            phraseDrumScale *= 1.03f;
            phraseBassScale *= 1.04f;
            phraseLeadScale *= 1.18f;
            break;
        case SectionType::Variation:
            phraseDrumScale *= 0.96f;
            phraseBassScale *= 1.00f;
            phraseLeadScale *= 1.06f;
            phraseChordScale *= 1.03f;
            break;
        case SectionType::Suspension:
            phraseDrumScale *= clamp(0.95f - 0.30f * mComposition.deviceDepth, 0.42f, 0.95f);
            phraseBassScale *= clamp(0.96f - 0.24f * mComposition.deviceDepth, 0.50f, 0.96f);
            phraseLeadScale *= clamp(1.02f - 0.16f * mComposition.deviceDepth, 0.66f, 1.02f);
            phraseChordScale *= 1.08f + 0.26f * mComposition.deviceDepth;
            break;
        case SectionType::Mirror:
            phraseDrumScale *= 0.94f;
            phraseBassScale *= 1.06f;
            phraseLeadScale *= 1.10f;
            phraseChordScale *= 1.04f;
            break;
        case SectionType::Orbit:
            phraseDrumScale *= 0.86f;
            phraseBassScale *= 1.10f;
            phraseLeadScale *= 0.96f;
            phraseChordScale *= 1.16f;
            break;
        case SectionType::Cascade:
            phraseDrumScale *= 1.06f + 0.08f * mComposition.drama;
            phraseBassScale *= 1.04f;
            phraseLeadScale *= 1.14f + 0.16f * mComposition.surgeLift;
            break;
        case SectionType::Surge:
            phraseDrumScale *= 1.04f + 0.12f * mComposition.surgeLift;
            phraseBassScale *= 1.04f + 0.10f * mComposition.surgeLift;
            phraseLeadScale *= 1.10f + 0.20f * mComposition.surgeLift;
            break;
        case SectionType::Outro: {
            const int32_t remaining = std::max(0, mComposition.pieceSteps - pieceStep);
            const float ramp = clamp01(static_cast<float>(remaining) / static_cast<float>(std::max(1, kPhraseSteps * 4)));
            if (mComposition.conclusiveOutro) {
                const float finality = 1.0f - ramp;
                phraseDrumScale *= clamp(0.88f - 0.46f * finality, 0.30f, 0.94f);
                phraseBassScale *= 0.92f + 0.28f * finality;
                phraseLeadScale *= 0.88f + 0.20f * finality;
                phraseChordScale *= 1.10f + 0.48f * finality;
            } else {
                phraseDrumScale *= 0.54f + 0.46f * ramp;
                phraseBassScale *= 0.62f + 0.38f * ramp;
                phraseLeadScale *= 0.58f + 0.42f * ramp;
                phraseChordScale *= 0.70f + 0.44f * ramp;
            }
            break;
        }
        case SectionType::Theme:
        default:
            break;
    }

    const int32_t phraseNumber = pieceStep / kPhraseSteps;
    const bool prismLatch = (section == SectionType::Suspension && mComposition.drama > 0.68f && (phraseNumber & 3) == 3 && p16 >= 12);
    const bool gravityFold = ((section == SectionType::Orbit || section == SectionType::Suspension) && mComposition.drama > 0.76f && ((phraseNumber + static_cast<int32_t>(mComposition.paletteHash & 3u)) & 7) == 5 && p16 < 12);
    const bool mirrorGate = (section == SectionType::Mirror && mComposition.drama > 0.58f && (p16 == 4 || p16 == 12));
    const bool cascadeSpray = (section == SectionType::Cascade && (p16 >= 10 || p16 == 3));
    if (prismLatch) {
        phraseDrumScale *= 0.34f;
        phraseBassScale *= 0.54f;
        phraseLeadScale *= 0.78f;
        phraseChordScale *= 1.24f;
    }
    if (gravityFold) {
        phraseDrumScale *= 0.24f;
        phraseBassScale *= 1.10f;
        phraseLeadScale *= 0.74f;
        phraseChordScale *= 1.38f;
    }
    if (mirrorGate) {
        phraseDrumScale *= 0.70f;
        phraseLeadScale *= 1.26f;
        phraseChordScale *= 1.08f;
    }
    if (cascadeSpray) {
        phraseDrumScale *= 1.08f;
        phraseLeadScale *= 1.20f;
    }

    float kickP = mPattern.kick[pos] * (0.52f + 0.70f * p.drum) * (0.72f + 0.42f * mPattern.energy) * phraseDrumScale * mComposition.useKick;
    const bool forceKick = downbeat && !p.ambient && mComposition.useKick > 0.0f;
    if (forceKick) kickP = std::max(kickP, 0.72f * phraseDrumScale);
    if (forceKick || mRng.chance(clamp01(kickP))) {
        const float amp = (0.44f + 0.34f * mPattern.energy) * accent * phraseDrumScale;
        scheduleDrum(humanizeSamples(0.12f, downbeat, false), DrumType::Kick, amp, 0.0f, 0.52f + 0.22f * mPattern.roughness, clamp01(0.18f * mPattern.roughness + 0.82f * mComposition.kickTone));
        mLastKickStep = static_cast<int32_t>(mStepIndex);
        eventHappened = true;
    }

    float snareP = mPattern.snare[pos] * (0.48f + 0.62f * p.drum) * phraseDrumScale * mComposition.useSnare;
    if (backbeat && !p.ambient && mComposition.useSnare > 0.0f) snareP = std::max(snareP, 0.68f * phraseDrumScale);
    if (mRng.chance(clamp01(snareP))) {
        DrumType sn = (mRng.chance(0.22f + 0.20f * mPattern.roughness) && !p.breakbeat) ? DrumType::Clap : DrumType::Snare;
        const float amp = (0.30f + 0.30f * mPattern.energy) * accent * phraseDrumScale;
        scheduleDrum(humanizeSamples(0.50f, false, backbeat), sn, amp, mRng.bipolar() * 0.08f, 0.17f + 0.10f * mRng.uni(), clamp01(0.16f * mPattern.roughness + 0.84f * mComposition.snareTone));
        if (mRng.chance(0.12f + 0.12f * mPattern.roughness) && backbeat && phrase != PhraseType::Suspension) {
            scheduleDrum(static_cast<int32_t>(0.010f * mSampleRate + mRng.uni() * 0.014f * mSampleRate), DrumType::Clap,
                         amp * 0.42f, mRng.bipolar() * 0.22f, 0.18f, mComposition.snareTone);
        }
        mLastSnareStep = static_cast<int32_t>(mStepIndex);
        eventHappened = true;
    }

    float hatP = mPattern.hat[pos] * (0.40f + 0.64f * p.density) * (1.0f - 0.35f * mPattern.space) * phraseDrumScale * mComposition.useHat;
    if (p.trapHats && (pos & 1) == 0) hatP += 0.18f * phraseDrumScale * mComposition.useHat;
    if (mRng.chance(clamp01(hatP))) {
        const float amp = (0.10f + 0.16f * mPattern.energy) * (0.70f + 0.50f * accent) * phraseDrumScale;
        scheduleDrum(humanizeSamples(0.85f, false, false), DrumType::HatClosed,
                     amp, mRng.bipolar() * 0.55f, 0.020f + mRng.uni() * 0.060f, clamp01(0.20f * mPattern.roughness + 0.80f * mComposition.hatTone));
        eventHappened = true;

        if (p.trapHats && !downbeat && phrase != PhraseType::Suspension && mRng.chance(p.hatRoll * (0.12f + 0.65f * mPattern.density))) {
            const int32_t rolls = mRng.chance(0.62f) ? 2 : (mRng.chance(0.50f) ? 3 : 4);
            for (int32_t r = 1; r < rolls; ++r) {
                const float frac = static_cast<float>(r) / static_cast<float>(rolls);
                scheduleDrum(static_cast<int32_t>(stepSamples * frac), DrumType::HatClosed,
                             amp * (0.48f + 0.11f * r), mRng.bipolar() * 0.64f,
                             0.016f + mRng.uni() * 0.026f, clamp01(0.50f + 0.50f * mComposition.hatTone));
            }
        }
    }

    if (mRng.chance(clamp01((mPattern.openHat[pos] * (0.34f + 0.70f * p.drum) * phraseDrumScale + (offEighth ? 0.016f : 0.0f)) * mComposition.useOpenHat))) {
        scheduleDrum(humanizeSamples(0.52f, false, false), DrumType::HatOpen,
                     (0.08f + 0.14f * mPattern.energy) * accent * phraseDrumScale, mRng.bipolar() * 0.50f,
                     0.10f + mRng.uni() * 0.19f, mComposition.hatTone);
        eventHappened = true;
    }

    float percP = mPattern.perc[pos] * (0.25f + 0.85f * mPattern.syncopation) * (0.50f + 0.70f * p.drum) * phraseDrumScale * mComposition.usePerc;
    if (p.breakbeat && (p16 == 1 || p16 == 7 || p16 == 14)) percP += 0.05f * phraseDrumScale;
    if (p.ambient) percP *= 0.55f;
    if (!backbeat && mRng.chance(clamp01(percP))) {
        DrumType type = DrumType::Perc;
        const float r = mRng.uni();
        if (r < 0.16f) type = DrumType::Rim;
        else if (r < 0.30f) type = DrumType::Tom;
        else if (r > 0.88f && p.rough > 0.36f) type = DrumType::Zap;
        else if (r > 0.74f && p.rough > 0.52f) type = DrumType::Noise;
        scheduleDrum(humanizeSamples(0.90f, false, false), type,
                     (0.075f + 0.19f * mPattern.energy) * accent * phraseDrumScale,
                     mRng.bipolar() * 0.72f, 0.045f + mRng.uni() * 0.16f, clamp01(mComposition.percTone + mRng.bipolar() * 0.08f));
        eventHappened = true;
    }

    const bool needsLevelFloor = (section == SectionType::Intro || section == SectionType::Suspension || section == SectionType::Orbit || section == SectionType::Outro);
    const bool floorPulse = needsLevelFloor && (p16 == 0 || p16 == 8);
    const bool anchorPulse = downbeat && !p.ambient;

    float bassGate = clamp01(mComposition.bassGate[p16] * mComposition.bassGain * phraseBassScale * mComposition.useBass);
    if ((floorPulse || anchorPulse) && mComposition.useBass > 0.0f) {
        bassGate = std::max(bassGate, p.ambient ? 0.30f : 0.44f);
    }
    if (bassGate > 0.02f && (floorPulse || anchorPulse || mRng.chance(clamp01(bassGate * (0.84f + 0.14f * accent))))) {
        const int32_t degree = bassDegreeForStep(p16, chordRoot, nextChordRoot);
        const int32_t octave = (mPattern.style == StyleType::GlassNoir || mPattern.style == StyleType::SubOrbit || mPattern.style == StyleType::DeepMagnet || mPattern.style == StyleType::MarbleBass) ? -1 : 0;
        const float freq = midiToHz(static_cast<float>(scaleDegreeToMidi(degree, octave)));
        float dur = stepDurationSeconds() * (p.halfTime ? 2.8f : 1.5f);
        if (downbeat) dur *= p.ambient ? 5.0f : 1.65f;
        scheduleBass(humanizeSamples(0.18f, downbeat, false), freq,
                     (0.25f + 0.27f * mPattern.energy) * phraseBassScale,
                     dur, mRng.bipolar() * 0.045f, clamp01(mComposition.bassTone + mRng.bipolar() * 0.045f + p.rough * 0.04f));
        mLastBassStep = static_cast<int32_t>(mStepIndex);
        eventHappened = true;
    }

    float chordGate = clamp01(mComposition.chordGate[p16] * mComposition.chordGain * phraseChordScale * mComposition.useChord);
    if (floorPulse && mComposition.useChord > 0.0f) {
        chordGate = std::max(chordGate, p.ambient ? 0.66f : 0.48f);
    } else if (anchorPulse && mComposition.useChord > 0.0f) {
        chordGate = std::max(chordGate, 0.16f);
    }
    if (chordGate > 0.02f && (floorPulse || mRng.chance(chordGate))) {
        const float dur = stepDurationSeconds() * (p.ambient ? 10.0f : (p.fourOnFloor ? 3.2f : 5.6f));
        const float amp = (0.050f + 0.12f * mPattern.texture + 0.08f * p.chord) * accent * phraseChordScale;
        schedulePad(humanizeSamples(0.28f, downbeat, false), chordRoot, amp, dur, mRng.bipolar() * 0.42f, clamp01(mComposition.padTone + mRng.bipolar() * 0.04f));
        eventHappened = true;
    }

    bool leadRest = false;
    float leadGate = 0.0f;
    float leadDurSteps = 0.0f;
    int32_t leadDegree = grammarDegree(phrase, p16, chordRoot, leadRest, leadGate, leadDurSteps);
    leadDegree = applyThemeTransform(leadDegree, pieceStep, p16, chordRoot, phrase);
    leadGate = clamp01(leadGate * phraseLeadScale * (1.0f - 0.18f * mComposition.leadSpace) * mComposition.useLead);
    if (!leadRest && leadGate > 0.04f && mRng.chance(clamp01(0.72f + 0.26f * leadGate))) {
        int32_t octave = mComposition.octaveBias;
        if ((phrase == PhraseType::Surge || phrase == PhraseType::Cascade || phrase == PhraseType::Hook) && mRng.chance(0.35f)) octave += 1;
        if (phrase == PhraseType::Suspension) octave = std::max(1, octave - 1);
        const float freq = midiToHz(static_cast<float>(scaleDegreeToMidi(leadDegree, octave)));
        const float dur = stepDurationSeconds() * (0.60f + leadDurSteps * (p.ambient ? 3.4f : 1.75f));
        scheduleLead(humanizeSamples(0.55f, false, false), freq,
                     (0.055f + 0.12f * mPattern.melody + 0.05f * p.melodyRun) * accent * phraseLeadScale,
                     dur, mRng.bipolar() * 0.62f, clamp01(mComposition.leadTone + ((phrase == PhraseType::Surge || phrase == PhraseType::Cascade) ? 0.08f : 0.0f) + mRng.bipolar() * 0.035f));
        if (phrase != PhraseType::Suspension && mRng.chance(mComposition.ornament * leadGate)) {
            const int32_t neighbor = leadDegree + (mRng.chance(0.50f) ? 1 : -1);
            const float f2 = midiToHz(static_cast<float>(scaleDegreeToMidi(neighbor, octave)));
            scheduleLead(static_cast<int32_t>(stepSamples * (0.45f + 0.24f * mRng.uni())), f2,
                         (0.026f + 0.050f * mPattern.melody) * accent * phraseLeadScale,
                         dur * 0.38f, mRng.bipolar() * 0.66f, clamp01(mComposition.leadTone + 0.04f + mRng.bipolar() * 0.035f));
        }
        if ((phrase == PhraseType::Hook || phrase == PhraseType::Surge || phrase == PhraseType::Cascade) && mComposition.drama > 0.55f && (p16 == 0 || p16 == 8 || p16 == 15) && mRng.chance(0.34f + 0.30f * mComposition.drama)) {
            const float f3 = midiToHz(static_cast<float>(scaleDegreeToMidi(leadDegree, octave + 1)));
            scheduleLead(static_cast<int32_t>(stepSamples * 0.18f), f3,
                         (0.018f + 0.045f * mPattern.melody) * accent * phraseLeadScale,
                         dur * 0.72f, mRng.bipolar() * 0.72f, clamp01(mComposition.leadTone + 0.10f));
        }
        mLastLeadStep = static_cast<int32_t>(mStepIndex);
        eventHappened = true;
    }

    if (mComposition.useArp > 0.02f && phrase != PhraseType::Suspension && !prismLatch) {
        const bool arpPulse = offEighth || ((p16 & 3) == 1 && (section == SectionType::Hook || section == SectionType::Surge || section == SectionType::Cascade));
        const float arpP = (0.12f + 0.46f * p.melodyRun + 0.18f * mPattern.density) * phraseLeadScale * mComposition.useArp;
        if (arpPulse && mRng.chance(clamp01(arpP))) {
            static constexpr int32_t arpShape[8] = {0, 2, 4, 6, 4, 2, 7, 4};
            const int32_t arpIndex = (p16 / 2 + phraseNumber + static_cast<int32_t>(mComposition.paletteHash & 3u)) & 7;
            const int32_t degree = chordRoot + arpShape[arpIndex];
            const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(degree, mComposition.octaveBias + 1)));
            scheduleLead(humanizeSamples(0.42f, false, false), f,
                         (0.020f + 0.056f * p.brightness + 0.020f * mPattern.melody) * accent * phraseLeadScale * mComposition.useArp,
                         stepDurationSeconds() * (0.34f + 0.18f * mRng.uni()),
                         mRng.bipolar() * 0.78f, clamp01(mComposition.arpTone + mRng.bipolar() * 0.030f));
            eventHappened = true;
        }
    }

    if (mComposition.useCounter > 0.02f && phrase != PhraseType::Suspension && (p16 == 6 || p16 == 10 || p16 == 14 || ((section == SectionType::Variation || section == SectionType::Mirror) && p16 == 3))) {
        bool rest2 = false;
        float gate2 = 0.0f;
        float dur2 = 0.0f;
        int32_t deg2 = grammarDegree(PhraseType::Answer, (p16 + 8) & 15, chordRoot, rest2, gate2, dur2);
        if (mRng.chance(0.68f * mComposition.counterpoint)) deg2 = counterpointDegree(pieceStep, p16, chordRoot);
        else deg2 = applyThemeTransform(deg2, pieceStep, p16, chordRoot, PhraseType::Answer);
        const float counterP = clamp01(gate2 * (0.38f + 0.50f * mComposition.useCounter + 0.18f * mComposition.callResponse) * phraseLeadScale);
        if (!rest2 && mRng.chance(counterP)) {
            const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(deg2, std::max(1, mComposition.octaveBias - 1))));
            scheduleLead(humanizeSamples(0.62f, false, false), f,
                         (0.020f + 0.052f * mPattern.melody) * accent * phraseLeadScale * mComposition.useCounter,
                         stepDurationSeconds() * (0.52f + 0.80f * dur2),
                         mRng.bipolar() * 0.70f, clamp01(mComposition.counterTone + mRng.bipolar() * 0.034f));
            eventHappened = true;
        }
    }

    if (mComposition.useStab > 0.02f && mComposition.useChord > 0.0f && phrase != PhraseType::Suspension &&
        (p16 == 2 || p16 == 6 || p16 == 10 || p16 == 14) && mRng.chance(clamp01((0.12f + 0.34f * p.chord) * mComposition.useStab * phraseChordScale))) {
        schedulePad(humanizeSamples(0.46f, false, false), chordRoot + ((p16 >= 8) ? 2 : 0),
                    (0.028f + 0.060f * p.chord) * accent * phraseChordScale * mComposition.useStab,
                    stepDurationSeconds() * (1.2f + 0.9f * mRng.uni()), mRng.bipolar() * 0.62f,
                    clamp01(mComposition.stabTone + mRng.bipolar() * 0.035f));
        eventHappened = true;
    }

    if (mComposition.useDrone > 0.02f && downbeat && ((phraseNumber % std::max(2, mComposition.sectionPhraseLength)) == 0) &&
        mRng.chance(clamp01(0.22f + 0.34f * p.texture + 0.18f * mComposition.useDrone))) {
        schedulePad(0, chordRoot, (0.024f + 0.060f * p.texture) * phraseChordScale * mComposition.useDrone,
                    stepDurationSeconds() * (p.ambient ? 14.0f : 8.0f), mRng.bipolar() * 0.36f,
                    clamp01(mComposition.droneTone + mRng.bipolar() * 0.020f));
        eventHappened = true;
    }

    if (mComposition.useSpark > 0.02f && phrase != PhraseType::Suspension && (p16 == 3 || p16 == 7 || p16 == 11 || p16 == 15) &&
        mRng.chance(clamp01((0.05f + 0.18f * p.brightness + 0.12f * p.sync) * mComposition.useSpark * phraseLeadScale))) {
        const int32_t degree = chordRoot + ((p16 == 15) ? 7 : (p16 & 7));
        const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(degree, mComposition.octaveBias + 1)));
        scheduleLead(static_cast<int32_t>(stepSamples * (0.10f + 0.34f * mRng.uni())), f,
                     (0.012f + 0.038f * p.brightness) * accent * phraseLeadScale * mComposition.useSpark,
                     stepDurationSeconds() * (0.18f + 0.22f * mRng.uni()),
                     mRng.bipolar() * 0.86f, clamp01(mComposition.sparkTone + mRng.bipolar() * 0.040f));
        eventHappened = true;
    }

    if (mComposition.useSub > 0.02f && (downbeat || p16 == 8 || (section == SectionType::Orbit && p16 == 4)) &&
        mRng.chance(clamp01((0.24f + 0.28f * p.bass) * mComposition.useSub * phraseBassScale))) {
        const int32_t degree = chordRoot + ((p16 >= 8) ? 4 : 0);
        const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(degree, -1)));
        scheduleBass(humanizeSamples(0.10f, downbeat, false), f,
                     (0.090f + 0.090f * p.bass) * phraseBassScale * mComposition.useSub,
                     stepDurationSeconds() * (downbeat ? 3.6f : 2.0f), mRng.bipolar() * 0.035f,
                     clamp01(mComposition.subTone + 0.02f * mRng.bipolar()));
        eventHappened = true;
    }

    if (mComposition.useEcho > 0.02f && phrase != PhraseType::Suspension && (p16 == 2 || p16 == 6 || p16 == 10 || p16 == 14) &&
        mRng.chance(clamp01((0.10f + 0.34f * p.melodyRun) * mComposition.useEcho * phraseLeadScale))) {
        bool er = false; float eg = 0.0f; float ed = 0.0f;
        const int32_t edeg = grammarDegree(PhraseType::Answer, (p16 + 4) & 15, chordRoot, er, eg, ed);
        if (!er && eg > 0.02f) {
            const int32_t themeEcho = applyThemeTransform(edeg, pieceStep, p16, chordRoot, PhraseType::Answer);
            const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(themeEcho, mComposition.octaveBias + 1)));
            scheduleLead(static_cast<int32_t>(stepSamples * (0.52f + 0.18f * mRng.uni())), f,
                         (0.016f + 0.046f * p.melody) * phraseLeadScale * mComposition.useEcho,
                         stepDurationSeconds() * (0.34f + 0.52f * ed), mRng.bipolar() * 0.84f,
                         clamp01(mComposition.echoTone + 0.04f * mRng.bipolar()));
            eventHappened = true;
        }
    }

    if (mComposition.useOrbit > 0.02f && (section == SectionType::Orbit || p16 == 0 || p16 == 5 || p16 == 10 || p16 == 15) &&
        mRng.chance(clamp01((0.08f + 0.26f * p.texture + 0.16f * p.melody) * mComposition.useOrbit))) {
        static constexpr int32_t orbitShape[8] = {0, 2, 4, 2, 0, -1, 2, 4};
        const int32_t oi = (phraseNumber + p16 / 2 + static_cast<int32_t>(mComposition.paletteHash & 7u)) & 7;
        const int32_t deg = chordRoot + orbitShape[oi];
        const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(deg, mComposition.octaveBias)));
        scheduleLead(humanizeSamples(0.36f, false, false), f,
                     (0.015f + 0.042f * mPattern.melody) * phraseLeadScale * mComposition.useOrbit,
                     stepDurationSeconds() * (section == SectionType::Orbit ? 1.1f : 0.52f), mRng.bipolar() * 0.72f,
                     clamp01(mComposition.orbitTone + mRng.bipolar() * 0.035f));
        eventHappened = true;
    }

    if (mComposition.useBloom > 0.02f && mComposition.useChord > 0.0f && (downbeat || section == SectionType::Mirror || section == SectionType::Surge) &&
        mRng.chance(clamp01((0.10f + 0.30f * p.chord + 0.14f * p.texture) * mComposition.useBloom * phraseChordScale))) {
        schedulePad(humanizeSamples(0.18f, downbeat, false), chordRoot + ((section == SectionType::Mirror) ? 2 : 0),
                    (0.024f + 0.070f * p.chord) * phraseChordScale * mComposition.useBloom,
                    stepDurationSeconds() * (p.ambient ? 12.0f : 6.0f), mRng.bipolar() * 0.52f,
                    clamp01(mComposition.bloomTone + mRng.bipolar() * 0.030f));
        eventHappened = true;
    }

    if (mComposition.useGlyph > 0.02f && (p16 == 1 || p16 == 5 || p16 == 9 || p16 == 13 || section == SectionType::Cascade) &&
        mRng.chance(clamp01((0.045f + 0.16f * p.rough + 0.12f * p.sync) * mComposition.useGlyph))) {
        if (mRng.chance(0.58f)) {
            scheduleDrum(static_cast<int32_t>(stepSamples * (0.15f + 0.60f * mRng.uni())),
                         mRng.chance(0.50f) ? DrumType::Zap : DrumType::Rim,
                         0.022f + 0.070f * mPattern.roughness, mRng.bipolar() * 0.86f,
                         0.025f + 0.070f * mRng.uni(), clamp01(mComposition.glyphTone + mRng.bipolar() * 0.060f));
        } else {
            const int32_t deg = chordRoot + 7 + ((p16 / 4) & 1);
            const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(deg, mComposition.octaveBias + 1)));
            scheduleLead(static_cast<int32_t>(stepSamples * (0.20f + 0.48f * mRng.uni())), f,
                         0.012f + 0.036f * mComposition.useGlyph,
                         stepDurationSeconds() * (0.14f + 0.18f * mRng.uni()), mRng.bipolar() * 0.90f,
                         clamp01(mComposition.glyphTone + mRng.bipolar() * 0.050f));
        }
        eventHappened = true;
    }

    if (mComposition.useSheen > 0.02f && phrase != PhraseType::Suspension && (p16 == 3 || p16 == 7 || p16 == 11 || p16 == 15) &&
        mRng.chance(clamp01((0.05f + 0.20f * p.brightness + 0.16f * p.melodyRun) * mComposition.useSheen * phraseLeadScale))) {
        const int32_t deg = chordRoot + ((p16 == 15) ? 9 : 6 + (p16 & 3));
        const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(deg, mComposition.octaveBias + 2)));
        scheduleLead(static_cast<int32_t>(stepSamples * (0.06f + 0.30f * mRng.uni())), f,
                     (0.010f + 0.032f * p.brightness) * phraseLeadScale * mComposition.useSheen,
                     stepDurationSeconds() * (0.12f + 0.16f * mRng.uni()), mRng.bipolar() * 0.94f,
                     clamp01(mComposition.sheenTone + mRng.bipolar() * 0.040f));
        eventHappened = true;
    }

    if (mComposition.usePluck > 0.02f && phrase != PhraseType::Suspension &&
        (p16 == 0 || p16 == 3 || p16 == 6 || p16 == 10 || p16 == 13) &&
        mRng.chance(clamp01((0.09f + 0.24f * p.melodyRun + 0.10f * p.sync) * mComposition.usePluck * phraseLeadScale))) {
        bool pr = false; float pg = 0.0f; float pd = 0.0f;
        int32_t deg = grammarDegree(phrase, p16, chordRoot, pr, pg, pd);
        deg = applyThemeTransform(deg, pieceStep, p16, chordRoot, phrase);
        if (!pr) {
            const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(deg, mComposition.octaveBias + (mRng.chance(0.35f) ? 1 : 0))));
            scheduleLead(humanizeSamples(0.30f, false, false), f,
                         (0.018f + 0.050f * mPattern.melody) * accent * phraseLeadScale * mComposition.usePluck,
                         stepDurationSeconds() * (0.20f + 0.34f * pd), mRng.bipolar() * 0.76f,
                         clamp01(mComposition.pluckTone + 0.04f * mRng.bipolar()));
            eventHappened = true;
        }
    }

    if (mComposition.useBell > 0.02f && (p16 == 7 || p16 == 15 || (section == SectionType::Hook && (p16 == 0 || p16 == 8))) &&
        mRng.chance(clamp01((0.06f + 0.22f * p.brightness + 0.16f * mComposition.longMemory) * mComposition.useBell * phraseLeadScale))) {
        bool br = false; float bg = 0.0f; float bd = 0.0f;
        int32_t deg = grammarDegree((section == SectionType::Hook) ? PhraseType::Hook : PhraseType::Answer, p16, chordRoot, br, bg, bd);
        deg = applyThemeTransform(deg, pieceStep, p16, chordRoot, phrase);
        const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(deg + (p16 == 15 ? 0 : 2), mComposition.octaveBias + 2)));
        scheduleLead(static_cast<int32_t>(stepSamples * (0.04f + 0.12f * mRng.uni())), f,
                     (0.010f + 0.038f * p.brightness) * phraseLeadScale * mComposition.useBell,
                     stepDurationSeconds() * (0.42f + 0.72f * bd), mRng.bipolar() * 0.82f,
                     clamp01(mComposition.bellTone + 0.05f * mRng.bipolar()));
        eventHappened = true;
    }

    // v18.1: theme braid.  A low-mid answer and a high afterimage can appear
    // around strong phrase joints, giving the lead line a conversational frame
    // without adding heavy DSP or extra instrument classes.
    if (phrase != PhraseType::Suspension && (p16 == 4 || p16 == 12) &&
        mComposition.useCounter > 0.02f && mComposition.useLead > 0.02f &&
        mRng.chance(clamp01((0.055f + 0.18f * mComposition.callResponse + 0.10f * p.melody) * phraseLeadScale))) {
        const int32_t braid = counterpointDegree(pieceStep, p16, chordRoot);
        const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(braid, std::max(1, mComposition.octaveBias - 1))));
        scheduleLead(static_cast<int32_t>(stepSamples * (0.28f + 0.10f * mRng.uni())), f,
                     (0.014f + 0.042f * mPattern.melody) * accent * phraseLeadScale * mComposition.useCounter,
                     stepDurationSeconds() * (0.78f + 0.72f * mComposition.longMemory),
                     mRng.bipolar() * 0.58f, clamp01(mComposition.counterTone + 0.03f * mRng.bipolar()));
        if (mComposition.useSheen > 0.02f && mRng.chance(0.36f + 0.24f * p.brightness)) {
            const float fHi = midiToHz(static_cast<float>(scaleDegreeToMidi(chordRoot + ((p16 == 12) ? 7 : 4), mComposition.octaveBias + 2)));
            scheduleLead(static_cast<int32_t>(stepSamples * 0.62f), fHi,
                         (0.006f + 0.020f * p.brightness) * phraseLeadScale * mComposition.useSheen,
                         stepDurationSeconds() * 0.86f, mRng.bipolar() * 0.92f,
                         clamp01(mComposition.sheenTone + 0.04f));
        }
        eventHappened = true;
    }

    // v18.1: bass glides sometimes answer the melody instead of merely anchoring it.
    if (mComposition.useBass > 0.02f && (p16 == 2 || p16 == 14) &&
        (section == SectionType::Variation || section == SectionType::Mirror || section == SectionType::Afterimage || phrase == PhraseType::Answer) &&
        mRng.chance(clamp01((0.055f + 0.16f * p.bass + 0.10f * mComposition.counterpoint) * phraseBassScale))) {
        const int32_t bdeg = chordRoot + ((p16 == 14) ? -1 : (mComposition.themeOffset[themeIndexForStep(pieceStep)] >= 0 ? 4 : 2));
        const float bf = midiToHz(static_cast<float>(scaleDegreeToMidi(bdeg, -1)));
        scheduleBass(static_cast<int32_t>(stepSamples * 0.38f), bf,
                     (0.050f + 0.060f * p.bass) * phraseBassScale * mComposition.useBass,
                     stepDurationSeconds() * 1.25f, mRng.bipolar() * 0.06f,
                     clamp01(mComposition.bassTone + 0.06f * mRng.bipolar()));
        eventHappened = true;
    }

    if (mComposition.usePulse > 0.02f && phrase != PhraseType::Suspension &&
        (p16 == 1 || p16 == 5 || p16 == 9 || p16 == 13) &&
        mRng.chance(clamp01((0.08f + 0.24f * p.sync + 0.12f * p.density) * mComposition.usePulse * phraseLeadScale))) {
        const int32_t themeIdx = themeIndexForStep(pieceStep);
        const int32_t deg = chordRoot + mComposition.themeOffset[themeIdx] + ((p16 >= 8) ? 2 : 0);
        const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(deg, mComposition.octaveBias)));
        scheduleLead(humanizeSamples(0.34f, false, false), f,
                     (0.012f + 0.038f * mPattern.melody) * accent * phraseLeadScale * mComposition.usePulse,
                     stepDurationSeconds() * 0.46f, mRng.bipolar() * 0.70f,
                     clamp01(mComposition.pulseTone + 0.04f * mRng.bipolar()));
        eventHappened = true;
    }

    if (mComposition.useGrain > 0.02f && (section == SectionType::Cascade || p16 == 5 || p16 == 13) &&
        mRng.chance(clamp01((0.035f + 0.16f * p.rough + 0.10f * p.texture) * mComposition.useGrain))) {
        if (mRng.chance(0.64f)) {
            scheduleDrum(static_cast<int32_t>(stepSamples * (0.12f + 0.70f * mRng.uni())),
                         mRng.chance(0.55f) ? DrumType::Noise : DrumType::Zap,
                         0.020f + 0.060f * mPattern.roughness, mRng.bipolar() * 0.96f,
                         0.020f + 0.090f * mRng.uni(), clamp01(mComposition.grainTone + 0.08f * mRng.bipolar()));
        } else {
            const int32_t deg = chordRoot + ((p16 & 3) == 1 ? 5 : -1);
            const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(deg, mComposition.octaveBias + 1)));
            scheduleLead(static_cast<int32_t>(stepSamples * (0.20f + 0.50f * mRng.uni())), f,
                         0.010f + 0.030f * mComposition.useGrain,
                         stepDurationSeconds() * (0.12f + 0.16f * mRng.uni()), mRng.bipolar() * 0.94f,
                         clamp01(mComposition.grainTone + 0.06f * mRng.bipolar()));
        }
        eventHappened = true;
    }

    if (mComposition.useComet > 0.02f && downbeat && (section == SectionType::Surge || section == SectionType::Hook || (phraseNumber % std::max(2, mComposition.recallCycle)) == 0) &&
        mRng.chance(clamp01((0.04f + 0.18f * mComposition.drama + 0.10f * p.melody) * mComposition.useComet))) {
        const int32_t themeIdx = themeIndexForStep(pieceStep);
        const int32_t deg = chordRoot + mComposition.themeOffset[themeIdx] + 7;
        const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(deg, mComposition.octaveBias + 1)));
        scheduleLead(0, f,
                     (0.012f + 0.034f * p.melody) * phraseLeadScale * mComposition.useComet,
                     stepDurationSeconds() * (2.2f + 2.6f * mComposition.longMemory),
                     mRng.bipolar() * 0.70f, clamp01(mComposition.cometTone + 0.04f));
        eventHappened = true;
    }

    if (mComposition.useRotor > 0.02f && mComposition.useChord > 0.0f && (downbeat || (section == SectionType::Orbit && (p16 == 4 || p16 == 12))) &&
        mRng.chance(clamp01((0.08f + 0.26f * p.chord + 0.16f * p.space) * mComposition.useRotor * phraseChordScale))) {
        const int32_t offset = (section == SectionType::Mirror) ? 2 : ((section == SectionType::Orbit && p16 >= 8) ? 4 : 0);
        schedulePad(humanizeSamples(0.16f, downbeat, false), chordRoot + offset,
                    (0.024f + 0.066f * p.chord) * phraseChordScale * mComposition.useRotor,
                    stepDurationSeconds() * (p.ambient ? 10.0f : 5.4f), mRng.bipolar() * 0.60f,
                    clamp01(mComposition.rotorTone + 0.03f * mRng.bipolar()));
        eventHappened = true;
    }

    if (mComposition.useFx > 0.02f && !p.ambient && (p16 == 15 || ((section == SectionType::Surge || section == SectionType::Cascade) && (p16 == 6 || p16 == 14))) &&
        mRng.chance(clamp01((0.045f + 0.18f * mComposition.drama + 0.14f * p.rough) * mComposition.useFx))) {
        scheduleDrum(static_cast<int32_t>(stepSamples * (0.20f + 0.50f * mRng.uni())),
                     mRng.chance(0.55f) ? DrumType::Zap : DrumType::Noise,
                     0.030f + 0.070f * mPattern.roughness, mRng.bipolar() * 0.82f,
                     0.035f + 0.080f * mRng.uni(), clamp01(mComposition.fxTone + mRng.bipolar() * 0.060f));
        eventHappened = true;
    }

    if ((section == SectionType::Surge || section == SectionType::Cascade) && downbeat && mComposition.drama > 0.60f && mComposition.useChord > 0.0f && mRng.chance(0.38f + 0.30f * mComposition.drama)) {
        schedulePad(0, chordRoot, (0.045f + 0.090f * mPattern.texture) * phraseChordScale,
                    stepDurationSeconds() * (p.ambient ? 9.0f : 4.4f), mRng.bipolar() * 0.34f, clamp01(mComposition.padTone + 0.08f));
        eventHappened = true;
    }

    if ((phrase == PhraseType::Surge || phrase == PhraseType::Cascade) && (p16 == 14 || p16 == 15) && mRng.chance(0.18f + 0.20f * p.rough) && !p.ambient) {
        const int32_t repeats = mRng.rangeInt(2, 4);
        for (int32_t r = 0; r < repeats; ++r) {
            scheduleDrum(static_cast<int32_t>(stepSamples * (static_cast<float>(r) / static_cast<float>(repeats))),
                         mRng.chance(0.45f) ? DrumType::Zap : DrumType::Noise,
                         0.052f + 0.075f * mPattern.roughness,
                         mRng.bipolar() * 0.80f,
                         0.025f + 0.045f * mRng.uni(), clamp01(mComposition.percTone + mRng.bipolar() * 0.10f));
        }
        eventHappened = true;
    }

    if (eventHappened) mSilentSteps = 0;
    if (mSilentSteps > (p.ambient ? 96 : 28)) {
        scheduleDrum(0, DrumType::Kick, p.ambient ? 0.20f : 0.62f, 0.0f, 0.45f, mComposition.kickTone);
        scheduleBass(0, midiToHz(static_cast<float>(scaleDegreeToMidi(chordRoot, p.ambient ? 0 : -1))),
                     p.ambient ? 0.18f : 0.36f, stepDurationSeconds() * 2.0f, 0.0f, mComposition.bassTone);
        mSilentSteps = 0;
    }
}

void MusicEngine::processEvents() {
    for (auto& event : mEvents) {
        if (!event.active) continue;
        if (event.samples <= 0) {
            switch (event.kind) {
                case EventKind::Drum:
                    triggerDrum(event.drumType, event.amp, event.pan, event.dur, event.aux);
                    break;
                case EventKind::Bass:
                    triggerBass(event.freq, event.amp, event.dur, event.pan, event.aux);
                    break;
                case EventKind::Pad:
                    triggerPad(event.degree, event.amp, event.dur, event.pan, event.aux);
                    break;
                case EventKind::Lead:
                    triggerLead(event.freq, event.amp, event.dur, event.pan, event.aux);
                    break;
            }
            event.active = false;
        } else {
            --event.samples;
        }
    }
}

double MusicEngine::stepDurationSamples() const {
    const double bpm = std::max(40.0, static_cast<double>(mBpm));
    const double base = mSampleRate * 60.0 / bpm / 4.0;
    const bool even = ((mStepIndex & 1) == 0);
    const double swing = static_cast<double>(mPattern.swing) * 0.58;
    return base * (even ? (1.0 + swing) : (1.0 - swing));
}

float MusicEngine::stepDurationSeconds() const {
    return static_cast<float>(stepDurationSamples() / mSampleRate);
}

int32_t MusicEngine::humanizeSamples(float amount, bool downbeat, bool backbeat) {
    if (downbeat) return 0;
    const float human = clamp01(amount) * mPattern.humanize;
    const float late = mRng.uni() * mRng.uni() * 0.018f * human;
    const float pocket = backbeat ? mPocketLate * (0.30f + 0.70f * mPattern.swing) : 0.0f;
    return static_cast<int32_t>((late + pocket) * static_cast<float>(mSampleRate));
}

void MusicEngine::scheduleDrum(int32_t offsetSamples, DrumType type, float amp, float pan, float dur, float aux) {
    for (auto& e : mEvents) {
        if (e.active) continue;
        e.active = true;
        e.samples = std::max(0, offsetSamples);
        e.kind = EventKind::Drum;
        e.drumType = type;
        e.amp = amp;
        e.pan = clamp(pan, -1.0f, 1.0f);
        e.dur = std::max(0.005f, dur);
        e.aux = aux;
        return;
    }
}

void MusicEngine::scheduleBass(int32_t offsetSamples, float freq, float amp, float dur, float pan, float color) {
    for (auto& e : mEvents) {
        if (e.active) continue;
        e.active = true;
        e.samples = std::max(0, offsetSamples);
        e.kind = EventKind::Bass;
        e.freq = freq;
        e.amp = amp;
        e.dur = std::max(0.025f, dur);
        e.pan = clamp(pan, -1.0f, 1.0f);
        e.aux = color;
        return;
    }
}

void MusicEngine::schedulePad(int32_t offsetSamples, int32_t degree, float amp, float dur, float pan, float color) {
    for (auto& e : mEvents) {
        if (e.active) continue;
        e.active = true;
        e.samples = std::max(0, offsetSamples);
        e.kind = EventKind::Pad;
        e.degree = degree;
        e.amp = amp;
        e.dur = std::max(0.050f, dur);
        e.pan = clamp(pan, -1.0f, 1.0f);
        e.aux = color;
        return;
    }
}

void MusicEngine::scheduleLead(int32_t offsetSamples, float freq, float amp, float dur, float pan, float color) {
    for (auto& e : mEvents) {
        if (e.active) continue;
        e.active = true;
        e.samples = std::max(0, offsetSamples);
        e.kind = EventKind::Lead;
        e.freq = freq;
        e.amp = amp;
        e.dur = std::max(0.020f, dur);
        e.pan = clamp(pan, -1.0f, 1.0f);
        e.aux = color;
        return;
    }
}

void MusicEngine::triggerDrum(DrumType type, float amp, float pan, float dur, float aux) {
    DrumVoice* chosen = nullptr;
    for (auto& v : mDrums) {
        if (!v.active) { chosen = &v; break; }
    }
    if (!chosen) {
        chosen = &mDrums[0];
        for (auto& v : mDrums) if (v.age > chosen->age) chosen = &v;
    }
    *chosen = DrumVoice{};
    chosen->active = true;
    chosen->type = type;
    chosen->dur = dur;
    chosen->amp = amp;
    chosen->aux = aux;
    chosen->noiseState = mRng.nextU32();
    panGains(pan, chosen->panL, chosen->panR);
    if (type == DrumType::Kick) mSidechain = std::min(mSidechain, 0.46f);
}

void MusicEngine::triggerBass(float freq, float amp, float dur, float pan, float color) {
    BassVoice* chosen = nullptr;
    for (auto& v : mBass) {
        if (!v.active) { chosen = &v; break; }
    }
    if (!chosen) chosen = &mBass[0];
    *chosen = BassVoice{};
    chosen->active = true;
    chosen->dur = dur;
    chosen->amp = amp;
    chosen->freq = freq * (0.995f + 0.010f * mRng.uni());
    chosen->targetFreq = freq;
    chosen->cutoff = 0.025f + 0.080f * clamp01(color);
    chosen->drive = 1.2f + 3.2f * clamp01(color);
    chosen->color = color;
    panGains(pan, chosen->panL, chosen->panR);
}

void MusicEngine::triggerPad(int32_t degree, float amp, float dur, float pan, float color) {
    PadVoice* chosen = nullptr;
    for (auto& v : mPads) {
        if (!v.active) { chosen = &v; break; }
    }
    if (!chosen) chosen = &mPads[0];
    *chosen = PadVoice{};
    chosen->active = true;
    chosen->dur = dur;
    chosen->amp = amp;
    chosen->count = mRng.chance(0.30f) ? 4 : 3;
    chosen->cutoff = 0.014f + 0.060f * color + 0.030f * mPattern.texture;
    chosen->color = color;
    panGains(pan, chosen->panL, chosen->panR);

    const int32_t intervals[] = {0, 2, 4, 6};
    for (int32_t i = 0; i < chosen->count; ++i) {
        const int32_t octave = (i >= 3) ? 2 : 1;
        const float hz = midiToHz(static_cast<float>(scaleDegreeToMidi(degree + intervals[i], octave)));
        chosen->freq[i] = hz * (0.997f + 0.006f * mRng.uni());
        chosen->phase[i] = mRng.uni();
    }
}

void MusicEngine::triggerLead(float freq, float amp, float dur, float pan, float color) {
    LeadVoice* chosen = nullptr;
    for (auto& v : mLeads) {
        if (!v.active) { chosen = &v; break; }
    }
    if (!chosen) chosen = &mLeads[0];
    *chosen = LeadVoice{};
    chosen->active = true;
    chosen->dur = dur;
    chosen->amp = amp;
    chosen->freq = freq * (0.996f + 0.008f * mRng.uni());
    chosen->targetFreq = freq;
    chosen->cutoff = 0.035f + 0.16f * color + 0.08f * mPattern.melody;
    chosen->color = color;
    chosen->noiseState = mRng.nextU32();
    panGains(pan, chosen->panL, chosen->panR);
}

float MusicEngine::renderDrum(DrumVoice& v) {
    const float sr = static_cast<float>(mSampleRate);
    const float dt = 1.0f / sr;
    const float t = v.age / std::max(0.001f, v.dur);
    if (t >= 1.0f) {
        v.active = false;
        return 0.0f;
    }

    float out = 0.0f;
    switch (v.type) {
        case DrumType::Kick: {
            const float env = std::exp(-7.2f * t);
            const float pitch = 38.0f + 116.0f * std::exp(-12.0f * t) + 18.0f * v.aux;
            v.phase += kTwoPi * pitch / sr;
            if (v.phase > kTwoPi) v.phase -= kTwoPi;
            const float body = std::sin(v.phase) * env;
            const float click = noise(v.noiseState) * std::exp(-70.0f * t) * (0.10f + 0.14f * v.aux);
            out = std::tanh((body + click) * (1.8f + 1.4f * v.aux));
            break;
        }
        case DrumType::Snare: {
            const float env = std::exp(-10.0f * t);
            const float n = noise(v.noiseState);
            v.hp = 0.92f * (v.hp + n - v.lp);
            v.lp = n;
            v.phase += kTwoPi * (160.0f + 80.0f * v.aux) / sr;
            if (v.phase > kTwoPi) v.phase -= kTwoPi;
            out = (0.72f * v.hp + 0.24f * std::sin(v.phase)) * env;
            break;
        }
        case DrumType::Clap: {
            const float n = noise(v.noiseState);
            const float e1 = std::exp(-18.0f * t);
            const float burst = (std::exp(-180.0f * std::fabs(t - 0.10f)) +
                                 std::exp(-180.0f * std::fabs(t - 0.22f)) +
                                 std::exp(-120.0f * std::fabs(t - 0.36f))) * 0.35f;
            out = n * (e1 * 0.35f + burst);
            break;
        }
        case DrumType::HatClosed:
        case DrumType::HatOpen: {
            const float env = std::exp(-(v.type == DrumType::HatOpen ? 5.5f : 28.0f) * t);
            float n = noise(v.noiseState);
            v.hp = 0.86f * (v.hp + n - v.lp);
            v.lp = n;
            const float metallic = std::sin(v.phase) * 0.18f + std::sin(v.phase2) * 0.11f;
            v.phase += kTwoPi * (6400.0f + 1600.0f * v.aux) / sr;
            v.phase2 += kTwoPi * (9100.0f + 1200.0f * v.aux) / sr;
            if (v.phase > kTwoPi) v.phase -= kTwoPi;
            if (v.phase2 > kTwoPi) v.phase2 -= kTwoPi;
            out = (v.hp + metallic) * env;
            break;
        }
        case DrumType::Rim: {
            const float env = std::exp(-24.0f * t);
            v.phase += kTwoPi * (580.0f + 260.0f * v.aux) / sr;
            if (v.phase > kTwoPi) v.phase -= kTwoPi;
            out = std::sin(v.phase) * env + noise(v.noiseState) * env * 0.18f;
            break;
        }
        case DrumType::Tom: {
            const float env = std::exp(-7.0f * t);
            const float pitch = 105.0f + 150.0f * v.aux + 120.0f * std::exp(-9.0f * t);
            v.phase += kTwoPi * pitch / sr;
            if (v.phase > kTwoPi) v.phase -= kTwoPi;
            out = std::sin(v.phase) * env;
            break;
        }
        case DrumType::Zap: {
            const float env = std::exp(-18.0f * t);
            const float pitch = 220.0f + 4600.0f * std::exp(-20.0f * t) * (0.35f + v.aux);
            v.phase += kTwoPi * pitch / sr;
            if (v.phase > kTwoPi) v.phase -= kTwoPi;
            out = std::sin(v.phase + 2.0f * std::sin(v.phase2)) * env;
            v.phase2 += kTwoPi * (pitch * 1.91f) / sr;
            if (v.phase2 > kTwoPi) v.phase2 -= kTwoPi;
            break;
        }
        case DrumType::Noise:
        case DrumType::Perc:
        default: {
            const float env = std::exp(-13.0f * t);
            float n = noise(v.noiseState);
            v.lp += (n - v.lp) * (0.04f + 0.20f * v.aux);
            out = (n - v.lp * 0.4f) * env;
            break;
        }
    }

    v.age += dt;
    return out * v.amp;
}

float MusicEngine::renderBass(BassVoice& v) {
    const float sr = static_cast<float>(mSampleRate);
    const float dt = 1.0f / sr;
    const float t = v.age / std::max(0.001f, v.dur);
    if (t >= 1.0f) {
        v.active = false;
        return 0.0f;
    }

    const float model = clamp01(v.color);
    const float attack = clamp(t * (16.0f + 22.0f * (1.0f - model)), 0.0f, 1.0f);
    const float release = 1.0f - clamp((t - (0.58f + 0.12f * (1.0f - model))) / (0.42f - 0.10f * (1.0f - model)), 0.0f, 1.0f);
    const float env = attack * release;
    v.freq += (v.targetFreq - v.freq) * (0.0025f + 0.018f * model);
    v.phase += v.freq / sr;
    v.phase2 += v.freq * (0.498f + 0.010f * model) / sr;
    if (v.phase >= 1.0f) v.phase -= 1.0f;
    if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;

    const float sine = std::sin(kTwoPi * v.phase);
    const float sub = std::sin(kTwoPi * v.phase2);
    const float saw = 2.0f * v.phase - 1.0f;
    const float tri = 1.0f - 4.0f * std::fabs(v.phase - 0.5f);
    float raw = 0.0f;
    if (model < 0.17f) {
        raw = 0.54f * sine + 0.50f * sub;
    } else if (model < 0.34f) {
        raw = 0.44f * std::tanh(sine * 3.2f) + 0.36f * sub + 0.22f * tri;
    } else if (model < 0.50f) {
        raw = 0.38f * sine + 0.28f * sub + 0.50f * saw;
    } else if (model < 0.67f) {
        const float fm = std::sin(kTwoPi * v.phase + (1.6f + 3.6f * model) * std::sin(kTwoPi * (v.phase2 * 2.01f)));
        raw = 0.48f * fm + 0.34f * sub + 0.24f * saw;
    } else if (model < 0.84f) {
        const float pulse = (v.phase < (0.22f + 0.20f * model)) ? 1.0f : -1.0f;
        raw = 0.48f * pulse + 0.28f * tri + 0.34f * sub;
    } else {
        raw = 0.42f * std::tanh(saw * 2.6f) + 0.46f * sub + 0.24f * sine;
    }

    raw = std::tanh(raw * (v.drive + 0.80f * model));
    const float cutoff = clamp(v.cutoff + env * (0.035f + 0.18f * model), 0.004f, 0.46f);
    v.lp += (raw - v.lp) * cutoff;
    v.age += dt;
    return v.lp * env * v.amp;
}

float MusicEngine::renderPad(PadVoice& v) {
    const float sr = static_cast<float>(mSampleRate);
    const float dt = 1.0f / sr;
    const float t = v.age / std::max(0.001f, v.dur);
    if (t >= 1.0f) {
        v.active = false;
        return 0.0f;
    }

    const float model = clamp01(v.color);
    const float attack = clamp(t * (v.dur > 2.0f ? (1.6f + 2.8f * model) : (5.0f + 8.0f * model)), 0.0f, 1.0f);
    const float release = 1.0f - clamp((t - 0.70f) / 0.30f, 0.0f, 1.0f);
    const float env = attack * release;
    float raw = 0.0f;
    for (int32_t i = 0; i < v.count; ++i) {
        v.phase[i] += v.freq[i] / sr;
        if (v.phase[i] >= 1.0f) v.phase[i] -= 1.0f;
        const float ph = v.phase[i];
        const float sine = std::sin(kTwoPi * ph);
        const float tri = 1.0f - 4.0f * std::fabs(ph - 0.5f);
        const float saw = 2.0f * ph - 1.0f;
        const float pulse = (ph < (0.38f + 0.16f * model)) ? 1.0f : -1.0f;
        float osc = 0.0f;
        if (model < 0.15f) osc = 0.84f * sine + 0.16f * tri;
        else if (model < 0.30f) osc = 0.68f * tri + 0.22f * sine;
        else if (model < 0.45f) osc = 0.56f * tri + 0.34f * saw;
        else if (model < 0.60f) osc = std::sin(kTwoPi * ph + (0.8f + 2.8f * model) * std::sin(kTwoPi * ph * 2.0f));
        else if (model < 0.75f) osc = 0.52f * pulse + 0.34f * tri;
        else osc = 0.42f * saw + 0.38f * sine + 0.20f * std::sin(kTwoPi * ph * 1.503f);
        raw += osc;
    }
    raw /= static_cast<float>(std::max(1, v.count));
    raw = std::tanh(raw * (1.05f + 0.95f * model));
    v.lp += (raw - v.lp) * clamp(v.cutoff * (0.70f + 1.10f * model), 0.003f, 0.26f);
    v.hp += (v.lp - v.hp) * (0.00020f + 0.00042f * (1.0f - model));
    v.age += dt;
    return (v.lp - v.hp * (0.12f + 0.18f * model)) * env * v.amp;
}

float MusicEngine::renderLead(LeadVoice& v) {
    const float sr = static_cast<float>(mSampleRate);
    const float dt = 1.0f / sr;
    const float t = v.age / std::max(0.001f, v.dur);
    if (t >= 1.0f) {
        v.active = false;
        return 0.0f;
    }

    const float model = clamp01(v.color);
    const float attack = clamp(t * (10.0f + 20.0f * (1.0f - model)), 0.0f, 1.0f);
    const float release = 1.0f - clamp((t - (0.50f + 0.10f * model)) / (0.50f - 0.10f * model), 0.0f, 1.0f);
    const float env = attack * release;
    v.vibPhase += (3.2f + 4.0f * model) / sr;
    if (v.vibPhase > 1.0f) v.vibPhase -= 1.0f;
    const float vib = 1.0f + std::sin(kTwoPi * v.vibPhase) * (0.0008f + 0.0050f * model);
    v.freq += (v.targetFreq - v.freq) * (0.0015f + 0.012f * model);
    const float freq = v.freq * vib;

    v.phase += freq / sr;
    v.modPhase += freq * (1.35f + 3.10f * model) / sr;
    if (v.phase >= 1.0f) v.phase -= 1.0f;
    if (v.modPhase >= 1.0f) v.modPhase -= 1.0f;

    const float sine = std::sin(kTwoPi * v.phase);
    const float tri = 1.0f - 4.0f * std::fabs(v.phase - 0.5f);
    const float saw = 2.0f * v.phase - 1.0f;
    float raw = 0.0f;
    if (model < 0.14f) {
        const float pulse = (v.phase < 0.34f) ? 1.0f : -1.0f;
        raw = 0.58f * pulse + 0.34f * saw;
    } else if (model < 0.28f) {
        raw = std::sin(kTwoPi * v.phase + (1.0f + 4.5f * model) * std::sin(kTwoPi * v.modPhase));
    } else if (model < 0.42f) {
        raw = 0.72f * sine + 0.24f * tri + 0.14f * std::sin(kTwoPi * v.modPhase * 0.503f);
    } else if (model < 0.56f) {
        raw = std::tanh((0.58f * saw + 0.30f * sine) * (1.6f + 2.2f * model));
    } else if (model < 0.70f) {
        const float stepped = (v.phase < 0.25f ? -0.65f : (v.phase < 0.50f ? -0.15f : (v.phase < 0.75f ? 0.35f : 0.80f)));
        raw = 0.52f * stepped + 0.30f * tri + 0.20f * sine;
    } else if (model < 0.84f) {
        float n = noise(v.noiseState);
        v.lp += (n - v.lp) * 0.055f;
        raw = 0.68f * std::sin(kTwoPi * v.phase + 1.9f * std::sin(kTwoPi * v.modPhase)) + 0.24f * (n - v.lp);
    } else if (model < 0.92f) {
        const float vowel = std::sin(kTwoPi * v.phase) + 0.42f * std::sin(kTwoPi * v.phase * 2.01f + 0.7f) + 0.24f * std::sin(kTwoPi * v.phase * 3.02f + 1.4f);
        raw = std::tanh(vowel * 0.82f);
    } else {
        const float bell = std::sin(kTwoPi * v.phase + 0.70f * std::sin(kTwoPi * v.modPhase * 1.618f)) +
                           0.38f * std::sin(kTwoPi * v.phase * 2.414f + 0.30f) +
                           0.18f * std::sin(kTwoPi * v.phase * 3.732f + 1.10f);
        raw = std::tanh(bell * (0.62f + 0.28f * env));
    }

    v.lp += (raw - v.lp) * clamp(v.cutoff * (0.42f + 1.38f * env + 0.42f * model), 0.006f, 0.52f);
    v.age += dt;
    return v.lp * env * v.amp;
}

float MusicEngine::renderTexture() {
    const float sr = static_cast<float>(mSampleRate);
    const float tone = clamp01(mComposition.textureTone);
    const float profileTexture = mPattern.profileTexture;
    const float amount = (0.010f + mPattern.texture * (0.022f + 0.052f * profileTexture)) * mComposition.useTexture;
    if (amount <= 0.0001f) return 0.0f;
    mTexturePhaseA += (0.030f + 0.070f * profileTexture + 0.030f * tone) / sr;
    mTexturePhaseB += (0.045f + 0.062f * mPattern.roughness + 0.050f * tone) / sr;
    if (mTexturePhaseA >= 1.0f) mTexturePhaseA -= 1.0f;
    if (mTexturePhaseB >= 1.0f) mTexturePhaseB -= 1.0f;
    const float n = noise(mTextureNoise) * (0.08f + 0.22f * tone);
    const float slow = std::sin(kTwoPi * mTexturePhaseA) * (0.45f + 0.20f * (1.0f - tone)) +
                       std::sin(kTwoPi * mTexturePhaseB * (1.0f + tone)) * (0.22f + 0.26f * tone) + n;
    mTextureLp += (slow - mTextureLp) * 0.0017f;
    mTextureHp += (mTextureLp - mTextureHp) * 0.00022f;
    return (mTextureLp - 0.30f * mTextureHp) * amount;
}

void MusicEngine::applyDelayAndMaster(float& left, float& right) {
    if (!mDelayL.empty() && !mDelayR.empty()) {
        const int32_t size = static_cast<int32_t>(mDelayL.size());
        const float bpm = std::max(40.0f, mBpm);
        const float beatSeconds = 60.0f / bpm;
        const float desired = beatSeconds * (mPattern.profileAmbient ? 0.75f : (mPattern.profileBreakbeat ? 0.375f : 0.50f));
        const int32_t targetDelay = clamp(static_cast<float>(desired * static_cast<float>(mSampleRate)), 1200.0f, static_cast<float>(size - 1));
        mDelaySamples += static_cast<int32_t>((targetDelay - mDelaySamples) * 0.00002f);
        mDelaySamples = std::max(1, std::min(size - 1, mDelaySamples));
        const int32_t read = (mDelayWrite - mDelaySamples + size) % size;
        const float dl = mDelayL[read];
        const float dr = mDelayR[read];
        const float send = mPattern.delay * (0.22f + 0.55f * mPattern.space);
        const float feedback = clamp(0.18f + 0.42f * mPattern.space + 0.10f * mPattern.profileTexture, 0.10f, 0.66f);
        mDelayL[mDelayWrite] = std::tanh((left * send + dr * feedback) * 0.92f);
        mDelayR[mDelayWrite] = std::tanh((right * send + dl * feedback) * 0.92f);
        mDelayWrite = (mDelayWrite + 1) % size;
        left += dl * (0.16f + 0.35f * mPattern.delay);
        right += dr * (0.16f + 0.35f * mPattern.delay);
    }

    const float drive = 1.05f + 0.82f * mPattern.drive;
    left = std::tanh(left * drive) / std::tanh(drive);
    right = std::tanh(right * drive) / std::tanh(drive);

    const float hpL = left - mDcInL + 0.995f * mDcOutL;
    const float hpR = right - mDcInR + 0.995f * mDcOutR;
    mDcInL = left;
    mDcInR = right;
    mDcOutL = hpL;
    mDcOutR = hpR;

    float normL = hpL;
    float normR = hpR;
    const float instantPower = 0.5f * (normL * normL + normR * normR);
    if (mTransitionStage == TransitionStage::None && instantPower > 0.000001f) {
        const float rmsCoeff = 1.0f / std::max(1.0f, static_cast<float>(mSampleRate) * 0.42f);
        mAgcRms += (instantPower - mAgcRms) * rmsCoeff;
        const float targetRms = 0.105f;
        const float measured = std::sqrt(std::max(0.000001f, mAgcRms));
        const float desiredGain = clamp(targetRms / measured, 0.54f, 2.45f);
        const float gainCoeff = desiredGain < mAgcGain ? 0.00048f : 0.000075f;
        mAgcGain += (desiredGain - mAgcGain) * gainCoeff;
    }

    normL *= mAgcGain;
    normR *= mAgcGain;
    const float peak = std::max(std::fabs(normL), std::fabs(normR));
    if (peak > 0.94f) {
        const float scale = 0.94f / peak;
        normL *= scale;
        normR *= scale;
    }

    left = std::tanh(normL * mMaster * 1.10f) * 0.93f;
    right = std::tanh(normR * mMaster * 1.10f) * 0.93f;
}

float MusicEngine::noise(uint32_t& state) {
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return static_cast<float>((state >> 8u) & 0x00ffffffu) * (2.0f / 16777216.0f) - 1.0f;
}

float MusicEngine::midiToHz(float midi) const {
    return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
}

int32_t MusicEngine::scaleLength() const {
    return mPattern.scaleMode == 4 ? 5 : 7;
}

int32_t MusicEngine::scaleDegreeToMidi(int32_t degree, int32_t octaveOffset) const {
    static constexpr int32_t naturalMinor[7] = {0, 2, 3, 5, 7, 8, 10};
    static constexpr int32_t dorian[7] = {0, 2, 3, 5, 7, 9, 10};
    static constexpr int32_t phrygian[7] = {0, 1, 3, 5, 7, 8, 10};
    static constexpr int32_t harmonicMinor[7] = {0, 2, 3, 5, 7, 8, 11};
    static constexpr int32_t minorPent[5] = {0, 3, 5, 7, 10};

    const int32_t len = scaleLength();
    int32_t octave = 0;
    int32_t idx = degree;
    while (idx < 0) { idx += len; --octave; }
    while (idx >= len) { idx -= len; ++octave; }

    int32_t semitone = 0;
    switch (mPattern.scaleMode) {
        case 1: semitone = dorian[idx]; break;
        case 2: semitone = phrygian[idx]; break;
        case 3: semitone = harmonicMinor[idx]; break;
        case 4: semitone = minorPent[idx]; break;
        case 0:
        default: semitone = naturalMinor[idx]; break;
    }
    return mPattern.rootMidi + semitone + 12 * (octave + octaveOffset);
}

float MusicEngine::clamp01(float value) const {
    return clamp(value, 0.0f, 1.0f);
}

float MusicEngine::clamp(float value, float lo, float hi) const {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

void MusicEngine::panGains(float pan, float& left, float& right) const {
    const float p = clamp(pan, -1.0f, 1.0f) * 0.5f + 0.5f;
    const float angle = p * (kPi * 0.5f);
    left = std::cos(angle);
    right = std::sin(angle);
}

} // namespace rb
