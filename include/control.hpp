#pragma once

#include "pedal.hpp"

#include <string>

namespace stage1 {

// Local text socket for the touchscreen. Bind is 127.0.0.1 only.
class ControlServer {
public:
    ControlServer();
    ~ControlServer();
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    bool start(Pedal& pedal, int port, std::string& err);
    void stop();

    // True for about one second after the screen sends a knob or effect command,
    // so a finger drag is not immediately overwritten by the ADS7830.
    bool commandsRecent() const;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace stage1
