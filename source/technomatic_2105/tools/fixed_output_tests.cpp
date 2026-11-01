// Focused regression for the fixed full-scale output, including v30.2's
// increased calibration. Musical identity and measured loudness are checked
// separately against the delivered v30.1 baseline.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <set>
#include <string>
#include <type_traits>
#include <vector>
#define private public
#include "../app/src/main/cpp/MusicEngine.h"
#undef private
#include "../app/src/main/cpp/OutputStage.h"

namespace {
using Engine = rb::MusicEngine;
using Stage = rb::OutputStage;
constexpr int rate = 48000;
int checks = 0, failures = 0;
void require(bool okay, const std::string& label) {
    ++checks; if (!okay) ++failures;
    std::cout << (okay ? "PASS " : "FAIL ") << label << '\n';
}
std::vector<float> render(Engine& e, int frames, int block) {
    std::vector<float> output(static_cast<size_t>(frames * 2));
    for (int cursor = 0; cursor < frames;) {
        const int n = std::min(block, frames - cursor);
        e.render(output.data() + cursor * 2, n, 2); cursor += n;
    }
    return output;
}
bool bounded(const std::vector<float>& pcm) {
    return std::all_of(pcm.begin(), pcm.end(), [](float x) { return std::isfinite(x) && std::fabs(x) <= 1.0f; });
}
bool nonzero(const std::vector<float>& pcm) {
    return std::any_of(pcm.begin(), pcm.end(), [](float x) { return x != 0; });
}
void mute(Engine& e) {
    auto& c = e.mComposition;
    c.useKick = c.useSnare = c.useHat = c.useOpenHat = c.usePerc = 0;
    c.useBass = c.useLead = c.useChord = 0;
    c.useArp = c.useCounter = c.useStab = c.useDrone = c.useSpark = c.useFx = c.useEcho = 0;
    c.useOrbit = c.useBloom = c.useGlyph = c.useSub = c.useSheen = c.usePluck = 0;
    c.useBell = c.usePulse = c.useGrain = c.useComet = c.useRotor = c.useTexture = 0;
    e.clearVoicesAndEvents();
}
std::vector<int16_t> readPcm(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto bytes = input.tellg();
    if (bytes < 0 || bytes % 2) return {};
    input.seekg(0);
    std::vector<int16_t> result(static_cast<size_t>(bytes) / 2);
    input.read(reinterpret_cast<char*>(result.data()), bytes);
    return input ? result : std::vector<int16_t>{};
}
double toneEnergy(Engine& e, float amplitude, int frames, int inspectLast) {
    double energy = 0;
    for (int i = 0; i < frames; ++i) {
        float left = amplitude * std::sin(static_cast<float>(i % 48) * 6.2831853071795864769f / 48);
        float right = left * .6f;
        e.applyMaster(left, right);
        if (i >= frames - inspectLast) energy += static_cast<double>(left) * left;
    }
    return energy / inspectLast;
}
bool phraseAmplitudeCheck(uint32_t seed, bool conclusive, int& observed, bool& covered) {
    Engine e; e.prepare(rate); e.reset(seed);
    e.mExportSinglePieceMode = true; e.mComposition.pieceSteps = 128 * 16;
    e.mComposition.conclusiveOutro = conclusive;
    // Hold per-note accent and the seed's role/instrument parameters fixed.
    // The actual scheduler still selects roles, gates, entrances and exits.
    std::set<float> kickAmplitudes, leadAmplitudes;
    std::set<Engine::SectionType> sections;
    bool release = false;
    for (int step = 0; step < 128 * 16; ++step) {
        e.clearVoicesAndEvents(); e.mPattern.accent.fill(1.0f); e.mSilentSteps = 0;
        const auto plan = e.phrasePlanForStep(step);
        e.onStep();
        for (const auto& event : e.mEvents) {
            if (!event.active || event.amp <= 0) continue;
            const bool kick = event.kind == Engine::EventKind::Drum && event.drumType == Engine::DrumType::Kick;
            const bool lead = event.kind == Engine::EventKind::Lead && event.foreground;
            if (!kick && !lead) continue;
            ++observed;
            if (kick) kickAmplitudes.insert(event.amp);
            if (lead) leadAmplitudes.insert(event.amp);
            sections.insert(plan.section);
            release = release || plan.device == Engine::MusicalDevice::OpenRelease;
        }
    }
    covered = covered || (sections.count(Engine::SectionType::Intro) && sections.count(Engine::SectionType::Hook) &&
        sections.count(Engine::SectionType::Shadow) && sections.count(Engine::SectionType::Outro) && release);
    return kickAmplitudes.size() == 1 && leadAmplitudes.size() == 1;
}
}

