#include "AudioEngine.h"

#include <android/log.h>
#include <chrono>
#include <utility>

namespace rb {
namespace {
constexpr const char* kTag = "Technomatic2105";
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::start() {
    std::lock_guard<std::mutex> controlGuard(mControlLock);
    std::unique_lock<std::mutex> guard(mLock);
    if (mPlaying && mStream) return true;

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setSharingMode(oboe::SharingMode::Exclusive)
            ->setFormat(oboe::AudioFormat::Float)
            ->setChannelCount(oboe::ChannelCount::Stereo)
            ->setUsage(oboe::Usage::Media)
            ->setContentType(oboe::ContentType::Music)
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

    if (!mMusicInitialized) {
        const uint32_t seed = static_cast<uint32_t>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count());
        mMusic.reset(seed);
        mMusicInitialized = true;
    }
    if (mLoadSongDataRequested.load(std::memory_order_acquire)) {
        std::string data;
        {
            std::lock_guard<std::mutex> dataGuard(mSongDataLock);
            data = mPendingSongData;
        }
        if (!data.empty()) {
            mMusic.loadSongData(data);
            mLoadSongDataRequested.store(false, std::memory_order_release);
        }
    }

    result = mStream->requestStart();
    if (result != oboe::Result::OK) {
        __android_log_print(ANDROID_LOG_ERROR, kTag,
                            "requestStart failed: %s",
                            oboe::convertToText(result));
        std::shared_ptr<oboe::AudioStream> failedStream = std::move(mStream);
        mPlaying = false;
        // A close can deliver onErrorAfterClose(), which needs mLock. Keep
        // external controls serialized while allowing that callback to finish.
        guard.unlock();
        failedStream->close();
        return false;
    }

    mPlaying = true;
    return true;
}

void AudioEngine::next() {
    mNextRequested.store(true, std::memory_order_release);
}

void AudioEngine::forceNew() {
    mForceNewRequested.store(true, std::memory_order_release);
}


// ABI compatibility only. v30 always uses the full generator, and an old
// channel command must not change the sound, rewind it or open a stream.
void AudioEngine::setGenreMask(int32_t /*mask*/) {}
void AudioEngine::setGenreBlendMode(int32_t /*mode*/) {}
void AudioEngine::setGenrePrimary(int32_t /*primary*/) {}
void AudioEngine::setGenreStateAndRerenderCurrent(
        int32_t /*mask*/, int32_t /*mode*/, int32_t /*primary*/) {}

std::string AudioEngine::currentSongData() const {
    return mMusic.currentSongData();
}

std::string AudioEngine::historyData() const {
    return mMusic.historyData();
}

void AudioEngine::clearHistory() {
    mMusic.clearHistory();
}

bool AudioEngine::loadSongData(const std::string& data) {
    if (data.empty()) return false;
    {
        std::lock_guard<std::mutex> guard(mSongDataLock);
        mPendingSongData = data;
    }
    mLoadSongDataRequested.store(true, std::memory_order_release);
    return true;
}

bool AudioEngine::exportPcm16ToFile(const std::string& data, int32_t seconds, const std::string& path) {
    return MusicEngine::exportPcm16File(data, seconds, path);
}

bool AudioEngine::exportPcm16RangeToFile(const std::string& data, int32_t startSeconds,
                                         int32_t endSeconds, const std::string& path) {
    return MusicEngine::exportPcm16RangeFile(data, startSeconds, endSeconds, path);
}

int32_t AudioEngine::currentGenreMask() const { return 0; }
int32_t AudioEngine::currentGenreBlendMode() const { return 0; }
int32_t AudioEngine::currentGenrePrimary() const { return 0; }
int32_t AudioEngine::currentGenreMode() const { return 0; }

double AudioEngine::currentElapsedSeconds() const {
    return mMusic.currentElapsedSeconds();
}


void AudioEngine::stop() {
    std::lock_guard<std::mutex> controlGuard(mControlLock);
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

    if (mLoadSongDataRequested.exchange(false, std::memory_order_acq_rel)) {
        std::string data;
        {
            std::lock_guard<std::mutex> guard(mSongDataLock);
            data = mPendingSongData;
        }
        if (!data.empty()) {
            mMusic.loadSongData(data);
        }
    }

    if (mForceNewRequested.exchange(false, std::memory_order_acq_rel)) {
        mMusic.forceNewPiece();
    }

    if (mNextRequested.exchange(false, std::memory_order_acq_rel)) {
        mMusic.next();
    }

    const int32_t channelCount = stream ? stream->getChannelCount() : 2;
    mMusic.render(static_cast<float*>(audioData), numFrames, channelCount);
    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) {
    __android_log_print(ANDROID_LOG_ERROR, kTag,
                        "audio stream closed after error: %s",
                        oboe::convertToText(error));
    std::lock_guard<std::mutex> guard(mLock);
    // A late callback from a replaced/explicitly stopped stream must not tear
    // down a newer stream. Oboe guarantees this stream is already closed here.
    if (mStream.get() != stream) return;
    mStream.reset();
    mPlaying = false;
}

} // namespace rb
