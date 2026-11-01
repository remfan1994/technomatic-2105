// Structural audit, deliberately separate from subjective listening.
// See ../docs/V29_VALIDATION.md for commands and interpretation.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef TECHNOMATIC_ENGINE_HEADER
#define TECHNOMATIC_ENGINE_HEADER "../app/src/main/cpp/MusicEngine.h"
#endif
#define private public
#include TECHNOMATIC_ENGINE_HEADER
#undef private

namespace audit {

int field(const std::string& data, const char* name, int fallback = -1) {
    const std::string key = std::string(";") + name + "=";
    const std::size_t at = data.find(key);
    if (at == std::string::npos) return fallback;
    try { return std::stoi(data.substr(at + key.size())); }
    catch (...) { return fallback; }
}

template<class A> int hits(const A& values) {
    return static_cast<int>(std::count_if(values.begin(), values.end(),
                                        [](float value) { return value > 0.20f; }));
}

float foreground(const rb::MusicEngine::Composition& c) {
    return c.useBass + c.useLead + c.useChord;
}

float auxiliary(const rb::MusicEngine::Composition& c) {
    return c.useTexture + c.useArp + c.useCounter + c.useStab + c.useDrone +
           c.useSpark + c.useFx + c.useEcho + c.useOrbit + c.useBloom + c.useGlyph +
           c.useSub + c.useSheen + c.usePluck + c.useBell + c.usePulse + c.useGrain +
           c.useComet + c.useRotor;
}

// Exact symbolic representation: integer MIDI pitch + onset rounded to 1/8
// of a sixteenth-note step. It excludes amplitude, timbre, pan, random decimal
// timing and unpitched percussion, so such changes cannot fake novelty.
// A pad contributes its root and generated voicing degrees explicitly.
std::string pitchedStep(const rb::MusicEngine& engine) {
    std::vector<std::array<int, 3>> notes;
    const double stepSamples = engine.stepDurationSamples();
    for (const auto& event : engine.mEvents) {
        if (!event.active || event.kind == rb::MusicEngine::EventKind::Drum) continue;
        const int onset = static_cast<int>(std::lround(event.samples * 8.0 / stepSamples));
        if (event.kind == rb::MusicEngine::EventKind::Pad) {
            for (int i = 0; i < engine.mComposition.padVoiceCount; ++i) {
                const int degree = event.degree + engine.mComposition.padIntervals[i];
                notes.push_back({2, onset, engine.scaleDegreeToMidi(degree, i >= 3 ? 2 : 1)});
            }
        } else {
            const int midi = static_cast<int>(std::lround(69.0 + 12.0 *
                std::log2(std::max(1.0f, event.freq) / 440.0)));
            const int kind = event.kind == rb::MusicEngine::EventKind::Bass ? 0 : 1;
            notes.push_back({kind, onset, midi});
        }
    }
    std::sort(notes.begin(), notes.end());
    std::ostringstream out;
    for (const auto& n : notes) out << n[0] << ':' << n[1] << ':' << n[2] << ',';
    return out.str();
}

struct ScheduledAudit {
    int phrases = 0;
    int uniquePhrases = 0;
    int repeatedRecentPhrases = 0;
    int adjacentRepeats = 0;
    int unpitchedFx = 0;
    int leadEvents = 0;
    int bassEvents = 0;
    int hatEvents = 0;
    int kickEvents = 0;
    int maxEventsAtStep = 0;
    double seconds = 0;
    double leadDuration = 0;
    double bassDuration = 0;
};

ScheduledAudit scheduledAudit(rb::MusicEngine& engine, int phraseCount) {
    ScheduledAudit result;
    result.phrases = phraseCount;
    constexpr int skipPhrases = 16;
    std::set<std::string> all;
    std::vector<std::string> recent;
    std::string phrase;
    for (int step = 0; step < (skipPhrases + phraseCount) * 16; ++step) {
        for (auto& event : engine.mEvents) event.active = false;
        engine.onStep();
        if (step < skipPhrases * 16) continue;
        result.seconds += engine.stepDurationSeconds();
        int events = 0;
        for (const auto& event : engine.mEvents) {
            if (!event.active) continue;
            ++events;
            switch (event.kind) {
                case rb::MusicEngine::EventKind::Lead:
                    ++result.leadEvents;
                    result.leadDuration += event.dur;
                    break;
                case rb::MusicEngine::EventKind::Bass:
                    ++result.bassEvents;
                    result.bassDuration += event.dur;
                    break;
                case rb::MusicEngine::EventKind::Drum:
                    if (event.drumType == rb::MusicEngine::DrumType::Zap ||
                        event.drumType == rb::MusicEngine::DrumType::Noise) ++result.unpitchedFx;
                    if (event.drumType == rb::MusicEngine::DrumType::HatClosed ||
                        event.drumType == rb::MusicEngine::DrumType::HatOpen) ++result.hatEvents;
                    if (event.drumType == rb::MusicEngine::DrumType::Kick) ++result.kickEvents;
                    break;
                default: break;
            }
        }
        result.maxEventsAtStep = std::max(result.maxEventsAtStep, events);
        phrase += pitchedStep(engine) + '|';
        if ((step & 15) == 15) {
            all.insert(phrase);
            if (!recent.empty() && phrase == recent.back()) ++result.adjacentRepeats;
            if (std::find(recent.begin(), recent.end(), phrase) != recent.end()) {
                ++result.repeatedRecentPhrases;
            }
            recent.push_back(phrase);
            if (recent.size() > 32) recent.erase(recent.begin());
            phrase.clear();
        }
    }
    result.uniquePhrases = static_cast<int>(all.size());
    return result;
}

} // namespace audit

