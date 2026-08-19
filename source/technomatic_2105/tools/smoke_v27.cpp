#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "../app/src/main/cpp/MusicEngine.h"

int main() {
    constexpr int kRate = 48000;
    std::vector<float> block(1024 * 2);

    for (uint32_t seed = 1; seed <= 16; ++seed) {
        rb::MusicEngine engine;
        engine.prepare(kRate);
        engine.reset(seed * 2654435761u);
        for (int i = 0; i < 6 * kRate / 1024; ++i) {
            std::fill(block.begin(), block.end(), 0.0f);
            engine.render(block.data(), 1024, 2);
            for (float sample : block) {
                if (!std::isfinite(sample) || std::fabs(sample) > 1.01f) {
                    std::cerr << "invalid sample for seed " << seed << "\n";
                    return 1;
                }
            }
        }
    }

    rb::MusicEngine a, b;
    a.prepare(kRate);
    b.prepare(kRate);
    a.reset(3290437499u);
    const std::string data = a.currentSongData();
    if (!b.loadSongData(data)) {
        std::cerr << "snapshot reload failed\n";
        return 1;
    }
    std::vector<float> x(kRate * 2), y(kRate * 2);
    a.render(x.data(), kRate, 2);
    b.render(y.data(), kRate, 2);
    if (x != y) {
        std::cerr << "snapshot mismatch\n";
        return 1;
    }

    const char* path = "/tmp/technomatic_v27_sanitizer_export.pcm";
    std::remove(path);
    if (!rb::MusicEngine::exportPcm16File(data, 8, path)) {
        std::cerr << "export failed\n";
        return 1;
    }
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return 1;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fclose(file);
    std::remove(path);
    if (size != 8L * kRate * 2L * static_cast<long>(sizeof(int16_t))) {
        std::cerr << "export size mismatch: " << size << "\n";
        return 1;
    }

    std::cout << "PASS v27 sanitizer smoke\n";
    return 0;
}
