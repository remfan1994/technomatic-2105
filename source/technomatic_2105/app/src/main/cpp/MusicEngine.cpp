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

MusicEngine::MusicEngine() : mRng(0x52423934u), mDevelopmentRng(0x63d83595u) {
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
    const uint32_t safeSeed = seed ? seed : 0x52423934u;
    mRng = Rng(safeSeed ^ 0xa511e9b3u);
    mDevelopmentRng = Rng(safeSeed ^ 0x63d83595u);
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

    mCurrentSongSeed = safeSeed;
    mPendingSongSeed = safeSeed;
    generateSeededSong(safeSeed);

    if (!mDelayL.empty()) std::fill(mDelayL.begin(), mDelayL.end(), 0.0f);
    if (!mDelayR.empty()) std::fill(mDelayR.begin(), mDelayR.end(), 0.0f);
}


void MusicEngine::next() {
    if (mTransitionStage != TransitionStage::None) {
        mPendingSongSeed = mDevelopmentRng.nextU32();
        return;
    }
    mPendingSongSeed = mDevelopmentRng.nextU32();
    const float fadeSeconds = 0.12f + 0.20f * mDevelopmentRng.uni();
    mTransitionSamplesTotal = std::max(1, static_cast<int32_t>(fadeSeconds * static_cast<float>(mSampleRate)));
    mTransitionSamplesLeft = mTransitionSamplesTotal;
    mDeadAirSamples = static_cast<int32_t>((0.05f + 0.20f * mDevelopmentRng.uni()) * static_cast<float>(mSampleRate));
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
    if (!mDelayL.empty()) std::fill(mDelayL.begin(), mDelayL.end(), 0.0f);
    if (!mDelayR.empty()) std::fill(mDelayR.begin(), mDelayR.end(), 0.0f);
    mDelayWrite = 0;
    mDcInL = mDcInR = mDcOutL = mDcOutR = 0.0f;
    mTextureLp = 0.0f;
    mTextureHp = 0.0f;
    mTexturePhaseA = 0.0f;
    mTexturePhaseB = 0.0f;
    mTextureNoise = mRng.nextU32();

    uint32_t seed = mDevelopmentRng.nextU32();
    seed ^= static_cast<uint32_t>(currentGenreMask() + 1) * 0x9e3779b9u;
    seed ^= static_cast<uint32_t>(mCurrentPieceSamples.load(std::memory_order_acquire) + 0x27d4eb2du);
    if (seed == 0u) seed = 0x52423934u;
    generateSeededSong(seed);
    mCurrentPieceSamples.store(0, std::memory_order_release);
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

void MusicEngine::setGenrePrimary(int32_t mode) {
    mGenrePrimary = std::max(0, std::min(kGenreModeCount, mode));
}

int32_t MusicEngine::currentGenrePrimary() const {
    return std::max(0, std::min(kGenreModeCount, mGenrePrimary));
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


std::string MusicEngine::currentSongData() const {
    std::lock_guard<std::mutex> guard(mSongDataMutex);
    return mCurrentSongData.empty() ? std::string("technomatic2105-v1;seed=1379932468;seconds=180;edited=0;gmask=0;gblend=0;gprimary=0;gmode=0;cand=0") : mCurrentSongData;
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
    uint32_t seconds = static_cast<uint32_t>(kDefaultExportSeconds);
    if (!parseUnsignedField(data, "seed", seed)) return false;
    uint32_t parsedSeconds = 0;
    if (parseUnsignedField(data, "seconds", parsedSeconds) && parsedSeconds >= 8u && parsedSeconds <= 999999u) {
        seconds = parsedSeconds;
    }
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
    // Live playback has no automatic sound boundary. Export alone receives a
    // finite boundary so it can render one file with an optional generated outro.
    engine.mExportSinglePieceMode = true;
    engine.mExportStopSamples = static_cast<int64_t>(seconds) * 48000LL;
    engine.mComposition.pieceSteps = engine.pieceStepsFromSeconds(seconds, engine.mBpmTarget);

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
    int32_t legacySeconds = kDefaultExportSeconds;
    if (!decodeSongData(data, seed, legacySeconds)) return false;

    const int32_t oldMask = mGenreMask;
    const int32_t oldBlend = mGenreBlendMode;
    const int32_t oldPrimary = mGenrePrimary;
    int32_t savedMask = 0;
    int32_t savedBlend = 0;
    int32_t savedPrimary = 0;
    int32_t savedCandidate = -1;
    const bool hasSavedMask = parseSignedField(data, "gmask", savedMask);
    const bool hasSavedBlend = parseSignedField(data, "gblend", savedBlend);
    const bool hasSavedPrimary = parseSignedField(data, "gprimary", savedPrimary);
    const bool hasSavedCandidate = parseSignedField(data, "cand", savedCandidate);

    // v15-v22 used bit 12 as a special No Genre value. No Channel is mask 0.
    if (hasSavedMask && savedMask == (1 << 12)) savedMask = 0;
    if (hasSavedMask) mGenreMask = clampInt32(savedMask, 0, (1 << kGenreModeCount) - 1);
    if (hasSavedBlend) mGenreBlendMode = clampInt32(savedBlend, 0, 1);
    if (hasSavedPrimary) {
        mGenrePrimary = clampInt32(savedPrimary, 0, kGenreModeCount);
    } else if (hasSavedMask && savedMask != 0) {
        mGenrePrimary = 0;
        for (int32_t i = 0; i < kGenreModeCount; ++i) {
            if ((savedMask & (1 << i)) != 0) { mGenrePrimary = i + 1; break; }
        }
    }
    mForcedCandidateIndex = hasSavedCandidate ? clampInt32(savedCandidate, 0, 47) : -1;

    const bool oldSuppressHistory = mSuppressHistoryRecord;
    mSuppressHistoryRecord = true;
    reset(seed);
    mSuppressHistoryRecord = oldSuppressHistory;
    mForcedCandidateIndex = -1;

    // The loaded sound freezes its saved channel identity. The selector for a
    // later manually generated sound remains unchanged.
    if (hasSavedMask) mGenreMask = oldMask;
    if (hasSavedBlend) mGenreBlendMode = oldBlend;
    if (hasSavedPrimary || hasSavedMask) mGenrePrimary = oldPrimary;

    int32_t edited = 0;
    const bool hasExtendedGeneratorData = data.find(";style=") != std::string::npos ||
                                          data.find(";tempo=") != std::string::npos ||
                                          data.find(";motif=") != std::string::npos;
    if ((parseSignedField(data, "edited", edited) && edited == 1) || (!hasSavedMask && hasExtendedGeneratorData)) {
        applySongDataOverrides(data);
    }
    updateCurrentSongData();
    return true;
}


void MusicEngine::updateCurrentSongData() {
    auto appendField = [](std::string& out, const char* key, int32_t value) {
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), ";%s=%d", key, value);
        out += buffer;
    };

    char header[96];
    std::snprintf(header, sizeof(header), "technomatic2105-v1;seed=%u;seconds=%d", mCurrentSongSeed, kDefaultExportSeconds);
    std::string out(header);
    appendField(out, "edited", mCurrentSongEdited ? 1 : 0);
    appendField(out, "gmask", clampInt32(mActiveGenreMask, 0, (1 << kGenreModeCount) - 1));
    appendField(out, "gblend", clampInt32(mActiveGenreBlendMode, 0, 1));
    appendField(out, "gprimary", clampInt32(mActiveGenrePrimary, 0, kGenreModeCount));
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
    mActiveGenreMask = currentGenreMask();
    mActiveGenreBlendMode = currentGenreBlendMode();
    mActiveGenrePrimary = currentGenrePrimary();
    if (mActiveGenreMask == 0) {
        mActiveGenreBlendMode = 0;
        mActiveGenrePrimary = 0;
    } else if (mActiveGenrePrimary <= 0 ||
               (mActiveGenreMask & (1 << (mActiveGenrePrimary - 1))) == 0) {
        mActiveGenrePrimary = 0;
        for (int32_t i = 0; i < kGenreModeCount; ++i) {
            if ((mActiveGenreMask & (1 << i)) != 0) { mActiveGenrePrimary = i + 1; break; }
        }
    }
    mCurrentSongSeed = requestedSeed;
    mCurrentSongEdited = false;
    mCurrentPieceSamples.store(0, std::memory_order_release);
    mDevelopmentRng = Rng(requestedSeed ^ 0x63d83595u);

    const auto recentCopy = mRecentHash;
    const int32_t recentWriteCopy = mRecentHashWrite;
    const auto recentMotifCopy = mRecentMotifHash;
    const int32_t recentMotifWriteCopy = mRecentMotifHashWrite;
    auto inRecentPatternCopy = [&](uint32_t hash) {
        if (hash == 0u) return false;
        for (uint32_t h : recentCopy) if (h == hash) return true;
        return false;
    };
    auto inRecentMotifCopy = [&](uint32_t hash) {
        if (hash == 0u) return false;
        for (uint32_t h : recentMotifCopy) if (h == hash) return true;
        return false;
    };

    Pattern bestPattern{};
    Composition bestComposition{};
    Rng bestRng(requestedSeed ^ 0xa511e9b3u);
    float bestBpm = 92.0f;
    float bestBpmTarget = 92.0f;
    int32_t bestGenreMode = 0;
    int32_t bestCandidateIndex = 0;
    float bestScore = -1000000.0f;
    const int32_t forcedCandidateIndex = clampInt32(mForcedCandidateIndex, -1, 47);
    for (int32_t i = 0; i < 48; ++i) {
        if (forcedCandidateIndex >= 0 && i != forcedCandidateIndex) continue;
        const uint32_t salt = 0x9e3779b9u * static_cast<uint32_t>(i + 1) + 0x85ebca6bu;
        const uint32_t candidateSeed = (i == 0) ? requestedSeed : (requestedSeed ^ salt);
        mRecentHash = recentCopy;
        mRecentHashWrite = recentWriteCopy;
        mRecentMotifHash = recentMotifCopy;
        mRecentMotifHashWrite = recentMotifWriteCopy;
        mRng = Rng(candidateSeed ^ 0xa511e9b3u);
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
            if (inRecentMotifCopy(candidateMotifHash)) score -= 1.95f;
            if ((i % 7) == 0 && i > 0) score += 0.025f * static_cast<float>(i / 7);
        }
        if (score > bestScore) {
            bestScore = score;
            bestPattern = mPattern;
            bestComposition = mComposition;
            bestRng = mRng;
            bestBpm = mBpm;
            bestBpmTarget = mBpmTarget;
            bestGenreMode = candidateGenreMode;
            bestCandidateIndex = i;
        }
    }

    mCurrentSongSeed = requestedSeed;
    mCurrentCandidateIndex = bestCandidateIndex;
    mPattern = bestPattern;
    mComposition = bestComposition;
    mRng = bestRng;
    mDevelopmentRng = Rng(requestedSeed ^ (0x63d83595u + static_cast<uint32_t>(bestCandidateIndex + 1) * 0x9e3779b9u));
    mBpm = bestBpm;
    mBpmTarget = bestBpmTarget;
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
    int32_t hookHits = 0;
    int32_t verseHits = 0;
    int32_t answerHits = 0;
    int32_t bassHits = 0;
    int32_t bassDevelopmentHits = 0;
    int32_t secondaryHits = 0;
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
        if (mComposition.gateF[i] > 0.20f) ++hookHits;
        if (mComposition.gateG[i] > 0.20f) ++verseHits;
        if (mComposition.bassGate[i] > 0.20f) ++bassHits;
        if (mComposition.bassVerseGate[i] > 0.20f || mComposition.bassAnswerGate[i] > 0.20f) ++bassDevelopmentHits;
        if (mComposition.counterGate[i] > 0.20f || mComposition.arpGate[i] > 0.20f ||
            mComposition.pulseGate[i] > 0.20f || mComposition.ornamentGate[i] > 0.20f) ++secondaryHits;
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
            0.28f * clamp(static_cast<float>(hookHits) / 7.0f, 0.0f, 1.5f) +
            0.18f * clamp(static_cast<float>(verseHits) / 8.0f, 0.0f, 1.5f) +
            0.24f * clamp(static_cast<float>(uniqueDegrees) / 5.0f, 0.0f, 1.5f) +
            0.24f * clamp(static_cast<float>(span) / 7.0f, 0.0f, 1.5f);
    const float foundation = 0.60f * clamp(static_cast<float>(bassHits) / 4.0f, 0.0f, 1.4f) +
            0.20f * clamp(static_cast<float>(bassDevelopmentHits) / 7.0f, 0.0f, 1.4f) +
            0.28f * clamp(static_cast<float>(chordHits) / 2.0f, 0.0f, 1.2f);
    const float phraseLogic = 0.18f * clamp(static_cast<float>(repeatedCells) / 3.0f, 0.0f, 1.4f) +
            0.18f * clamp(static_cast<float>(directionChanges) / 3.0f, 0.0f, 1.4f) +
            0.14f * clamp(static_cast<float>(strongAnchors) / 3.0f, 0.0f, 1.3f) +
            0.12f * clamp(static_cast<float>(answerContrast) / 4.0f, 0.0f, 1.3f) +
            0.10f * clamp(static_cast<float>(rhythmicCells) / 5.0f, 0.0f, 1.2f) +
            0.10f * clamp(static_cast<float>(secondaryHits) / 8.0f, 0.0f, 1.3f);
    const float layerTarget = mPattern.space > 0.70f ? 7.0f : 11.0f;
    const float layerPenalty = std::fabs(lanes - layerTarget) * 0.055f;
    const float grammar = 0.38f * mComposition.longMemory +
            0.34f * mComposition.callResponse +
            0.26f * mComposition.counterpoint +
            0.22f * mComposition.melodicGravity;

    float score = melodic + foundation + grammar + phraseLogic - layerPenalty;
    if (leadHits < 4) score -= 1.20f;
    if (hookHits < 3) score -= 0.55f;
    if (bassHits < 2) score -= 0.65f;
    if (span <= 1 && mPattern.melody > 0.45f) score -= 0.55f;
    if (uniqueDegrees < 3 && mPattern.melody > 0.45f) score -= 0.42f;
    if (directionalMoves > 0 && directionChanges == 0 && mPattern.melody > 0.50f) score -= 0.30f;
    if (largeLeaps > 3) score -= 0.26f;
    if (lanes < 5.0f) score -= 0.80f;
    if (lanes > 18.0f && mPattern.space < 0.40f) score -= 0.35f;
    if (mComposition.hookStability < 0.76f) score -= 0.18f;
    if (mComposition.verseFreedom < 0.30f) score -= 0.12f;
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

    applyChannelBias(p);
    return p;
}

int32_t MusicEngine::chooseGenreModeFromMask(int32_t genreMask) {
    genreMask &= ((1 << kGenreModeCount) - 1);
    if (genreMask == 0) return 0;
    int32_t selected[kGenreModeCount] = {};
    int32_t count = 0;
    for (int32_t i = 0; i < kGenreModeCount; ++i) {
        if ((genreMask & (1 << i)) != 0) selected[count++] = i + 1;
    }
    return count > 0 ? selected[mRng.rangeInt(0, count - 1)] : 0;
}

MusicEngine::StyleProfile MusicEngine::channelProfile(int32_t mode, const StyleProfile& base) const {
    StyleProfile c = base;
    auto set = [&](float lo, float hi, float swLo, float swHi,
                   float den, float dr, float ba, float mel, float ch,
                   float tex, float ro, float sp, float sy, float dra, float bright) {
        c.bpmMin = lo; c.bpmMax = hi; c.swingMin = swLo; c.swingMax = swHi;
        c.density = den; c.drum = dr; c.bass = ba; c.melody = mel; c.chord = ch;
        c.texture = tex; c.rough = ro; c.space = sp; c.sync = sy;
        c.drama = dra; c.brightness = bright;
        c.hatRoll = clamp01(0.08f + 0.50f * sy);
        c.melodyRun = clamp01(0.12f + 0.72f * mel);
        c.transitionSilence = clamp01(0.12f + 0.42f * sp);
        c.palette = clamp01(0.40f + 0.42f * (mel + tex) * 0.5f);
        c.fourOnFloor = c.halfTime = c.breakbeat = c.trapHats = c.ambient = false;
    };

    switch (std::max(0, std::min(kGenreModeCount, mode))) {
        case 1: set(80,124,.02f,.12f,.58f,.70f,.62f,.44f,.28f,.22f,.44f,.34f,.60f,.44f,.42f); c.fourOnFloor=true; break;
        case 2: set(62,106,.03f,.14f,.38f,.38f,.52f,.72f,.70f,.62f,.12f,.72f,.34f,.42f,.50f); c.ambient=true; break;
        case 3: set(120,156,.00f,.08f,.60f,.66f,.78f,.55f,.26f,.32f,.42f,.44f,.58f,.58f,.62f); c.halfTime=true; c.trapHats=true; break;
        case 4: set(88,150,.00f,.10f,.70f,.80f,.66f,.44f,.24f,.42f,.78f,.30f,.84f,.70f,.46f); c.breakbeat=true; break;
        case 5: set(84,136,.00f,.08f,.56f,.58f,.62f,.78f,.68f,.38f,.16f,.44f,.44f,.54f,.76f); c.fourOnFloor=true; break;
        case 6: set(96,148,.00f,.08f,.58f,.58f,.56f,.86f,.62f,.38f,.16f,.42f,.48f,.60f,.86f); break;
        case 7: set(90,168,.00f,.08f,.76f,.88f,.68f,.44f,.20f,.46f,.86f,.28f,.90f,.76f,.52f); c.breakbeat=true; break;
        case 8: set(76,128,.01f,.10f,.42f,.48f,.98f,.34f,.28f,.48f,.46f,.62f,.44f,.74f,.28f); c.halfTime=true; break;
        case 9: set(104,146,.00f,.05f,.58f,.64f,.48f,.42f,.38f,.34f,.36f,.44f,.42f,.42f,.72f); c.fourOnFloor=true; break;
        case 10:set(58,98,.02f,.13f,.34f,.30f,.44f,.84f,.78f,.68f,.10f,.78f,.30f,.44f,.66f); c.ambient=true; break;
        case 11:set(88,142,.00f,.09f,.54f,.60f,.96f,.36f,.32f,.50f,.42f,.62f,.48f,.70f,.32f); c.halfTime=true; break;
        case 12:set(104,148,.00f,.05f,.60f,.66f,.54f,.54f,.42f,.30f,.22f,.38f,.46f,.50f,.86f); c.fourOnFloor=true; break;
        default: break;
    }
    return c;
}

void MusicEngine::mixProfile(StyleProfile& target, const StyleProfile& source, float amount) const {
    amount = clamp01(amount);
    const float keep = 1.0f - amount;
    auto mix = [&](float& a, float b) { a = a * keep + b * amount; };
    mix(target.bpmMin, source.bpmMin); mix(target.bpmMax, source.bpmMax);
    mix(target.swingMin, source.swingMin); mix(target.swingMax, source.swingMax);
    mix(target.density, source.density); mix(target.drum, source.drum);
    mix(target.bass, source.bass); mix(target.melody, source.melody);
    mix(target.chord, source.chord); mix(target.texture, source.texture);
    mix(target.rough, source.rough); mix(target.space, source.space);
    mix(target.sync, source.sync); mix(target.hatRoll, source.hatRoll);
    mix(target.melodyRun, source.melodyRun); mix(target.transitionSilence, source.transitionSilence);
    mix(target.drama, source.drama); mix(target.palette, source.palette);
    mix(target.brightness, source.brightness);

    auto blendFlag = [&](bool base, bool incoming, uint32_t salt) {
        if (base == incoming) return base;
        // Profile construction happens before the candidate Composition exists.
        // Do not consult mComposition here: that would let the previous candidate
        // or previously playing sound contaminate the next candidate. The current
        // candidate RNG state is already a pure function of seed and candidate.
        uint32_t h = mCurrentSongSeed ^ mRng.state ^ salt;
        h ^= h >> 16u; h *= 0x7feb352du; h ^= h >> 15u;
        const float u = static_cast<float>(h & 0xffffu) / 65535.0f;
        return u < amount ? incoming : base;
    };
    target.fourOnFloor = blendFlag(target.fourOnFloor, source.fourOnFloor, 0x11u);
    target.halfTime = blendFlag(target.halfTime, source.halfTime, 0x23u);
    target.breakbeat = blendFlag(target.breakbeat, source.breakbeat, 0x47u);
    target.trapHats = blendFlag(target.trapHats, source.trapHats, 0x89u);
    target.ambient = blendFlag(target.ambient, source.ambient, 0x101u);
}

