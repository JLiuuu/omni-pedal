#pragma once

#include <string>
#include <vector>

struct Wav {
    int sampleRate = 48000;
    std::vector<float> samples;
};

bool readWav(const std::string& path, Wav& wav, std::string& err);
bool writeWav16(const std::string& path, const float* samples, int n, int sampleRate, std::string& err);
