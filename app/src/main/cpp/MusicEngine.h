#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace rb {

class MusicEngine {
public:
    MusicEngine();

    void prepare(double sampleRate);
    void reset(uint32_t seed);
    void next();
    void setPieceLengthSeconds(int32_t seconds);
    void render(float* output, int32_t frames, int32_t channelCount);

private:
    static constexpr int kPatternSteps = 64;
    static constexpr int kMotifSteps = 32;
    static constexpr int kChordSteps = 16;
    static constexpr int kMaxDrumVoices = 64;
    static constexpr int kMaxBassVoices = 16;
    static constexpr int kMaxPadVoices = 18;
    static constexpr int kMaxLeadVoices = 24;
    static constexpr int kMaxEvents = 128;
    static constexpr int kMemorySlots = 24;
    static constexpr int kRecentHashes = 64;
    static constexpr int kPhraseSteps = 16;
    static constexpr int kMaxFormSlots = 16;
    static constexpr int kMaxProgressionSlots = 8;
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kTwoPi = 6.28318530717958647692f;

    enum class DrumType : int32_t {
        Kick = 0,
        Snare = 1,
        Clap = 2,
        HatClosed = 3,
        HatOpen = 4,
        Rim = 5,
        Tom = 6,
        Perc = 7,
        Zap = 8,
        Noise = 9
    };

    enum class EventKind : int32_t {
        Drum = 0,
        Bass = 1,
        Pad = 2,
        Lead = 3
    };

    enum class StyleType : int32_t {
        BoomBap = 0,
        TrapNoir = 1,
        BreakRush = 2,
        ElectroFunk = 3,
        MicroHouse = 4,
        Synthwave = 5,
        GlitchHop = 6,
        VaporDrift = 7,
        PulseDub = 8,
        Count = 9
    };

    enum class TransitionStage : int32_t {
        None = 0,
        FadeOut = 1,
        DeadAir = 2,
        FadeIn = 3
    };


    enum class PhraseType : int32_t {
        Statement = 0,
        Repeat = 1,
        Answer = 2,
        Variation = 3,
        Breakdown = 4,
        Climax = 5,
        Hook = 6
    };

    enum class SectionType : int32_t {
        Intro = 0,
        Theme = 1,
        Hook = 2,
        Variation = 3,
        Breakdown = 4,
        Climax = 5,
        Outro = 6
    };

    struct Rng {
        uint32_t state = 0x12345678u;
        explicit Rng(uint32_t seed = 0x12345678u) : state(seed ? seed : 0x12345678u) {}
        uint32_t nextU32();
        float uni();
        float bipolar();
        bool chance(float probability);
        int32_t rangeInt(int32_t loInclusive, int32_t hiInclusive);
    };

    struct StyleProfile {
        StyleType type = StyleType::BoomBap;
        float bpmMin = 82.0f;
        float bpmMax = 96.0f;
        float swingMin = 0.04f;
        float swingMax = 0.14f;
        float density = 0.55f;
        float drum = 0.70f;
        float bass = 0.60f;
        float melody = 0.35f;
        float chord = 0.20f;
        float texture = 0.20f;
        float rough = 0.35f;
        float space = 0.35f;
        float sync = 0.45f;
        float hatRoll = 0.12f;
        float melodyRun = 0.20f;
        float transitionSilence = 0.35f;
        bool fourOnFloor = false;
        bool halfTime = false;
        bool breakbeat = false;
        bool trapHats = false;
        bool ambient = false;
    };



    struct Composition {
        int32_t generation = 0;
        int32_t pieceSteps = 512;
        uint32_t arcSeed = 0x524235u;
        int32_t sectionPhraseLength = 8;
        int32_t hookCycle = 4;
        int32_t formLength = 8;
        int32_t progressionLength = 4;
        std::array<int32_t, kMaxProgressionSlots> chordRoot{};
        std::array<PhraseType, kMaxFormSlots> form{};
        std::array<int32_t, kPhraseSteps> motifA{};
        std::array<int32_t, kPhraseSteps> motifB{};
        std::array<int32_t, kPhraseSteps> motifC{};
        std::array<float, kPhraseSteps> gateA{};
        std::array<float, kPhraseSteps> gateB{};
        std::array<float, kPhraseSteps> gateC{};
        std::array<float, kPhraseSteps> durA{};
        std::array<float, kPhraseSteps> durB{};
        std::array<float, kPhraseSteps> durC{};
        std::array<int32_t, kPhraseSteps> bassRel{};
        std::array<float, kPhraseSteps> bassGate{};
        std::array<float, kPhraseSteps> chordGate{};
        int32_t hookOffset = 0;
        int32_t answerOffset = 0;
        int32_t octaveBias = 2;
        float motifGain = 1.0f;
        float bassGain = 1.0f;
        float chordGain = 1.0f;
        float ornament = 0.20f;
        float leadSpace = 0.22f;
    };

