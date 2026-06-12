#include <jni.h>
#include <cstdint>
#include <memory>
#include <mutex>

#include "AudioEngine.h"

namespace {
std::mutex gLock;
std::unique_ptr<rb::AudioEngine> gEngine;

rb::AudioEngine* engine() {
    if (!gEngine) {
        gEngine = std::make_unique<rb::AudioEngine>();
    }
    return gEngine.get();
}
}

extern "C" JNIEXPORT jboolean JNICALL
Java_vip_thatiam_radiobreaker_NativeAudio_start(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    return engine()->start() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_vip_thatiam_radiobreaker_NativeAudio_stop(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    if (gEngine) {
        gEngine->stop();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_vip_thatiam_radiobreaker_NativeAudio_next(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    if (gEngine) {
        gEngine->next();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_vip_thatiam_radiobreaker_NativeAudio_setPieceLengthSeconds(JNIEnv*, jclass, jint seconds) {
    std::lock_guard<std::mutex> guard(gLock);
    engine()->setPieceLengthSeconds(static_cast<int32_t>(seconds));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_vip_thatiam_radiobreaker_NativeAudio_isPlaying(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    return (gEngine && gEngine->isPlaying()) ? JNI_TRUE : JNI_FALSE;
}
