#include "stage1.hpp"

#include <algorithm>
#include <cmath>
#include <ostream>

namespace stage1 {

void DryPath::prepare(int rate, double muteSeconds) {
    sampleRate = rate > 0 ? rate : kSampleRate;
    if (muteSeconds < 0.0) {
        muteSeconds = 0.0;
    }
    muteLeft = static_cast<int>(std::llround(muteSeconds * sampleRate));
}

int DryPath::muteSamples() const {
    return static_cast<int>(std::llround(kMuteSeconds * sampleRate));
}

// The first muteLeft samples after power-up are written as 0.
// Writing 0 does not delay later audio: sample n of the output is sample n of the input.
// in and out may be the same buffer.
void DryPath::process(const float* in, float* out, int n) {
    if (n <= 0 || in == nullptr || out == nullptr) {
        return;
    }
    const int muted = std::min(n, muteLeft);
    for (int i = 0; i < muted; ++i) {
        out[i] = 0.0f;
    }
    muteLeft -= muted;
    if (in != out) {
        for (int i = muted; i < n; ++i) {
            out[i] = in[i];
        }
    }
}

Latency latencyFor(int sampleRate, int periodFrames, int periods) {
    Latency L;
    L.sampleRate = sampleRate;
    L.periodFrames = periodFrames;
    L.periods = periods;
    L.periodMs = 1000.0 * static_cast<double>(periodFrames) / static_cast<double>(sampleRate);
    L.oneDirectionMs = L.periodMs * static_cast<double>(periods);
    // Upper bound when both the capture buffer and the playback buffer are full.
    // A lab measurement can come in under this.
    L.softwareRoundTripMs = L.oneDirectionMs * 2.0;
    L.dspMs = 0.0;
    return L;
}

void printReport(std::ostream& os, int sampleRate, int muteSamples) {
    const Latency planned = latencyFor(sampleRate, kPeriodFrames, kPeriods);
    const double muteSec = static_cast<double>(muteSamples) / static_cast<double>(sampleRate);

    os << "Software status: dry input stage for the current analog input circuit\n";
    os << "Date: 2026-11-04\n\n";

    os << "Where the code is\n";
    os << "src/pedal.cpp     effects, Bass Split, knob map, footswitch debounce\n";
    os << "src/stage1.cpp    startup mute and the latency budget\n";
    os << "src/main.cpp      demo wav files and the live path on the Pi\n\n";

    os << "What the program does\n";
    os << "src/pedal.cpp holds the effects. Bypass is a dry copy.\n";
    os << "Standard mode: overdrive, distortion, delay, chorus, reverb.\n";
    os << "Switching crossfades for about 10 ms.\n";
    os << "Bass Split adds one octave down below the split and leaves higher notes dry.\n";
    os << "It tracks one note at a time.\n";
    os << "ADS7830 knob reads and the GPIO footswitch open on the Pi.\n\n";

    os << "Latency\n";
    os << "Dry copy: " << planned.dspMs << " ms (0 samples)\n";
    os << "Sample rate: " << planned.sampleRate << " Hz\n";
    os << "Codec period: " << planned.periodFrames << " frames, " << planned.periodMs << " ms\n";
    os << planned.periods << " periods each direction, " << planned.oneDirectionMs << " ms\n";
    os << "Software upper bound with both buffers full: " << planned.softwareRoundTripMs << " ms\n";
    os << "The analog input stage is microseconds and is outside this 5 ms budget.\n";
    os << "The codec digital-filter delay still needs a scope measurement.\n";
    os << "The software upper bound is about 2.67 ms, which leaves room for the codec\n";
    os << "inside the 5 ms input-jack to output-jack target.\n\n";

    os << "Startup mute\n";
    os << "Muted for " << muteSec << " s (" << muteSamples << " samples).\n";
    os << "The 50 kOhm bias resistor and 10 uF capacitor have a 0.5 s time constant,\n";
    os << "so the bias settles in about 2.5 s.\n";
    os << "Mute writes zeros. It does not delay the audio that follows.\n\n";

    os << "Interface to the schematic\n";
    os << "Codec Zero AUX IN and AUX OUT, line level, about 1 V RMS.\n";
    os << "The onboard microphone jack is unused. That jack is the one with mic bias.\n";
    os << "This software stage adds no gain. Keep VR1 at unity for now.\n";
    os << "The codec opens at 24 bit (ALSA format S32_LE) and 48 kHz.\n\n";

    os << "Checked on the PC\n";
    os << "Compared against a 1 kHz sine in memory, with no Pi:\n";
    os << "- with mute off, every output sample matches the input and impulses stay put\n";
    os << "- with mute on, the first 3 seconds are 0, then samples line up with the input\n";
    os << "- one full-buffer pass matches processing in small blocks\n\n";

    os << "Still open\n";
    os << "After the Pi is connected, measure input-jack to output-jack latency with a guitar.\n\n";

    os << "Summary\n";
    os << "The proposal effects and Bass Split are in the program and checked with WAV files.\n";
    os << "Dry bypass still adds 0 samples. At 48 kHz, 32-frame periods, two buffers each\n";
    os << "way, the software upper bound is about 2.7 ms.\n";
    os << "Startup mute is 3 seconds. Knob and footswitch code waits for the board.\n\n";

    os << "numbers\n";
    os << "sample_rate_hz=" << planned.sampleRate << "\n";
    os << "period_frames=" << planned.periodFrames << "\n";
    os << "periods_each_way=" << planned.periods << "\n";
    os << "period_ms=" << planned.periodMs << "\n";
    os << "one_direction_ms=" << planned.oneDirectionMs << "\n";
    os << "software_round_trip_upper_ms=" << planned.softwareRoundTripMs << "\n";
    os << "dsp_ms=" << planned.dspMs << "\n";
    os << "mute_samples=" << muteSamples << "\n";
    os << "mute_seconds=" << muteSec << "\n";
}

}  // namespace stage1
