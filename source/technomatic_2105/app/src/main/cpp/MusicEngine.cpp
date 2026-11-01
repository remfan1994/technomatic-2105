#include "MusicEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cctype>
#include <cstdio>

namespace rb {

static bool parseUnsignedField(const std::string& data, const char* key, uint32_t& out);
static bool parseSignedField(const std::string& data, const char* key, int32_t& out);
static int32_t clampInt32(int32_t value, int32_t lo, int32_t hi);

static uint32_t mixSeed(uint32_t seed) {
    seed ^= seed >> 16u;
    seed *= 0x7feb352du;
    seed ^= seed >> 15u;
    seed *= 0x846ca68bu;
    seed ^= seed >> 16u;
    return seed;
}

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

    mPrepared = true;
}

void MusicEngine::reset(uint32_t seed) {
    finalizeCurrentHistoryEntry();
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
    mNovelty = 0.0f;
    mPocketLate = 0.0015f + mRng.uni() * 0.0120f;
    mDcInL = mDcInR = mDcOutL = mDcOutR = 0.0f;
    mRecentHash.fill(0u);
    mRecentHashWrite = 0;
    mRecentMotifHash.fill(0u);
    mRecentMotifHashWrite = 0;
    mComposition = Composition{};

    mCurrentSongSeed = safeSeed;
    mPendingSongSeed = safeSeed;
    generateSeededSong(safeSeed);

}


void MusicEngine::next() {
    // Legacy callers get the same full-level replacement as the current app's
    // Next path. No whole-mix fade, dead-air timer or gain state is introduced.
    mPendingSongSeed = mDevelopmentRng.nextU32();
    switchToPendingStyle();
}


void MusicEngine::forceNewPiece() {
    finalizeCurrentHistoryEntry();
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
    mNovelty = 0.0f;
    mBpm = 0.0f;
    mBpmTarget = 92.0f;
    mDcInL = mDcInR = mDcOutL = mDcOutR = 0.0f;

    uint32_t seed = mDevelopmentRng.nextU32();
    seed ^= static_cast<uint32_t>(currentGenreMask() + 1) * 0x9e3779b9u;
    seed ^= static_cast<uint32_t>(mCurrentPieceSamples.load(std::memory_order_acquire) + 0x27d4eb2du);
    if (seed == 0u) seed = 0x52423934u;
    generateSeededSong(seed);
    mCurrentPieceSamples.store(0, std::memory_order_release);
}


// Retained only so older Java/JNI clients can link. Channels have no state,
// no audio influence and no ability to restart the currently playing piece.
void MusicEngine::rerenderCurrentWithChannel(int32_t, int32_t, int32_t) {}
void MusicEngine::setGenreMask(int32_t) {}
void MusicEngine::setGenreBlendMode(int32_t) {}
void MusicEngine::setGenrePrimary(int32_t) {}
int32_t MusicEngine::currentGenreMask() const { return 0; }
int32_t MusicEngine::currentGenreBlendMode() const { return 0; }
int32_t MusicEngine::currentGenrePrimary() const { return 0; }
int32_t MusicEngine::currentGenreMode() const { return 0; }

int32_t MusicEngine::pieceStepsFromSeconds(int32_t seconds, float bpm) const {
    const double safeBpm = std::max(40.0, std::min(220.0, static_cast<double>(bpm)));
    const double rawSteps = static_cast<double>(std::max(8, seconds)) * safeBpm * 4.0 / 60.0;
    int64_t phrases = static_cast<int64_t>(std::llround(rawSteps / static_cast<double>(kPhraseSteps)));
    phrases = std::max<int64_t>(2, phrases);
    phrases = std::min<int64_t>(phrases, 60000000LL / kPhraseSteps);
    return static_cast<int32_t>(phrases * kPhraseSteps);
}


std::string MusicEngine::dataWithListeningDuration(const std::string& data, int32_t seconds) const {
    seconds = std::max(0, seconds);
    const std::string needle = ";listened=";
    const size_t pos = data.find(needle);
    char field[48];
    std::snprintf(field, sizeof(field), ";listened=%d", seconds);
    if (pos == std::string::npos) return data + field;
    const size_t valueEnd = data.find(';', pos + needle.size());
    if (valueEnd == std::string::npos) return data.substr(0, pos) + field;
    return data.substr(0, pos) + field + data.substr(valueEnd);
}

std::string MusicEngine::currentSongData() const {
    std::string data;
    {
        std::lock_guard<std::mutex> guard(mSongDataMutex);
        data = mCurrentSongData.empty()
            ? std::string("technomatic2105-v1;seed=1379932468;seconds=180;edited=0;gmask=0;gblend=0;gprimary=0;gmode=0;cand=0;engine=30;outputrev=2")
            : mCurrentSongData;
    }
    const int32_t listened = static_cast<int32_t>(std::min<double>(2147483647.0, std::floor(currentElapsedSeconds())));
    return dataWithListeningDuration(data, listened);
}

std::string MusicEngine::historyData() const {
    const std::string current = currentSongData();
    uint32_t currentSeed = 0;
    int32_t currentCandidate = -1, currentMask = 0, currentBlend = 0, currentPrimary = 0;
    parseUnsignedField(current, "seed", currentSeed);
    parseSignedField(current, "cand", currentCandidate);
    parseSignedField(current, "gmask", currentMask);
    parseSignedField(current, "gblend", currentBlend);
    parseSignedField(current, "gprimary", currentPrimary);

    std::lock_guard<std::mutex> guard(mHistoryMutex);
    std::string out;
    for (int32_t i = 0; i < mSongHistorySize; ++i) {
        if (mSongHistory[i].empty()) continue;
        std::string entry = mSongHistory[i];
        uint32_t seed = 0;
        int32_t candidate = -1, mask = 0, blend = 0, primary = 0;
        parseUnsignedField(entry, "seed", seed);
        parseSignedField(entry, "cand", candidate);
        parseSignedField(entry, "gmask", mask);
        parseSignedField(entry, "gblend", blend);
        parseSignedField(entry, "gprimary", primary);
        if (seed == currentSeed && candidate == currentCandidate && mask == currentMask &&
            blend == currentBlend && primary == currentPrimary) {
            entry = current;
        }
        if (!out.empty()) out += '\n';
        out += entry;
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


bool MusicEngine::exportPcm16File(const std::string& data, int32_t seconds,
                                      const std::string& path,
                                      const std::atomic<bool>* cancelFlag) {
    return exportPcm16RangeFile(data, 0, seconds, path, cancelFlag);
}

bool MusicEngine::exportPcm16RangeFile(const std::string& data,
                                       int32_t startSeconds,
                                       int32_t endSeconds,
                                       const std::string& path,
                                       const std::atomic<bool>* cancelFlag) {
    if (path.empty()) return false;
    if (startSeconds < 0 || endSeconds <= startSeconds) return false;
    if (endSeconds > 86400 || (endSeconds - startSeconds) < 8 ||
        (endSeconds - startSeconds) > 3600) return false;

    MusicEngine engine;
    engine.prepare(48000.0);
    if (!data.empty() && !engine.loadSongData(data)) return false;

    // Export is an independent finite interpretation of the otherwise endless
    // sound. The engine develops from time zero so its symbolic memory at Start
    // is identical to live playback. Samples before Start are generated and
    // discarded; only [Start, End) is written.
    engine.mExportSinglePieceMode = true;
    engine.mExportStopSamples = static_cast<int64_t>(endSeconds) * 48000LL;
    engine.mComposition.pieceSteps = engine.pieceStepsFromSeconds(endSeconds,
                                                                  engine.mBpmTarget);

    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;

    static constexpr int32_t kSampleRate = 48000;
    static constexpr int32_t kChannels = 2;
    static constexpr int32_t kFramesPerChunk = 1024;
    const int64_t startFrame = static_cast<int64_t>(startSeconds) * kSampleRate;
    const int64_t endFrame = static_cast<int64_t>(endSeconds) * kSampleRate;
    const int64_t outputFrames = endFrame - startFrame;
    std::vector<float> floats(static_cast<size_t>(kFramesPerChunk * kChannels), 0.0f);
    std::vector<int16_t> pcm(static_cast<size_t>(kFramesPerChunk * kChannels), 0);

    int64_t timelineFrame = 0;
    int64_t writtenFrames = 0;
    bool ok = true;
    while (timelineFrame < endFrame) {
        if (cancelFlag && cancelFlag->load(std::memory_order_relaxed)) {
            ok = false;
            break;
        }
        const int32_t frames = static_cast<int32_t>(std::min<int64_t>(
            kFramesPerChunk, endFrame - timelineFrame));
        std::fill(floats.begin(), floats.begin() + static_cast<size_t>(frames * kChannels), 0.0f);
        engine.render(floats.data(), frames, kChannels);

        const int64_t chunkStart = timelineFrame;
        const int64_t chunkEnd = timelineFrame + frames;
        const int64_t writeStart = std::max<int64_t>(chunkStart, startFrame);
        const int64_t writeEnd = std::min<int64_t>(chunkEnd, endFrame);
        if (writeEnd > writeStart) {
            const int32_t first = static_cast<int32_t>(writeStart - chunkStart);
            const int32_t count = static_cast<int32_t>(writeEnd - writeStart);
            for (int32_t frame = 0; frame < count; ++frame) {
                for (int32_t ch = 0; ch < kChannels; ++ch) {
                    const int32_t sourceIndex = (first + frame) * kChannels + ch;
                    const int32_t destIndex = frame * kChannels + ch;
                    const float v = engine.clamp(
                        floats[static_cast<size_t>(sourceIndex)],
                        -1.0f, 1.0f);
                    pcm[static_cast<size_t>(destIndex)] =
                        static_cast<int16_t>(std::lrint(v * 32767.0f));
                }
            }
            const size_t wanted = static_cast<size_t>(count * kChannels);
            const size_t wrote = std::fwrite(pcm.data(), sizeof(int16_t), wanted, file);
            if (wrote != wanted) {
                ok = false;
                break;
            }
            writtenFrames += count;
        }
        timelineFrame += frames;
    }

    if (std::fclose(file) != 0) ok = false;
    if (writtenFrames != outputFrames) ok = false;
    return ok;
}

bool MusicEngine::loadSongData(const std::string& data) {
    // outputrev records delivery processing, not musical identity. Legacy and
    // current recipes both use the current fixed output transfer.
    uint32_t seed = 0;
    int32_t legacySeconds = kDefaultExportSeconds;
    if (!decodeSongData(data, seed, legacySeconds)) return false;

    int32_t ignoredMask = 0;
    int32_t savedCandidate = -1;
    const bool hasSavedMask = parseSignedField(data, "gmask", ignoredMask);
    const bool hasSavedCandidate = parseSignedField(data, "cand", savedCandidate);
    mGenreMask = mGenreBlendMode = mGenrePrimary = 0;
    mForcedCandidateIndex = hasSavedCandidate ? clampInt32(savedCandidate, 0, 47) : -1;

    const bool oldSuppressHistory = mSuppressHistoryRecord;
    mSuppressHistoryRecord = true;
    reset(seed);
    mSuppressHistoryRecord = oldSuppressHistory;
    mForcedCandidateIndex = -1;

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
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), ";%s=%d", key, value);
        out += buffer;
    };

    char header[96];
    std::snprintf(header, sizeof(header), "technomatic2105-v1;seed=%u;seconds=%d", mCurrentSongSeed, kDefaultExportSeconds);
    std::string out(header);
    appendField(out, "edited", mCurrentSongEdited ? 1 : 0);
    if (mCurrentSongEdited) {
        appendField(out, "editrecipe", 1);
        for (size_t i = 0; i < kSongOverrideKeys.size(); ++i) {
            if (!mSongOverridePresent[i]) continue;
            char key[48];
            std::snprintf(key, sizeof(key), "edit_%s", kSongOverrideKeys[i]);
            appendField(out, key, mSongOverrideValues[i]);
        }
    }
    appendField(out, "gmask", clampInt32(mActiveGenreMask, 0, (1 << kGenreModeCount) - 1));
    appendField(out, "gblend", clampInt32(mActiveGenreBlendMode, 0, 1));
    appendField(out, "gprimary", clampInt32(mActiveGenrePrimary, 0, kGenreModeCount));
    appendField(out, "gmode", currentGenreMode());
    appendField(out, "cand", clampInt32(mCurrentCandidateIndex, 0, 47));
    appendField(out, "engine", 30);
    appendField(out, "outputrev", 2);
    appendField(out, "intent", mPattern.intent);
    char planSeed[48];
    std::snprintf(planSeed, sizeof(planSeed), ";planseed=%u", mComposition.phrasePlanSeed);
    out += planSeed;
    appendField(out, "planbars", mComposition.developmentCycle);
    appendField(out, "recallbars", mComposition.recallPhrases);
    appendField(out, "harmonicspan", mComposition.harmonicSpan);
    appendField(out, "devicerotation", mComposition.deviceRotation);
    appendField(out, "phrasejoint", mComposition.phraseJoint);
    appendField(out, "leadmodel", mComposition.leadModel);
    appendField(out, "bassmodel", mComposition.bassModel);
    appendField(out, "harmonymodel", mComposition.padModel);
    appendField(out, "drumkit", mComposition.drumKit);
    for (int32_t lane = 0; lane < kSupportLanes; ++lane) {
        char key[16];
        std::snprintf(key, sizeof(key), "support%d", lane);
        const bool harmonic = lane == 2 || lane == 3 || lane == 7 || lane == 15;
        const int32_t model = harmonic ? mComposition.padModel :
            (lane == 9 ? mComposition.bassModel : mComposition.supportModels[lane]);
        appendField(out, key, supportLaneWeight(lane) > 0.02f ? model : -1);
    }


    appendField(out, "style", clampInt32(static_cast<int32_t>(mPattern.style), 0, static_cast<int32_t>(StyleType::Count) - 1));
    appendField(out, "tempo", clampInt32(static_cast<int32_t>(std::lround(mBpmTarget)), 40, 220));
    appendField(out, "root", clampInt32(mPattern.rootMidi, 24, 60));
    appendField(out, "scale", clampInt32(mPattern.scaleMode, 0, 8));
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
    const std::string data = currentSongData();
    if (data.empty()) return;

    auto sameIdentity = [](const std::string& a, const std::string& b) {
        uint32_t sa = 0, sb = 0;
        int32_t ca = -1, cb = -1, ma = 0, mb = 0, ba = 0, bb = 0, pa = 0, pb = 0;
        parseUnsignedField(a, "seed", sa); parseUnsignedField(b, "seed", sb);
        parseSignedField(a, "cand", ca); parseSignedField(b, "cand", cb);
        parseSignedField(a, "gmask", ma); parseSignedField(b, "gmask", mb);
        parseSignedField(a, "gblend", ba); parseSignedField(b, "gblend", bb);
        parseSignedField(a, "gprimary", pa); parseSignedField(b, "gprimary", pb);
        return sa == sb && ca == cb && ma == mb && ba == bb && pa == pb;
    };

    std::lock_guard<std::mutex> guard(mHistoryMutex);
    for (int32_t i = 0; i < mSongHistorySize; ++i) {
        if (sameIdentity(mSongHistory[i], data)) {
            mSongHistory[i] = data;
            return;
        }
    }
    if (mSongHistorySize < kSongHistoryLimit) {
        mSongHistory[mSongHistorySize++] = data;
        return;
    }
    for (int32_t i = 1; i < kSongHistoryLimit; ++i) mSongHistory[i - 1] = mSongHistory[i];
    mSongHistory[kSongHistoryLimit - 1] = data;
    mSongHistorySize = kSongHistoryLimit;
}

