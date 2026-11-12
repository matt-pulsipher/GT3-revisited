// Discord Rich Presence (platform/discord_presence.h): the frames and JSON the game sends, and a
// whole exchange with a stand-in for the Discord app on a Unix socket.

#include "test_framework.h"

#include "platform/discord_presence.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

using namespace gt3r;
using namespace gt3r::platform;
namespace dc = gt3r::platform::discord;

TEST(discord_json_escape) {
    CHECK(dc::json_escape("plain") == "plain");
    CHECK(dc::json_escape("a\"b\\c") == "a\\\"b\\\\c");
    CHECK(dc::json_escape("line\nnext\t") == "line\\nnext\\t");
    CHECK(dc::json_escape(std::string("\x01", 1)) == "\\u0001");
    CHECK(dc::json_escape("Café · Côte") == "Café · Côte"); // UTF-8 kept as is
}

TEST(discord_utf8_cut) {
    CHECK(dc::utf8_cut("abcdef", 10) == "abcdef");
    CHECK(dc::utf8_cut("abcdef", 3) == "abc");
    // "·" is two bytes (C2 B7): a cut through it drops the whole character.
    CHECK(dc::utf8_cut("ab·cd", 3) == "ab");
    CHECK(dc::utf8_cut("ab·cd", 4) == "ab·");
}

TEST(discord_frames) {
    std::vector<u8> f = dc::frame(dc::Op::Frame, "{}");
    CHECK(f.size() == 10);
    CHECK(f[0] == 1 && f[1] == 0 && f[2] == 0 && f[3] == 0);
    CHECK(f[4] == 2 && f[5] == 0 && f[6] == 0 && f[7] == 0);
    CHECK(f[8] == '{' && f[9] == '}');
    CHECK(dc::frame_size(f.data(), f.size()) == 10);
    CHECK(dc::frame_size(f.data(), 9) == 0); // not all there yet
    CHECK(dc::frame_size(f.data(), 4) == 0);
    std::vector<u8> big = {1, 0, 0, 0, 0, 0, 2, 0}; // 128 KiB payload: refused
    CHECK(dc::frame_size(big.data(), big.size()) == -1);
}

TEST(discord_handshake_and_activity_json) {
    CHECK(dc::handshake_json("123") == "{\"v\":1,\"client_id\":\"123\"}");
    CHECK(dc::activity_json(std::nullopt, 42, 7) ==
          "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":42,\"activity\":null},\"nonce\":\"7\"}");

    DiscordActivity a;
    a.details = "Arcade · Tsukuba Circuit";
    a.state = "Lap 2/5";
    a.start = 1700000000;
    a.large_image = "logo";
    a.large_text = "GT3 Revisited";
    std::string j = dc::activity_json(a, 42, 1);
    CHECK(j == "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":42,\"activity\":{\"details\":\"Arcade · Tsukuba Circuit\","
               "\"state\":\"Lap 2/5\",\"timestamps\":{\"start\":1700000000},\"assets\":{\"large_image\":\"logo\","
               "\"large_text\":\"GT3 Revisited\"},\"instance\":false}},\"nonce\":\"1\"}");

    // Text Discord would refuse: under 2 bytes left out, over 128 cut.
    DiscordActivity b;
    b.details = "x";
    b.state = std::string(200, 's');
    b.party_size = 2;
    b.party_max = 8;
    j = dc::activity_json(b, 5, 2);
    CHECK(j.find("details") == std::string::npos);
    CHECK(j.find("\"state\":\"" + std::string(128, 's') + "\"") != std::string::npos);
    CHECK(j.find(std::string(129, 's')) == std::string::npos);
    CHECK(j.find("\"party\":{\"id\":\"gt3r-5\",\"size\":[2,8]}") != std::string::npos);
    CHECK(j.find("timestamps") == std::string::npos);
}

TEST(discord_events) {
    CHECK(dc::is_ready("{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1},\"evt\":\"READY\",\"nonce\":null}"));
    CHECK(!dc::is_ready("{\"cmd\":\"DISPATCH\",\"evt\":\"ERROR\",\"data\":{\"code\":4000,\"message\":\"Invalid Client ID\"}}"));
    CHECK(dc::error_message("{\"evt\":\"ERROR\",\"data\":{\"code\":4000,\"message\":\"Invalid Client ID\"}}") ==
          "Invalid Client ID");
    CHECK(dc::error_message("{\"code\":4000,\"message\":\"say \\\"hi\\\"\"}") == "say \"hi\"");
    CHECK(dc::error_message("{}") == "");
}