#ifndef TECHNOMATIC_AUDIT_NO_MAIN
int main(int argc, char** argv) {
    // Usage: audit_music [count=512] [mask=0] [blend=0] [primary=0]
    //                    [scheduled_phrases=0] [single_seed=0]
    // Scheduled audit is a structural simulation with events drained after
    // every step; it is not an audio render or a concurrency/loudness test.
    const int count = argc > 1 ? std::max(1, std::min(4096, std::atoi(argv[1]))) : 512;
    const int mask = argc > 2 ? std::max(0, std::min(4095, std::atoi(argv[2]))) : 0;
    const int blend = argc > 3 ? std::max(0, std::min(1, std::atoi(argv[3]))) : 0;
    const int primary = argc > 4 ? std::max(0, std::min(12, std::atoi(argv[4]))) : 0;
    const int phrases = argc > 5 ? std::max(0, std::min(4096, std::atoi(argv[5]))) : 0;
    const uint32_t singleSeed = argc > 6 ? static_cast<uint32_t>(std::strtoul(argv[6], nullptr, 0)) : 0;
    std::cout << "seed,mask,blend,primary,candidate,style,intent,bpm,density,roughness,texture,space,"
                 "lead_hits,bass_hits,hat_hits,foreground,auxiliary,texture_lane,fx_lane,grain_lane,"
                 "phrases,unique_phrases,repeated_recent,adjacent_repeats,unpitched_fx,"
                 "scheduled_seconds,lead_events,bass_events,hat_events,kick_events,"
                 "mean_lead_duration,mean_bass_duration,max_events_at_step\n";
    std::cout << std::fixed << std::setprecision(6);
    for (int i = 1; i <= (singleSeed ? 1 : count); ++i) {
        const uint32_t seed = singleSeed ? singleSeed : static_cast<uint32_t>(i) * 2654435761u;
        rb::MusicEngine engine;
        engine.prepare(48000.0);
        engine.setGenreMask(mask);
        engine.setGenreBlendMode(blend);
        engine.setGenrePrimary(primary);
        engine.reset(seed);
        const auto p = engine.mPattern;
        const auto c = engine.mComposition;
        const int candidate = engine.mCurrentCandidateIndex;
        const float bpm = engine.mBpmTarget;
        const int intent = audit::field(engine.currentSongData(), "intent");
        const auto a = phrases ? audit::scheduledAudit(engine, phrases) : audit::ScheduledAudit{};
        std::cout << seed << ',' << mask << ',' << blend << ',' << primary << ',' << candidate << ','
                  << static_cast<int>(p.style) << ',' << intent << ',' << bpm << ',' << p.density << ','
                  << p.roughness << ',' << p.texture << ',' << p.space << ',' << audit::hits(c.gateA)
                  << ',' << audit::hits(c.bassGate) << ',' << audit::hits(p.hat) << ','
                  << audit::foreground(c) << ',' << audit::auxiliary(c) << ',' << c.useTexture << ','
                  << c.useFx << ',' << c.useGrain << ',' << a.phrases << ',' << a.uniquePhrases << ','
                  << a.repeatedRecentPhrases << ',' << a.adjacentRepeats << ',' << a.unpitchedFx << ','
                  << a.seconds << ',' << a.leadEvents << ',' << a.bassEvents << ',' << a.hatEvents << ','
                  << a.kickEvents << ',' << a.leadDuration / std::max(1, a.leadEvents) << ','
                  << a.bassDuration / std::max(1, a.bassEvents) << ',' << a.maxEventsAtStep << '\n';
    }
}
#endif
