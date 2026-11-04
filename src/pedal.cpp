#include "pedal.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

#if OMNI_HAS_ALSA
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace stage1 {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float clampf(float x, float lo, float hi) {
    return std::max(lo, std::min(hi, x));
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

}  // namespace

struct PedalState {
    int sr = 48000;
    std::atomic<float> drive{0.35f};
    std::atomic<float> tone{0.65f};
    std::atomic<float> mix{0.45f};
    std::atomic<float> time{0.35f};
    std::atomic<float> level{1.0f};
    std::atomic<int> requested{0};

    Effect active = Effect::Bypass;
    Effect incoming = Effect::Bypass;
    bool fading = false;
    int fadePos = 0;
    int fadeLen = 480;

    std::vector<float> bufA;
    std::vector<float> bufB;

    float odLp = 0.0f;
    float distLp = 0.0f;
    float distX = 0.0f;
    float distY = 0.0f;
    float distR = 0.995f;

    std::vector<float> delayBuf;
    int delayW = 0;

    std::vector<float> chorusBuf;
    int chorusW = 0;
    float chorusPhase = 0.0f;

    struct Comb {
        std::vector<float> buf;
        int idx = 0;
        float store = 0.0f;
    };
    struct Allpass {
        std::vector<float> buf;
        int idx = 0;
    };
    Comb combs[4];
    Allpass aps[2];

    float trackLp = 0.0f;
    float trackA = 0.05f;
    float bassLp = 0.0f;
    float peak = 0.0f;
    float freq = 1000.0f;
    float gate = 0.0f;
    bool schmitt = false;
    bool ff = false;
    int since = 0;
    int valid = 0;
};

namespace {

Knobs loadKnobs(const PedalState& impl) {
    Knobs k;
    k.drive = clampf(impl.drive.load(), 0.0f, 1.0f);
    k.tone = clampf(impl.tone.load(), 0.0f, 1.0f);
    k.mix = clampf(impl.mix.load(), 0.0f, 1.0f);
    k.time = clampf(impl.time.load(), 0.0f, 1.0f);
    k.level = clampf(impl.level.load(), 0.0f, 1.0f);
    return k;
}

void clearVec(std::vector<float>& v) {
    std::fill(v.begin(), v.end(), 0.0f);
}

int scaledLen(int len44100, int sr) {
    const int n = static_cast<int>(std::lround(static_cast<double>(len44100) * sr / 44100.0));
    return std::max(8, n);
}

void renderBypass(const float* in, float* out, int n, float level) {
    for (int i = 0; i < n; ++i) {
        out[i] = in[i] * level;
    }
}

void renderOverdrive(PedalState& st, const float* in, float* out, int n, const Knobs& k) {
    const float drive = lerp(1.5f, 12.0f, k.drive);
    const float norm = std::tanh(drive);
    const float toneHz = lerp(700.0f, 6000.0f, k.tone);
    const float a = 1.0f - std::exp(-2.0f * kPi * toneHz / static_cast<float>(st.sr));
    for (int i = 0; i < n; ++i) {
        const float x = in[i];
        float y = std::tanh(x * drive) / norm;
        st.odLp += a * (y - st.odLp);
        out[i] = k.level * ((1.0f - k.mix) * x + k.mix * st.odLp);
    }
}

void renderDistortion(PedalState& st, const float* in, float* out, int n, const Knobs& k) {
    const float drive = lerp(4.0f, 40.0f, k.drive);
    const float toneHz = lerp(500.0f, 3500.0f, k.tone);
    const float a = 1.0f - std::exp(-2.0f * kPi * toneHz / static_cast<float>(st.sr));
    for (int i = 0; i < n; ++i) {
        const float x = in[i];
        const float shaped = (x * drive) / (1.0f + std::fabs(x * drive));
        const float hp = shaped - st.distX + st.distR * st.distY;
        st.distX = shaped;
        st.distY = hp;
        st.distLp += a * (hp - st.distLp);
        out[i] = k.level * ((1.0f - k.mix) * x + k.mix * st.distLp);
    }
}

void renderDelay(PedalState& st, const float* in, float* out, int n, const Knobs& k) {
    const int len = static_cast<int>(st.delayBuf.size());
    if (len < 2) {
        renderBypass(in, out, n, k.level);
        return;
    }
    int d = delaySamples(k.time, st.sr);
    d = std::max(1, std::min(d, len - 1));
    const float fb = lerp(0.05f, 0.72f, k.drive);
    for (int i = 0; i < n; ++i) {
        int read = st.delayW - d;
        if (read < 0) {
            read += len;
        }
        const float delayed = st.delayBuf[static_cast<size_t>(read)];
        st.delayBuf[static_cast<size_t>(st.delayW)] = in[i] + delayed * fb;
        st.delayW += 1;
        if (st.delayW >= len) {
            st.delayW = 0;
        }
        out[i] = k.level * (in[i] + k.mix * delayed);
    }
}

float readFrac(const std::vector<float>& buf, int write, float delay, int len) {
    float pos = static_cast<float>(write) - delay;
    while (pos < 0.0f) {
        pos += static_cast<float>(len);
    }
    const int i0 = static_cast<int>(pos) % len;
    const float frac = pos - static_cast<float>(static_cast<int>(pos));
    const int i1 = (i0 + 1) % len;
    return buf[static_cast<size_t>(i0)] * (1.0f - frac) + buf[static_cast<size_t>(i1)] * frac;
}

void renderChorus(PedalState& st, const float* in, float* out, int n, const Knobs& k) {
    const int len = static_cast<int>(st.chorusBuf.size());
    if (len < 2) {
        renderBypass(in, out, n, k.level);
        return;
    }
    const float rateHz = lerp(0.3f, 3.5f, k.time);
    const float depth = (0.002f + 0.006f * k.tone) * static_cast<float>(st.sr);
    const float base = 0.015f * static_cast<float>(st.sr);
    for (int i = 0; i < n; ++i) {
        st.chorusPhase += rateHz / static_cast<float>(st.sr);
        if (st.chorusPhase >= 1.0f) {
            st.chorusPhase -= 1.0f;
        }
        const float lfo = std::sin(2.0f * kPi * st.chorusPhase);
        float delay = base + lfo * depth;
        delay = clampf(delay, 1.0f, static_cast<float>(len - 2));
        const float delayed = readFrac(st.chorusBuf, st.chorusW, delay, len);
        st.chorusBuf[static_cast<size_t>(st.chorusW)] = in[i];
        st.chorusW += 1;
        if (st.chorusW >= len) {
            st.chorusW = 0;
        }
        out[i] = k.level * (in[i] + k.mix * delayed);
    }
}

float processComb(PedalState::Comb& c, float in, float feedback, float damp) {
    const float y = c.buf[static_cast<size_t>(c.idx)];
    c.store = y * (1.0f - damp) + c.store * damp;
    if (std::fabs(c.store) < 1.0e-12f) {
        c.store = 0.0f;
    }
    c.buf[static_cast<size_t>(c.idx)] = in + c.store * feedback;
    c.idx += 1;
    if (c.idx >= static_cast<int>(c.buf.size())) {
        c.idx = 0;
    }
    return y;
}

float processAllpass(PedalState::Allpass& a, float in) {
    const float bufout = a.buf[static_cast<size_t>(a.idx)];
    const float y = -in + bufout;
    a.buf[static_cast<size_t>(a.idx)] = in + bufout * 0.5f;
    a.idx += 1;
    if (a.idx >= static_cast<int>(a.buf.size())) {
        a.idx = 0;
    }
    return y;
}

void renderReverb(PedalState& st, const float* in, float* out, int n, const Knobs& k) {
    const float feedback = lerp(0.55f, 0.84f, k.time);
    const float damp = lerp(0.65f, 0.12f, k.tone);
    for (int i = 0; i < n; ++i) {
        const float x = in[i];
        const float excite = x * 0.20f;
        float wet = 0.0f;
        for (auto& comb : st.combs) {
            wet += processComb(comb, excite, feedback, damp);
        }
        wet *= 0.25f;
        for (auto& ap : st.aps) {
            wet = processAllpass(ap, wet);
        }
        out[i] = k.level * (x + k.mix * wet);
    }
}

void renderBass(PedalState& st, const float* in, float* out, int n, const Knobs& k) {
    const float split = splitHzFromTime(k.time);
    const float bassHz = lerp(90.0f, 240.0f, k.tone);
    const float bassA = 1.0f - std::exp(-2.0f * kPi * bassHz / static_cast<float>(st.sr));
    const float gateA = 1.0f - std::exp(-1.0f / (0.03f * static_cast<float>(st.sr)));
    const int minP = std::max(2, st.sr / 2000);
    const int maxP = std::max(minP + 1, st.sr / 40);
    for (int i = 0; i < n; ++i) {
        const float x = in[i];
        st.trackLp += st.trackA * (x - st.trackLp);
        const float ax = std::fabs(st.trackLp);
        const float att = ax > st.peak ? 0.05f : 0.0008f;
        st.peak += att * (ax - st.peak);
        const float thresh = std::max(0.004f, st.peak * 0.35f);
        bool rising = false;
        if (!st.schmitt && st.trackLp > thresh) {
            st.schmitt = true;
            rising = true;
        } else if (st.schmitt && st.trackLp < -thresh) {
            st.schmitt = false;
        }
        st.since += 1;
        if (rising) {
            const int period = st.since;
            st.since = 0;
            if (period >= minP && period <= maxP) {
                st.valid += 1;
                const float f = static_cast<float>(st.sr) / static_cast<float>(period);
                st.freq += 0.35f * (f - st.freq);
            }
            st.ff = !st.ff;
        }
        const bool trust = st.valid >= 2;
        const float target =
            (trust && st.freq < split && st.freq > 45.0f && st.peak > 0.01f) ? 1.0f : 0.0f;
        st.gate += gateA * (target - st.gate);
        const float square = st.ff ? st.peak : -st.peak;
        st.bassLp += bassA * (square - st.bassLp);
        const float bass = st.bassLp * 0.55f;
        out[i] = k.level * (x + k.mix * st.gate * bass);
    }
}

void renderEffect(PedalState& st, Effect effect, const float* in, float* out, int n, const Knobs& k) {
    switch (effect) {
    case Effect::Overdrive:
        renderOverdrive(st, in, out, n, k);
        break;
    case Effect::Distortion:
        renderDistortion(st, in, out, n, k);
        break;
    case Effect::Delay:
        renderDelay(st, in, out, n, k);
        break;
    case Effect::Chorus:
        renderChorus(st, in, out, n, k);
        break;
    case Effect::Reverb:
        renderReverb(st, in, out, n, k);
        break;
    case Effect::BassSplit:
        renderBass(st, in, out, n, k);
        break;
    case Effect::Bypass:
    case Effect::Count:
    default:
        renderBypass(in, out, n, k.level);
        break;
    }
}

}  // namespace

const char* effectName(Effect effect) {
    switch (effect) {
    case Effect::Bypass:
        return "bypass";
    case Effect::Overdrive:
        return "overdrive";
    case Effect::Distortion:
        return "distortion";
    case Effect::Delay:
        return "delay";
    case Effect::Chorus:
        return "chorus";
    case Effect::Reverb:
        return "reverb";
    case Effect::BassSplit:
        return "bass";
    case Effect::Count:
        break;
    }
    return "bypass";
}

bool parseEffect(const std::string& text, Effect& effect) {
    if (text == "bypass" || text == "dry") {
        effect = Effect::Bypass;
    } else if (text == "overdrive" || text == "od") {
        effect = Effect::Overdrive;
    } else if (text == "distortion" || text == "dist") {
        effect = Effect::Distortion;
    } else if (text == "delay") {
        effect = Effect::Delay;
    } else if (text == "chorus") {
        effect = Effect::Chorus;
    } else if (text == "reverb") {
        effect = Effect::Reverb;
    } else if (text == "bass" || text == "basssplit" || text == "split") {
        effect = Effect::BassSplit;
    } else {
        return false;
    }
    return true;
}

float knobFromByte(int raw) {
    if (raw < 0) {
        raw = 0;
    }
    if (raw > 255) {
        raw = 255;
    }
    return static_cast<float>(raw) / 255.0f;
}

int ads7830Command(int channel) {
    static const int kCmd[8] = {0x8C, 0xCC, 0x9C, 0xDC, 0xAC, 0xEC, 0xBC, 0xFC};
    if (channel < 0 || channel > 7) {
        return -1;
    }
    return kCmd[channel];
}

int delaySamples(float time01, int sampleRate) {
    const float t = clampf(time01, 0.0f, 1.0f);
    const double minD = 0.04 * static_cast<double>(sampleRate);
    const double maxD = 0.80 * static_cast<double>(sampleRate);
    return static_cast<int>(std::llround(minD + static_cast<double>(t) * (maxD - minD)));
}

float splitHzFromTime(float time01) {
    return lerp(80.0f, 250.0f, clampf(time01, 0.0f, 1.0f));
}

Pedal::Pedal() : impl_(new PedalState) {}

Pedal::Pedal(Pedal&& other) noexcept : impl_(other.impl_) {
    other.impl_ = nullptr;
}

Pedal::~Pedal() {
    delete impl_;
}

void Pedal::prepare(int sampleRate) {
    PedalState& st = *impl_;
    st.sr = sampleRate > 0 ? sampleRate : 48000;
    st.fadeLen = std::max(1, static_cast<int>(std::llround(0.010 * st.sr)));
    st.bufA.assign(256, 0.0f);
    st.bufB.assign(256, 0.0f);
    st.delayBuf.assign(static_cast<size_t>(st.sr), 0.0f);
    st.delayW = 0;
    st.chorusBuf.assign(4096, 0.0f);
    st.chorusW = 0;
    st.chorusPhase = 0.0f;
    st.odLp = 0.0f;
    st.distLp = 0.0f;
    st.distX = 0.0f;
    st.distY = 0.0f;
    st.distR = 1.0f - (2.0f * kPi * 10.0f / static_cast<float>(st.sr));
    st.trackA = 1.0f - std::exp(-2.0f * kPi * 400.0f / static_cast<float>(st.sr));
    st.trackLp = 0.0f;
    st.bassLp = 0.0f;
    st.peak = 0.0f;
    st.freq = 1000.0f;
    st.gate = 0.0f;
    st.schmitt = false;
    st.ff = false;
    st.since = 0;
    st.valid = 0;
    const int combLen[4] = {1116, 1188, 1277, 1356};
    const int apLen[2] = {225, 556};
    for (int i = 0; i < 4; ++i) {
        st.combs[i].buf.assign(static_cast<size_t>(scaledLen(combLen[i], st.sr)), 0.0f);
        st.combs[i].idx = 0;
        st.combs[i].store = 0.0f;
    }
    for (int i = 0; i < 2; ++i) {
        st.aps[i].buf.assign(static_cast<size_t>(scaledLen(apLen[i], st.sr)), 0.0f);
        st.aps[i].idx = 0;
    }
    st.active = Effect::Bypass;
    st.incoming = Effect::Bypass;
    st.fading = false;
    st.fadePos = 0;
    st.requested.store(static_cast<int>(Effect::Bypass));
}

void Pedal::setKnobs(const Knobs& knobs) {
    impl_->drive.store(clampf(knobs.drive, 0.0f, 1.0f));
    impl_->tone.store(clampf(knobs.tone, 0.0f, 1.0f));
    impl_->mix.store(clampf(knobs.mix, 0.0f, 1.0f));
    impl_->time.store(clampf(knobs.time, 0.0f, 1.0f));
    impl_->level.store(clampf(knobs.level, 0.0f, 1.0f));
}

Knobs Pedal::knobs() const {
    return loadKnobs(*impl_);
}

void Pedal::resetOne(Effect effect) {
    PedalState& st = *impl_;
    if (effect == Effect::Overdrive) {
        st.odLp = 0.0f;
    } else if (effect == Effect::Distortion) {
        st.distLp = 0.0f;
        st.distX = 0.0f;
        st.distY = 0.0f;
    } else if (effect == Effect::Delay) {
        clearVec(st.delayBuf);
        st.delayW = 0;
    } else if (effect == Effect::Chorus) {
        clearVec(st.chorusBuf);
        st.chorusW = 0;
        st.chorusPhase = 0.0f;
    } else if (effect == Effect::Reverb) {
        for (auto& comb : st.combs) {
            clearVec(comb.buf);
            comb.idx = 0;
            comb.store = 0.0f;
        }
        for (auto& ap : st.aps) {
            clearVec(ap.buf);
            ap.idx = 0;
        }
    } else if (effect == Effect::BassSplit) {
        st.trackLp = 0.0f;
        st.bassLp = 0.0f;
        st.peak = 0.0f;
        st.freq = 1000.0f;
        st.gate = 0.0f;
        st.schmitt = false;
        st.ff = false;
        st.since = 0;
        st.valid = 0;
    }
}

void Pedal::cutTo(Effect effect) {
    if (effect == Effect::Count) {
        effect = Effect::Bypass;
    }
    resetOne(effect);
    impl_->active = effect;
    impl_->incoming = effect;
    impl_->fading = false;
    impl_->fadePos = 0;
    impl_->requested.store(static_cast<int>(effect));
}

void Pedal::request(Effect effect) {
    if (effect == Effect::Count) {
        effect = Effect::Bypass;
    }
    impl_->requested.store(static_cast<int>(effect));
}

Effect Pedal::nextEffect() {
    const int count = static_cast<int>(Effect::Count);
    const int cur = impl_->requested.load();
    const int next = (cur + 1) % count;
    impl_->requested.store(next);
    return static_cast<Effect>(next);
}

Effect Pedal::current() const {
    return impl_->active;
}

float Pedal::trackedHz() const {
    return impl_->freq;
}

void Pedal::process(const float* in, float* out, int n) {
    if (n <= 0 || in == nullptr || out == nullptr) {
        return;
    }
    PedalState& st = *impl_;
    if (st.bufA.empty()) {
        renderBypass(in, out, n, 1.0f);
        return;
    }
    const Knobs k = loadKnobs(st);
    int done = 0;
    while (done < n) {
        const int chunk = std::min(256, n - done);
        const float* cin = in + done;
        float* cout = out + done;
        int i = 0;
        while (i < chunk) {
            const Effect req = static_cast<Effect>(st.requested.load());
            if (!st.fading && req != st.active) {
                st.incoming = req;
                resetOne(req);
                st.fading = true;
                st.fadePos = 0;
            }
            if (!st.fading) {
                const int run = chunk - i;
                renderEffect(st, st.active, cin + i, cout + i, run, k);
                i += run;
                continue;
            }
            int run = st.fadeLen - st.fadePos;
            if (run > chunk - i) {
                run = chunk - i;
            }
            if (run < 1) {
                run = 1;
            }
            renderEffect(st, st.active, cin + i, st.bufA.data(), run, k);
            renderEffect(st, st.incoming, cin + i, st.bufB.data(), run, k);
            for (int j = 0; j < run; ++j) {
                float g = static_cast<float>(st.fadePos + j) / static_cast<float>(st.fadeLen);
                if (g > 1.0f) {
                    g = 1.0f;
                }
                cout[i + j] = st.bufA[static_cast<size_t>(j)] * (1.0f - g) +
                              st.bufB[static_cast<size_t>(j)] * g;
            }
            st.fadePos += run;
            if (st.fadePos >= st.fadeLen) {
                st.fading = false;
                st.active = st.incoming;
            }
            i += run;
        }
        done += chunk;
    }
}

bool Footswitch::update(bool rawPressed, int sampleRate, int samplesAdvanced) {
    if (samplesAdvanced < 0) {
        samplesAdvanced = 0;
    }
    if (rawPressed == lastRaw_) {
        pending_ += samplesAdvanced;
    } else {
        lastRaw_ = rawPressed;
        pending_ = samplesAdvanced;
    }
    const int need = std::max(1, static_cast<int>(std::lround(0.030 * sampleRate)));
    if (pending_ >= need && rawPressed != stablePressed_) {
        stablePressed_ = rawPressed;
        pending_ = 0;
        return stablePressed_;
    }
    return false;
}

bool applyCommand(Pedal& pedal, const std::string& line, std::string& err) {
    std::istringstream in(line);
    std::string cmd;
    if (!(in >> cmd)) {
        err = "empty command";
        return false;
    }
    if (cmd == "next") {
        pedal.nextEffect();
        return true;
    }
    if (cmd == "effect" || cmd == "bypass") {
        std::string name = cmd == "bypass" ? "bypass" : "";
        if (cmd == "effect" && !(in >> name)) {
            err = "usage: effect overdrive";
            return false;
        }
        Effect effect;
        if (!parseEffect(name, effect)) {
            err = "unknown effect";
            return false;
        }
        pedal.request(effect);
        return true;
    }
    float value = 0.0f;
    if (!(in >> value)) {
        err = "usage: drive 0.5";
        return false;
    }
    Knobs k = pedal.knobs();
    if (cmd == "drive") {
        k.drive = value;
    } else if (cmd == "tone") {
        k.tone = value;
    } else if (cmd == "mix") {
        k.mix = value;
    } else if (cmd == "time") {
        k.time = value;
    } else if (cmd == "level") {
        k.level = value;
    } else {
        err = "unknown command";
        return false;
    }
    pedal.setKnobs(k);
    return true;
}

ControlIo::ControlIo() = default;

ControlIo::~ControlIo() {
#if OMNI_HAS_ALSA
    if (lineFd_ >= 0) {
        close(lineFd_);
    }
    if (chipFd_ >= 0) {
        close(chipFd_);
    }
    if (potFd_ >= 0) {
        close(potFd_);
    }
#else
    (void)potFd_;
    (void)chipFd_;
    (void)lineFd_;
#endif
}

bool ControlIo::openPots(int bus, int address, std::string& err) {
#if OMNI_HAS_ALSA
    if (bus < 0) {
        bus = 1;
    }
    if (address <= 0) {
        address = 0x48;
    }
    char path[32];
    std::snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
    const int fd = open(path, O_RDWR);
    if (fd < 0) {
        err = "cannot open I2C bus";
        return false;
    }
    if (ioctl(fd, I2C_SLAVE, address) < 0) {
        close(fd);
        err = "ADS7830 address rejected";
        return false;
    }
    if (potFd_ >= 0) {
        close(potFd_);
    }
    potFd_ = fd;
    return true;
#else
    (void)bus;
    (void)address;
    err = "ADS7830 is read on the Raspberry Pi";
    return false;
#endif
}

bool ControlIo::openSwitch(int bcmPin, std::string& err) {
#if OMNI_HAS_ALSA
    if (bcmPin < 0) {
        err = "bad GPIO";
        return false;
    }
    const int fd = open("/dev/gpiochip0", O_RDONLY);
    if (fd < 0) {
        err = "cannot open gpiochip0";
        return false;
    }
    gpiohandle_request req{};
    req.lineoffsets[0] = static_cast<unsigned>(bcmPin);
    req.flags = GPIOHANDLE_REQUEST_INPUT;
    req.lines = 1;
    std::snprintf(req.consumer_label, sizeof(req.consumer_label), "omni-pedal");
    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        close(fd);
        err = "GPIO request failed. Use a free pin such as 17.";
        return false;
    }
    if (lineFd_ >= 0) {
        close(lineFd_);
    }
    if (chipFd_ >= 0) {
        close(chipFd_);
    }
    chipFd_ = fd;
    lineFd_ = req.fd;
    return true;
#else
    (void)bcmPin;
    err = "footswitch GPIO is read on the Raspberry Pi";
    return false;
#endif
}

