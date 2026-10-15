// Discord Rich Presence over Discord's local IPC socket / named pipe.
// No SDK needed: the protocol is length-prefixed JSON frames. All I/O runs on a
// background thread, so a missing or closed Discord never stalls the game.
// Deliberately does not include raylib (the Windows build includes windows.h).
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace pg {

struct Presence {
    std::string details;           // first line, e.g. "Classic"
    std::string state;             // second line, e.g. "Wave 4 / 10"
    std::string largeImage = "logo", largeText = "PieterGame 3";
    std::string smallImage, smallText;
    int64_t startTimestamp = 0;    // unix seconds, shows "xx:xx elapsed"

    bool operator==(const Presence&) const = default;
};

class Discord {
public:
    ~Discord() { stop(); }

    // `appId` is the Application ID from discord.com/developers. Empty = disabled.
    void start(const std::string& appId);
    void stop();
    void setPresence(const Presence& p);
    void clear();                  // removes the presence but stays connected
    bool connected() const { return connected_; }
    bool running() const { return thread_.joinable(); }

private:
    void run();
    bool connect();
    void disconnect();
    bool writeFrame(uint32_t opcode, const std::string& json);
    bool drainReads();             // false if the connection died / was closed

    std::string appId_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool quit_ = false;
    bool dirty_ = false;
    bool hasPresence_ = false;
    Presence presence_;
    std::atomic<bool> connected_{false};
    int nonce_ = 0;

#ifdef _WIN32
    void* pipe_ = nullptr;
#else
    int sock_ = -1;
#endif
};

}  // namespace pg
