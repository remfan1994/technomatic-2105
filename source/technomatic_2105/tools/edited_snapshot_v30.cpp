#include "MusicEngine.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Exercise the public save/load/render contract. These controls are inputs;
// the unprefixed values in a canonical snapshot are rounded output summaries.
namespace {
using Overrides = std::map<std::string, int>;
constexpr std::array<const char*, 40> overrideKeys{{
    "style", "tempo", "root", "scale", "density", "swing", "melody", "motif",
    "drama", "space", "roughness", "brightness", "diversity", "kick", "snare",
    "hats", "openhat", "perc", "bass", "sub", "chords", "lead", "arp", "counter",
    "stab", "texture", "drone", "spark", "fx", "echo", "orbit", "bloom", "glyph",
    "sheen", "pluck", "bell", "pulse", "grain", "comet", "rotor"
}};
int checks = 0;
int cases = 0;
int reloads = 0;
uint64_t comparedSamples = 0;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
    ++checks;
}

std::string field(const std::string& data, const std::string& key) {
    const std::string needle = ";" + key + "=";
    const size_t begin = data.find(needle);
    if (begin == std::string::npos) return "";
    const size_t start = begin + needle.size();
    const size_t end = data.find(';', start);
    return data.substr(start, end == std::string::npos ? end : end - start);
}

std::string replaceField(std::string data, const std::string& key, int value) {
    const std::string needle = ";" + key + "=";
    const size_t begin = data.find(needle);
    const std::string replacement = needle + std::to_string(value);
    if (begin == std::string::npos) return data + replacement;
    const size_t end = data.find(';', begin + needle.size());
    data.replace(begin, end == std::string::npos ? std::string::npos : end - begin,
                 replacement);
    return data;
}

std::string recipe(const Overrides& controls, uint32_t seed = 141414419u,
                   int candidate = 17) {
    std::string data = "technomatic2105-v1;seed=" + std::to_string(seed) +
        ";seconds=180;edited=1;engine=29;gmask=4095;gblend=1;gprimary=6;gmode=6;cand=" +
        std::to_string(candidate);
    for (const auto& control : controls) {
        data += ";" + control.first + "=" + std::to_string(control.second);
    }
    return data;
}

void checkSnapshot(const std::string& data, const Overrides& controls,
                   const std::string& seed, const std::string& candidate) {
    require(field(data, "seed") == seed, "actual seed survives edited load");
    require(field(data, "cand") == candidate, "candidate survives edited load");
    require(field(data, "engine") == "30", "edited snapshot identifies engine 30");
    require(field(data, "edited") == "1" && field(data, "editrecipe") == "1",
            "canonical edited snapshot carries its input-recipe marker");
    for (const auto* key : {"gmask", "gblend", "gprimary", "gmode"}) {
        require(field(data, key) == "0", std::string("retired selector is zero: ") + key);
    }
    for (const auto* key : overrideKeys) {
        const auto found = controls.find(key);
        const std::string expected = found == controls.end() ? "" : std::to_string(found->second);
        require(field(data, std::string("edit_") + key) == expected,
                std::string("raw value and field presence preserved: ") + key);
    }
    // Regeneration owns the instrument palette and phrase plan. The complete
    // snapshot comparison below also checks every per-support model field.
    for (const auto* key : {"intent", "planseed", "planbars", "recallbars", "harmonicspan",
                           "devicerotation", "phrasejoint", "leadmodel", "bassmodel",
                           "harmonymodel", "drumkit", "support0", "support15"}) {
        require(!field(data, key).empty(), std::string("generated identity is present: ") + key);
    }
}

std::vector<float> render(rb::MusicEngine& engine, int seconds) {
    const int frames = 48000 * seconds;
    std::vector<float> pcm(static_cast<size_t>(frames) * 2u);
    // Deliberately use a normal callback-sized block and a short final block.
    for (int first = 0; first < frames; first += 257) {
        const int remaining = frames - first;
        engine.render(pcm.data() + first * 2, remaining < 257 ? remaining : 257, 2);
    }
    bool finite = true;
    for (float sample : pcm) finite = finite && std::isfinite(sample);
    require(finite, "edited PCM remains finite");
    return pcm;
}

