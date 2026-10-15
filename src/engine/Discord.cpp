#include "engine/Discord.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace pg {

namespace {

enum Op : uint32_t { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

// Discord rejects strings shorter than 2 or longer than 128 characters.
std::string fitText(std::string s) {
    if (s.size() > 128) s = s.substr(0, 125) + "...";
    while (!s.empty() && s.size() < 2) s += ' ';
    return s;
}

int processId() {
#ifdef _WIN32
    return (int)GetCurrentProcessId();
#else
    return (int)getpid();
#endif
}

}  // namespace

void Discord::start(const std::string& appId) {
    if (appId.empty() || thread_.joinable()) return;
    appId_ = appId;
    quit_ = false;
    thread_ = std::thread([this] { run(); });
}

void Discord::stop() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void Discord::setPresence(const Presence& p) {
    std::lock_guard lock(mutex_);
    if (hasPresence_ && p == presence_) return;
    presence_ = p;
    hasPresence_ = true;
    dirty_ = true;
}

void Discord::clear() {
    std::lock_guard lock(mutex_);
    hasPresence_ = false;
    dirty_ = true;
}

void Discord::run() {
    using namespace std::chrono;
    auto nextConnect = steady_clock::now();
    auto lastSend = steady_clock::now() - seconds(60);
    std::unique_lock lock(mutex_);
    while (!quit_) {
        if (!connected_) {
            if (steady_clock::now() >= nextConnect) {
                lock.unlock();
                bool ok = connect();
                lock.lock();
                if (ok) dirty_ = true;  // re-send the presence after (re)connecting
                else nextConnect = steady_clock::now() + seconds(15);
            }
        } else {
            lock.unlock();
            bool alive = drainReads();
            lock.lock();
            if (!alive) {
                disconnect();
                nextConnect = steady_clock::now() + seconds(5);
            } else if (dirty_ && steady_clock::now() - lastSend >= seconds(4)) {
                // Discord rate-limits activity updates (5 per 20s): batch changes
                std::string act = "null";
                if (hasPresence_) {
                    const Presence& p = presence_;
                    act = "{\"type\":0";
                    if (!p.details.empty()) act += ",\"details\":\"" + jsonEscape(fitText(p.details)) + "\"";
                    if (!p.state.empty()) act += ",\"state\":\"" + jsonEscape(fitText(p.state)) + "\"";
                    if (p.startTimestamp > 0) act += ",\"timestamps\":{\"start\":" + std::to_string(p.startTimestamp) + "}";
                    act += ",\"assets\":{";
                    std::string a;
                    auto field = [&](const char* k, const std::string& v) {
                        if (v.empty()) return;
                        if (!a.empty()) a += ",";
                        a += std::string("\"") + k + "\":\"" + jsonEscape(fitText(v)) + "\"";
                    };
                    field("large_image", p.largeImage);
                    field("large_text", p.largeText);
                    field("small_image", p.smallImage);
                    field("small_text", p.smallText);
                    act += a + "}}";
                }
                std::string json = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(processId()) +
                                   ",\"activity\":" + act + "},\"nonce\":\"" + std::to_string(++nonce_) + "\"}";
                dirty_ = false;
                lastSend = steady_clock::now();
                lock.unlock();
                bool ok = writeFrame(Frame, json);
                lock.lock();
                if (!ok) {
                    disconnect();
                    dirty_ = true;
                }
            }
        }
        wake_.wait_for(lock, milliseconds(250));
    }
    lock.unlock();
    disconnect();
}

// ------------------------------------------------------------------ transport

#ifdef _WIN32

bool Discord::connect() {
    for (int i = 0; i < 10; i++) {
        std::string name = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
        HANDLE h = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        pipe_ = h;
        if (writeFrame(Handshake, "{\"v\":1,\"client_id\":\"" + jsonEscape(appId_) + "\"}")) {
            connected_ = true;
            return true;
        }
        disconnect();
    }
    return false;
}

void Discord::disconnect() {
    if (pipe_) CloseHandle((HANDLE)pipe_);
    pipe_ = nullptr;
    connected_ = false;
}

static bool writeAll(void* pipe, const char* data, size_t len) {
    while (len > 0) {
        DWORD written = 0;
        if (!WriteFile((HANDLE)pipe, data, (DWORD)len, &written, nullptr)) return false;
        data += written;
        len -= written;
    }
    return true;
}

static int readSome(void* pipe, char* buf, size_t len) {  // >0 bytes, 0 nothing yet, -1 dead
    DWORD avail = 0;
    if (!PeekNamedPipe((HANDLE)pipe, nullptr, 0, nullptr, &avail, nullptr)) return -1;
    if (avail == 0) return 0;
    DWORD got = 0;
    if (!ReadFile((HANDLE)pipe, buf, (DWORD)std::min<size_t>(len, avail), &got, nullptr)) return -1;
    return (int)got;
}
#define PG_HANDLE pipe_

#else

bool Discord::connect() {
    std::vector<std::string> dirs;
    for (const char* env : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"})
        if (const char* v = std::getenv(env); v && *v) dirs.push_back(v);
    dirs.push_back("/tmp");
    // regular, flatpak and snap installs of Discord
    const char* subdirs[] = {"", "app/com.discordapp.Discord/", "snap.discord/", ".flatpak/dev.vencord.Vesktop/xdg-run/"};
    for (auto& d : dirs)
        for (const char* sub : subdirs)
            for (int i = 0; i < 10; i++) {
                std::string path = d + "/" + sub + "discord-ipc-" + std::to_string(i);
                sockaddr_un addr{};
                addr.sun_family = AF_UNIX;
                if (path.size() >= sizeof addr.sun_path) continue;
                std::strcpy(addr.sun_path, path.c_str());
                int s = socket(AF_UNIX, SOCK_STREAM, 0);
                if (s < 0) return false;
#ifdef SO_NOSIGPIPE
                int one = 1;
                setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
                if (::connect(s, (sockaddr*)&addr, sizeof addr) != 0) {
                    close(s);
                    continue;
                }
                fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
                sock_ = s;
                if (writeFrame(Handshake, "{\"v\":1,\"client_id\":\"" + jsonEscape(appId_) + "\"}")) {
                    connected_ = true;
                    return true;
                }
                disconnect();
            }
    return false;
}

void Discord::disconnect() {
    if (sock_ >= 0) close(sock_);
    sock_ = -1;
    connected_ = false;
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static bool writeAll(int sock, const char* data, size_t len) {
    int spins = 0;
    while (len > 0) {
        ssize_t n = send(sock, data, len, MSG_NOSIGNAL);
        if (n < 0) {
            if ((errno == EAGAIN || errno == EWOULDBLOCK) && spins++ < 200) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            return false;
        }
        data += n;
        len -= (size_t)n;
    }
    return true;
}

static int readSome(int sock, char* buf, size_t len) {
    ssize_t n = recv(sock, buf, len, 0);
    if (n > 0) return (int)n;
    if (n == 0) return -1;  // closed by Discord
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
}
#define PG_HANDLE sock_

#endif

bool Discord::writeFrame(uint32_t opcode, const std::string& json) {
    std::string frame(8, '\0');
    uint32_t len = (uint32_t)json.size();
    for (int i = 0; i < 4; i++) {  // little-endian header: opcode, length
        frame[i] = (char)((opcode >> (8 * i)) & 0xff);
        frame[4 + i] = (char)((len >> (8 * i)) & 0xff);
    }
    frame += json;
    return writeAll(PG_HANDLE, frame.data(), frame.size());
}

bool Discord::drainReads() {
    // We don't need Discord's replies, but they must be read so the pipe never
    // fills up. A CLOSE frame (e.g. invalid application id) ends the session.
    char buf[4096];
    for (;;) {
        int n = readSome(PG_HANDLE, buf, sizeof buf);
        if (n < 0) return false;
        if (n == 0) return true;
        if (std::getenv("PG_DISCORD_DEBUG") && n > 8) std::fprintf(stderr, "[discord] %.*s\n", n - 8, buf + 8);
        if (n >= 8) {
            uint32_t op = (uint8_t)buf[0] | (uint8_t)buf[1] << 8 | (uint8_t)buf[2] << 16 | (uint32_t)(uint8_t)buf[3] << 24;
            if (op == Close) return false;
            if (op == Ping) {
                uint32_t len = (uint8_t)buf[4] | (uint8_t)buf[5] << 8 | (uint8_t)buf[6] << 16 | (uint32_t)(uint8_t)buf[7] << 24;
                if (len <= (uint32_t)n - 8) writeFrame(Pong, std::string(buf + 8, len));
            }
        }
    }
}

}  // namespace pg
