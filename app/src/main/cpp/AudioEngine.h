#pragma once

#include <atomic>
#include <memory>
#include <mutex>

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
    void setPieceLengthSeconds(int32_t seconds);
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
    std::atomic<bool> mLengthChangeRequested{false};
    std::atomic<int32_t> mPieceLengthSeconds{1200};
    bool mPlaying = false;
};

} // namespace rb
