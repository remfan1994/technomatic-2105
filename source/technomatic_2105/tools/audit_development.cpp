// Independent composition audit. This observes note/rest plans through the
// engine's grammar functions, before random performance omissions and timbre.
// It is deliberately stricter than hashing the full mixed event stream.
// Header override allows the exact same observer to audit archived versions.
#ifndef TECHNOMATIC_DEVELOPMENT_AUDIT_INCLUDED
#define TECHNOMATIC_DEVELOPMENT_AUDIT_INCLUDED
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
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

namespace development_audit {
using Engine = rb::MusicEngine;
using Phrase = Engine::PhraseType;
using Section = Engine::SectionType;
using Layer = Engine::MelodyLayer;

struct IdentitySeries {
    std::map<std::string, int> counts;
    std::vector<std::string> sequence;
    int repeatedRecent = 0, adjacent = 0, run = 0, maxRun = 0;
    void add(const std::string& value) {
        ++counts[value];
        if (!sequence.empty() && sequence.back() == value) { ++adjacent; ++run; }
        else run = 1;
        maxRun = std::max(maxRun, run);
        const std::size_t from = sequence.size() > 32 ? sequence.size() - 32 : 0;
        if (std::find(sequence.begin() + from, sequence.end(), value) != sequence.end())
            ++repeatedRecent;
        sequence.push_back(value);
    }
    int top(int n) const {
        std::vector<int> values;
        for (const auto& item : counts) values.push_back(item.second);
        std::sort(values.rbegin(), values.rend());
        return std::accumulate(values.begin(), values.begin() + std::min(n, static_cast<int>(values.size())), 0);
    }
    int blockUnique(int n) const {
        std::set<std::string> blocks;
        for (std::size_t i = 0; i + n <= sequence.size(); i += n) {
            std::string block;
            for (int j = 0; j < n; ++j) block += sequence[i+j] + ';';
            blocks.insert(block);
        }
        return static_cast<int>(blocks.size());
    }
};

struct Metrics {
    int phrases = 0, hookSections = 0, hookRoles = 0, repeatRoles = 0;
    int statementRoles = 0, answerRoles = 0, exploratoryRoles = 0;
    int longestHookSection = 0, leadNotes = 0, bassNotes = 0;
    int cadenceNotes = 0, cadenceRoots = 0, largeLeaps = 0, intervals = 0;
    int supportCount = 0, primaryPhrases = 0, handedPhrases = 0;
    int outOfSlowRegister = 0, interphraseIntervals = 0, interphraseLargeLeaps = 0;
    std::array<int, 19> ownerPhrases{};
    std::array<int, 6> devicePhrases{};
    double leadDuration = 0, bassDuration = 0, gap = 0, interval = 0;
    double leadRegister = 0, interphraseInterval = 0;
    IdentitySeries lead, bass, rhythm, leadContour, foreground, foregroundRhythm;
};

int supportCount(const Engine::Composition& c) {
    const float levels[] = {c.useArp,c.useCounter,c.useStab,c.useDrone,
        c.useSpark,c.useEcho,c.useOrbit,c.useBloom,c.useGlyph,c.useSub,
        c.useSheen,c.usePluck,c.useBell,c.usePulse,c.useComet,c.useRotor};
    return static_cast<int>(std::count_if(std::begin(levels),std::end(levels),
                                         [](float x){ return x > 0.02f; }));
}

Metrics planned(Engine& engine, int phraseCount, int skipPhrases = 16) {
    Metrics m;
    m.phrases = phraseCount;
    m.supportCount = supportCount(engine.mComposition);
    const auto profile = engine.profile(engine.mPattern.style);
    const auto& composition = engine.mComposition;
    const int64_t savedStep = engine.mStepIndex;
    int hookRun = 0;
    int priorPhraseMidi = -1;
    for (int pn = skipPhrases; pn < skipPhrases + phraseCount; ++pn) {
        const int begin = pn * 16;
        const Phrase phrase = engine.currentPhraseType(begin);
        const Section section = engine.currentSectionType(begin);
        if (section == Section::Hook) { ++m.hookSections; ++hookRun; }
        else hookRun = 0;
        m.longestHookSection = std::max(m.longestHookSection, hookRun);
        if (phrase == Phrase::Hook) ++m.hookRoles;
        else if (phrase == Phrase::Repeat) ++m.repeatRoles;
        else if (phrase == Phrase::Statement) ++m.statementRoles;
        else if (phrase == Phrase::Answer) ++m.answerRoles;
        else ++m.exploratoryRoles;
        int owner = 0;
#ifndef TECHNOMATIC_AUDIT_V29
        const auto plan = engine.phrasePlanForStep(begin);
        owner = plan.instrumentSlot;
        ++m.devicePhrases[static_cast<int>(plan.device)];
#endif
        if (owner >= 0 && owner < 19) ++m.ownerPhrases[owner];
        if (owner == 0) ++m.primaryPhrases; else ++m.handedPhrases;
        std::ostringstream lead, bass, rhythm, contour, foreground, foregroundRhythm;
        int previousMidi = 0, previousPos = -1, lastDegree = 0, lastPos = -1;
        int lastPrimaryMidi = -1;
        for (int pos = 0; pos < 16; ++pos) {
            const int step = begin + pos;
            engine.mStepIndex = step; // Match the real alternating swing clock.
            const int root = engine.currentChordRoot(step);
            const int nextRoot = engine.currentChordRoot((pn + 1) * 16);
            bool rest = false; float gate = 0, dur = 0;
            int degree = 0, octave = composition.octaveBias;
#ifdef TECHNOMATIC_AUDIT_V29
            degree = engine.grammarDegree(phrase, pos, root, rest, gate, dur);
            degree = engine.applyThemeTransform(degree, step, pos, root, phrase);
            gate = engine.evolvedGate(gate, step, pos, phrase, section, Layer::Lead);
            dur = std::max(0.30f, dur * profile.noteLength);
#else
            const auto note = engine.foregroundNoteForStep(step, root);
            rest = note.rest; gate = note.gate; dur = note.duration * profile.noteLength;
            degree = note.degree; octave = note.octave;
#endif
            // This is the deterministic grammar, not a stochastic performance:
            // dynamics and tiny timing differences cannot manufacture novelty.
            if (!rest && gate > 0.04f) {
                foreground << degree - root << ',';
                foregroundRhythm << 'x';
                lastDegree = degree - root; lastPos = pos;
            } else { foreground << "r,"; foregroundRhythm << 'r'; }
            if (owner == 0 && !rest && gate > 0.04f) {
                lead << degree - root << ','; rhythm << 'x';
                const int midi = engine.scaleDegreeToMidi(degree, octave);
                if (previousPos >= 0) {
                    const int leap = std::abs(midi - previousMidi);
                    ++m.intervals; m.interval += leap;
                    if (leap > 12) ++m.largeLeaps;
                    m.gap += pos - previousPos;
                    contour << midi - previousMidi << ',';
                } else {
                    contour << "s,";
                    if (priorPhraseMidi >= 0) {
                        ++m.interphraseIntervals;
                        const int leap = std::abs(midi-priorPhraseMidi);
                        m.interphraseInterval += leap;
                        if (leap > 12) ++m.interphraseLargeLeaps;
                    }
                }
                previousPos = pos; previousMidi = midi;
                lastPrimaryMidi = midi;
                ++m.leadNotes;
                m.leadDuration += engine.stepDurationSeconds() * dur;
                m.leadRegister += midi;
                if (engine.mPattern.intent == 0 && (midi < 52 || midi > 78)) ++m.outOfSlowRegister;
            } else { lead << "r,"; rhythm << 'r'; contour << "r,"; }
            float bassGate = engine.generatedLayerGate(Layer::Bass, step, pos, phrase, section);
            const bool floor = (section == Section::Suspension || section == Section::Orbit || section == Section::Shadow) && (pos == 0 || pos == 8);
            const bool anchor = pos == 0 && !profile.ambient;
            if (floor || anchor) bassGate = std::max(bassGate, 0.30f);
#ifndef TECHNOMATIC_AUDIT_V29
            if (owner == 1) bassGate = (!rest && gate > 0.04f) ? gate : 0.0f;
            if (plan.device == Engine::MusicalDevice::Confluence && pos == plan.joint) bassGate = 1.0f;
#endif
            if (bassGate > 0.02f) {
                int b = engine.bassDegreeForStep(step,pos,root,nextRoot,phrase,section);
#ifndef TECHNOMATIC_AUDIT_V29
                if (owner == 1) b = degree;
                if (plan.device == Engine::MusicalDevice::Confluence && pos == plan.joint) b = root;
#endif
                bass << b - root << ',';
                ++m.bassNotes;
                int gap = 1;
                while (gap < 12 && composition.bassGate[(pos + gap) & 15] <= 0.02f) ++gap;
                float bassDur = std::max(0.75f,std::min(8.0f,gap * profile.noteLength));
#ifndef TECHNOMATIC_AUDIT_V29
                if (owner == 1) bassDur = dur;
#endif
                m.bassDuration += engine.stepDurationSeconds() * bassDur;
            } else bass << "r,";
        }
        if (lastPos >= 0) { ++m.cadenceNotes; if (lastDegree % engine.scaleLength() == 0) ++m.cadenceRoots; }
        if (owner == 0) {
            m.lead.add(lead.str()); m.rhythm.add(rhythm.str()); m.leadContour.add(contour.str());
            priorPhraseMidi = lastPrimaryMidi;
        }
        m.bass.add(bass.str());m.foreground.add(foreground.str());m.foregroundRhythm.add(foregroundRhythm.str());
    }
    engine.mStepIndex = savedStep;
    return m;
}

void csvHeader() {
    std::cout << "seed,intent,style,bpm,support_count,spark,bell,hook_cycle,section_length,phrases,"
        "hook_sections,hook_roles,repeat_roles,statement_roles,answer_roles,exploratory_roles,max_hook_section_run,"
        "lead_unique,lead_top1,lead_top4,lead_recent_repeats,lead_adjacent_repeats,lead_max_run,lead_blocks4_unique,"
        "rhythm_unique,contour_unique,bass_unique,bass_top1,bass_recent_repeats,bass_max_run,"
        "lead_notes_per_phrase,bass_notes_per_phrase,lead_duration_seconds,bass_duration_seconds,"
        "mean_lead_gap_steps,mean_lead_interval_semitones,large_leaps_over12,intervals,mean_lead_midi,cadence_roots,cadence_notes,"
        "primary_phrases,handed_phrases,foreground_unique,foreground_rhythm_unique,foreground_top1,foreground_top4,"
        "foreground_recent_repeats,foreground_max_run,out_of_slow_register,interphrase_mean_interval,interphrase_large_leaps,"
        "owner_lead,owner_bass,owner_harmony,owner_support,device_none,device_counterweight,device_pocket_exchange,"
        "device_held_question,device_confluence,device_open_release\n";
}

void csvRow(const Engine& e, const Metrics& m, uint32_t seed) {
    const auto& c=e.mComposition;
    std::cout << seed << ',' << e.mPattern.intent << ',' << static_cast<int>(e.mPattern.style) << ','
        << e.mBpmTarget << ',' << m.supportCount << ',' << c.useSpark << ',' << c.useBell << ','
        << c.hookCycle << ',' << c.sectionPhraseLength << ',' << m.phrases << ','
        << m.hookSections << ',' << m.hookRoles << ',' << m.repeatRoles << ',' << m.statementRoles << ','
        << m.answerRoles << ',' << m.exploratoryRoles << ',' << m.longestHookSection << ','
        << m.lead.counts.size() << ',' << m.lead.top(1) << ',' << m.lead.top(4) << ','
        << m.lead.repeatedRecent << ',' << m.lead.adjacent << ',' << m.lead.maxRun << ','
        << m.lead.blockUnique(4) << ',' << m.rhythm.counts.size() << ',' << m.leadContour.counts.size() << ','
        << m.bass.counts.size() << ',' << m.bass.top(1) << ',' << m.bass.repeatedRecent << ',' << m.bass.maxRun << ','
        << static_cast<double>(m.leadNotes)/m.phrases << ',' << static_cast<double>(m.bassNotes)/m.phrases << ','
        << m.leadDuration/std::max(1,m.leadNotes) << ',' << m.bassDuration/std::max(1,m.bassNotes) << ','
        << m.gap/std::max(1,m.intervals) << ',' << m.interval/std::max(1,m.intervals) << ','
        << m.largeLeaps << ',' << m.intervals << ',' << m.leadRegister/std::max(1,m.leadNotes) << ','
        << m.cadenceRoots << ',' << m.cadenceNotes << ',' << m.primaryPhrases << ',' << m.handedPhrases << ','
        << m.foreground.counts.size() << ',' << m.foregroundRhythm.counts.size() << ',' << m.foreground.top(1) << ','
        << m.foreground.top(4) << ',' << m.foreground.repeatedRecent << ',' << m.foreground.maxRun << ','
        << m.outOfSlowRegister << ',' << m.interphraseInterval/std::max(1,m.interphraseIntervals) << ','
        << m.interphraseLargeLeaps << ',' << m.ownerPhrases[0] << ',' << m.ownerPhrases[1] << ',' << m.ownerPhrases[2] << ','
        << std::accumulate(m.ownerPhrases.begin()+3,m.ownerPhrases.end(),0);
    for (int n : m.devicePhrases) std::cout << ',' << n;
    std::cout << '\n';
}
} // namespace development_audit