void MusicEngine::applyChannelBias(StyleProfile& p) const {
    const int32_t mask = mActiveGenreMask & ((1 << kGenreModeCount) - 1);
    if (mask == 0) return; // No Channel: unrestricted engine.

    int32_t primary = std::max(1, std::min(kGenreModeCount, mActiveGenrePrimary));
    if ((mask & (1 << (primary - 1))) == 0) {
        primary = 0;
        for (int32_t i = 0; i < kGenreModeCount; ++i) {
            if ((mask & (1 << i)) != 0) { primary = i + 1; break; }
        }
    }
    if (primary <= 0) return;

    StyleProfile channelTarget = channelProfile(primary, p);
    const bool hybrid = mActiveGenreBlendMode == 1 && (mask & (mask - 1)) != 0;
    if (hybrid) {
        StyleProfile secondaryAverage = p;
        bool haveSecondary = false;
        int32_t count = 0;
        for (int32_t i = 0; i < kGenreModeCount; ++i) {
            const int32_t mode = i + 1;
            if (mode == primary || (mask & (1 << i)) == 0) continue;
            const StyleProfile c = channelProfile(mode, p);
            if (!haveSecondary) {
                secondaryAverage = c;
                haveSecondary = true;
                count = 1;
            } else {
                ++count;
                mixProfile(secondaryAverage, c, 1.0f / static_cast<float>(count));
            }
        }
        if (haveSecondary) {
            // Primary identity is 70 percent of the channel half; secondaries color it.
            mixProfile(channelTarget, secondaryAverage, 0.30f);
        }
    }

    // Named channels are always half unrestricted generator and half channel character.
    mixProfile(p, channelTarget, 0.50f);
}

MusicEngine::StyleType MusicEngine::randomStyle() {
    const int32_t mask = mActiveGenreMask & ((1 << kGenreModeCount) - 1);
    if (mask == 0) {
        mWorkingGenreMode = 0;
    } else if (mActiveGenreBlendMode == 1 && (mask & (mask - 1)) != 0) {
        int32_t primary = std::max(1, std::min(kGenreModeCount, mActiveGenrePrimary));
        if ((mask & (1 << (primary - 1))) == 0) primary = chooseGenreModeFromMask(mask);
        mWorkingGenreMode = primary;
    } else {
        mWorkingGenreMode = chooseGenreModeFromMask(mask);
    }
    return static_cast<StyleType>(mRng.rangeInt(0, static_cast<int32_t>(StyleType::Count) - 1));
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

    // Candidate material is a pure function of seed, candidate index, and frozen
    // channel state. Recent-session memory influences candidate scoring only;
    // it must never rewrite a candidate, or saved seeds/history/export snapshots
    // would reconstruct differently in another engine instance.
    // Song data and recent-history state are committed only after
    // generateSeededSong() selects the winning candidate. Keeping candidate
    // evaluation symbolic avoids repeated string allocation and mutex work.
}

void MusicEngine::generateHarmonyGrammar(const StyleProfile& p) {
    mComposition.chordRoot.fill(0);
    Rng rng(mComposition.arcSeed ^ 0x7f4a7c15u ^ mCurrentSongSeed);
    mComposition.progressionLength = rng.chance(0.30f + 0.24f * mComposition.longMemory) ? 8 : 4;
    const int32_t length = mComposition.progressionLength;
    int32_t current = 0;
    mComposition.chordRoot[0] = 0;

    for (int32_t i = 1; i < length; ++i) {
        int32_t best = current;
        float bestScore = -10000.0f;
        for (int32_t attempt = 0; attempt < 18; ++attempt) {
            int32_t move = rng.rangeInt(-4, 5);
            if (move == 0) move = rng.chance(0.50f) ? 1 : -1;
            int32_t candidate = clampInt32(current + move, -3, 9);
            float score = rng.uni() * 0.45f;
            const int32_t distance = std::abs(candidate - current);
            score += (distance >= 1 && distance <= 4) ? 0.72f : -0.28f;
            if (candidate == current) score -= 0.90f;
            if (i == length / 2) score += 0.12f * static_cast<float>(std::abs(candidate));
            if (i >= length - 2) score += 0.20f * static_cast<float>(6 - std::min(6, std::abs(candidate)));
            if (p.ambient) score += 0.18f * static_cast<float>(4 - std::min(4, distance));
            if (p.rough > 0.65f && distance >= 3) score += 0.22f;
            if (candidate == 0 && i < length - 1) score -= 0.18f;
            if (score > bestScore) { bestScore = score; best = candidate; }
        }
        current = best;
        mComposition.chordRoot[i] = current;
    }

    // The last harmony points toward home without forcing every progression to end identically.
    if (length > 2) {
        int32_t approach = mComposition.chordRoot[length - 2];
        if (approach > 1) approach -= rng.rangeInt(1, std::min(3, approach));
        else if (approach < -1) approach += rng.rangeInt(1, std::min(3, -approach));
        else if (approach == 0) approach = rng.chance(0.50f) ? 2 : -1;
        mComposition.chordRoot[length - 1] = clampInt32(approach, -3, 7);
    }
    for (int32_t i = length; i < kMaxProgressionSlots; ++i) {
        const int32_t source = i % length;
        int32_t v = mComposition.chordRoot[source];
        if (length == 4 && i >= 4 && i < 7 && rng.chance(0.36f)) v = clampInt32(v + rng.rangeInt(-1, 1), -3, 9);
        mComposition.chordRoot[i] = v;
    }

    uint32_t h = 2166136261u;
    for (int32_t v : mComposition.chordRoot) { h ^= static_cast<uint32_t>(v + 32); h *= 16777619u; }
    mComposition.progressionId = static_cast<int32_t>(h & 0x7fffffffu);
}

void MusicEngine::generatePrimaryMotif(const StyleProfile& p) {
    mComposition.motifA.fill(0);
    mComposition.gateA.fill(0.0f);
    mComposition.durA.fill(0.0f);
    Rng rng(mComposition.leadGrammarSeed);

    mComposition.leadContour = rng.rangeInt(0, 6);
    mComposition.leadApexStep = rng.rangeInt(4, 11);
    mComposition.leadApexDegree = rng.rangeInt(3, 8) * (rng.chance(0.84f) ? 1 : -1);
    mComposition.leadCadenceDegree = rng.chance(0.78f) ? 0 : (rng.chance(0.50f) ? 2 : -1);
    mComposition.leadLeapBudget = rng.rangeInt(1, 3) + (p.melodyRun > 0.72f ? 1 : 0);
    mComposition.leadSyncopation = clamp(0.16f + 0.62f * p.sync + rng.bipolar() * 0.12f, 0.04f, 0.92f);
    int32_t noteCount = 4 + static_cast<int32_t>(4.0f * p.melody + 2.0f * p.density) + rng.rangeInt(0, 3);
    if (p.ambient) noteCount -= 2;
    if (p.melodyRun > 0.70f) noteCount += 2;
    noteCount = clampInt32(noteCount, 4, 13);
    mComposition.leadNoteCount = noteCount;

    bool used[kPhraseSteps] = {};
    used[0] = true;
    used[15] = true;
    int32_t selected = 2;
    if (noteCount >= 7 && rng.chance(0.80f)) { used[8] = true; ++selected; }
    while (selected < noteCount) {
        int32_t bestPos = -1;
        float bestScore = -10000.0f;
        for (int32_t attempt = 0; attempt < 32; ++attempt) {
            const int32_t pos = rng.rangeInt(1, 14);
            if (used[pos]) continue;
            int32_t nearest = 16;
            for (int32_t j = 0; j < 16; ++j) if (used[j]) nearest = std::min(nearest, std::abs(pos - j));
            const bool strong = (pos % 4) == 0;
            const bool off = (pos & 1) != 0;
            float score = rng.uni() * 0.55f + 0.10f * static_cast<float>(std::min(4, nearest));
            score += strong ? (0.34f * (1.0f - mComposition.leadSyncopation)) : 0.0f;
            score += off ? (0.32f * mComposition.leadSyncopation) : 0.0f;
            if (pos == mComposition.leadApexStep) score += 0.48f;
            if (pos == 7 || pos == 14) score += 0.10f * p.melodyRun;
            if (nearest == 1 && p.melodyRun < 0.46f) score -= 0.24f;
            if (score > bestScore) { bestScore = score; bestPos = pos; }
        }
        if (bestPos < 0) {
            for (int32_t pos = 1; pos < 15; ++pos) if (!used[pos]) { bestPos = pos; break; }
        }
        if (bestPos < 0) break;
        used[bestPos] = true;
        ++selected;
    }

    auto contourTarget = [&](int32_t pos) {
        const float x = static_cast<float>(pos) / 15.0f;
        const float apexX = static_cast<float>(mComposition.leadApexStep) / 15.0f;
        const float apex = static_cast<float>(mComposition.leadApexDegree);
        float y = 0.0f;
        switch (mComposition.leadContour) {
            case 0: y = (x <= apexX) ? apex * (x / std::max(0.08f, apexX)) : apex * ((1.0f - x) / std::max(0.08f, 1.0f - apexX)); break;
            case 1: y = apex * x; break;
            case 2: y = apex * (1.0f - x) - 2.0f * std::sin(kPi * x); break;
            case 3: y = apex * 0.62f * std::sin(kTwoPi * x - 0.35f); break;
            case 4: y = apex * (0.62f * std::sin(2.0f * kTwoPi * x) + 0.38f * std::sin(kPi * x)); break;
            case 5: y = (x < 0.42f) ? -1.0f + 2.0f * x : apex * (x - 0.42f) / 0.58f; break;
            default: y = apex * 0.38f * std::sin(3.0f * kPi * x) + static_cast<float>((pos * 3 + static_cast<int32_t>(mComposition.leadGrammarSeed & 7u)) % 5 - 2); break;
        }
        if (pos >= 13) {
            const float settle = static_cast<float>(15 - pos) / 2.0f;
            y = y * settle + static_cast<float>(mComposition.leadCadenceDegree) * (1.0f - settle);
        }
        return clampInt32(static_cast<int32_t>(std::lround(y)), -7, 12);
    };

    static constexpr int32_t stableDegrees[] = {-2, 0, 2, 4, 5, 7, 9};
    int32_t current = 0;
    int32_t leapBudget = mComposition.leadLeapBudget;
    for (int32_t pos = 0; pos < 16; ++pos) {
        if (!used[pos]) { mComposition.motifA[pos] = current; continue; }
        int32_t target = contourTarget(pos);
        if (pos == 0) target = 0;
        if (pos == 15) target = mComposition.leadCadenceDegree;
        const int32_t diff = target - current;
        int32_t move = 0;
        if (std::abs(diff) <= 2) move = diff;
        else if (leapBudget > 0 && rng.chance(0.22f + 0.24f * p.melodyRun)) {
            move = clampInt32(diff, -5, 5);
            --leapBudget;
        } else {
            move = (diff > 0 ? 1 : -1) * rng.rangeInt(1, 2);
        }
        current = clampInt32(current + move, -7, 14);
        if ((pos == 0 || pos == 4 || pos == 8 || pos == 12 || pos == 15) && rng.chance(mComposition.melodicGravity)) {
            int32_t nearest = current;
            int32_t dist = 99;
            for (int32_t d : stableDegrees) {
                const int32_t dd = std::abs(d - current);
                if (dd < dist) { dist = dd; nearest = d; }
            }
            current = nearest;
        }
        if (pos == 15) current = mComposition.leadCadenceDegree;
        mComposition.motifA[pos] = current;
        float gate = 0.58f + 0.36f * rng.uni();
        if (pos == 0 || pos == 8 || pos == 15) gate = std::max(gate, pos == 15 ? 0.82f : 0.88f);
        mComposition.gateA[pos] = clamp01(gate);
    }

    for (int32_t pos = 0; pos < 16; ++pos) {
        if (!used[pos]) continue;
        int32_t next = 16;
        for (int32_t j = pos + 1; j < 16; ++j) if (used[j]) { next = j; break; }
        const int32_t gap = std::max(1, next - pos);
        float dur = 0.40f + 0.20f * static_cast<float>(std::min(4, gap));
        if (pos == 0 || pos == 8) dur += 0.18f;
        if (pos == 15) dur = 1.35f;
        mComposition.durA[pos] = dur;
    }
    mComposition.motifTemplateId = static_cast<int32_t>((mComposition.leadGrammarSeed ^ (mComposition.leadGrammarSeed >> 16u)) & 0x7fffffffu);
}

void MusicEngine::deriveRelatedMotifs() {
    Rng rng(mComposition.counterGrammarSeed ^ mComposition.leadGrammarSeed);
    const int32_t rotation = rng.rangeInt(2, 6);
    for (int32_t i = 0; i < kPhraseSteps; ++i) {
        const int32_t reverseSrc = (15 - i + rotation) & 15;
        const int32_t forwardSrc = (i + rotation) & 15;

        int32_t answer = -mComposition.motifA[reverseSrc] / 2 + mComposition.answerOffset;
        if (i >= 8) answer += mComposition.bassAnswerShift;
        if (i == 0) answer = mComposition.motifA[8] + mComposition.answerOffset;
        if (i == 15) answer = 0;
        mComposition.motifB[i] = clampInt32(answer, -7, 12);
        mComposition.gateB[i] = clamp01(mComposition.gateA[reverseSrc] * (0.70f + 0.22f * rng.uni()));
        mComposition.durB[i] = std::max(0.28f, mComposition.durA[reverseSrc] * (0.72f + 0.24f * rng.uni()));

        int32_t variation = mComposition.motifA[i];
        if (mComposition.gateA[i] > 0.0f && i != 0 && i != 15 && rng.chance(0.34f)) variation += rng.rangeInt(-2, 2);
        if ((i == 4 || i == 12) && rng.chance(0.62f)) variation += (i == 4 ? 2 : -1);
        if (i == 15) variation = 0;
        mComposition.motifC[i] = clampInt32(variation, -7, 14);
        mComposition.gateC[i] = clamp01(mComposition.gateA[i] * (0.84f + 0.28f * rng.uni()) + ((i == 3 || i == 11) ? 0.20f : 0.0f));
        mComposition.durC[i] = std::max(0.26f, mComposition.durA[i] * (0.64f + 0.28f * rng.uni()));

        const bool fragmentHit = mComposition.gateA[forwardSrc] > 0.0f && (((i + rotation) % 3) != 1 || i == 0 || i == 8 || i == 15);
        mComposition.motifD[i] = clampInt32(mComposition.motifA[forwardSrc] + ((i >= 8) ? mComposition.answerOffset : 0), -7, 14);
        mComposition.gateD[i] = fragmentHit ? clamp01(mComposition.gateA[forwardSrc] * (0.58f + 0.30f * rng.uni())) : 0.0f;
        mComposition.durD[i] = fragmentHit ? std::max(0.20f, mComposition.durA[forwardSrc] * 0.58f) : 0.0f;

        int32_t recall = mComposition.motifA[i];
        if (i != 0 && i != 8 && i != 15 && rng.chance(0.24f)) recall += rng.chance(0.50f) ? 1 : -1;
        if (i == 15) recall = 0;
        mComposition.motifE[i] = clampInt32(recall, -7, 14);
        mComposition.gateE[i] = clamp01(mComposition.gateA[i] * (0.78f + 0.24f * rng.uni()));
        if (i == 0 || i == 8 || i == 15) mComposition.gateE[i] = std::max(mComposition.gateE[i], i == 15 ? 0.82f : 0.66f);
        mComposition.durE[i] = std::max(0.30f, mComposition.durA[i] * (0.92f + 0.26f * rng.uni()));

        // Crown: a stable hook derived from this seed's own motif, never from a
        // stored note sentence. The first and final anchors remain unmistakable.
        const int32_t crownSrc = (i < 8) ? i : ((i + rotation / 2) & 7);
        int32_t crown = mComposition.motifA[crownSrc];
        if (i >= 8 && i != 15 && rng.chance(0.28f)) crown += mComposition.answerOffset;
        if (i == 0 || i == 15) crown = 0;
        if (i == 8) crown = mComposition.motifA[8];
        mComposition.motifF[i] = clampInt32(crown, -7, 14);
        mComposition.gateF[i] = clamp01(std::max(mComposition.gateA[crownSrc] * 0.92f,
                                                (i == 0 || i == 8 || i == 15) ? 0.76f : 0.0f));
        mComposition.durF[i] = std::max(0.30f, mComposition.durA[crownSrc] * (0.88f + 0.16f * rng.uni()));

        // Wander: verse material shares the same vocabulary but admits more
        // grammatical freedom, so it contrasts with the hook without betrayal.
        const int32_t wanderSrc = (forwardSrc + ((i >= 8) ? rotation : 0)) & 15;
        int32_t wander = (mComposition.motifA[wanderSrc] + mComposition.motifB[i]) / 2;
        if (i != 0 && i != 15 && rng.chance(0.58f)) wander += rng.rangeInt(-2, 2);
        if (i == 15) wander = rng.chance(0.70f) ? 0 : mComposition.leadCadenceDegree;
        mComposition.motifG[i] = clampInt32(wander, -7, 14);
        float wanderGate = 0.50f * mComposition.gateA[wanderSrc] + 0.50f * mComposition.gateB[i];
        if (rng.chance(0.24f)) wanderGate += 0.22f;
        if (i == 0 || i == 8) wanderGate = std::max(wanderGate, 0.62f);
        mComposition.gateG[i] = clamp01(wanderGate * (0.74f + 0.32f * rng.uni()));
        mComposition.durG[i] = std::max(0.22f, 0.55f * mComposition.durA[wanderSrc] + 0.45f * mComposition.durB[i]);
    }
}

