#pragma once

#include <string>

// Proposal software: five effects, Bass Split, knobs, and the footswitch.
// Bypass, delay, chorus, reverb, and bass keep the dry sample at index 0.
// The 5 ms budget is that dry path. Overdrive and distortion replace the signal.

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
    float drive = 0.35f;  // overdrive/distortion amount, and delay feedback
    float tone = 0.65f;   // brightness, chorus depth, and bass tone
    float mix = 0.45f;    // wet level. For bass, the octave volume
    float time = 0.35f;   // delay time, chorus rate, reverb size, bass split
    float level = 1.0f;   // output level
};

// Map an ADS7830 byte, 0..255, to 0..1.
float knobFromByte(int raw);

// ADS7830 single-ended command byte. Returns -1 for a bad channel.
int ads7830Command(int channel);

// Map the time knob, 0..1, to about 40 ms through 800 ms.
int delaySamples(float time01, int sampleRate);

// Map the time knob, 0..1, to a split from 80 Hz to 250 Hz.
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

    // Pitch Bass Split is tracking. Meaningless for the other effects.
    float trackedHz() const;

private:
    PedalState* impl_;
    void resetOne(Effect effect);
};

// Footswitch debounce. rawPressed is true while the switch is down.
// A press counts after about 30 ms. Returns true on the confirmed press.
class Footswitch {
public:
    bool update(bool rawPressed, int sampleRate, int samplesAdvanced);

private:
    bool stablePressed_ = false;
    bool lastRaw_ = false;
    int pending_ = 0;
};

// One text command, for use before the knobs are wired:
//   effect overdrive
//   drive 0.7
//   next
bool applyCommand(Pedal& pedal, const std::string& line, std::string& err);

// ADS7830 and GPIO on the Pi. open fails on this PC. Algorithm tests do not use it.
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
