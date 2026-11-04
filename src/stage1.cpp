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

// 上电后的前 muteLeft 个采样写成 0。
// 写 0 不是把音频往后推：静音一结束，输出的第 n 个采样就是输入的第 n 个。
// in 和 out 可以是同一块内存。
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
    // 采集缓冲和播放缓冲都排满时的上限。实验室里量到的值可以比这个小。
    L.softwareRoundTripMs = L.oneDirectionMs * 2.0;
    L.dspMs = 0.0;
    return L;
}

void printReport(std::ostream& os, int sampleRate, int muteSamples) {
    const Latency planned = latencyFor(sampleRate, kPeriodFrames, kPeriods);
    const double muteSec = static_cast<double>(muteSamples) / static_cast<double>(sampleRate);

    os << "软件进度：输入级干声直通（对应现在的模拟输入电路）\n";
    os << "日期：2026-11-04\n\n";

    os << "代码位置\n";
    os << "src/pedal.cpp     效果器、Bass Split、旋钮映射、踏板消抖\n";
    os << "src/stage1.cpp    上电静音和延迟预算\n";
    os << "src/main.cpp      演示 wav，以及 Pi 上的实时通路\n\n";

    os << "程序里有什么\n";
    os << "src/pedal.cpp 是效果器。bypass 是干声直通。\n";
    os << "Standard：overdrive、distortion、delay、chorus、reverb，切换大约 10 ms 交叉淡化。\n";
    os << "Bass Split：分界点以下加低八度，以上保持原音。先按单音做。\n";
    os << "旋钮 ADS7830 和踏板 GPIO 的读取在 Pi 上打开。\n\n";

    os << "延迟\n";
    os << "干声复制：" << planned.dspMs << " ms（0 个采样点）\n";
    os << "采样率：" << planned.sampleRate << " Hz\n";
    os << "声卡周期：" << planned.periodFrames << " 帧，" << planned.periodMs << " ms\n";
    os << "每个方向 " << planned.periods << " 个周期，" << planned.oneDirectionMs << " ms\n";
    os << "采集和播放缓冲都排满时，软件上限：" << planned.softwareRoundTripMs << " ms\n";
    os << "模拟输入级是微秒级，不占这 5 ms。\n";
    os << "Codec 数字滤波器还要用示波器量。软件上限大约 2.67 ms，\n";
    os << "留给 Codec 之后，输入口到输出口的目标仍然是 5 ms 以内。\n\n";

    os << "上电静音\n";
    os << "静音 " << muteSec << " 秒（" << muteSamples << " 个采样）。\n";
    os << "偏置电阻 50 kΩ 和 10 µF 的时间常数是 0.5 秒，大约 2.5 秒后稳定。\n";
    os << "静音是把这段输出写成 0，不是把后面的声音推迟。\n\n";

    os << "和电路图的接口\n";
    os << "走 Codec Zero 的 AUX IN 和 AUX OUT，线路电平，大约 1 V RMS。\n";
    os << "不接板载麦克风，那边才有 mic bias。\n";
    os << "这一步软件不加增益。电路上的 VR1 先放在 1 倍。\n";
    os << "打开声卡时用 24 bit（ALSA 格式 S32_LE），采样率 48 kHz。\n\n";

    os << "电脑上已经检查的\n";
    os << "用 1 kHz 正弦在内存里对过，不依赖 Pi：\n";
    os << "- 关掉静音时，输出和输入逐个采样相同，脉冲留在原来的位置\n";
    os << "- 打开静音时，前 3 秒是 0，之后和输入的同一位置对齐\n";
    os << "- 一次处理整段，和拆成小块处理，结果相同\n\n";

    os << "还没做\n";
    os << "Pi 接上之后，用真吉他量输入口到输出口的延迟。\n\n";

    os << "可以这样说\n";
    os << "提案里的效果和 Bass Split 已经写进程序，电脑上用 WAV 对过。\n";
    os << "干声 bypass 仍是 0 延迟。48 kHz、32 帧、每方向两块缓冲，软件上限大约 2.7 ms。\n";
    os << "上电静音 3 秒。旋钮和踏板的代码在，要等板子接上才读得到数。\n\n";

    os << "English\n";
    os << "The proposal effects are in the program: overdrive, distortion,\n";
    os << "delay, chorus, reverb, and monophonic Bass Split, with a 10 ms\n";
    os << "crossfade. Bypass still adds zero samples. At 48 kHz, 32-frame\n";
    os << "periods, two each way, the software buffer upper bound is 2.67 ms.\n";
    os << "Output is muted for 3 seconds at startup. Knob and footswitch code\n";
    os << "runs when the Pi, ADS7830, and switch are connected.\n\n";

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
