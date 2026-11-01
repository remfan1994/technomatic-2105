#include "AudioEngine.h"

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>

using namespace std::chrono_literals;

namespace {
int checks = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
    ++checks;
}

template <class T>
void complete(std::future<T>& operation, const char* message) {
    require(operation.wait_for(5s) == std::future_status::ready, message);
}

std::string value(const std::string& data, const std::string& key) {
    const std::string needle = ";" + key + "=";
    const size_t begin = data.find(needle);
    if (begin == std::string::npos) return "";
    const size_t start = begin + needle.size();
    const size_t end = data.find(';', start);
    return data.substr(start, end == std::string::npos ? end : end - start);
}

void identity(const rb::AudioEngine& engine, const std::string& seed,
              const std::string& candidate) {
    const std::string data = engine.currentSongData();
    require(value(data, "seed") == seed, "actual seed preserved");
    require(value(data, "cand") == candidate, "candidate preserved");
    require(value(data, "gmask") == "0", "retired snapshot mask is zero");
    require(value(data, "gblend") == "0", "retired snapshot blend is zero");
    require(value(data, "gprimary") == "0", "retired snapshot primary is zero");
    require(value(data, "gmode") == "0", "retired snapshot mode is zero");
    require(value(data, "engine") == "30", "actual snapshot identifies engine 30");
}

void legacyCommands(rb::AudioEngine& engine) {
    engine.setGenreMask(4095);
    engine.setGenreBlendMode(1);
    engine.setGenrePrimary(6);
    engine.setGenreStateAndRerenderCurrent(544, 1, 10);
    engine.setGenreStateAndRerenderCurrent(-1, 900, 999);
    require(engine.currentGenreMask() == 0, "legacy mask getter is neutral");
    require(engine.currentGenreBlendMode() == 0, "legacy blend getter is neutral");
    require(engine.currentGenrePrimary() == 0, "legacy primary getter is neutral");
    require(engine.currentGenreMode() == 0, "legacy mode getter is neutral");
}
}

int main() {
    rb::AudioEngine engine;
    const std::string uninitialized = engine.currentSongData();
    legacyCommands(engine);
    require(oboe::test::opens.load() == 0, "legacy commands do not open audio before Start");
    require(engine.currentSongData() == uninitialized, "legacy commands do not generate a sound");

    const std::string first = "technomatic2105-v1;seed=141414419;seconds=180;edited=0;"
            "gmask=4095;gblend=1;gprimary=6;gmode=6;cand=17;engine=29";
    require(engine.loadSongData(first), "queue legacy recipe with known seed");
    require(engine.start(), "first stream starts");
    identity(engine, "141414419", "17");
    engine.stop();
    const int opensAfterStop = oboe::test::opens.load();
    const std::string stopped = engine.currentSongData();
    legacyCommands(engine);
    require(!engine.isPlaying(), "retired channel API leaves playback stopped");
    require(oboe::test::opens.load() == opensAfterStop, "retired API opens no stream");
    require(engine.currentSongData() == stopped, "stopped legacy commands leave snapshot unchanged");

    const std::string pending = "technomatic2105-v1;seed=987654321;seconds=180;edited=0;"
            "gmask=1;gblend=0;gprimary=1;gmode=1;cand=7;engine=28";
    require(engine.loadSongData(pending), "queue stopped history load");
    legacyCommands(engine);
    require(engine.currentSongData() == stopped, "legacy commands do not consume pending history");
    require(engine.start(), "Start applies pending history");
    identity(engine, "987654321", "7");
    oboe::test::lastOpened->emit(48000);
    const double elapsed = engine.currentElapsedSeconds();
    const std::string playing = engine.currentSongData();
    legacyCommands(engine);
    require(engine.currentSongData() == playing, "playing legacy commands leave identity unchanged");
    require(engine.currentElapsedSeconds() == elapsed, "retired rendition API does not rewind playback");
    oboe::test::lastOpened->emit(64);
    require(engine.currentElapsedSeconds() > elapsed, "next callback continues without queued regeneration");
    identity(engine, "987654321", "7");

    // Start must not prepare/mutate MusicEngine until an old close completes.
    // A final data callback can still run during that close.
    auto oldStream = oboe::test::lastOpened;
    std::promise<void> closeEntered;
    auto entered = closeEntered.get_future();
    std::promise<void> releaseClose;
    auto closeRelease = releaseClose.get_future().share();
    oboe::test::closeHook = [&](oboe::AudioStream* stream) {
        closeEntered.set_value();
        closeRelease.wait();
        engine.onErrorAfterClose(stream, oboe::Result::ErrorDisconnected);
    };
    auto stopping = std::async(std::launch::async, [&] { engine.stop(); });
    complete(entered, "stop reaches close");
    require(!engine.isPlaying(), "stop marks status before close finishes");
    std::promise<void> startEntered;
    auto startEntry = startEntered.get_future();
    auto starting = std::async(std::launch::async, [&] {
        startEntered.set_value();
        return engine.start();
    });
    complete(startEntry, "concurrent Start attempted");
    require(starting.wait_for(80ms) == std::future_status::timeout,
            "Start cannot pass an in-flight close");
    auto finalCallback = std::async(std::launch::async, [&] { oldStream->emit(64); });
    complete(finalCallback, "data callback never takes control mutex");
    finalCallback.get();
    legacyCommands(engine);
    releaseClose.set_value();
    complete(stopping, "stop/error callback complete without lock inversion");
    stopping.get();
    complete(starting, "Start proceeds after close returns");
    require(starting.get(), "replacement stream starts");
    oboe::test::closeHook = {};
    identity(engine, "987654321", "7");

    auto replacement = oboe::test::lastOpened;
    oldStream->errorAfterClose();
    require(engine.isPlaying(), "stale error does not clear replacement");
    replacement->emit(64);
    identity(engine, "987654321", "7");
    replacement->errorAfterClose();
    require(!engine.isPlaying(), "active error marks stopped");
    legacyCommands(engine);
    identity(engine, "987654321", "7");

    oboe::test::startResult.store(oboe::Result::ErrorDisconnected);
    oboe::test::closeHook = [&](oboe::AudioStream* stream) {
        engine.onErrorAfterClose(stream, oboe::Result::ErrorDisconnected);
    };
    auto failedStart = std::async(std::launch::async, [&] { return engine.start(); });
    complete(failedStart, "failed-start close/error callback does not deadlock");
    require(!failedStart.get(), "failed requestStart is reported");
    require(!engine.isPlaying(), "failed start leaves stopped state");
    oboe::test::closeHook = {};
    oboe::test::startResult.store(oboe::Result::OK);
    require(engine.start(), "start recovers after failure");
    engine.stop();
    std::cout << "AudioEngine v30 control checks passed: " << checks << '\n';
    std::cout << "Retired APIs have no effect; legacy recipes retain seed/candidate with zero channel fields. "
                 "Pending load, playback continuity, close/start ordering, stale error and failure cleanup passed.\n";
    return 0;
}
