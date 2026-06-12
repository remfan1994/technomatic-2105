#include "MusicEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
    reset(0x52423934u);
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
    mComposition = Composition{};

    const StyleType initial = randomStyle();
    generatePattern(initial);
    for (auto& slot : mMemory) slot = mPattern;
    mMemoryWrite = 0;

    if (!mDelayL.empty()) std::fill(mDelayL.begin(), mDelayL.end(), 0.0f);
    if (!mDelayR.empty()) std::fill(mDelayR.begin(), mDelayR.end(), 0.0f);
}

void MusicEngine::next() {
    if (mTransitionStage != TransitionStage::None) {
        mPendingStyle = randomDifferentStyle(mPattern.style);
        return;
    }
    storeMemory();
    mPendingStyle = randomDifferentStyle(mPattern.style);
    const float fadeSeconds = 0.12f + 0.20f * mRng.uni();
    mTransitionSamplesTotal = std::max(1, static_cast<int32_t>(fadeSeconds * static_cast<float>(mSampleRate)));
    mTransitionSamplesLeft = mTransitionSamplesTotal;
    mDeadAirSamples = static_cast<int32_t>((0.08f + 0.28f * mRng.uni()) * static_cast<float>(mSampleRate));
    mTransitionStage = TransitionStage::FadeOut;
}

void MusicEngine::setPieceLengthSeconds(int32_t seconds) {
    if (seconds < 8) seconds = 8;
    if (seconds > 999999) seconds = 999999;
    mRequestedPieceSeconds = seconds;
    const int32_t steps = pieceStepsFromSeconds(mRequestedPieceSeconds, std::max(40.0f, mBpmTarget));
    if (steps > 0) {
        mComposition.pieceSteps = steps;
        mStyleTargetSteps = steps;
    }
}

int32_t MusicEngine::pieceStepsFromSeconds(int32_t seconds, float bpm) const {
    const double safeBpm = std::max(40.0, std::min(220.0, static_cast<double>(bpm)));
    const double rawSteps = static_cast<double>(std::max(8, seconds)) * safeBpm * 4.0 / 60.0;
    int64_t phrases = static_cast<int64_t>(std::llround(rawSteps / static_cast<double>(kPhraseSteps)));
    phrases = std::max<int64_t>(2, phrases);
    phrases = std::min<int64_t>(phrases, 60000000LL / kPhraseSteps);
    return static_cast<int32_t>(phrases * kPhraseSteps);
}

MusicEngine::StyleProfile MusicEngine::profile(StyleType style) const {
    StyleProfile p;
    p.type = style;

    switch (style) {
        case StyleType::BoomBap:
            p.bpmMin = 78.0f; p.bpmMax = 98.0f;
            p.swingMin = 0.08f; p.swingMax = 0.18f;
            p.density = 0.52f; p.drum = 0.74f; p.bass = 0.66f; p.melody = 0.34f; p.chord = 0.24f;
            p.texture = 0.14f; p.rough = 0.50f; p.space = 0.36f; p.sync = 0.62f;
            p.hatRoll = 0.06f; p.melodyRun = 0.22f; p.transitionSilence = 0.35f;
            break;
        case StyleType::TrapNoir:
            p.bpmMin = 128.0f; p.bpmMax = 154.0f;
            p.swingMin = 0.01f; p.swingMax = 0.08f;
            p.density = 0.58f; p.drum = 0.68f; p.bass = 0.84f; p.melody = 0.40f; p.chord = 0.16f;
            p.texture = 0.22f; p.rough = 0.46f; p.space = 0.44f; p.sync = 0.52f;
            p.hatRoll = 0.60f; p.melodyRun = 0.18f; p.transitionSilence = 0.28f;
            p.halfTime = true; p.trapHats = true;
            break;
        case StyleType::BreakRush:
            p.bpmMin = 158.0f; p.bpmMax = 174.0f;
            p.swingMin = 0.00f; p.swingMax = 0.06f;
            p.density = 0.82f; p.drum = 0.88f; p.bass = 0.64f; p.melody = 0.28f; p.chord = 0.12f;
            p.texture = 0.18f; p.rough = 0.58f; p.space = 0.18f; p.sync = 0.74f;
            p.hatRoll = 0.28f; p.melodyRun = 0.12f; p.transitionSilence = 0.18f;
            p.breakbeat = true;
            break;
        case StyleType::ElectroFunk:
            p.bpmMin = 106.0f; p.bpmMax = 128.0f;
            p.swingMin = 0.03f; p.swingMax = 0.12f;
            p.density = 0.66f; p.drum = 0.76f; p.bass = 0.76f; p.melody = 0.56f; p.chord = 0.28f;
            p.texture = 0.18f; p.rough = 0.42f; p.space = 0.28f; p.sync = 0.70f;
            p.hatRoll = 0.18f; p.melodyRun = 0.48f; p.transitionSilence = 0.24f;
            break;
        case StyleType::MicroHouse:
            p.bpmMin = 116.0f; p.bpmMax = 128.0f;
            p.swingMin = 0.00f; p.swingMax = 0.04f;
            p.density = 0.62f; p.drum = 0.70f; p.bass = 0.50f; p.melody = 0.26f; p.chord = 0.34f;
            p.texture = 0.22f; p.rough = 0.24f; p.space = 0.50f; p.sync = 0.42f;
            p.hatRoll = 0.08f; p.melodyRun = 0.12f; p.transitionSilence = 0.42f;
            p.fourOnFloor = true;
            break;
        case StyleType::Synthwave:
            p.bpmMin = 88.0f; p.bpmMax = 112.0f;
            p.swingMin = 0.00f; p.swingMax = 0.04f;
            p.density = 0.58f; p.drum = 0.58f; p.bass = 0.70f; p.melody = 0.72f; p.chord = 0.56f;
            p.texture = 0.28f; p.rough = 0.18f; p.space = 0.35f; p.sync = 0.38f;
            p.hatRoll = 0.04f; p.melodyRun = 0.70f; p.transitionSilence = 0.30f;
            break;
        case StyleType::GlitchHop:
            p.bpmMin = 92.0f; p.bpmMax = 116.0f;
            p.swingMin = 0.02f; p.swingMax = 0.11f;
            p.density = 0.70f; p.drum = 0.82f; p.bass = 0.66f; p.melody = 0.42f; p.chord = 0.20f;
            p.texture = 0.26f; p.rough = 0.76f; p.space = 0.26f; p.sync = 0.82f;
            p.hatRoll = 0.24f; p.melodyRun = 0.30f; p.transitionSilence = 0.16f;
            break;
        case StyleType::VaporDrift:
            p.bpmMin = 62.0f; p.bpmMax = 86.0f;
            p.swingMin = 0.03f; p.swingMax = 0.12f;
            p.density = 0.28f; p.drum = 0.22f; p.bass = 0.38f; p.melody = 0.60f; p.chord = 0.76f;
            p.texture = 0.70f; p.rough = 0.16f; p.space = 0.76f; p.sync = 0.32f;
            p.hatRoll = 0.02f; p.melodyRun = 0.50f; p.transitionSilence = 0.70f;
            p.ambient = false;
            break;
        case StyleType::PulseDub:
        default:
            p.bpmMin = 126.0f; p.bpmMax = 142.0f;
            p.swingMin = 0.00f; p.swingMax = 0.08f;
            p.density = 0.54f; p.drum = 0.62f; p.bass = 0.82f; p.melody = 0.28f; p.chord = 0.34f;
            p.texture = 0.38f; p.rough = 0.36f; p.space = 0.58f; p.sync = 0.50f;
            p.hatRoll = 0.10f; p.melodyRun = 0.16f; p.transitionSilence = 0.55f;
            p.halfTime = true;
            break;
    }

    return p;
}

