#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include <oboe/Oboe.h>

#include "MusicEngine.h"

namespace rb {

class AudioEngine : public oboe::AudioStreamDataCallback,
                    public oboe::AudioStreamErrorCallback {
public:
    AudioEngine() = default;
    ~AudioEngine();

    bool start();
    void stop();
    void next();
    void forceNew();
    // Retired channel API retained as no-op/zero-valued compatibility shims.
    void setGenreMask(int32_t mask);
    void setGenreBlendMode(int32_t mode);
    void setGenrePrimary(int32_t mode);
    void setGenreStateAndRerenderCurrent(int32_t mask, int32_t mode, int32_t primary);
    std::string currentSongData() const;
    std::string historyData() const;
    void clearHistory();
    bool loadSongData(const std::string& data);
    bool exportPcm16ToFile(const std::string& data, int32_t seconds, const std::string& path);
    bool exportPcm16RangeToFile(const std::string& data, int32_t startSeconds, int32_t endSeconds, const std::string& path);
    int32_t currentGenreMask() const;
    int32_t currentGenreBlendMode() const;
    int32_t currentGenrePrimary() const;
    int32_t currentGenreMode() const;
    double currentElapsedSeconds() const;
    bool isPlaying() const;

    oboe::DataCallbackResult onAudioReady(
            oboe::AudioStream* stream,
            void* audioData,
            int32_t numFrames) override;

    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    // External stream controls remain serialized until close() has actually
    // completed. Neither audio nor error callbacks acquire this mutex.
    mutable std::mutex mControlLock;
    mutable std::mutex mLock;
    std::shared_ptr<oboe::AudioStream> mStream;
    MusicEngine mMusic;
    std::atomic<bool> mNextRequested{false};
    std::atomic<bool> mForceNewRequested{false};
    std::atomic<bool> mLoadSongDataRequested{false};
    mutable std::mutex mSongDataLock;
    std::string mPendingSongData;
    bool mPlaying = false;
    bool mMusicInitialized = false;
};

} // namespace rb