void MusicEngine::finalizeCurrentHistoryEntry() {
    std::string base;
    {
        std::lock_guard<std::mutex> guard(mSongDataMutex);
        base = mCurrentSongData;
    }
    if (base.empty()) return;
    const int32_t listened = static_cast<int32_t>(std::min<double>(2147483647.0, std::floor(currentElapsedSeconds())));
    const std::string data = dataWithListeningDuration(base, listened);

    uint32_t currentSeed = 0;
    int32_t currentCandidate = -1, currentMask = 0, currentBlend = 0, currentPrimary = 0;
    parseUnsignedField(data, "seed", currentSeed);
    parseSignedField(data, "cand", currentCandidate);
    parseSignedField(data, "gmask", currentMask);
    parseSignedField(data, "gblend", currentBlend);
    parseSignedField(data, "gprimary", currentPrimary);

    std::lock_guard<std::mutex> guard(mHistoryMutex);
    for (int32_t i = 0; i < mSongHistorySize; ++i) {
        uint32_t seed = 0;
        int32_t candidate = -1, mask = 0, blend = 0, primary = 0;
        parseUnsignedField(mSongHistory[i], "seed", seed);
        parseSignedField(mSongHistory[i], "cand", candidate);
        parseSignedField(mSongHistory[i], "gmask", mask);
        parseSignedField(mSongHistory[i], "gblend", blend);
        parseSignedField(mSongHistory[i], "gprimary", primary);
        if (seed == currentSeed && candidate == currentCandidate && mask == currentMask &&
            blend == currentBlend && primary == currentPrimary) {
            mSongHistory[i] = data;
            return;
        }
    }
}