MusicEngine::StyleType MusicEngine::randomStyle() {
    return static_cast<StyleType>(mRng.rangeInt(0, static_cast<int32_t>(StyleType::Count) - 1));
}

MusicEngine::StyleType MusicEngine::randomDifferentStyle(StyleType current) {
    StyleType s = current;
    for (int32_t i = 0; i < 8 && s == current; ++i) {
        s = randomStyle();
    }
    if (s == current) {
        const int32_t n = static_cast<int32_t>(StyleType::Count);
        s = static_cast<StyleType>((static_cast<int32_t>(current) + 1 + mRng.rangeInt(0, n - 2)) % n);
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

    const int32_t roots[] = {31, 33, 34, 36, 38, 39, 41, 43, 46};
    mPattern.rootMidi = roots[mRng.rangeInt(0, 8)];
    if (style == StyleType::TrapNoir || style == StyleType::PulseDub) mPattern.rootMidi -= 2;
    if (style == StyleType::Synthwave) mPattern.rootMidi += 2;

    if (style == StyleType::VaporDrift) mPattern.scaleMode = mRng.chance(0.55f) ? 1 : 4;
    else if (style == StyleType::TrapNoir) mPattern.scaleMode = mRng.chance(0.55f) ? 2 : 0;
    else if (style == StyleType::Synthwave) mPattern.scaleMode = mRng.chance(0.50f) ? 0 : 3;
    else mPattern.scaleMode = mRng.rangeInt(0, 4);

    mPattern.swing = p.swingMin + mRng.uni() * (p.swingMax - p.swingMin);
    mPattern.humanize = clamp(0.12f + mRng.uni() * 0.62f + (p.swingMax * 1.1f), 0.06f, 0.95f);
    mPattern.energy = clamp(0.32f + p.density * 0.44f + mRng.bipolar() * 0.16f, 0.12f, 0.96f);
    mPattern.density = clamp(p.density + mRng.bipolar() * 0.12f, 0.10f, 0.96f);
    mPattern.syncopation = clamp(p.sync + mRng.bipolar() * 0.13f, 0.05f, 0.96f);
    mPattern.texture = clamp(p.texture + mRng.bipolar() * 0.11f, 0.00f, 0.95f);
    mPattern.roughness = clamp(p.rough + mRng.bipolar() * 0.12f, 0.00f, 0.98f);
    mPattern.space = clamp(p.space + mRng.bipolar() * 0.13f, 0.02f, 0.92f);
    mPattern.melody = clamp(p.melody + mRng.bipolar() * 0.14f, 0.03f, 0.95f);
    mPattern.delay = clamp(0.06f + p.space * 0.22f + p.texture * 0.12f + mRng.bipolar() * 0.06f, 0.02f, 0.42f);
    mPattern.drive = clamp(0.36f + p.rough * 0.48f + mRng.bipolar() * 0.08f, 0.18f, 0.92f);

    mBpmTarget = p.bpmMin + mRng.uni() * (p.bpmMax - p.bpmMin);
    if (mBpm <= 10.0f) mBpm = mBpmTarget;
    else mBpm = 0.45f * mBpm + 0.55f * mBpmTarget;

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
            case StyleType::BoomBap:
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

            case StyleType::TrapNoir:
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

            case StyleType::BreakRush:
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

            case StyleType::ElectroFunk:
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

            case StyleType::MicroHouse:
                if (p16 == 0 || p16 == 4 || p16 == 8 || p16 == 12) kick = 0.86f;
                if (p16 == 4 || p16 == 12) snare = 0.30f;
                hat = offEighth ? 0.72f : (eighth ? 0.16f : 0.07f);
                openHat = offEighth ? 0.18f : 0.002f;
                perc = (p16 == 3 || p16 == 7 || p16 == 11 || p16 == 15) ? 0.10f : 0.018f;
                bass = (p16 == 0 || p16 == 7 || p16 == 10 || p16 == 15) ? 0.24f : 0.034f;
                chord = (p16 == 2 || p16 == 10) ? 0.040f : 0.006f;
                lead = (p16 == 5 || p16 == 13) ? 0.026f : 0.003f;
                break;

            case StyleType::Synthwave:
                if (p16 == 0 || p16 == 8) kick = 0.82f;
                if (back) snare = 0.86f;
                hat = eighth ? 0.32f : 0.08f;
                openHat = offEighth ? 0.06f : 0.003f;
                bass = (p16 == 0 || p16 == 2 || p16 == 4 || p16 == 6 || p16 == 8 || p16 == 10 || p16 == 12 || p16 == 14) ? 0.34f : 0.018f;
                chord = (p16 == 0 || p16 == 8) ? 0.080f : ((p16 == 4 || p16 == 12) ? 0.028f : 0.004f);
                lead = (p16 == 1 || p16 == 3 || p16 == 6 || p16 == 9 || p16 == 11 || p16 == 14) ? 0.115f : 0.010f;
                perc = (p16 == 15) ? 0.070f : 0.010f;
                break;

            case StyleType::GlitchHop:
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

            case StyleType::VaporDrift:
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

            case StyleType::PulseDub:
            default:
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
        }

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
}

