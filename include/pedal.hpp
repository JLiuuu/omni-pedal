#pragma once

#include <string>

// 提案里的软件：五个效果、Bass Split、旋钮和踏板。
// bypass 和 delay/chorus/reverb/bass 的干声都在第 0 个采样，不额外推迟。
// 5 ms 指标量的是这条干声。overdrive 和 distortion 会换成失真后的声音。

namespace stage1 {

enum class Effect : int {
    Bypass = 0,
    Overdrive,
    Distortion,
    Delay,
    Chorus,
    Reverb,
    BassSplit,
    Count
};

const char* effectName(Effect effect);
bool parseEffect(const std::string& text, Effect& effect);

struct Knobs {
    float drive = 0.35f;  // 过载/失真强度，delay 的反馈
    float tone = 0.65f;   // 明暗，chorus 的深度，bass 的音色
    float mix = 0.45f;    // 湿声大小。bass 是低八度的音量
    float time = 0.35f;   // delay 时间、chorus 速度、reverb 房间、bass 分界频率
    float level = 1.0f;   // 输出音量
};

// 0..255 的旋钮读数换成 0..1。
float knobFromByte(int raw);

// ADS7830 单端通道命令字。通道非法时返回 -1。
int ads7830Command(int channel);

// time 旋钮 0..1 对应大约 40 ms 到 800 ms。
int delaySamples(float time01, int sampleRate);

// time 旋钮 0..1 对应分界频率 80 Hz 到 250 Hz。
float splitHzFromTime(float time01);

struct PedalState;

class Pedal {
public:
    Pedal();
    ~Pedal();
    Pedal(const Pedal&) = delete;
    Pedal& operator=(const Pedal&) = delete;
    Pedal(Pedal&& other) noexcept;
    Pedal& operator=(Pedal&&) = delete;

    void prepare(int sampleRate);
    void setKnobs(const Knobs& knobs);
    Knobs knobs() const;

    void cutTo(Effect effect);
    void request(Effect effect);
    Effect nextEffect();
    Effect current() const;

    void process(const float* in, float* out, int n);

    // Bass Split 当前跟踪到的音高。其它效果下这个值没有意义。
    float trackedHz() const;

private:
    PedalState* impl_;
    void resetOne(Effect effect);
};

// 踏板消抖。rawPressed 为 true 表示踩下。大约按住 30 ms 才算一次。
// 返回 true 表示刚刚确认踩下。
class Footswitch {
public:
    bool update(bool rawPressed, int sampleRate, int samplesAdvanced);

private:
    bool stablePressed_ = false;
    bool lastRaw_ = false;
    int pending_ = 0;
};

// 一行文字命令，给还没接旋钮时用：
//   effect overdrive
//   drive 0.7
//   next
bool applyCommand(Pedal& pedal, const std::string& line, std::string& err);

// Pi 上的 ADS7830 和 GPIO。这台电脑上 open 会失败，算法测试不依赖它。
class ControlIo {
public:
    ControlIo();
    ~ControlIo();
    ControlIo(const ControlIo&) = delete;
    ControlIo& operator=(const ControlIo&) = delete;

    bool openPots(int bus, int address, std::string& err);
    bool openSwitch(int bcmPin, std::string& err);
    bool readPots(Knobs& out);
    bool pollSwitch(int sampleRate, int samplesAdvanced);

private:
    int potFd_ = -1;
    int chipFd_ = -1;
    int lineFd_ = -1;
    Footswitch sw_;
};

}  // namespace stage1