int main(int argc, char** argv) {
    const std::string directory = argc > 1 ? argv[1] : "/tmp";
    require(std::is_empty<Stage>::value, "output transfer has no persistent state");
    require(Stage::kCeiling == 1.0f, "output permits 100 percent digital full scale");
    require(Stage::kMakeupGain >= 1.0f, "fixed output gain does not attenuate the app master");
    require(Stage::processSample(0) == 0, "zero input remains exact silence");
    bool linear = true, odd = true, monotonic = true, finite = true;
    float previous = -1;
    for (int i = -100000; i <= 100000; ++i) {
        const float x = i * .0001f;
        const float y = Stage::processSample(x);
        finite = finite && std::isfinite(y) && std::fabs(y) <= 1.0f;
        odd = odd && y == -Stage::processSample(-x);
        monotonic = monotonic && y >= previous; previous = y;
        if (std::fabs(x * Stage::kMakeupGain) <= Stage::kKnee)
            linear = linear && y == x * Stage::kMakeupGain;
    }
    require(linear, "small signals retain exact fixed linear gain below the peak knee");
    require(odd && monotonic, "peak curve is symmetric and monotonic");
    require(finite, "200001 tested inputs stay finite and inside full scale");
    const float boundary = Stage::kKnee / Stage::kMakeupGain;
    constexpr float epsilon = .0001f;
    const float leftSlope = (Stage::processSample(boundary) - Stage::processSample(boundary - epsilon)) / epsilon;
    const float rightSlope = (Stage::processSample(boundary + epsilon) - Stage::processSample(boundary)) / epsilon;
    require(std::fabs(leftSlope - rightSlope) < .002f && std::fabs(rightSlope - Stage::kMakeupGain) < .002f,
            "curve joins the linear region with a continuous first derivative");
    bool historyFree = true, independent = true;
    for (float probe : {-1.2f, -.3f, -.0001f, 0.0f, .07f, .8f, 2.0f}) {
        const float expected = Stage::processSample(probe);
        for (int i = 0; i < 1000; ++i) {
            const float ignored = Stage::processSample(i % 2 ? 8.0f : -8.0f); (void)ignored;
        }
        historyFree = historyFree && Stage::processSample(probe) == expected;
        float left = probe, right = 100;
        Stage::process(left, right); independent = independent && left == expected;
    }
    require(historyFree, "loud earlier samples never change the gain of a later sample");
    require(independent, "an opposite-channel peak does not impose a changing gain on this channel");
    require(std::isfinite(Stage::processSample(std::numeric_limits<float>::infinity())) &&
            Stage::processSample(std::numeric_limits<float>::quiet_NaN()) == 0,
            "nonfinite injected stage inputs cannot escape as nonfinite output");

    Engine follower; follower.prepare(rate); follower.reset(1013904226u);
    const double quietBefore = toneEnergy(follower, .03f, rate / 2, rate / 10);
    toneEnergy(follower, .8f, rate * 3, rate / 10);
    const double quietAfter = toneEnergy(follower, .03f, rate / 2, rate / 10);
    require(std::fabs(quietAfter / quietBefore - 1) < .00001,
            "quiet output recovers only the short DC-filter tail, with no slow gain recovery");

    const uint32_t seeds[] = {1013904226u, 2654435761u, 2027808452u, 3668339987u};
    bool phraseAmplitudes = true, phraseCoverage = false; int observedAmplitudes = 0;
    for (uint32_t seed : seeds) {
        const std::string suffix = " seed=" + std::to_string(seed);
        Engine original; original.prepare(rate); original.reset(seed);
        const std::string recipe = original.currentSongData();
        require(recipe.find(";engine=30;") != std::string::npos && recipe.find(";outputrev=2;") != std::string::npos,
                "recipe preserves composition engine 30 and records output revision 2" + suffix);
        const auto reference = render(original, rate * 6, 256);
        require(bounded(reference) && nonzero(reference), "six-second output is finite, bounded and non-silent" + suffix);
        bool blockExact = true;
        for (int block : {1, 257, 1024, 4093}) {
            Engine e; e.prepare(rate); e.reset(seed);
            blockExact = blockExact && render(e, rate * 6, block) == reference;
        }
        require(blockExact, "render block sizes 1,257,1024,4093 preserve every float sample" + suffix);
        original.reset(seed);
        require(render(original, rate * 6, 509) == reference, "reset after playback restores exact output" + suffix);
        Engine loaded; loaded.prepare(rate);
        require(loaded.loadSongData(recipe) && render(loaded, rate * 6, 997) == reference,
                "saved recipe restores exact output without inherited gain state" + suffix);
        std::string legacyRecipe = recipe;
        legacyRecipe.replace(legacyRecipe.find("outputrev=2"), std::strlen("outputrev=2"), "outputrev=1");
        Engine legacy; legacy.prepare(rate);
        require(legacy.loadSongData(legacyRecipe) &&
                legacy.currentSongData().find(";outputrev=2;") != std::string::npos &&
                render(legacy, rate * 6, 997) == reference,
                "v30.1 recipe uses current louder output with identical music and replay" + suffix);

        Engine next; next.prepare(rate); next.reset(seed); render(next, rate, 251);
        const auto oldRecipe = next.currentSongData(); next.next();
        const auto nextRecipe = next.currentSongData();
        Engine restoredNext; restoredNext.prepare(rate);
        const bool nextIdentity = nextRecipe != oldRecipe && next.currentElapsedSeconds() == 0;
        require(nextIdentity && restoredNext.loadSongData(nextRecipe) &&
                render(next, rate * 2, 256) == render(restoredNext, rate * 2, 997),
                "legacy next switches immediately to exact fresh output with no master fade" + suffix);

        Engine appNext; appNext.prepare(rate); appNext.reset(seed); render(appNext, rate, 251);
        appNext.forceNewPiece(); const auto appRecipe = appNext.currentSongData();
        Engine restoredAppNext; restoredAppNext.prepare(rate);
        require(appNext.currentElapsedSeconds() == 0 && restoredAppNext.loadSongData(appRecipe) &&
                render(appNext, rate * 2, 256) == render(restoredAppNext, rate * 2, 997),
                "app next starts immediately with sample-exact fresh-recipe output" + suffix);

        Engine silent; silent.prepare(rate); silent.reset(seed); mute(silent);
        const auto silence = render(silent, rate * 3, 127);
        require(std::all_of(silence.begin(), silence.end(), [](float x) { return x == 0; }),
                "fully muted voices remain exact silence without gain pumping" + suffix);

        const std::string path = directory + "/fixed-output-" + std::to_string(seed) + ".pcm";
        const bool exported = Engine::exportPcm16RangeFile(recipe, 3, 12, path);
        const auto pcm = readPcm(path);
        require(exported && pcm.size() == 9u * rate * 2,
                "range export writes the exact requested stereo frame count" + suffix);
        Engine finiteEngine; finiteEngine.prepare(rate); finiteEngine.loadSongData(recipe);
        finiteEngine.mExportSinglePieceMode = true; finiteEngine.mExportStopSamples = 12LL * rate;
        finiteEngine.mComposition.pieceSteps = finiteEngine.pieceStepsFromSeconds(12, finiteEngine.mBpmTarget);
        const auto finiteRender = render(finiteEngine, rate * 12, 257);
        bool exportExact = pcm.size() == 9u * rate * 2;
        for (size_t i = 0; exportExact && i < pcm.size(); ++i) {
            const float sample = finiteRender[3u * rate * 2 + i];
            exportExact = pcm[i] == static_cast<int16_t>(std::lrint(sample * 32767.0f));
        }
        require(exportExact, "every exported sample including the tail matches finite render at fixed gain" + suffix);
        require(Engine::exportPcm16RangeFile(recipe, 3, 12, path) && readPcm(path) == pcm,
                "repeated export is byte-identical" + suffix);
        std::remove(path.c_str());
        for (bool conclusive : {false, true})
            phraseAmplitudes = phraseAmplitudeCheck(seed, conclusive, observedAmplitudes, phraseCoverage) && phraseAmplitudes;
    }
    require(phraseAmplitudes && phraseCoverage,
            "fixed-accent kick/foreground-lead amplitudes stay constant across Intro,Hook,Shadow,OpenRelease and both Outro modes");
    std::cout << "INFO controlled_phrase_amplitude_events=" << observedAmplitudes << '\n';
    // Device sample rates affect synthesis timing, but cannot create output
    // gain state or exceed the fixed sample bound.
    bool rates = true;
    for (int sampleRate : {8000, 44100, 96000}) {
        Engine e; e.prepare(sampleRate); e.reset(3668339987u);
        const auto pcm = render(e, sampleRate * 2, 257);
        rates = rates && bounded(pcm) && nonzero(pcm);
    }
    require(rates, "8000,44100,96000Hz host renders remain finite and bounded");
    std::cout << "INFO fixed_gain=" << Stage::kMakeupGain << " knee=" << Stage::kKnee << " ceiling=" << Stage::kCeiling << '\n';
    std::cout << (failures ? "FAIL" : "PASS") << " fixed-output checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