void MusicEngine::fillMotifFromTemplate(std::array<int32_t, kPhraseSteps>& motif,
                                        std::array<float, kPhraseSteps>& gate,
                                        std::array<float, kPhraseSteps>& dur,
                                        int32_t templateId,
                                        int32_t contourOffset) {
    motif.fill(0);
    gate.fill(0.0f);
    dur.fill(0.70f);

    static constexpr int32_t degrees[8][16] = {
        {0, 0, 2, 0, 3, 0, 4, 0, 5, 0, 4, 0, 2, 0, 0, 0},
        {0, 0, 0, 2, 3, 0, 5, 0, 4, 0, 3, 0, 2, 0, 0, 0},
        {0, 0, 2, 3, 0, 0, 5, 4, 0, 3, 0, 2, 0, 0, -1, 0},
        {2, 0, 3, 0, 5, 0, 4, 0, 3, 0, 2, 0, 0, 0, 0, 0},
        {0, 2, 0, 4, 0, 5, 4, 0, 3, 0, 2, 0, 0, -1, 0, 0},
        {0, 0, -1, 0, 0, 2, 0, 3, 4, 0, 3, 0, 2, 0, 0, 0},
        {0, 2, 3, 0, 5, 0, 7, 0, 5, 0, 4, 3, 2, 0, 0, 0},
        {0, 0, 4, 0, 3, 0, 2, 0, 0, 0, -1, 0, -2, 0, 0, 0}
    };
    static constexpr float gates[8][16] = {
        {0.98f,0,0.70f,0,0.86f,0,0.72f,0,0.92f,0,0.64f,0,0.70f,0,0.82f,0},
        {0.92f,0,0,0.82f,0.78f,0,0.88f,0,0.62f,0,0.72f,0,0.66f,0,0.92f,0},
        {0.96f,0,0.64f,0.72f,0,0,0.86f,0.70f,0,0.58f,0,0.70f,0,0,0.55f,0.88f},
        {0.88f,0,0.68f,0,0.92f,0,0.70f,0,0.74f,0,0.62f,0,0.90f,0,0,0},
        {0.92f,0.54f,0,0.74f,0,0.88f,0.62f,0,0.74f,0,0.60f,0,0.82f,0.46f,0,0},
        {0.90f,0,0.54f,0,0.86f,0.58f,0,0.74f,0.60f,0,0.64f,0,0.56f,0,0.92f,0},
        {0.84f,0.48f,0.64f,0,0.86f,0,0.92f,0,0.66f,0,0.62f,0.54f,0.70f,0,0.90f,0},
        {0.88f,0,0.72f,0,0.64f,0,0.72f,0,0.88f,0,0.52f,0,0.62f,0,0.88f,0}
    };

    const int32_t t = std::max(0, std::min(7, templateId));
    for (int32_t i = 0; i < kPhraseSteps; ++i) {
        motif[i] = degrees[t][i] + contourOffset;
        gate[i] = gates[t][i];
        dur[i] = gate[i] > 0.0f ? (0.70f + 0.25f * ((i & 3) == 0 ? 1.0f : 0.0f)) : 0.0f;
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
    }
}

