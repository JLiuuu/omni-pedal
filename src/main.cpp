#include "pedal.hpp"
#include "stage1.hpp"
#include "wav.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#if OMNI_HAS_ALSA
#include <alsa/asoundlib.h>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace {

void makeOutDir() {
#ifdef _WIN32
    _mkdir("out");
#else
    mkdir("out", 0755);
#endif
}

float toneAt(int i, int rate) {
    const double phase = 2.0 * 3.14159265358979323846 * 1000.0 * static_cast<double>(i) /
                         static_cast<double>(rate);
    return 0.2f * static_cast<float>(std::sin(phase));
}

int renderBuffer(const std::vector<float>& in, int rate, const std::string& outPath, bool mute) {
    stage1::DryPath dry;
    dry.prepare(rate, mute ? stage1::kMuteSeconds : 0.0);
    std::vector<float> out(in.size());
    const int block = stage1::kPeriodFrames;
    for (size_t pos = 0; pos < in.size();) {
        const int n = static_cast<int>(std::min(static_cast<size_t>(block), in.size() - pos));
        dry.process(in.data() + pos, out.data() + pos, n);
        pos += static_cast<size_t>(n);
    }
    if (mute) {
        const int muted = static_cast<int>(std::llround(stage1::kMuteSeconds * rate));
        for (int i = 0; i < muted && i < static_cast<int>(out.size()); ++i) {
            if (out[static_cast<size_t>(i)] != 0.0f) {
                std::cerr << "demo check failed: sample " << i << " was not muted\n";
                return 1;
            }
        }
        for (int i = muted; i < static_cast<int>(out.size()); ++i) {
            if (out[static_cast<size_t>(i)] != in[static_cast<size_t>(i)]) {
                std::cerr << "demo check failed: sample " << i << " was shifted or changed\n";
                return 1;
            }
        }
    }
    std::string err;
    if (!writeWav16(outPath, out.data(), static_cast<int>(out.size()), rate, err)) {
        std::cerr << err << "\n";
        return 1;
    }
    return 0;
}

int renderEffectFile(const std::vector<float>& in, int rate, const std::string& outPath,
                      stage1::Effect effect, const stage1::Knobs& knobs, bool mute);
void pluckInto(std::vector<float>& dst, int start, float freq, float amp, int rate);
std::vector<float> guitarPhrase(int rate, int seconds);