    struct Pattern {
        std::array<float, kPatternSteps> kick{};
        std::array<float, kPatternSteps> snare{};
        std::array<float, kPatternSteps> hat{};
        std::array<float, kPatternSteps> openHat{};
        std::array<float, kPatternSteps> perc{};
        std::array<float, kPatternSteps> bass{};
        std::array<float, kPatternSteps> chord{};
        std::array<float, kPatternSteps> lead{};
        std::array<float, kPatternSteps> accent{};
        std::array<int32_t, kMotifSteps> bassMotif{};
        std::array<int32_t, kMotifSteps> leadMotif{};
        std::array<float, kMotifSteps> leadGate{};
        std::array<int32_t, kChordSteps> chordMotif{};
        StyleType style = StyleType::BoomBap;
        int32_t rootMidi = 36;
        int32_t scaleMode = 0;
        float swing = 0.08f;
        float humanize = 0.35f;
        float energy = 0.60f;
        float density = 0.55f;
        float syncopation = 0.45f;
        float texture = 0.20f;
        float roughness = 0.35f;
        float space = 0.35f;
        float melody = 0.35f;
        float delay = 0.16f;
        float drive = 0.55f;
    };

    struct ScheduledEvent {
        bool active = false;
        int32_t samples = 0;
        EventKind kind = EventKind::Drum;
        DrumType drumType = DrumType::Kick;
        float amp = 0.0f;
        float pan = 0.0f;
        float dur = 0.10f;
        float freq = 55.0f;
        float aux = 0.0f;
        int32_t degree = 0;
        int32_t octave = 0;
    };

    struct DrumVoice {
        bool active = false;
        DrumType type = DrumType::Kick;
        float age = 0.0f;
        float dur = 0.20f;
        float amp = 0.0f;
        float panL = 0.707f;
        float panR = 0.707f;
        float phase = 0.0f;
        float phase2 = 0.0f;
        float lp = 0.0f;
        float hp = 0.0f;
        float aux = 0.0f;
        uint32_t noiseState = 1u;
    };

    struct BassVoice {
        bool active = false;
        float age = 0.0f;
        float dur = 0.35f;
        float amp = 0.0f;
        float panL = 0.707f;
        float panR = 0.707f;
        float phase = 0.0f;
        float phase2 = 0.0f;
        float freq = 55.0f;
        float targetFreq = 55.0f;
        float lp = 0.0f;
        float cutoff = 0.05f;
        float drive = 1.0f;
        float color = 0.0f;
    };

    struct PadVoice {
        bool active = false;
        float age = 0.0f;
        float dur = 1.0f;
        float amp = 0.0f;
        float panL = 0.707f;
        float panR = 0.707f;
        std::array<float, 4> phase{};
        std::array<float, 4> freq{};
        int32_t count = 3;
        float lp = 0.0f;
        float hp = 0.0f;
        float cutoff = 0.025f;
        float color = 0.0f;
    };

    struct LeadVoice {
        bool active = false;
        float age = 0.0f;
        float dur = 0.35f;
        float amp = 0.0f;
        float panL = 0.707f;
        float panR = 0.707f;
        float phase = 0.0f;
        float modPhase = 0.0f;
        float vibPhase = 0.0f;
        float freq = 220.0f;
        float targetFreq = 220.0f;
        float lp = 0.0f;
        float cutoff = 0.08f;
        float color = 0.0f;
        uint32_t noiseState = 1u;
    };

    double mSampleRate = 48000.0;
    bool mPrepared = false;
    Rng mRng;

    Pattern mPattern;
    Composition mComposition;
    std::array<Pattern, kMemorySlots> mMemory{};
    int32_t mMemoryWrite = 0;

    std::array<DrumVoice, kMaxDrumVoices> mDrums{};
    std::array<BassVoice, kMaxBassVoices> mBass{};
    std::array<PadVoice, kMaxPadVoices> mPads{};
    std::array<LeadVoice, kMaxLeadVoices> mLeads{};
    std::array<ScheduledEvent, kMaxEvents> mEvents{};