void MusicEngine::generateComposition(const StyleProfile& p) {
    ++mComposition.generation;
    mComposition.chordRoot.fill(0);
    mComposition.form.fill(PhraseType::Statement);

    mComposition.pieceSteps = pieceStepsFromSeconds(mRequestedPieceSeconds, mBpmTarget);
    mStyleTargetSteps = mComposition.pieceSteps;
    mComposition.arcSeed = mRng.nextU32();
    {
        const int32_t totalPhrases = std::max(2, mComposition.pieceSteps / kPhraseSteps);
        const int32_t baseSection = totalPhrases > 160 ? 10 : (totalPhrases > 64 ? 8 : 6);
        mComposition.sectionPhraseLength = std::max(4, std::min(18, baseSection + mRng.rangeInt(-2, 4)));
        mComposition.hookCycle = mRng.rangeInt(3, 6);
    }

    mComposition.progressionLength = mRng.chance(0.25f) ? 8 : 4;
    static constexpr int32_t prog[10][8] = {
        {0, 5, 6, 0, 0, 5, 6, 4},
        {0, 3, 6, 0, 0, 3, 5, 4},
        {0, 0, 5, 6, 0, 0, 3, 4},
        {0, 6, 5, 6, 0, 6, 3, 4},
        {0, 4, 5, 3, 0, 4, 6, 5},
        {0, 2, 3, 5, 0, 2, 5, 4},
        {0, 1, 0, 6, 0, 1, 3, 0},
        {0, 0, -1, 0, 0, 3, 2, 0},
        {0, 4, 0, 6, 0, 4, 3, 2},
        {0, 5, 3, 4, 0, 6, 5, 4}
    };
    int32_t pi = mRng.rangeInt(0, 9);
    if (mPattern.style == StyleType::TrapNoir || mPattern.style == StyleType::PulseDub) pi = mRng.chance(0.50f) ? 6 : 7;
    if (mPattern.style == StyleType::Synthwave) pi = mRng.chance(0.50f) ? 0 : 4;
    if (mPattern.style == StyleType::MicroHouse) pi = mRng.chance(0.50f) ? 2 : 8;
    for (int32_t i = 0; i < kMaxProgressionSlots; ++i) mComposition.chordRoot[i] = prog[pi][i];
    if (mComposition.progressionLength == 4) {
        for (int32_t i = 4; i < kMaxProgressionSlots; ++i) mComposition.chordRoot[i] = mComposition.chordRoot[i & 3];
    }

    static constexpr PhraseType formA[8] = {
        PhraseType::Statement, PhraseType::Repeat, PhraseType::Answer, PhraseType::Variation,
        PhraseType::Statement, PhraseType::Breakdown, PhraseType::Answer, PhraseType::Climax
    };
    static constexpr PhraseType formB[8] = {
        PhraseType::Statement, PhraseType::Answer, PhraseType::Statement, PhraseType::Variation,
        PhraseType::Breakdown, PhraseType::Statement, PhraseType::Answer, PhraseType::Variation
    };
    static constexpr PhraseType formC[12] = {
        PhraseType::Statement, PhraseType::Repeat, PhraseType::Answer, PhraseType::Variation,
        PhraseType::Statement, PhraseType::Answer, PhraseType::Breakdown, PhraseType::Statement,
        PhraseType::Answer, PhraseType::Variation, PhraseType::Climax, PhraseType::Statement
    };
    if (mRng.chance(0.35f)) {
        mComposition.formLength = 12;
        for (int32_t i = 0; i < 12; ++i) mComposition.form[i] = formC[i];
    } else if (mRng.chance(0.50f)) {
        mComposition.formLength = 8;
        for (int32_t i = 0; i < 8; ++i) mComposition.form[i] = formA[i];
    } else {
        mComposition.formLength = 8;
        for (int32_t i = 0; i < 8; ++i) mComposition.form[i] = formB[i];
    }

    const int32_t motifTemplate = mRng.rangeInt(0, 7);
    mComposition.hookOffset = (mRng.chance(0.50f) ? 0 : (mRng.chance(0.50f) ? 2 : -1));
    mComposition.answerOffset = (mRng.chance(0.50f) ? 0 : (mRng.chance(0.50f) ? 1 : -1));
    mComposition.octaveBias = (mPattern.style == StyleType::TrapNoir || mPattern.style == StyleType::PulseDub) ? 2 : (mPattern.style == StyleType::Synthwave ? 3 : 2);
    mComposition.motifGain = clamp(0.90f + p.melody * 0.80f + mRng.bipolar() * 0.10f, 0.62f, 1.65f);
    mComposition.bassGain = clamp(0.86f + p.bass * 0.70f + mRng.bipolar() * 0.08f, 0.62f, 1.50f);
    mComposition.chordGain = clamp(0.55f + p.chord * 0.70f + p.texture * 0.35f, 0.35f, 1.30f);
    mComposition.ornament = clamp(0.08f + p.melodyRun * 0.45f + p.sync * 0.18f, 0.05f, 0.55f);
    mComposition.leadSpace = clamp(p.space * 0.55f + (p.ambient ? 0.25f : 0.02f), 0.04f, 0.75f);

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
    }
    return h ? h : 1u;
}

bool MusicEngine::isHashRecent(uint32_t hash) const {
    for (uint32_t h : mRecentHash) {
        if (h == hash && h != 0u) return true;
    }
    return false;
}