#ifndef TECHNOMATIC_DEVELOPMENT_NO_MAIN
int main(int argc, char** argv) {
    // count, phrases, mask, blend, primary, optional single seed.
    const int count=argc>1?std::max(1,std::atoi(argv[1])):256;
    const int phrases=argc>2?std::max(16,std::atoi(argv[2])):512;
    const int mask=argc>3?std::atoi(argv[3]):0;
    const int blend=argc>4?std::atoi(argv[4]):0;
    const int primary=argc>5?std::atoi(argv[5]):0;
    const uint32_t seedOnly=argc>6?static_cast<uint32_t>(std::strtoul(argv[6],nullptr,0)):0;
    development_audit::csvHeader();
    std::cout<<std::fixed<<std::setprecision(6);
    for(int i=1;i<=(seedOnly?1:count);++i) {
        const uint32_t seed=seedOnly?seedOnly:static_cast<uint32_t>(i)*2654435761u;
        rb::MusicEngine engine; engine.prepare(48000);
        engine.setGenreMask(mask); engine.setGenreBlendMode(blend);engine.setGenrePrimary(primary);
        engine.reset(seed);
        auto m=development_audit::planned(engine,phrases);
        development_audit::csvRow(engine,m,seed);
    }
}
#endif
#endif // TECHNOMATIC_DEVELOPMENT_AUDIT_INCLUDED
