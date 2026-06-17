#include <jni.h>
#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "AudioEngine.h"

namespace {
std::mutex gLock;
std::atomic<bool> gExportCancel{false};
std::unique_ptr<rb::AudioEngine> gEngine;

rb::AudioEngine* engine() {
    if (!gEngine) {
        gEngine = std::make_unique<rb::AudioEngine>();
    }
    return gEngine.get();
}
}

extern "C" JNIEXPORT jboolean JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_start(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    return engine()->start() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_stop(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    if (gEngine) {
        gEngine->stop();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_next(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    if (gEngine) {
        gEngine->next();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_forceNew(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    if (gEngine) {
        gEngine->forceNew();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_setPieceLengthSeconds(JNIEnv*, jclass, jint seconds) {
    std::lock_guard<std::mutex> guard(gLock);
    engine()->setPieceLengthSeconds(static_cast<int32_t>(seconds));
}

extern "C" JNIEXPORT void JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_setGenreMask(JNIEnv*, jclass, jint mask) {
    std::lock_guard<std::mutex> guard(gLock);
    engine()->setGenreMask(static_cast<int32_t>(mask));
}


extern "C" JNIEXPORT void JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_setGenreBlendMode(JNIEnv*, jclass, jint mode) {
    std::lock_guard<std::mutex> guard(gLock);
    engine()->setGenreBlendMode(static_cast<int32_t>(mode));
}


extern "C" JNIEXPORT void JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_setGenreStateAndForceNew(JNIEnv*, jclass, jint mask, jint mode) {
    std::lock_guard<std::mutex> guard(gLock);
    engine()->setGenreStateAndForceNew(static_cast<int32_t>(mask), static_cast<int32_t>(mode));
}

extern "C" JNIEXPORT jstring JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_currentSongData(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    const std::string data = engine()->currentSongData();
    return env->NewStringUTF(data.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_historyData(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    const std::string data = engine()->historyData();
    return env->NewStringUTF(data.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_clearHistory(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    if (gEngine) gEngine->clearHistory();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_loadSongData(JNIEnv* env, jclass, jstring data) {
    if (!data) return JNI_FALSE;
    const char* chars = env->GetStringUTFChars(data, nullptr);
    if (!chars) return JNI_FALSE;
    std::string value(chars);
    env->ReleaseStringUTFChars(data, chars);
    std::lock_guard<std::mutex> guard(gLock);
    return engine()->loadSongData(value) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_exportPcm16ToFile(JNIEnv* env, jclass, jstring data, jint seconds, jstring path) {
    if (!data || !path) return JNI_FALSE;
    const char* dataChars = env->GetStringUTFChars(data, nullptr);
    if (!dataChars) return JNI_FALSE;
    const char* pathChars = env->GetStringUTFChars(path, nullptr);
    if (!pathChars) {
        env->ReleaseStringUTFChars(data, dataChars);
        return JNI_FALSE;
    }
    std::string dataValue(dataChars);
    std::string pathValue(pathChars);
    env->ReleaseStringUTFChars(data, dataChars);
    env->ReleaseStringUTFChars(path, pathChars);
    gExportCancel.store(false, std::memory_order_relaxed);
    const bool ok = rb::MusicEngine::exportPcm16File(dataValue, static_cast<int32_t>(seconds), pathValue, &gExportCancel);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_cancelExportRender(JNIEnv*, jclass) {
    gExportCancel.store(true, std::memory_order_relaxed);
}

extern "C" JNIEXPORT jint JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_currentGenreMask(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    return engine()->currentGenreMask();
}

extern "C" JNIEXPORT jint JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_currentGenreBlendMode(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    return engine()->currentGenreBlendMode();
}

extern "C" JNIEXPORT jint JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_currentGenreMode(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    return gEngine ? static_cast<jint>(gEngine->currentGenreMode()) : 0;
}

extern "C" JNIEXPORT jdouble JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_currentElapsedSeconds(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    return gEngine ? static_cast<jdouble>(gEngine->currentElapsedSeconds()) : 0.0;
}

extern "C" JNIEXPORT jint JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_currentPieceLengthSeconds(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    return gEngine ? static_cast<jint>(gEngine->currentPieceLengthSeconds()) : 180;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_vip_thatiam_technomatic2105_NativeAudio_isPlaying(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> guard(gLock);
    return (gEngine && gEngine->isPlaying()) ? JNI_TRUE : JNI_FALSE;
}