    int64_t mStepIndex = -1;
    double mSamplesUntilNextStep = 0.0;
    float mBpm = 92.0f;
    float mBpmTarget = 92.0f;
    int32_t mStyleAgeSteps = 0;
    int32_t mStyleTargetSteps = 800;
    int32_t mRequestedPieceSeconds = 1200;
    int32_t mPhraseSeed = 0;
    int32_t mLeadRunSteps = 0;
    int32_t mLastKickStep = -1000;
    int32_t mLastSnareStep = -1000;
    int32_t mLastBassStep = -1000;
    int32_t mLastLeadStep = -1000;
    int32_t mSilentSteps = 0;

    TransitionStage mTransitionStage = TransitionStage::None;
    StyleType mPendingStyle = StyleType::BoomBap;
    int32_t mTransitionSamplesLeft = 0;
    int32_t mTransitionSamplesTotal = 1;
    int32_t mDeadAirSamples = 0;
    float mTransitionGain = 1.0f;

    float mSidechain = 1.0f;
    float mMaster = 0.74f;
    float mAgcRms = 0.010f;
    float mAgcGain = 1.0f;
    float mNovelty = 0.0f;
    float mPocketLate = 0.0f;
    float mTexturePhaseA = 0.0f;
    float mTexturePhaseB = 0.0f;
    float mTextureLp = 0.0f;
    float mTextureHp = 0.0f;
    uint32_t mTextureNoise = 0xabcdefu;

    std::vector<float> mDelayL;
    std::vector<float> mDelayR;
    int32_t mDelayWrite = 0;
    int32_t mDelaySamples = 12000;
    float mDcInL = 0.0f;
    float mDcInR = 0.0f;
    float mDcOutL = 0.0f;
    float mDcOutR = 0.0f;

    std::array<uint32_t, kRecentHashes> mRecentHash{};
    int32_t mRecentHashWrite = 0;

    StyleProfile profile(StyleType style) const;
    StyleType randomStyle();
    StyleType randomDifferentStyle(StyleType current);
    void scheduleNextStyleTarget();
    int32_t pieceStepsFromSeconds(int32_t seconds, float bpm) const;
    void generatePattern(StyleType style);
    void generateComposition(const StyleProfile& p);
    void fillMotifFromTemplate(std::array<int32_t, kPhraseSteps>& motif,
                               std::array<float, kPhraseSteps>& gate,
                               std::array<float, kPhraseSteps>& dur,
                               int32_t templateId, int32_t contourOffset);
    void deriveAnswerMotif();
    void writeCompositionToPattern();
    void mutateDrumsOnly();
    void repairPattern();
    void mutateSmall();
    void mutateLarge();
    void storeMemory();
    void recallMemory(float amount);
    uint32_t patternHash() const;
    bool isHashRecent(uint32_t hash) const;
    int32_t currentChordRoot(int32_t step) const;
    SectionType currentSectionType(int32_t step) const;
    PhraseType currentPhraseType(int32_t step) const;
    int32_t grammarDegree(PhraseType phrase, int32_t phrasePos, int32_t chordRoot, bool& isRest, float& gate, float& dur) const;
    int32_t bassDegreeForStep(int32_t phrasePos, int32_t chordRoot, int32_t nextChordRoot) const;

    void beginTransition();
    void switchToPendingStyle();
    void updateTransition();
    void clearVoicesAndEvents();

    void onStep();
    void processEvents();
    double stepDurationSamples() const;
    float stepDurationSeconds() const;
    int32_t humanizeSamples(float amount, bool downbeat, bool backbeat);

    void scheduleDrum(int32_t offsetSamples, DrumType type, float amp, float pan, float dur, float aux = 0.0f);
    void scheduleBass(int32_t offsetSamples, float freq, float amp, float dur, float pan, float color);
    void schedulePad(int32_t offsetSamples, int32_t degree, float amp, float dur, float pan, float color);
    void scheduleLead(int32_t offsetSamples, float freq, float amp, float dur, float pan, float color);

    void triggerDrum(DrumType type, float amp, float pan, float dur, float aux = 0.0f);
    void triggerBass(float freq, float amp, float dur, float pan, float color);
    void triggerPad(int32_t degree, float amp, float dur, float pan, float color);
    void triggerLead(float freq, float amp, float dur, float pan, float color);

    float renderDrum(DrumVoice& voice);
    float renderBass(BassVoice& voice);
    float renderPad(PadVoice& voice);
    float renderLead(LeadVoice& voice);
    float renderTexture();
    void applyDelayAndMaster(float& left, float& right);

    float noise(uint32_t& state);
    float midiToHz(float midi) const;
    int32_t scaleLength() const;
    int32_t scaleDegreeToMidi(int32_t degree, int32_t octaveOffset = 0) const;
    float clamp01(float value) const;
    float clamp(float value, float lo, float hi) const;
    void panGains(float pan, float& left, float& right) const;
};

} // namespace rb