void MusicEngine::generateBassGrammar(const StyleProfile& p) {
    mComposition.bassRel.fill(0);
    mComposition.bassGate.fill(0.0f);
    mComposition.chordGate.fill(0.0f);
    Rng rng(mComposition.bassGrammarSeed);

    mComposition.bassSyncopation = clamp(0.10f + 0.72f * p.sync + rng.bipolar() * 0.12f, 0.02f, 0.94f);
    mComposition.bassKickAffinity = clamp(0.34f + 0.52f * p.bass + (p.fourOnFloor ? 0.16f : 0.0f) - (p.breakbeat ? 0.08f : 0.0f), 0.18f, 0.96f);
    mComposition.bassRootGravity = clamp(0.46f + 0.40f * p.bass + (p.ambient ? 0.08f : 0.0f), 0.34f, 0.98f);
    mComposition.bassAnswerShift = rng.rangeInt(-2, 2);
    mComposition.bassMotionBias = rng.rangeInt(-1, 1);
    int32_t hitCount = 2 + static_cast<int32_t>(4.0f * p.bass + 3.0f * p.density) + rng.rangeInt(-1, 2);
    if (p.ambient) hitCount -= 3;
    if (p.halfTime) hitCount -= 1;
    if (p.breakbeat) hitCount += 1;
    hitCount = clampInt32(hitCount, 2, 10);
    mComposition.bassHitCount = hitCount;

    bool used[16] = {};
    used[0] = true;
    int32_t firstTarget = std::max(1, (hitCount + 1) / 2);
    int32_t firstCount = 1;
    while (firstCount < firstTarget) {
        int32_t best = -1;
        float bestScore = -10000.0f;
        for (int32_t attempt = 0; attempt < 28; ++attempt) {
            int32_t pos = rng.rangeInt(1, 7);
            if (used[pos]) continue;
            const float kick = mPattern.kick[pos];
            float score = rng.uni() * 0.52f;
            score += mComposition.bassKickAffinity * kick * 0.88f;
            score += ((pos & 1) ? mComposition.bassSyncopation : (1.0f - mComposition.bassSyncopation)) * 0.34f;
            if (pos == 4) score += 0.16f;
            int32_t nearest = 8;
            for (int32_t j = 0; j < 8; ++j) if (used[j]) nearest = std::min(nearest, std::abs(pos-j));
            score += 0.06f * static_cast<float>(nearest);
            if (score > bestScore) { bestScore = score; best = pos; }
        }
        if (best < 0) break;
        used[best] = true;
        ++firstCount;
    }

    int32_t total = firstCount;
    for (int32_t pos = 0; pos < 8 && total < hitCount; ++pos) {
        if (!used[pos]) continue;
        int32_t answer = 8 + ((pos + mComposition.bassAnswerShift + 8) & 7);
        if (answer == 8 && pos != 0) answer = 9;
        while (answer < 16 && used[answer]) ++answer;
        if (answer >= 16) {
            for (int32_t j = 8; j < 16; ++j) if (!used[j]) { answer = j; break; }
        }
        if (answer < 16 && !used[answer]) { used[answer] = true; ++total; }
    }
    while (total < hitCount) {
        int32_t best = -1;
        float bestScore = -10000.0f;
        for (int32_t attempt = 0; attempt < 28; ++attempt) {
            const int32_t pos = rng.rangeInt(8, 15);
            if (used[pos]) continue;
            float score = rng.uni() * 0.55f + mPattern.kick[pos] * mComposition.bassKickAffinity * 0.72f;
            score += ((pos & 1) ? mComposition.bassSyncopation : (1.0f - mComposition.bassSyncopation)) * 0.26f;
            if (pos == 15) score += 0.18f;
            if (score > bestScore) { bestScore = score; best = pos; }
        }
        if (best < 0) break;
        used[best] = true;
        ++total;
    }

    static constexpr int32_t stableBassDegrees[] = {0, 2, 4, 5};
    int32_t current = 0;
    for (int32_t pos = 0; pos < 16; ++pos) {
        if (!used[pos]) continue;
        int32_t rel = current;
        if (pos == 0) rel = 0;
        else if (pos >= 8 && used[pos - 8]) {
            rel = mComposition.bassRel[pos - 8] + mComposition.bassAnswerShift;
            if (rng.chance(0.42f)) rel += rng.rangeInt(-1, 1);
        } else if (rng.chance(mComposition.bassRootGravity)) {
            rel = 0;
        } else if (rng.chance(0.66f)) {
            rel = stableBassDegrees[rng.rangeInt(0, 3)];
        } else {
            rel = current + mComposition.bassMotionBias + rng.rangeInt(-2, 2);
        }
        if (pos == 15) rel = rng.chance(0.62f) ? -1 : 0;
        rel = clampInt32(rel, -4, 7);
        current = rel;
        mComposition.bassRel[pos] = rel;
        float gate = 0.46f + 0.42f * rng.uni();
        if (pos == 0 || pos == 8) gate = std::max(gate, 0.84f);
        if (mPattern.kick[pos] > 0.60f) gate = std::max(gate, 0.68f + 0.18f * mComposition.bassKickAffinity);
        mComposition.bassGate[pos] = clamp01(gate);
    }

    // A sound keeps one bass identity, then derives a freer verse and an
    // answering phrase from it. No stored bass patterns are involved.
    for (int32_t pos = 0; pos < 16; ++pos) {
        const int32_t shifted = (pos + rng.rangeInt(1, 4)) & 15;
        int32_t verseRel = mComposition.bassRel[pos];
        float verseGate = mComposition.bassGate[pos] * (0.72f + 0.28f * rng.uni());
        if (pos != 0 && pos != 8 && pos != 15 && rng.chance(0.46f + 0.22f * p.sync)) {
            verseRel = clampInt32(verseRel + rng.rangeInt(-2, 2), -4, 7);
        }
        if (verseGate <= 0.02f && mComposition.bassGate[shifted] > 0.02f && rng.chance(0.34f)) {
            verseRel = mComposition.bassRel[shifted] + rng.rangeInt(-1, 1);
            verseGate = 0.32f + 0.38f * rng.uni();
        }
        if (pos == 0 || pos == 8) {
            verseRel = 0;
            verseGate = std::max(verseGate, 0.76f);
        }
        if (pos == 15) verseRel = rng.chance(0.72f) ? -1 : 0;
        mComposition.bassVerseRel[pos] = clampInt32(verseRel, -4, 7);
        mComposition.bassVerseGate[pos] = clamp01(verseGate);

        const int32_t source = (15 - pos + mComposition.bassAnswerShift + 16) & 15;
        int32_t answerRel = -mComposition.bassRel[source] / 2 + mComposition.bassAnswerShift;
        float answerGate = mComposition.bassGate[source] * (0.64f + 0.30f * rng.uni());
        if (pos >= 8 && rng.chance(0.44f)) answerRel += rng.rangeInt(-1, 2);
        if (pos == 0) { answerRel = mComposition.bassRel[8]; answerGate = std::max(answerGate, 0.62f); }
        if (pos == 8) { answerRel = 0; answerGate = std::max(answerGate, 0.72f); }
        if (pos == 15) { answerRel = 0; answerGate = std::max(answerGate, 0.68f); }
        mComposition.bassAnswerRel[pos] = clampInt32(answerRel, -4, 7);
        mComposition.bassAnswerGate[pos] = clamp01(answerGate);
    }

    mComposition.chordGate[0] = clamp(0.36f + 0.46f * p.chord + (p.ambient ? 0.22f : 0.0f), 0.24f, 0.96f);
    if (rng.chance(0.54f + 0.24f * p.chord)) mComposition.chordGate[8] = clamp(0.20f + 0.42f * p.chord, 0.16f, 0.78f);
    const int32_t punctuation = rng.rangeInt(2, 14);
    if (rng.chance(0.30f + 0.34f * p.sync)) mComposition.chordGate[punctuation] = 0.16f + 0.34f * rng.uni();
}

void MusicEngine::generateSecondaryLayerGrammars(const StyleProfile& p) {
    mComposition.counterRel.fill(0); mComposition.counterGate.fill(0.0f); mComposition.counterDur.fill(0.0f);
    mComposition.arpRel.fill(0); mComposition.arpGate.fill(0.0f); mComposition.arpDur.fill(0.0f);
    mComposition.pulseRel.fill(0); mComposition.pulseGate.fill(0.0f); mComposition.pulseDur.fill(0.0f);
    mComposition.ornamentRel.fill(0); mComposition.ornamentGate.fill(0.0f); mComposition.ornamentDur.fill(0.0f);

    Rng counterRng(mComposition.counterGrammarSeed);
    Rng arpRng(mComposition.arpGrammarSeed);
    Rng pulseRng(mComposition.pulseGrammarSeed);
    Rng ornamentRng(mComposition.ornamentGrammarSeed);

    int32_t counterWalk = counterRng.rangeInt(-2, 2);
    int32_t arpWalk = arpRng.rangeInt(-1, 3);
    int32_t pulseWalk = pulseRng.rangeInt(-2, 2);
    for (int32_t pos = 0; pos < kPhraseSteps; ++pos) {
        const int32_t counterSource = (pos - mComposition.counterDelay + 32) & 15;
        const int32_t leadSource = (pos < 8) ? mComposition.motifA[counterSource] : mComposition.motifB[counterSource];
        int32_t counter = mComposition.counterDirection * leadSource + mComposition.counterInterval + counterWalk;
        if (pos != 0 && pos != 15 && counterRng.chance(0.28f)) counterWalk = clampInt32(counterWalk + counterRng.rangeInt(-1, 1), -3, 3);
        if (pos == 14 || pos == 15) counter = 0;
        float counterGate = 0.0f;
        const float sourceGate = (pos < 8) ? mComposition.gateA[counterSource] : mComposition.gateB[counterSource];
        if (sourceGate > 0.02f && counterRng.chance(0.38f + 0.44f * mComposition.callResponse)) {
            counterGate = sourceGate * (0.56f + 0.30f * counterRng.uni());
        }
        if ((pos == 6 || pos == 10 || pos == 14) && counterRng.chance(0.54f + 0.26f * p.melody)) {
            counterGate = std::max(counterGate, 0.40f + 0.34f * counterRng.uni());
        }
        mComposition.counterRel[pos] = clampInt32(counter, -7, 13);
        mComposition.counterGate[pos] = clamp01(counterGate);
        mComposition.counterDur[pos] = 0.34f + 0.90f * counterRng.uni();

        const int32_t arpSource = (mComposition.arpRotation + pos * mComposition.arpStride) & 15;
        int32_t arp = (mComposition.motifF[arpSource] + arpWalk) / 2;
        if (arpRng.chance(0.48f)) arpWalk = clampInt32(arpWalk + arpRng.rangeInt(-2, 2), -3, 7);
        while (arp > 9) arp -= 7;
        while (arp < -2) arp += 7;
        const bool arpAnchor = pos == 0 || pos == 4 || pos == 8 || pos == 12;
        const float arpDensity = clamp01(0.16f + 0.54f * p.melodyRun + 0.24f * p.density);
        mComposition.arpRel[pos] = clampInt32(arp, -4, 10);
        mComposition.arpGate[pos] = (arpAnchor || arpRng.chance(arpDensity))
            ? clamp01(0.30f + 0.54f * arpRng.uni()) : 0.0f;
        mComposition.arpDur[pos] = 0.18f + 0.48f * arpRng.uni();

        if (pos == 0 || pos == 8) pulseWalk = pulseRng.chance(0.68f) ? 0 : pulseRng.rangeInt(-2, 2);
        else if (pulseRng.chance(0.32f + 0.22f * p.sync)) pulseWalk = clampInt32(pulseWalk + pulseRng.rangeInt(-2, 2), -4, 7);
        const bool pulseAccent = ((pos + static_cast<int32_t>(mComposition.pulseGrammarSeed & 3u)) & 3) == 1;
        const bool pulseHit = pulseAccent || pulseRng.chance(0.12f + 0.42f * p.sync + 0.18f * p.density);
        mComposition.pulseRel[pos] = pulseWalk;
        mComposition.pulseGate[pos] = pulseHit ? clamp01(0.26f + 0.56f * pulseRng.uni()) : 0.0f;
        mComposition.pulseDur[pos] = 0.16f + 0.44f * pulseRng.uni();

        const int32_t ornamentSource = (pos + ornamentRng.rangeInt(0, 3)) & 15;
        int32_t ornament = mComposition.motifG[ornamentSource];
        if (pos != 0 && pos != 15) ornament += ornamentRng.chance(0.50f) ? 1 : -1;
        const float ornamentSourceGate = mComposition.gateG[ornamentSource];
        const bool ornamentHit = ornamentSourceGate > 0.02f &&
            ornamentRng.chance(0.18f + 0.46f * mComposition.ornament + 0.16f * p.brightness);
        mComposition.ornamentRel[pos] = clampInt32(ornament, -8, 15);
        mComposition.ornamentGate[pos] = ornamentHit ? clamp01(0.22f + 0.48f * ornamentRng.uni()) : 0.0f;
        mComposition.ornamentDur[pos] = 0.10f + 0.34f * ornamentRng.uni();
    }

    // Preserve recognizable joints while leaving the secondary layers freer.
    mComposition.counterRel[15] = 0;
    mComposition.arpRel[0] = 0;
    mComposition.pulseRel[0] = 0;
    mComposition.ornamentRel[15] = 0;
}

void MusicEngine::generateTensionGrammar(const StyleProfile& p) {
    mComposition.tensionRel.fill(0);
    mComposition.tensionGate.fill(0.0f);
    mComposition.tensionDur.fill(0.0f);

    Rng rng(mComposition.tensionGrammarSeed);

    // A tension device describes a relationship among layers. It is not a
    // stored note sentence. Long pad breath is intentionally a rare result.
    auto chooseDevice = [&]() -> TensionDevice {
        const float weights[7] = {
            0.08f,                                      // None
            0.22f + 0.10f * p.space,                   // Vacuum
            0.23f + 0.10f * p.melody,                  // Convergence
            0.18f + 0.10f * p.sync,                    // Hinge
            0.17f + 0.10f * p.bass,                    // Shadow
            0.18f + 0.10f * mComposition.longMemory,   // Afterimage
            0.010f + 0.025f * p.space + 0.018f * p.texture // Pad Breath
        };
        float total = 0.0f;
        for (float w : weights) total += w;
        float target = rng.uni() * total;
        for (int32_t i = 0; i < 7; ++i) {
            target -= weights[i];
            if (target <= 0.0f) return static_cast<TensionDevice>(i);
        }
        return TensionDevice::Afterimage;
    };

    TensionDevice primary = chooseDevice();
    TensionDevice secondary = chooseDevice();
    if (primary == TensionDevice::None && secondary == TensionDevice::None) {
        secondary = TensionDevice::Convergence;
    }
    // Never create two sustained-pad identities in one sound.
    if (primary == TensionDevice::PadBreath && secondary == TensionDevice::PadBreath) {
        secondary = TensionDevice::Afterimage;
    }
    mComposition.tensionPrimary = static_cast<int32_t>(primary);
    mComposition.tensionSecondary = static_cast<int32_t>(secondary);

    static constexpr int32_t primeCycles[] = {5, 7, 11, 13};
    static constexpr int32_t coprimeStrides[] = {3, 5, 7, 9, 11, 13, 15};
    mComposition.tensionCycle = primeCycles[rng.rangeInt(0, 3)];
    mComposition.tensionPhase = rng.rangeInt(0, mComposition.tensionCycle - 1);
    mComposition.tensionStride = coprimeStrides[rng.rangeInt(0, 6)];
    mComposition.tensionWindow = rng.rangeInt(1, 3);
    mComposition.tensionDepth = clamp(0.20f + 0.42f * mComposition.deviceDepth +
                                      0.18f * p.drama + rng.bipolar() * 0.08f,
                                      0.16f, 0.86f);
    mComposition.padBreathRarity = clamp(0.006f + 0.016f * p.space +
                                         0.012f * p.texture + 0.008f * mComposition.useDrone,
                                         0.004f, 0.048f);

    // Most chords are expressed with short electronic gestures. A sustained
    // pad articulation exists only as an uncommon color, never as a floor.
    if (rng.chance(0.025f + 0.018f * p.space)) {
        mComposition.chordArticulation = 3;
    } else {
        static constexpr int32_t articulations[] = {0, 1, 2, 4};
        mComposition.chordArticulation = articulations[rng.rangeInt(0, 3)];
    }

    // Generate an asymmetric rhythmic word. Candidate positions are selected
    // by circular separation and explicitly penalized for reproducing the old
    // 2,6,10,14 lattice. Odd strides are coprime with 16, so repeated turns
    // visit the whole phrase before returning.
    bool used[kPhraseSteps] = {};
    const int32_t eventCount = clampInt32(2 + rng.rangeInt(0, 2) +
                                          (p.density > 0.72f ? 1 : 0) -
                                          (p.ambient ? 1 : 0), 2, 4);
    int32_t cursor = static_cast<int32_t>(mComposition.tensionGrammarSeed & 15u);
    for (int32_t event = 0; event < eventCount; ++event) {
        int32_t best = -1;
        float bestScore = -10000.0f;
        for (int32_t attempt = 0; attempt < 16; ++attempt) {
            const int32_t pos = (cursor + attempt * mComposition.tensionStride) & 15;
            if (used[pos]) continue;
            int32_t nearest = 16;
            for (int32_t j = 0; j < 16; ++j) {
                if (!used[j]) continue;
                const int32_t d = std::abs(pos - j);
                nearest = std::min(nearest, std::min(d, 16 - d));
            }
            if (event == 0) nearest = 4;
            float score = 0.34f * static_cast<float>(nearest) + rng.uni() * 0.55f;
            if (pos == 0 || pos == 8) score -= 0.18f;
            if (pos == 2 || pos == 6 || pos == 10 || pos == 14) score -= 1.10f;
            if (event > 0 && ((pos - cursor + 16) & 15) == 4) score -= 0.52f;
            if ((pos & 1) != 0) score += 0.12f * p.sync;
            if (score > bestScore) { bestScore = score; best = pos; }
        }
        if (best < 0) break;
        used[best] = true;
        cursor = (best + mComposition.tensionStride) & 15;

        const int32_t motifSource = (best + rng.rangeInt(0, 5)) & 15;
        int32_t rel = (event & 1) ? mComposition.motifG[motifSource]
                                  : mComposition.motifF[motifSource];
        rel = clampInt32(rel / 2 + rng.rangeInt(-2, 3), -5, 11);
        mComposition.tensionRel[best] = rel;
        mComposition.tensionGate[best] = clamp(0.34f + 0.48f * rng.uni(), 0.22f, 0.88f);
        mComposition.tensionDur[best] = 0.26f + 1.18f * rng.uni();
    }
}

