#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "../app/src/main/cpp/MusicEngine.h"

int main() {
    constexpr int rate = 48000;
    constexpr int blockFrames = 512;
    std::vector<float> block(blockFrames * 2);

    for (uint32_t seed : {1u, 3290437499u, 0x12345678u, 0xffffffffu}) {
        rb::MusicEngine e;
        e.prepare(rate);
        e.reset(seed);
        int frames = rate * 2;
        while (frames > 0) {
            const int n = std::min(blockFrames, frames);
            std::fill(block.begin(), block.end(), 0.0f);
            e.render(block.data(), n, 2);
            for (int i = 0; i < n * 2; ++i) {
                const float sample = block[static_cast<std::size_t>(i)];
                if (!std::isfinite(sample) || std::fabs(sample) > 1.01f) return 1;
            }
            frames -= n;
        }
        const std::string before = e.currentSongData();
        e.rerenderCurrentWithChannel(1 << 9, 0, 10);
        const std::string after = e.currentSongData();
        if (before.empty() || after.empty()) return 2;
    }

    rb::MusicEngine a, b;
    a.prepare(rate);
    b.prepare(rate);
    a.setGenreMask((1 << 0) | (1 << 9));
    a.setGenreBlendMode(1);
    a.setGenrePrimary(1);
    a.reset(0x12345678u);
    const std::string data = a.currentSongData();
    if (!b.loadSongData(data)) return 3;
    std::vector<float> x(rate * 2), y(rate * 2);
    a.render(x.data(), rate, 2);
    b.render(y.data(), rate, 2);
    if (x != y) return 4;

    const char* path = "/tmp/technomatic_v28_sanitizer_range.pcm";
    std::remove(path);
    if (!rb::MusicEngine::exportPcm16RangeFile(data, 1, 9, path)) return 5;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return 6;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fclose(f);
    std::remove(path);
    const long expected = 8L * rate * 2L * static_cast<long>(sizeof(int16_t));
    if (size != expected) return 7;

    std::cout << "PASS v28 sanitizer smoke\n";
    return 0;
}