int runDemo() {
    makeOutDir();
    const int rate = stage1::kSampleRate;
    const int n = rate * 5;
    std::vector<float> in(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        in[static_cast<size_t>(i)] = toneAt(i, rate);
    }

    std::string err;
    if (!writeWav16("out/input.wav", in.data(), n, rate, err)) {
        std::cerr << err << "\n";
        return 1;
    }
    if (renderBuffer(in, rate, "out/output.wav", true) != 0) {
        return 1;
    }
    if (renderBuffer(in, rate, "out/output_no_mute.wav", false) != 0) {
        return 1;
    }

    stage1::DryPath dry;
    dry.prepare(rate, stage1::kMuteSeconds);
    std::ofstream report("out/report.txt");
    if (!report) {
        std::cerr << "cannot write out/report.txt\n";
        return 1;
    }
    report << std::fixed << std::setprecision(2);
    stage1::printReport(report, rate, dry.muteLeft);
    report << "demo_seconds=5\n";
    report << "demo_tone_hz=1000\n";
    report << "demo_peak=0.2\n";
    report << "files=out/input.wav out/output.wav out/output_no_mute.wav\n";

    const auto phrase = guitarPhrase(rate, 4);
    stage1::Knobs od;
    od.drive = 0.55f;
    od.tone = 0.7f;
    od.mix = 1.0f;
    od.level = 0.9f;
    stage1::Knobs dist = od;
    dist.drive = 0.75f;
    dist.tone = 0.4f;
    dist.level = 0.8f;
    stage1::Knobs delay;
    delay.drive = 0.45f;
    delay.mix = 0.45f;
    delay.time = 0.25f;
    delay.level = 1.0f;
    stage1::Knobs chorus;
    chorus.tone = 0.65f;
    chorus.mix = 0.5f;
    chorus.time = 0.35f;
    chorus.level = 1.0f;
    stage1::Knobs reverb;
    reverb.mix = 0.4f;
    reverb.time = 0.55f;
    reverb.tone = 0.6f;
    reverb.level = 1.0f;
    if (renderEffectFile(phrase, rate, "out/overdrive.wav", stage1::Effect::Overdrive, od, false) != 0 ||
        renderEffectFile(phrase, rate, "out/distortion.wav", stage1::Effect::Distortion, dist, false) != 0 ||
        renderEffectFile(phrase, rate, "out/delay.wav", stage1::Effect::Delay, delay, false) != 0 ||
        renderEffectFile(phrase, rate, "out/chorus.wav", stage1::Effect::Chorus, chorus, false) != 0 ||
        renderEffectFile(phrase, rate, "out/reverb.wav", stage1::Effect::Reverb, reverb, false) != 0) {
        return 1;
    }

    stage1::Knobs bass;
    bass.time = (180.0f - 80.0f) / (250.0f - 80.0f);
    bass.mix = 1.0f;
    bass.tone = 0.5f;
    bass.level = 1.0f;
    const int bassN = rate * 3;
    std::vector<float> low(static_cast<size_t>(bassN)), high(static_cast<size_t>(bassN));
    for (int i = 0; i < bassN; ++i) {
        low[static_cast<size_t>(i)] = 0.3f * std::sin(2.0 * 3.14159265358979323846 * 110.0 * i / rate);
        high[static_cast<size_t>(i)] = 0.3f * std::sin(2.0 * 3.14159265358979323846 * 330.0 * i / rate);
    }
    if (renderEffectFile(low, rate, "out/bass_low.wav", stage1::Effect::BassSplit, bass, false) != 0 ||
        renderEffectFile(high, rate, "out/bass_high.wav", stage1::Effect::BassSplit, bass, false) != 0) {
        return 1;
    }

    {
        stage1::Pedal pedal;
        stage1::Knobs mild;
        mild.drive = 0.4f;
        mild.mix = 0.55f;
        mild.tone = 0.6f;
        mild.time = 0.3f;
        mild.level = 0.9f;
        pedal.setKnobs(mild);
        pedal.prepare(rate);
        const int switchN = rate * 7;
        std::vector<float> in = guitarPhrase(rate, 7);
        std::vector<float> out(static_cast<size_t>(switchN));
        const int block = 480;
        for (int pos = 0; pos < switchN; pos += block) {
            if (pos > 0 && pos % rate == 0) {
                pedal.nextEffect();
            }
            pedal.process(in.data() + pos, out.data() + pos, block);
        }
        if (!writeWav16("out/switch.wav", out.data(), switchN, rate, err)) {
            std::cerr << err << "\n";
            return 1;
        }
    }

    report << "effects=overdrive distortion delay chorus reverb bass\n";
    report << "files=out/overdrive.wav out/distortion.wav out/delay.wav out/chorus.wav "
              "out/reverb.wav out/bass_low.wav out/bass_high.wav out/switch.wav\n";

    std::cout << "dry passthrough and mute checked\n";
    std::cout << "wrote effect demos in out/: overdrive distortion delay chorus reverb\n";
    std::cout << "bass_low.wav is 110 Hz with the octave; bass_high.wav is 330 Hz and stays dry\n";
    std::cout << "switch.wav changes effect once per second\n";
    return 0;
}

int renderEffectFile(const std::vector<float>& in, int rate, const std::string& outPath,
                      stage1::Effect effect, const stage1::Knobs& knobs, bool mute) {
    stage1::Pedal pedal;
    pedal.setKnobs(knobs);
    pedal.prepare(rate);
    pedal.cutTo(effect);
    stage1::DryPath dry;
    dry.prepare(rate, mute ? stage1::kMuteSeconds : 0.0);
    std::vector<float> out(in.size());
    const int block = 480;
    for (size_t pos = 0; pos < in.size();) {
        const int n = static_cast<int>(std::min(static_cast<size_t>(block), in.size() - pos));
        pedal.process(in.data() + pos, out.data() + pos, n);
        dry.process(out.data() + pos, out.data() + pos, n);
        pos += static_cast<size_t>(n);
    }
    std::string err;
    if (!writeWav16(outPath, out.data(), static_cast<int>(out.size()), rate, err)) {
        std::cerr << err << "\n";
        return 1;
    }
    return 0;
}

