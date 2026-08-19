#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace rb {

class MusicEngine {
public:
    MusicEngine();

    void prepare(double sampleRate);
    void reset(uint32_t seed);
    void next();
    void forceNewPiece();
    void setGenreMask(int32_t mask);
    void setGenreBlendMode(int32_t mode);
    void setGenrePrimary(int32_t mode);
    int32_t currentGenreMask() const;
    int32_t currentGenreBlendMode() const;
    int32_t currentGenrePrimary() const;
    int32_t currentGenreMode() const;
    void render(float* output, int32_t frames, int32_t channelCount);
    std::string currentSongData() const;
    std::string historyData() const;
    void clearHistory();
    bool loadSongData(const std::string& data);
    double currentElapsedSeconds() const;
    static bool decodeSongData(const std::string& data, uint32_t& seedOut, int32_t& secondsOut);
    static bool exportPcm16File(const std::string& data, int32_t seconds, const std::string& path, const std::atomic<bool>* cancelFlag = nullptr);

private:
    static constexpr int kPatternSteps = 64;
    static constexpr int kMotifSteps = 32;
    static constexpr int kChordSteps = 16;
    static constexpr int kMaxDrumVoices = 64;
    static constexpr int kMaxBassVoices = 24;
    static constexpr int kMaxPadVoices = 36;
    static constexpr int kMaxLeadVoices = 64;
    static constexpr int kMaxEvents = 320;
    static constexpr int kRecentHashes = 192;
    static constexpr int kRecentMotifHashes = 256;
    static constexpr int kSongHistoryLimit = 20;
    static constexpr int kPhraseSteps = 16;
    static constexpr int kMaxFormSlots = 16;
    static constexpr int kMaxProgressionSlots = 8;
    static constexpr int kThemeSlots = 8;
    static constexpr int kGenreModeCount = 12;
    static constexpr int32_t kIndefinitePieceSteps = 1000000000;
    static constexpr int32_t kDefaultExportSeconds = 180;
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
        ConcretePulse = 0,
        GlassNoir = 1,
        ShardRush = 2,
        NeonLatch = 3,
        TinyGrid = 4,
        PrismCruise = 5,
        BrokenMagnet = 6,
        VelvetDrift = 7,
        SubOrbit = 8,
        SoftVoltage = 9,
        DeepMagnet = 10,
        ChromeBloom = 11,
        WarmCurrent = 12,
        PulseGarden = 13,
        VoidStep = 14,
        SolarFold = 15,
        IonGarden = 16,
        MarbleBass = 17,
        EchoCrown = 18,
        BitFog = 19,
        MagentaWell = 20,
        CarbonRain = 21,
        LatticeSun = 22,
        StrangeHarbor = 23,
        CopperChord = 24,
        GhostMeter = 25,
        ObsidianBloom = 26,
        VoltageMoth = 27,
        QuartzTide = 28,
        StaticCathedral = 29,
        MercuryThread = 30,
        NightLatch = 31,
        Count = 32
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
        Suspension = 4,
        Surge = 5,
        Hook = 6,
        Mirror = 7,
        Orbit = 8,
        Cascade = 9,
        Crystallize = 10,
        Eclipse = 11,
        Afterimage = 12,
        Weave = 13,
        Tide = 14,
        Hinge = 15,
        Shadow = 16
    };

    enum class SectionType : int32_t {
        Intro = 0,
        Theme = 1,
        Hook = 2,
        Variation = 3,
        Suspension = 4,
        Surge = 5,
        Outro = 6,
        Mirror = 7,
        Orbit = 8,
        Cascade = 9,
        Crystallize = 10,
        Eclipse = 11,
        Afterimage = 12,
        Weave = 13,
        Tide = 14,
        Hinge = 15,
        Shadow = 16
    };

    enum class MelodyLayer : int32_t {
        Lead = 0,
        Bass = 1,
        Counter = 2,
        Arp = 3,
        Pulse = 4,
        Ornament = 5
    };

    // Tension is a generated musical condition, not a fixed pad rhythm.
    enum class TensionDevice : int32_t {
        None = 0,
        Vacuum = 1,
        Convergence = 2,
        Hinge = 3,
        Shadow = 4,
        Afterimage = 5,
        PadBreath = 6
    };

    struct EvolutionFrame {
        int32_t epoch = 0;
        float phase = 0.0f;
        float leadMutation = 0.0f;
        float bassMutation = 0.0f;
        float rhythmMigration = 0.0f;
        float densityDrift = 0.0f;
        float registerTide = 0.0f;
        float harmonyLens = 0.0f;
        float paletteDrift = 0.0f;
        float braid = 0.0f;
        int32_t focusPosition = 8;
        int32_t direction = 1;
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
        StyleType type = StyleType::ConcretePulse;
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
        float drama = 0.35f;
        float palette = 0.60f;
        float brightness = 0.50f;
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
        // Generated musical identity. These seeds create laws, not stored melodies.
        uint32_t leadGrammarSeed = 0x13579bdfu;
        uint32_t bassGrammarSeed = 0x2468ace1u;
        uint32_t counterGrammarSeed = 0x9e3779b9u;
        uint32_t arpGrammarSeed = 0x243f6a88u;
        uint32_t pulseGrammarSeed = 0xb7e15162u;
        uint32_t ornamentGrammarSeed = 0x8aed2a6bu;
        // Derived from the existing identity seeds so adding tension grammar does
        // not perturb candidate selection or the established lead/bass identity.
        uint32_t tensionGrammarSeed = 0x94d049bbu;
        // Per-sound electronic instrument identity. This is derived from the
        // composition grammar, so expanding the sound bank does not perturb
        // the established melody, bass, harmony, or candidate search.
        uint32_t timbreGrammarSeed = 0x7f4a7c15u;
        uint32_t evolutionSeed = 0xc6ef3720u;
        int32_t leadContour = 0;
        int32_t leadApexStep = 7;
        int32_t leadApexDegree = 5;
        int32_t leadCadenceDegree = 0;
        int32_t leadNoteCount = 8;
        int32_t leadLeapBudget = 2;
        float leadSyncopation = 0.40f;
        float bassSyncopation = 0.36f;
        float bassKickAffinity = 0.62f;
        float bassRootGravity = 0.72f;
        int32_t bassHitCount = 6;
        int32_t bassAnswerShift = 0;
        int32_t bassMotionBias = 0;
        int32_t counterDirection = -1;
        int32_t counterInterval = 4;
        int32_t counterDelay = 2;
        int32_t arpStride = 3;
        int32_t arpRotation = 0;
        int32_t evolutionCycle = 12;
        int32_t evolutionSpan = 3;
        int32_t evolutionMorphPhrases = 5;
        int32_t identityReturnCycle = 32;
        int32_t verseMutationSpan = 2;
        float evolutionDepth = 0.42f;
        float hookStability = 0.88f;
        float verseFreedom = 0.58f;
        float layerMigration = 0.50f;
        float harmonyEvolution = 0.34f;
        int32_t sectionPhraseLength = 8;
        int32_t hookCycle = 4;
        int32_t formLength = 8;
        int32_t progressionLength = 4;
        std::array<int32_t, kMaxProgressionSlots> chordRoot{};
        std::array<PhraseType, kMaxFormSlots> form{};
        std::array<int32_t, kPhraseSteps> motifA{};
        std::array<int32_t, kPhraseSteps> motifB{};
        std::array<int32_t, kPhraseSteps> motifC{};
        std::array<int32_t, kPhraseSteps> motifD{};
        std::array<int32_t, kPhraseSteps> motifE{};
        std::array<int32_t, kPhraseSteps> motifF{};
        std::array<int32_t, kPhraseSteps> motifG{};
        std::array<float, kPhraseSteps> gateA{};
        std::array<float, kPhraseSteps> gateB{};
        std::array<float, kPhraseSteps> gateC{};
        std::array<float, kPhraseSteps> gateD{};
        std::array<float, kPhraseSteps> gateE{};
        std::array<float, kPhraseSteps> gateF{};
        std::array<float, kPhraseSteps> gateG{};
        std::array<float, kPhraseSteps> durA{};
        std::array<float, kPhraseSteps> durB{};
        std::array<float, kPhraseSteps> durC{};
        std::array<float, kPhraseSteps> durD{};
        std::array<float, kPhraseSteps> durE{};
        std::array<float, kPhraseSteps> durF{};
        std::array<float, kPhraseSteps> durG{};
        std::array<int32_t, kThemeSlots> themeOffset{};
        std::array<int32_t, kThemeSlots> themeContour{};
        std::array<float, kThemeSlots> themeWeight{};
        std::array<int32_t, kPhraseSteps> bassRel{};
        std::array<float, kPhraseSteps> bassGate{};
        std::array<int32_t, kPhraseSteps> bassVerseRel{};
        std::array<float, kPhraseSteps> bassVerseGate{};
        std::array<int32_t, kPhraseSteps> bassAnswerRel{};
        std::array<float, kPhraseSteps> bassAnswerGate{};
        std::array<int32_t, kPhraseSteps> counterRel{};
        std::array<float, kPhraseSteps> counterGate{};
        std::array<float, kPhraseSteps> counterDur{};
        std::array<int32_t, kPhraseSteps> arpRel{};
        std::array<float, kPhraseSteps> arpGate{};
        std::array<float, kPhraseSteps> arpDur{};
        std::array<int32_t, kPhraseSteps> pulseRel{};
        std::array<float, kPhraseSteps> pulseGate{};
        std::array<float, kPhraseSteps> pulseDur{};
        std::array<int32_t, kPhraseSteps> ornamentRel{};
        std::array<float, kPhraseSteps> ornamentGate{};
        std::array<float, kPhraseSteps> ornamentDur{};
        // Per-seed tension grammar. It replaces the former universal pad stab at
        // phrase positions 2, 6, 10 and 14.
        std::array<int32_t, kPhraseSteps> tensionRel{};
        std::array<float, kPhraseSteps> tensionGate{};
        std::array<float, kPhraseSteps> tensionDur{};
        int32_t tensionPrimary = static_cast<int32_t>(TensionDevice::Vacuum);
        int32_t tensionSecondary = static_cast<int32_t>(TensionDevice::Convergence);
        int32_t tensionCycle = 7;
        int32_t tensionPhase = 0;
        int32_t tensionStride = 5;
        int32_t tensionWindow = 2;
        float tensionDepth = 0.44f;
        float padBreathRarity = 0.025f;
        // 0 split dyad, 1 unequal cascade, 2 polarity frame,
        // 3 rare pad breath, 4 harmonic beam.
        int32_t chordArticulation = 0;
        std::array<float, kPhraseSteps> chordGate{};

        // A broad but bounded per-sound instrument grammar. Each generated
        // sound chooses a related window from these banks rather than using
        // one universal bass, lead, pad, and drum timbre.
        int32_t bassModel = 0;
        int32_t leadModel = 0;
        int32_t padModel = 0;
        int32_t drumKit = 0;
        int32_t padVoiceCount = 3;
        std::array<int32_t, 4> padIntervals{{0, 2, 4, 6}};
        float bassAttack = 0.45f;
        float bassRelease = 0.72f;
        float bassGlide = 0.24f;
        float bassPulseWidth = 0.34f;
        float bassMotion = 0.35f;
        float leadAttack = 0.42f;
        float leadRelease = 0.70f;
        float leadGlide = 0.20f;
        float leadVibratoDepth = 0.24f;
        float leadVibratoRate = 0.45f;
        float leadModRatio = 0.40f;
        float leadAir = 0.18f;
        float padAttack = 0.60f;
        float padRelease = 0.82f;
        float padDetune = 0.28f;
        float padMotion = 0.30f;
        float padWidth = 0.52f;
        float drumBody = 0.52f;
        float drumMetal = 0.38f;
        float drumNoise = 0.48f;
        int32_t hookOffset = 0;
        int32_t answerOffset = 0;
        int32_t themeCount = 3;
        int32_t recallCycle = 9;
        int32_t dialogueCycle = 5;
        int32_t counterShape = 0;
        int32_t themeShapeId = 0;
        int32_t octaveBias = 2;
        float longMemory = 0.55f;
        float callResponse = 0.45f;
        float counterpoint = 0.40f;
        float melodicGravity = 0.68f;
        float phraseArc = 0.50f;
        float layerDepth = 0.70f;
        float motifGain = 1.0f;
        float bassGain = 1.0f;
        float chordGain = 1.0f;
        float ornament = 0.20f;
        float leadSpace = 0.22f;
        float drama = 0.35f;
        float deviceDepth = 0.45f;
        bool conclusiveOutro = false;
        float hookEmphasis = 1.0f;
        float surgeLift = 1.0f;
        float useKick = 1.0f;
        float useSnare = 1.0f;
        float useHat = 1.0f;
        float useOpenHat = 1.0f;
        float usePerc = 1.0f;
        float useBass = 1.0f;
        float useChord = 1.0f;
        float useLead = 1.0f;
        float useTexture = 1.0f;
        float useArp = 0.0f;
        float useCounter = 0.0f;
        float useStab = 0.0f;
        float useDrone = 0.0f;
        float useSpark = 0.0f;
        float useFx = 0.0f;
        float useEcho = 0.0f;
        float useOrbit = 0.0f;
        float useBloom = 0.0f;
        float useGlyph = 0.0f;
        float useSub = 0.0f;
        float useSheen = 0.0f;
        float usePluck = 0.0f;
        float useBell = 0.0f;
        float usePulse = 0.0f;
        float useGrain = 0.0f;
        float useComet = 0.0f;
        float useRotor = 0.0f;
        int32_t motifTemplateId = 0;
        int32_t progressionId = 0;
        uint32_t paletteHash = 0u;
        uint32_t motifHash = 0u;
        float kickTone = 0.45f;
        float snareTone = 0.45f;
        float hatTone = 0.45f;
        float percTone = 0.45f;
        float bassTone = 0.45f;
        float padTone = 0.45f;
        float leadTone = 0.45f;
        float arpTone = 0.45f;
        float counterTone = 0.45f;
        float stabTone = 0.45f;
        float droneTone = 0.45f;
        float sparkTone = 0.45f;
        float fxTone = 0.45f;
        float echoTone = 0.45f;
        float orbitTone = 0.45f;
        float bloomTone = 0.45f;
        float glyphTone = 0.45f;
        float subTone = 0.45f;
        float sheenTone = 0.45f;
        float pluckTone = 0.45f;
        float bellTone = 0.45f;
        float pulseTone = 0.45f;
        float grainTone = 0.45f;
        float cometTone = 0.45f;
        float rotorTone = 0.45f;
        float textureTone = 0.45f;
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
        StyleType style = StyleType::ConcretePulse;
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
        float profileTexture = 0.20f;
        bool profileAmbient = false;
        bool profileBreakbeat = false;
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
        int32_t kit = 0;
        float body = 0.50f;
        float metal = 0.35f;
        float noiseMix = 0.50f;
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
        int32_t model = 0;
        float attackShape = 0.45f;
        float releasePoint = 0.72f;
        float glide = 0.24f;
        float pulseWidth = 0.34f;
        float motion = 0.35f;
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
        int32_t model = 0;
        float attackShape = 0.60f;
        float releasePoint = 0.82f;
        float detune = 0.28f;
        float motion = 0.30f;
        float width = 0.52f;
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
        int32_t model = 0;
        float attackShape = 0.42f;
        float releasePoint = 0.70f;
        float glide = 0.20f;
        float vibratoDepth = 0.24f;
        float vibratoRate = 0.45f;
        float modRatio = 0.40f;
        float air = 0.18f;
        uint32_t noiseState = 1u;
    };

    double mSampleRate = 48000.0;
    bool mPrepared = false;
    // Performance variation is separated from long-form development so that
    // ornament randomness cannot change the composition's structural future.
    Rng mRng;
    Rng mDevelopmentRng;

    Pattern mPattern;
    Composition mComposition;

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
    bool mExportSinglePieceMode = false;
    int64_t mExportStopSamples = 0;
    // mGenreMask/mGenreBlendMode are the user's selector for the next generated piece.
    // mActiveGenreMask/mActiveGenreBlendMode are frozen into the currently sounding piece.
    // Rendering and profile lookup must use the active values, otherwise loading history
    // while a different selector is active audibly bleeds the new genre into the old track.
    int32_t mGenreMask = 0;
    int32_t mGenreBlendMode = 0;
    int32_t mGenrePrimary = 0;
    int32_t mActiveGenreMask = 0;
    int32_t mActiveGenreBlendMode = 0;
    int32_t mActiveGenrePrimary = 0;
    int32_t mWorkingGenreMode = 0;
    std::atomic<int32_t> mCurrentGenreMode{0};
    int32_t mPhraseSeed = 0;
    int32_t mLeadRunSteps = 0;
    int32_t mLastKickStep = -1000;
    int32_t mLastSnareStep = -1000;
    int32_t mLastBassStep = -1000;
    int32_t mLastLeadStep = -1000;
    int32_t mSilentSteps = 0;

    TransitionStage mTransitionStage = TransitionStage::None;
    uint32_t mCurrentSongSeed = 0x52423934u;
    uint32_t mPendingSongSeed = 0x52423934u;
    int32_t mCurrentCandidateIndex = 0;
    int32_t mForcedCandidateIndex = -1;
    bool mCurrentSongEdited = false;
    std::atomic<int64_t> mCurrentPieceSamples{0};
    mutable std::mutex mSongDataMutex;
    std::string mCurrentSongData;
    mutable std::mutex mHistoryMutex;
    std::array<std::string, kSongHistoryLimit> mSongHistory{};
    int32_t mSongHistorySize = 0;
    bool mSuppressHistoryRecord = false;
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
    std::array<uint32_t, kRecentMotifHashes> mRecentMotifHash{};
    int32_t mRecentMotifHashWrite = 0;

    StyleProfile profile(StyleType style) const;
    StyleType randomStyle();
    int32_t chooseGenreModeFromMask(int32_t genreMask);
    StyleProfile channelProfile(int32_t mode, const StyleProfile& base) const;
    void mixProfile(StyleProfile& target, const StyleProfile& source, float amount) const;
    void applyChannelBias(StyleProfile& p) const;
    float scoreCurrentComposition() const;
    int32_t pieceStepsFromSeconds(int32_t seconds, float bpm) const;
    void generateSeededSong(uint32_t seed);
    void updateCurrentSongData();
    void recordCurrentSongDataToHistory();
    void applySongDataOverrides(const std::string& data);
    void generatePattern(StyleType style);
    void generateComposition(const StyleProfile& p);
    void chooseInstrumentPalette(const StyleProfile& p);
    void generateHarmonyGrammar(const StyleProfile& p);
    void generatePrimaryMotif(const StyleProfile& p);
    void generateBassGrammar(const StyleProfile& p);
    void generateSecondaryLayerGrammars(const StyleProfile& p);
    void generateTensionGrammar(const StyleProfile& p);
    void generateTimbreGrammar(const StyleProfile& p);
    void deriveRelatedMotifs();
    void writeCompositionToPattern();
    void mutateDrumsOnly();
    void repairPattern();
    uint32_t patternHash() const;
    bool isHashRecent(uint32_t hash) const;
    uint32_t motifSignatureHash() const;
    bool isMotifHashRecent(uint32_t hash) const;
    int32_t currentChordRoot(int32_t step) const;
    int32_t outroGravitySteps() const;
    SectionType currentSectionType(int32_t step) const;
    PhraseType currentPhraseType(int32_t step) const;
    int32_t grammarDegree(PhraseType phrase, int32_t phrasePos, int32_t chordRoot, bool& isRest, float& gate, float& dur) const;
    int32_t themeIndexForStep(int32_t step) const;
    int32_t applyThemeTransform(int32_t degree, int32_t step, int32_t phrasePos, int32_t chordRoot, PhraseType phrase) const;
    int32_t counterpointDegree(int32_t step, int32_t phrasePos, int32_t chordRoot) const;
    int32_t bassDegreeForStep(int32_t step, int32_t phrasePos, int32_t chordRoot, int32_t nextChordRoot, PhraseType phrase, SectionType section) const;
    EvolutionFrame evolutionFrameForStep(int32_t step) const;
    uint32_t evolutionHash(int32_t epoch, int32_t phrasePos, MelodyLayer layer, uint32_t salt = 0u) const;
    float sectionVariationAmount(PhraseType phrase, SectionType section) const;
    int32_t evolvedDegree(int32_t degree, int32_t step, int32_t phrasePos, int32_t chordRoot, PhraseType phrase, SectionType section, MelodyLayer layer) const;
    float evolvedGate(float gate, int32_t step, int32_t phrasePos, PhraseType phrase, SectionType section, MelodyLayer layer) const;
    int32_t generatedLayerDegree(MelodyLayer layer, int32_t step, int32_t phrasePos, int32_t chordRoot, PhraseType phrase, SectionType section) const;
    float generatedLayerGate(MelodyLayer layer, int32_t step, int32_t phrasePos, PhraseType phrase, SectionType section) const;
    float generatedLayerDur(MelodyLayer layer, int32_t phrasePos) const;
    TensionDevice tensionDeviceForPhrase(int32_t phraseNumber, SectionType section) const;
    int32_t tensionFocusForPhrase(int32_t phraseNumber) const;
    float tensionGateForStep(int32_t phraseNumber, int32_t phrasePos) const;

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
    void scheduleChordGesture(int32_t offsetSamples, int32_t degree, float amp, float dur,
                              float pan, float color, int32_t articulation);
    bool hasSustainedPad(float remainingSeconds = 0.18f) const;

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
