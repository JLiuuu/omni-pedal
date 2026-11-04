#include "stage1.hpp"
#include "wav.hpp"

int runFxTests();

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

int g_failed = 0;

void check(bool ok, const char* name) {
    if (ok) {
        std::cout << "PASS " << name << "\n";
    } else {
        std::cout << "FAIL " << name << "\n";
        ++g_failed;
    }
}

float tone(int i) {
    const double phase = 2.0 * 3.14159265358979323846 * 1000.0 * static_cast<double>(i) / 48000.0;
    return 0.2f * static_cast<float>(std::sin(phase));
}

}  // namespace

int main() {
    using stage1::DryPath;

    {
        DryPath dry;
        dry.prepare(48000, 0.0);
        const int n = 64;
        std::vector<float> in(n), out(n, 1.0f);
        for (int i = 0; i < n; ++i) {
            in[i] = tone(i);
        }
        in[10] = 0.5f;
        dry.process(in.data(), out.data(), n);
        bool exact = true;
        for (int i = 0; i < n; ++i) {
            if (out[i] != in[i]) {
                exact = false;
            }
        }
        check(exact && out[10] == 0.5f, "dry_copy_is_exact");
    }

    {
        DryPath dry;
        dry.prepare(48000, 0.0);
        float buf[8] = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f, -0.6f, 0.7f, -0.8f};
        const float original[8] = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f, -0.6f, 0.7f, -0.8f};
        dry.process(buf, buf, 8);
        bool exact = true;
        for (int i = 0; i < 8; ++i) {
            if (buf[i] != original[i]) {
                exact = false;
            }
        }
        check(exact, "in_place_copy");
    }

    {
        DryPath dry;
        dry.prepare(48000, stage1::kMuteSeconds);
        check(dry.muteLeft == 144000, "mute_is_3_seconds");

        const int n = 200000;
        std::vector<float> in(n), out(n, 7.0f);
        for (int i = 0; i < n; ++i) {
            in[i] = tone(i);
        }
        dry.process(in.data(), out.data(), n);

        bool front = true;
        for (int i = 0; i < 144000; ++i) {
            if (out[i] != 0.0f) {
                front = false;
                break;
            }
        }
        bool aligned = true;
        for (int i = 144000; i < n; ++i) {
            if (out[i] != in[i]) {
                aligned = false;
                break;
            }
        }
        check(front, "mute_writes_zeros");
        check(aligned && out[144001] == in[144001] && in[144001] != 0.0f,
              "mute_does_not_shift_audio");
        check(dry.muteLeft == 0, "mute_counter_reaches_zero");
    }

    {
        DryPath a;
        DryPath b;
        a.prepare(48000, 3.0);
        b.prepare(48000, 3.0);
        const int n = 1000;
        std::vector<float> in(n), whole(n), chunked(n, 3.0f);
        for (int i = 0; i < n; ++i) {
            in[i] = tone(i + 140000);
        }
        std::vector<float> burn(143880, 0.25f);
        std::vector<float> burnOut(143880);
        a.process(burn.data(), burnOut.data(), 143880);
        b.process(burn.data(), burnOut.data(), 143880);
        a.process(in.data(), whole.data(), n);
        int done = 0;
        while (done < n) {
            const int k = std::min(32, n - done);
            b.process(in.data() + done, chunked.data() + done, k);
            done += k;
        }
        bool same = whole[100] == 0.0f && whole[200] == in[200] && whole[200] != 0.0f;
        for (int i = 0; i < n; ++i) {
            if (whole[i] != chunked[i]) {
                same = false;
            }
        }
        check(same, "block_size_does_not_matter");
    }

    {
        const auto L = stage1::latencyFor(48000, 32, 2);
        const bool under = L.softwareRoundTripMs < 5.0;
        const bool dsp = L.dspMs == 0.0;
        const bool period = std::fabs(L.periodMs - (1000.0 * 32.0 / 48000.0)) < 1e-9;
        check(under && dsp && period, "latency_budget_under_5ms");
    }

    {
#ifdef _WIN32
        _mkdir("out");
#else
        mkdir("out", 0755);
#endif
        std::vector<float> in(128);
        for (int i = 0; i < 128; ++i) {
            in[i] = tone(i);
        }
        std::string err;
        const bool wrote = writeWav16("out/roundtrip.wav", in.data(), 128, 48000, err);
        Wav wav;
        const bool loaded = wrote && readWav("out/roundtrip.wav", wav, err);
        bool close = loaded && wav.sampleRate == 48000 && wav.samples.size() == 128;
        if (close) {
            for (int i = 0; i < 128; ++i) {
                if (std::fabs(wav.samples[static_cast<size_t>(i)] - in[static_cast<size_t>(i)]) > (1.5f / 32767.0f)) {
                    close = false;
                }
            }
        }
        check(close, "wav_roundtrip");
        if (!close && !err.empty()) {
            std::cout << "  " << err << "\n";
        }
    }

    g_failed += runFxTests();

    if (g_failed != 0) {
        std::cout << g_failed << " failed\n";
        return 1;
    }
    std::cout << "all passed\n";
    return 0;
}