bool ControlIo::readPots(Knobs& out) {
#if OMNI_HAS_ALSA
    if (potFd_ < 0) {
        return false;
    }
    int raw[5];
    for (int ch = 0; ch < 5; ++ch) {
        const int cmd = ads7830Command(ch);
        const unsigned char command = static_cast<unsigned char>(cmd);
        unsigned char value = 0;
        if (write(potFd_, &command, 1) != 1 || read(potFd_, &value, 1) != 1) {
            return false;
        }
        if (write(potFd_, &command, 1) != 1 || read(potFd_, &value, 1) != 1) {
            return false;
        }
        raw[ch] = value;
    }
    Knobs current = out;
    current.drive = knobFromByte(raw[0]);
    current.tone = knobFromByte(raw[1]);
    current.mix = knobFromByte(raw[2]);
    current.time = knobFromByte(raw[3]);
    current.level = knobFromByte(raw[4]);
    out = current;
    return true;
#else
    (void)out;
    return false;
#endif
}

bool ControlIo::pollSwitch(int sampleRate, int samplesAdvanced) {
#if OMNI_HAS_ALSA
    if (lineFd_ < 0) {
        return false;
    }
    gpiohandle_data data{};
    if (ioctl(lineFd_, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
        return false;
    }
    const bool pressed = data.values[0] == 0;
    return sw_.update(pressed, sampleRate, samplesAdvanced);
#else
    (void)sampleRate;
    (void)samplesAdvanced;
    return false;
#endif
}

}  // namespace stage1
