// Historical v29 regression: run against v29 AudioEngine/MusicEngine sources.
// The current runner uses audio_controls_v30.cpp after channel retirement.
#include "AudioEngine.h"

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>
#include <thread>

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
              const std::string& candidate, const std::string& mask,
              const std::string& primary) {
    const std::string data = engine.currentSongData();
    require(value(data, "seed") == seed, "actual seed preserved");
    require(value(data, "cand") == candidate, "candidate preserved");
    require(value(data, "gmask") == mask, "actual channel mask updated");
    require(value(data, "gprimary") == primary, "actual primary updated");
    require(value(data, "engine") == "29", "versioned actual snapshot");
}
}

int main() {
    rb::AudioEngine engine;
    const std::string uninitialized = engine.currentSongData();
    engine.setGenreStateAndRerenderCurrent(4095, 1, 6);
    require(oboe::test::opens.load() == 0, "initial selection does not open audio");
    require(engine.currentSongData() == uninitialized, "uninitialized selection waits for first Start");
    require(engine.currentGenreMask() == 4095, "initial selection retained");

    const std::string first = "technomatic2105-v1;seed=141414419;seconds=180;edited=0;"
            "gmask=4095;gblend=1;gprimary=6;gmode=6;cand=17;engine=29";
    require(engine.loadSongData(first), "queue known seed");
    require(engine.start(), "first stream starts");
    identity(engine, "141414419", "17", "4095", "6");
    engine.stop();
    const int opensAfterStop = oboe::test::opens.load();
    engine.setGenreStateAndRerenderCurrent(544, 1, 10);
    require(!engine.isPlaying(), "stopped recolor does not start playback");
    require(oboe::test::opens.load() == opensAfterStop, "stopped recolor opens no stream");
    identity(engine, "141414419", "17", "544", "10");

    // A pending history/seed load is consumed before a stopped recolor. It must
    // not be reapplied over the selected rendition at the subsequent Start.
    const std::string pending = "technomatic2105-v1;seed=987654321;seconds=180;edited=0;"
            "gmask=1;gblend=0;gprimary=1;gmode=1;cand=7;engine=29";
    require(engine.loadSongData(pending), "queue stopped history load");
    engine.setGenreStateAndRerenderCurrent(4095, 1, 6);
    identity(engine, "987654321", "7", "4095", "6");
    require(oboe::test::opens.load() == opensAfterStop, "pending load/recolor remain stopped");
    require(engine.start(), "resume recolored pending sound");
    oboe::test::lastOpened->emit(64);
    identity(engine, "987654321", "7", "4095", "6");

    // Playing changes remain callback-owned, not synchronous music mutations.
    engine.setGenreStateAndRerenderCurrent(1024, 0, 11);
    require(value(engine.currentSongData(), "gmask") == "4095", "playing recolor waits for callback");
    oboe::test::lastOpened->emit(64);
    identity(engine, "987654321", "7", "1024", "11");

    // Hold stream.close() open while one final data callback executes. A
    // simultaneous stopped recolor must wait for close, despite isPlaying=false.
    std::shared_ptr<oboe::AudioStream> oldStream = oboe::test::lastOpened;
    std::promise<void> closeEntered;
    std::future<void> entered = closeEntered.get_future();
    std::promise<void> releaseClose;
    std::shared_future<void> closeRelease = releaseClose.get_future().share();
    oboe::test::closeHook = [&](oboe::AudioStream* stream) {
        closeEntered.set_value();
        closeRelease.wait();
        // A close callback must not deadlock on mLock or mControlLock.
        engine.onErrorAfterClose(stream, oboe::Result::ErrorDisconnected);
    };
    auto stopping = std::async(std::launch::async, [&] { engine.stop(); });
    complete(entered, "stop reaches close");
    require(!engine.isPlaying(), "stop marks status stopped before close finishes");
    std::promise<void> recolorEntered;
    auto recolorEntry = recolorEntered.get_future();
    auto recoloring = std::async(std::launch::async, [&] {
        recolorEntered.set_value();
        engine.setGenreStateAndRerenderCurrent(4095, 1, 6);
    });
    complete(recolorEntry, "concurrent recolor attempted");
    require(recoloring.wait_for(80ms) == std::future_status::timeout,
            "recolor cannot pass an in-flight close");
    require(value(engine.currentSongData(), "gmask") == "1024", "old snapshot remains until quiescent");
    auto finalCallback = std::async(std::launch::async, [&] { oldStream->emit(64); });
    complete(finalCallback, "data callback never takes control mutex");
    finalCallback.get();
    releaseClose.set_value();
    complete(stopping, "stop/error callback complete without lock inversion");
    stopping.get();
    complete(recoloring, "recolor proceeds after close returns");
    recoloring.get();
    identity(engine, "987654321", "7", "4095", "6");
    oboe::test::closeHook = {};

    // A delayed old-stream error cannot reset a replacement stream.
    require(engine.start(), "replacement stream starts");
    auto replacement = oboe::test::lastOpened;
    oldStream->errorAfterClose();
    require(engine.isPlaying(), "stale error does not clear replacement");
    replacement->emit(64);
    identity(engine, "987654321", "7", "4095", "6");

    // An active-stream error occurs after Oboe closes the stream; stopped
    // recoloring is therefore safe immediately after that notification.
    replacement->errorAfterClose();
    require(!engine.isPlaying(), "active error marks stopped");
    engine.setGenreStateAndRerenderCurrent(2, 0, 2);
    identity(engine, "987654321", "7", "2", "2");

    // A failed requestStart also closes outside mLock, while holding only the
    // external-control mutex. Force a callback inside close to test this path.
    oboe::test::startResult.store(oboe::Result::ErrorDisconnected);
    oboe::test::closeHook = [&](oboe::AudioStream* stream) {
        engine.onErrorAfterClose(stream, oboe::Result::ErrorDisconnected);
    };
    auto failedStart = std::async(std::launch::async, [&] { return engine.start(); });
    complete(failedStart, "failed start close/error callback does not deadlock");
    require(!failedStart.get(), "failed requestStart is reported");
    require(!engine.isPlaying(), "failed start leaves stopped state");
    oboe::test::closeHook = {};
    oboe::test::startResult.store(oboe::Result::OK);
    engine.setGenreStateAndRerenderCurrent(4095, 1, 6);
    identity(engine, "987654321", "7", "4095", "6");
    require(engine.start(), "start recovers after failure");
    engine.stop();
    std::cout << "AudioEngine control checks passed: " << checks << '\n';
    std::cout << "Stopped selection updates actual snapshot without playback; close/control ordering, "
                 "pending load, playing callback, stale error and failed-start cleanup passed.\n";
    return 0;
}