int32_t MusicEngine::currentChordRoot(int32_t step) const {
    const int32_t progression = std::max(1, std::min(kMaxProgressionSlots, mComposition.progressionLength));
    int32_t bar = step / kPhraseSteps;
    while (bar < 0) bar += progression;
    return mComposition.chordRoot[bar % progression];
}

MusicEngine::SectionType MusicEngine::currentSectionType(int32_t step) const {
    const int32_t phrase = std::max(0, step) / kPhraseSteps;
    const int32_t totalPhrases = std::max(2, mComposition.pieceSteps / kPhraseSteps);
    const int32_t edgePhrases = std::max(2, std::min(8, totalPhrases / 16));

    if (phrase < edgePhrases) return SectionType::Intro;
    if (phrase >= totalPhrases - edgePhrases) return SectionType::Outro;

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

    if (r < 10) return SectionType::Breakdown;
    if (r > 88) return SectionType::Climax;
    if (r < 40) return SectionType::Variation;
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
            return local < 2 ? PhraseType::Breakdown : PhraseType::Statement;
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
        case SectionType::Breakdown:
            return local == 2 ? PhraseType::Answer : PhraseType::Breakdown;
        case SectionType::Climax:
            if (local == 0) return PhraseType::Hook;
            if (local == 1) return PhraseType::Climax;
            if (local == 2) return PhraseType::Answer;
            return PhraseType::Climax;
        case SectionType::Outro:
            return local == 0 ? PhraseType::Statement : PhraseType::Breakdown;
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
        case PhraseType::Breakdown:
            motif = &mComposition.motifA; gates = &mComposition.gateA; durs = &mComposition.durA;
            gateScale = 0.58f;
            trans = -1;
            break;
        case PhraseType::Hook:
            motif = &mComposition.motifA; gates = &mComposition.gateA; durs = &mComposition.durA;
            gateScale = 1.28f;
            trans = 0;
            break;
        case PhraseType::Climax:
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
    generatePattern(mPendingStyle);
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

    if (pieceStep >= mComposition.pieceSteps && (p16 == 15 || p16 == 0 || mRng.chance(0.040f + 0.040f * mNovelty))) {
        beginTransition();
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
        case PhraseType::Breakdown:
            phraseDrumScale = p.ambient ? 0.82f : 0.72f;
            phraseBassScale = 0.74f;
            phraseLeadScale = 0.76f;
            phraseChordScale = 1.16f;
            break;
        case PhraseType::Hook:
            phraseDrumScale = 1.05f;
            phraseBassScale = 1.05f;
            phraseLeadScale = 1.28f;
            phraseChordScale = 1.00f;
            break;
        case PhraseType::Climax:
            phraseDrumScale = 1.12f;
            phraseBassScale = 1.10f;
            phraseLeadScale = 1.22f;
            phraseChordScale = 0.92f;
            break;
        case PhraseType::Answer:
            phraseLeadScale = 0.98f;
            phraseBassScale = 0.92f;
            break;
        case PhraseType::Variation:
            phraseLeadScale = 1.08f;
            phraseBassScale = 1.04f;
            break;
        default:
            break;
    }

    switch (section) {
        case SectionType::Intro: {
            const float ramp = clamp01(static_cast<float>(pieceStep) / static_cast<float>(std::max(1, kPhraseSteps * 4)));
            phraseDrumScale *= 0.72f + 0.28f * ramp;
            phraseBassScale *= 0.78f + 0.22f * ramp;
            phraseLeadScale *= 0.82f + 0.18f * ramp;
            phraseChordScale *= 1.08f;
            break;
        }
        case SectionType::Hook:
            phraseDrumScale *= 1.03f;
            phraseBassScale *= 1.04f;
            phraseLeadScale *= 1.16f;
            break;
        case SectionType::Variation:
            phraseDrumScale *= 0.96f;
            phraseBassScale *= 1.00f;
            phraseLeadScale *= 1.06f;
            phraseChordScale *= 1.03f;
            break;
        case SectionType::Breakdown:
            phraseDrumScale *= 0.82f;
            phraseBassScale *= 0.86f;
            phraseLeadScale *= 0.94f;
            phraseChordScale *= 1.16f;
            break;
        case SectionType::Climax:
            phraseDrumScale *= 1.10f;
            phraseBassScale *= 1.08f;
            phraseLeadScale *= 1.18f;
            break;
        case SectionType::Outro: {
            const int32_t remaining = std::max(0, mComposition.pieceSteps - pieceStep);
            const float ramp = clamp01(static_cast<float>(remaining) / static_cast<float>(std::max(1, kPhraseSteps * 4)));
            phraseDrumScale *= 0.74f + 0.26f * ramp;
            phraseBassScale *= 0.78f + 0.22f * ramp;
            phraseLeadScale *= 0.80f + 0.20f * ramp;
            phraseChordScale *= 1.06f;
            break;
        }
        case SectionType::Theme:
        default:
            break;
    }

    float kickP = mPattern.kick[pos] * (0.52f + 0.70f * p.drum) * (0.72f + 0.42f * mPattern.energy) * phraseDrumScale;
    const bool forceKick = downbeat && !p.ambient;
    if (forceKick) kickP = std::max(kickP, 0.72f * phraseDrumScale);
    if (forceKick || mRng.chance(clamp01(kickP))) {
        const float amp = (0.44f + 0.34f * mPattern.energy) * accent * phraseDrumScale;
        scheduleDrum(humanizeSamples(0.12f, downbeat, false), DrumType::Kick, amp, 0.0f, 0.52f + 0.22f * mPattern.roughness, mPattern.roughness);
        mLastKickStep = static_cast<int32_t>(mStepIndex);
        eventHappened = true;
    }

    float snareP = mPattern.snare[pos] * (0.48f + 0.62f * p.drum) * phraseDrumScale;
    if (backbeat && !p.ambient) snareP = std::max(snareP, 0.68f * phraseDrumScale);
    if (mRng.chance(clamp01(snareP))) {
        DrumType sn = (mRng.chance(0.22f + 0.20f * mPattern.roughness) && !p.breakbeat) ? DrumType::Clap : DrumType::Snare;
        const float amp = (0.30f + 0.30f * mPattern.energy) * accent * phraseDrumScale;
        scheduleDrum(humanizeSamples(0.50f, false, backbeat), sn, amp, mRng.bipolar() * 0.08f, 0.17f + 0.10f * mRng.uni(), mPattern.roughness);
        if (mRng.chance(0.12f + 0.12f * mPattern.roughness) && backbeat && phrase != PhraseType::Breakdown) {
            scheduleDrum(static_cast<int32_t>(0.010f * mSampleRate + mRng.uni() * 0.014f * mSampleRate), DrumType::Clap,
                         amp * 0.42f, mRng.bipolar() * 0.22f, 0.18f, mRng.uni());
        }
        mLastSnareStep = static_cast<int32_t>(mStepIndex);
        eventHappened = true;
    }

    float hatP = mPattern.hat[pos] * (0.40f + 0.64f * p.density) * (1.0f - 0.35f * mPattern.space) * phraseDrumScale;
    if (p.trapHats && (pos & 1) == 0) hatP += 0.18f * phraseDrumScale;
    if (mRng.chance(clamp01(hatP))) {
        const float amp = (0.10f + 0.16f * mPattern.energy) * (0.70f + 0.50f * accent) * phraseDrumScale;
        scheduleDrum(humanizeSamples(0.85f, false, false), DrumType::HatClosed,
                     amp, mRng.bipolar() * 0.55f, 0.020f + mRng.uni() * 0.060f, mPattern.roughness);
        eventHappened = true;

        if (p.trapHats && !downbeat && phrase != PhraseType::Breakdown && mRng.chance(p.hatRoll * (0.12f + 0.65f * mPattern.density))) {
            const int32_t rolls = mRng.chance(0.62f) ? 2 : (mRng.chance(0.50f) ? 3 : 4);
            for (int32_t r = 1; r < rolls; ++r) {
                const float frac = static_cast<float>(r) / static_cast<float>(rolls);
                scheduleDrum(static_cast<int32_t>(stepSamples * frac), DrumType::HatClosed,
                             amp * (0.48f + 0.11f * r), mRng.bipolar() * 0.64f,
                             0.016f + mRng.uni() * 0.026f, 1.0f);
            }
        }
    }

    if (mRng.chance(clamp01(mPattern.openHat[pos] * (0.34f + 0.70f * p.drum) * phraseDrumScale + (offEighth ? 0.016f : 0.0f)))) {
        scheduleDrum(humanizeSamples(0.52f, false, false), DrumType::HatOpen,
                     (0.08f + 0.14f * mPattern.energy) * accent * phraseDrumScale, mRng.bipolar() * 0.50f,
                     0.10f + mRng.uni() * 0.19f, mRng.uni());
        eventHappened = true;
    }

    float percP = mPattern.perc[pos] * (0.25f + 0.85f * mPattern.syncopation) * (0.50f + 0.70f * p.drum) * phraseDrumScale;
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
                     mRng.bipolar() * 0.72f, 0.045f + mRng.uni() * 0.16f, mRng.uni());
        eventHappened = true;
    }

    const bool needsLevelFloor = (section == SectionType::Intro || section == SectionType::Breakdown || section == SectionType::Outro);
    const bool floorPulse = needsLevelFloor && (p16 == 0 || p16 == 8);
    const bool anchorPulse = downbeat && !p.ambient;

    float bassGate = clamp01(mComposition.bassGate[p16] * mComposition.bassGain * phraseBassScale);
    if (floorPulse || anchorPulse) {
        bassGate = std::max(bassGate, p.ambient ? 0.30f : 0.44f);
    }
    if (bassGate > 0.02f && (floorPulse || anchorPulse || mRng.chance(clamp01(bassGate * (0.84f + 0.14f * accent))))) {
        const int32_t degree = bassDegreeForStep(p16, chordRoot, nextChordRoot);
        const int32_t octave = (mPattern.style == StyleType::TrapNoir || mPattern.style == StyleType::PulseDub) ? -1 : 0;
        const float freq = midiToHz(static_cast<float>(scaleDegreeToMidi(degree, octave)));
        float dur = stepDurationSeconds() * (p.halfTime ? 2.8f : 1.5f);
        if (downbeat) dur *= p.ambient ? 5.0f : 1.65f;
        scheduleBass(humanizeSamples(0.18f, downbeat, false), freq,
                     (0.25f + 0.27f * mPattern.energy) * phraseBassScale,
                     dur, mRng.bipolar() * 0.045f, p.rough + mRng.uni() * 0.22f);
        mLastBassStep = static_cast<int32_t>(mStepIndex);
        eventHappened = true;
    }

    float chordGate = clamp01(mComposition.chordGate[p16] * mComposition.chordGain * phraseChordScale);
    if (floorPulse) {
        chordGate = std::max(chordGate, p.ambient ? 0.66f : 0.48f);
    } else if (anchorPulse) {
        chordGate = std::max(chordGate, 0.16f);
    }
    if (chordGate > 0.02f && (floorPulse || mRng.chance(chordGate))) {
        const float dur = stepDurationSeconds() * (p.ambient ? 10.0f : (p.fourOnFloor ? 3.2f : 5.6f));
        const float amp = (0.050f + 0.12f * mPattern.texture + 0.08f * p.chord) * accent * phraseChordScale;
        schedulePad(humanizeSamples(0.28f, downbeat, false), chordRoot, amp, dur, mRng.bipolar() * 0.42f, mRng.uni());
        eventHappened = true;
    }

    bool leadRest = false;
    float leadGate = 0.0f;
    float leadDurSteps = 0.0f;
    int32_t leadDegree = grammarDegree(phrase, p16, chordRoot, leadRest, leadGate, leadDurSteps);
    leadGate = clamp01(leadGate * phraseLeadScale * (1.0f - 0.18f * mComposition.leadSpace));
    if (!leadRest && leadGate > 0.04f && mRng.chance(clamp01(0.72f + 0.26f * leadGate))) {
        int32_t octave = mComposition.octaveBias;
        if (phrase == PhraseType::Climax && mRng.chance(0.35f)) octave += 1;
        if (phrase == PhraseType::Breakdown) octave = std::max(1, octave - 1);
        const float freq = midiToHz(static_cast<float>(scaleDegreeToMidi(leadDegree, octave)));
        const float dur = stepDurationSeconds() * (0.60f + leadDurSteps * (p.ambient ? 3.4f : 1.75f));
        scheduleLead(humanizeSamples(0.55f, false, false), freq,
                     (0.055f + 0.12f * mPattern.melody + 0.05f * p.melodyRun) * accent * phraseLeadScale,
                     dur, mRng.bipolar() * 0.62f, mRng.uni());
        if (phrase != PhraseType::Breakdown && mRng.chance(mComposition.ornament * leadGate)) {
            const int32_t neighbor = leadDegree + (mRng.chance(0.50f) ? 1 : -1);
            const float f2 = midiToHz(static_cast<float>(scaleDegreeToMidi(neighbor, octave)));
            scheduleLead(static_cast<int32_t>(stepSamples * (0.45f + 0.24f * mRng.uni())), f2,
                         (0.026f + 0.050f * mPattern.melody) * accent * phraseLeadScale,
                         dur * 0.38f, mRng.bipolar() * 0.66f, mRng.uni());
        }
        mLastLeadStep = static_cast<int32_t>(mStepIndex);
        eventHappened = true;
    }

    if (phrase == PhraseType::Climax && (p16 == 14 || p16 == 15) && mRng.chance(0.18f + 0.20f * p.rough) && !p.ambient) {
        const int32_t repeats = mRng.rangeInt(2, 4);
        for (int32_t r = 0; r < repeats; ++r) {
            scheduleDrum(static_cast<int32_t>(stepSamples * (static_cast<float>(r) / static_cast<float>(repeats))),
                         mRng.chance(0.45f) ? DrumType::Zap : DrumType::Noise,
                         0.052f + 0.075f * mPattern.roughness,
                         mRng.bipolar() * 0.80f,
                         0.025f + 0.045f * mRng.uni(), mRng.uni());
        }
        eventHappened = true;
    }

    if (eventHappened) mSilentSteps = 0;
    if (mSilentSteps > (p.ambient ? 96 : 28)) {
        scheduleDrum(0, DrumType::Kick, p.ambient ? 0.20f : 0.62f, 0.0f, 0.45f, mPattern.roughness);
        scheduleBass(0, midiToHz(static_cast<float>(scaleDegreeToMidi(chordRoot, p.ambient ? 0 : -1))),
                     p.ambient ? 0.18f : 0.36f, stepDurationSeconds() * 2.0f, 0.0f, mPattern.roughness);
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

    const float attack = clamp(t * 24.0f, 0.0f, 1.0f);
    const float release = 1.0f - clamp((t - 0.66f) / 0.34f, 0.0f, 1.0f);
    const float env = attack * release;
    v.freq += (v.targetFreq - v.freq) * (0.003f + 0.010f * v.color);
    v.phase += v.freq / sr;
    v.phase2 += v.freq * 0.501f / sr;
    if (v.phase >= 1.0f) v.phase -= 1.0f;
    if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;

    const float saw = 2.0f * v.phase - 1.0f;
    const float sub = std::sin(kTwoPi * v.phase2);
    const float sine = std::sin(kTwoPi * v.phase);
    float raw = 0.48f * sine + 0.38f * sub + (0.20f + 0.38f * v.color) * saw;
    raw = std::tanh(raw * v.drive);
    const float cutoff = clamp(v.cutoff + env * (0.05f + 0.12f * v.color), 0.005f, 0.42f);
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

    const float attack = clamp(t * (v.dur > 2.0f ? 2.4f : 8.0f), 0.0f, 1.0f);
    const float release = 1.0f - clamp((t - 0.72f) / 0.28f, 0.0f, 1.0f);
    const float env = attack * release;
    float raw = 0.0f;
    for (int32_t i = 0; i < v.count; ++i) {
        v.phase[i] += v.freq[i] / sr;
        if (v.phase[i] >= 1.0f) v.phase[i] -= 1.0f;
        const float saw = 2.0f * v.phase[i] - 1.0f;
        const float tri = 1.0f - 4.0f * std::fabs(v.phase[i] - 0.5f);
        raw += 0.60f * tri + 0.28f * saw;
    }
    raw /= static_cast<float>(std::max(1, v.count));
    v.lp += (raw - v.lp) * clamp(v.cutoff, 0.004f, 0.22f);
    v.hp += (v.lp - v.hp) * 0.00035f;
    v.age += dt;
    return (v.lp - v.hp * 0.22f) * env * v.amp;
}

