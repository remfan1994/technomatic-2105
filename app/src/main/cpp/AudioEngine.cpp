#include "AudioEngine.h"

#include <android/log.h>
#include <algorithm>
#include <chrono>

namespace rb {
namespace {
constexpr const char* kTag = "RadioBreaker";
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::start() {
    std::lock_guard<std::mutex> guard(mLock);
    if (mPlaying && mStream) return true;

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setSharingMode(oboe::SharingMode::Exclusive)
            ->setFormat(oboe::AudioFormat::Float)
            ->setChannelCount(oboe::ChannelCount::Stereo)
            ->setDataCallback(this)
            ->setErrorCallback(this);

    oboe::Result result = builder.openStream(mStream);
    if (result != oboe::Result::OK || !mStream) {
        __android_log_print(ANDROID_LOG_WARN, kTag,
                            "exclusive stream failed: %s; retrying shared",
                            oboe::convertToText(result));
        builder.setSharingMode(oboe::SharingMode::Shared);
        result = builder.openStream(mStream);
        if (result != oboe::Result::OK || !mStream) {
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                                "openStream failed: %s",
                                oboe::convertToText(result));
            return false;
        }
    }

    const int32_t sampleRate = mStream->getSampleRate() > 0 ? mStream->getSampleRate() : 48000;
    mMusic.prepare(static_cast<double>(sampleRate));
    mMusic.setPieceLengthSeconds(mPieceLengthSeconds.load(std::memory_order_acquire));

    const uint32_t seed = static_cast<uint32_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
    mMusic.reset(seed);

    result = mStream->requestStart();
    if (result != oboe::Result::OK) {
        __android_log_print(ANDROID_LOG_ERROR, kTag,
                            "requestStart failed: %s",
                            oboe::convertToText(result));
        mStream->close();
        mStream.reset();
        return false;
    }

    mPlaying = true;
    return true;
}

void AudioEngine::next() {
    mNextRequested.store(true, std::memory_order_release);
}

void AudioEngine::setPieceLengthSeconds(int32_t seconds) {
    seconds = std::max(8, std::min(999999, seconds));
    mPieceLengthSeconds.store(seconds, std::memory_order_release);
    mLengthChangeRequested.store(true, std::memory_order_release);
}

void AudioEngine::stop() {
    std::shared_ptr<oboe::AudioStream> stream;
    {
        std::lock_guard<std::mutex> guard(mLock);
        stream = mStream;
        mStream.reset();
        mPlaying = false;
    }

    if (stream) {
        stream->requestStop();
        stream->close();
    }
}

bool AudioEngine::isPlaying() const {
    std::lock_guard<std::mutex> guard(mLock);
    return mPlaying;
}

oboe::DataCallbackResult AudioEngine::onAudioReady(
        oboe::AudioStream* stream,
        void* audioData,
        int32_t numFrames) {
    if (!audioData || numFrames <= 0) {
        return oboe::DataCallbackResult::Continue;
    }

    if (mLengthChangeRequested.exchange(false, std::memory_order_acq_rel)) {
        mMusic.setPieceLengthSeconds(mPieceLengthSeconds.load(std::memory_order_acquire));
    }

    if (mNextRequested.exchange(false, std::memory_order_acq_rel)) {
        mMusic.next();
    }

    const int32_t channelCount = stream ? stream->getChannelCount() : 2;
    mMusic.render(static_cast<float*>(audioData), numFrames, channelCount);
    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream* /*stream*/, oboe::Result error) {
    __android_log_print(ANDROID_LOG_ERROR, kTag,
                        "audio stream closed after error: %s",
                        oboe::convertToText(error));
    std::lock_guard<std::mutex> guard(mLock);
    mStream.reset();
    mPlaying = false;
}

} // namespace rb