void MusicEngine::generateTimbreGrammar(const StyleProfile& p) {
    // Derive timbre from the already-generated identity so extending the sound
    // bank does not consume the composition RNG or reduce the 48-candidate search.
    mComposition.timbreGrammarSeed = mComposition.leadGrammarSeed ^
        (mComposition.bassGrammarSeed << 11u | mComposition.bassGrammarSeed >> 21u) ^
        (mComposition.arcSeed << 23u | mComposition.arcSeed >> 9u) ^
        (mComposition.paletteHash * 0x85ebca6bu) ^ 0x7f4a7c15u;
    Rng rng(mComposition.timbreGrammarSeed);

    mComposition.bassModel = rng.rangeInt(0, 11);
    mComposition.leadModel = rng.rangeInt(0, 15);
    mComposition.padModel = rng.rangeInt(0, 9);
    mComposition.drumKit = rng.rangeInt(0, 7);

    // Channel/style character biases the instrument window without collapsing
    // it to a stored preset. Every seed still receives a distinct grammar.
    if (p.ambient) {
        mComposition.padModel = (mComposition.padModel + 2) % 10;
        mComposition.leadModel = (mComposition.leadModel + 2) % 16;
    }
    if (p.rough > 0.68f) {
        mComposition.bassModel = (mComposition.bassModel + 5) % 12;
        mComposition.drumKit = (mComposition.drumKit + 3) % 8;
    }
    if (p.brightness > 0.68f) {
        mComposition.leadModel = (mComposition.leadModel + 7) % 16;
        mComposition.padModel = (mComposition.padModel + 4) % 10;
    }

    mComposition.bassAttack = clamp(0.12f + 0.72f * rng.uni(), 0.08f, 0.92f);
    mComposition.bassRelease = clamp(0.48f + 0.40f * rng.uni(), 0.44f, 0.94f);
    mComposition.bassGlide = clamp(0.06f + 0.68f * rng.uni(), 0.04f, 0.82f);
    mComposition.bassPulseWidth = clamp(0.16f + 0.54f * rng.uni(), 0.12f, 0.76f);
    mComposition.bassMotion = clamp(0.12f + 0.72f * rng.uni(), 0.08f, 0.90f);

    mComposition.leadAttack = clamp(0.10f + 0.78f * rng.uni(), 0.06f, 0.94f);
    mComposition.leadRelease = clamp(0.42f + 0.50f * rng.uni(), 0.38f, 0.96f);
    mComposition.leadGlide = clamp(0.04f + 0.66f * rng.uni(), 0.02f, 0.80f);
    mComposition.leadVibratoDepth = clamp(0.02f + 0.72f * rng.uni(), 0.0f, 0.82f);
    mComposition.leadVibratoRate = clamp(0.08f + 0.84f * rng.uni(), 0.04f, 0.96f);
    mComposition.leadModRatio = clamp(0.06f + 0.88f * rng.uni(), 0.02f, 0.98f);
    mComposition.leadAir = clamp(0.02f + 0.62f * rng.uni(), 0.0f, 0.72f);

    mComposition.padAttack = clamp(0.20f + 0.72f * rng.uni(), 0.16f, 0.96f);
    mComposition.padRelease = clamp(0.58f + 0.36f * rng.uni(), 0.54f, 0.98f);
    mComposition.padDetune = clamp(0.04f + 0.70f * rng.uni(), 0.02f, 0.82f);
    mComposition.padMotion = clamp(0.06f + 0.76f * rng.uni(), 0.04f, 0.88f);
    mComposition.padWidth = clamp(0.22f + 0.70f * rng.uni(), 0.18f, 0.96f);
    mComposition.padVoiceCount = rng.chance(0.18f + 0.24f * p.chord) ? 4 :
                                 (rng.chance(0.26f + 0.18f * p.space) ? 2 : 3);

    // Generate a scale-degree voicing rather than reusing 0,2,4,6 in every sound.
    // Ascending constraints keep it harmonic while allowing broad variation.
    mComposition.padIntervals[0] = 0;
    int32_t cursor = 0;
    for (int32_t i = 1; i < 4; ++i) {
        const int32_t step = rng.rangeInt(1, (i == 3) ? 4 : 3);
        cursor = clampInt32(cursor + step, i, 11);
        mComposition.padIntervals[i] = cursor;
    }
    if (rng.chance(0.35f)) {
        mComposition.padIntervals[1] = clampInt32(mComposition.padIntervals[1] + 1, 1, 4);
    }
    for (int32_t i = 1; i < 4; ++i) {
        if (mComposition.padIntervals[i] <= mComposition.padIntervals[i - 1]) {
            mComposition.padIntervals[i] = mComposition.padIntervals[i - 1] + 1;
        }
    }

    mComposition.drumBody = clamp(0.14f + 0.78f * rng.uni(), 0.10f, 0.96f);
    mComposition.drumMetal = clamp(0.06f + 0.82f * rng.uni(), 0.02f, 0.94f);
    mComposition.drumNoise = clamp(0.10f + 0.78f * rng.uni(), 0.06f, 0.94f);

    // Make timbre part of the palette identity so session anti-repetition can
    // distinguish the same note grammar performed by different instruments.
    auto mix = [&](uint32_t v) {
        mComposition.paletteHash ^= v + 0x9e3779b9u +
            (mComposition.paletteHash << 6u) + (mComposition.paletteHash >> 2u);
        mComposition.paletteHash *= 0x85ebca6bu;
    };
    mix(mComposition.timbreGrammarSeed);
    mix(static_cast<uint32_t>(mComposition.bassModel));
    mix(static_cast<uint32_t>(mComposition.leadModel));
    mix(static_cast<uint32_t>(mComposition.padModel));
    mix(static_cast<uint32_t>(mComposition.drumKit));
    for (int32_t interval : mComposition.padIntervals) {
        mix(static_cast<uint32_t>(interval + 16));
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

    // Use a broad per-sound instrument window. Lanes are intensities, not simple on/off gates.
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
    mComposition = Composition{};
    // A Composition is a self-contained candidate. Its identity must not depend
    // on how many rejected candidates or previous songs the engine has visited.
    mComposition.generation = 1;
    mComposition.form.fill(PhraseType::Statement);
    chooseInstrumentPalette(p);

    // A live sound has no automatic endpoint. It keeps its seed identity and
    // evolves until the listener explicitly requests another sound. Offline
    // export overlays a finite boundary after reconstruction.
    mComposition.pieceSteps = kIndefinitePieceSteps;
    mComposition.conclusiveOutro = mRng.chance(0.34f + 0.22f * p.drama + 0.10f * p.chord);

    // Identity: generated once per sound and preserved so the sound remains recognizable.
    mComposition.arcSeed = mRng.nextU32();
    mComposition.leadGrammarSeed = mRng.nextU32();
    mComposition.bassGrammarSeed = mRng.nextU32();
    mComposition.counterGrammarSeed = mRng.nextU32();
    mComposition.arpGrammarSeed = mRng.nextU32();
    mComposition.pulseGrammarSeed = mRng.nextU32();
    mComposition.ornamentGrammarSeed = mRng.nextU32();
    mComposition.evolutionSeed = mRng.nextU32();
    // Tension grammar is derived rather than consuming another identity RNG
    // value. Candidate selection and established melody/bass identities remain
    // unchanged while the old fixed pad sentence is replaced at render time.
    mComposition.tensionGrammarSeed = mComposition.arcSeed ^
        (mComposition.leadGrammarSeed << 7u | mComposition.leadGrammarSeed >> 25u) ^
        (mComposition.bassGrammarSeed << 17u | mComposition.bassGrammarSeed >> 15u) ^
        (mComposition.evolutionSeed * 0x9e3779b9u) ^ 0x94d049bbu;
    mComposition.themeCount = mRng.rangeInt(4, 8);
    mComposition.recallCycle = mRng.rangeInt(5, 17);
    mComposition.dialogueCycle = mRng.rangeInt(3, 10);
    mComposition.counterShape = mRng.rangeInt(0, 0x7fff);
    mComposition.themeShapeId = mRng.rangeInt(0, 0x7fff);
    mComposition.longMemory = clamp(0.44f + p.melody * 0.32f + mRng.uni() * 0.30f, 0.34f, 0.99f);
    mComposition.callResponse = clamp(0.34f + p.melody * 0.42f + p.sync * 0.18f + mRng.bipolar() * 0.07f, 0.24f, 0.98f);
    mComposition.counterpoint = clamp(0.30f + p.melody * 0.34f + p.chord * 0.18f + mRng.uni() * 0.24f, 0.18f, 0.94f);
    mComposition.melodicGravity = clamp(0.60f + p.melody * 0.26f + p.chord * 0.12f, 0.50f, 0.98f);
    mComposition.phraseArc = clamp(0.34f + p.melodyRun * 0.34f + mRng.uni() * 0.22f, 0.20f, 0.94f);
    mComposition.layerDepth = clamp(0.48f + p.palette * 0.34f + p.density * 0.18f, 0.35f, 1.00f);
    mComposition.evolutionCycle = mRng.rangeInt(11, 29);
    mComposition.evolutionSpan = mRng.rangeInt(1, 4);
    // Evolution should be audible over time but almost impossible to locate at
    // a particular bar. The old 4-9 phrase path could move too quickly on a
    // small number of seeds. Longer morphs preserve continuity while still
    // allowing a held sound to travel far from its starting point.
    mComposition.evolutionDepth = clamp(0.18f + 0.38f * p.melody +
                                        0.18f * mComposition.longMemory,
                                        0.14f, 0.72f);
    mComposition.evolutionMorphPhrases = mRng.rangeInt(10, 24);
    mComposition.identityReturnCycle = mRng.rangeInt(48, 128);
    mComposition.verseMutationSpan = mRng.rangeInt(1, 3);
    mComposition.hookStability = clamp(0.78f + 0.16f * mComposition.longMemory + mRng.bipolar() * 0.05f, 0.72f, 0.98f);
    mComposition.verseFreedom = clamp(0.34f + 0.42f * p.melodyRun + 0.20f * p.sync + mRng.bipolar() * 0.08f, 0.24f, 0.94f);
    mComposition.layerMigration = clamp(0.30f + 0.34f * p.palette + 0.22f * mComposition.longMemory, 0.20f, 0.92f);
    mComposition.harmonyEvolution = clamp(0.18f + 0.30f * p.chord + 0.20f * p.melody, 0.12f, 0.70f);
    mComposition.counterDirection = mRng.chance(0.62f) ? -1 : 1;
    mComposition.counterInterval = mRng.rangeInt(2, 7);
    mComposition.counterDelay = mRng.rangeInt(1, 5);
    mComposition.arpStride = mRng.rangeInt(1, 7);
    if ((mComposition.arpStride & 1) == 0) ++mComposition.arpStride;
    mComposition.arpRotation = mRng.rangeInt(0, 15);

    int32_t themeWalk = 0;
    for (int32_t i = 0; i < kThemeSlots; ++i) {
        if (i == 0) themeWalk = 0;
        else {
            themeWalk += mRng.rangeInt(-2, 3);
            themeWalk = clampInt32(themeWalk, -3, 7);
            if (themeWalk == mComposition.themeOffset[i - 1]) themeWalk += mRng.chance(0.50f) ? 1 : -1;
        }
        mComposition.themeOffset[i] = clampInt32(themeWalk, -3, 7);
        mComposition.themeContour[i] = mRng.chance(0.50f) ? 1 : -1;
        mComposition.themeWeight[i] = clamp(0.50f + 0.42f * mRng.uni(), 0.36f, 0.98f);
    }
    mComposition.themeOffset[0] = 0;

    // Section scale belongs to the generated identity, not to the selected playback
    // or export duration. This keeps a captured seed sample-identical when rendered
    // for a different finite length.
    const int32_t baseSection = 5 + (p.ambient ? 2 : 0) + (p.melody > 0.72f ? 1 : 0) +
                                (p.halfTime ? 1 : 0) - (p.breakbeat ? 1 : 0);
    mComposition.sectionPhraseLength = std::max(4, std::min(18, baseSection + mRng.rangeInt(-1, 5)));
    mComposition.hookCycle = mRng.rangeInt(3, 7);

    // Development grammar: phrase roles are generated, not a stored song form.
    mComposition.formLength = mRng.chance(0.38f + 0.24f * mComposition.longMemory) ? 16 : (mRng.chance(0.48f) ? 12 : 8);
    for (int32_t i = 0; i < mComposition.formLength; ++i) {
        PhraseType role = PhraseType::Statement;
        if (i == 0) role = PhraseType::Statement;
        else if (i == mComposition.formLength - 1) role = mRng.chance(0.56f) ? PhraseType::Surge : PhraseType::Statement;
        else if ((i % mComposition.hookCycle) == 0) role = PhraseType::Hook;
        else {
            const float r = mRng.uni();
            if (r < 0.13f) role = PhraseType::Answer;
            else if (r < 0.24f) role = PhraseType::Variation;
            else if (r < 0.33f) role = PhraseType::Repeat;
            else if (r < 0.41f) role = PhraseType::Weave;
            else if (r < 0.48f) role = PhraseType::Tide;
            else if (r < 0.55f) role = PhraseType::Hinge;
            else if (r < 0.61f) role = PhraseType::Shadow;
            else if (r < 0.68f) role = PhraseType::Mirror;
            else if (r < 0.75f) role = PhraseType::Orbit;
            else if (r < 0.81f) role = PhraseType::Crystallize;
            else if (r < 0.86f) role = PhraseType::Afterimage;
            else if (r < 0.91f) role = PhraseType::Suspension;
            else if (r < 0.95f) role = PhraseType::Cascade;
            else if (r < 0.98f) role = PhraseType::Eclipse;
            else role = PhraseType::Surge;
        }
        mComposition.form[i] = role;
    }

    generateHarmonyGrammar(p);
    mComposition.hookOffset = mRng.chance(0.52f) ? 0 : mRng.rangeInt(-1, 2);
    mComposition.answerOffset = mRng.chance(0.54f) ? 0 : mRng.rangeInt(-2, 2);
    mComposition.bassAnswerShift = mRng.rangeInt(-1, 1);

    const bool lowRegisterStyle = p.bass > 0.76f || p.halfTime || (p.rough > 0.68f && p.brightness < 0.50f);
    const bool highRegisterStyle = p.brightness > 0.66f || p.melody > 0.76f;
    mComposition.octaveBias = lowRegisterStyle ? 2 : (highRegisterStyle ? 3 : 2);
    mComposition.motifGain = clamp(1.10f + p.melody * 1.04f + mRng.bipolar() * 0.09f, 0.82f, 2.12f);
    mComposition.bassGain = clamp(0.90f + p.bass * 0.72f + mRng.bipolar() * 0.08f, 0.64f, 1.56f);
    mComposition.chordGain = clamp(0.58f + p.chord * 0.78f + p.texture * 0.42f, 0.38f, 1.46f);
    mComposition.motifGain *= mComposition.hookEmphasis;
    mComposition.ornament = clamp(0.16f + p.melodyRun * 0.62f + p.sync * 0.22f, 0.08f, 0.86f);
    mComposition.leadSpace = clamp(p.space * 0.42f + (p.ambient ? 0.18f : 0.01f), 0.03f, 0.62f);

    generatePrimaryMotif(p);
    deriveRelatedMotifs();
    generateBassGrammar(p);
    generateSecondaryLayerGrammars(p);
    generateTensionGrammar(p);
    generateTimbreGrammar(p);

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
        mixMotif(static_cast<uint32_t>(mComposition.motifF[i] + 64));
        mixMotif(static_cast<uint32_t>(mComposition.motifG[i] + 64));
        mixMotif(static_cast<uint32_t>(std::lround(mComposition.gateA[i] * 31.0f)));
        mixMotif(static_cast<uint32_t>(mComposition.bassRel[i] + 64));
        mixMotif(static_cast<uint32_t>(std::lround(mComposition.bassGate[i] * 31.0f)));
        mixMotif(static_cast<uint32_t>(mComposition.bassVerseRel[i] + 64));
        mixMotif(static_cast<uint32_t>(mComposition.bassAnswerRel[i] + 64));
        mixMotif(static_cast<uint32_t>(mComposition.counterRel[i] + 64));
        mixMotif(static_cast<uint32_t>(mComposition.arpRel[i] + 64));
        mixMotif(static_cast<uint32_t>(mComposition.pulseRel[i] + 64));
        mixMotif(static_cast<uint32_t>(mComposition.ornamentRel[i] + 64));
    }
    for (int32_t v : mComposition.chordRoot) mixMotif(static_cast<uint32_t>(v + 64));
    mixMotif(mComposition.leadGrammarSeed); mixMotif(mComposition.bassGrammarSeed); mixMotif(mComposition.counterGrammarSeed);
    mixMotif(mComposition.arpGrammarSeed); mixMotif(mComposition.pulseGrammarSeed);
    mixMotif(mComposition.ornamentGrammarSeed); mixMotif(mComposition.evolutionSeed);
    mixMotif(mComposition.timbreGrammarSeed);
    mixMotif(static_cast<uint32_t>(mComposition.leadContour));
    mixMotif(static_cast<uint32_t>(mComposition.leadApexStep));
    mixMotif(static_cast<uint32_t>(mComposition.leadApexDegree + 32));
    mixMotif(static_cast<uint32_t>(mComposition.bassHitCount));
    mixMotif(static_cast<uint32_t>(mComposition.progressionId));
    mixMotif(static_cast<uint32_t>(mComposition.formLength));
    mixMotif(static_cast<uint32_t>(mComposition.evolutionCycle));
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
    Rng& rng = mDevelopmentRng;
    const int32_t pos = rng.rangeInt(0, kPatternSteps - 1);
    const int32_t track = rng.rangeInt(0, 4);
    const float delta = rng.bipolar() * (0.035f + 0.10f * mPattern.roughness + 0.04f * mNovelty);
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
        add(mComposition.motifF[i]);
        add(mComposition.motifG[i]);
        add(static_cast<int32_t>(std::lround(mComposition.gateA[i] * 15.0f)));
        add(static_cast<int32_t>(std::lround(mComposition.gateB[i] * 15.0f)));
        add(mComposition.bassRel[i]);
        add(static_cast<int32_t>(std::lround(mComposition.bassGate[i] * 15.0f)));
        add(mComposition.bassVerseRel[i]);
        add(mComposition.bassAnswerRel[i]);
        add(mComposition.counterRel[i]);
        add(mComposition.arpRel[i]);
        add(mComposition.pulseRel[i]);
        add(mComposition.ornamentRel[i]);
        add(static_cast<int32_t>(std::lround(mComposition.counterGate[i] * 15.0f)));
        add(static_cast<int32_t>(std::lround(mComposition.arpGate[i] * 15.0f)));
        add(static_cast<int32_t>(std::lround(mComposition.pulseGate[i] * 15.0f)));
        add(static_cast<int32_t>(std::lround(mComposition.ornamentGate[i] * 15.0f)));
    }
    for (int32_t i = 0; i < kMaxProgressionSlots; ++i) add(mComposition.chordRoot[i]);
    for (int32_t i = 0; i < kThemeSlots; ++i) {
        add(mComposition.themeOffset[i]);
        add(mComposition.themeContour[i]);
    }
    add(static_cast<int32_t>(mComposition.leadGrammarSeed));
    add(static_cast<int32_t>(mComposition.bassGrammarSeed));
    add(static_cast<int32_t>(mComposition.counterGrammarSeed));
    add(static_cast<int32_t>(mComposition.arpGrammarSeed));
    add(static_cast<int32_t>(mComposition.pulseGrammarSeed));
    add(static_cast<int32_t>(mComposition.ornamentGrammarSeed));
    add(static_cast<int32_t>(mComposition.evolutionSeed));
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
    if (mExportSinglePieceMode && mComposition.conclusiveOutro && step >= mComposition.pieceSteps - outroGravitySteps()) {
        return 0;
    }
    const int32_t progression = std::max(1, std::min(kMaxProgressionSlots, mComposition.progressionLength));
    int32_t bar = step / kPhraseSteps;
    while (bar < 0) bar += progression;
    int32_t root = mComposition.chordRoot[bar % progression];

    // Harmony evolves only away from the recurring hook. It returns to the
    // original progression during identity-return windows, so the sound keeps
    // an audible home even in an indefinitely held generation.
    const bool hookBar = mComposition.hookCycle > 0 &&
                         ((bar / std::max(1, mComposition.sectionPhraseLength)) %
                          mComposition.hookCycle) == 1;
    if (!hookBar && bar > 1) {
        const EvolutionFrame frame = evolutionFrameForStep(step);
        const uint32_t h0 = evolutionHash(frame.epoch, bar & 15, MelodyLayer::Lead, 0xc0a1u);
        const uint32_t h1 = evolutionHash(frame.epoch + 1, bar & 15, MelodyLayer::Lead, 0xc0a1u);
        const float p0 = static_cast<float>(h0 & 0xffffu) / 32767.5f - 1.0f;
        const float p1 = static_cast<float>(h1 & 0xffffu) / 32767.5f - 1.0f;
        const float path = p0 + (p1 - p0) * frame.phase;
        const float shift = frame.harmonyLens * (1.35f + 0.65f * path);
        root += clampInt32(static_cast<int32_t>(std::lround(shift)), -1, 1);
    }
    return clampInt32(root, -7, 12);
}

