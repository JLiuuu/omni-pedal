#include "pedal.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
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

float sineAt(int i, float freq, float amp) {
    const double phase = 2.0 * 3.14159265358979323846 * static_cast<double>(freq) * i / 48000.0;
    return amp * static_cast<float>(std::sin(phase));
}

float goertzel(const float* x, int n, float freq) {
    const double w = 2.0 * 3.14159265358979323846 * static_cast<double>(freq) / 48000.0;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;
    for (int i = 0; i < n; ++i) {
        s0 = static_cast<double>(x[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return static_cast<float>(std::sqrt(std::max(0.0, power)) * 2.0 / n);
}

bool allFinite(const std::vector<float>& x) {
    for (float v : x) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    return true;
}

float maxAbs(const std::vector<float>& x) {
    float m = 0.0f;
    for (float v : x) {
        m = std::max(m, std::fabs(v));
    }
    return m;
}

stage1::Pedal make(stage1::Effect effect, stage1::Knobs knobs) {
    stage1::Pedal pedal;
    pedal.setKnobs(knobs);
    pedal.prepare(48000);
    pedal.cutTo(effect);
    return pedal;
}

}  // namespace

int runFxTests() {
    using stage1::Effect;
    using stage1::Knobs;
    using stage1::Pedal;

    {
        const float mag = goertzel(nullptr, 0, 100.0f);
        (void)mag;
        std::vector<float> tone(48000);
        for (int i = 0; i < 48000; ++i) {
            tone[static_cast<size_t>(i)] = sineAt(i, 100.0f, 0.25f);
        }
        const float got = goertzel(tone.data(), 48000, 100.0f);
        check(std::fabs(got - 0.25f) < 0.02f, "goertzel_known_sine");
    }

    {
        Knobs knobs;
        knobs.level = 1.0f;
        Pedal pedal = make(Effect::Bypass, knobs);
        std::vector<float> in(128), out(128, 3.0f);
        for (int i = 0; i < 128; ++i) {
            in[static_cast<size_t>(i)] = sineAt(i, 1000.0f, 0.2f);
        }
        in[10] = 0.5f;
        pedal.process(in.data(), out.data(), 128);
        bool exact = true;
        for (int i = 0; i < 128; ++i) {
            if (out[static_cast<size_t>(i)] != in[static_cast<size_t>(i)]) {
                exact = false;
            }
        }
        check(exact, "pedal_bypass_exact");
    }

    {
        Knobs knobs;
        knobs.drive = 0.0f;
        knobs.mix = 0.5f;
        knobs.time = 0.2f;
        knobs.level = 1.0f;
        const int expect = stage1::delaySamples(knobs.time, 48000);
        Pedal whole = make(Effect::Delay, knobs);
        Pedal chunked = make(Effect::Delay, knobs);
        const int n = 48000;
        std::vector<float> in(static_cast<size_t>(n), 0.0f);
        std::vector<float> out(static_cast<size_t>(n), 0.0f);
        std::vector<float> outBlocks(static_cast<size_t>(n), 0.0f);
        in[0] = 1.0f;
        whole.process(in.data(), out.data(), n);
        for (int pos = 0; pos < n; pos += 32) {
            chunked.process(in.data() + pos, outBlocks.data() + pos, 32);
        }
        bool quiet = true;
        for (int i = 1; i < expect; ++i) {
            if (std::fabs(out[static_cast<size_t>(i)]) > 1.0e-5f) {
                quiet = false;
            }
        }
        bool same = true;
        for (int i = 0; i < n; ++i) {
            if (out[static_cast<size_t>(i)] != outBlocks[static_cast<size_t>(i)]) {
                same = false;
            }
        }
        const bool echo = std::fabs(out[static_cast<size_t>(expect)] - 0.5f) < 1.0e-4f;
        check(out[0] == 1.0f && quiet && echo, "delay_dry_is_immediate");
        check(same, "delay_block_size");
        if (!(out[0] == 1.0f && quiet && echo)) {
            std::cout << "  delay expect " << expect << " got " << out[static_cast<size_t>(expect)] << "\n";
        }
    }

    {
        auto ratio = [](float drive) {
            Knobs knobs;
            knobs.drive = drive;
            knobs.tone = 1.0f;
            knobs.mix = 1.0f;
            knobs.level = 1.0f;
            Pedal pedal = make(Effect::Overdrive, knobs);
            const int n = 4800;
            std::vector<float> in(static_cast<size_t>(n)), out(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) {
                in[static_cast<size_t>(i)] = sineAt(i, 1000.0f, 0.3f);
            }
            pedal.process(in.data(), out.data(), n);
            const float h1 = goertzel(out.data(), n, 1000.0f);
            const float h3 = goertzel(out.data(), n, 3000.0f);
            return h1 > 1.0e-4f ? h3 / h1 : 0.0f;
        };
        const float low = ratio(0.0f);
        const float high = ratio(1.0f);
        check(high > low * 2.0f && high > 0.03f, "overdrive_adds_harmonics");
        if (!(high > low * 2.0f && high > 0.03f)) {
            std::cout << "  H3/H1 low " << low << " high " << high << "\n";
        }
    }

    {
        Knobs knobs;
        knobs.drive = 0.85f;
        knobs.tone = 0.4f;
        knobs.mix = 1.0f;
        knobs.level = 1.0f;
        Pedal pedal = make(Effect::Distortion, knobs);
        const int n = 4800;
        std::vector<float> in(static_cast<size_t>(n)), out(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            in[static_cast<size_t>(i)] = sineAt(i, 220.0f, 0.2f);
        }
        pedal.process(in.data(), out.data(), n);
        float diff = 0.0f;
        for (int i = 0; i < n; ++i) {
            diff += std::fabs(out[static_cast<size_t>(i)] - in[static_cast<size_t>(i)]);
        }
        check(allFinite(out) && maxAbs(out) < 1.2f && diff / n > 0.02f, "distortion_stays_in_range");
    }

    {
        Knobs knobs;
        knobs.tone = 0.7f;
        knobs.mix = 0.6f;
        knobs.time = 0.4f;
        knobs.level = 1.0f;
        Pedal pedal = make(Effect::Chorus, knobs);
        std::vector<float> impulseIn(64, 0.0f), impulseOut(64, 0.0f);
        impulseIn[0] = 1.0f;
        pedal.process(impulseIn.data(), impulseOut.data(), 64);
        Pedal body = make(Effect::Chorus, knobs);
        const int n = 48000;
        std::vector<float> in(static_cast<size_t>(n)), out(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            in[static_cast<size_t>(i)] = sineAt(i, 440.0f, 0.2f);
        }
        body.process(in.data(), out.data(), n);
        float diff = 0.0f;
        for (int i = 0; i < n; ++i) {
            diff += std::fabs(out[static_cast<size_t>(i)] - in[static_cast<size_t>(i)]);
        }
        check(impulseOut[0] == 1.0f && allFinite(out) && maxAbs(out) < 1.5f && diff / n > 0.01f,
              "chorus_keeps_dry_and_moves");
    }

    {
        Knobs knobs;
        knobs.mix = 0.8f;
        knobs.time = 0.6f;
        knobs.tone = 0.55f;
        knobs.level = 1.0f;
        Pedal pedal = make(Effect::Reverb, knobs);
        const int n = 48000 * 3;
        std::vector<float> in(static_cast<size_t>(n), 0.0f), out(static_cast<size_t>(n));
        in[0] = 1.0f;
        pedal.process(in.data(), out.data(), n);
        auto rms = [&](int a, int b) {
            double s = 0.0;
            const int count = b - a;
            for (int i = a; i < b; ++i) {
                const double v = out[static_cast<size_t>(i)];
                s += v * v;
            }
            return std::sqrt(s / count);
        };
        const double early = rms(static_cast<int>(0.15 * 48000), static_cast<int>(0.5 * 48000));
        const double late = rms(static_cast<int>(2.5 * 48000), static_cast<int>(3.0 * 48000));
        const bool ok = out[0] == 1.0f && allFinite(out) && maxAbs(out) < 4.0f && early > 1.0e-4 && early > late;
        check(ok, "reverb_tail_decays");
        if (!ok) {
            std::cout << "  reverb early " << early << " late " << late << " peak " << maxAbs(out) << "\n";
        }
    }

    {
        const float splitKnob = (180.0f - 80.0f) / (250.0f - 80.0f);
        auto runNote = [&](float freq, std::vector<float>& in, std::vector<float>& out, float& tracked) {
            Knobs knobs;
            knobs.time = splitKnob;
            knobs.mix = 1.0f;
            knobs.tone = 0.5f;
            knobs.level = 1.0f;
            Pedal pedal = make(Effect::BassSplit, knobs);
            const int n = 48000 * 2;
            in.assign(static_cast<size_t>(n), 0.0f);
            out.assign(static_cast<size_t>(n), 0.0f);
            for (int i = 0; i < n; ++i) {
                in[static_cast<size_t>(i)] = sineAt(i, freq, 0.25f);
            }
            pedal.process(in.data(), out.data(), n);
            tracked = pedal.trackedHz();
        };
        std::vector<float> lowIn, lowOut, highIn, highOut;
        float lowHz = 0.0f;
        float highHz = 0.0f;
        runNote(100.0f, lowIn, lowOut, lowHz);
        runNote(400.0f, highIn, highOut, highHz);
        std::vector<float> diff(48000);
        for (int i = 0; i < 48000; ++i) {
            diff[static_cast<size_t>(i)] =
                lowOut[static_cast<size_t>(48000 + i)] - lowIn[static_cast<size_t>(48000 + i)];
        }
        const float mag50 = goertzel(diff.data(), 48000, 50.0f);
        const float mag100 = goertzel(diff.data(), 48000, 100.0f);
        bool highDry = true;
        for (size_t i = 0; i < highOut.size(); ++i) {
            if (highOut[i] != highIn[i]) {
                highDry = false;
                break;
            }
        }
        const bool lowOk = std::fabs(lowHz - 100.0f) < 8.0f && mag50 > 0.02f && mag50 > mag100;
        const bool highOk = std::fabs(highHz - 400.0f) < 15.0f && highDry;
        check(lowOk, "bass_low_note_adds_octave");
        check(highOk, "bass_high_note_stays_dry");
        if (!lowOk || !highOk) {
            std::cout << "  bass lowHz " << lowHz << " mag50 " << mag50 << " mag100 " << mag100
                      << " highHz " << highHz << "\n";
        }
    }

    {
        Knobs knobs;
        knobs.drive = 0.2f;
        knobs.mix = 0.5f;
        knobs.tone = 0.8f;
        knobs.level = 1.0f;
        Pedal pedal;
        pedal.setKnobs(knobs);
        pedal.prepare(48000);
        const int n = 48000;
        std::vector<float> in(static_cast<size_t>(n)), out(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            in[static_cast<size_t>(i)] = sineAt(i, 1000.0f, 0.2f);
        }
        pedal.process(in.data(), out.data(), 4000);
        pedal.request(Effect::Overdrive);
        pedal.process(in.data() + 4000, out.data() + 4000, n - 4000);
        float step = 0.0f;
        for (int i = 1; i < n; ++i) {
            step = std::max(step, std::fabs(out[static_cast<size_t>(i)] - out[static_cast<size_t>(i - 1)]));
        }
        check(pedal.current() == Effect::Overdrive && allFinite(out) && step < 0.2f, "crossfade_switches");
        if (!(pedal.current() == Effect::Overdrive && step < 0.2f)) {
            std::cout << "  step " << step << " effect " << stage1::effectName(pedal.current()) << "\n";
        }
    }

    {
        stage1::Footswitch sw;
        bool early = false;
        for (int i = 0; i < 10; ++i) {
            early = sw.update(true, 48000, 100) || early;
        }
        const bool pressed = sw.update(true, 48000, 500);
        const bool held = sw.update(true, 48000, 2000);
        stage1::Footswitch bounce;
        const bool tap = bounce.update(true, 48000, 200);
        const bool release = bounce.update(false, 48000, 2000);
        check(!early && pressed && !held && !tap && !release, "footswitch_debounce");
    }

    {
        const bool map = stage1::knobFromByte(0) == 0.0f && stage1::knobFromByte(255) == 1.0f &&
                         std::fabs(stage1::knobFromByte(128) - 128.0f / 255.0f) < 1.0e-6f;
        const bool cmd = stage1::ads7830Command(0) == 0x8C && stage1::ads7830Command(1) == 0xCC &&
                         stage1::ads7830Command(7) == 0xFC && stage1::ads7830Command(8) == -1;
        const float split = stage1::splitHzFromTime((180.0f - 80.0f) / (250.0f - 80.0f));
        check(map && cmd && std::fabs(split - 180.0f) < 0.2f, "knob_map_and_ads7830");
    }

    {
        Pedal pedal;
        pedal.prepare(48000);
        std::string err;
        const bool parsed = stage1::applyCommand(pedal, "effect delay", err);
        std::vector<float> silence(2000, 0.0f), out(2000, 0.0f);
        pedal.process(silence.data(), out.data(), 2000);
        const bool atDelay = pedal.current() == Effect::Delay;
        const Effect skipped = pedal.nextEffect();
        pedal.process(silence.data(), out.data(), 2000);
        check(parsed && atDelay && skipped == Effect::Chorus && pedal.current() == Effect::Chorus,
              "commands_change_effect");
    }

    if (g_failed != 0) {
        std::cout << g_failed << " effect checks failed\n";
    }
    return g_failed;
}