void samePcm(const std::vector<float>& expected, const std::vector<float>& actual,
             const std::string& message) {
    require(expected.size() == actual.size() &&
            std::memcmp(expected.data(), actual.data(), expected.size() * sizeof(float)) == 0,
            message);
    comparedSamples += expected.size();
}

std::string replayCase(const std::string& name, const std::string& input,
                       const Overrides& controls, int seconds = 1) {
    rb::MusicEngine original;
    original.prepare(48000.0);
    require(original.loadSongData(input), name + ": input loads");
    const std::string initial = original.currentSongData();
    const std::string seed = field(initial, "seed");
    const std::string candidate = field(initial, "cand");
    checkSnapshot(initial, controls, seed, candidate);
    const auto expectedPcm = render(original, seconds);
    std::string saved = initial;
    // Reuse this instance to exercise replacement of earlier override state.
    rb::MusicEngine restored;
    restored.prepare(48000.0);
    for (int pass = 1; pass <= 3; ++pass) {
        require(restored.loadSongData(saved), name + ": saved recipe reloads");
        const std::string next = restored.currentSongData();
        checkSnapshot(next, controls, seed, candidate);
        require(next == initial, name + ": complete snapshot stable after reload " + std::to_string(pass));
        samePcm(expectedPcm, render(restored, seconds),
                name + ": sample-exact PCM after reload " + std::to_string(pass));
        saved = next;
        ++reloads;
    }
    ++cases;
    return initial;
}

void summaryEquals(const std::string& data, const char* key, int expected) {
    require(field(data, key) == std::to_string(expected),
            std::string("typed override has expected effective summary: ") + key);
}
}