void pluckInto(std::vector<float>& dst, int start, float freq, float amp, int rate) {
    if (start < 0 || start >= static_cast<int>(dst.size())) {
        return;
    }
    const int period = std::max(2, static_cast<int>(std::lround(static_cast<double>(rate) / freq)));
    std::vector<float> buf(static_cast<size_t>(period));
    unsigned rng = 1;
    for (int i = 0; i < period; ++i) {
        rng = rng * 1664525u + 1013904223u;
        buf[static_cast<size_t>(i)] = static_cast<float>(static_cast<int>((rng >> 16) & 0xffff) - 32768) / 32768.0f;
    }
    float prev = 0.0f;
    int idx = 0;
    for (int i = start; i < static_cast<int>(dst.size()); ++i) {
        const float y = buf[static_cast<size_t>(idx)];
        const float avg = 0.5f * (y + prev);
        prev = y;
        buf[static_cast<size_t>(idx)] = avg * 0.996f;
        idx = (idx + 1) % period;
        dst[static_cast<size_t>(i)] += y * amp;
    }
}

std::vector<float> guitarPhrase(int rate, int seconds) {
    std::vector<float> in(static_cast<size_t>(rate * seconds), 0.0f);
    const float notes[] = {196.0f, 247.0f, 294.0f, 220.0f};
    for (int i = 0; i < 4; ++i) {
        pluckInto(in, i * rate * seconds / 4, notes[i], 0.45f, rate);
    }
    return in;
}

int renderFile(const std::string& inPath, const std::string& outPath, stage1::Effect effect,
               const stage1::Knobs& knobs, bool mute) {
    Wav wav;
    std::string err;
    if (!readWav(inPath, wav, err)) {
        std::cerr << err << "\n";
        return 1;
    }
    std::cout << "read " << wav.samples.size() << " samples at " << wav.sampleRate << " Hz, effect "
              << stage1::effectName(effect) << "\n";
    return renderEffectFile(wav.samples, wav.sampleRate, outPath, effect, knobs, mute);
}

void printUsage() {
    std::cout << "stage1                         write demo wavs in out/\n";
    std::cout << "stage1 in.wav out.wav          dry signal, 3 s mute\n";
    std::cout << "stage1 --effect bass --mix 0.8 in.wav out.wav\n";
    std::cout << "stage1 --live [device] [--gpio 17] [--i2c 1]\n";
    std::cout << "  commands on the Pi: effect overdrive | drive 0.5 | next\n";
}

#if OMNI_HAS_ALSA

volatile std::sig_atomic_t g_stop = 0;

void onSignal(int) {
    g_stop = 1;
}

int configure(snd_pcm_t* pcm, unsigned& rate, snd_pcm_uframes_t& period, snd_pcm_uframes_t& buffer, std::string& err) {
    snd_pcm_hw_params_t* hw = nullptr;
    snd_pcm_hw_params_alloca(&hw);
    if (snd_pcm_hw_params_any(pcm, hw) < 0 ||
        snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED) < 0 ||
        snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S32_LE) < 0 ||
        snd_pcm_hw_params_set_channels(pcm, hw, 2) < 0 ||
        snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, nullptr) < 0 ||
        snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, nullptr) < 0) {
        err = "ALSA refused the requested format";
        return -1;
    }
    buffer = period * static_cast<snd_pcm_uframes_t>(stage1::kPeriods);
    if (snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer) < 0 ||
        snd_pcm_hw_params(pcm, hw) < 0) {
        err = snd_strerror(errno);
        return -1;
    }
    snd_pcm_hw_params_get_rate(hw, &rate, nullptr);
    snd_pcm_hw_params_get_period_size(hw, &period, nullptr);
    snd_pcm_hw_params_get_buffer_size(hw, &buffer);
    if (snd_pcm_prepare(pcm) < 0) {
        err = "snd_pcm_prepare failed";
        return -1;
    }
    return 0;
}

int recover(snd_pcm_t* pcm, int rc) {
    return snd_pcm_recover(pcm, rc, 1);
}

