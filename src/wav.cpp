#include "wav.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace {

uint32_t readU32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t readU16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

void writeU32(std::ostream& os, uint32_t v) {
    const unsigned char b[4] = {
        static_cast<unsigned char>(v & 0xff),
        static_cast<unsigned char>((v >> 8) & 0xff),
        static_cast<unsigned char>((v >> 16) & 0xff),
        static_cast<unsigned char>((v >> 24) & 0xff),
    };
    os.write(reinterpret_cast<const char*>(b), 4);
}

void writeU16(std::ostream& os, uint16_t v) {
    const unsigned char b[2] = {
        static_cast<unsigned char>(v & 0xff),
        static_cast<unsigned char>((v >> 8) & 0xff),
    };
    os.write(reinterpret_cast<const char*>(b), 2);
}

float sampleFromPcm(const unsigned char* p, int bits, int format) {
    if (format == 3 && bits == 32) {
        float x = 0.0f;
        std::memcpy(&x, p, 4);
        return x;
    }
    if (bits == 16) {
        const int16_t s = static_cast<int16_t>(readU16(p));
        return static_cast<float>(s) / 32767.0f;
    }
    if (bits == 24) {
        int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
        if (v & 0x800000) {
            v |= ~0xFFFFFF;
        }
        return static_cast<float>(v) / 8388608.0f;
    }
    if (bits == 32) {
        int32_t v = 0;
        std::memcpy(&v, p, 4);
        return static_cast<float>(static_cast<double>(v) / 2147483648.0);
    }
    return 0.0f;
}

}  // namespace

bool readWav(const std::string& path, Wav& wav, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open " + path;
        return false;
    }
    std::vector<unsigned char> file(std::istreambuf_iterator<char>(in), {});
    if (file.size() < 12 || std::memcmp(file.data(), "RIFF", 4) != 0 ||
        std::memcmp(file.data() + 8, "WAVE", 4) != 0) {
        err = "not a wav file: " + path;
        return false;
    }

    int format = 0;
    int channels = 0;
    int rate = 0;
    int bits = 0;
    size_t dataPos = 0;
    size_t dataSize = 0;
    bool haveFmt = false;
    bool haveData = false;

    size_t pos = 12;
    while (pos + 8 <= file.size()) {
        char id[5] = {};
        std::memcpy(id, file.data() + pos, 4);
        const uint32_t sz = readU32(file.data() + pos + 4);
        pos += 8;
        if (pos + sz > file.size()) {
            err = "truncated wav chunk";
            return false;
        }
        if (std::strcmp(id, "fmt ") == 0) {
            if (sz < 16) {
                err = "bad fmt chunk";
                return false;
            }
            format = readU16(file.data() + pos);
            channels = readU16(file.data() + pos + 2);
            rate = static_cast<int>(readU32(file.data() + pos + 4));
            bits = readU16(file.data() + pos + 14);
            haveFmt = true;
        } else if (std::strcmp(id, "data") == 0) {
            dataPos = pos;
            dataSize = sz;
            haveData = true;
        }
        pos += sz;
        if (sz & 1u) {
            ++pos;
        }
    }

    if (!haveFmt || !haveData) {
        err = "wav is missing fmt or data";
        return false;
    }
    if (!(format == 1 || format == 3) || channels < 1 || channels > 2 || rate <= 0) {
        err = "unsupported wav (need pcm or float, mono or stereo)";
        return false;
    }
    const int bytesPerSample = bits / 8;
    if (bytesPerSample <= 0 || (format == 3 && bits != 32)) {
        err = "unsupported wav bit depth";
        return false;
    }
    const size_t frameBytes = static_cast<size_t>(bytesPerSample * channels);
    if (frameBytes == 0 || dataSize < frameBytes) {
        err = "wav has no samples";
        return false;
    }
    const size_t frames = dataSize / frameBytes;
    wav.sampleRate = rate;
    wav.samples.assign(frames, 0.0f);
    for (size_t i = 0; i < frames; ++i) {
        const unsigned char* frame = file.data() + dataPos + i * frameBytes;
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c) {
            sum += sampleFromPcm(frame + static_cast<size_t>(c * bytesPerSample), bits, format);
        }
        wav.samples[i] = sum / static_cast<float>(channels);
    }
    return true;
}

bool writeWav16(const std::string& path, const float* samples, int n, int sampleRate, std::string& err) {
    if (samples == nullptr || n < 0 || sampleRate <= 0) {
        err = "bad wav write arguments";
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        err = "cannot write " + path;
        return false;
    }
    const uint32_t dataBytes = static_cast<uint32_t>(n) * 2u;
    const uint32_t riffSize = 36u + dataBytes;
    out.write("RIFF", 4);
    writeU32(out, riffSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeU32(out, 16);
    writeU16(out, 1);
    writeU16(out, 1);
    writeU32(out, static_cast<uint32_t>(sampleRate));
    writeU32(out, static_cast<uint32_t>(sampleRate) * 2u);
    writeU16(out, 2);
    writeU16(out, 16);
    out.write("data", 4);
    writeU32(out, dataBytes);
    for (int i = 0; i < n; ++i) {
        float x = samples[i];
        if (x > 1.0f) {
            x = 1.0f;
        } else if (x < -1.0f) {
            x = -1.0f;
        }
        const long rounded = std::lround(static_cast<double>(x) * 32767.0);
        const int16_t s = static_cast<int16_t>(rounded);
        const unsigned char b[2] = {
            static_cast<unsigned char>(s & 0xff),
            static_cast<unsigned char>((s >> 8) & 0xff),
        };
        out.write(reinterpret_cast<const char*>(b), 2);
    }
    if (!out) {
        err = "short write " + path;
        return false;
    }
    return true;
}
