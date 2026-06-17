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
    void setPieceLengthSeconds(int32_t seconds);
    void setGenreMask(int32_t mask);
    void setGenreBlendMode(int32_t mode);
    void setGenreStateAndForceNew(int32_t mask, int32_t mode);
    std::string currentSongData() const;
    std::string historyData() const;
    void clearHistory();
    bool loadSongData(const std::string& data);
    bool exportPcm16ToFile(const std::string& data, int32_t seconds, const std::string& path);
    int32_t currentGenreMask() const;
    int32_t currentGenreBlendMode() const;
    int32_t currentGenreMode() const;
    double currentElapsedSeconds() const;
    int32_t currentPieceLengthSeconds() const;
    bool isPlaying() const;

    oboe::DataCallbackResult onAudioReady(
            oboe::AudioStream* stream,
            void* audioData,
            int32_t numFrames) override;

    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    mutable std::mutex mLock;
    std::shared_ptr<oboe::AudioStream> mStream;
    MusicEngine mMusic;
    std::atomic<bool> mNextRequested{false};
    std::atomic<bool> mForceNewRequested{false};
    std::atomic<bool> mLengthChangeRequested{false};
    std::atomic<bool> mGenreMaskChangeRequested{false};
    std::atomic<bool> mGenreBlendModeChangeRequested{false};
    std::atomic<bool> mLoadSongDataRequested{false};
    std::atomic<int32_t> mPieceLengthSeconds{180};
    std::atomic<int32_t> mGenreMask{0};
    std::atomic<int32_t> mGenreBlendMode{0};
    mutable std::mutex mSongDataLock;
    std::string mPendingSongData;
    bool mPlaying = false;
};

} // namespace rb
