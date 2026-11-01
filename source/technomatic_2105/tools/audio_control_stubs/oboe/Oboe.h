#pragma once

// Host-only Oboe surface used to exercise production AudioEngine control flow.
// This directory is supplied explicitly by tools/test_audio_controls.py and is
// never on the Android app's include path.
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace oboe {

enum class Result { OK, ErrorDisconnected };
enum class Direction { Output };
enum class PerformanceMode { LowLatency };
enum class SharingMode { Exclusive, Shared };
enum class AudioFormat { Float };
enum class ChannelCount { Stereo };
enum class Usage { Media };
enum class ContentType { Music };
enum class DataCallbackResult { Continue, Stop };

inline const char* convertToText(Result value) {
    return value == Result::OK ? "OK" : "ErrorDisconnected";
}

class AudioStream;

class AudioStreamDataCallback {
public:
    virtual ~AudioStreamDataCallback() = default;
    virtual DataCallbackResult onAudioReady(AudioStream*, void*, int32_t) = 0;
};

class AudioStreamErrorCallback {
public:
    virtual ~AudioStreamErrorCallback() = default;
    virtual void onErrorAfterClose(AudioStream*, Result) {}
};

namespace test {
inline std::shared_ptr<AudioStream> lastOpened;
inline std::atomic<int> opens{0};
inline std::atomic<int> starts{0};
inline std::atomic<int> closes{0};
inline std::atomic<Result> startResult{Result::OK};
// Set only while the test is quiescent; callbacks can then invoke the hook.
inline std::function<void(AudioStream*)> closeHook;
}

class AudioStream {
public:
    AudioStream(AudioStreamDataCallback* data, AudioStreamErrorCallback* error)
        : dataCallback(data), errorCallback(error) {}

    int32_t getSampleRate() const { return 48000; }
    int32_t getChannelCount() const { return 2; }
    Result requestStart() {
        test::starts.fetch_add(1);
        return test::startResult.load();
    }
    Result requestStop() { return Result::OK; }
    Result close() {
        if (closed.load()) return Result::OK;
        test::closes.fetch_add(1);
        if (test::closeHook) test::closeHook(this);
        closed.store(true);
        return Result::OK;
    }
    void emit(int frames) {
        std::vector<float> samples(static_cast<size_t>(frames * 2));
        dataCallback->onAudioReady(this, samples.data(), frames);
    }
    void errorAfterClose() {
        closed.store(true);
        errorCallback->onErrorAfterClose(this, Result::ErrorDisconnected);
    }

private:
    AudioStreamDataCallback* dataCallback;
    AudioStreamErrorCallback* errorCallback;
    std::atomic<bool> closed{false};
};

class AudioStreamBuilder {
public:
    AudioStreamBuilder* setDirection(Direction) { return this; }
    AudioStreamBuilder* setPerformanceMode(PerformanceMode) { return this; }
    AudioStreamBuilder* setSharingMode(SharingMode) { return this; }
    AudioStreamBuilder* setFormat(AudioFormat) { return this; }
    AudioStreamBuilder* setChannelCount(ChannelCount) { return this; }
    AudioStreamBuilder* setUsage(Usage) { return this; }
    AudioStreamBuilder* setContentType(ContentType) { return this; }
    AudioStreamBuilder* setDataCallback(AudioStreamDataCallback* callback) {
        data = callback;
        return this;
    }
    AudioStreamBuilder* setErrorCallback(AudioStreamErrorCallback* callback) {
        error = callback;
        return this;
    }
    Result openStream(std::shared_ptr<AudioStream>& output) {
        output = std::make_shared<AudioStream>(data, error);
        test::lastOpened = output;
        test::opens.fetch_add(1);
        return Result::OK;
    }

private:
    AudioStreamDataCallback* data = nullptr;
    AudioStreamErrorCallback* error = nullptr;
};

} // namespace oboe