TEST(discord_socket_paths) {
    std::map<std::string, std::string> env = {{"XDG_RUNTIME_DIR", "/run/user/1000/"}, {"TMPDIR", "/tmp"}};
    auto paths = dc::socket_paths([&](const char* v) -> const char* {
        auto it = env.find(v);
        return it == env.end() ? nullptr : it->second.c_str();
    });
    // Two folders (/tmp once), ten sockets each, in Discord's own folder and its Flatpak and Snap ones.
    CHECK(paths.size() == 2 * 10 * 3);
    CHECK(paths.front() == "/run/user/1000/discord-ipc-0");
    CHECK(paths[9] == "/run/user/1000/discord-ipc-9");
    CHECK(paths[10] == "/tmp/discord-ipc-0");
    CHECK(paths[20] == "/run/user/1000/app/com.discordapp.Discord/discord-ipc-0");
    CHECK(paths[40] == "/run/user/1000/snap.discord/discord-ipc-0");
    CHECK(dc::socket_paths(nullptr).size() == 30); // /tmp alone
}

#if !defined(_WIN32)
namespace {

// A stand-in for the Discord app: accepts one connection, answers the handshake with READY and
// keeps the activity frames it gets.
struct FakeDiscord {
    int listen_fd = -1;
    std::vector<std::pair<u32, std::string>> got;

    bool listen(const std::string& path) {
        listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof addr.sun_path - 1);
        ::unlink(path.c_str());
        return listen_fd >= 0 && ::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0 &&
               ::listen(listen_fd, 1) == 0;
    }

    static bool read_frame(int fd, u32& op, std::string& json, int ms) {
        std::vector<u8> buf;
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < until) {
            if (long long n = dc::frame_size(buf.data(), buf.size()); n > 0) {
                op = u32(buf[0]);
                json.assign(reinterpret_cast<const char*>(buf.data()) + 8, size_t(n - 8));
                return true;
            }
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, 50) <= 0) continue;
            u8 tmp[1];
            if (::recv(fd, tmp, 1, 0) != 1) return false;
            buf.push_back(tmp[0]);
        }
        return false;
    }

    // Serves until `frames` activity frames arrived (or a few seconds pass).
    void serve(size_t frames) {
        pollfd p{listen_fd, POLLIN, 0};
        if (::poll(&p, 1, 5000) <= 0) return;
        int fd = ::accept(listen_fd, nullptr, nullptr);
        if (fd < 0) return;
        u32 op;
        std::string json;
        if (read_frame(fd, op, json, 3000)) {
            got.emplace_back(op, json);
            auto f = dc::frame(dc::Op::Frame, "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1},\"evt\":\"READY\",\"nonce\":null}");
            ::send(fd, f.data(), f.size(), 0);
            while (got.size() < 1 + frames && read_frame(fd, op, json, 8000)) got.emplace_back(op, json);
        }
        ::close(fd);
    }

    ~FakeDiscord() {
        if (listen_fd >= 0) ::close(listen_fd);
    }
};

} // namespace

TEST(discord_presence_talks_to_the_app) {
    if (!DiscordPresence::supported()) SKIP("no Discord on this platform");
    char dir[] = "/tmp/gt3rd-XXXXXX"; // short: Unix socket paths are limited to about 100 bytes
    if (!::mkdtemp(dir)) SKIP("no /tmp");
    const std::string sock = std::string(dir) + "/discord-ipc-0";
    const char* old = std::getenv("XDG_RUNTIME_DIR");
    std::string saved = old ? old : "";
    ::setenv("XDG_RUNTIME_DIR", dir, 1);

    FakeDiscord fake;
    CHECK(fake.listen(sock));
    // Two activities 5 s apart (the rate limit), then the clear at stop().
    std::thread server([&] { fake.serve(3); });
    {
        DiscordPresence p;
        DiscordActivity a;
        a.details = "Arcade · Tsukuba Circuit";
        a.state = "Mazda RX-7 Type RS";
        p.set(a); // before start: ignored
        p.start("1234");
        p.set(a);
        for (int i = 0; i < 100 && !p.connected(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(p.connected());
        CHECK(p.status() == "Connected");
        p.set(a); // the same again: not sent twice
        a.state = "Mazda RX-7 Type RS · Lap 2/5";
        p.set(a);
        for (int i = 0; i < 400 && fake.got.size() < 3; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        p.stop();
        CHECK(!p.running());
    }
    server.join();

    CHECK(fake.got.size() == 4);
    if (fake.got.size() == 4) {
        CHECK(fake.got[0].first == 0 && fake.got[0].second == "{\"v\":1,\"client_id\":\"1234\"}");
        CHECK(fake.got[1].first == 1 && fake.got[1].second.find("\"state\":\"Mazda RX-7 Type RS\"") != std::string::npos);
        CHECK(fake.got[2].first == 1 && fake.got[2].second.find("Lap 2/5") != std::string::npos);
        CHECK(fake.got[3].first == 1 && fake.got[3].second.find("\"activity\":null") != std::string::npos);
    }

    ::unlink(sock.c_str());
    ::rmdir(dir);
    if (old) ::setenv("XDG_RUNTIME_DIR", saved.c_str(), 1);
    else ::unsetenv("XDG_RUNTIME_DIR");
}
#endif
