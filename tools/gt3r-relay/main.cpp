// gt3r-relay: the rendezvous / relay server for GT3 Revisited online battles
// (docs/dev/ONLINE.md "Server"). Session codes, endpoint exchange for UDP hole punching,
// and a relay for pairs that cannot connect directly. Keeps nothing on disk.
//
//   gt3r-relay [--port 47631] [--quiet] [--log-ips] [--geoip PATH] [--bench] [limits] [versions]
//
// Link slots (docs/dev/ONLINE.md "Link slots and the queue"):
//   --relay-links 300        racing links (relayed player pairs) at once; more lobbies queue
//   --max-queue 100          lobbies waiting for slots
// Limits (defaults suit a 1 vCPU / 0.5 GB VPS; docs/dev/ONLINE.md "Abuse protection"):
//   --channel-mbit 0.5       per racing relay channel and direction, Mbit/s (a flood guard)
//   --idle-channel-kbit 256  per lobby (not racing) relay channel and direction, kbit/s
//   --idle-mbit 20           all lobby relay channels together, Mbit/s
//   --max-sessions 4000      --sessions-per-ip 8      --max-channels 20000
//   --max-pending 8192       joins waiting to bind, all sessions
//   --ip-rate 10             control datagrams per second per source IP (IPv6: /64)
//   --ip-burst 40
//   --misses-per-min 10      lookups of unknown codes per source IP
//   --misses-per-s 200       lookups of unknown codes, all sources
//   --bench                  load testing: sources on loopback (127.0.0.0/8, ::1) skip the
//                            per-IP limits (run gt3r-relay-bench on the same machine)
// Game versions (docs/dev/ONLINE.md "Versions and updates"; unset: every version may play):
//   --latest 0.2.0           the newest release (games older than it show an update notice)
//   --min-online 0.2.0       games below it cannot create or join sessions ("update required")
//   --release-url URL        where the games' "Download update" goes (https://..., at most 128
//                            characters). {asset} in it becomes the player's package
//                            (gt3r-windows-x64.zip, gt3r-macos-arm64.zip, gt3r-linux-x86_64.tar.gz,
//                            gt3r-android.apk, gt3r-windows-x64-vr.zip, gt3r-quest.apk); the release
//                            notes page is the URL cut at /download/:
//                            https://github.com/<owner>/<repo>/releases/latest/download/{asset}
//   --versions-file PATH     the same as "latest = ...", "min_online = ...", "release_url = ..."
//                            lines (over the flags); re-read on SIGHUP (systemctl reload
//                            gt3r-relay) without dropping anything. tools/gt3r-relay/set-versions.sh
//                            writes it. A missing file is fine (the flags apply).
//
// Logs to stdout (one line per session event with the code and a tag for the host's /
// joining player's IP: a keyed hash, new key each UTC day; a stats line every minute).
//   --log-ips                log the IPs themselves instead of tags
//   --geoip PATH             add each player's country: a "first IP,last IP,country" CSV such as
//                            DB-IP's IP to Country Lite (tools/gt3r-relay/update-geoip.sh fetches
//                            it); re-read on SIGHUP. Missing: "??" until a reload finds it.
// Nothing else is kept. Version queries are only counted (in the stats line), never logged.

#include "net/geoip.h"
#include "net/relay_server.h"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

