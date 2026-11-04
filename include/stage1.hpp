#pragma once

#include <iosfwd>

// 输入级。对应现在这张模拟电路：吉他 -> 偏置/缓冲 -> Codec Zero。
// 这里只把采样送回去，不加效果。

namespace stage1 {

constexpr int kSampleRate = 48000;
constexpr int kPeriodFrames = 32;
constexpr int kPeriods = 2;

// 50 kΩ * 10 µF = 0.5 s，大约 2.5 s 后偏置才稳。静音留到 3 s。
constexpr double kMuteSeconds = 3.0;

struct DryPath {
    int sampleRate = kSampleRate;
    int muteLeft = 0;

    void prepare(int rate, double muteSeconds);
    int muteSamples() const;
    void process(const float* in, float* out, int n);
};

struct Latency {
    int sampleRate = kSampleRate;
    int periodFrames = kPeriodFrames;
    int periods = kPeriods;
    double periodMs = 0.0;
    double oneDirectionMs = 0.0;
    double softwareRoundTripMs = 0.0;
    double dspMs = 0.0;
};

Latency latencyFor(int sampleRate, int periodFrames, int periods);
void printReport(std::ostream& os, int sampleRate, int muteSamples);

}  // namespace stage1
