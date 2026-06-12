#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <vector>

#include "../app/src/main/cpp/MusicEngine.h"

static void writeLE16(FILE* f, uint16_t v) {
    unsigned char b[2] = {static_cast<unsigned char>(v & 255u), static_cast<unsigned char>((v >> 8u) & 255u)};
    fwrite(b, 1, 2, f);
}

static void writeLE32(FILE* f, uint32_t v) {
    unsigned char b[4] = {
        static_cast<unsigned char>(v & 255u),
        static_cast<unsigned char>((v >> 8u) & 255u),
        static_cast<unsigned char>((v >> 16u) & 255u),
        static_cast<unsigned char>((v >> 24u) & 255u)
    };
    fwrite(b, 1, 4, f);
}

static void writeWav(const char* path, const std::vector<float>& samples, int sampleRate, int channels) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;

    const uint32_t sampleCount = static_cast<uint32_t>(samples.size());
    const uint32_t dataBytes = sampleCount * sizeof(int16_t);
    const uint32_t fmtBytes = 16;
    const uint32_t riffBytes = 4 + (8 + fmtBytes) + (8 + dataBytes);

    fwrite("RIFF", 1, 4, f);
    writeLE32(f, riffBytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    writeLE32(f, fmtBytes);
    writeLE16(f, 1); // PCM
    writeLE16(f, static_cast<uint16_t>(channels));
    writeLE32(f, static_cast<uint32_t>(sampleRate));
    writeLE32(f, static_cast<uint32_t>(sampleRate * channels * sizeof(int16_t)));
    writeLE16(f, static_cast<uint16_t>(channels * sizeof(int16_t)));
    writeLE16(f, 16);
    fwrite("data", 1, 4, f);
    writeLE32(f, dataBytes);

    for (float x : samples) {
        if (x > 1.0f) x = 1.0f;
        if (x < -1.0f) x = -1.0f;
        const int16_t s = static_cast<int16_t>(x * 32767.0f);
        writeLE16(f, static_cast<uint16_t>(s));
    }

    std::fclose(f);
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "radio_breaker_preview.wav";
    constexpr int sampleRate = 48000;
    constexpr int channels = 2;
    const int seconds = argc > 2 ? std::max(1, std::atoi(argv[2])) : 180;
    const uint32_t seed = argc > 3 ? static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 0)) : 0x52423934u;
    const int trackSeconds = argc > 4 ? std::max(8, std::atoi(argv[4])) : 1200;
    constexpr int blockFrames = 256;

    rb::MusicEngine engine;
    engine.prepare(sampleRate);
    engine.setPieceLengthSeconds(trackSeconds);
    engine.reset(seed);

    std::vector<float> all(static_cast<size_t>(sampleRate * seconds * channels), 0.0f);
    std::vector<float> block(static_cast<size_t>(blockFrames * channels), 0.0f);

    int framesLeft = sampleRate * seconds;
    size_t write = 0;
    while (framesLeft > 0) {
        const int frames = framesLeft > blockFrames ? blockFrames : framesLeft;
        std::memset(block.data(), 0, block.size() * sizeof(float));
        engine.render(block.data(), frames, channels);
        std::memcpy(all.data() + write, block.data(), static_cast<size_t>(frames * channels) * sizeof(float));
        write += static_cast<size_t>(frames * channels);
        framesLeft -= frames;
    }

    writeWav(out, all, sampleRate, channels);
    std::printf("wrote %s (%d sec render, %d sec track length)\n", out, seconds, trackSeconds);
    return 0;
}