float MusicEngine::renderLead(LeadVoice& v) {
    const float sr = static_cast<float>(mSampleRate);
    const float dt = 1.0f / sr;
    const float t = v.age / std::max(0.001f, v.dur);
    if (t >= 1.0f) {
        v.active = false;
        return 0.0f;
    }

    const float attack = clamp(t * 18.0f, 0.0f, 1.0f);
    const float release = 1.0f - clamp((t - 0.58f) / 0.42f, 0.0f, 1.0f);
    const float env = attack * release;
    v.vibPhase += 4.8f / sr;
    if (v.vibPhase > 1.0f) v.vibPhase -= 1.0f;
    const float vib = 1.0f + std::sin(kTwoPi * v.vibPhase) * (0.001f + 0.004f * v.color);
    v.freq += (v.targetFreq - v.freq) * (0.0015f + 0.010f * v.color);
    const float freq = v.freq * vib;

    v.phase += freq / sr;
    v.modPhase += freq * (1.97f + 2.1f * v.color) / sr;
    if (v.phase >= 1.0f) v.phase -= 1.0f;
    if (v.modPhase >= 1.0f) v.modPhase -= 1.0f;

    float raw;
    if (v.color < 0.33f) {
        const float pulse = (v.phase < (0.35f + 0.20f * v.color)) ? 1.0f : -1.0f;
        raw = 0.70f * pulse + 0.30f * (2.0f * v.phase - 1.0f);
    } else if (v.color < 0.66f) {
        raw = std::sin(kTwoPi * v.phase + (1.3f + 4.2f * v.color) * std::sin(kTwoPi * v.modPhase));
    } else {
        const float saw = 2.0f * v.phase - 1.0f;
        const float pluck = std::sin(kTwoPi * v.phase) * 0.55f + saw * 0.45f;
        raw = std::tanh(pluck * (1.6f + 2.0f * v.color));
    }

    v.lp += (raw - v.lp) * clamp(v.cutoff * (0.50f + 1.20f * env), 0.008f, 0.46f);
    v.age += dt;
    return v.lp * env * v.amp;
}