int runLive(const std::string& device, int gpioPin, int i2cBus, int i2cAddr) {
    snd_pcm_t* cap = nullptr;
    snd_pcm_t* play = nullptr;
    if (snd_pcm_open(&cap, device.c_str(), SND_PCM_STREAM_CAPTURE, 0) < 0 ||
        snd_pcm_open(&play, device.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        std::cerr << "cannot open ALSA device " << device << "\n";
        std::cerr << "try: arecord -l    then    ./stage1 --live hw:0\n";
        return 1;
    }

    unsigned rate = static_cast<unsigned>(stage1::kSampleRate);
    snd_pcm_uframes_t period = static_cast<snd_pcm_uframes_t>(stage1::kPeriodFrames);
    snd_pcm_uframes_t buffer = period * static_cast<snd_pcm_uframes_t>(stage1::kPeriods);
    std::string err;
    if (configure(cap, rate, period, buffer, err) < 0 ||
        configure(play, rate, period, buffer, err) < 0) {
        std::cerr << "ALSA configure: " << err << "\n";
        return 1;
    }

    const int periods = period > 0 ? static_cast<int>(buffer / period) : 0;
    const auto L = stage1::latencyFor(static_cast<int>(rate), static_cast<int>(period), periods);
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "device " << device << "\n";
    std::cout << "actual rate " << rate << "  period " << period
              << " frames (" << L.periodMs << " ms)  buffer " << buffer << "\n";
    std::cout << "software round-trip upper bound " << L.softwareRoundTripMs << " ms\n";
    if (L.softwareRoundTripMs >= 5.0) {
        std::cout << "warning: software buffer upper bound is no longer under 5 ms\n";
    }
    std::cout << "dry bypass adds 0 samples. Type: effect overdrive   or   next\n";
    const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }

    sched_param sp{};
    sp.sched_priority = 60;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
        std::cout << "SCHED_FIFO not set. For the real measurement: sudo chrt -f 60 ./stage1 --live\n";
    }

    snd_pcm_link(cap, play);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    stage1::Pedal pedal;
    pedal.prepare(static_cast<int>(rate));
    stage1::DryPath dry;
    dry.prepare(static_cast<int>(rate), stage1::kMuteSeconds);
    stage1::ControlIo controls;
    if (gpioPin >= 0) {
        if (!controls.openSwitch(gpioPin, err)) {
            std::cerr << err << "\n";
        }
    }
    if (i2cBus >= 0) {
        if (!controls.openPots(i2cBus, i2cAddr, err)) {
            std::cerr << err << "\n";
        }
    }
    std::vector<int32_t> in(period * 2);
    std::vector<int32_t> out(period * 2);
    std::vector<float> mono(period);
    std::vector<float> processed(period);
    int sincePots = 0;
    std::string command;
    long long seen = 0;
    int lastPrint = -1;

    while (!g_stop) {
        snd_pcm_sframes_t got = snd_pcm_readi(cap, in.data(), period);
        if (got < 0) {
            recover(cap, static_cast<int>(got));
            recover(play, -EPIPE);
            continue;
        }
        const int n = static_cast<int>(got);
        for (int i = 0; i < n; ++i) {
            mono[static_cast<size_t>(i)] =
                static_cast<float>(static_cast<double>(in[static_cast<size_t>(i * 2)]) / 2147483648.0);
        }
        pedal.process(mono.data(), processed.data(), n);
        dry.process(processed.data(), processed.data(), n);
        sincePots += n;
        if (sincePots >= static_cast<int>(rate) / 50) {
            sincePots = 0;
            stage1::Knobs knobs = pedal.knobs();
            if (controls.readPots(knobs)) {
                pedal.setKnobs(knobs);
            }
        }
        if (controls.pollSwitch(static_cast<int>(rate), n)) {
            const stage1::Effect next = pedal.nextEffect();
            std::cout << "effect " << stage1::effectName(next) << "\n";
        }
        char keys[64];
        const ssize_t gotKeys = read(STDIN_FILENO, keys, sizeof(keys));
        if (gotKeys > 0) {
            for (ssize_t c = 0; c < gotKeys; ++c) {
                if (keys[c] == '\n') {
                    std::string cmdErr;
                    if (!command.empty() && !stage1::applyCommand(pedal, command, cmdErr)) {
                        std::cout << cmdErr << "\n";
                    } else if (!command.empty()) {
                        std::cout << "effect " << stage1::effectName(pedal.current()) << "\n";
                    }
                    command.clear();
                } else if (keys[c] != '\r') {
                    command.push_back(keys[c]);
                }
            }
        }
        for (int i = 0; i < n; ++i) {
            float y = processed[static_cast<size_t>(i)];
            if (y > 1.0f) {
                y = 1.0f;
            } else if (y < -1.0f) {
                y = -1.0f;
            }
            const int32_t s = static_cast<int32_t>(std::lrint(static_cast<double>(y) * 2147483647.0));
            out[static_cast<size_t>(i * 2)] = s;
            out[static_cast<size_t>(i * 2 + 1)] = s;
        }
        snd_pcm_sframes_t wrote = snd_pcm_writei(play, out.data(), static_cast<snd_pcm_uframes_t>(n));
        if (wrote < 0) {
            recover(play, static_cast<int>(wrote));
            recover(cap, -EPIPE);
        }
        seen += n;
        const int sec = static_cast<int>(seen / static_cast<long long>(rate));
        if (sec != lastPrint) {
            lastPrint = sec;
            if (dry.muteLeft > 0) {
                std::cout << "mute " << std::setprecision(1)
                          << (static_cast<double>(dry.muteLeft) / static_cast<double>(rate))
                          << " s left\n";
            } else if (sec == static_cast<int>(stage1::kMuteSeconds)) {
                std::cout << "mute finished, dry passthrough\n";
            }
        }
    }

    snd_pcm_close(cap);
    snd_pcm_close(play);
    return 0;
}