namespace {

volatile std::sig_atomic_t g_stop = 0, g_reload = 0;
void on_signal(int) { g_stop = 1; }
void on_reload(int) { g_reload = 1; }

void line(const std::string& s) {
    char t[32];
    std::time_t now = std::time(nullptr);
    std::strftime(t, sizeof t, "%Y-%m-%d %H:%M:%S", std::gmtime(&now));
    std::printf("%s %s\n", t, s.c_str());
    std::fflush(stdout);
}

std::string describe(const gt3r::net::RelayServer::Versions& v) {
    if (!v.latest.known() && !v.min_online.known() && v.release_url.empty())
        return "versions: not set (every game version may play online)";
    std::string t = "versions: latest " + (v.latest.known() ? v.latest.number() : std::string("not set"));
    t += v.min_online.known() ? ", min_online " + v.min_online.number() + " (older games cannot play online)"
                              : ", no min_online (every game version may play online)";
    t += ", release page " + (v.release_url.empty() ? std::string("not set") : v.release_url);
    return t;
}

// The flags' versions, with the file's over them. False: the file is unreadable (`v` unchanged).
bool load_versions(const std::string& path, gt3r::net::RelayServer::Versions& v, std::string& note) {
    if (path.empty()) return true;
    std::ifstream f(path);
    if (!f) {
        note = "versions file " + path + " not found: the flags apply";
        return true;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    if (!gt3r::net::parse_relay_versions(ss.str(), v, &err)) {
        note = "versions file " + path + ": " + err;
        return false;
    }
    note = "versions file " + path + " read";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    unsigned port = gt3r::net::kDefaultRelayPort;
    bool quiet = false, log_ips = false;
    double buffers_kb = 4096; // kernel socket buffers asked for (a burst of 60 Hz traffic)
    gt3r::net::RelayServer::Limits lim;
    struct Flag {
        const char* name;
        double* value = nullptr;
        size_t* count = nullptr;
        double scale = 1; // flag unit -> Limits unit
    };
    const double kMbit = 1e6 / 8;
    const Flag flags[] = {
        {"--relay-links", nullptr, &lim.relay_links},
        {"--max-queue", nullptr, &lim.max_queue},
        {"--channel-mbit", &lim.channel_bytes_per_s, nullptr, kMbit},
        {"--idle-channel-kbit", &lim.idle_channel_bytes_per_s, nullptr, 1e3 / 8},
        {"--idle-mbit", &lim.idle_bytes_per_s, nullptr, kMbit},
        {"--buffers-kb", &buffers_kb},
        {"--max-sessions", nullptr, &lim.max_sessions},
        {"--sessions-per-ip", nullptr, &lim.max_sessions_per_ip},
        {"--max-channels", nullptr, &lim.max_channels},
        {"--max-pending", nullptr, &lim.max_pending},
        {"--ip-rate", &lim.ip_msgs_per_s},
        {"--ip-burst", &lim.ip_msgs_burst},
        {"--misses-per-min", &lim.ip_misses_per_s, nullptr, 1 / 60.0},
        {"--misses-per-s", &lim.misses_per_s},
    };
    gt3r::net::RelayServer::Versions flag_versions;
    std::string versions_file, geoip_file;
    bool bad_value = false;
    for (int i = 1; i < argc; ++i) {
        bool known = false;
        auto version_flag = [&](const char* name, gt3r::net::GameVersion& to) {
            if (known || std::strcmp(argv[i], name) || i + 1 >= argc) return;
            std::optional<gt3r::net::GameVersion> v = gt3r::net::parse_game_version(argv[++i]);
            if (v) to = *v;
            else bad_value = true;
            known = v.has_value();
        };
        version_flag("--latest", flag_versions.latest);
        version_flag("--min-online", flag_versions.min_online);
        if (!known && !std::strcmp(argv[i], "--release-url") && i + 1 < argc) flag_versions.release_url = argv[++i], known = true;
        if (!known && !std::strcmp(argv[i], "--versions-file") && i + 1 < argc) versions_file = argv[++i], known = true;
        if (!known && !std::strcmp(argv[i], "--geoip") && i + 1 < argc) geoip_file = argv[++i], known = true;
        if (known) continue;
        if (bad_value) break;
        if (!std::strcmp(argv[i], "--port") && i + 1 < argc) port = unsigned(std::atoi(argv[++i])), known = true;
        else if (!std::strcmp(argv[i], "--quiet")) quiet = true, known = true;
        else if (!std::strcmp(argv[i], "--log-ips")) log_ips = true, known = true;
        else if (!std::strcmp(argv[i], "--bench")) lim.bench = true, known = true;
        for (const Flag& f : flags)
            if (!known && !std::strcmp(argv[i], f.name) && i + 1 < argc) {
                double v = std::atof(argv[++i]);
                if (!(v > 0)) break;
                if (f.value) *f.value = v * f.scale;
                if (f.count) *f.count = size_t(v);
                known = true;
            }
        if (!known) {
            std::fprintf(stderr,
                         "usage: gt3r-relay [--port N] [--quiet] [--log-ips] [--geoip PATH] [--bench]\n"
                         "  link slots: [--relay-links 300] [--max-queue 100]\n"
                         "  guards:     [--channel-mbit 0.5] [--idle-channel-kbit 256] [--idle-mbit 20]\n"
                         "  limits:     [--max-sessions 4000] [--sessions-per-ip 8] [--max-channels 20000] [--max-pending 8192]\n"
                         "              [--ip-rate 10] [--ip-burst 40] [--misses-per-min 10] [--misses-per-s 200]\n"
                         "  socket:     [--buffers-kb 4096]\n"
                         "  versions:   [--latest x.y.z] [--min-online x.y.z] [--release-url URL] [--versions-file PATH]\n"
                         "              (URL may hold {asset}: .../releases/latest/download/{asset})\n"
                         "  (values > 0; --log-ips logs players' IPs instead of daily tags; --bench lets loopback\n"
                         "  sources past the per-IP limits, for gt3r-relay-bench)\n");
            return 2;
        }
    }
    if (std::string err; bad_value || !gt3r::net::check_relay_versions(flag_versions, &err)) {
        std::fprintf(stderr, "gt3r-relay: %s\n", bad_value ? "a version is x.y.z" : err.c_str());
        return 2;
    }
    lim.ip_misses_burst = std::max(1.0, lim.ip_misses_per_s * 60);
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
#ifdef SIGHUP
    std::signal(SIGHUP, on_reload); // re-read --versions-file
#endif
    gt3r::net::RelayServer srv;
    srv.set_limits(lim);
    {
        gt3r::net::RelayServer::Versions v = flag_versions;
        std::string note;
        bool ok = load_versions(versions_file, v, note);
        if (!note.empty()) line(note + (ok ? "" : " (ignored: the flags apply)"));
        srv.set_versions(v);
    }
    if (!quiet) srv.set_log(line);
    srv.set_log_ips(log_ips);
    gt3r::net::GeoIp geo;
    auto load_geo = [&] {
        if (geoip_file.empty()) return;
        std::string geo_err;
        if (geo.load(geoip_file, &geo_err)) line("geoip: " + std::to_string(geo.ranges()) + " ranges from " + geoip_file);
        else line("geoip: " + geo_err + (geo.ranges() ? " (kept the previous list)" : " (countries show as \"\?\?\")"));
    };
    if (!geoip_file.empty()) srv.set_log_country([&geo](const gt3r::net::Address& a) { return geo.country(a); });
    std::string err;
    if (!srv.open(gt3r::u16(port), &err)) {
        std::fprintf(stderr, "gt3r-relay: %s\n", err.c_str());
        return 1;
    }
    int rcvbuf = srv.set_buffers(int(buffers_kb * 1024));
    line("gt3r-relay listening on UDP " + std::to_string(srv.port()) + " (rendezvous protocol " +
         std::to_string(gt3r::net::kRendezvousVersion) + ", game protocol " + std::to_string(gt3r::net::kProtocolVersion) + ")");
    char limits[320];
    std::snprintf(limits, sizeof limits,
                  "limits: %zu link slots (queue %zu), %.2f Mbit/s per racing channel, %.0f kbit/s per lobby channel "
                  "(%.0f Mbit/s all), %zu sessions (%zu per IP), %zu channels, %.0f msgs/s per IP, receive buffer %d KB%s",
                  lim.relay_links, lim.max_queue, lim.channel_bytes_per_s / kMbit, lim.idle_channel_bytes_per_s * 8 / 1e3,
                  lim.idle_bytes_per_s / kMbit, lim.max_sessions, lim.max_sessions_per_ip, lim.max_channels,
                  lim.ip_msgs_per_s, rcvbuf / 1024, lim.bench ? ", BENCH MODE (loopback skips per-IP limits)" : "");
    line(limits);
    line(log_ips ? "logging players' IP addresses (--log-ips)"
                 : "logging players as tags (a keyed hash of the IP, new key each UTC day), not IP addresses");
    load_geo();
    line(describe(srv.versions()));
    double last_stats = gt3r::net::now_ms();
    while (!g_stop) {
        srv.step(50);
        if (g_reload) { // SIGHUP: new versions and country list; sessions, channels and reservations stay
            g_reload = 0;
            load_geo();
            gt3r::net::RelayServer::Versions v = flag_versions;
            std::string note;
            if (load_versions(versions_file, v, note)) {
                srv.set_versions(v);
                line("reload: " + note + "; " + describe(v));
            } else {
                line("reload: " + note + " (kept the previous versions)");
            }
        }
        double now = gt3r::net::now_ms();
        if (now - last_stats > 60000) {
            last_stats = now;
            gt3r::net::RelayServer::Stats s = srv.stats();
            char buf[640];
            auto q = [&](gt3r::net::RvPlatform p) { return (unsigned long long)s.queries_by_platform[int(p)]; };
            std::snprintf(buf, sizeof buf,
                          "stats: %zu sessions, %zu channels, %zu pending joins, links %zu/%zu in use, %zu racing, "
                          "%zu queued, %llu datagrams / %llu KB relayed, peak channel %.0f kbit/s, %llu dropped, "
                          "%llu challenges, %llu rate limited, typical race %.1f min, %llu version checks "
                          "(windows %llu, macos %llu, linux %llu, android %llu), %llu refused as outdated",
                          s.sessions, s.channels, s.pending, s.links_used, s.links_capacity, s.reservations, s.queued,
                          (unsigned long long)s.relayed_datagrams, (unsigned long long)(s.relayed_bytes / 1024),
                          s.peak_channel_bytes_per_s * 8 / 1e3, (unsigned long long)s.dropped,
                          (unsigned long long)s.challenges, (unsigned long long)s.limited, s.race_estimate_ms / 60000,
                          (unsigned long long)s.version_queries, q(gt3r::net::RvPlatform::Windows),
                          q(gt3r::net::RvPlatform::MacOS), q(gt3r::net::RvPlatform::Linux),
                          q(gt3r::net::RvPlatform::Android), (unsigned long long)s.outdated);
            line(buf);
            srv.reset_peak();
        }
    }
    line("gt3r-relay stopping");
    return 0;
}
