#pragma once

#include <iosfwd>

// Input stage for the current analog circuit: guitar -> bias/buffer -> Codec Zero.
// This path returns the samples unchanged. Effects live in pedal.cpp.

namespace stage1 {

constexpr int kSampleRate = 48000;
constexpr int kPeriodFrames = 32;
constexpr int kPeriods = 2;

// 50 kΩ * 10 µF = 0.5 s, so the bias settles in about 2.5 s. Mute lasts 3 s.
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
