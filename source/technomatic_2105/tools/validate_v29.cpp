// Build from the project root:
// g++ -std=c++17 -O2 -pthread tools/validate_v29.cpp \
//     app/src/main/cpp/MusicEngine.cpp -o /tmp/technomatic_validate_v29
// /tmp/technomatic_validate_v29
// This retains v28 regressions and adds behavior tests for the radio design.
#define main validateV28Regressions
#include "validate_v28.cpp"
#undef main
#define TECHNOMATIC_AUDIT_NO_MAIN
#include "audit_music.cpp"
#include <chrono>
#include <numeric>

namespace {

template<class Notes, class Gates>
std::string symbolicIdentity(const Notes& notes, const Gates& gates) {
    std::ostringstream out;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (gates[i] > 0.02f) out << notes[i];
        else out << 'r';
        out << ',';
    }
    return out.str();
}

struct IntentSummary {
    int count = 0;
    double bpm = 0;
    double density = 0;
    double leadHits = 0;
    double hatHits = 0;
    double foreground = 0;
    double auxiliary = 0;
    float bpmMin = 1000;
    float bpmMax = 0;
    std::set<std::string> leadIds;
    std::set<std::string> bassIds;
    std::set<std::string> bassDevelopmentIds;
    std::set<int> styles;
    uint32_t representativeSeed = 0;
};

bool exactSnapshotFor(uint32_t seed, int mask, int blend, int primary) {
    rb::MusicEngine a, b;
    a.prepare(48000.0);
    b.prepare(48000.0);
    a.setGenreMask(mask);
    a.setGenreBlendMode(blend);
    a.setGenrePrimary(primary);
    a.reset(seed);
    // Loading must be isolated from a different selection for the next tune.
    b.setGenreMask(1 << 9);
    b.setGenreBlendMode(0);
    b.setGenrePrimary(10);
    if (!b.loadSongData(a.currentSongData())) return false;
    constexpr int block = 257;
    std::vector<float> aa(block * 2), bb(block * 2);
    for (int frames = 6 * 48000; frames > 0; frames -= block) {
        const int n = std::min(frames, block);
        a.render(aa.data(), n, 2);
        b.render(bb.data(), n, 2);
        if (!std::equal(aa.begin(), aa.begin() + n * 2, bb.begin())) return false;
    }
    return true;
}

} // namespace