#else

int runLive(const std::string&, int, int, int) {
    std::cerr << "Live mode is for the Raspberry Pi.\n";
    std::cerr << "On this PC the progress demo is: ./stage1\n";
    return 1;
}

#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        return runDemo();
    }
    bool live = false;
    std::string device = "hw:CodecZero";
    int gpioPin = -1;
    int i2cBus = -1;
    int i2cAddr = 0x48;
    bool mute = true;
    bool effectSet = false;
    stage1::Effect effect = stage1::Effect::Bypass;
    stage1::Knobs knobs;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << name << " needs a value\n";
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--live") {
            live = true;
            if (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0) {
                device = argv[++i];
            }
        } else if (arg == "--gpio") {
            const char* v = need("--gpio");
            if (v == nullptr) {
                return 1;
            }
            gpioPin = std::atoi(v);
        } else if (arg == "--i2c") {
            const char* v = need("--i2c");
            if (v == nullptr) {
                return 1;
            }
            i2cBus = std::atoi(v);
        } else if (arg == "--addr") {
            const char* v = need("--addr");
            if (v == nullptr) {
                return 1;
            }
            i2cAddr = static_cast<int>(std::strtol(v, nullptr, 0));
        } else if (arg == "--effect") {
            const char* v = need("--effect");
            if (v == nullptr || !stage1::parseEffect(v, effect)) {
                std::cerr << "effect is bypass, overdrive, distortion, delay, chorus, reverb, bass\n";
                return 1;
            }
            effectSet = true;
            mute = false;
        } else if (arg == "--drive" || arg == "--tone" || arg == "--mix" || arg == "--time" || arg == "--level") {
            const char* v = need(arg.c_str());
            if (v == nullptr) {
                return 1;
            }
            const float value = std::strtof(v, nullptr);
            if (arg == "--drive") {
                knobs.drive = value;
            } else if (arg == "--tone") {
                knobs.tone = value;
            } else if (arg == "--mix") {
                knobs.mix = value;
            } else if (arg == "--time") {
                knobs.time = value;
            } else {
                knobs.level = value;
            }
        } else if (arg == "--mute") {
            mute = true;
        } else {
            files.push_back(arg);
        }
    }
    if (live) {
        return runLive(device, gpioPin, i2cBus, i2cAddr);
    }
    if (files.size() == 2) {
        if (!effectSet) {
            effect = stage1::Effect::Bypass;
            mute = true;
        }
        return renderFile(files[0], files[1], effect, knobs, mute);
    }
    printUsage();
    return 1;
}