float MusicEngine::renderTexture() {
    const StyleProfile p = profile(mPattern.style);
    const float sr = static_cast<float>(mSampleRate);
    const float amount = 0.014f + mPattern.texture * (0.026f + 0.056f * p.texture);
    if (amount <= 0.0001f) return 0.0f;
    mTexturePhaseA += (0.045f + 0.045f * p.texture) / sr;
    mTexturePhaseB += (0.071f + 0.038f * mPattern.roughness) / sr;
    if (mTexturePhaseA >= 1.0f) mTexturePhaseA -= 1.0f;
    if (mTexturePhaseB >= 1.0f) mTexturePhaseB -= 1.0f;
    const float n = noise(mTextureNoise) * 0.20f;
    const float slow = std::sin(kTwoPi * mTexturePhaseA) * 0.55f + std::sin(kTwoPi * mTexturePhaseB) * 0.35f + n;
    mTextureLp += (slow - mTextureLp) * 0.0017f;
    mTextureHp += (mTextureLp - mTextureHp) * 0.00022f;
    return (mTextureLp - 0.30f * mTextureHp) * amount;
}

void MusicEngine::applyDelayAndMaster(float& left, float& right) {
    if (!mDelayL.empty() && !mDelayR.empty()) {
        const StyleProfile p = profile(mPattern.style);
        const int32_t size = static_cast<int32_t>(mDelayL.size());
        const float bpm = std::max(40.0f, mBpm);
        const float beatSeconds = 60.0f / bpm;
        const float desired = beatSeconds * (p.ambient ? 0.75f : (p.breakbeat ? 0.375f : 0.50f));
        const int32_t targetDelay = clamp(static_cast<float>(desired * static_cast<float>(mSampleRate)), 1200.0f, static_cast<float>(size - 1));
        mDelaySamples += static_cast<int32_t>((targetDelay - mDelaySamples) * 0.00002f);
        mDelaySamples = std::max(1, std::min(size - 1, mDelaySamples));
        const int32_t read = (mDelayWrite - mDelaySamples + size) % size;
        const float dl = mDelayL[read];
        const float dr = mDelayR[read];
        const float send = mPattern.delay * (0.22f + 0.55f * mPattern.space);
        const float feedback = clamp(0.18f + 0.42f * mPattern.space + 0.10f * p.texture, 0.10f, 0.66f);
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