int main() {
    bool ok = validateV28Regressions() == 0;
    auto require = [&](bool condition, const std::string& label) {
        std::cout << (condition ? "PASS " : "FAIL ") << label << std::endl;
        ok = ok && condition;
    };

    constexpr int seedCount = 512;
    std::array<IntentSummary, 4> summaries{};
    std::set<int> winningStyles;
    std::vector<float> tempos;
    bool validIntents = true;
    bool noSfxLanes = true;
    bool foregroundPresent = true;
    bool serialized = true;
    float maxAuxiliaryRatio = 0;
    int slow = 0, fast = 0;
    for (int i = 1; i <= seedCount; ++i) {
        const uint32_t seed = static_cast<uint32_t>(i) * 2654435761u;
        rb::MusicEngine engine;
        engine.prepare(48000.0);
        engine.reset(seed);
        const auto& p = engine.mPattern;
        const auto& c = engine.mComposition;
        const std::string data = engine.currentSongData();
        const int intent = audit::field(data, "intent");
        validIntents = validIntents && intent >= 0 && intent < 4;
        serialized = serialized && audit::field(data, "engine") == 29;
        noSfxLanes = noSfxLanes && c.useTexture == 0 && c.useFx == 0 && c.useGrain == 0;
        foregroundPresent = foregroundPresent && c.useLead > 0.1f && c.useBass > 0.1f;
        const float ratio = audit::auxiliary(c) / std::max(0.01f, audit::foreground(c));
        maxAuxiliaryRatio = std::max(maxAuxiliaryRatio, ratio);
        winningStyles.insert(static_cast<int>(p.style));
        tempos.push_back(engine.mBpmTarget);
        if (engine.mBpmTarget <= 90.0f) ++slow;
        if (engine.mBpmTarget >= 135.0f) ++fast;
        if (intent < 0 || intent >= 4) continue;
        auto& summary = summaries[intent];
        ++summary.count;
        summary.bpm += engine.mBpmTarget;
        summary.bpmMin = std::min(summary.bpmMin, engine.mBpmTarget);
        summary.bpmMax = std::max(summary.bpmMax, engine.mBpmTarget);
        summary.density += p.density;
        summary.leadHits += audit::hits(c.gateA);
        summary.hatHits += audit::hits(p.hat);
        summary.foreground += audit::foreground(c);
        summary.auxiliary += audit::auxiliary(c);
        summary.styles.insert(static_cast<int>(p.style));
        summary.leadIds.insert(symbolicIdentity(c.motifA, c.gateA));
        summary.bassIds.insert(symbolicIdentity(c.bassRel, c.bassGate));
        summary.bassDevelopmentIds.insert(symbolicIdentity(c.bassRel, c.bassGate) + ";" +
            symbolicIdentity(c.bassVerseRel, c.bassVerseGate) + ";" +
            symbolicIdentity(c.bassAnswerRel, c.bassAnswerGate));
        if (!summary.representativeSeed) summary.representativeSeed = seed;
    }
    require(validIntents && serialized, "all generated snapshots identify engine 29 and a valid listening intent");
    require(noSfxLanes, "texture noise, special-effect and grain lanes are off across 512 seeds");
    require(foregroundPresent, "every sampled tune has a lead and a bass foundation");
    require(winningStyles.size() >= 24, "candidate selection retains broad style-family coverage");
    require(slow >= seedCount / 5 && fast >= seedCount / 5,
            "slow and intense tempos each reach at least one fifth of independent seeds");
    require(maxAuxiliaryRatio < 1.7f,
            "support-lane control weights stay bounded relative to the musical foreground");

    for (int intent = 0; intent < 4; ++intent) {
        const auto& s = summaries[intent];
        const std::string suffix = " for intent " + std::to_string(intent);
        require(s.count >= seedCount * 15 / 100, "listening intent has meaningful generation coverage" + suffix);
        require(s.bpmMax - s.bpmMin >= 15.0f, "tempo varies within the listening intent" + suffix);
        require(s.styles.size() >= 12, "style families vary within the listening intent" + suffix);
        require(s.leadIds.size() >= static_cast<std::size_t>(s.count * 95 / 100),
                "lead note/rest identities vary across seeds" + suffix);
        // Sparse root-led bass may share a short anchoring gesture. Test the
        // actual generated foundation/verse/answer grammar, rather than force
        // extra notes into quiet music to make every short cell different.
        // The unadjusted base-phrase diversity remains reported below.
        require(s.bassDevelopmentIds.size() >= static_cast<std::size_t>(s.count * 95 / 100),
                "bass foundation/verse/answer note-rest grammars vary across seeds" + suffix);
        std::cout << "INFO intent=" << intent << " n=" << s.count << " bpm=" << s.bpmMin << ".."
                  << s.bpmMax << " mean=" << s.bpm / std::max(1, s.count)
                  << " lead_hits=" << s.leadHits / std::max(1, s.count)
                  << " hat_hits=" << s.hatHits / std::max(1, s.count)
                  << " styles=" << s.styles.size() << " unique_lead=" << s.leadIds.size()
                  << " unique_bass_base=" << s.bassIds.size()
                  << " unique_bass_development=" << s.bassDevelopmentIds.size()
                  << " representative_seed="
                  << s.representativeSeed << std::endl;
    }
    require(summaries[0].leadHits / std::max(1, summaries[0].count) <
            0.8 * summaries[3].leadHits / std::max(1, summaries[3].count),
            "slow compositions leave materially more lead-note space than intense compositions");
    require(summaries[0].hatHits / std::max(1, summaries[0].count) <
            0.8 * summaries[3].hatHits / std::max(1, summaries[3].count),
            "slow compositions use materially fewer hat attacks than intense compositions");

    // The v28 regression checked one selected channel and one two-channel
    // Hybrid. Multiple selected channels in Single mode consumed the
    // composition RNG and were not covered. Exercise that failure directly.
    bool channelsPreserveIdentity = true;
    bool channelsPreserveIntent = true;
    bool channelsKeepTempoBand = true;
    int comparisons = 0;
    const int masks[] = {4095, 4095, (1 << 0) | (1 << 9), 1 << 9};
    const int blends[] = {0, 1, 0, 0};
    const int primaries[] = {6, 6, 1, 10};
    const float intentLo[] = {56, 84, 112, 144};
    const float intentHi[] = {84, 112, 144, 180};
    for (int i = 1; i <= 32; ++i) {
        const uint32_t seed = static_cast<uint32_t>(i) * 2654435761u;
        rb::MusicEngine base;
        base.prepare(48000.0);
        base.reset(seed);
        const int intent = audit::field(base.currentSongData(), "intent");
        for (int rendition = 0; rendition < 4; ++rendition) {
            rb::MusicEngine channel;
            channel.prepare(48000.0);
            channel.setGenreMask(masks[rendition]);
            channel.setGenreBlendMode(blends[rendition]);
            channel.setGenrePrimary(primaries[rendition]);
            channel.reset(seed);
            ++comparisons;
            channelsPreserveIdentity = channelsPreserveIdentity &&
                base.mCurrentCandidateIndex == channel.mCurrentCandidateIndex &&
                coreIdentityHash(base) == coreIdentityHash(channel);
            channelsPreserveIntent = channelsPreserveIntent &&
                audit::field(channel.currentSongData(), "intent") == intent;
            channelsKeepTempoBand = channelsKeepTempoBand && intent >= 0 && intent < 4 &&
                channel.mBpmTarget >= intentLo[intent] - 0.01f &&
                channel.mBpmTarget <= intentHi[intent] + 0.01f;
        }
    }
    require(channelsPreserveIdentity, "128 selected-channel comparisons preserve candidate, melody, bass, harmony and form");
    require(channelsPreserveIntent && channelsKeepTempoBand,
            "selected channels retain the seed's intended tempo band, including all-channel Neon Drift");

    bool exactSnapshots = true;
    bool noScheduledFx = true;
    bool variedPhrases = true;
    std::array<audit::ScheduledAudit, 4> scheduling{};
    for (int intent = 0; intent < 4; ++intent) {
        const uint32_t seed = summaries[intent].representativeSeed;
        for (int rendition = 0; rendition < 3; ++rendition) {
            const int mask = rendition == 0 ? 0 : 4095;
            const int blend = rendition == 2 ? 1 : 0;
            exactSnapshots = exactSnapshots && exactSnapshotFor(seed, mask, blend, mask ? 6 : 0);
            rb::MusicEngine events;
            events.prepare(48000.0);
            events.setGenreMask(mask);
            events.setGenreBlendMode(blend);
            events.setGenrePrimary(mask ? 6 : 0);
            events.reset(seed);
            const auto result = audit::scheduledAudit(events, 256);
            if (rendition == 0) scheduling[intent] = result;
            noScheduledFx = noScheduledFx && result.unpitchedFx == 0;
            variedPhrases = variedPhrases && result.uniquePhrases >= 240 && result.adjacentRepeats == 0;
            std::cout << "INFO phrase_audit intent=" << intent << " mask=" << mask << " blend=" << blend
                      << " seed=" << seed << " distinct=" << result.uniquePhrases << "/256"
                      << " recent32_repeats=" << result.repeatedRecentPhrases
                      << " adjacent_repeats=" << result.adjacentRepeats
                      << " special_effect_events=" << result.unpitchedFx
                      << " lead_events_per_second=" << result.leadEvents / result.seconds
                      << " hat_events_per_second=" << result.hatEvents / result.seconds
                      << " mean_lead_duration=" << result.leadDuration / std::max(1, result.leadEvents)
                      << std::endl;
        }
    }
    require(exactSnapshots, "12 snapshots across every intent and channel modes reconstruct six seconds sample exactly");
    require(noScheduledFx, "3,072 scheduled phrases contain no unpitched Zap or Noise special effects");
    require(variedPhrases, "pitched note/onset phrases remain diverse with no adjacent complete-phrase repeats in the sampled development");
    require(scheduling[0].leadEvents / scheduling[0].seconds <
            scheduling[3].leadEvents / scheduling[3].seconds,
            "slow performance schedules fewer pitched upper-voice events per second than intense performance");
    require(scheduling[0].hatEvents / scheduling[0].seconds <
            scheduling[3].hatEvents / scheduling[3].seconds,
            "slow performance schedules fewer hat events per second than intense performance");
    require(scheduling[0].leadDuration / std::max(1, scheduling[0].leadEvents) >
            scheduling[3].leadDuration / std::max(1, scheduling[3].leadEvents),
            "slow performance uses longer pitched upper-voice articulations than intense performance");

    // A positive random walk at empty drum positions used to fill almost every
    // rest over long playback. Accelerate that same mutation path without
    // rendering many hours of audio, and check the entire sampled trajectory.
    bool boundedDrumDevelopment = true;
    for (int intent = 0; intent < 4; ++intent) {
        rb::MusicEngine evolving;
        evolving.prepare(48000.0);
        evolving.reset(summaries[intent].representativeSeed);
        const double initialMass = std::accumulate(evolving.mPattern.hat.begin(),
                                                   evolving.mPattern.hat.end(), 0.0);
        const int initialHits = audit::hits(evolving.mPattern.hat);
        double minMass = initialMass, maxMass = initialMass;
        int maxHits = initialHits;
        for (int iteration = 1; iteration <= 200000; ++iteration) {
            evolving.mutateDrumsOnly();
            if ((iteration % 1000) != 0) continue;
            const double mass = std::accumulate(evolving.mPattern.hat.begin(),
                                                evolving.mPattern.hat.end(), 0.0);
            minMass = std::min(minMass, mass);
            maxMass = std::max(maxMass, mass);
            maxHits = std::max(maxHits, audit::hits(evolving.mPattern.hat));
        }
        boundedDrumDevelopment = boundedDrumDevelopment &&
            minMass >= initialMass * 0.5 && maxMass <= initialMass * 1.45 + 1.0 &&
            maxHits <= initialHits + 8;
        std::cout << "INFO drum_development intent=" << intent << " mutations=200000"
                  << " initial_hat_mass=" << initialMass << " min_mass=" << minMass
                  << " max_mass=" << maxMass << " initial_hits=" << initialHits
                  << " max_hits=" << maxHits << std::endl;
    }
    require(boundedDrumDevelopment,
            "800,000 accelerated drum mutations preserve rests and bounded rhythmic density");

    // A full render still exercises clock/transition/audio state rather than
    // only the fast symbolic schedule. The seed must remain live indefinitely.
    rb::MusicEngine live;
    live.prepare(48000.0);
    live.reset(summaries[1].representativeSeed);
    const uint32_t liveSeed = parseUnsigned(live.currentSongData(), "seed");
    const auto begin = std::chrono::steady_clock::now();
    std::vector<float> block(1024 * 2);
    bool finite = true;
    float peak = 0;
    double squares = 0;
    int64_t sampleCount = 0;
    for (int frames = 180 * 48000; frames > 0; frames -= 1024) {
        const int n = std::min(frames, 1024);
        live.render(block.data(), n, 2);
        for (int i = 0; i < n * 2; ++i) {
            const float value = block[i];
            finite = finite && std::isfinite(value);
            peak = std::max(peak, std::fabs(value));
            squares += static_cast<double>(value) * value;
            ++sampleCount;
        }
    }
    require(parseUnsigned(live.currentSongData(), "seed") == liveSeed &&
            live.currentElapsedSeconds() >= 180.0 &&
            live.mTransitionStage == rb::MusicEngine::TransitionStage::None &&
            live.currentSectionType(live.mStyleAgeSteps) != rb::MusicEngine::SectionType::Outro,
            "three-minute live playback keeps the same seed developing without an automatic ending");
    require(finite && peak <= 1.0f && squares > 1.0,
            "three-minute audio render is finite, bounded and non-silent");
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::cout << "INFO live_render_seconds=180 host_wall_seconds=" << wall << " peak=" << peak
              << " rms=" << std::sqrt(squares / std::max<int64_t>(1, sampleCount)) << std::endl;
    std::sort(tempos.begin(), tempos.end());
    std::cout << "INFO total_seed_count=" << seedCount << " slow_le90=" << slow << " fast_ge135=" << fast
              << " winning_styles=" << winningStyles.size() << " tempo_min=" << tempos.front()
              << " tempo_median=" << 0.5f * (tempos[255] + tempos[256]) << " tempo_max=" << tempos.back()
              << " max_auxiliary_to_foreground_weight=" << maxAuxiliaryRatio
              << " channel_comparisons=" << comparisons << std::endl;
    std::cout << (ok ? "PASS v29 validation" : "FAIL v29 validation") << std::endl;
    return ok ? 0 : 1;
}
