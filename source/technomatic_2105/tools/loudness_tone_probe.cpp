// Offline calibration only. Reads loudness_probe's stereo float WAV and applies
// exactly v30's first static drive and DC filter, without AGC/ducking/fades.
// Optional fixed knee is a prototype for measurement, not a production path.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: tone_probe raw.wav output.wav pattern_drive [gain=1] [knee=0.72] [ceiling=0.94]\n";
        return 2;
    }
    FILE* input = std::fopen(argv[1], "rb");
    FILE* output = std::fopen(argv[2], "wb");
    if (!input || !output) return 2;
    unsigned char header[44];
    if (std::fread(header, 1, 44, input) != 44 || std::memcmp(header, "RIFF", 4) ||
        std::memcmp(header + 8, "WAVEfmt ", 8) || header[20] != 3 || header[22] != 2 ||
        header[34] != 32 || std::memcmp(header + 36, "data", 4)) return 2;
    if (std::fwrite(header, 1, 44, output) != 44) return 2;
    const float drive = 1.05f + 0.82f * static_cast<float>(std::atof(argv[3]));
    const float gain = argc > 4 ? static_cast<float>(std::atof(argv[4])) : 1.0f;
    const float knee = argc > 5 ? static_cast<float>(std::atof(argv[5])) : 0.72f;
    const float ceiling = argc > 6 ? static_cast<float>(std::atof(argv[6])) : 0.94f;
    const bool useKnee = argc > 4;
    if (useKnee && (gain <= 0 || knee < 0 || ceiling <= knee || ceiling > 1)) return 2;
    float dcIn[2] = {0, 0}, dcOut[2] = {0, 0};
    float samples[8192]; size_t count; size_t knees = 0, total = 0; double sumsq = 0, peak = 0;
    while ((count = std::fread(samples, sizeof(float), 8192, input)) != 0) {
        if (count % 2) return 2;
        for (size_t i = 0; i < count; ++i) {
            const int ch = static_cast<int>(i % 2);
            const float tone = std::tanh(samples[i] * drive) / std::tanh(drive);
            const float hp = tone - dcIn[ch] + 0.995f * dcOut[ch];
            dcIn[ch] = tone; dcOut[ch] = hp;
            float result = hp;
            if (useKnee) {
                result *= gain;
                const float magnitude = std::fabs(result);
                if (magnitude > knee) {
                    ++knees;
                    result = std::copysign(knee + (ceiling - knee) * std::tanh((magnitude - knee) / (ceiling - knee)), result);
                }
            }
            samples[i] = result; ++total;
            sumsq += static_cast<double>(result) * result;
            peak = std::max(peak, std::fabs(static_cast<double>(result)));
        }
        if (std::fwrite(samples, sizeof(float), count, output) != count) return 2;
    }
    std::fclose(input); if (std::fclose(output)) return 2;
    std::cout.precision(12);
    std::cout << "{\"rms\":" << std::sqrt(sumsq / total) << ",\"peak\":" << peak
        << ",\"knee_samples\":" << knees << ",\"samples\":" << total
        << ",\"gain\":" << gain << ",\"knee\":" << knee << ",\"ceiling\":" << ceiling << "}\n";
}