void MusicEngine::applySongDataOverrides(const std::string& data) {
    mCurrentSongEdited = true;
    int32_t editRecipe = 0;
    const bool canonical = parseSignedField(data, "editrecipe", editRecipe) && editRecipe == 1;
    auto readOverride = [&](const char* key, int32_t& result) {
        if (!canonical) return parseSignedField(data, key, result);
        char exactKey[48];
        std::snprintf(exactKey, sizeof(exactKey), "edit_%s", key);
        return parseSignedField(data, exactKey, result);
    };
    for (size_t i = 0; i < kSongOverrideKeys.size(); ++i) {
        mSongOverridePresent[i] = readOverride(kSongOverrideKeys[i], mSongOverrideValues[i]);
    }
    int32_t value = 0;
    if (readOverride("style", value)) {
        value = clampInt32(value, 0, static_cast<int32_t>(StyleType::Count) - 1);
        const StyleType requestedStyle = static_cast<StyleType>(value);
        if (requestedStyle != mPattern.style) {
            mRng = Rng(mCurrentSongSeed ^ (0x9e3779b9u * static_cast<uint32_t>(value + 1)));
            generatePattern(requestedStyle);
        }
    }

    if (readOverride("tempo", value)) {
        mBpmTarget = static_cast<float>(clampInt32(value, 40, 220));
        mBpm = mBpmTarget;
    }
    if (readOverride("root", value)) mPattern.rootMidi = clampInt32(value, 24, 60);
    if (readOverride("scale", value)) mPattern.scaleMode = clampInt32(value, 0, 8);
    if (readOverride("density", value)) {
        mPattern.density = clamp01(static_cast<float>(clampInt32(value, 0, 100)) / 100.0f);
        mPattern.energy = clamp(0.28f + mPattern.density * 0.62f, 0.10f, 0.98f);
    }
    if (readOverride("swing", value)) mPattern.swing = static_cast<float>(clampInt32(value, 0, 100)) / 500.0f;
    if (readOverride("melody", value)) {
        const float x = static_cast<float>(clampInt32(value, 0, 100)) / 100.0f;
        mPattern.melody = clamp01(x);
        mComposition.useLead = std::max(mComposition.useLead, laneFromValue(value, 1.10f));
        mComposition.useCounter = std::max(mComposition.useCounter, laneFromValue(static_cast<int32_t>(value * 0.72f), 0.88f));
        mComposition.useArp = std::max(mComposition.useArp, laneFromValue(static_cast<int32_t>(value * 0.68f), 0.88f));
    }
    if (readOverride("motif", value)) {
        const float x = static_cast<float>(clampInt32(value, 0, 100)) / 100.0f;
        mComposition.motifGain = clamp(0.55f + 1.75f * x, 0.45f, 2.35f);
        mComposition.hookEmphasis = clamp(0.72f + 1.40f * x, 0.70f, 2.20f);
    }
    if (readOverride("drama", value)) {
        const float x = static_cast<float>(clampInt32(value, 0, 100)) / 100.0f;
        mComposition.drama = clamp01(x);
        mComposition.deviceDepth = clamp(0.16f + 0.84f * x, 0.12f, 1.05f);
        mComposition.surgeLift = clamp(0.82f + 1.20f * x, 0.70f, 2.20f);
    }
    if (readOverride("space", value)) mPattern.space = clamp01(static_cast<float>(clampInt32(value, 0, 100)) / 100.0f);
    if (readOverride("roughness", value)) {
        const float x = static_cast<float>(clampInt32(value, 0, 100)) / 100.0f;
        mPattern.roughness = clamp01(x);
        mPattern.drive = clamp(0.22f + 0.72f * x, 0.12f, 1.05f);
    }
    if (readOverride("brightness", value)) {
        const float x = static_cast<float>(clampInt32(value, 0, 100)) / 100.0f;
        mComposition.leadTone = clamp01(0.20f + 0.78f * x);
        mComposition.arpTone = clamp01(0.18f + 0.80f * x);
        mComposition.sparkTone = clamp01(0.24f + 0.74f * x);
        mComposition.sheenTone = clamp01(0.26f + 0.72f * x);
        mComposition.hatTone = clamp01(0.16f + 0.80f * x);
    }
    if (readOverride("diversity", value)) {
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
        if (readOverride(key, v)) target = laneFromValue(v, high);
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

    // Legacy recipes may still contain these controls. A music-only rendition
    // never restores the retired background hiss, zap, or noise-effect lanes.
    mComposition.useTexture = 0.0f;
    mComposition.useFx = 0.0f;
    mComposition.useGrain = 0.0f;

    constrainSupportPalette();
    repairPattern();
    rememberDrumReference();
    mCurrentPieceSamples.store(0, std::memory_order_release);
}

void MusicEngine::generateSeededSong(uint32_t seed) {
    const uint32_t requestedSeed = seed ? seed : 0x52423934u;
    // Use reset's original timing draws for every entry path. A direct Next
    // must not inherit the previous song's backbeat delay. This separate RNG
    // leaves candidate selection and subsequent note improvisation unchanged.
    Rng timingRng(requestedSeed ^ 0xa511e9b3u);
    mPhraseSeed = timingRng.rangeInt(0, 4095);
    mPocketLate = 0.0015f + timingRng.uni() * 0.0120f;
    mGenreMask = mGenreBlendMode = mGenrePrimary = 0;
    mActiveGenreMask = mActiveGenreBlendMode = mActiveGenrePrimary = 0;
    mWorkingGenreMode = 0;
    mCurrentGenreMode.store(0, std::memory_order_release);
    mCurrentSongSeed = requestedSeed;
    mCurrentSongEdited = false;
    mSongOverridePresent.fill(false);
    mSongOverrideValues.fill(0);
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
        float score = scoreCurrentComposition() + 0.0016f * static_cast<float>(mRng.rangeInt(0, 100));
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
    rememberDrumReference();
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

    const StyleProfile target = profile(mPattern.style, false);
    auto fit = [&](float value, float desired, float width) {
        const float distance = (value - desired) / std::max(1.0f, width);
        return 1.0f / (1.0f + distance * distance);
    };
    // A slow phrase earns its score by saying something with fewer notes.
    // Neither note count nor a large interval span is an intrinsic quality bonus.
    const float melodic = 1.10f * fit(static_cast<float>(leadHits), target.leadTarget, 1.7f) +
            0.30f * fit(static_cast<float>(answerHits), target.leadTarget * 1.35f, 3.0f) +
            0.34f * fit(static_cast<float>(hookHits), target.leadTarget, 2.3f) +
            0.24f * fit(static_cast<float>(verseHits), target.leadTarget + 1.0f, 2.8f) +
            0.30f * fit(static_cast<float>(uniqueDegrees), 3.0f + 3.0f * target.motion, 2.0f) +
            0.28f * fit(static_cast<float>(span), 4.0f + 3.0f * target.motion, 2.8f);
    const float foundation = 0.68f * fit(static_cast<float>(bassHits), target.bassTarget, 1.6f) +
            0.22f * fit(static_cast<float>(bassDevelopmentHits), target.bassTarget * 1.5f, 3.0f) +
            0.28f * fit(static_cast<float>(chordHits), 1.4f + target.chord, 1.4f);
    const float phraseLogic = 0.18f * clamp(static_cast<float>(repeatedCells) / 3.0f, 0.0f, 1.4f) +
            0.18f * clamp(static_cast<float>(directionChanges) / 3.0f, 0.0f, 1.4f) +
            0.14f * clamp(static_cast<float>(strongAnchors) / 3.0f, 0.0f, 1.3f) +
            0.12f * clamp(static_cast<float>(answerContrast) / 4.0f, 0.0f, 1.3f) +
            0.10f * clamp(static_cast<float>(rhythmicCells) / 5.0f, 0.0f, 1.2f) +
            0.10f * fit(static_cast<float>(secondaryHits), target.leadTarget + 2.0f, 3.0f);
    const float layerTarget = 3.8f + 0.75f * target.supportTarget + 1.10f * target.motion;
    const float layerPenalty = std::max(0.0f, lanes - layerTarget) * 0.16f;
    const float grammar = 0.38f * mComposition.longMemory +
            0.34f * mComposition.callResponse +
            0.26f * mComposition.counterpoint +
            0.22f * mComposition.melodicGravity;

    float score = melodic + foundation + grammar + phraseLogic - layerPenalty;
    if (leadHits < 3) score -= 1.20f;
    if (hookHits < 3) score -= 0.55f;
    if (bassHits < 2) score -= 0.65f;
    if (span <= 1 && mPattern.melody > 0.45f) score -= 0.55f;
    if (uniqueDegrees < 3 && mPattern.melody > 0.45f) score -= 0.42f;
    if (directionalMoves > 0 && directionChanges == 0 && mPattern.melody > 0.50f) score -= 0.30f;
    if (largeLeaps > 3) score -= 0.26f;
    if (mComposition.useLead < 0.55f || mComposition.useBass < 0.55f) score -= 0.80f;
    if (mComposition.hookStability < 0.76f) score -= 0.18f;
    if (mComposition.verseFreedom < 0.30f) score -= 0.12f;
    return score;
}

MusicEngine::StyleProfile MusicEngine::profile(StyleType style, bool) const {
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

    applyCompositionIntent(p);
    return p;
}

void MusicEngine::applyCompositionIntent(StyleProfile& p) const {
    // All 48 candidates compete within the SAME listening intention. A busy
    // candidate cannot win a quiet seed merely by having more notes or layers.
    // The macro coordinates are seed-only; candidates explore musical solutions
    // around them. The complete intent range is available to every new song.
    Rng design(mixSeed(mCurrentSongSeed ^ 0x2905d17au));
    Rng detail(mixSeed(mPattern.designSeed ^ 0x51ed270bu));
    p.intent = design.rangeInt(0, 3);
    const float pace = design.uni();
    const float movement = design.uni();
    const float sync = design.uni();
    const float warmth = design.uni();
    const float harmony = design.uni();
    const float bassWeight = design.uni();
    const float legato = design.uni();
    const float ensemble = design.uni();

    static constexpr float tempoLo[] = {56.0f, 84.0f, 112.0f, 144.0f};
    static constexpr float tempoHi[] = {84.0f, 112.0f, 144.0f, 180.0f};
    static constexpr float motionLo[] = {0.16f, 0.34f, 0.53f, 0.74f};
    static constexpr float motionSpan[] = {0.19f, 0.20f, 0.20f, 0.22f};
    const float center = tempoLo[p.intent] + 2.0f +
        pace * (tempoHi[p.intent] - tempoLo[p.intent] - 4.0f);
    p.bpmMin = center - 2.0f;
    p.bpmMax = center + 2.0f;
    p.motion = motionLo[p.intent] + movement * motionSpan[p.intent];
    p.density = clamp(p.motion + 0.08f * (p.density - 0.50f) +
                      0.022f * detail.bipolar(), 0.12f, 0.98f);
    p.drum = clamp(0.25f + 0.62f * p.motion + 0.10f * p.drum +
                   0.055f * detail.bipolar(), 0.28f, 0.98f);
    p.bass = clamp(0.47f + 0.37f * bassWeight + 0.08f * p.bass, 0.45f, 0.96f);
    p.melody = clamp(0.62f + 0.20f * detail.uni() + 0.12f * p.melody, 0.60f, 0.96f);
    p.chord = clamp(0.32f + 0.48f * harmony + 0.12f * p.chord, 0.30f, 0.96f);
    p.sync = clamp(0.12f + 0.69f * sync + 0.10f * p.sync, 0.08f, 0.95f);
    p.space = clamp(0.91f - 0.63f * p.motion + 0.065f * detail.bipolar(), 0.16f, 0.88f);
    p.rough = clamp(0.05f + 0.55f * p.motion + 0.18f * p.rough +
                    0.085f * detail.bipolar(), 0.02f, 0.86f);
    p.brightness = clamp(0.14f + 0.58f * warmth + 0.14f * p.brightness +
                         0.08f * detail.bipolar(), 0.12f, 0.96f);
    // Texture now means harmonic body only. There is no noise/atmosphere lane.
    p.texture = clamp(0.16f + 0.45f * harmony + 0.12f * p.texture, 0.12f, 0.80f);
    p.melodyRun = clamp(0.04f + 0.71f * p.motion + 0.09f * detail.uni(), 0.06f, 0.86f);
    p.hatRoll = clamp(0.36f * p.motion * p.motion + 0.045f * sync, 0.0f, 0.42f);
    p.drama = clamp(0.20f + 0.50f * p.motion + 0.12f * detail.uni(), 0.20f, 0.86f);
    p.palette = clamp(0.48f + 0.30f * ensemble + 0.12f * p.palette, 0.42f, 0.94f);
    p.transitionSilence = 0.18f + 0.42f * p.space;
    p.swingMin = clamp(0.015f + 0.10f * sync * (1.12f - 0.70f * p.motion), 0.0f, 0.14f);
    p.swingMax = p.swingMin + 0.025f + 0.028f * detail.uni();
    p.noteLength = clamp(0.92f - 0.36f * p.motion + 0.20f * legato, 0.48f, 1.06f);
    p.leadTarget = 3.5f + 8.1f * p.motion;
    p.bassTarget = 2.0f + 6.8f * p.motion + 0.55f * bassWeight;
    p.supportTarget = 1.0f + 2.7f * p.motion + 1.4f * ensemble;

    // These are musical relationships selected per seed, not genre presets.
    // Sparse slow pieces still have a pulse; intense pieces retain space for
    // their motif instead of requiring constant sixteenth-note hats.
    p.ambient = p.intent == 0 && design.chance(0.24f);
    p.halfTime = p.intent == 0 ? design.chance(0.48f) :
        (p.intent == 3 ? design.chance(0.14f) : design.chance(0.24f));
    p.fourOnFloor = !p.ambient && !p.halfTime &&
        design.chance(0.12f + 0.30f * p.motion);
    p.breakbeat = !p.ambient && design.chance(0.12f + 0.40f * sync * p.motion);
    p.trapHats = p.intent >= 2 && design.chance(0.18f + 0.22f * sync);
}

MusicEngine::StyleType MusicEngine::randomStyle() {
    mWorkingGenreMode = 0;
    return static_cast<StyleType>(mRng.rangeInt(0, static_cast<int32_t>(StyleType::Count) - 1));
}

void MusicEngine::generatePattern(StyleType style) {
    const uint32_t designSeed = mixSeed(mCurrentSongSeed ^ mRng.state ^ 0x29c0de51u);
    mPattern = Pattern{};
    mPattern.designSeed = designSeed;
    mPattern.style = style;
    // The seed and candidate create the complete composition directly.
    const StyleProfile p = profile(style, false);
    mPattern.intent = p.intent;
    mPattern.profileTexture = p.texture;
    mPattern.profileAmbient = p.ambient;
    mPattern.profileBreakbeat = p.breakbeat;

    const int32_t roots[] = {31, 33, 34, 36, 38, 39, 41, 43, 46};
    mPattern.rootMidi = roots[mRng.rangeInt(0, 8)];
    if (style == StyleType::GlassNoir || style == StyleType::SubOrbit || style == StyleType::DeepMagnet || style == StyleType::MarbleBass || style == StyleType::StrangeHarbor || style == StyleType::ObsidianBloom || style == StyleType::NightLatch || style == StyleType::StaticCathedral) mPattern.rootMidi -= 2;
    if (style == StyleType::PrismCruise || style == StyleType::ChromeBloom || style == StyleType::SolarFold || style == StyleType::WarmCurrent || style == StyleType::IonGarden || style == StyleType::EchoCrown || style == StyleType::MagentaWell || style == StyleType::LatticeSun || style == StyleType::VoltageMoth || style == StyleType::QuartzTide || style == StyleType::CopperChord || style == StyleType::MercuryThread) mPattern.rootMidi += 2;

    Rng tonality(mixSeed(mCurrentSongSeed ^ 0x6d61726bu));
    mPattern.scaleMode = tonality.rangeInt(0, 8);

    mPattern.swing = p.swingMin + mRng.uni() * (p.swingMax - p.swingMin);
    mPattern.humanize = clamp(0.12f + mRng.uni() * 0.62f + (p.swingMax * 1.1f), 0.06f, 0.95f);
    mPattern.energy = clamp(0.15f + p.motion * 0.78f + mRng.bipolar() * 0.045f, 0.12f, 0.96f);
    mPattern.density = clamp(p.density + mRng.bipolar() * 0.045f, 0.10f, 0.98f);
    mPattern.syncopation = clamp(p.sync + mRng.bipolar() * 0.13f, 0.05f, 0.96f);
    mPattern.texture = clamp(p.texture + mRng.bipolar() * 0.11f, 0.00f, 0.95f);
    mPattern.roughness = clamp(p.rough + mRng.bipolar() * 0.12f, 0.00f, 0.98f);
    mPattern.space = clamp(p.space + mRng.bipolar() * 0.13f, 0.02f, 0.92f);
    mPattern.melody = clamp(p.melody + mRng.bipolar() * 0.14f, 0.15f, 0.98f);
    mPattern.drive = clamp(0.36f + p.rough * 0.48f + mRng.bipolar() * 0.08f, 0.18f, 0.92f);

    mBpmTarget = p.bpmMin + mRng.uni() * (p.bpmMax - p.bpmMin);
    if (mBpm <= 10.0f) mBpm = mBpmTarget;
    else mBpm = 0.45f * mBpm + 0.55f * mBpmTarget;

    // Generate a rhythmic sentence from weighted positions and its own pulse
    // grammar. There is no finite DNA/riff table. The following bars answer the
    // first bar, and the existing long-form development continues to vary them.
    Rng rhythm(mixSeed(mPattern.designSeed ^ 0xa4517e2du));
    std::array<float, kPhraseSteps> kickWord{};
    std::array<float, kPhraseSteps> snareWord{};
    std::array<float, kPhraseSteps> hatWord{};
    std::array<float, kPhraseSteps> percWord{};
    const int32_t hatStride = p.motion < 0.38f ? (rhythm.chance(0.60f) ? 4 : 2) : 2;
    const int32_t hatPhase = rhythm.rangeInt(0, hatStride - 1);
    kickWord[0] = p.ambient ? 0.42f : 0.94f;
    if (p.halfTime) snareWord[8] = 0.88f;
    else { snareWord[4] = 0.86f; snareWord[12] = 0.92f; }
    if (p.fourOnFloor) {
        for (int32_t pos = 0; pos < 16; pos += 4) kickWord[pos] = 0.88f;
    }
    auto buildWord = [&](std::array<float, kPhraseSteps>& word, int32_t count, int32_t kind) {
        int32_t used = 0;
        for (float value : word) if (value > 0.0f) ++used;
        for (; used < count; ++used) {
            int32_t best = -1;
            float bestScore = -10000.0f;
            for (int32_t pos = 0; pos < 16; ++pos) {
                if (word[pos] > 0.0f) continue;
                int32_t distance = 8;
                for (int32_t other = 0; other < 16; ++other) {
                    if (word[other] <= 0.0f) continue;
                    const int32_t delta = std::abs(pos - other);
                    distance = std::min(distance, std::min(delta, 16 - delta));
                }
                const bool strong = (pos & 3) == 0;
                const bool off = (pos & 1) != 0;
                float score = 0.70f * rhythm.uni() + 0.055f * static_cast<float>(distance);
                if (kind == 0) {
                    score += strong ? 0.42f * (1.0f - p.sync) : 0.0f;
                    score += off ? 0.44f * p.sync : 0.0f;
                    score -= snareWord[pos] * 0.36f;
                    if (distance <= 1) score -= 0.50f * (1.0f - p.motion);
                } else if (kind == 1) {
                    score += (pos == 3 || pos == 7 || pos == 11 || pos == 15) ? 0.20f * p.sync : 0.0f;
                    score -= kickWord[pos] * 0.22f;
                } else if (kind == 2) {
                    score += ((pos + hatPhase) % hatStride == 0) ? 0.52f : 0.16f * p.motion;
                    if (distance == 1) score -= 0.38f * (1.0f - p.motion);
                } else {
                    score += off ? 0.22f * p.sync : 0.0f;
                    score -= (kickWord[pos] + snareWord[pos]) * 0.36f;
                }
                if (score > bestScore) { bestScore = score; best = pos; }
            }
            if (best < 0) break;
            const float strength = kind == 0 ? 0.56f : (kind == 1 ? 0.25f : 0.46f);
            word[best] = strength + (kind == 1 ? 0.16f : 0.30f) * rhythm.uni();
        }
    };
    const int32_t kickCount = clampInt32(2 + static_cast<int32_t>(3.8f * p.motion) + rhythm.rangeInt(0, 2), 2, 8);
    const int32_t snareCount = (p.halfTime ? 1 : 2) + static_cast<int32_t>(2.4f * p.motion * p.sync);
    const int32_t hatCount = clampInt32(static_cast<int32_t>(std::lround(2.0f + 10.0f * p.motion + 2.0f * p.sync)), 3, 15);
    const int32_t percCount = clampInt32(static_cast<int32_t>(std::lround(1.0f + 4.0f * p.motion * p.sync)), 1, 6);
    buildWord(kickWord, kickCount, 0);
    buildWord(snareWord, snareCount, 1);
    buildWord(hatWord, hatCount, 2);
    buildWord(percWord, percCount, 3);

    const int32_t openHatPosition = rhythm.rangeInt(0, 15);
    for (int32_t i = 0; i < kPatternSteps; ++i) {
        const int32_t pos = i & 15;
        const int32_t bar = i / 16;
        const bool down = pos == 0;
        const bool back = p.halfTime ? pos == 8 : (pos == 4 || pos == 12);
        const float barBreath = 0.92f + 0.12f * rhythm.uni();
        const float quiet = p.ambient ? 0.58f : 1.0f;
        mPattern.kick[i] = clamp(kickWord[pos] * barBreath * quiet, 0.0f, 0.98f);
        mPattern.snare[i] = clamp(snareWord[pos] * barBreath * quiet, 0.0f, 0.98f);
        mPattern.hat[i] = clamp(hatWord[pos] * (0.60f + 0.36f * p.motion), 0.0f, 0.96f);
        mPattern.openHat[i] = pos == ((openHatPosition + (bar >= 2 ? 4 : 0)) & 15)
            ? 0.08f + 0.18f * p.motion : 0.0f;
        mPattern.perc[i] = clamp(percWord[pos] * (0.20f + 0.40f * p.motion), 0.0f, 0.72f);
        if ((bar & 1) != 0 && pos >= 9 && !back && rhythm.chance(0.09f + 0.20f * p.motion)) {
            mPattern.perc[i] = std::max(mPattern.perc[i], 0.10f + 0.22f * p.sync);
        }
        mPattern.bass[i] = (0.05f + 0.48f * kickWord[pos]) * (0.58f + 0.38f * p.bass);
        mPattern.chord[i] = down ? 0.10f + 0.12f * p.chord : 0.0f;
        mPattern.lead[i] = 0.02f + 0.08f * p.melody;
        const float accent = down ? 0.96f : (back ? 0.76f : ((pos & 1) == 0 ? 0.48f : 0.32f));
        mPattern.accent[i] = clamp(accent + 0.08f * rhythm.bipolar(), 0.0f, 1.0f);
    }

    generateComposition(p);
    repairPattern();
    rememberDrumReference();

    // Candidate material is a pure function of seed and candidate index.
    // Recent-session memory influences candidate scoring only;
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
    int32_t noteCount = static_cast<int32_t>(std::lround(p.leadTarget)) + rng.rangeInt(-1, 1);
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
        // Phrase durations follow the actual distance to the next note. The
        // old near-constant duration smeared dense lines and made sparse lines
        // sound like the same short riff with a slower clock.
        float dur = static_cast<float>(gap) * (0.78f + 0.18f * p.space);
        if (pos == 0 || pos == 8) dur *= 1.08f;
        if (pos == 15) dur = 1.10f;
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
    int32_t hitCount = static_cast<int32_t>(std::lround(p.bassTarget)) + rng.rangeInt(-1, 1);
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

    mComposition.bassModel = rng.rangeInt(0, kBassModels - 1);
    mComposition.leadModel = rng.rangeInt(0, kLeadModels - 1);
    mComposition.padModel = rng.rangeInt(0, kPadModels - 1);
    mComposition.drumKit = rng.rangeInt(0, kDrumKits - 1);
    // Mellow instruments keep a stable harmonic center. Their choice still
    // spans many synthesis designs and continuous shape/envelope coordinates.
    static constexpr std::array<int32_t, 18> mellowLeads{{
        2, 6, 7, 12, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 28, 29, 30, 31
    }};
    static constexpr std::array<int32_t, 12> mellowBass{{0, 1, 6, 9, 12, 13, 14, 15, 16, 17, 18, 19}};
    if (p.intent == 0) {
        mComposition.leadModel = mellowLeads[rng.rangeInt(0, static_cast<int32_t>(mellowLeads.size()) - 1)];
        mComposition.bassModel = mellowBass[rng.rangeInt(0, static_cast<int32_t>(mellowBass.size()) - 1)];
        mComposition.padModel = rng.rangeInt(10, kPadModels - 1);
    }
    // The catalogue expands between songs. Three fixed supporting colors are
    // shared among selected roles, so primary+bass+harmony+supports never exceeds
    // six distinct pitched families, and no role changes family with tone.
    Rng paletteRng(mComposition.timbreGrammarSeed ^ 0x30ba11adu);
    std::array<int32_t, 3> supportingModels{};
    for (int32_t& model : supportingModels) {
        model = p.intent == 0 ? mellowLeads[paletteRng.rangeInt(0, static_cast<int32_t>(mellowLeads.size()) - 1)] :
                               paletteRng.rangeInt(0, kLeadModels - 1);
    }
    for (int32_t lane = 0; lane < kSupportLanes; ++lane) {
        mComposition.supportModels[lane] = supportingModels[static_cast<size_t>(lane) % supportingModels.size()];
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
    // A harmonic operator ratio keeps the pitch readable while depth, waveform,
    // envelope and register still create continuously variable instruments.
    mComposition.leadModRatio = (static_cast<float>(rng.rangeInt(1, 5)) - 0.62f) / 5.20f;
    mComposition.leadAir = 0.0f;

    mComposition.padAttack = clamp(0.20f + 0.72f * rng.uni(), 0.16f, 0.96f);
    mComposition.padRelease = clamp(0.58f + 0.36f * rng.uni(), 0.54f, 0.98f);
    mComposition.padDetune = clamp(0.04f + 0.70f * rng.uni(), 0.02f, 0.82f);
    mComposition.padMotion = clamp(0.06f + 0.76f * rng.uni(), 0.04f, 0.88f);
    mComposition.padWidth = clamp(0.22f + 0.70f * rng.uni(), 0.18f, 0.96f);
    mComposition.padVoiceCount = rng.chance(0.18f + 0.24f * p.chord) ? 4 :
                                 (rng.chance(0.26f + 0.18f * p.space) ? 2 : 3);

    // Voicings move among chord members and inversions of the generated
    // harmony. Close dissonant clusters no longer dominate sparse slow lines.
    const int32_t scaleSize = scaleLength();
    const int32_t inversion = rng.rangeInt(0, 2);
    for (int32_t i = 0; i < 4; ++i) {
        const int32_t member = (i + inversion) % 3;
        const int32_t octave = (i + inversion) / 3;
        mComposition.padIntervals[i] = member * 2 + octave * scaleSize;
    }
    if (p.intent > 0 && rng.chance(0.34f)) mComposition.padIntervals[3] -= 1;

    mComposition.drumBody = clamp(0.14f + 0.78f * rng.uni(), 0.10f, 0.96f);
    mComposition.drumMetal = clamp(0.05f + (0.24f + 0.38f * p.motion) * rng.uni(), 0.02f, 0.78f);
    mComposition.drumNoise = clamp(0.10f + (0.24f + 0.28f * p.motion) * rng.uni(), 0.06f, 0.72f);
    mComposition.bassMotion *= 0.45f + 0.45f * p.motion;
    mComposition.leadVibratoDepth *= 0.42f;
    mComposition.leadAttack = clamp(mComposition.leadAttack + 0.18f * (0.50f - p.motion), 0.06f, 0.94f);
    mComposition.leadRelease = clamp(0.64f + 0.26f * p.noteLength, 0.64f, 0.92f);
    mComposition.padDetune *= 0.60f;

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
    for (int32_t model : mComposition.supportModels) mix(static_cast<uint32_t>(model));
    for (int32_t interval : mComposition.padIntervals) {
        mix(static_cast<uint32_t>(interval + 16));
    }
}


void MusicEngine::generateBoundaryGrammar(const StyleProfile& p) {
    // Boundary behavior is derived from the established composition identity.
    // It adds no prefabricated melody: openings reveal existing motifs, while
    // finite exports close them using generated phrase roles and cadence timing.
    mComposition.boundaryGrammarSeed = mComposition.arcSeed ^
        (mComposition.leadGrammarSeed << 9u | mComposition.leadGrammarSeed >> 23u) ^
        (mComposition.bassGrammarSeed << 19u | mComposition.bassGrammarSeed >> 13u) ^
        (mComposition.timbreGrammarSeed * 0x9e3779b9u) ^ 0x6a09e667u;
    Rng rng(mComposition.boundaryGrammarSeed);

    mComposition.introPhrases = clampInt32(
        3 + rng.rangeInt(0, 4) + (p.ambient ? 1 : 0) - (p.breakbeat ? 1 : 0), 3, 8);
    mComposition.introShape = rng.rangeInt(0, 5);
    mComposition.introCurve = clamp(0.72f + 1.12f * rng.uni(), 0.65f, 1.90f);

    // Reveal order varies by sound. At least one identity-bearing tonal layer is
    // present immediately; other layers arrive over complete phrases.
    std::array<int32_t, 4> order{{0, 1, 2, 3}}; // drums, bass, lead, chords
    for (int32_t i = 3; i > 0; --i) {
        const int32_t j = rng.rangeInt(0, i);
        std::swap(order[i], order[j]);
    }
    std::array<int32_t, 4> entry{};
    for (int32_t rank = 0; rank < 4; ++rank) {
        const int32_t spread = std::max(1, mComposition.introPhrases - 1);
        entry[order[rank]] = clampInt32((rank * spread + 1) / 3, 0, spread);
    }
    if (p.ambient) entry[0] = std::max(entry[0], 1);
    if (p.breakbeat || p.drum > 0.80f) entry[0] = std::min(entry[0], 1);
    if (p.bass > 0.82f) entry[1] = std::min(entry[1], 1);
    if (p.melody > 0.78f) entry[2] = std::min(entry[2], 1);
    if (p.chord > 0.72f) entry[3] = std::min(entry[3], 1);
    if (entry[1] > 0 && entry[2] > 0 && entry[3] > 0) entry[rng.rangeInt(1, 3)] = 0;
    if (p.intent == 0) {
        mComposition.introPhrases = std::min(mComposition.introPhrases, 3);
        entry[0] = std::min(entry[0], 1);
        entry[1] = std::min(entry[1], 1);
        entry[2] = 0;
        entry[3] = std::min(entry[3], 1);
    }
    mComposition.introDrumEntry = entry[0];
    mComposition.introBassEntry = entry[1];
    mComposition.introLeadEntry = entry[2];
    mComposition.introChordEntry = entry[3];

    mComposition.introForm.fill(PhraseType::Statement);
    for (int32_t i = 0; i < mComposition.introPhrases; ++i) {
        const float phase = static_cast<float>(i) /
            static_cast<float>(std::max(1, mComposition.introPhrases - 1));
        PhraseType role = PhraseType::Statement;
        const float r = rng.uni();
        if (i == mComposition.introPhrases - 1) {
            role = rng.chance(0.48f + 0.24f * p.melody) ? PhraseType::Statement : PhraseType::Hook;
        } else if (phase < 0.34f) {
            if (r < 0.20f) role = PhraseType::Afterimage;
            else if (r < 0.40f) role = PhraseType::Shadow;
            else if (r < 0.58f) role = PhraseType::Orbit;
            else if (r < 0.76f) role = PhraseType::Weave;
            else role = PhraseType::Suspension;
        } else if (phase < 0.72f) {
            if (r < 0.24f) role = PhraseType::Statement;
            else if (r < 0.45f) role = PhraseType::Answer;
            else if (r < 0.64f) role = PhraseType::Variation;
            else if (r < 0.82f) role = PhraseType::Tide;
            else role = PhraseType::Crystallize;
        } else {
            role = r < 0.42f ? PhraseType::Statement :
                   (r < 0.70f ? PhraseType::Answer : PhraseType::Hook);
        }
        mComposition.introForm[i] = role;
    }

    mComposition.outroPhrases = clampInt32(
        3 + rng.rangeInt(0, 4) + ((p.chord + p.melody) > 1.35f ? 1 : 0), 3, 8);
    mComposition.outroShape = rng.rangeInt(0, 4);
    mComposition.outroCurve = clamp(0.72f + 1.16f * rng.uni(), 0.65f, 1.95f);
    mComposition.outroCadencePos = rng.rangeInt(10, 14);
    mComposition.conclusiveOutro = rng.chance(0.30f + 0.24f * p.drama + 0.14f * p.chord);
    mComposition.outroForm.fill(PhraseType::Afterimage);
    for (int32_t i = 0; i < mComposition.outroPhrases; ++i) {
        const bool last = i == mComposition.outroPhrases - 1;
        const bool penultimate = i == mComposition.outroPhrases - 2;
        const float r = rng.uni();
        PhraseType role;
        if (last) {
            role = mComposition.conclusiveOutro ? PhraseType::Crystallize : PhraseType::Eclipse;
        } else if (penultimate && mComposition.conclusiveOutro) {
            role = r < 0.50f ? PhraseType::Afterimage : PhraseType::Answer;
        } else if (mComposition.conclusiveOutro) {
            if (r < 0.24f) role = PhraseType::Hook;
            else if (r < 0.48f) role = PhraseType::Afterimage;
            else if (r < 0.69f) role = PhraseType::Answer;
            else if (r < 0.86f) role = PhraseType::Tide;
            else role = PhraseType::Hinge;
        } else {
            if (r < 0.28f) role = PhraseType::Afterimage;
            else if (r < 0.50f) role = PhraseType::Shadow;
            else if (r < 0.70f) role = PhraseType::Eclipse;
            else if (r < 0.86f) role = PhraseType::Weave;
            else role = PhraseType::Tide;
        }
        mComposition.outroForm[i] = role;
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

    mComposition.drama = clamp(0.12f + p.drama * 0.80f + mRng.bipolar() * 0.08f, 0.12f, 0.90f);
    mComposition.deviceDepth = clamp(0.20f + mComposition.drama * 0.58f, 0.18f, 0.80f);
    mComposition.hookEmphasis = clamp(1.00f + p.melody * 0.34f + mComposition.drama * 0.18f, 1.0f, 1.55f);
    mComposition.surgeLift = clamp(0.96f + mComposition.drama * 0.38f + p.motion * 0.16f, 0.94f, 1.50f);

    // Every tune has a foreground and a foundation. Optional colors are chosen
    // as a bounded ensemble, never filled to a universal 12/15-layer minimum.
    mComposition.useKick = lane(1.0f, p.ambient ? 0.40f : 0.76f, 0.98f);
    mComposition.useSnare = lane(1.0f, p.ambient ? 0.28f : 0.62f, 0.90f);
    mComposition.useHat = lane(0.86f, 0.30f + 0.16f * p.motion, 0.46f + 0.38f * p.motion);
    mComposition.useOpenHat = lane(0.16f + 0.38f * p.motion, 0.24f, 0.60f);
    mComposition.usePerc = lane(0.38f + 0.36f * p.sync, 0.32f, 0.64f + 0.18f * p.motion);
    mComposition.useBass = 0.90f + 0.16f * mRng.uni();
    mComposition.useChord = 0.50f + 0.34f * p.chord + 0.12f * mRng.uni();
    mComposition.useLead = 1.00f + 0.12f * mRng.uni();
    mComposition.useTexture = 0.0f;
    mComposition.useFx = 0.0f;
    mComposition.useGrain = 0.0f;

    std::array<float*, 16> support{{
        &mComposition.useArp, &mComposition.useCounter, &mComposition.useStab,
        &mComposition.useDrone, &mComposition.useSpark, &mComposition.useEcho,
        &mComposition.useOrbit, &mComposition.useBloom, &mComposition.useGlyph,
        &mComposition.useSub, &mComposition.useSheen, &mComposition.usePluck,
        &mComposition.useBell, &mComposition.usePulse, &mComposition.useComet,
        &mComposition.useRotor
    }};
    const std::array<float, 16> affinity{{
        0.35f + 0.90f * p.motion, 0.70f + 0.55f * p.melody, 0.28f + 0.50f * p.chord,
        0.16f + 0.54f * p.space, 0.18f + 0.45f * p.brightness, 0.28f + 0.55f * p.sync,
        0.26f + 0.52f * p.melodyRun, 0.22f + 0.58f * p.chord, 0.16f + 0.50f * p.sync,
        0.14f + 0.44f * p.bass, 0.18f + 0.42f * p.brightness, 0.46f + 0.48f * p.sync,
        0.34f + 0.45f * p.chord, 0.22f + 0.65f * p.motion, 0.14f + 0.35f * p.space,
        0.22f + 0.50f * p.chord
    }};
    std::array<float, 16> rank{};
    for (size_t i = 0; i < support.size(); ++i) {
        *support[i] = 0.0f;
        rank[i] = affinity[i] * (0.12f + 0.88f * mRng.uni());
    }
    const int32_t supportCount = clampInt32(static_cast<int32_t>(std::lround(p.supportTarget)), 1, 6);
    for (int32_t selected = 0; selected < supportCount; ++selected) {
        const auto best = std::max_element(rank.begin(), rank.end());
        const size_t index = static_cast<size_t>(best - rank.begin());
        *support[index] = 0.30f + 0.44f * mRng.uni();
        *best = -1.0f;
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

    // The final boundary policy is generated later from this composition's
    // identity. Live playback keeps developing until the listener changes tune.
    mComposition.conclusiveOutro = mRng.chance(
        0.34f + 0.22f * p.drama + 0.10f * p.chord);

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
    mComposition.octaveBias = (p.intent == 0 || lowRegisterStyle) ? 2 : (highRegisterStyle ? 3 : 2);
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
    generateBoundaryGrammar(p);
    generatePhrasePlan(p);

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
    const float reference = mPattern.drumReference[track][pos];
    const float spread = reference > 0.015f ? (0.06f + 0.13f * p.motion) :
                                             (0.015f + 0.055f * p.motion);
    auto develop = [&](float& value, float scale) {
        value = clamp(reference + 0.92f * (value - reference) + delta * scale,
                      std::max(0.0f, reference - spread),
                      std::min(0.98f, reference + spread));
    };
    switch (track) {
        case 0: develop(mPattern.kick[pos], 0.70f); break;
        case 1: develop(mPattern.snare[pos], 0.85f); break;
        case 2: develop(mPattern.hat[pos], p.trapHats ? 1.35f : 0.90f); break;
        case 3: develop(mPattern.perc[pos], 1.40f); break;
        default: develop(mPattern.openHat[pos], 0.55f); break;
    }
    repairPattern();
}

void MusicEngine::rememberDrumReference() {
    mPattern.drumReference[0] = mPattern.kick;
    mPattern.drumReference[1] = mPattern.snare;
    mPattern.drumReference[2] = mPattern.hat;
    mPattern.drumReference[3] = mPattern.perc;
    mPattern.drumReference[4] = mPattern.openHat;
}


void MusicEngine::repairPattern() {
    // Repair the composed rhythm, not the currently selected performance lens.
    const StyleProfile p = profile(mPattern.style, false);
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

    // Rests in the generated hat word are intentional. A repair must not add
    // eighth/sixteenth-note hits back into every slow or sparse composition.

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

int32_t MusicEngine::effectiveIntroPhrases() const {
    int32_t phrases = clampInt32(mComposition.introPhrases, 1, kBoundarySlots);
    if (mExportSinglePieceMode) {
        const int32_t total = std::max(2, mComposition.pieceSteps / kPhraseSteps);
        phrases = std::min(phrases, std::max(1, total / 3));
    }
    return phrases;
}

int32_t MusicEngine::effectiveOutroPhrases() const {
    if (!mExportSinglePieceMode) return 0;
    const int32_t total = std::max(2, mComposition.pieceSteps / kPhraseSteps);
    const int32_t room = std::max(1, total - effectiveIntroPhrases() - 1);
    const int32_t proportionalCap = std::max(1, total / 3);
    return std::min(std::min(clampInt32(mComposition.outroPhrases, 1, kBoundarySlots), room),
                    proportionalCap);
}

int32_t MusicEngine::outroStartStep() const {
    if (!mExportSinglePieceMode) return mComposition.pieceSteps;
    return std::max(0, mComposition.pieceSteps - effectiveOutroPhrases() * kPhraseSteps);
}

int32_t MusicEngine::outroGravitySteps() const {
    return effectiveOutroPhrases() * kPhraseSteps;
}

float MusicEngine::boundaryEnvelope(int32_t phrase, int32_t phrasePos, int32_t entryPhrase,
                                    int32_t totalPhrases, float curve) const {
    totalPhrases = std::max(1, totalPhrases);
    entryPhrase = clampInt32(entryPhrase, 0, totalPhrases - 1);
    const float position = static_cast<float>(phrase) +
        (static_cast<float>(clampInt32(phrasePos, 0, kPhraseSteps - 1)) + 0.35f) /
        static_cast<float>(kPhraseSteps);
    const float span = std::max(0.75f, static_cast<float>(totalPhrases - entryPhrase));
    float x = clamp01((position - static_cast<float>(entryPhrase)) / span);
    x = x * x * (3.0f - 2.0f * x);
    return std::pow(x, clamp(curve, 0.55f, 2.20f));
}

int32_t MusicEngine::currentChordRoot(int32_t step) const {
    const int32_t progression = std::max(1, std::min(kMaxProgressionSlots, mComposition.progressionLength));
    int32_t bar = std::max(0, step) / (kPhraseSteps * clampInt32(mComposition.harmonicSpan, 1, 3));
    int32_t root = mComposition.chordRoot[bar % progression];

    // Harmony evolves only away from the recurring hook. It returns to the
    // original progression during identity-return windows, so the sound keeps
    // an audible home even in an indefinitely held generation.
    const bool hookBar = phrasePlanForStep(step).section == SectionType::Hook;
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

    // A conclusive finite export approaches home over several complete phrases
    // rather than snapping to the tonic in the final seconds.
    if (mExportSinglePieceMode && mComposition.conclusiveOutro && step >= outroStartStep()) {
        const int32_t total = std::max(1, mComposition.pieceSteps - outroStartStep());
        const float t0 = clamp01(static_cast<float>(step - outroStartStep()) / static_cast<float>(total));
        const float t = t0 * t0 * (3.0f - 2.0f * t0);
        root = static_cast<int32_t>(std::lround(static_cast<float>(root) * (1.0f - t)));
        if (step >= mComposition.pieceSteps - kPhraseSteps + mComposition.outroCadencePos) root = 0;
    }
    return clampInt32(root, -7, 12);
}

void MusicEngine::generatePhrasePlan(const StyleProfile& p) {
    // A single form owns recall, questions, answers and arrival. It consumes no
    // candidate RNG, and it never adds an instrument to the selected ensemble.
    mComposition.phrasePlanSeed = mixSeed(mComposition.arcSeed ^
        mComposition.leadGrammarSeed ^ 0x3051a71eu);
    Rng rng(mComposition.phrasePlanSeed);
    mComposition.developmentCycle = rng.rangeInt(10, 18);
    mComposition.recallPhrases = rng.rangeInt(1, 2);
    mComposition.harmonicSpan = p.intent == 0 ? rng.rangeInt(2, 3) :
        (p.intent == 1 ? rng.rangeInt(1, 2) : 1);
    mComposition.deviceRotation = rng.rangeInt(0, 4);
    mComposition.phraseJoint = rng.rangeInt(6, 11);
}

float MusicEngine::supportLaneWeight(int32_t lane) const {
    const std::array<float, kSupportLanes> weights{{
        mComposition.useArp, mComposition.useCounter, mComposition.useStab,
        mComposition.useDrone, mComposition.useSpark, mComposition.useEcho,
        mComposition.useOrbit, mComposition.useBloom, mComposition.useGlyph,
        mComposition.useSub, mComposition.useSheen, mComposition.usePluck,
        mComposition.useBell, mComposition.usePulse, mComposition.useComet,
        mComposition.useRotor
    }};
    return lane >= 0 && lane < kSupportLanes ? weights[lane] : 0.0f;
}

void MusicEngine::constrainSupportPalette() {
    std::array<float*, kSupportLanes> weights{{
        &mComposition.useArp, &mComposition.useCounter, &mComposition.useStab,
        &mComposition.useDrone, &mComposition.useSpark, &mComposition.useEcho,
        &mComposition.useOrbit, &mComposition.useBloom, &mComposition.useGlyph,
        &mComposition.useSub, &mComposition.useSheen, &mComposition.usePluck,
        &mComposition.useBell, &mComposition.usePulse, &mComposition.useComet,
        &mComposition.useRotor
    }};
    // Edited legacy recipes cannot resurrect every former layer at once.
    for (int32_t retained = 0; retained < kSupportLanes; ++retained) {
        int32_t best = -1;
        for (int32_t i = 0; i < kSupportLanes; ++i) {
            if (weights[i] && (best < 0 || *weights[i] > *weights[best])) best = i;
        }
        if (best < 0) break;
        if (retained >= 6) *weights[best] = 0.0f;
        weights[best] = nullptr;
    }
    mComposition.useTexture = mComposition.useFx = mComposition.useGrain = 0.0f;
}

MusicEngine::PhrasePlan MusicEngine::phrasePlanForStep(int32_t step) const {
    PhrasePlan plan;
    const int32_t phrase = std::max(0, step) / kPhraseSteps;
    const int32_t intro = effectiveIntroPhrases();
    const int32_t length = clampInt32(mComposition.developmentCycle, 10, 18);
    const int32_t local = std::max(0, phrase - intro);
    plan.cycle = local / length;
    plan.position = local % length;
    const uint32_t cycleHash = mixSeed(mComposition.phrasePlanSeed ^
        (static_cast<uint32_t>(plan.cycle) + 1u) * 0x9e3779b9u);
    const uint32_t phraseHash = mixSeed(cycleHash ^
        (static_cast<uint32_t>(plan.position) + 1u) * 0x85ebca6bu);
    plan.joint = clampInt32(mComposition.phraseJoint + static_cast<int32_t>((cycleHash >> 8u) % 3u) - 1, 5, 12);
    plan.rhythmShift = static_cast<int32_t>((phraseHash >> 3u) % 5u) - 2;
    plan.turnPosition = 3 + static_cast<int32_t>((phraseHash >> 11u) % 10u);
    plan.turnDirection = ((phraseHash >> 20u) & 1u) ? 1 : -1;
    plan.cadenceTone = ((phraseHash >> 21u) & 1u) ? 2 : 4;

    if (phrase < intro) {
        plan.section = SectionType::Intro;
        plan.role = mComposition.introForm[clampInt32(phrase, 0, kBoundarySlots - 1)];
        plan.rhythmShift = 0;
        return plan;
    }
    if (mExportSinglePieceMode && step >= outroStartStep()) {
        plan.section = SectionType::Outro;
        plan.role = mComposition.outroForm[clampInt32(phrase - outroStartStep() / kPhraseSteps, 0, kBoundarySlots - 1)];
        plan.rhythmShift = 0;
        plan.resolves = mComposition.conclusiveOutro;
        return plan;
    }

    const int32_t recall = clampInt32(mComposition.recallPhrases, 1, 2);
    if (plan.position < recall) {
        // Theme recall is one or two phrases, never a whole eight-bar section.
        plan.section = SectionType::Hook;
        plan.role = plan.position == 0 ? PhraseType::Hook : PhraseType::Repeat;
        plan.rhythmShift = 0;
        plan.resolves = true;
        return plan;
    }

    const bool answer = plan.position == recall ||
        ((plan.position + mComposition.deviceRotation) % 3) == 1;
    if (plan.position == length - 3) {
        plan.section = SectionType::Hinge;
        plan.role = PhraseType::Hinge;
        plan.device = MusicalDevice::HeldQuestion;
        plan.cadenceTone = ((cycleHash >> 17u) & 1u) ? 1 : 5;
        plan.articulation = 1.18f;
    } else if (plan.position == length - 2) {
        plan.section = SectionType::Surge;
        plan.role = PhraseType::Surge;
        plan.device = MusicalDevice::Confluence;
        plan.resolves = true;
        plan.articulation = 0.94f;
    } else if (plan.position == length - 1) {
        plan.section = SectionType::Shadow;
        plan.role = PhraseType::Afterimage;
        plan.device = MusicalDevice::OpenRelease;
        plan.articulation = 1.24f;
    } else if (answer) {
        plan.section = SectionType::Weave;
        plan.role = PhraseType::Answer;
        plan.device = ((phraseHash >> 7u) & 1u) ? MusicalDevice::Counterweight : MusicalDevice::PocketExchange;
        plan.articulation = 0.92f + 0.20f * static_cast<float>((phraseHash >> 23u) & 1u);
    } else {
        static constexpr std::array<PhraseType, 6> development{{
            PhraseType::Variation, PhraseType::Mirror, PhraseType::Tide,
            PhraseType::Crystallize, PhraseType::Weave, PhraseType::Statement
        }};
        plan.role = development[(phraseHash >> 6u) % development.size()];
        plan.section = SectionType::Variation;
        plan.device = MusicalDevice::PocketExchange;
    }

    if (answer && plan.device != MusicalDevice::HeldQuestion &&
        plan.device != MusicalDevice::Confluence && plan.device != MusicalDevice::OpenRelease) {
        // Answers change who speaks. A supporting voice is promoted rather than
        // layered on top of a second complete lead. All choices already exist.
        std::array<int32_t, kSupportLanes + 1> owners{};
        int32_t count = 0;
        if (mComposition.useBass > 0.02f) owners[count++] = 1;
        for (int32_t lane = 0; lane < kSupportLanes; ++lane) {
            const bool harmonic = lane == 2 || lane == 3 || lane == 7 || lane == 15;
            if (!harmonic && lane != 8 && lane != 9 && supportLaneWeight(lane) > 0.02f) owners[count++] = lane + 3;
        }
        if (count > 0) plan.instrumentSlot = owners[(phraseHash >> 13u) % static_cast<uint32_t>(count)];
    }
    if (plan.device == MusicalDevice::OpenRelease && mComposition.useChord > 0.02f &&
        (cycleHash & 3u) == 1u) plan.instrumentSlot = 2;
    return plan;
}

MusicEngine::SectionType MusicEngine::currentSectionType(int32_t step) const {
    return phrasePlanForStep(step).section;
}

MusicEngine::PhraseType MusicEngine::currentPhraseType(int32_t step) const {
    return phrasePlanForStep(step).role;
}

MusicEngine::ForegroundNote MusicEngine::foregroundNoteForStep(int32_t step, int32_t chordRoot) const {
    const PhrasePlan plan = phrasePlanForStep(step);
    const int32_t position = std::max(0, step) & 15;
    const int32_t phraseStart = std::max(0, step) - position;
    const bool recall = plan.section == SectionType::Hook;
    const bool slow = mPattern.intent == 0;
    const uint32_t phraseHash = mixSeed(mComposition.phrasePlanSeed ^
        (static_cast<uint32_t>(phraseStart / kPhraseSteps) + 1u) * 0x27d4eb2du);
    std::array<int32_t, kPhraseSteps> degrees{};
    std::array<float, kPhraseSteps> gates{};
    std::array<float, kPhraseSteps> lengths{};
    int32_t lastDestination = -1;
    int32_t lastDegree = chordRoot;
    const int32_t octave = plan.instrumentSlot == 1 ? 0 :
        (plan.instrumentSlot >= 3 ? std::max(1, mComposition.octaveBias - 1) : mComposition.octaveBias);
    const int32_t scaleSize = scaleLength();

    for (int32_t source = 0; source < kPhraseSteps; ++source) {
        bool rest = false;
        float gate = 0.0f, duration = 0.0f;
        int32_t degree = grammarDegree(plan.role, source, chordRoot, rest, gate, duration);
        if (rest) continue;
        int32_t destination = source;
        if (!recall && plan.section != SectionType::Intro && plan.section != SectionType::Outro &&
            source != 0 && source != 8 && source != 15) {
            const uint32_t cellHash = mixSeed(phraseHash ^ static_cast<uint32_t>(source / 3 + 1) * 0x85ebca6bu);
            const int32_t shift = clampInt32(plan.rhythmShift + static_cast<int32_t>(cellHash % 3u) - 1,
                                             slow ? -1 : -2, slow ? 1 : 2);
            const int32_t low = source < 8 ? 1 : 9;
            const int32_t high = source < 8 ? 7 : 14;
            destination = clampInt32(source + shift, low, high);
            destination = std::max(destination, lastDestination + 1);
            if (destination > high) continue;
        }
        if (destination <= lastDestination) continue;
        degree = applyThemeTransform(degree, phraseStart + destination, destination, chordRoot, plan.role);
        int32_t relative = degree - chordRoot;
        if (!recall && source != 0 && source != 8) {
            const int32_t turnDistance = std::abs(destination - plan.turnPosition);
            if (turnDistance <= 2) relative += plan.turnDirection * (turnDistance == 0 ? 2 : 1);
            if (plan.device == MusicalDevice::Counterweight && destination >= plan.joint) {
                relative = mComposition.motifA[8] - relative / 2;
            }
        }
        degree = chordRoot + relative;
        // Middle-register slow music and bounded voice leading. Equivalent
        // scale octaves preserve contour without the old soprano register tail.
        const int32_t lowerMidi = plan.instrumentSlot == 1 ? 33 : (slow ? 52 : 48);
        const int32_t upperMidi = plan.instrumentSlot == 1 ? 62 : (slow ? 78 : 91);
        for (int32_t pass = 0; pass < 4; ++pass) {
            const int32_t midi = scaleDegreeToMidi(degree, octave);
            if (midi > upperMidi) degree -= scaleSize;
            else if (midi < lowerMidi) degree += scaleSize;
        }
        if (lastDestination >= 0 && !recall) {
            const int32_t maxLeap = slow ? 4 : 6;
            degree = clampInt32(degree, lastDegree - maxLeap, lastDegree + maxLeap);
        }
        gates[destination] = evolvedGate(gate, phraseStart + destination, destination,
                                         plan.role, plan.section, MelodyLayer::Lead);
        degrees[destination] = degree;
        lengths[destination] = duration;
        lastDegree = degree;
        lastDestination = destination;
    }

    if (lastDestination >= 0) {
        // Endings belong to the multi-phrase argument. Questions and continuing
        // thoughts land on a third/fifth/suspension; recall and arrival resolve.
        int32_t landing = chordRoot + (plan.resolves ? 0 : plan.cadenceTone);
        for (int32_t pass = 0; pass < 4; ++pass) {
            if (landing - lastDegree > scaleSize / 2) landing -= scaleSize;
            else if (lastDegree - landing > scaleSize / 2) landing += scaleSize;
        }
        degrees[lastDestination] = landing;
    }
    if (plan.device == MusicalDevice::Confluence) {
        gates[plan.joint] = std::max(gates[plan.joint], 0.86f);
        degrees[plan.joint] = chordRoot + (mComposition.padIntervals[1] % scaleSize);
        lengths[plan.joint] = 1.6f;
    }
    if (plan.device == MusicalDevice::OpenRelease) {
        for (int32_t i = 0; i < kPhraseSteps; ++i) {
            if (i > plan.joint && i != lastDestination) gates[i] = 0.0f;
            gates[i] *= 0.74f;
        }
    }
    if (plan.instrumentSlot == 2) {
        for (int32_t i = 0; i < kPhraseSteps; ++i) if (i != 0 && i != plan.joint) gates[i] = 0.0f;
        gates[0] = 0.70f;
        degrees[0] = chordRoot;
    }

    // Cadence, arrival and ownership edits above can create new notes after
    // source placement. Fold the final score, preserving every pitch class.
    // Slow lines choose the nearest allowed octave of each next note rather
    // than jumping down to the root at the end of every phrase.
    const int32_t lowerMidi = plan.instrumentSlot == 1 ? 33 : (slow ? 52 : 48);
    const int32_t upperMidi = plan.instrumentSlot == 1 ? 62 : (slow ? 78 : 91);
    int32_t precedingMidi = -1;
    for (int32_t i = 0; i < kPhraseSteps; ++i) {
        if (gates[i] <= 0.02f) continue;
        int32_t bestDegree = degrees[i];
        int32_t bestScore = 100000;
        for (int32_t displacement = -5; displacement <= 5; ++displacement) {
            const int32_t candidate = degrees[i] + displacement * scaleSize;
            const int32_t midi = scaleDegreeToMidi(candidate, octave);
            if (midi < lowerMidi || midi > upperMidi) continue;
            const int32_t score = slow && precedingMidi >= 0 ?
                std::abs(midi - precedingMidi) * 12 + std::abs(displacement) : std::abs(displacement);
            if (score < bestScore) {
                bestScore = score;
                bestDegree = candidate;
            }
        }
        degrees[i] = bestDegree;
        precedingMidi = scaleDegreeToMidi(bestDegree, octave);
    }

    ForegroundNote result;
    result.instrumentSlot = plan.instrumentSlot;
    result.octave = octave;
    result.gate = gates[position];
    result.rest = result.gate <= 0.02f;
    result.degree = degrees[position];
    int32_t gap = 1;
    while (position + gap < kPhraseSteps && gates[position + gap] <= 0.02f) ++gap;
    const float connected = slow ? 0.90f : 0.73f;
    result.duration = slow ?
        std::max(0.30f, static_cast<float>(gap) * std::min(0.98f, connected * plan.articulation)) :
        std::max(0.30f, std::min(static_cast<float>(gap) * connected,
            std::max(lengths[position], 0.45f)) * plan.articulation);
    if (plan.device == MusicalDevice::HeldQuestion && position == lastDestination) result.duration = std::max(result.duration, 1.45f);
    return result;
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
        if (phrasePos == 15 && (phrase == PhraseType::Hook || phrase == PhraseType::Repeat)) best = chordRoot;
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
    if (phrasePos == 15 && (phrase == PhraseType::Hook || phrase == PhraseType::Repeat)) rel = 0;
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

    if (phrasePos == 0 && mComposition.melodicGravity > 0.56f) rel = 0;
    if (phrasePos == 15 && (phrase == PhraseType::Hook || phrase == PhraseType::Repeat)) rel = 0;
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
    finalizeCurrentHistoryEntry();
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
    mNovelty = 0.0f;
    mDcInL = mDcInR = mDcOutL = mDcOutR = 0.0f;
    // Manual Next preserves the full composition search.
    generateSeededSong(mPendingSongSeed);
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


        applyMaster(left, right);

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
    const PhrasePlan phrasePlan = phrasePlanForStep(pieceStep);
    const SectionType section = phrasePlan.section;
    const PhraseType phrase = phrasePlan.role;
    const bool arrivalJoint = phrasePlan.device == MusicalDevice::Confluence && p16 == phrasePlan.joint;
    const EvolutionFrame evolution = evolutionFrameForStep(pieceStep);
    const bool finiteOutro = mExportSinglePieceMode && section == SectionType::Outro;
    const int32_t outroLocalStep = finiteOutro ? std::max(0, pieceStep - outroStartStep()) : 0;
    const int32_t outroPhraseIndex = finiteOutro ? (outroLocalStep / kPhraseSteps) : 0;
    const bool finalOutroPhrase = finiteOutro &&
        outroPhraseIndex >= std::max(0, effectiveOutroPhrases() - 1);
    const bool closingCadence = finalOutroPhrase && mComposition.conclusiveOutro &&
        p16 == mComposition.outroCadencePos;
    const bool afterCadence = finalOutroPhrase && mComposition.conclusiveOutro &&
        p16 > mComposition.outroCadencePos;

    if ((p16 == 0) && mDevelopmentRng.chance(0.18f + 0.16f * p.rough)) {
        mutateDrumsOnly();
    }
    if (mDevelopmentRng.chance(0.0045f + 0.0060f * p.rough + 0.0040f * mNovelty)) {
        mutateDrumsOnly();
    }

    bool eventHappened = false;

    // These factors control note activity only. Phrase, section, opening,
    // ending and device decisions never turn the band's note amplitudes up
    // or down together; each played note keeps its normal role and accent.
    float drumActivity = 1.0f;
    float bassActivity = 1.0f;
    float leadActivity = 1.0f;
    float chordActivity = 1.0f;
    switch (phrase) {
        case PhraseType::Suspension:
            drumActivity = p.ambient ? 0.76f : clamp(0.88f - 0.36f * mComposition.deviceDepth, 0.38f, 0.88f);
            bassActivity = clamp(0.92f - 0.22f * mComposition.deviceDepth, 0.48f, 0.92f);
            leadActivity = clamp(0.92f - 0.16f * mComposition.deviceDepth, 0.58f, 0.92f);
            chordActivity = 1.08f + 0.34f * mComposition.deviceDepth;
            break;
        case PhraseType::Hook:
            drumActivity = 1.02f + 0.08f * mComposition.drama;
            bassActivity = 1.02f + 0.06f * mComposition.drama;
            leadActivity = 1.14f + 0.36f * mComposition.hookEmphasis;
            chordActivity = 0.98f + 0.10f * mComposition.drama;
            break;
        case PhraseType::Mirror:
            drumActivity = 0.96f;
            bassActivity = 1.04f;
            leadActivity = 1.08f;
            chordActivity = 1.06f;
            break;
        case PhraseType::Orbit:
            drumActivity = 0.86f;
            bassActivity = 1.08f;
            leadActivity = 0.94f;
            chordActivity = 1.18f;
            break;
        case PhraseType::Cascade:
            drumActivity = 1.04f + 0.12f * mComposition.drama;
            bassActivity = 1.02f + 0.08f * mComposition.drama;
            leadActivity = 1.16f + 0.20f * mComposition.surgeLift;
            chordActivity = 0.94f + 0.08f * mComposition.drama;
            break;
        case PhraseType::Surge:
            drumActivity = 1.06f + 0.20f * mComposition.surgeLift;
            bassActivity = 1.04f + 0.16f * mComposition.surgeLift;
            leadActivity = 1.08f + 0.28f * mComposition.surgeLift;
            chordActivity = 0.88f + 0.08f * mComposition.drama;
            break;
        case PhraseType::Answer:
            leadActivity = 0.98f;
            bassActivity = 0.92f;
            break;
        case PhraseType::Variation:
            leadActivity = 1.08f;
            bassActivity = 1.04f;
            break;
        case PhraseType::Crystallize:
            drumActivity = 0.88f;
            bassActivity = 0.96f;
            leadActivity = 1.16f;
            chordActivity = 1.10f;
            break;
        case PhraseType::Eclipse:
            drumActivity = 0.62f;
            bassActivity = 0.78f;
            leadActivity = 0.68f;
            chordActivity = 1.24f;
            break;
        case PhraseType::Afterimage:
            drumActivity = 0.78f;
            bassActivity = 0.86f;
            leadActivity = 0.82f;
            chordActivity = 1.18f;
            break;
        case PhraseType::Weave:
            drumActivity = 0.92f;
            bassActivity = 0.98f;
            leadActivity = 1.10f;
            chordActivity = 0.96f;
            break;
        case PhraseType::Tide:
            drumActivity = 0.86f;
            bassActivity = 1.04f;
            leadActivity = 1.02f;
            chordActivity = 1.12f;
            break;
        case PhraseType::Hinge:
            drumActivity = p16 < evolution.focusPosition ? 0.88f : 1.06f;
            bassActivity = p16 < evolution.focusPosition ? 0.96f : 1.08f;
            leadActivity = p16 < evolution.focusPosition ? 0.94f : 1.12f;
            chordActivity = 1.04f;
            break;
        case PhraseType::Shadow:
            drumActivity = 0.58f;
            bassActivity = 0.82f;
            leadActivity = 0.72f;
            chordActivity = 1.22f;
            break;
        default:
            break;
    }

    switch (section) {
        case SectionType::Intro: {
            const int32_t introPhrase = pieceStep / kPhraseSteps;
            const int32_t totalIntro = effectiveIntroPhrases();
            auto reveal = [&](int32_t entry, float curveOffset) {
                float v = boundaryEnvelope(introPhrase, p16, entry, totalIntro,
                                           mComposition.introCurve + curveOffset);
                if (introPhrase == entry) {
                    const float within = static_cast<float>(p16 + 1) /
                                         static_cast<float>(kPhraseSteps);
                    v = std::max(v, 0.12f + 0.48f * within);
                }
                return clamp01(v);
            };
            float drumReveal = reveal(mComposition.introDrumEntry, 0.10f);
            float bassReveal = reveal(mComposition.introBassEntry, -0.08f);
            float leadReveal = reveal(mComposition.introLeadEntry, 0.04f);
            float chordReveal = reveal(mComposition.introChordEntry, -0.12f);

            // Six generated opening geometries. They alter reveal relationships,
            // never the stored motif sentences, so each opening remains part of
            // the same composition rather than becoming a generic buildup.
            switch (mComposition.introShape) {
                case 0: // germ: harmony and bass establish home before impact
                    drumReveal *= 0.78f;
                    chordReveal = std::max(chordReveal, 0.26f);
                    break;
                case 1: // aperture: rhythm opens first, melody enters through gaps
                    drumReveal = std::min(1.0f, drumReveal * 1.14f + 0.08f);
                    leadReveal *= 0.86f;
                    break;
                case 2: // counterlight: lead and chord identify the sound first
                    leadReveal = std::min(1.0f, leadReveal * 1.10f + 0.06f);
                    chordReveal = std::min(1.0f, chordReveal * 1.08f + 0.06f);
                    drumReveal *= 0.82f;
                    break;
                case 3: // tidal reveal: alternating phrase pressure
                    if ((introPhrase & 1) != 0) {
                        drumReveal *= 0.84f;
                        leadReveal = std::min(1.0f, leadReveal * 1.12f);
                    } else {
                        bassReveal = std::min(1.0f, bassReveal * 1.10f);
                    }
                    break;
                case 4: { // negative reveal: a brief breathing notch before arrival
                    const float center = static_cast<float>(totalIntro - 1) * 0.55f;
                    const float distance = std::abs(static_cast<float>(introPhrase) - center);
                    const float notch = 1.0f - 0.24f * clamp01(1.0f - distance / 1.5f);
                    drumReveal *= notch;
                    chordReveal = std::min(1.0f, chordReveal + 0.10f * (1.0f - notch));
                    break;
                }
                case 5: // braid: bass and lead trade foreground while layers assemble
                default:
                    if ((introPhrase & 1) == 0) leadReveal *= 0.82f;
                    else bassReveal *= 0.84f;
                    break;
            }

            drumActivity *= 0.04f + 0.96f * drumReveal;
            bassActivity *= 0.06f + 0.94f * bassReveal;
            leadActivity *= 0.05f + 0.95f * leadReveal;
            chordActivity *= 0.06f + 0.98f * chordReveal;
            break;
        }
        case SectionType::Hook:
            drumActivity *= 1.03f;
            bassActivity *= 1.04f;
            leadActivity *= 1.18f;
            break;
        case SectionType::Variation:
            drumActivity *= 0.96f;
            bassActivity *= 1.00f;
            leadActivity *= 1.06f;
            chordActivity *= 1.03f;
            break;
        case SectionType::Suspension:
            drumActivity *= clamp(0.95f - 0.30f * mComposition.deviceDepth, 0.42f, 0.95f);
            bassActivity *= clamp(0.96f - 0.24f * mComposition.deviceDepth, 0.50f, 0.96f);
            leadActivity *= clamp(1.02f - 0.16f * mComposition.deviceDepth, 0.66f, 1.02f);
            chordActivity *= 1.08f + 0.26f * mComposition.deviceDepth;
            break;
        case SectionType::Mirror:
            drumActivity *= 0.94f;
            bassActivity *= 1.06f;
            leadActivity *= 1.10f;
            chordActivity *= 1.04f;
            break;
        case SectionType::Orbit:
            drumActivity *= 0.86f;
            bassActivity *= 1.10f;
            leadActivity *= 0.96f;
            chordActivity *= 1.16f;
            break;
        case SectionType::Weave:
            drumActivity *= 0.94f;
            bassActivity *= 0.98f;
            leadActivity *= 1.12f;
            chordActivity *= 0.96f;
            break;
        case SectionType::Tide:
            drumActivity *= 0.86f + 0.18f * static_cast<float>(p16) / 15.0f;
            bassActivity *= 1.00f + 0.08f * evolution.registerTide;
            leadActivity *= 1.02f + 0.10f * std::abs(evolution.registerTide);
            chordActivity *= 1.10f;
            break;
        case SectionType::Hinge:
            drumActivity *= p16 < evolution.focusPosition ? 0.90f : 1.08f;
            bassActivity *= p16 < evolution.focusPosition ? 0.96f : 1.08f;
            leadActivity *= p16 < evolution.focusPosition ? 0.94f : 1.12f;
            break;
        case SectionType::Shadow:
            drumActivity *= 0.62f;
            bassActivity *= 0.84f;
            leadActivity *= 0.74f;
            chordActivity *= 1.20f;
            break;
        case SectionType::Cascade:
            drumActivity *= 1.06f + 0.08f * mComposition.drama;
            bassActivity *= 1.04f;
            leadActivity *= 1.14f + 0.16f * mComposition.surgeLift;
            break;
        case SectionType::Surge:
            drumActivity *= 1.04f + 0.12f * mComposition.surgeLift;
            bassActivity *= 1.04f + 0.10f * mComposition.surgeLift;
            leadActivity *= 1.10f + 0.20f * mComposition.surgeLift;
            break;
        case SectionType::Outro: {
            const int32_t start = outroStartStep();
            const int32_t total = std::max(1, mComposition.pieceSteps - start);
            const int32_t local = std::max(0, pieceStep - start);
            float progress = clamp01((static_cast<float>(local) +
                                      static_cast<float>(p16) / kPhraseSteps) /
                                     static_cast<float>(total));
            progress = progress * progress * (3.0f - 2.0f * progress);
            progress = std::pow(progress, clamp(mComposition.outroCurve, 0.60f, 2.0f));

            if (mComposition.conclusiveOutro) {
                // A conclusion narrows the field while keeping the identity-bearing
                // bass, hook, and harmony available for a generated final cadence.
                float drum = 1.0f - 0.68f * progress;
                float bass = 0.96f + 0.18f * progress;
                float lead = 1.00f - 0.20f * progress;
                float chord = 1.00f + 0.30f * progress;
                switch (mComposition.outroShape) {
                    case 0: // gravity: low and harmonic layers take control
                        bass += 0.12f * progress;
                        chord += 0.16f * progress;
                        break;
                    case 1: // compression: rhythmic detail collapses toward one point
                        drum -= 0.16f * progress;
                        lead -= 0.10f * progress;
                        break;
                    case 2: // returning orbit: hook remains visible while space opens
                        lead += 0.12f * (1.0f - progress);
                        chord += 0.10f * progress;
                        break;
                    case 3: { // framed silence before the final statement
                        const float preCadence = clamp01((progress - 0.62f) / 0.24f);
                        const float release = clamp01((progress - 0.88f) / 0.10f);
                        const float hush = 1.0f - 0.48f * preCadence * (1.0f - release);
                        drum *= hush;
                        bass *= 0.82f + 0.18f * hush;
                        lead *= 0.76f + 0.24f * hush;
                        break;
                    }
                    case 4: // luminous closure: upper identity leads into tonal home
                    default:
                        lead += 0.14f * progress;
                        drum -= 0.08f * progress;
                        break;
                }
                drumActivity *= clamp(drum, 0.10f, 1.08f);
                bassActivity *= clamp(bass, 0.60f, 1.36f);
                leadActivity *= clamp(lead, 0.48f, 1.28f);
                chordActivity *= clamp(chord, 0.70f, 1.54f);
            } else {
                // A dissolve is staggered: different layers leave at different
                // rates, avoiding a mechanically synchronized master fade.
                const float d = std::pow(1.0f - progress, 1.20f);
                const float b = std::pow(1.0f - progress, 0.92f);
                const float l = std::pow(1.0f - progress, 1.05f);
                const float c = std::pow(1.0f - progress, 0.78f);
                switch (mComposition.outroShape) {
                    case 0: drumActivity *= d; bassActivity *= b; leadActivity *= l; chordActivity *= c; break;
                    case 1: drumActivity *= d * d; bassActivity *= b; leadActivity *= std::sqrt(l); chordActivity *= c; break;
                    case 2: drumActivity *= std::sqrt(d); bassActivity *= b * b; leadActivity *= l; chordActivity *= c; break;
                    case 3: drumActivity *= d; bassActivity *= std::sqrt(b); leadActivity *= l * l; chordActivity *= c; break;
                    case 4:
                    default: drumActivity *= d; bassActivity *= b; leadActivity *= std::sqrt(l); chordActivity *= c * c; break;
                }
            }
            break;
        }
        case SectionType::Theme:
        default:
            break;
    }

    // Event activity evolves with the composition. These factors affect when
    // notes occur, while each instrument keeps its fixed performance level.
    drumActivity *= clamp(1.0f + 0.12f * evolution.densityDrift - 0.04f * evolution.paletteDrift,
                             0.78f, 1.22f);
    bassActivity *= clamp(1.0f + 0.10f * evolution.bassMutation + 0.06f * evolution.densityDrift,
                             0.80f, 1.20f);
    leadActivity *= clamp(1.0f + 0.12f * evolution.leadMutation + 0.08f * evolution.paletteDrift,
                             0.78f, 1.24f);
    chordActivity *= clamp(1.0f + 0.10f * evolution.harmonyLens - 0.05f * evolution.densityDrift,
                              0.82f, 1.22f);

    // Device activity follows the same phrase plan as pitch and ownership.
    // A question leaves room for its answer; an arrival uses the existing band.
    if (phrasePlan.device == MusicalDevice::HeldQuestion && p16 >= phrasePlan.joint) {
        drumActivity *= 0.62f;
        chordActivity *= 0.72f;
        leadActivity *= 1.06f;
    } else if (phrasePlan.device == MusicalDevice::Confluence) {
        const float arrival = p16 < phrasePlan.joint ? 0.90f : 1.10f;
        drumActivity *= arrival;
        bassActivity *= arrival;
        leadActivity *= 1.10f;
    } else if (phrasePlan.device == MusicalDevice::OpenRelease) {
        drumActivity *= 0.56f;
        bassActivity *= 0.82f;
        leadActivity *= 0.78f;
        chordActivity *= 0.86f;
    } else if (phrasePlan.instrumentSlot != 0) {
        chordActivity *= 0.78f;
        drumActivity *= 0.90f;
    }

    if (afterCadence) {
        drumActivity = 0.0f;
        bassActivity = 0.0f;
        leadActivity = 0.0f;
        chordActivity = 0.0f;
    } else if (closingCadence) {
        // The cadence is one generated point of arrival, not a last-second
        // master fade. Bass, harmony, and lead meet on tonal home while rhythmic
        // detail yields enough space for the arrival to be perceived.
        drumActivity = std::min(drumActivity, 0.48f);
        bassActivity = std::max(bassActivity, 1.12f);
        leadActivity = std::max(leadActivity, 0.96f);
        chordActivity = std::max(chordActivity, 1.28f);
    }

    // Preserve structural silence at a completed ending, including zero-level
    // events historically scheduled by forced anchors. Presence is binary;
    // continuous activity curves above never reach the amplitude path.
    const float drumPresence = drumActivity > 0.0f ? 1.0f : 0.0f;
    const float bassPresence = bassActivity > 0.0f ? 1.0f : 0.0f;
    const float leadPresence = leadActivity > 0.0f ? 1.0f : 0.0f;
    const float chordPresence = chordActivity > 0.0f ? 1.0f : 0.0f;

    float kickP = mPattern.kick[pos] * (0.52f + 0.70f * p.drum) * (0.72f + 0.42f * mPattern.energy) * drumActivity * mComposition.useKick;
    const bool forceKick = ((downbeat || arrivalJoint) && !p.ambient && mComposition.useKick > 0.0f) ||
                           (closingCadence && mComposition.useKick > 0.0f);
    if (forceKick) kickP = std::max(kickP,
        (closingCadence ? 0.44f : 0.72f) * drumActivity);
    if (forceKick || mRng.chance(clamp01(kickP))) {
        const float amp = (0.44f + 0.34f * mPattern.energy) * accent * drumPresence;
        scheduleDrum(humanizeSamples(0.12f, downbeat, false), DrumType::Kick, amp, 0.0f, 0.52f + 0.22f * mPattern.roughness, clamp01(0.18f * mPattern.roughness + 0.82f * mComposition.kickTone));
        mLastKickStep = static_cast<int32_t>(mStepIndex);
        eventHappened = true;
    }

    float snareP = mPattern.snare[pos] * (0.48f + 0.62f * p.drum) * drumActivity * mComposition.useSnare;
    if (backbeat && !p.ambient && mComposition.useSnare > 0.0f) snareP = std::max(snareP, 0.68f * drumActivity);
    if (mRng.chance(clamp01(snareP))) {
        DrumType sn = (mRng.chance(0.22f + 0.20f * mPattern.roughness) && !p.breakbeat) ? DrumType::Clap : DrumType::Snare;
        const float amp = (0.24f + 0.24f * mPattern.energy) * accent * drumPresence;
        scheduleDrum(humanizeSamples(0.50f, false, backbeat), sn, amp, mRng.bipolar() * 0.08f, 0.17f + 0.10f * mRng.uni(), clamp01(0.16f * mPattern.roughness + 0.84f * mComposition.snareTone));
        if (mRng.chance(0.12f + 0.12f * mPattern.roughness) && backbeat && phrase != PhraseType::Suspension) {
            scheduleDrum(static_cast<int32_t>(0.010f * mSampleRate + mRng.uni() * 0.014f * mSampleRate), DrumType::Clap,
                         amp * 0.42f, mRng.bipolar() * 0.22f, 0.18f, mComposition.snareTone);
        }
        mLastSnareStep = static_cast<int32_t>(mStepIndex);
        eventHappened = true;
    }

    float hatP = mPattern.hat[pos] * (0.40f + 0.64f * p.density) * (1.0f - 0.35f * mPattern.space) * drumActivity * mComposition.useHat;
    if (p.trapHats && (pos & 1) == 0) hatP += 0.18f * drumActivity * mComposition.useHat;
    if (mRng.chance(clamp01(hatP))) {
        const float amp = (0.055f + 0.105f * mPattern.energy) * (0.70f + 0.50f * accent) * drumPresence;
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

    if (mRng.chance(clamp01((mPattern.openHat[pos] * (0.34f + 0.70f * p.drum) * drumActivity + (offEighth ? 0.016f : 0.0f)) * mComposition.useOpenHat))) {
        scheduleDrum(humanizeSamples(0.52f, false, false), DrumType::HatOpen,
                     (0.08f + 0.14f * mPattern.energy) * accent * drumPresence, mRng.bipolar() * 0.50f,
                     0.10f + mRng.uni() * 0.19f, mComposition.hatTone);
        eventHappened = true;
    }

    float percP = mPattern.perc[pos] * (0.25f + 0.85f * mPattern.syncopation) * (0.50f + 0.70f * p.drum) * drumActivity * mComposition.usePerc;
    if (p.breakbeat && (p16 == 1 || p16 == 7 || p16 == 14)) percP += 0.05f * drumActivity;
    if (p.ambient) percP *= 0.55f;
    if (!backbeat && mRng.chance(clamp01(percP))) {
        DrumType type = DrumType::Perc;
        const float r = mRng.uni();
        if (r < 0.16f) type = DrumType::Rim;
        else if (r < 0.30f) type = DrumType::Tom;
        scheduleDrum(humanizeSamples(0.90f, false, false), type,
                     (0.075f + 0.19f * mPattern.energy) * accent * drumPresence,
                     mRng.bipolar() * 0.72f, 0.045f + mRng.uni() * 0.16f, clamp01(mComposition.percTone + mRng.bipolar() * 0.08f));
        eventHappened = true;
    }

    const bool needsBassFloor = (section == SectionType::Suspension ||
                                 section == SectionType::Orbit || section == SectionType::Shadow ||
                                 section == SectionType::Outro);
    const bool bassFloorPulse = needsBassFloor && (p16 == 0 || p16 == 8);
    const bool anchorPulse = downbeat && !p.ambient;

    float bassGate = clamp01(generatedLayerGate(MelodyLayer::Bass, pieceStep, p16, phrase, section) *
                              mComposition.bassGain * bassActivity * mComposition.useBass);
    if ((bassFloorPulse || anchorPulse) && mComposition.useBass > 0.0f) {
        bassGate = std::max(bassGate, p.ambient ? 0.30f : 0.44f);
    }
    if (phrasePlan.instrumentSlot == 1) bassGate = 0.0f;
    if ((arrivalJoint || closingCadence) && mComposition.useBass > 0.02f) bassGate = 1.0f;
    if (afterCadence) bassGate = 0.0f;
    if (bassGate > 0.02f && (closingCadence || arrivalJoint || bassFloorPulse || anchorPulse ||
                             mRng.chance(clamp01(bassGate * (0.84f + 0.14f * accent))))) {
        const int32_t degree = closingCadence ? 0 : (arrivalJoint ? chordRoot :
            bassDegreeForStep(pieceStep, p16, chordRoot, nextChordRoot, phrase, section));
        const int32_t octave = mPattern.rootMidi >= 40 ? -1 : 0;
        const float freq = midiToHz(static_cast<float>(scaleDegreeToMidi(degree, octave)));
        int32_t gap = 1;
        while (gap < 12 && mComposition.bassGate[(p16 + gap) & 15] <= 0.02f) ++gap;
        const float dur = closingCadence
            ? stepDurationSeconds() * static_cast<float>(std::max(2, kPhraseSteps - p16))
            : stepDurationSeconds() * clamp(static_cast<float>(gap) * p.noteLength, 0.75f, 8.0f);
        scheduleBass(humanizeSamples(0.18f, downbeat, false), freq,
                     (0.22f + 0.22f * mPattern.energy) * bassPresence,
                     dur, mRng.bipolar() * 0.045f,
                     clamp01(mComposition.bassTone + 0.055f * evolution.paletteDrift +
                             mRng.bipolar() * 0.045f + p.rough * 0.04f));
        mLastBassStep = static_cast<int32_t>(mStepIndex);
        eventHappened = true;
    }

    float chordGate = clamp01(mComposition.chordGate[p16] * mComposition.chordGain *
                              chordActivity * mComposition.useChord);
    if (anchorPulse && mComposition.useChord > 0.0f) {
        chordGate = std::max(chordGate, 0.10f);
    }
    if (phrasePlan.instrumentSlot == 2) chordGate = 0.0f;
    if (p.intent == 0 && (pieceStep / kPhraseSteps) % mComposition.harmonicSpan != 0) chordGate *= 0.36f;
    if ((arrivalJoint || closingCadence) && mComposition.useChord > 0.02f) chordGate = 1.0f;
    if (afterCadence) chordGate = 0.0f;
    if (chordGate > 0.02f && (closingCadence || mRng.chance(chordGate))) {
        const float dur = closingCadence
            ? stepDurationSeconds() * static_cast<float>(std::max(2, kPhraseSteps - p16))
            : stepDurationSeconds() * (p.intent == 0 ? (3.4f + 2.0f * p.space) :
                (0.72f + 1.30f * p.space + 0.46f * mComposition.longMemory));
        const float amp = (0.030f + 0.070f * mPattern.texture + 0.055f * p.chord) *
                          accent * chordPresence;
        scheduleChordGesture(closingCadence ? 0 : humanizeSamples(0.28f, downbeat, false),
                             closingCadence ? 0 : chordRoot,
                             amp, dur, mRng.bipolar() * 0.42f,
                             clamp01(mComposition.padTone + mRng.bipolar() * 0.04f),
                             mComposition.chordArticulation, closingCadence);
        eventHappened = true;
    }

    ForegroundNote foreground = foregroundNoteForStep(pieceStep, chordRoot);
    if (closingCadence) {
        foreground.rest = false;
        foreground.degree = 0;
        foreground.instrumentSlot = 0;
        foreground.octave = mComposition.octaveBias;
        foreground.gate = 1.0f;
        foreground.duration = static_cast<float>(std::max(2, kPhraseSteps - p16));
    } else if (afterCadence) {
        foreground.rest = true;
        foreground.gate = 0.0f;
    }
    if (mPattern.intent == 0 && foreground.instrumentSlot != 1) {
        for (int32_t pass = 0; pass < 5; ++pass) {
            const int32_t midi = scaleDegreeToMidi(foreground.degree, foreground.octave);
            if (midi > 78) foreground.degree -= scaleLength();
            else if (midi < 52) foreground.degree += scaleLength();
        }
    }
    const float foregroundWeight = foreground.instrumentSlot == 1 ? mComposition.useBass :
        (foreground.instrumentSlot == 2 ? mComposition.useChord :
         (foreground.instrumentSlot >= 3 ? supportLaneWeight(foreground.instrumentSlot - 3) : mComposition.useLead));
    const float leadGate = clamp01(foreground.gate * leadActivity *
        (1.0f - 0.18f * mComposition.leadSpace) * foregroundWeight);
    if (!foreground.rest && leadGate > 0.04f) {
        // Pitch/rest development is already decided in the phrase grammar.
        // Performance varies color and timing, never randomly deletes its answer.
        const float freq = midiToHz(static_cast<float>(scaleDegreeToMidi(foreground.degree, foreground.octave)));
        const float dur = stepDurationSeconds() * foreground.duration * p.noteLength;
        const float amp = (0.085f + 0.145f * mPattern.melody + 0.025f * p.melodyRun) *
                           accent * leadPresence;
        const int32_t offset = closingCadence ? 0 : humanizeSamples(0.46f, downbeat, false);
        if (foreground.instrumentSlot == 1) {
            scheduleBass(offset, freq, amp * 1.28f, dur, 0.0f,
                         clamp01(mComposition.bassTone + 0.04f * evolution.paletteDrift), true);
        } else if (foreground.instrumentSlot == 2) {
            scheduleChordGesture(offset, foreground.degree, amp * 0.72f,
                                 std::max(dur, stepDurationSeconds() * 3.0f), 0.0f,
                                 mComposition.padTone, mComposition.chordArticulation, true);
        } else {
            const float color = foreground.instrumentSlot >= 3 ?
                clamp01(0.65f * mComposition.counterTone + 0.35f * mComposition.leadTone) : mComposition.leadTone;
            scheduleLead(offset, freq, amp, dur, mRng.bipolar() * 0.16f,
                         clamp01(color + 0.05f * evolution.paletteDrift + mRng.bipolar() * 0.025f),
                         foreground.instrumentSlot, true);
        }
        mLastLeadStep = static_cast<int32_t>(mStepIndex);
        eventHappened = true;
    }

    // Every supporting role uses a fixed instrument selected at composition
    // time. Its rhythm and register come from the tune's generated grammar,
    // replacing the former regular high sparkle/bell and reward-like cascades.
    const std::array<float, kSupportLanes> tones{{
        mComposition.arpTone, mComposition.counterTone, mComposition.stabTone,
        mComposition.droneTone, mComposition.sparkTone, mComposition.echoTone,
        mComposition.orbitTone, mComposition.bloomTone, mComposition.glyphTone,
        mComposition.subTone, mComposition.sheenTone, mComposition.pluckTone,
        mComposition.bellTone, mComposition.pulseTone, mComposition.cometTone,
        mComposition.rotorTone
    }};
    const std::array<MelodyLayer, kSupportLanes> grammars{{
        MelodyLayer::Arp, MelodyLayer::Counter, MelodyLayer::Pulse, MelodyLayer::Counter,
        MelodyLayer::Ornament, MelodyLayer::Counter, MelodyLayer::Arp, MelodyLayer::Pulse,
        MelodyLayer::Pulse, MelodyLayer::Bass, MelodyLayer::Ornament, MelodyLayer::Lead,
        MelodyLayer::Counter, MelodyLayer::Pulse, MelodyLayer::Lead, MelodyLayer::Arp
    }};
    const bool foregroundSpeaking = !foreground.rest && leadGate > 0.04f;
    int32_t supportingEvents = 0;
    const int32_t supportLimit = p.intent <= 1 ? 1 : 2;
    // Rotate evaluation priority as the phrase develops; one lane cannot always
    // consume the shared gap before another selected instrument is considered.
    const int32_t laneStart = static_cast<int32_t>((mComposition.phrasePlanSeed +
        static_cast<uint32_t>(pieceStep / kPhraseSteps)) % kSupportLanes);
    for (int32_t order = 0; order < kSupportLanes && supportingEvents < supportLimit; ++order) {
        const int32_t lane = (laneStart + order) % kSupportLanes;
        const float weight = supportLaneWeight(lane);
        if (weight <= 0.02f || foreground.instrumentSlot == lane + 3 || afterCadence) continue;
        const bool harmonic = lane == 2 || lane == 3 || lane == 7 || lane == 15;
        if ((harmonic && foreground.instrumentSlot == 2) || (lane == 9 && foreground.instrumentSlot == 1)) continue;
        const uint32_t laneHash = mixSeed(mComposition.arcSeed ^
            static_cast<uint32_t>(lane + 1) * 0x85ebca6bu);
        const uint32_t stepHash = mixSeed(laneHash ^
            (static_cast<uint32_t>(pieceStep) + 1u) * 0x9e3779b9u);
        const float room = foregroundSpeaking ? (0.12f + 0.20f * p.motion) : (0.56f + 0.24f * p.motion);
        const float choice = static_cast<float>(stepHash >> 8u) * (1.0f / 16777216.0f);
        if (choice > room) continue;
        const int32_t source = (p16 + static_cast<int32_t>((laneHash >> 3u) & 15u) +
                               phrasePlan.rhythmShift + 16) & 15;
        const MelodyLayer grammar = grammars[lane];
        const float gate = generatedLayerGate(grammar, pieceStep, source, phrase, section);
        if (gate <= 0.02f || !mRng.chance(clamp01(gate * weight * (0.38f + 0.30f * p.motion)))) continue;
        int32_t degree = generatedLayerDegree(grammar, pieceStep, source, chordRoot, phrase, section);
        float duration = stepDurationSeconds() * generatedLayerDur(grammar, source) * p.noteLength;
        const float color = clamp01(tones[lane] + 0.04f * evolution.paletteDrift);
        const float amp = (0.022f + 0.054f * mPattern.melody) * weight *
                           accent * (harmonic ? chordPresence : leadPresence);
        if (harmonic) {
            if (hasSustainedPad(0.12f)) continue;
            scheduleChordGesture(humanizeSamples(0.25f, false, false), chordRoot + (degree - chordRoot) / 3,
                                 amp, std::max(duration, stepDurationSeconds() * 1.8f),
                                 mRng.bipolar() * 0.40f, color, mComposition.chordArticulation);
        } else if (lane == 9) {
            const float freq = midiToHz(static_cast<float>(scaleDegreeToMidi(degree, -1)));
            scheduleBass(humanizeSamples(0.18f, false, false), freq, amp * 1.8f,
                         std::max(duration, stepDurationSeconds() * 1.5f), 0.0f, color);
        } else {
            const int32_t octave = std::min(2, mComposition.octaveBias);
            for (int32_t pass = 0; pass < 4; ++pass) {
                if (scaleDegreeToMidi(degree, octave) > (p.intent == 0 ? 76 : 86)) degree -= scaleLength();
                else if (scaleDegreeToMidi(degree, octave) < 48) degree += scaleLength();
            }
            const float freq = midiToHz(static_cast<float>(scaleDegreeToMidi(degree, octave)));
            duration = std::max(duration, stepDurationSeconds() * (p.intent == 0 ? 0.72f : 0.24f));
            scheduleLead(humanizeSamples(0.42f, false, false), freq, amp, duration,
                         mRng.bipolar() * 0.64f, color, lane + 3);
        }
        ++supportingEvents;
        eventHappened = true;
    }

    if (eventHappened) mSilentSteps = 0;
    if (mSilentSteps > (p.ambient ? 96 : 28)) {
        if (!afterCadence && mComposition.useKick > 0.02f) {
            scheduleDrum(0, DrumType::Kick, p.ambient ? 0.20f : 0.62f, 0.0f, 0.45f, mComposition.kickTone);
        }
        if (!afterCadence && mComposition.useBass > 0.02f) {
            scheduleBass(0, midiToHz(static_cast<float>(scaleDegreeToMidi(chordRoot, p.ambient ? 0 : -1))),
                         p.ambient ? 0.18f : 0.36f, stepDurationSeconds() * 2.0f, 0.0f, mComposition.bassTone);
        }
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
                    triggerLead(event.freq, event.amp, event.dur, event.pan, event.aux, event.instrumentSlot);
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
    if (type == DrumType::Zap || type == DrumType::Noise) return;
    for (auto& e : mEvents) {
        if (e.active) continue;
        e.active = true;
        e.samples = std::max(0, offsetSamples);
        e.kind = EventKind::Drum;
        e.instrumentSlot = -1;
        e.foreground = false;
        e.drumType = type;
        e.amp = amp;
        e.pan = clamp(pan, -1.0f, 1.0f);
        e.dur = std::max(0.005f, dur);
        e.aux = aux;
        return;
    }
}

void MusicEngine::scheduleBass(int32_t offsetSamples, float freq, float amp, float dur, float pan, float color, bool foreground) {
    for (auto& e : mEvents) {
        if (e.active) continue;
        e.active = true;
        e.samples = std::max(0, offsetSamples);
        e.kind = EventKind::Bass;
        e.instrumentSlot = 1;
        e.foreground = foreground;
        e.freq = freq;
        e.amp = amp;
        e.dur = std::max(0.025f, dur);
        e.pan = clamp(pan, -1.0f, 1.0f);
        e.aux = color;
        return;
    }
}

void MusicEngine::schedulePad(int32_t offsetSamples, int32_t degree, float amp, float dur, float pan, float color, bool foreground) {
    // A foreground answer must be audible even when an accompaniment chord is
    // still releasing. Retire that chord smoothly before the new one begins,
    // retaining the single-harmony-voice limit instead of discarding the answer.
    if (foreground) {
        for (auto& event : mEvents) {
            if (event.active && event.kind == EventKind::Pad) event.active = false;
        }
        const int32_t releaseSamples = std::max(1, static_cast<int32_t>(mSampleRate * 0.008));
        for (auto& voice : mPads) {
            if (!voice.active) continue;
            voice.forcedReleaseSamples = releaseSamples;
            voice.forcedReleaseTotal = releaseSamples;
            offsetSamples = std::max(offsetSamples, releaseSamples + 1);
        }
    } else if (hasSustainedPad(0.12f)) {
        return;
    }
    for (auto& e : mEvents) {
        if (e.active) continue;
        e.active = true;
        e.samples = std::max(0, offsetSamples);
        e.kind = EventKind::Pad;
        e.instrumentSlot = 2;
        e.foreground = foreground;
        e.degree = degree;
        e.amp = amp;
        e.dur = std::max(0.050f, dur);
        e.pan = clamp(pan, -1.0f, 1.0f);
        e.aux = color;
        return;
    }
}

void MusicEngine::scheduleLead(int32_t offsetSamples, float freq, float amp, float dur, float pan, float color, int32_t instrumentSlot, bool foreground) {
    for (auto& e : mEvents) {
        if (e.active) continue;
        e.active = true;
        e.samples = std::max(0, offsetSamples);
        e.kind = EventKind::Lead;
        e.instrumentSlot = instrumentSlot;
        e.foreground = foreground;
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
                                       int32_t articulation,
                                       bool foreground) {
    // Harmony keeps its own selected instrument. All notes of the generated
    // voicing arrive together: no universal rising three-note chime figure.
    const float articulationScale = 0.80f + 0.10f * static_cast<float>(clampInt32(articulation, 0, 4));
    schedulePad(offsetSamples, degree, std::max(0.0f, amp) * 1.12f,
                std::max(0.10f, dur * articulationScale), pan * 0.72f, color, foreground);
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
    chosen->model = mComposition.bassModel;
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
    chosen->model = mComposition.padModel;
    chosen->attackShape = mComposition.padAttack;
    chosen->releasePoint = mComposition.padRelease;
    chosen->detune = mComposition.padDetune;
    chosen->motion = mComposition.padMotion;
    chosen->width = mComposition.padWidth;
    panGains(pan, chosen->panL, chosen->panR);

    for (int32_t i = 0; i < chosen->count; ++i) {
        const int32_t octave = 1;
        const int32_t interval = mComposition.padIntervals[i];
        int32_t voicedDegree = degree + interval;
        for (int32_t pass = 0; pass < 5; ++pass) {
            const int32_t midi = scaleDegreeToMidi(voicedDegree, octave);
            if (midi > (mPattern.intent == 0 ? 76 : 85)) voicedDegree -= scaleLength();
            else if (midi < 45) voicedDegree += scaleLength();
        }
        const float hz = midiToHz(static_cast<float>(scaleDegreeToMidi(voicedDegree, octave)));
        const float detune = (mRng.bipolar() * 0.0035f) * (0.18f + chosen->detune);
        chosen->freq[i] = hz * (1.0f + detune);
        chosen->phase[i] = mRng.uni();
    }
}

void MusicEngine::triggerLead(float freq, float amp, float dur, float pan, float color, int32_t instrumentSlot) {
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
    chosen->instrumentSlot = clampInt32(instrumentSlot, 0, kSupportLanes + 2);
    chosen->model = instrumentSlot >= 3 && instrumentSlot < kSupportLanes + 3 ?
        mComposition.supportModels[instrumentSlot - 3] : mComposition.leadModel;
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

    const int32_t kit = clampInt32(v.kit, 0, kDrumKits - 1);
    const float bodyColor = clamp01(v.body);
    const float metalColor = clamp01(v.metal);
    const float noiseColor = clamp01(v.noiseMix);
    static constexpr float pitchBias[kDrumKits] = {-7.0f, 0.0f, 8.0f, -3.0f, 12.0f, 4.0f, -10.0f, 6.0f, -5.0f, 10.0f, -1.0f, 3.0f};
    static constexpr float decayBias[kDrumKits] = {0.4f, -0.5f, 1.2f, 0.0f, 1.8f, -1.0f, 0.8f, -0.2f, -1.4f, 2.2f, 0.2f, -0.7f};
    static constexpr float metalRatio[kDrumKits] = {1.37f, 1.61f, 1.91f, 2.17f, 2.43f, 2.71f, 3.07f, 3.41f, 1.23f, 2.89f, 1.79f, 3.19f};

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

    const float motion = std::sin(kTwoPi * v.age * (0.30f + 0.70f * v.motion));
    const float freq = v.freq * (1.0f + motion * (0.0004f + 0.0032f * v.motion));
    v.phase += freq / sr;
    v.phase2 += freq * 0.50f / sr;
    if (v.phase >= 1.0f) v.phase -= 1.0f;
    if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;

    const float sine = std::sin(kTwoPi * v.phase);
    const float sub = std::sin(kTwoPi * v.phase2);
    const float saw = 2.0f * v.phase - 1.0f;
    const float tri = 1.0f - 4.0f * std::fabs(v.phase - 0.5f);
    const float pulse = (v.phase < clamp(v.pulseWidth, 0.10f, 0.84f)) ? 1.0f : -1.0f;
    const float mod = std::sin(kTwoPi * v.phase * 2.0f);
    float raw = 0.0f;
    switch (clampInt32(v.model, 0, kBassModels - 1)) {
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
            raw = 0.44f * sine + 0.27f * std::sin(kTwoPi * v.phase * 2.0f + 0.5f) +
                  0.15f * std::sin(kTwoPi * v.phase * 3.0f + 1.1f) + 0.34f * sub;
            break;
        case 10:
            raw = 0.54f * tri + 0.32f * std::sin(kTwoPi * v.phase * 2.0f) +
                  0.34f * sub;
            break;
        case 11: {
            const float stepped = v.phase < 0.20f ? -0.72f :
                                  (v.phase < 0.46f ? -0.22f :
                                  (v.phase < 0.73f ? 0.34f : 0.82f));
            raw = 0.40f * stepped + 0.32f * tri + 0.40f * sub;
            break;
        }
        default: {
            const int32_t family = clampInt32(v.model - 12, 0, 7);
            const float breath = std::exp(-(1.0f + 0.34f * static_cast<float>(family)) * t);
            const float second = std::sin(kTwoPi * v.phase * 2.0f);
            const float third = std::sin(kTwoPi * v.phase * 3.0f);
            const float fourth = std::sin(kTwoPi * v.phase * 4.0f);
            const float evenWeight = (family & 1) ? 0.08f + 0.22f * color : 0.30f - 0.14f * color;
            const float oddWeight = (family & 2) ? 0.26f : 0.10f;
            const float body = 0.66f * sine + evenWeight * second * breath +
                               oddWeight * third * (0.45f + 0.55f * breath) +
                               0.08f * fourth * color * breath;
            raw = (family & 4) ? std::tanh(body * (1.1f + 0.9f * v.motion)) : body;
            raw += sub * (0.16f + 0.16f * (1.0f - v.motion));
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
    if (t >= 1.0f || v.forcedReleaseSamples == 0) {
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
    const float priorityRelease = v.forcedReleaseSamples < 0 ? 1.0f :
        static_cast<float>(v.forcedReleaseSamples) / static_cast<float>(std::max(1, v.forcedReleaseTotal));
    const float env = attack * release * priorityRelease;
    if (v.forcedReleaseSamples > 0) --v.forcedReleaseSamples;
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
        switch (clampInt32(v.model, 0, kPadModels - 1)) {
            case 0: osc = 0.82f * sine + 0.18f * tri; break; // mist
            case 1:
                osc = 0.66f * tri + 0.22f * sine +
                      0.12f * std::sin(kTwoPi * ph * 2.0f + 0.4f * i);
                break;
            case 2: osc = 0.48f * tri + 0.34f * saw + 0.18f * sine; break;
            case 3:
                osc = std::sin(kTwoPi * ph + (0.7f + 2.6f * v.motion) *
                               std::sin(kTwoPi * ph * 2.0f + 0.4f * i));
                break;
            case 4: osc = 0.46f * pulse + 0.34f * tri + 0.20f * sine; break;
            case 5:
                osc = 0.48f * sine +
                      0.30f * std::sin(kTwoPi * ph * 2.0f + 0.4f * i) +
                      0.18f * std::sin(kTwoPi * ph * 3.0f + 0.8f * i);
                break;
            case 6: {
                const float partial2 = std::sin(kTwoPi * ph * 2.0f + 0.4f * i);
                const float warped = ph + (0.04f + 0.16f * v.motion) * partial2;
                osc = 0.68f * std::sin(kTwoPi * warped) + 0.25f * tri;
                break;
            }
            case 7:
                osc = 0.62f * sine + 0.26f * std::sin(kTwoPi * ph * 2.0f) +
                      0.12f * std::sin(kTwoPi * ph * 3.0f + 0.8f * i);
                break;
            case 8:
                osc = 0.42f * tri +
                      0.26f * std::sin(kTwoPi * ph * 2.0f + 0.4f * i) +
                      0.22f * std::sin(kTwoPi * ph * 4.0f);
                break;
            case 9: {
                const float fold = std::sin(kTwoPi *
                    (ph + 0.18f * saw * (0.3f + v.motion)));
                osc = 0.56f * fold + 0.24f * sine +
                      0.16f * std::sin(kTwoPi * ph * 3.0f + 0.8f * i);
                break;
            }
            default: {
                const int32_t family = clampInt32(v.model - 10, 0, 7);
                const float second = std::sin(kTwoPi * ph * 2.0f);
                const float third = std::sin(kTwoPi * ph * 3.0f);
                const float fifth = std::sin(kTwoPi * ph * 5.0f);
                const float opening = 0.60f + 0.40f * std::sin(kPi * clamp01(t));
                const float evenWeight = (family & 1) ? 0.09f : 0.24f;
                const float oddWeight = (family & 2) ? 0.28f : 0.12f;
                osc = 0.70f * sine + evenWeight * second * opening +
                      oddWeight * third * (0.65f + 0.35f * color) +
                      0.06f * fifth * color;
                if (family & 4) osc = std::sin(kTwoPi * ph +
                    (0.12f + 0.30f * v.motion) * second * opening) * 0.78f + 0.20f * osc;
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
    const int32_t modelIndex = clampInt32(v.model, 0, kLeadModels - 1);

    float raw = 0.0f;
    switch (modelIndex) {
        case 0: raw = 0.56f * pulse + 0.32f * saw + 0.14f * sine; break;
        case 1: raw = std::sin(kTwoPi * v.phase +
                              (0.8f + 4.4f * v.modRatio) * mod); break;
        case 2:
            raw = 0.70f * sine + 0.25f * tri +
                  0.12f * std::sin(kTwoPi * v.phase * 2.0f);
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
            raw = 0.66f * std::sin(kTwoPi * v.phase + 1.8f * mod) + 0.22f * sine;
            break;
        case 6: {
            const float vowel = sine + 0.44f * std::sin(kTwoPi * v.phase * 2.0f + 0.7f) +
                                0.24f * std::sin(kTwoPi * v.phase * 3.0f + 1.4f);
            raw = std::tanh(vowel * 0.82f);
            break;
        }
        case 7: {
            const float glass = sine +
                                0.38f * std::sin(kTwoPi * v.phase * 2.0f + 0.30f) +
                                0.18f * std::sin(kTwoPi * v.phase * 4.0f + 1.10f);
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
                (2.0f + static_cast<float>(static_cast<int32_t>(v.modRatio * 3.0f))), 1.0f);
            raw = 0.48f * (2.0f * wrapped - 1.0f) + 0.38f * sine;
            break;
        }
        case 11: {
            const float p2 = std::fmod(v.phase * 2.0f, 1.0f) < 0.42f ? 1.0f : -1.0f;
            raw = 0.42f * pulse + 0.34f * p2 + 0.22f * tri;
            break;
        }
        case 12:
            raw = 0.56f * sine +
                  0.26f * std::sin(kTwoPi * v.phase * 3.0f + 0.4f) +
                  0.14f * std::sin(kTwoPi * v.phase * 5.0f + 1.1f);
            break;
        case 13: {
            const float chirp = std::sin(kTwoPi * v.phase +
                                         (0.4f + 2.4f * env) * mod);
            raw = 0.64f * chirp + 0.22f * tri;
            break;
        }
        case 14:
            raw = 0.42f * sine + 0.28f * std::sin(kTwoPi * v.phase * 2.0f) +
                  0.18f * std::sin(kTwoPi * v.phase * 4.0f) + 0.14f * pulse;
            break;
        case 15: {
            const float stack = sine +
                0.31f * std::sin(kTwoPi * v.phase * 2.0f + mod * 0.20f) +
                0.16f * std::sin(kTwoPi * v.phase * 4.0f + 0.8f);
            raw = std::tanh(stack * 0.78f);
            break;
        }
        default: {
            // Sixteen additional original harmonic designs: reed bodies,
            // rounded struck strings, open voiced brass, and sustained filaments.
            // Color continuously changes a family; it never changes its index.
            const int32_t design = clampInt32(modelIndex - 16, 0, 15);
            const int32_t family = design / 4;
            const float variant = static_cast<float>(design % 4) / 3.0f;
            const float second = std::sin(kTwoPi * v.phase * 2.0f);
            const float third = std::sin(kTwoPi * v.phase * 3.0f);
            const float fourth = std::sin(kTwoPi * v.phase * 4.0f);
            const float fifth = std::sin(kTwoPi * v.phase * 5.0f);
            const float bloom = std::exp(-(1.0f + 2.8f * variant) * t);
            if (family == 0) {
                raw = 0.70f * sine + (0.12f + 0.18f * variant) * third +
                      (0.04f + 0.10f * color) * fifth * (0.55f + 0.45f * bloom) +
                      0.08f * second * (1.0f - variant);
            } else if (family == 1) {
                raw = 0.74f * sine + (0.10f + 0.26f * color) * second * bloom +
                      (0.06f + 0.18f * variant) * third * bloom * bloom +
                      0.08f * fourth * color * bloom;
            } else if (family == 2) {
                const float body = 0.72f * sine + (0.16f + 0.18f * color) * second +
                                   (0.06f + 0.18f * variant) * third;
                raw = std::tanh(body * (1.05f + 1.20f * color)) * 0.82f + 0.08f * fourth * bloom;
            } else {
                const float bow = 0.60f + 0.40f * std::sin(kPi * clamp01(t));
                raw = 0.70f * sine + (0.08f + 0.12f * variant) * second * bow +
                      (0.16f - 0.10f * variant) * third +
                      0.08f * fifth * color * bow;
            }
            break;
        }
    }

    const float fundamentalWeight = mPattern.intent <= 1 ? 0.36f : 0.22f;
    raw = raw * (1.0f - fundamentalWeight) + sine * fundamentalWeight;
    v.lp += (raw - v.lp) * clamp(v.cutoff *
        (0.38f + 1.42f * env + 0.38f * color), 0.006f, 0.52f);
    v.age += dt;
    return v.lp * env * v.amp;
}

void MusicEngine::applyMaster(float& left, float& right) {
    // This drive is fixed when the song is generated (or explicitly imported).
    // Preserve that v30 tone and its fixed DC filter without riding mix levels.
    const float drive = 1.05f + 0.82f * mPattern.drive;
    left = std::tanh(left * drive) / std::tanh(drive);
    right = std::tanh(right * drive) / std::tanh(drive);

    const float hpL = left - mDcInL + 0.995f * mDcOutL;
    const float hpR = right - mDcInR + 0.995f * mDcOutR;
    mDcInL = left;
    mDcInR = right;
    mDcOutL = hpL;
    mDcOutR = hpR;

    left = hpL;
    right = hpR;
    OutputStage::process(left, right);
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
    return (mPattern.scaleMode == 4 || mPattern.scaleMode == 8) ? 5 : 7;
}

int32_t MusicEngine::scaleDegreeToMidi(int32_t degree, int32_t octaveOffset) const {
    static constexpr int32_t naturalMinor[7] = {0, 2, 3, 5, 7, 8, 10};
    static constexpr int32_t dorian[7] = {0, 2, 3, 5, 7, 9, 10};
    static constexpr int32_t phrygian[7] = {0, 1, 3, 5, 7, 8, 10};
    static constexpr int32_t harmonicMinor[7] = {0, 2, 3, 5, 7, 8, 11};
    static constexpr int32_t minorPent[5] = {0, 3, 5, 7, 10};
    static constexpr int32_t major[7] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr int32_t lydian[7] = {0, 2, 4, 6, 7, 9, 11};
    static constexpr int32_t mixolydian[7] = {0, 2, 4, 5, 7, 9, 10};
    static constexpr int32_t majorPent[5] = {0, 2, 4, 7, 9};

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
        case 5: semitone = major[idx]; break;
        case 6: semitone = lydian[idx]; break;
        case 7: semitone = mixolydian[idx]; break;
        case 8: semitone = majorPent[idx]; break;
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