int main() {
    const Overrides first{{"root", 43}, {"scale", 2}, {"tempo", 88}, {"brightness", 80},
        {"lead", 70}, {"bass", 65}, {"chords", 60}, {"arp", 90}, {"counter", 75},
        {"motif", 70}, {"drama", 65}};
    const std::string firstSnapshot = replayCase("original drift reproduction", recipe(first), first, 12);
    for (const auto& expected : Overrides{{"root", 43}, {"scale", 2}, {"tempo", 88},
             {"brightness", 83}, {"lead", 85}, {"bass", 81}, {"chords", 71},
             {"arp", 92}, {"counter", 73}, {"motif", 70}, {"drama", 65}}) {
        summaryEquals(firstSnapshot, expected.first.c_str(), expected.second);
    }

    const Overrides second{{"root", 36}, {"scale", 6}, {"tempo", 104}, {"lead", 0},
        {"bass", 100}, {"brightness", 25}, {"diversity", 90}};
    const std::string secondSnapshot = replayCase("muted lead and full bass",
        recipe(second, 987654321u, 7), second, 12);
    summaryEquals(secondSnapshot, "lead", 0);
    summaryEquals(secondSnapshot, "bass", 100); // Effective gain > 1 is a saturated display summary.
    summaryEquals(secondSnapshot, "brightness", 42);

    replayCase("no overrides", recipe({}), {});
    size_t index = 0;
    for (const auto* key : overrideKeys) {
        int input = 11 + static_cast<int>((index * 13) % 83);
        const std::string name(key);
        if (name == "style") input = 31;
        if (name == "tempo") input = 88;
        if (name == "root") input = 43;
        if (name == "scale") input = 8;
        const Overrides one{{name, input}};
        replayCase("single " + name, recipe(one, 111111111u + static_cast<uint32_t>(index),
                   static_cast<int>(index % 48)), one);
        ++index;
    }

    // Zero is an explicit control value, not an absent field. Raw values remain
    // exact while typed controls clamp their effective operating ranges.
    for (int input : {0, -500, 500}) {
        Overrides all;
        for (const auto* key : overrideKeys) all[key] = input;
        const auto snapshot = replayCase("all controls " + std::to_string(input), recipe(all), all, 2);
        summaryEquals(snapshot, "style", input > 0 ? 31 : 0);
        summaryEquals(snapshot, "tempo", input > 0 ? 220 : 40);
        summaryEquals(snapshot, "root", input > 0 ? 60 : 24);
        summaryEquals(snapshot, "scale", input > 0 ? 8 : 0);
        for (const auto* key : {"density", "swing", "melody", "motif", "drama", "space", "roughness"}) {
            summaryEquals(snapshot, key, input > 0 ? 100 : 0);
        }
        for (const auto* key : {"kick", "snare", "hats", "bass", "chords", "lead"}) {
            summaryEquals(snapshot, key, input > 0 ? 100 : 0);
        }
        for (const auto* key : {"texture", "fx", "grain"}) summaryEquals(snapshot, key, 0);
    }

    // An old recipe with extended controls and no gmask/edited marker still
    // enters the historical override path, then gains the canonical marker.
    const Overrides inferred{{"tempo", 101}, {"root", 38}, {"brightness", 72}};
    replayCase("legacy inferred edited recipe",
        "technomatic2105-v1;seed=314159265;seconds=180;cand=3;tempo=101;root=38;brightness=72",
        inferred, 2);

    // Rounded display fields can change or be removed by an external reader.
    // A canonical recipe must use only edit_* fields, including their absence.
    std::string misleading = firstSnapshot;
    for (const auto* key : overrideKeys) misleading = replaceField(misleading, key, 0);
    misleading = replaceField(misleading, "leadmodel", 0);
    misleading = replaceField(misleading, "planbars", 999);
    const auto canonical = replayCase("canonical summary fields are not controls", misleading, first, 3);
    require(canonical == firstSnapshot, "canonical marker ignores rounded summaries and reconstructs its palette/plan");

    // Obsolete channel fields never select a different edited rendition.
    rb::MusicEngine reference;
    reference.prepare(48000.0);
    require(reference.loadSongData(recipe(first)), "channel-neutral reference loads");
    const auto referencePcm = render(reference, 3);
    for (const auto& values : std::array<std::array<int, 4>, 4>{{
             {{0, 0, 0, 0}}, {{1, 0, 1, 1}}, {{544, 1, 10, 10}}, {{-1, 900, 999, -80}}
         }}) {
        std::string changed = recipe(first);
        const std::array<const char*, 4> keys{{"gmask", "gblend", "gprimary", "gmode"}};
        for (size_t i = 0; i < keys.size(); ++i) changed = replaceField(changed, keys[i], values[i]);
        rb::MusicEngine neutral;
        neutral.prepare(48000.0);
        require(neutral.loadSongData(changed), "edited legacy selector variant loads");
        require(neutral.currentSongData() == firstSnapshot, "all legacy selector values produce the same edited recipe");
        samePcm(referencePcm, render(neutral, 3), "all legacy selector values produce sample-exact edited PCM");
    }

    // A later unedited or sparse edited load must not inherit earlier controls.
    rb::MusicEngine reused;
    reused.prepare(48000.0);
    require(reused.loadSongData(firstSnapshot), "reused engine accepts edited state");
    require(reused.loadSongData("technomatic2105-v1;seed=987654321;cand=7;edited=0;gmask=0"),
            "unedited recipe replaces edited state");
    const auto unedited = reused.currentSongData();
    require(field(unedited, "edited") == "0" && field(unedited, "editrecipe").empty(),
            "unedited replacement clears the edit marker");
    for (const auto* key : overrideKeys) {
        require(field(unedited, std::string("edit_") + key).empty(),
                std::string("unedited replacement clears raw input: ") + key);
    }
    require(reused.loadSongData(recipe({})), "empty edited recipe replaces previous controls");
    checkSnapshot(reused.currentSongData(), {}, "141414419", "17");

    std::cout << "Edited snapshot v30 checks passed: " << checks << '\n';
    std::cout << "Replay cases: " << cases << "; successive save/load passes per case: 3; total reloads: "
              << reloads << '\n';
    std::cout << "All 40 override fields covered individually and at zero/negative/high input; "
                 "absent fields and raw typed values are preserved.\n";
    std::cout << "Exact stereo float samples compared: " << comparedSamples << '\n';
    std::cout << "Original two drift reproductions: 12 seconds at 48 kHz on each pass, zero differing samples.\n";
    std::cout << "Full snapshot, generated palette/plan fields, legacy selector neutrality, "
                 "summary/input separation and replacement-state cleanup passed.\n";
}