MusicEngine::SectionType MusicEngine::currentSectionType(int32_t step) const {
    const int32_t phrase = std::max(0, step) / kPhraseSteps;
    // Intro scale is part of identity and therefore independent of total duration.
    const int32_t introPhrases = std::max(1, std::min(4, mComposition.sectionPhraseLength / 3));

    if (phrase < introPhrases) return SectionType::Intro;
    if (mExportSinglePieceMode && mExportStopSamples > 0) {
        const int64_t nowSamples = mCurrentPieceSamples.load(std::memory_order_relaxed);
        const int64_t remainingSamples = mExportStopSamples - nowSamples;
        const int64_t outroWindow = std::max<int64_t>(static_cast<int64_t>(mSampleRate) / 2,
                std::min<int64_t>(static_cast<int64_t>(mSampleRate) * 2, mExportStopSamples / 20));
        if (remainingSamples <= outroWindow) return SectionType::Outro;
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

    const int32_t suspensionCut = 4 + static_cast<int32_t>(mComposition.deviceDepth * 7.0f);
    const int32_t shadowCut = suspensionCut + 5 + static_cast<int32_t>(mComposition.longMemory * 4.0f);
    const int32_t weaveCut = shadowCut + 6 + static_cast<int32_t>(mComposition.counterpoint * 5.0f);
    const int32_t tideCut = weaveCut + 5 + static_cast<int32_t>(mComposition.layerMigration * 5.0f);
    const int32_t hingeCut = tideCut + 5 + static_cast<int32_t>(mComposition.callResponse * 4.0f);
    const int32_t mirrorCut = hingeCut + 6 + static_cast<int32_t>(mComposition.paletteHash & 3u);
    const int32_t orbitCut = mirrorCut + 5 + static_cast<int32_t>(mPattern.texture * 5.0f);
    const int32_t variationCut = std::min(76, orbitCut + 8 + static_cast<int32_t>(mPattern.melody * 8.0f));
    const int32_t cascadeCut = 88 - static_cast<int32_t>(mComposition.drama * 8.0f);
    const int32_t surgeCut = 94 - static_cast<int32_t>(mComposition.drama * 14.0f);

    if (r < suspensionCut) return SectionType::Suspension;
    if (r < shadowCut) return SectionType::Shadow;
    if (r < weaveCut) return SectionType::Weave;
    if (r < tideCut) return SectionType::Tide;
    if (r < hingeCut) return SectionType::Hinge;
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
        case SectionType::Weave:
            if (local == 0 || local == 2) return PhraseType::Weave;
            if (local == 1) return PhraseType::Answer;
            return PhraseType::Hook;
        case SectionType::Tide:
            if (local == 0 || local == 3) return PhraseType::Tide;
            if (local == 1) return PhraseType::Statement;
            return PhraseType::Variation;
        case SectionType::Hinge:
            if (local == 0) return PhraseType::Statement;
            if (local == 1) return PhraseType::Hinge;
            if (local == 2) return PhraseType::Answer;
            return PhraseType::Hook;
        case SectionType::Shadow:
            if (local == 0 || local == 3) return PhraseType::Shadow;
            if (local == 1) return PhraseType::Afterimage;
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
            motif = &mComposition.motifG; gates = &mComposition.gateG; durs = &mComposition.durG;
            trans = ((phrasePos >= 8) ? -1 : 0);
            break;
        case PhraseType::Suspension:
            motif = &mComposition.motifA; gates = &mComposition.gateA; durs = &mComposition.durA;
            gateScale = 0.66f;
            trans = -1;
            break;
        case PhraseType::Hook:
            motif = &mComposition.motifF; gates = &mComposition.gateF; durs = &mComposition.durF;
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
        case PhraseType::Weave:
            motif = (phrasePos & 1) ? &mComposition.motifB : &mComposition.motifG;
            gates = (phrasePos & 1) ? &mComposition.gateB : &mComposition.gateG;
            durs = (phrasePos & 1) ? &mComposition.durB : &mComposition.durG;
            gateScale = 0.96f;
            trans = (phrasePos & 3) == 1 ? 1 : 0;
            break;
        case PhraseType::Tide:
            motif = &mComposition.motifG; gates = &mComposition.gateG; durs = &mComposition.durG;
            gateScale = 0.82f + 0.035f * static_cast<float>(phrasePos);
            trans = phrasePos < 8 ? -1 : 1;
            break;
        case PhraseType::Hinge:
            motif = phrasePos < 8 ? &mComposition.motifA : &mComposition.motifB;
            gates = phrasePos < 8 ? &mComposition.gateA : &mComposition.gateB;
            durs = phrasePos < 8 ? &mComposition.durA : &mComposition.durB;
            gateScale = 1.02f;
            trans = phrasePos < 8 ? 0 : mComposition.answerOffset;
            break;
        case PhraseType::Shadow:
            motif = &mComposition.motifE; gates = &mComposition.gateE; durs = &mComposition.durE;
            gateScale = 0.58f;
            trans = -2;
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
    if (mExportSinglePieceMode && mComposition.conclusiveOutro &&
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

uint32_t MusicEngine::evolutionHash(int32_t epoch,
                                    int32_t phrasePos,
                                    MelodyLayer layer,
                                    uint32_t salt) const {
    uint32_t h = mComposition.evolutionSeed ^ salt;
    h ^= static_cast<uint32_t>(epoch + 1) * 0x9e3779b9u;
    h ^= static_cast<uint32_t>(phrasePos + 17) * 0x85ebca6bu;
    h ^= static_cast<uint32_t>(static_cast<int32_t>(layer) + 3) * 0xc2b2ae35u;
    h ^= h >> 16u;
    h *= 0x7feb352du;
    h ^= h >> 15u;
    h *= 0x846ca68bu;
    h ^= h >> 16u;
    return h;
}

MusicEngine::EvolutionFrame MusicEngine::evolutionFrameForStep(int32_t step) const {
    EvolutionFrame frame;
    const float phraseTime = static_cast<float>(std::max(0, step)) /
                             static_cast<float>(kPhraseSteps);
    const float morphPhrases = static_cast<float>(std::max(3, mComposition.evolutionMorphPhrases));
    const float epochFloat = phraseTime / morphPhrases;
    const int32_t epoch = static_cast<int32_t>(std::floor(epochFloat));
    const float rawPhase = clamp(epochFloat - static_cast<float>(epoch), 0.0f, 1.0f);
    // Quintic smoothing has zero first and second derivatives at both ends.
    // Consecutive mutation paths therefore join without an audible corner.
    const float smoothPhase = rawPhase * rawPhase * rawPhase *
                              (rawPhase * (rawPhase * 6.0f - 15.0f) + 10.0f);
    frame.epoch = epoch;
    frame.phase = smoothPhase;

    auto bipolar = [&](int32_t whichEpoch, uint32_t salt) {
        if (whichEpoch <= 0) return 0.0f;
        const uint32_t h = evolutionHash(whichEpoch, 0, MelodyLayer::Lead, salt);
        const float u = static_cast<float>((h >> 8u) & 0x00ffffffu) /
                        static_cast<float>(0x00ffffffu);
        return u * 2.0f - 1.0f;
    };
    auto morph = [&](uint32_t salt) {
        const float a = bipolar(epoch, salt);
        const float b = bipolar(epoch + 1, salt);
        return a + (b - a) * smoothPhase;
    };

    // Every so often, the long-form development glides back toward the original
    // identity before leaving again. This produces recognition without looping.
    const float returnCycle = static_cast<float>(std::max(16, mComposition.identityReturnCycle));
    const float returnPhase = std::fmod(phraseTime, returnCycle) / returnCycle;
    const float returnDistance = std::min(returnPhase, 1.0f - returnPhase);
    // Identity returns are broad crossfades, not abrupt resets. A wider window
    // makes the long-form mutation nearly imperceptible from phrase to phrase.
    const float returnWidth = 0.20f;
    const float returnRaw = clamp(returnDistance / returnWidth, 0.0f, 1.0f);
    const float identityScale = returnRaw * returnRaw * returnRaw *
                                (returnRaw * (returnRaw * 6.0f - 15.0f) + 10.0f);
    const float depth = mComposition.evolutionDepth * identityScale;

    frame.leadMutation = morph(0x1001u) * depth;
    frame.bassMutation = morph(0x2003u) * depth * 0.72f;
    frame.rhythmMigration = morph(0x3007u) * depth;
    frame.densityDrift = morph(0x4009u) * depth;
    frame.registerTide = morph(0x5011u) * depth;
    frame.harmonyLens = morph(0x6013u) * depth * mComposition.harmonyEvolution;
    frame.paletteDrift = morph(0x7015u) * depth * mComposition.layerMigration;
    frame.braid = morph(0x8017u) * depth * mComposition.counterpoint;

    const uint32_t focusA = evolutionHash(epoch, 0, MelodyLayer::Lead, 0x9019u);
    const uint32_t focusB = evolutionHash(epoch + 1, 0, MelodyLayer::Lead, 0x9019u);
    const float focus = static_cast<float>(4 + static_cast<int32_t>(focusA % 8u)) +
                        (static_cast<float>(4 + static_cast<int32_t>(focusB % 8u)) -
                         static_cast<float>(4 + static_cast<int32_t>(focusA % 8u))) * smoothPhase;
    frame.focusPosition = clampInt32(static_cast<int32_t>(std::lround(focus)), 4, 11);
    const float directionA = (focusA & 0x100u) ? 1.0f : -1.0f;
    const float directionB = (focusB & 0x100u) ? 1.0f : -1.0f;
    frame.direction = (directionA + (directionB - directionA) * smoothPhase) >= 0.0f ? 1 : -1;
    return frame;
}

float MusicEngine::sectionVariationAmount(PhraseType phrase, SectionType section) const {
    float amount = 0.20f;
    switch (phrase) {
        case PhraseType::Hook: amount = 0.035f + (1.0f - mComposition.hookStability) * 0.28f; break;
        case PhraseType::Repeat: amount = 0.08f; break;
        case PhraseType::Statement: amount = 0.14f; break;
        case PhraseType::Answer: amount = 0.34f; break;
        case PhraseType::Variation: amount = 0.52f + 0.34f * mComposition.verseFreedom; break;
        case PhraseType::Suspension: amount = 0.22f; break;
        case PhraseType::Mirror: amount = 0.42f; break;
        case PhraseType::Orbit: amount = 0.34f; break;
        case PhraseType::Cascade: amount = 0.58f; break;
        case PhraseType::Crystallize: amount = 0.30f; break;
        case PhraseType::Eclipse: amount = 0.28f; break;
        case PhraseType::Afterimage: amount = 0.26f; break;
        case PhraseType::Weave: amount = 0.46f; break;
        case PhraseType::Tide: amount = 0.44f; break;
        case PhraseType::Hinge: amount = 0.48f; break;
        case PhraseType::Shadow: amount = 0.24f; break;
        case PhraseType::Surge: amount = 0.50f; break;
    }

    switch (section) {
        case SectionType::Hook: amount *= 0.52f; break;
        case SectionType::Intro: amount *= 0.65f; break;
        case SectionType::Outro: amount *= 0.45f; break;
        case SectionType::Variation: amount *= 1.16f; break;
        case SectionType::Weave: amount *= 1.10f; break;
        case SectionType::Tide: amount *= 1.06f; break;
        case SectionType::Hinge: amount *= 1.12f; break;
        case SectionType::Cascade: amount *= 1.18f; break;
        case SectionType::Surge: amount *= 1.14f; break;
        default: break;
    }
    return clamp01(amount);
}

int32_t MusicEngine::evolvedDegree(int32_t degree,
                                   int32_t step,
                                   int32_t phrasePos,
                                   int32_t chordRoot,
                                   PhraseType phrase,
                                   SectionType section,
                                   MelodyLayer layer) const {
    phrasePos &= 15;
    const bool identityAnchor = phrasePos == 0 || phrasePos == 8 || phrasePos == 15;
    if (identityAnchor && (phrase == PhraseType::Hook || section == SectionType::Hook)) {
        return (phrasePos == 8) ? degree : chordRoot;
    }

    const EvolutionFrame frame = evolutionFrameForStep(step);
    float layerMotion = frame.leadMutation;
    float layerScale = 1.0f;
    switch (layer) {
        case MelodyLayer::Bass: layerMotion = frame.bassMutation; layerScale = 0.68f; break;
        case MelodyLayer::Counter: layerMotion = frame.leadMutation + 0.45f * frame.braid; layerScale = 1.05f; break;
        case MelodyLayer::Arp: layerMotion = frame.leadMutation + 0.30f * frame.rhythmMigration; layerScale = 0.88f; break;
        case MelodyLayer::Pulse: layerMotion = 0.55f * frame.leadMutation + 0.45f * frame.rhythmMigration; layerScale = 0.70f; break;
        case MelodyLayer::Ornament: layerMotion = frame.leadMutation + 0.60f * frame.registerTide; layerScale = 1.25f; break;
        case MelodyLayer::Lead: default: break;
    }

    const float variation = sectionVariationAmount(phrase, section) * layerScale;
    const uint32_t h0 = evolutionHash(frame.epoch, phrasePos, layer, 0xa11ce5u);
    const uint32_t h1 = evolutionHash(frame.epoch + 1, phrasePos, layer, 0xa11ce5u);
    const float path0 = static_cast<float>(h0 & 0xffffu) / 32767.5f - 1.0f;
    const float path1 = static_cast<float>(h1 & 0xffffu) / 32767.5f - 1.0f;
    const float path = path0 + (path1 - path0) * frame.phase;
    int32_t rel = degree - chordRoot;
    const int32_t maxShift = layer == MelodyLayer::Ornament ? 3 :
                             ((layer == MelodyLayer::Bass || layer == MelodyLayer::Pulse) ? 1 : 2);
    const float anchorScale = identityAnchor ? 0.18f : 1.0f;
    const float pathMotion = (0.70f * layerMotion +
                              0.30f * path * std::abs(layerMotion));
    const float shiftFloat = pathMotion * static_cast<float>(maxShift + 1) *
                             variation * anchorScale;
    rel += clampInt32(static_cast<int32_t>(std::lround(shiftFloat)), -maxShift, maxShift);

    if (!identityAnchor) {
        const uint32_t lens0 = evolutionHash(frame.epoch, phrasePos, layer, 0xc011abu);
        const uint32_t lens1 = evolutionHash(frame.epoch + 1, phrasePos, layer, 0xc011abu);
        const float lp0 = static_cast<float>(lens0 & 0xffffu) / 32767.5f - 1.0f;
        const float lp1 = static_cast<float>(lens1 & 0xffffu) / 32767.5f - 1.0f;
        const float lensPath = lp0 + (lp1 - lp0) * frame.phase;
        const float harmonyShift = frame.harmonyLens * (0.72f + 0.28f * lensPath) *
                                   variation * 2.2f;
        rel += clampInt32(static_cast<int32_t>(std::lround(harmonyShift)), -1, 1);
    }

    // Original devices: each changes relationships rather than imitating a
    // conventional verse/build/drop template.
    if (section == SectionType::Weave || phrase == PhraseType::Weave) {
        rel += ((phrasePos & 1) ? -1 : 1) * (frame.braid >= 0.0f ? 1 : -1);
    } else if (section == SectionType::Tide || phrase == PhraseType::Tide) {
        const int32_t arc = phrasePos < 8 ? phrasePos : 15 - phrasePos;
        if (arc >= 4) rel += static_cast<int32_t>(std::lround(frame.registerTide * 2.0f));
    } else if (section == SectionType::Hinge || phrase == PhraseType::Hinge) {
        if (phrasePos >= frame.focusPosition) rel += frame.direction;
    } else if (section == SectionType::Shadow || phrase == PhraseType::Shadow) {
        rel -= (layer == MelodyLayer::Bass) ? 0 : 2;
    }

    if (phrasePos == 14 && std::abs(rel) > 6) rel = rel > 0 ? 4 : -2;
    if (phrasePos == 15 && mComposition.melodicGravity > 0.52f) rel = 0;
    return chordRoot + clampInt32(rel, -8, 15);
}

float MusicEngine::evolvedGate(float gate,
                               int32_t step,
                               int32_t phrasePos,
                               PhraseType phrase,
                               SectionType section,
                               MelodyLayer layer) const {
    phrasePos &= 15;
    const EvolutionFrame frame = evolutionFrameForStep(step);
    const float variation = sectionVariationAmount(phrase, section);
    const uint32_t h0 = evolutionHash(frame.epoch, phrasePos, layer, 0xbad5eedu);
    const uint32_t h1 = evolutionHash(frame.epoch + 1, phrasePos, layer, 0xbad5eedu);
    const float p0 = static_cast<float>(h0 & 0xffffu) / 32767.5f - 1.0f;
    const float p1 = static_cast<float>(h1 & 0xffffu) / 32767.5f - 1.0f;
    const float path = p0 + (p1 - p0) * frame.phase;
    const bool strong = phrasePos == 0 || phrasePos == 8 || phrasePos == 15;
    float result = gate;

    if (!(phrase == PhraseType::Hook && strong)) {
        result *= clamp(1.0f + frame.densityDrift * (0.20f + 0.35f * variation), 0.62f, 1.34f);
        const float rhythmMotion = frame.rhythmMigration * path * variation;
        if (result > 0.02f && !strong) {
            result *= clamp(1.0f + 0.62f * rhythmMotion, 0.42f, 1.42f);
        } else if (result <= 0.02f && !strong) {
            // New notes fade into a phrase instead of appearing at an epoch edge.
            const float emergence = std::max(0.0f, rhythmMotion - 0.16f);
            result = clamp01(emergence * 0.82f);
        }
    }

    if (section == SectionType::Weave && ((phrasePos + static_cast<int32_t>(layer)) & 1)) result *= 0.72f;
    if (section == SectionType::Shadow) result *= strong ? 0.72f : 0.48f;
    if (section == SectionType::Tide) {
        const float tide = static_cast<float>(phrasePos) / 15.0f;
        result *= clamp(0.72f + 0.58f * tide + 0.12f * frame.densityDrift, 0.52f, 1.34f);
    }
    if (strong && result > 0.02f) result = std::max(result, phrase == PhraseType::Hook ? 0.68f : 0.42f);
    return clamp01(result);
}

int32_t MusicEngine::generatedLayerDegree(MelodyLayer layer,
                                          int32_t step,
                                          int32_t phrasePos,
                                          int32_t chordRoot,
                                          PhraseType phrase,
                                          SectionType section) const {
    phrasePos &= 15;
    int32_t rel = 0;
    switch (layer) {
        case MelodyLayer::Bass:
            if (phrase == PhraseType::Answer || phrase == PhraseType::Hinge || phrase == PhraseType::Shadow ||
                section == SectionType::Hinge || section == SectionType::Shadow) {
                rel = mComposition.bassAnswerRel[phrasePos];
            } else if (phrase == PhraseType::Variation || phrase == PhraseType::Weave || phrase == PhraseType::Tide ||
                       phrase == PhraseType::Cascade || phrase == PhraseType::Surge ||
                       section == SectionType::Variation || section == SectionType::Weave || section == SectionType::Tide) {
                rel = mComposition.bassVerseRel[phrasePos];
            } else {
                rel = mComposition.bassRel[phrasePos];
            }
            break;
        case MelodyLayer::Counter: rel = mComposition.counterRel[phrasePos]; break;
        case MelodyLayer::Arp: rel = mComposition.arpRel[phrasePos]; break;
        case MelodyLayer::Pulse: rel = mComposition.pulseRel[phrasePos]; break;
        case MelodyLayer::Ornament: rel = mComposition.ornamentRel[phrasePos]; break;
        case MelodyLayer::Lead: rel = mComposition.motifA[phrasePos]; break;
    }
    return evolvedDegree(chordRoot + rel, step, phrasePos, chordRoot, phrase, section, layer);
}

float MusicEngine::generatedLayerGate(MelodyLayer layer,
                                      int32_t step,
                                      int32_t phrasePos,
                                      PhraseType phrase,
                                      SectionType section) const {
    phrasePos &= 15;
    float gate = 0.0f;
    switch (layer) {
        case MelodyLayer::Bass:
            if (phrase == PhraseType::Answer || phrase == PhraseType::Hinge || phrase == PhraseType::Shadow ||
                section == SectionType::Hinge || section == SectionType::Shadow) {
                gate = mComposition.bassAnswerGate[phrasePos];
            } else if (phrase == PhraseType::Variation || phrase == PhraseType::Weave || phrase == PhraseType::Tide ||
                       phrase == PhraseType::Cascade || phrase == PhraseType::Surge ||
                       section == SectionType::Variation || section == SectionType::Weave || section == SectionType::Tide) {
                gate = mComposition.bassVerseGate[phrasePos];
            } else {
                gate = mComposition.bassGate[phrasePos];
            }
            break;
        case MelodyLayer::Counter: gate = mComposition.counterGate[phrasePos]; break;
        case MelodyLayer::Arp: gate = mComposition.arpGate[phrasePos]; break;
        case MelodyLayer::Pulse: gate = mComposition.pulseGate[phrasePos]; break;
        case MelodyLayer::Ornament: gate = mComposition.ornamentGate[phrasePos]; break;
        case MelodyLayer::Lead: gate = mComposition.gateA[phrasePos]; break;
    }
    return evolvedGate(gate, step, phrasePos, phrase, section, layer);
}

float MusicEngine::generatedLayerDur(MelodyLayer layer, int32_t phrasePos) const {
    phrasePos &= 15;
    switch (layer) {
        case MelodyLayer::Counter: return std::max(0.12f, mComposition.counterDur[phrasePos]);
        case MelodyLayer::Arp: return std::max(0.10f, mComposition.arpDur[phrasePos]);
        case MelodyLayer::Pulse: return std::max(0.10f, mComposition.pulseDur[phrasePos]);
        case MelodyLayer::Ornament: return std::max(0.08f, mComposition.ornamentDur[phrasePos]);
        case MelodyLayer::Lead: return std::max(0.15f, mComposition.durA[phrasePos]);
        case MelodyLayer::Bass: default: return 0.72f;
    }
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
    } else if (phrase == PhraseType::Variation || phrase == PhraseType::Cascade || phrase == PhraseType::Tide) {
        rel += offset + ((phrasePos < 8) ? contour : -contour);
    } else if (phrase == PhraseType::Mirror) {
        rel = offset - rel / 2;
    } else if (phrase == PhraseType::Orbit) {
        rel += (phrasePos < 8) ? offset : -offset / 2;
    } else if (phrase == PhraseType::Weave) {
        rel += offset + ((phrasePos & 1) ? -contour : contour);
    } else if (phrase == PhraseType::Hinge) {
        rel += phrasePos < 8 ? offset / 2 : offset - contour;
    } else if (phrase == PhraseType::Shadow) {
        rel += offset / 2 - 2;
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
    const SectionType section = currentSectionType(step);
    return evolvedDegree(chordRoot + clampInt32(rel, -7, 14), step, phrasePos,
                         chordRoot, phrase, section, MelodyLayer::Lead);
}


int32_t MusicEngine::counterpointDegree(int32_t step, int32_t phrasePos, int32_t chordRoot) const {
    const PhraseType phrase = currentPhraseType(step);
    const SectionType section = currentSectionType(step);
    return generatedLayerDegree(MelodyLayer::Counter, step, phrasePos, chordRoot, phrase, section);
}


int32_t MusicEngine::bassDegreeForStep(int32_t step,
                                      int32_t phrasePos,
                                      int32_t chordRoot,
                                      int32_t nextChordRoot,
                                      PhraseType phrase,
                                      SectionType section) const {
    phrasePos &= (kPhraseSteps - 1);
    int32_t degree = generatedLayerDegree(MelodyLayer::Bass, step, phrasePos,
                                          chordRoot, phrase, section);
    if (phrasePos == 15) {
        int32_t rel = degree - chordRoot;
        if (nextChordRoot > chordRoot) rel = std::max(-1, nextChordRoot - chordRoot - 1);
        else if (nextChordRoot < chordRoot) rel = std::min(1, nextChordRoot - chordRoot + 1);
        degree = chordRoot + rel;
    }
    return degree;
}

MusicEngine::TensionDevice MusicEngine::tensionDeviceForPhrase(int32_t phraseNumber,
                                                                SectionType section) const {
    const bool structuralEdge = section == SectionType::Suspension ||
                                section == SectionType::Orbit ||
                                section == SectionType::Crystallize ||
                                section == SectionType::Eclipse ||
                                section == SectionType::Afterimage ||
                                section == SectionType::Hinge ||
                                section == SectionType::Shadow;
    const bool grammarTurn = ((phraseNumber + mComposition.tensionPhase) %
                              std::max(2, mComposition.tensionCycle)) == 0;
    if (!structuralEdge && !grammarTurn) return TensionDevice::None;

    const bool secondary = ((phraseNumber / std::max(1, mComposition.tensionCycle)) & 1) != 0;
    const int32_t raw = secondary ? mComposition.tensionSecondary : mComposition.tensionPrimary;
    return static_cast<TensionDevice>(clampInt32(raw, 0, 6));
}

int32_t MusicEngine::tensionFocusForPhrase(int32_t phraseNumber) const {
    return (mComposition.tensionPhase + phraseNumber * mComposition.tensionStride) & 15;
}

float MusicEngine::tensionGateForStep(int32_t phraseNumber, int32_t phrasePos) const {
    phrasePos &= 15;
    const int32_t rotation = tensionFocusForPhrase(phraseNumber);
    const int32_t source = (phrasePos + rotation) & 15;
    float gate = mComposition.tensionGate[source];
    const int32_t focus = rotation;
    int32_t d = std::abs(phrasePos - focus);
    d = std::min(d, 16 - d);
    if (d <= mComposition.tensionWindow) {
        gate = std::max(gate, (0.18f + 0.16f * static_cast<float>(mComposition.tensionWindow - d)) *
                              mComposition.tensionDepth);
    }
    return clamp01(gate);
}


void MusicEngine::clearVoicesAndEvents() {
    for (auto& v : mDrums) v = DrumVoice{};
    for (auto& v : mBass) v = BassVoice{};
    for (auto& v : mPads) v = PadVoice{};
    for (auto& v : mLeads) v = LeadVoice{};
    for (auto& e : mEvents) e = ScheduledEvent{};
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
    // Manual Next preserves the full composition search.
    generateSeededSong(mPendingSongSeed);
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
    const EvolutionFrame evolution = evolutionFrameForStep(pieceStep);

    if ((p16 == 0) && mDevelopmentRng.chance(0.18f + 0.16f * p.rough)) {
        mutateDrumsOnly();
    }
    if (mDevelopmentRng.chance(0.0045f + 0.0060f * p.rough + 0.0040f * mNovelty)) {
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
        case PhraseType::Weave:
            phraseDrumScale = 0.92f;
            phraseBassScale = 0.98f;
            phraseLeadScale = 1.10f;
            phraseChordScale = 0.96f;
            break;
        case PhraseType::Tide:
            phraseDrumScale = 0.86f;
            phraseBassScale = 1.04f;
            phraseLeadScale = 1.02f;
            phraseChordScale = 1.12f;
            break;
        case PhraseType::Hinge:
            phraseDrumScale = p16 < evolution.focusPosition ? 0.88f : 1.06f;
            phraseBassScale = p16 < evolution.focusPosition ? 0.96f : 1.08f;
            phraseLeadScale = p16 < evolution.focusPosition ? 0.94f : 1.12f;
            phraseChordScale = 1.04f;
            break;
        case PhraseType::Shadow:
            phraseDrumScale = 0.58f;
            phraseBassScale = 0.82f;
            phraseLeadScale = 0.72f;
            phraseChordScale = 1.22f;
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
        case SectionType::Weave:
            phraseDrumScale *= 0.94f;
            phraseBassScale *= 0.98f;
            phraseLeadScale *= 1.12f;
            phraseChordScale *= 0.96f;
            break;
        case SectionType::Tide:
            phraseDrumScale *= 0.86f + 0.18f * static_cast<float>(p16) / 15.0f;
            phraseBassScale *= 1.00f + 0.08f * evolution.registerTide;
            phraseLeadScale *= 1.02f + 0.10f * std::abs(evolution.registerTide);
            phraseChordScale *= 1.10f;
            break;
        case SectionType::Hinge:
            phraseDrumScale *= p16 < evolution.focusPosition ? 0.90f : 1.08f;
            phraseBassScale *= p16 < evolution.focusPosition ? 0.96f : 1.08f;
            phraseLeadScale *= p16 < evolution.focusPosition ? 0.94f : 1.12f;
            break;
        case SectionType::Shadow:
            phraseDrumScale *= 0.62f;
            phraseBassScale *= 0.84f;
            phraseLeadScale *= 0.74f;
            phraseChordScale *= 1.20f;
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

    // The current sound evolves as a coordinated system. These factors move
    // slowly and continuously; they do not replace its seed identity.
    phraseDrumScale *= clamp(1.0f + 0.12f * evolution.densityDrift - 0.04f * evolution.paletteDrift,
                             0.78f, 1.22f);
    phraseBassScale *= clamp(1.0f + 0.10f * evolution.bassMutation + 0.06f * evolution.densityDrift,
                             0.80f, 1.20f);
    phraseLeadScale *= clamp(1.0f + 0.12f * evolution.leadMutation + 0.08f * evolution.paletteDrift,
                             0.78f, 1.24f);
    phraseChordScale *= clamp(1.0f + 0.10f * evolution.harmonyLens - 0.05f * evolution.densityDrift,
                              0.82f, 1.22f);

    const int32_t phraseNumber = pieceStep / kPhraseSteps;
    const TensionDevice tensionDevice = tensionDeviceForPhrase(phraseNumber, section);
    const int32_t tensionFocus = tensionFocusForPhrase(phraseNumber);
    const int32_t tensionDistance0 = std::abs(p16 - tensionFocus);
    const int32_t tensionDistance = std::min(tensionDistance0, 16 - tensionDistance0);
    const float tensionShape = tensionDevice == TensionDevice::None ? 0.0f :
        clamp01((1.0f - static_cast<float>(tensionDistance) /
                 static_cast<float>(std::max(2, mComposition.tensionWindow + 3))) *
                mComposition.tensionDepth);
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

    // Original tension devices act through relationships among layers. They do
    // not all mean "play the same low pad again".
    switch (tensionDevice) {
        case TensionDevice::Vacuum:
            phraseDrumScale *= clamp(1.0f - 0.48f * tensionShape, 0.46f, 1.0f);
            phraseChordScale *= clamp(1.0f - 0.24f * tensionShape, 0.66f, 1.0f);
            phraseLeadScale *= 0.96f + 0.12f * tensionShape;
            break;
        case TensionDevice::Convergence:
            phraseDrumScale *= 0.94f;
            phraseLeadScale *= 1.0f + 0.18f * tensionShape;
            phraseChordScale *= 0.96f;
            break;
        case TensionDevice::Hinge:
            phraseDrumScale *= (p16 < tensionFocus) ? 0.90f : 1.06f;
            phraseBassScale *= (p16 < tensionFocus) ? 0.96f : 1.05f;
            phraseLeadScale *= (p16 < tensionFocus) ? 0.94f : 1.09f;
            break;
        case TensionDevice::Shadow:
            phraseLeadScale *= clamp(1.0f - 0.34f * tensionShape, 0.58f, 1.0f);
            phraseBassScale *= 0.98f + 0.12f * tensionShape;
            phraseChordScale *= clamp(1.0f - 0.20f * tensionShape, 0.72f, 1.0f);
            break;
        case TensionDevice::Afterimage:
            phraseDrumScale *= 0.90f;
            phraseLeadScale *= 0.92f + 0.14f * tensionShape;
            break;
        case TensionDevice::PadBreath:
            phraseDrumScale *= clamp(1.0f - 0.38f * tensionShape, 0.56f, 1.0f);
            phraseBassScale *= 0.96f;
            phraseLeadScale *= 0.90f;
            break;
        case TensionDevice::None:
        default:
            break;
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

    const bool needsBassFloor = (section == SectionType::Intro || section == SectionType::Suspension ||
                                 section == SectionType::Orbit || section == SectionType::Shadow ||
                                 section == SectionType::Outro);
    const bool bassFloorPulse = needsBassFloor && (p16 == 0 || p16 == 8);
    const bool anchorPulse = downbeat && !p.ambient;

    float bassGate = clamp01(generatedLayerGate(MelodyLayer::Bass, pieceStep, p16, phrase, section) *
                              mComposition.bassGain * phraseBassScale * mComposition.useBass);
    if ((bassFloorPulse || anchorPulse) && mComposition.useBass > 0.0f) {
        bassGate = std::max(bassGate, p.ambient ? 0.30f : 0.44f);
    }
    if (bassGate > 0.02f && (bassFloorPulse || anchorPulse || mRng.chance(clamp01(bassGate * (0.84f + 0.14f * accent))))) {
        const int32_t degree = bassDegreeForStep(pieceStep, p16, chordRoot, nextChordRoot, phrase, section);
        const int32_t octave = (mPattern.style == StyleType::GlassNoir || mPattern.style == StyleType::SubOrbit || mPattern.style == StyleType::DeepMagnet || mPattern.style == StyleType::MarbleBass) ? -1 : 0;
        const float freq = midiToHz(static_cast<float>(scaleDegreeToMidi(degree, octave)));
        float dur = stepDurationSeconds() * (p.halfTime ? 2.8f : 1.5f);
        if (downbeat) dur *= p.ambient ? 5.0f : 1.65f;
        scheduleBass(humanizeSamples(0.18f, downbeat, false), freq,
                     (0.25f + 0.27f * mPattern.energy) * phraseBassScale,
                     dur, mRng.bipolar() * 0.045f,
                     clamp01(mComposition.bassTone + 0.055f * evolution.paletteDrift +
                             mRng.bipolar() * 0.045f + p.rough * 0.04f));
        mLastBassStep = static_cast<int32_t>(mStepIndex);
        eventHappened = true;
    }

    float chordGate = clamp01(mComposition.chordGate[p16] * mComposition.chordGain *
                              phraseChordScale * mComposition.useChord);
    if (anchorPulse && mComposition.useChord > 0.0f) {
        chordGate = std::max(chordGate, 0.10f);
    }
    if (chordGate > 0.02f && mRng.chance(chordGate)) {
        const float dur = stepDurationSeconds() * (0.72f + 1.30f * p.space +
                                                   0.46f * mComposition.longMemory);
        const float amp = (0.030f + 0.070f * mPattern.texture + 0.055f * p.chord) *
                          accent * phraseChordScale;
        scheduleChordGesture(humanizeSamples(0.28f, downbeat, false), chordRoot,
                             amp, dur, mRng.bipolar() * 0.42f,
                             clamp01(mComposition.padTone + mRng.bipolar() * 0.04f),
                             mComposition.chordArticulation);
        eventHappened = true;
    }

    bool leadRest = false;
    float leadGate = 0.0f;
    float leadDurSteps = 0.0f;
    int32_t leadDegree = grammarDegree(phrase, p16, chordRoot, leadRest, leadGate, leadDurSteps);
    leadDegree = applyThemeTransform(leadDegree, pieceStep, p16, chordRoot, phrase);
    leadGate = evolvedGate(leadGate, pieceStep, p16, phrase, section, MelodyLayer::Lead);
    leadGate = clamp01(leadGate * phraseLeadScale * (1.0f - 0.18f * mComposition.leadSpace) * mComposition.useLead);
    if (!leadRest && leadGate > 0.04f && mRng.chance(clamp01(0.72f + 0.26f * leadGate))) {
        int32_t octave = mComposition.octaveBias;
        if ((phrase == PhraseType::Surge || phrase == PhraseType::Cascade || phrase == PhraseType::Hook) && mRng.chance(0.35f)) octave += 1;
        if (phrase == PhraseType::Suspension || phrase == PhraseType::Shadow) octave = std::max(1, octave - 1);
        if (phrase == PhraseType::Tide || section == SectionType::Tide) {
            octave += static_cast<int32_t>(std::lround(evolution.registerTide));
        }
        const float freq = midiToHz(static_cast<float>(scaleDegreeToMidi(leadDegree, octave)));
        const float dur = stepDurationSeconds() * (0.60f + leadDurSteps * (p.ambient ? 3.4f : 1.75f));
        scheduleLead(humanizeSamples(0.55f, false, false), freq,
                     (0.055f + 0.12f * mPattern.melody + 0.05f * p.melodyRun) * accent * phraseLeadScale,
                     dur, mRng.bipolar() * 0.62f,
                     clamp01(mComposition.leadTone + 0.060f * evolution.paletteDrift +
                             ((phrase == PhraseType::Surge || phrase == PhraseType::Cascade) ? 0.08f : 0.0f) +
                             mRng.bipolar() * 0.035f));
        if (phrase != PhraseType::Suspension && mRng.chance(mComposition.ornament * leadGate)) {
            const int32_t neighbor = generatedLayerDegree(MelodyLayer::Ornament, pieceStep, p16,
                                                          chordRoot, phrase, section);
            const float ornamentGate = generatedLayerGate(MelodyLayer::Ornament, pieceStep, p16,
                                                           phrase, section);
            const float f2 = midiToHz(static_cast<float>(scaleDegreeToMidi(neighbor, octave)));
            if (ornamentGate > 0.02f) {
                scheduleLead(static_cast<int32_t>(stepSamples * (0.45f + 0.24f * mRng.uni())), f2,
                             (0.022f + 0.046f * mPattern.melody) * accent * phraseLeadScale * ornamentGate,
                             stepDurationSeconds() * generatedLayerDur(MelodyLayer::Ornament, p16),
                             mRng.bipolar() * 0.66f,
                             clamp01(mComposition.leadTone + 0.04f + 0.05f * evolution.paletteDrift +
                                     mRng.bipolar() * 0.035f));
            }
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
        const float arpGate = generatedLayerGate(MelodyLayer::Arp, pieceStep, p16, phrase, section);
        const bool arpPulse = arpGate > 0.02f || offEighth ||
                              ((p16 & 3) == 1 && (section == SectionType::Hook ||
                                                 section == SectionType::Surge || section == SectionType::Cascade));
        const float arpP = arpGate * (0.42f + 0.48f * p.melodyRun + 0.18f * mPattern.density) *
                           phraseLeadScale * mComposition.useArp;
        if (arpPulse && mRng.chance(clamp01(arpP))) {
            const int32_t degree = generatedLayerDegree(MelodyLayer::Arp, pieceStep, p16,
                                                        chordRoot, phrase, section);
            const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(degree, mComposition.octaveBias + 1)));
            scheduleLead(humanizeSamples(0.42f, false, false), f,
                         (0.020f + 0.056f * p.brightness + 0.020f * mPattern.melody) * accent * phraseLeadScale * mComposition.useArp,
                         stepDurationSeconds() * generatedLayerDur(MelodyLayer::Arp, p16),
                         mRng.bipolar() * 0.78f,
                         clamp01(mComposition.arpTone + 0.05f * evolution.paletteDrift + mRng.bipolar() * 0.030f));
            eventHappened = true;
        }
    }

    if (mComposition.useCounter > 0.02f && phrase != PhraseType::Suspension) {
        const float gate2 = generatedLayerGate(MelodyLayer::Counter, pieceStep, p16, phrase, section);
        const int32_t deg2 = generatedLayerDegree(MelodyLayer::Counter, pieceStep, p16,
                                                  chordRoot, phrase, section);
        const float dur2 = generatedLayerDur(MelodyLayer::Counter, p16);
        const float counterP = clamp01(gate2 * (0.38f + 0.50f * mComposition.useCounter +
                                                0.18f * mComposition.callResponse) * phraseLeadScale);
        if (gate2 > 0.02f && mRng.chance(counterP)) {
            const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(deg2, std::max(1, mComposition.octaveBias - 1))));
            scheduleLead(humanizeSamples(0.62f, false, false), f,
                         (0.020f + 0.052f * mPattern.melody) * accent * phraseLeadScale * mComposition.useCounter,
                         stepDurationSeconds() * (0.52f + 0.80f * dur2),
                         mRng.bipolar() * 0.70f,
                         clamp01(mComposition.counterTone + 0.05f * evolution.paletteDrift + mRng.bipolar() * 0.034f));
            eventHappened = true;
        }
    }

    // The former fixed Stab and section-boundary Drone paths both used the
    // same PadVoice and produced a recognizable DOO lattice. Tension is now
    // generated from the current seed and expressed through several distinct
    // mathematical relationships.
    const float pressureGate = tensionGateForStep(phraseNumber, p16) *
        clamp01(0.32f + 0.38f * mComposition.useStab + 0.24f * mComposition.useDrone +
                0.18f * mComposition.deviceDepth);
    if (pressureGate > 0.02f && tensionDevice != TensionDevice::None &&
        mRng.chance(clamp01(pressureGate * phraseChordScale))) {
        const int32_t source = (p16 + tensionFocus) & 15;
        const int32_t rel = mComposition.tensionRel[source];
        const float duration = stepDurationSeconds() *
            std::max(0.22f, mComposition.tensionDur[source]);
        const float pressureAmp = (0.015f + 0.042f * mComposition.tensionDepth) *
                                  accent * (0.58f + 0.42f * pressureGate);

        switch (tensionDevice) {
            case TensionDevice::Vacuum:
                // Vacuum creates tension by omission. A small high marker makes
                // the missing center perceptible without replacing it with a pad.
                if (p16 == tensionFocus && mComposition.useSheen > 0.02f) {
                    const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(chordRoot + rel + 7,
                                                                                 mComposition.octaveBias + 1)));
                    scheduleLead(static_cast<int32_t>(stepSamples * 0.58f), f,
                                 pressureAmp * 0.42f, duration * 0.34f,
                                 mRng.bipolar() * 0.90f,
                                 clamp01(mComposition.sheenTone + 0.05f));
                    eventHappened = true;
                }
                break;

            case TensionDevice::Convergence: {
                const int32_t target = chordRoot + rel / 3;
                const float low = midiToHz(static_cast<float>(scaleDegreeToMidi(target - 3,
                                                                                std::max(0, mComposition.octaveBias - 1))));
                const float high = midiToHz(static_cast<float>(scaleDegreeToMidi(target + 4,
                                                                                 mComposition.octaveBias + 1)));
                scheduleLead(humanizeSamples(0.22f, false, false), low,
                             pressureAmp * 0.78f, duration * 0.54f, -0.46f,
                             clamp01(mComposition.counterTone - 0.06f));
                scheduleLead(static_cast<int32_t>(stepSamples * 0.47f), high,
                             pressureAmp * 0.62f, duration * 0.38f, 0.46f,
                             clamp01(mComposition.leadTone + 0.05f));
                eventHappened = true;
                break;
            }

            case TensionDevice::Hinge:
                scheduleChordGesture(humanizeSamples(0.26f, false, false),
                                     chordRoot + rel / 2, pressureAmp * 1.28f,
                                     duration, mRng.bipolar() * 0.48f,
                                     clamp01(mComposition.stabTone + 0.03f),
                                     (mComposition.chordArticulation == 3) ? 0 :
                                         ((mComposition.chordArticulation + 2) % 5));
                eventHappened = true;
                break;

            case TensionDevice::Shadow: {
                const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(chordRoot + rel - 2,
                                                                              std::max(0, mComposition.octaveBias - 1))));
                scheduleLead(humanizeSamples(0.34f, false, false), f,
                             pressureAmp * 0.74f, duration * 0.72f,
                             mRng.bipolar() * 0.32f,
                             clamp01(mComposition.droneTone - 0.10f));
                eventHappened = true;
                break;
            }

            case TensionDevice::Afterimage: {
                const int32_t motifSource = (p16 + 16 - mComposition.counterDelay) & 15;
                const int32_t degree = chordRoot + mComposition.motifF[motifSource] / 2 + rel / 3;
                const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(degree,
                                                                              mComposition.octaveBias)));
                scheduleLead(static_cast<int32_t>(stepSamples * 0.64f), f,
                             pressureAmp * 0.58f, duration * 0.46f,
                             mRng.bipolar() * 0.72f,
                             clamp01(mComposition.echoTone + 0.03f));
                eventHappened = true;
                break;
            }

            case TensionDevice::PadBreath:
                // One rare breath may exist, but it cannot repeat as a lattice or
                // overlap another slow pad mechanism.
                if (p16 == tensionFocus &&
                    mRng.chance(mComposition.padBreathRarity) &&
                    !hasSustainedPad(0.24f)) {
                    scheduleChordGesture(0, chordRoot + rel / 3,
                                         pressureAmp * 0.88f,
                                         std::min(duration * 1.8f,
                                                  stepDurationSeconds() * 3.4f),
                                         mRng.bipolar() * 0.28f,
                                         clamp01(mComposition.droneTone), 3);
                    eventHappened = true;
                }
                break;

            case TensionDevice::None:
            default:
                break;
        }
    }

    if (mComposition.useSpark > 0.02f && phrase != PhraseType::Suspension && (p16 == 3 || p16 == 7 || p16 == 11 || p16 == 15) &&
        mRng.chance(clamp01((0.05f + 0.18f * p.brightness + 0.12f * p.sync) * mComposition.useSpark * phraseLeadScale))) {
        const int32_t degree = generatedLayerDegree(MelodyLayer::Ornament, pieceStep, p16,
                                                    chordRoot, phrase, section) +
                               ((p16 == 15) ? 7 : 0);
        const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(degree, mComposition.octaveBias + 1)));
        scheduleLead(static_cast<int32_t>(stepSamples * (0.10f + 0.34f * mRng.uni())), f,
                     (0.012f + 0.038f * p.brightness) * accent * phraseLeadScale * mComposition.useSpark,
                     stepDurationSeconds() * generatedLayerDur(MelodyLayer::Ornament, p16),
                     mRng.bipolar() * 0.86f,
                     clamp01(mComposition.sparkTone + 0.05f * evolution.paletteDrift + mRng.bipolar() * 0.040f));
        eventHappened = true;
    }

    if (mComposition.useSub > 0.02f && (downbeat || p16 == 8 || (section == SectionType::Orbit && p16 == 4)) &&
        mRng.chance(clamp01((0.24f + 0.28f * p.bass) * mComposition.useSub * phraseBassScale))) {
        const int32_t degree = generatedLayerDegree(MelodyLayer::Bass, pieceStep, p16,
                                                    chordRoot, phrase, section);
        const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(degree, -1)));
        scheduleBass(humanizeSamples(0.10f, downbeat, false), f,
                     (0.090f + 0.090f * p.bass) * phraseBassScale * mComposition.useSub,
                     stepDurationSeconds() * (downbeat ? 3.6f : 2.0f), mRng.bipolar() * 0.035f,
                     clamp01(mComposition.subTone + 0.04f * evolution.paletteDrift + 0.02f * mRng.bipolar()));
        eventHappened = true;
    }

    if (mComposition.useEcho > 0.02f && phrase != PhraseType::Suspension && (p16 == 2 || p16 == 6 || p16 == 10 || p16 == 14) &&
        mRng.chance(clamp01((0.10f + 0.34f * p.melodyRun) * mComposition.useEcho * phraseLeadScale))) {
        const float eg = generatedLayerGate(MelodyLayer::Counter, pieceStep, (p16 + 4) & 15,
                                            PhraseType::Answer, section);
        const float ed = generatedLayerDur(MelodyLayer::Counter, (p16 + 4) & 15);
        if (eg > 0.02f) {
            const int32_t themeEcho = generatedLayerDegree(MelodyLayer::Counter, pieceStep,
                                                           (p16 + 4) & 15, chordRoot,
                                                           PhraseType::Answer, section);
            const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(themeEcho, mComposition.octaveBias + 1)));
            scheduleLead(static_cast<int32_t>(stepSamples * (0.52f + 0.18f * mRng.uni())), f,
                         (0.016f + 0.046f * p.melody) * phraseLeadScale * mComposition.useEcho * eg,
                         stepDurationSeconds() * (0.34f + 0.52f * ed), mRng.bipolar() * 0.84f,
                         clamp01(mComposition.echoTone + 0.05f * evolution.paletteDrift + 0.04f * mRng.bipolar()));
            eventHappened = true;
        }
    }

    if (mComposition.useOrbit > 0.02f && (section == SectionType::Orbit || p16 == 0 || p16 == 5 || p16 == 10 || p16 == 15) &&
        mRng.chance(clamp01((0.08f + 0.26f * p.texture + 0.16f * p.melody) * mComposition.useOrbit))) {
        const int32_t deg = generatedLayerDegree(MelodyLayer::Arp, pieceStep,
                                                 (p16 + phraseNumber) & 15,
                                                 chordRoot, phrase, section);
        const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(deg, mComposition.octaveBias)));
        scheduleLead(humanizeSamples(0.36f, false, false), f,
                     (0.015f + 0.042f * mPattern.melody) * phraseLeadScale * mComposition.useOrbit,
                     stepDurationSeconds() * (section == SectionType::Orbit ? 1.1f : 0.52f), mRng.bipolar() * 0.72f,
                     clamp01(mComposition.orbitTone + 0.045f * evolution.paletteDrift + mRng.bipolar() * 0.035f));
        eventHappened = true;
    }

    if (mComposition.useBloom > 0.02f && mComposition.useChord > 0.0f &&
        (downbeat || section == SectionType::Mirror || section == SectionType::Surge) &&
        mRng.chance(clamp01((0.10f + 0.30f * p.chord + 0.14f * p.texture) *
                            mComposition.useBloom * phraseChordScale))) {
        const int32_t bloomArticulation = (mComposition.chordArticulation == 3)
            ? 4 : ((mComposition.chordArticulation + 1) % 5);
        scheduleChordGesture(humanizeSamples(0.18f, downbeat, false),
                             chordRoot + ((section == SectionType::Mirror) ? 2 : 0),
                             (0.020f + 0.054f * p.chord) * phraseChordScale * mComposition.useBloom,
                             stepDurationSeconds() * (0.78f + 1.40f * p.space),
                             mRng.bipolar() * 0.52f,
                             clamp01(mComposition.bloomTone + mRng.bipolar() * 0.030f),
                             bloomArticulation);
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
            const int32_t deg = generatedLayerDegree(MelodyLayer::Ornament, pieceStep, p16,
                                                     chordRoot, phrase, section) + 5;
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
        const int32_t deg = generatedLayerDegree(MelodyLayer::Ornament, pieceStep, p16,
                                                 chordRoot, phrase, section) +
                            ((p16 == 15) ? 7 : 4);
        const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(deg, mComposition.octaveBias + 2)));
        scheduleLead(static_cast<int32_t>(stepSamples * (0.06f + 0.30f * mRng.uni())), f,
                     (0.010f + 0.032f * p.brightness) * phraseLeadScale * mComposition.useSheen,
                     stepDurationSeconds() * (0.12f + 0.16f * mRng.uni()), mRng.bipolar() * 0.94f,
                     clamp01(mComposition.sheenTone + 0.05f * evolution.paletteDrift + mRng.bipolar() * 0.040f));
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

    // Theme braid: a low-mid answer and a high afterimage can appear
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

    // Bass glides sometimes answer the melody instead of merely anchoring it.
    if (mComposition.useBass > 0.02f && (p16 == 2 || p16 == 14) &&
        (section == SectionType::Variation || section == SectionType::Mirror || section == SectionType::Afterimage || phrase == PhraseType::Answer) &&
        mRng.chance(clamp01((0.055f + 0.16f * p.bass + 0.10f * mComposition.counterpoint) * phraseBassScale))) {
        const int32_t bdeg = generatedLayerDegree(MelodyLayer::Bass, pieceStep, p16,
                                                  chordRoot, PhraseType::Answer, section);
        const float bf = midiToHz(static_cast<float>(scaleDegreeToMidi(bdeg, -1)));
        scheduleBass(static_cast<int32_t>(stepSamples * 0.38f), bf,
                     (0.050f + 0.060f * p.bass) * phraseBassScale * mComposition.useBass,
                     stepDurationSeconds() * 1.25f, mRng.bipolar() * 0.06f,
                     clamp01(mComposition.bassTone + 0.06f * mRng.bipolar()));
        eventHappened = true;
    }

    if (mComposition.usePulse > 0.02f && phrase != PhraseType::Suspension &&
        generatedLayerGate(MelodyLayer::Pulse, pieceStep, p16, phrase, section) > 0.02f &&
        mRng.chance(clamp01((0.18f + 0.42f * p.sync + 0.14f * p.density) *
                            mComposition.usePulse * phraseLeadScale))) {
        const int32_t deg = generatedLayerDegree(MelodyLayer::Pulse, pieceStep, p16,
                                                 chordRoot, phrase, section);
        const float f = midiToHz(static_cast<float>(scaleDegreeToMidi(deg, mComposition.octaveBias)));
        scheduleLead(humanizeSamples(0.34f, false, false), f,
                     (0.012f + 0.038f * mPattern.melody) * accent * phraseLeadScale * mComposition.usePulse,
                     stepDurationSeconds() * generatedLayerDur(MelodyLayer::Pulse, p16), mRng.bipolar() * 0.70f,
                     clamp01(mComposition.pulseTone + 0.05f * evolution.paletteDrift + 0.04f * mRng.bipolar()));
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
            const int32_t deg = generatedLayerDegree(MelodyLayer::Ornament, pieceStep, p16,
                                                     chordRoot, phrase, section);
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

    const bool rotorJoint = (tensionDevice != TensionDevice::None && p16 == tensionFocus) ||
                            (section == SectionType::Orbit && (p16 == 4 || p16 == 12));
    if (mComposition.useRotor > 0.02f && mComposition.useChord > 0.0f &&
        (downbeat || rotorJoint) &&
        mRng.chance(clamp01((0.08f + 0.26f * p.chord + 0.16f * p.space) *
                            mComposition.useRotor * phraseChordScale))) {
        const int32_t offset = (section == SectionType::Mirror) ? 2 :
                               ((section == SectionType::Orbit && p16 >= 8) ? 4 : 0);
        scheduleChordGesture(humanizeSamples(0.16f, downbeat, false), chordRoot + offset,
                             (0.020f + 0.050f * p.chord) * phraseChordScale * mComposition.useRotor,
                             stepDurationSeconds() * (0.84f + 1.28f * p.space),
                             mRng.bipolar() * 0.60f,
                             clamp01(mComposition.rotorTone + 0.03f * mRng.bipolar()),
                             (mComposition.chordArticulation + 2) % 5);
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

    if ((section == SectionType::Surge || section == SectionType::Cascade) && downbeat &&
        mComposition.drama > 0.60f && mComposition.useChord > 0.0f &&
        mRng.chance(0.38f + 0.30f * mComposition.drama)) {
        scheduleChordGesture(0, chordRoot,
                             (0.034f + 0.062f * mPattern.texture) * phraseChordScale,
                             stepDurationSeconds() * (0.90f + 1.16f * p.space),
                             mRng.bipolar() * 0.34f,
                             clamp01(mComposition.padTone + 0.08f),
                             (mComposition.chordArticulation == 3) ? 1 :
                                 mComposition.chordArticulation);
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
    // Slow pads are never allowed to accumulate into the old recurring wall.
    if (hasSustainedPad(0.12f)) return;
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

bool MusicEngine::hasSustainedPad(float remainingSeconds) const {
    for (const auto& voice : mPads) {
        if (voice.active && (voice.dur - voice.age) > remainingSeconds) return true;
    }
    for (const auto& event : mEvents) {
        if (event.active && event.kind == EventKind::Pad && event.dur > remainingSeconds) return true;
    }
    return false;
}

void MusicEngine::scheduleChordGesture(int32_t offsetSamples,
                                       int32_t degree,
                                       float amp,
                                       float dur,
                                       float pan,
                                       float color,
                                       int32_t articulation) {
    const float step = static_cast<float>(stepDurationSamples());
    articulation = ((articulation % 5) + 5) % 5;
    amp = std::max(0.0f, amp);
    dur = std::max(0.08f, dur);

    auto leadAt = [&](float fraction, int32_t rel, int32_t octave, float gain,
                      float durationScale, float panning, float toneShift) {
        const float freq = midiToHz(static_cast<float>(scaleDegreeToMidi(degree + rel, octave)));
        scheduleLead(offsetSamples + static_cast<int32_t>(step * fraction), freq,
                     amp * gain, dur * durationScale,
                     clamp(pan + panning, -1.0f, 1.0f),
                     clamp01(color + toneShift));
    };

    switch (articulation) {
        case 0: // Split dyad: unequal spacing prevents a march-like pulse.
            leadAt(0.00f, 0, std::max(0, mComposition.octaveBias - 1), 0.74f, 0.62f, -0.24f, -0.08f);
            leadAt(0.41f, 4, mComposition.octaveBias, 0.58f, 0.46f, 0.28f, 0.04f);
            break;

        case 1: // Unequal cascade: 0, 0.19 and 0.57 are deliberately nonuniform.
            leadAt(0.00f, 0, std::max(0, mComposition.octaveBias - 1), 0.62f, 0.50f, -0.30f, -0.06f);
            leadAt(0.19f, 2, mComposition.octaveBias, 0.52f, 0.42f, 0.06f, 0.00f);
            leadAt(0.57f, 4, mComposition.octaveBias, 0.44f, 0.34f, 0.34f, 0.06f);
            break;

        case 2: { // Polarity frame: low and high voices pull toward the center.
            const float low = midiToHz(static_cast<float>(scaleDegreeToMidi(degree - 3,
                                                                            std::max(0, mComposition.octaveBias - 1))));
            scheduleBass(offsetSamples, low, amp * 0.72f, dur * 0.58f,
                         clamp(pan - 0.18f, -1.0f, 1.0f),
                         clamp01(mComposition.bassTone - 0.08f));
            leadAt(0.33f, 5, mComposition.octaveBias + 1, 0.48f, 0.40f, 0.32f, 0.07f);
            break;
        }

        case 3: // Rare Pad Breath: single, short, and concurrency-limited.
            if (!hasSustainedPad(std::min(0.32f, dur * 0.30f))) {
                schedulePad(offsetSamples, degree, amp * 0.68f,
                            std::min(dur, stepDurationSeconds() * 3.4f),
                            pan * 0.58f, clamp01(color - 0.04f));
            }
            break;

        case 4: // Harmonic beam: related tones overlap without a chord-pad envelope.
        default:
            leadAt(0.00f, 0, mComposition.octaveBias, 0.58f, 0.78f, -0.20f, -0.02f);
            leadAt(0.29f, 7, mComposition.octaveBias, 0.40f, 0.56f, 0.24f, 0.08f);
            break;
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
    chosen->kit = mComposition.drumKit;
    chosen->body = mComposition.drumBody;
    chosen->metal = mComposition.drumMetal;
    chosen->noiseMix = mComposition.drumNoise;
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
    chosen->model = (mComposition.bassModel +
                     static_cast<int32_t>(clamp01(color) * 3.99f)) % 12;
    chosen->attackShape = mComposition.bassAttack;
    chosen->releasePoint = mComposition.bassRelease;
    chosen->glide = mComposition.bassGlide;
    chosen->pulseWidth = mComposition.bassPulseWidth;
    chosen->motion = mComposition.bassMotion;
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
    chosen->count = clampInt32(mComposition.padVoiceCount, 2, 4);
    chosen->cutoff = 0.014f + 0.060f * color + 0.030f * mPattern.texture;
    chosen->color = color;
    chosen->model = (mComposition.padModel +
                     static_cast<int32_t>(clamp01(color) * 2.99f)) % 10;
    chosen->attackShape = mComposition.padAttack;
    chosen->releasePoint = mComposition.padRelease;
    chosen->detune = mComposition.padDetune;
    chosen->motion = mComposition.padMotion;
    chosen->width = mComposition.padWidth;
    panGains(pan, chosen->panL, chosen->panR);

    for (int32_t i = 0; i < chosen->count; ++i) {
        const int32_t octave = (i >= 3) ? 2 : 1;
        const int32_t interval = mComposition.padIntervals[i];
        const float hz = midiToHz(static_cast<float>(scaleDegreeToMidi(degree + interval, octave)));
        const float detune = (mRng.bipolar() * 0.0035f) * (0.18f + chosen->detune);
        chosen->freq[i] = hz * (1.0f + detune);
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
    chosen->model = (mComposition.leadModel +
                     static_cast<int32_t>(clamp01(color) * 5.99f)) % 16;
    chosen->attackShape = mComposition.leadAttack;
    chosen->releasePoint = mComposition.leadRelease;
    chosen->glide = mComposition.leadGlide;
    chosen->vibratoDepth = mComposition.leadVibratoDepth;
    chosen->vibratoRate = mComposition.leadVibratoRate;
    chosen->modRatio = mComposition.leadModRatio;
    chosen->air = mComposition.leadAir;
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

    const int32_t kit = (v.kit % 8 + 8) % 8;
    const float bodyColor = clamp01(v.body);
    const float metalColor = clamp01(v.metal);
    const float noiseColor = clamp01(v.noiseMix);
    static constexpr float pitchBias[8] = {-7.0f, 0.0f, 8.0f, -3.0f, 12.0f, 4.0f, -10.0f, 6.0f};
    static constexpr float decayBias[8] = {0.4f, -0.5f, 1.2f, 0.0f, 1.8f, -1.0f, 0.8f, -0.2f};
    static constexpr float metalRatio[8] = {1.37f, 1.61f, 1.91f, 2.17f, 2.43f, 2.71f, 3.07f, 3.41f};

    float out = 0.0f;
    switch (v.type) {
        case DrumType::Kick: {
            const float env = std::exp(-(5.8f + 3.5f * (1.0f - bodyColor) + decayBias[kit]) * t);
            const float base = 31.0f + 24.0f * bodyColor + pitchBias[kit];
            const float sweep = 82.0f + 92.0f * bodyColor + 14.0f * static_cast<float>(kit & 3);
            const float pitch = base + sweep * std::exp(-(9.0f + 5.0f * noiseColor) * t) + 14.0f * v.aux;
            v.phase += kTwoPi * pitch / sr;
            v.phase2 += kTwoPi * pitch * (0.48f + 0.015f * static_cast<float>(kit)) / sr;
            if (v.phase > kTwoPi) v.phase -= kTwoPi;
            if (v.phase2 > kTwoPi) v.phase2 -= kTwoPi;
            const float sine = std::sin(v.phase);
            const float sub = std::sin(v.phase2);
            const float shaped = (kit == 2 || kit == 5)
                    ? std::tanh(sine * (1.5f + 2.0f * bodyColor))
                    : sine;
            const float click = noise(v.noiseState) * std::exp(-(55.0f + 35.0f * metalColor) * t) *
                                (0.04f + 0.18f * noiseColor + 0.10f * v.aux);
            out = std::tanh((0.62f * shaped + 0.38f * sub + click) *
                            (1.45f + 1.25f * bodyColor)) * env;
            break;
        }
        case DrumType::Snare: {
            const float env = std::exp(-(8.0f + 6.0f * (1.0f - bodyColor) + 0.45f * decayBias[kit]) * t);
            const float n = noise(v.noiseState);
            const float hpAmount = 0.84f + 0.10f * metalColor;
            v.hp = hpAmount * (v.hp + n - v.lp);
            v.lp = n;
            const float toneHz = 132.0f + 126.0f * bodyColor + 7.0f * pitchBias[kit] + 72.0f * v.aux;
            v.phase += kTwoPi * toneHz / sr;
            v.phase2 += kTwoPi * toneHz * metalRatio[kit] / sr;
            if (v.phase > kTwoPi) v.phase -= kTwoPi;
            if (v.phase2 > kTwoPi) v.phase2 -= kTwoPi;
            const float ring = 0.72f * std::sin(v.phase) + 0.28f * std::sin(v.phase2);
            const float noisy = v.hp * (0.42f + 0.48f * noiseColor);
            out = std::tanh((noisy + ring * (0.12f + 0.28f * bodyColor)) *
                            (1.05f + 0.55f * metalColor)) * env;
            break;
        }
        case DrumType::Clap: {
            const float n = noise(v.noiseState);
            const float e1 = std::exp(-(14.0f + 9.0f * (1.0f - noiseColor)) * t);
            const float s1 = 0.075f + 0.008f * static_cast<float>(kit & 3);
            const float s2 = 0.18f + 0.012f * static_cast<float>((kit + 1) & 3);
            const float s3 = 0.31f + 0.015f * static_cast<float>((kit + 2) & 3);
            const float burst = (std::exp(-190.0f * std::fabs(t - s1)) +
                                 std::exp(-155.0f * std::fabs(t - s2)) +
                                 std::exp(-115.0f * std::fabs(t - s3))) *
                                (0.18f + 0.24f * metalColor);
            v.lp += (n - v.lp) * (0.10f + 0.18f * bodyColor);
            out = (n - 0.35f * v.lp) * (e1 * (0.22f + 0.30f * noiseColor) + burst);
            break;
        }
        case DrumType::HatClosed:
        case DrumType::HatOpen: {
            const bool open = v.type == DrumType::HatOpen;
            const float env = std::exp(-(open ? (4.0f + 3.0f * (1.0f - bodyColor))
                                             : (22.0f + 14.0f * (1.0f - bodyColor))) * t);
            const float n = noise(v.noiseState);
            v.hp = (0.82f + 0.13f * metalColor) * (v.hp + n - v.lp);
            v.lp = n;
            const float f1 = 5100.0f + 2200.0f * metalColor + 180.0f * static_cast<float>(kit);
            const float f2 = f1 * metalRatio[kit];
            v.phase += kTwoPi * f1 / sr;
            v.phase2 += kTwoPi * f2 / sr;
            if (v.phase > kTwoPi) v.phase -= kTwoPi;
            if (v.phase2 > kTwoPi) v.phase2 -= kTwoPi;
            const float metallic = 0.18f * std::sin(v.phase) + 0.12f * std::sin(v.phase2) +
                                   0.07f * std::sin(v.phase + v.phase2);
            out = (v.hp * (0.46f + 0.52f * noiseColor) + metallic * (0.45f + 0.50f * metalColor)) * env;
            break;
        }
        case DrumType::Rim: {
            const float env = std::exp(-(20.0f + 9.0f * (1.0f - bodyColor)) * t);
            const float f = 410.0f + 260.0f * bodyColor + 34.0f * static_cast<float>(kit);
            v.phase += kTwoPi * f / sr;
            v.phase2 += kTwoPi * f * metalRatio[kit] / sr;
            if (v.phase > kTwoPi) v.phase -= kTwoPi;
            if (v.phase2 > kTwoPi) v.phase2 -= kTwoPi;
            out = (0.66f * std::sin(v.phase) + 0.25f * std::sin(v.phase2) +
                   noise(v.noiseState) * (0.08f + 0.16f * noiseColor)) * env;
            break;
        }
        case DrumType::Tom: {
            const float env = std::exp(-(5.5f + 3.0f * (1.0f - bodyColor)) * t);
            const float base = 72.0f + 120.0f * bodyColor + 14.0f * static_cast<float>(kit) + 130.0f * v.aux;
            const float pitch = base + (70.0f + 90.0f * metalColor) * std::exp(-8.0f * t);
            v.phase += kTwoPi * pitch / sr;
            v.phase2 += kTwoPi * pitch * (0.50f + 0.02f * static_cast<float>(kit & 3)) / sr;
            if (v.phase > kTwoPi) v.phase -= kTwoPi;
            if (v.phase2 > kTwoPi) v.phase2 -= kTwoPi;
            out = std::tanh((0.72f * std::sin(v.phase) + 0.30f * std::sin(v.phase2)) *
                            (1.0f + 0.85f * bodyColor)) * env;
            break;
        }
        case DrumType::Zap: {
            const float env = std::exp(-(14.0f + 8.0f * (1.0f - metalColor)) * t);
            const float pitch = 150.0f + (2800.0f + 2400.0f * metalColor) *
                                std::exp(-(15.0f + 7.0f * bodyColor) * t) * (0.32f + v.aux);
            v.phase += kTwoPi * pitch / sr;
            v.phase2 += kTwoPi * pitch * metalRatio[kit] / sr;
            if (v.phase > kTwoPi) v.phase -= kTwoPi;
            if (v.phase2 > kTwoPi) v.phase2 -= kTwoPi;
            out = std::sin(v.phase + (0.6f + 2.1f * metalColor) * std::sin(v.phase2)) * env;
            break;
        }
        case DrumType::Noise:
        case DrumType::Perc:
        default: {
            const float env = std::exp(-(9.0f + 8.0f * (1.0f - bodyColor)) * t);
            const float n = noise(v.noiseState);
            const float coeff = 0.025f + 0.25f * clamp01(v.aux + 0.45f * bodyColor);
            v.lp += (n - v.lp) * coeff;
            v.phase += kTwoPi * (180.0f + 850.0f * metalColor + 52.0f * static_cast<float>(kit)) / sr;
            if (v.phase > kTwoPi) v.phase -= kTwoPi;
            const float tonal = std::sin(v.phase) * (0.08f + 0.24f * metalColor);
            out = ((n - v.lp * (0.25f + 0.35f * noiseColor)) * (0.48f + 0.46f * noiseColor) + tonal) * env;
            break;
        }
    }

    v.age += dt;
    return std::tanh(out * (1.0f + 0.24f * bodyColor)) * v.amp;
}

float MusicEngine::renderBass(BassVoice& v) {
    const float sr = static_cast<float>(mSampleRate);
    const float dt = 1.0f / sr;
    const float t = v.age / std::max(0.001f, v.dur);
    if (t >= 1.0f) {
        v.active = false;
        return 0.0f;
    }

    const float color = clamp01(v.color);
    const float attackRate = 10.0f + 56.0f * (1.0f - clamp01(v.attackShape));
    const float attack = clamp(t * attackRate, 0.0f, 1.0f);
    const float releaseStart = clamp(v.releasePoint, 0.42f, 0.94f);
    const float release = 1.0f - clamp((t - releaseStart) /
                                      std::max(0.04f, 1.0f - releaseStart), 0.0f, 1.0f);
    const float env = attack * release;
    v.freq += (v.targetFreq - v.freq) * (0.0008f + 0.026f * clamp01(v.glide));

    const float motion = std::sin(kTwoPi * v.phase2 * (1.0f + 0.75f * v.motion));
    const float freq = v.freq * (1.0f + motion * (0.0004f + 0.0032f * v.motion));
    v.phase += freq / sr;
    v.phase2 += freq * (0.492f + 0.020f * color) / sr;
    if (v.phase >= 1.0f) v.phase -= 1.0f;
    if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;

    const float sine = std::sin(kTwoPi * v.phase);
    const float sub = std::sin(kTwoPi * v.phase2);
    const float saw = 2.0f * v.phase - 1.0f;
    const float tri = 1.0f - 4.0f * std::fabs(v.phase - 0.5f);
    const float pulse = (v.phase < clamp(v.pulseWidth, 0.10f, 0.84f)) ? 1.0f : -1.0f;
    const float mod = std::sin(kTwoPi * v.phase2 * (1.5f + 2.8f * v.motion));
    float raw = 0.0f;
    switch ((v.model % 12 + 12) % 12) {
        case 0: raw = 0.56f * sine + 0.48f * sub; break; // clean sub current
        case 1: raw = 0.44f * std::tanh(sine * (2.2f + 2.0f * color)) +
                      0.38f * sub + 0.24f * tri; break; // rubber saturation
        case 2: raw = 0.34f * sine + 0.42f * sub + 0.44f * saw; break; // saw/sub alloy
        case 3: raw = 0.55f * std::sin(kTwoPi * v.phase +
                                      (0.8f + 3.8f * v.motion) * mod) +
                      0.38f * sub; break; // two-operator low FM
        case 4: raw = 0.46f * pulse + 0.30f * tri + 0.38f * sub; break; // asymmetric pulse
        case 5: {
            const float folded = std::sin(kTwoPi *
                (v.phase + 0.22f * saw * (0.4f + v.motion)));
            raw = 0.48f * folded + 0.38f * sub + 0.20f * sine;
            break;
        }
        case 6: {
            const float warped = v.phase + (0.08f + 0.22f * v.motion) *
                                 std::sin(kTwoPi * v.phase);
            raw = 0.58f * std::sin(kTwoPi * warped) + 0.42f * sub;
            break;
        }
        case 7: raw = 0.42f * sine + 0.34f * sub + 0.26f * (sine * mod); break;
        case 8: {
            const float width = clamp(v.pulseWidth + 0.12f * motion, 0.10f, 0.88f);
            const float movingPulse = v.phase < width ? 1.0f : -1.0f;
            raw = 0.44f * movingPulse + 0.38f * sub + 0.22f * tri;
            break;
        }
        case 9:
            raw = 0.44f * sine + 0.27f * std::sin(kTwoPi * v.phase * 2.01f + 0.5f) +
                  0.15f * std::sin(kTwoPi * v.phase * 3.02f + 1.1f) + 0.34f * sub;
            break;
        case 10:
            raw = 0.54f * tri + 0.32f * std::sin(kTwoPi * v.phase * 1.503f) +
                  0.34f * sub;
            break;
        default: {
            const float stepped = v.phase < 0.20f ? -0.72f :
                                  (v.phase < 0.46f ? -0.22f :
                                  (v.phase < 0.73f ? 0.34f : 0.82f));
            raw = 0.40f * stepped + 0.32f * tri + 0.40f * sub;
            break;
        }
    }

    raw = std::tanh(raw * (v.drive + 0.55f * color));
    const float cutoff = clamp(v.cutoff + env *
        (0.025f + 0.19f * color + 0.08f * v.motion), 0.004f, 0.46f);
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

    const float color = clamp01(v.color);
    const float attackRate = v.dur > 2.0f
            ? (0.9f + 4.0f * (1.0f - v.attackShape))
            : (3.2f + 10.0f * (1.0f - v.attackShape));
    const float attack = clamp(t * attackRate, 0.0f, 1.0f);
    const float releaseStart = clamp(v.releasePoint, 0.54f, 0.98f);
    const float release = 1.0f - clamp((t - releaseStart) /
                                      std::max(0.02f, 1.0f - releaseStart), 0.0f, 1.0f);
    const float env = attack * release;
    const float slowMotion = std::sin(kTwoPi *
        (v.age / std::max(0.25f, v.dur)) * (0.25f + 1.2f * v.motion));
    float raw = 0.0f;
    for (int32_t i = 0; i < v.count; ++i) {
        const float detuneMotion = 1.0f + slowMotion *
            (0.00015f + 0.0012f * v.detune) * (static_cast<float>(i) - 1.5f);
        v.phase[i] += v.freq[i] * detuneMotion / sr;
        if (v.phase[i] >= 1.0f) v.phase[i] -= 1.0f;
        const float ph = v.phase[i];
        const float sine = std::sin(kTwoPi * ph);
        const float tri = 1.0f - 4.0f * std::fabs(ph - 0.5f);
        const float saw = 2.0f * ph - 1.0f;
        const float pulse = ph < clamp(0.24f + 0.45f * v.width +
                                     0.06f * slowMotion, 0.12f, 0.88f) ? 1.0f : -1.0f;
        float osc = 0.0f;
        switch ((v.model % 10 + 10) % 10) {
            case 0: osc = 0.82f * sine + 0.18f * tri; break; // mist
            case 1:
                osc = 0.66f * tri + 0.22f * sine +
                      0.12f * std::sin(kTwoPi * ph * 2.003f + 0.4f * i);
                break;
            case 2: osc = 0.48f * tri + 0.34f * saw + 0.18f * sine; break;
            case 3:
                osc = std::sin(kTwoPi * ph + (0.7f + 2.6f * v.motion) *
                               std::sin(kTwoPi * ph * 2.003f + 0.4f * i));
                break;
            case 4: osc = 0.46f * pulse + 0.34f * tri + 0.20f * sine; break;
            case 5:
                osc = 0.48f * sine +
                      0.30f * std::sin(kTwoPi * ph * 2.003f + 0.4f * i) +
                      0.18f * std::sin(kTwoPi * ph * 3.011f + 0.8f * i);
                break;
            case 6: {
                const float partial2 = std::sin(kTwoPi * ph * 2.003f + 0.4f * i);
                const float warped = ph + (0.04f + 0.16f * v.motion) * partial2;
                osc = 0.68f * std::sin(kTwoPi * warped) + 0.25f * tri;
                break;
            }
            case 7:
                osc = 0.62f * sine + 0.26f * std::sin(kTwoPi * ph * 1.501f) +
                      0.12f * std::sin(kTwoPi * ph * 3.011f + 0.8f * i);
                break;
            case 8:
                osc = 0.42f * tri +
                      0.26f * std::sin(kTwoPi * ph * 2.003f + 0.4f * i) +
                      0.22f * std::sin(kTwoPi * ph * 4.017f);
                break;
            default: {
                const float fold = std::sin(kTwoPi *
                    (ph + 0.18f * saw * (0.3f + v.motion)));
                osc = 0.56f * fold + 0.24f * sine +
                      0.16f * std::sin(kTwoPi * ph * 3.011f + 0.8f * i);
                break;
            }
        }
        raw += osc;
    }
    raw /= static_cast<float>(std::max(1, v.count));
    raw = std::tanh(raw * (1.02f + 0.78f * color + 0.25f * v.motion));
    v.lp += (raw - v.lp) * clamp(v.cutoff *
        (0.62f + 1.08f * color + 0.34f * env), 0.003f, 0.27f);
    v.hp += (v.lp - v.hp) * (0.00016f + 0.00048f * (1.0f - color));
    v.age += dt;
    return (v.lp - v.hp * (0.10f + 0.16f * color)) * env * v.amp;
}

float MusicEngine::renderLead(LeadVoice& v) {
    const float sr = static_cast<float>(mSampleRate);
    const float dt = 1.0f / sr;
    const float t = v.age / std::max(0.001f, v.dur);
    if (t >= 1.0f) {
        v.active = false;
        return 0.0f;
    }

    const float color = clamp01(v.color);
    const float attackRate = 7.0f + 52.0f * (1.0f - clamp01(v.attackShape));
    const float attack = clamp(t * attackRate, 0.0f, 1.0f);
    const float releaseStart = clamp(v.releasePoint, 0.36f, 0.96f);
    const float release = 1.0f - clamp((t - releaseStart) /
                                      std::max(0.03f, 1.0f - releaseStart), 0.0f, 1.0f);
    const float env = attack * release;
    v.vibPhase += (2.1f + 7.4f * v.vibratoRate) / sr;
    if (v.vibPhase > 1.0f) v.vibPhase -= 1.0f;
    const float vib = 1.0f + std::sin(kTwoPi * v.vibPhase) *
                      (0.0002f + 0.0062f * v.vibratoDepth);
    v.freq += (v.targetFreq - v.freq) * (0.0007f + 0.022f * v.glide);
    const float freq = v.freq * vib;

    v.phase += freq / sr;
    v.modPhase += freq * (0.62f + 5.20f * v.modRatio) / sr;
    if (v.phase >= 1.0f) v.phase -= 1.0f;
    if (v.modPhase >= 1.0f) v.modPhase -= 1.0f;

    const float sine = std::sin(kTwoPi * v.phase);
    const float tri = 1.0f - 4.0f * std::fabs(v.phase - 0.5f);
    const float saw = 2.0f * v.phase - 1.0f;
    const float mod = std::sin(kTwoPi * v.modPhase);
    const float pulse = v.phase < (0.18f + 0.48f * color) ? 1.0f : -1.0f;
    const int32_t modelIndex = (v.model % 16 + 16) % 16;
    float airNoise = 0.0f;
    if (modelIndex == 5 || modelIndex == 13 || v.air > 0.62f) {
        const float n = noise(v.noiseState);
        v.lp += (n - v.lp) * (0.025f + 0.060f * v.air);
        airNoise = n - v.lp;
    }

    float raw = 0.0f;
    switch (modelIndex) {
        case 0: raw = 0.56f * pulse + 0.32f * saw + 0.14f * sine; break;
        case 1: raw = std::sin(kTwoPi * v.phase +
                              (0.8f + 4.4f * v.modRatio) * mod); break;
        case 2:
            raw = 0.70f * sine + 0.25f * tri +
                  0.12f * std::sin(kTwoPi * v.modPhase * 0.503f);
            break;
        case 3:
            raw = std::tanh((0.56f * saw + 0.32f * sine) *
                            (1.5f + 2.8f * color));
            break;
        case 4: {
            const float stepped = v.phase < 0.20f ? -0.72f :
                                  (v.phase < 0.43f ? -0.20f :
                                  (v.phase < 0.70f ? 0.30f : 0.82f));
            raw = 0.50f * stepped + 0.30f * tri + 0.18f * sine;
            break;
        }
        case 5:
            raw = 0.66f * std::sin(kTwoPi * v.phase + 1.8f * mod) +
                  0.24f * airNoise * v.air;
            break;
        case 6: {
            const float vowel = sine + 0.44f * std::sin(kTwoPi * v.phase * 2.01f + 0.7f) +
                                0.24f * std::sin(kTwoPi * v.phase * 3.02f + 1.4f);
            raw = std::tanh(vowel * 0.82f);
            break;
        }
        case 7: {
            const float glass = sine +
                                0.38f * std::sin(kTwoPi * v.phase * 2.414f + 0.30f) +
                                0.18f * std::sin(kTwoPi * v.phase * 3.732f + 1.10f);
            raw = std::tanh(glass * (0.60f + 0.30f * env));
            break;
        }
        case 8: {
            const float fold = std::sin(kTwoPi *
                (v.phase + 0.24f * tri * (0.4f + v.modRatio)));
            raw = 0.64f * fold + 0.24f * tri;
            break;
        }
        case 9: {
            const float warped = v.phase + (0.06f + 0.18f * v.modRatio) *
                                 std::sin(kTwoPi * v.phase);
            raw = 0.72f * std::sin(kTwoPi * warped) + 0.20f * saw;
            break;
        }
        case 10: {
            const float wrapped = std::fmod(v.phase *
                (1.6f + 2.8f * v.modRatio), 1.0f);
            raw = 0.48f * (2.0f * wrapped - 1.0f) + 0.38f * sine;
            break;
        }
        case 11: {
            const float p2 = std::fmod(v.phase * 1.503f, 1.0f) < 0.42f ? 1.0f : -1.0f;
            raw = 0.42f * pulse + 0.34f * p2 + 0.22f * tri;
            break;
        }
        case 12:
            raw = 0.56f * sine +
                  0.26f * std::sin(kTwoPi * v.phase * 2.997f + 0.4f) +
                  0.14f * std::sin(kTwoPi * v.phase * 5.011f + 1.1f);
            break;
        case 13: {
            const float chirp = std::sin(kTwoPi * v.phase +
                                         (0.4f + 2.4f * env) * mod);
            raw = 0.64f * chirp + 0.22f * tri + 0.12f * airNoise * v.air;
            break;
        }
        case 14:
            raw = 0.42f * sine + 0.28f * std::sin(kTwoPi * v.phase * 2.0f) +
                  0.18f * std::sin(kTwoPi * v.phase * 4.0f) + 0.14f * pulse;
            break;
        default: {
            const float stack = sine +
                0.31f * std::sin(kTwoPi * v.phase * 1.997f + mod * 0.20f) +
                0.16f * std::sin(kTwoPi * v.phase * 4.003f + 0.8f);
            raw = std::tanh(stack * 0.78f);
            break;
        }
    }

    raw += airNoise * (0.02f + 0.12f * v.air) * env;
    v.lp += (raw - v.lp) * clamp(v.cutoff *
        (0.38f + 1.42f * env + 0.38f * color), 0.006f, 0.52f);
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
