#include "control.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace stage1 {
namespace {

bool sendAll(int fd, const std::string& text) {
    std::size_t sent = 0;
    while (sent < text.size()) {
        const ssize_t n = ::send(fd, text.data() + sent, text.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

std::string dispatch(Pedal& pedal, const std::string& raw, bool& changed) {
    std::string line = raw;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
        line.pop_back();
    }
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
        ++start;
    }
    line = line.substr(start);
    if (line.empty() || line == "status") {
        changed = false;
        return pedalStatus(pedal);
    }
    std::string err;
    if (!applyCommand(pedal, line, err)) {
        changed = false;
        return "error " + err + "\n";
    }
    changed = true;
    return "ok\n";
}

}  // namespace

struct ControlServer::Impl {
    Pedal* pedal = nullptr;
    int listenFd = -1;
    std::atomic<bool> running{false};
    std::atomic<long long> lastCommandMs{0};
    std::thread worker;
};

ControlServer::ControlServer() : impl_(new Impl) {}

ControlServer::~ControlServer() {
    stop();
    delete impl_;
    impl_ = nullptr;
}

bool ControlServer::commandsRecent() const {
    if (impl_ == nullptr) {
        return false;
    }
    const long long stamp = impl_->lastCommandMs.load();
    if (stamp <= 0) {
        return false;
    }
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    return now - stamp < 1000;
}

bool ControlServer::start(Pedal& pedal, int port, std::string& err) {
    stop();
    if (port <= 0 || port > 65535) {
        err = "panel port is out of range";
        return false;
    }
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err = "cannot open the panel socket";
        return false;
    }
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        err = "panel port " + std::to_string(port) + " is already in use";
        return false;
    }
    if (::listen(fd, 4) < 0) {
        ::close(fd);
        err = "panel listen failed";
        return false;
    }
    impl_->pedal = &pedal;
    impl_->listenFd = fd;
    impl_->running.store(true);
    impl_->worker = std::thread([this]() {
        while (impl_->running.load()) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(impl_->listenFd, &fds);
            timeval wait{};
            wait.tv_sec = 0;
            wait.tv_usec = 200000;
            const int ready = ::select(impl_->listenFd + 1, &fds, nullptr, nullptr, &wait);
            if (!impl_->running.load()) {
                break;
            }
            if (ready <= 0) {
                continue;
            }
            const int client = ::accept(impl_->listenFd, nullptr, nullptr);
            if (client < 0) {
                continue;
            }
            std::string buf;
            while (impl_->running.load()) {
                fd_set one;
                FD_ZERO(&one);
                FD_SET(client, &one);
                timeval slice{};
                slice.tv_sec = 0;
                slice.tv_usec = 200000;
                const int got = ::select(client + 1, &one, nullptr, nullptr, &slice);
                if (!impl_->running.load()) {
                    break;
                }
                if (got <= 0) {
                    continue;
                }
                char tmp[256];
                const ssize_t n = ::recv(client, tmp, sizeof(tmp), 0);
                if (n <= 0) {
                    break;
                }
                buf.append(tmp, tmp + n);
                if (buf.size() > 8192) {
                    break;
                }
                std::size_t pos = 0;
                while ((pos = buf.find('\n')) != std::string::npos) {
                    bool changed = false;
                    const std::string reply = dispatch(*impl_->pedal, buf.substr(0, pos), changed);
                    buf.erase(0, pos + 1);
                    if (changed) {
                        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now().time_since_epoch())
                                            .count();
                        impl_->lastCommandMs.store(ms);
                    }
                    if (!sendAll(client, reply)) {
                        buf.clear();
                        break;
                    }
                }
            }
            ::close(client);
        }
        if (impl_->listenFd >= 0) {
            ::close(impl_->listenFd);
            impl_->listenFd = -1;
        }
    });
    return true;
}

void ControlServer::stop() {
    if (impl_ == nullptr) {
        return;
    }
    impl_->running.store(false);
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
    impl_->pedal = nullptr;
}

}  // namespace stage1
