// Online play: wire format, the session's reliable channel and lobby/race flow over
// loopback UDP (host + clients in one process), remote car prediction.

#include "test_framework.h"

#include "net/protocol.h"
#include "base/file.h"
#include "base/sha256.h"
#include "disc/volume.h"
#include "formats/png_writer.h"
#include "game/data/car_database.h"
#include "game/mod_package.h"
#include "game/gt_mode.h"
#include "game/gt_state.h"
#include "game/net_car.h"
#include "net/geoip.h"
#include "net/relay_server.h"
#include "vfs/vfs.h"
#include "net/session.h"
#include "net/update_check.h"
#include "race/exact_setup.h"
#include "race/net_race.h"
#include "race/interp.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <thread>

using namespace gt3r;

namespace {

// A simulated clock behind net::now_ms() (sessions, their receive threads and the relay server
// all read it): time moves only when the test advances it, so a machine too loaded to run the
// test at full speed does not make timers fire early (resends, punch give-up, the fall back to
// the relay, time-outs, rate limits, cookie windows). Every test that runs sessions or a relay
// server's step() declares one first (destroyed last, after the sessions' receive threads).
struct FakeClock {
    static inline std::atomic<double> ms{0};
    static inline bool on = false;
    static double read() { return ms.load(); }
    explicit FakeClock(double start = 1e6) { // not 0: some timers treat 0 as "never"
        ms = start;
        on = true;
        net::set_clock(&read);
    }
    ~FakeClock() {
        net::set_clock(nullptr);
        on = false;
    }
    FakeClock(const FakeClock&) = delete;
    FakeClock& operator=(const FakeClock&) = delete;
    static void advance(double d) { ms.store(ms.load() + d); } // the test thread is the only writer
};

// Waits in real time (bounded, generous) for what another thread or the kernel delivers
// (loopback datagrams arrive asynchronously on some systems, macOS among them).
bool settle(const std::function<bool()>& done, int wall_ms = 10000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(wall_ms);
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// One step of simulated time for the pumps: the real sleep lets the receive threads take in
// what was sent, so simulated time never runs ahead of real time.
void step_clock(double ms) {
    if (!FakeClock::on) {
        std::printf("  FAIL: pumping without a FakeClock\n");
        ++test::failures();
    }
    std::this_thread::sleep_for(std::chrono::microseconds(int(ms * 1000)));
    FakeClock::advance(ms);
}

// Pumps every session until `done` or 3 s of simulated time pass.
// Loopback only: no LAN endpoints in any session of these tests (see Session::set_advertise_lan).
const bool kNoLanEndpoints = (net::Session::set_advertise_lan(false), true);

bool pump(std::vector<net::Session*> s, const std::function<bool()>& done) {
    for (int i = 0; i < 600; ++i) {
        for (net::Session* x : s) x->poll();
        if (done()) return true;
        step_clock(5);
    }
    return false;
}

std::vector<net::Event> drain(net::Session& s) {
    std::vector<net::Event> out;
    while (auto e = s.next_event()) out.push_back(*e);
    return out;
}

bool has(const std::vector<net::Event>& ev, net::Event::Kind k) {
    for (const net::Event& e : ev)
        if (e.kind == k) return true;
    return false;
}

} // namespace

namespace {
u32 fbits(float v) {
    u32 b;
    std::memcpy(&b, &v, 4);
    return b;
}
} // namespace

TEST(net_wire_roundtrip) {
    // An exact car state: the record's floats bit for bit, the fixed-unit fields, the entry and the pad frames.
    net::CarSnapshot s;
    s.player = 3;
    s.tick = 12345;
    s.clock = -2950; // in the countdown
    net::CarMotion& m = s.motion;
    m.pos = {fbits(-812.3456f), fbits(640.0019f), fbits(21.25f)};
    m.vel = {fbits(55.55f), fbits(-20.04f), fbits(-1.2f)};
    m.heading = fbits(-1.25f), m.pitch = fbits(0.01f), m.roll = 0x80000000u, m.yaw_rate = fbits(-0.5f);
    m.omega = fbits(745.6f), m.steer_cmd = fbits(0.11f), m.axle_speed = {fbits(180.3f), fbits(181.0f)};
    m.pitch_rate = -321, m.roll_rate = 77;
    m.rim_speed = {7000, 7001, -12, 32767};
    m.susp = {-6000, -6100, 200, -32768};
    m.gear = 3, m.clutch = 2;
    s.entry = {1, 1, false, true, 1, 1};
    s.laps = 2;
    s.rev_limiter = true;
    s.inputs[0] = {0x1F, 2, 1, -4096, 4096, 120, 0, 0};
    s.inputs[1] = s.inputs[0];                          // repeated: one byte
    s.inputs[2] = {0x1B, 4, -1, 2048, 0, 4096, 4096, 4096}; // the back and third axes too
    s.input_count = 3;
    net::Writer w;
    net::write_snapshot(w, s);
    std::printf("  state: %zu bytes with %d frames of history (one repeated)\n", w.data.size(), net::kInputHistory);
    CHECK(w.data.size() <= 125);
    net::Reader r(w.data);
    net::CarSnapshot t;
    CHECK(net::read_snapshot(r, t));
    CHECK(r.at_end());
    CHECK(t.player == 3 && t.tick == 12345 && t.clock == -2950);
    CHECK(t.motion == s.motion && t.entry == s.entry);
    CHECK(t.laps == 2 && t.rev_limiter && !t.respawned);
    CHECK(t.input_count == 3 && t.inputs[0] == s.inputs[0] && t.inputs[1] == s.inputs[1] && t.inputs[2] == s.inputs[2]);
    // Read back and written again (the host relays states this way): the same bytes.
    net::Writer w2;
    net::write_snapshot(w2, t);
    CHECK(w2.data == w.data);
    // Typical analog driving: three different frames without the back or third axis.
    net::CarSnapshot d = s;
    d.inputs[1].steer = -4000, d.inputs[2] = d.inputs[0], d.inputs[2].steer = -3900;
    net::Writer wd;
    net::write_snapshot(wd, d);
    std::printf("  state while steering: %zu bytes\n", wd.data.size());
    CHECK(wd.data.size() <= 118);
    // Truncated input fails cleanly.
    net::Reader cut(ByteSpan(w.data.data(), w.data.size() - 3));
    CHECK(!net::read_snapshot(cut, t));
    // A float a peer sends as infinity or NaN is refused (the exact core would carry it; the host's floats would not).
    net::CarSnapshot bad = s;
    bad.motion.vel[1] = 0x7FC00000u;
    net::Writer wb;
    net::write_snapshot(wb, bad);
    net::Reader rb(wb.data);
    CHECK(!net::read_snapshot(rb, t));
    // A repeat marker without a frame before it is malformed.
    net::CarSnapshot one = s;
    one.input_count = 1;
    net::Writer w1;
    net::write_snapshot(w1, one);
    w1.data[w1.data.size() - 8] = 0xFF; // the first frame's flags byte
    net::Reader r1(ByteSpan(w1.data.data(), w1.data.size() - 7));
    CHECK(!net::read_snapshot(r1, t));
    // A NaN from a peer never reaches us.
    net::Writer n;
    n.f32_(std::nanf(""));
    net::Reader nr(n.data);
    CHECK(nr.f32_() == 0.0f);
}

TEST(net_snapshot_completion) {
    // What the wire leaves out: the race time from the clock, the physics frame (Z up) in the course frame, the lamps.
    net::CarSnapshot s;
    s.clock = 4500;
    s.motion.pos = {fbits(10), fbits(20), fbits(1.5f)};
    s.motion.vel = {fbits(3), fbits(-4), fbits(0.5f)};
    s.motion.heading = fbits(0.5f), s.motion.yaw_rate = fbits(0.25f);
    s.motion.gear = 0;
    s.inputs[0].brake = 2048;
    s.input_count = 1;
    race::complete_snapshot(s);
    CHECK(std::fabs(s.race_time - 1.5f) < 1e-6f);
    CHECK(s.state.position.x == 10 && s.state.position.y == 1.5f && s.state.position.z == -20);
    CHECK(s.state.velocity.x == 3 && s.state.velocity.y == 0.5f && s.state.velocity.z == 4);
    CHECK(std::fabs(s.state.speed_kmh - std::sqrt(25.25f) * 3.6f) < 1e-3f);
    CHECK(s.body_x == 10 && s.body_z == -20 && s.heading == 0.5f && s.yaw_rate == 0.25f);
    CHECK(s.state.gear == -1 && std::fabs(s.brake - 0.5f) < 1e-6f);
    // The heading's rotation turns the car's nose (-Z) to the physics frame's forward axis (cos h, sin h) in the course
    // frame (x, -y).
    sim::Vec3 nose = race::rotate(s.state.orientation, {0, 0, -1});
    CHECK(std::fabs(nose.x - std::cos(0.5f)) < 1e-5f && std::fabs(nose.z + std::sin(0.5f)) < 1e-5f);
}

TEST(net_resolve) {
    net::Address a;
    CHECK(net::resolve("127.0.0.1", 1234, a));
    CHECK(a.port() == 1234 && a.to_string() == "127.0.0.1:1234");
    CHECK(net::resolve("127.0.0.1:99", 1234, a));
    CHECK(a.port() == 99);
    CHECK(net::resolve("[::1]:7", 1234, a));
    CHECK(a.port() == 7 && a.to_string() == "[::1]:7");
}

static void session_flow(float loss) {
    FakeClock clock;
    net::Session host, c1, c2;
    std::string err;
    if (!host.host(0, "Host", "r34_gtr_v_99", 1, &err)) SKIP("no UDP sockets here");
    std::string addr = "127.0.0.1:" + std::to_string(host.port());
    host.set_loss(loss);
    // One joins first (and so is player 1) before Two: the tests below rely on the ids.
    c1.set_loss(loss);
    CHECK(c1.join(addr, "One", "nsx_type_r_02", 2, &err));
    CHECK(pump({&host, &c1}, [&] { return c1.my_id() == 1; }));
    CHECK(c2.join(addr, "Two", "supra_rz_97", 3, &err));
    c2.set_loss(loss);
    std::vector<net::Session*> all{&host, &c1, &c2};
    net::Settings st;
    st.course = "trialmt";
    st.laps = 3;
    st.ghost_ping_ms = 120;
    host.set_settings(st);
    CHECK(pump(all, [&] { return host.players().size() == 3 && c1.players().size() == 3 && c2.players().size() == 3; }));
    CHECK(c1.my_id() == 1 && c2.my_id() == 2);
    CHECK(c2.course() == "trialmt" && c2.laps() == 3);
    const net::Player* p2 = c1.player(2);
    CHECK(p2 && p2->name == "Two" && p2->car == "supra_rz_97" && p2->paint == 3);
    CHECK(has(drain(c1), net::Event::Kind::Joined));

    // Ready -> load -> loaded -> go.
    CHECK(!host.all_ready());
    c1.set_ready(true);
    c2.set_car("impreza_wrc_01", 0);
    c2.set_ready(true);
    CHECK(pump(all, [&] { return host.all_ready(); }));
    CHECK(host.player(2) && host.player(2)->car == "impreza_wrc_01");
    host.load();
    std::vector<net::Event> e1;
    CHECK(pump(all, [&] {
        for (auto& e : drain(c1)) e1.push_back(e);
        return has(e1, net::Event::Kind::Load);
    }));
    for (const net::Event& e : e1)
        if (e.kind == net::Event::Kind::Load) CHECK(e.course == "trialmt" && e.laps == 3 && e.grid.size() == 3);
    host.loaded(), c1.loaded(), c2.loaded();
    CHECK(pump(all, [&] { return host.all_loaded(); }));
    drain(host), drain(c2);
    host.go(1000);
    double go_at = 0;
    CHECK(pump(all, [&] {
        for (auto& e : drain(c2))
            if (e.kind == net::Event::Kind::Go) go_at = e.start_at_ms;
        return go_at > 0;
    }));
    double host_at = 0;
    for (auto& e : drain(host))
        if (e.kind == net::Event::Kind::Go) host_at = e.start_at_ms;
    // Loopback with resends: the start times agree to well under a frame or two without loss.
    if (loss == 0) CHECK(std::fabs(go_at - host_at) < 30);

    // States: a client's car reaches the other client through the host.
    for (u32 t = 1; t <= 20; ++t) {
        net::CarSnapshot s;
        s.tick = t;
        s.motion.pos = {fbits(float(t)), 0, 0};
        c1.send_state(s);
        s.motion.pos = {0, 0, fbits(float(t))};
        host.send_state(s);
        for (net::Session* x : all) x->poll();
    }
    CHECK(pump(all, [&] {
        const net::Session::Remote* a = c2.remote(1);
        const net::Session::Remote* b = c2.remote(0);
        return a && b && a->snap.tick >= 15 && b->snap.tick >= 15;
    }));
    // Each car's own state, bit for bit (with loss the newest state to arrive may be any tick from 15 on).
    CHECK(c2.remote(1) && c2.remote(1)->snap.player == 1 &&
          c2.remote(1)->snap.motion.pos[0] == fbits(float(c2.remote(1)->snap.tick)));
    CHECK(c2.remote(0) && c2.remote(0)->snap.motion.pos[2] == fbits(float(c2.remote(0)->snap.tick)));
    CHECK(!c2.remote(2)); // never our own car

    // A finish is relayed to everyone.
    c2.send_finish(95.5, 30.25);
    std::vector<net::Event> f1, fh;
    CHECK(pump(all, [&] {
        for (auto& e : drain(c1)) f1.push_back(e);
        for (auto& e : drain(host)) fh.push_back(e);
        return has(f1, net::Event::Kind::Finish) && has(fh, net::Event::Kind::Finish);
    }));
    for (const net::Event& e : f1)
        if (e.kind == net::Event::Kind::Finish) CHECK(e.player == 2 && e.time == 95.5 && e.best == 30.25);

    // Leaving: the host and the other client hear about it.
    c1.close();
    std::vector<net::Event> r2;
    CHECK(pump({&host, &c2}, [&] {
        for (auto& e : drain(c2)) r2.push_back(e);
        return host.players().size() == 2 && has(r2, net::Event::Kind::Retire);
    }));
    host.close();
    std::vector<net::Event> d2;
    CHECK(pump({&c2}, [&] {
        for (auto& e : drain(c2)) d2.push_back(e);
        return has(d2, net::Event::Kind::Disconnected);
    }));
    CHECK(!c2.active());
}

TEST(net_session_flow) { session_flow(0); }
TEST(net_session_flow_lossy) { session_flow(0.3f); } // 30 % of datagrams dropped each way

// Host + two clients; returns once both clients are in the roster.
static bool three(net::Session& host, net::Session& c1, net::Session& c2) {
    if (!host.host(0, "Host", "a", 0)) return false;
    std::string addr = "127.0.0.1:" + std::to_string(host.port());
    // One first, so it is player 1 and Two player 2 (a relayed join can be slower than a direct one).
    c1.join(addr, "One", "b", 0);
    if (!pump({&host, &c1}, [&] { return c1.my_id() == 1; })) return false;
    c2.join(addr, "Two", "c", 0);
    return pump({&host, &c1, &c2}, [&] { return c1.players().size() == 3 && c2.players().size() == 3; });
}

TEST(net_mesh_direct_links) {
    // The clients punch through to each other (loopback: no NAT, but the same handshake) and
    // then exchange states directly; the host stops relaying for that pair.
    FakeClock clock;
    net::Session host, c1, c2;
    if (!three(host, c1, c2)) SKIP("no UDP sockets here");
    CHECK(pump({&host, &c1, &c2}, [&] { return c1.link(2).direct && c2.link(1).direct; }));
    CHECK(c1.link(0).direct && c1.link(2).known);
    std::printf("  direct link c1<->c2: %.1f ms round trip\n", c1.link(2).rtt_ms);
    // States between the clients now arrive over the direct link.
    for (u32 t = 1; t <= 10; ++t) {
        net::CarSnapshot s;
        s.tick = t;
        c1.send_state(s);
        for (net::Session* x : {&host, &c1, &c2}) x->poll();
    }
    CHECK(pump({&host, &c1, &c2}, [&] { return c2.remote(1) && c2.remote(1)->snap.tick == 10; }));
    // The host relays nothing more for the pair: closing the host's relay path (block the host's
    // sends) still delivers c1's states to c2.
    host.set_loss(1.0f);
    for (u32 t = 11; t <= 20; ++t) {
        net::CarSnapshot s;
        s.tick = t;
        c1.send_state(s);
        for (net::Session* x : {&host, &c1, &c2}) x->poll();
    }
    CHECK(pump({&host, &c1, &c2}, [&] { return c2.remote(1) && c2.remote(1)->snap.tick == 20; }));
}

TEST(net_mesh_relay_fallback) {
    // Punching blocked (symmetric NAT): the pair gives up and the host relays their states.
    FakeClock clock;
    net::Session host, c1, c2;
    c1.set_block_direct(true);
    c1.set_punch_give_up_ms(300), c2.set_punch_give_up_ms(300);
    if (!three(host, c1, c2)) SKIP("no UDP sockets here");
    std::vector<net::Event> ev;
    CHECK(pump({&host, &c1, &c2}, [&] {
        for (auto& e : drain(c2)) ev.push_back(e);
        return has(ev, net::Event::Kind::Link);
    }));
    CHECK(!c2.link(1).direct && c2.link(1).known);
    for (u32 t = 1; t <= 10; ++t) {
        net::CarSnapshot s;
        s.tick = t;
        c1.send_state(s);
        for (net::Session* x : {&host, &c1, &c2}) x->poll();
    }
    CHECK(pump({&host, &c1, &c2}, [&] { return c2.remote(1) && c2.remote(1)->snap.tick == 10; }));
}

TEST(net_clock_sync) {
    // A client whose clock is 5 s ahead: the offset is measured, and the countdown starts at the
    // same real instant on both (within a few ms over loopback).
    FakeClock clock;
    net::Session host, c1;
    c1.set_clock_skew(5000);
    if (!host.host(0, "Host", "a", 0)) SKIP("no UDP sockets here");
    c1.join("127.0.0.1:" + std::to_string(host.port()), "One", "b", 0);
    CHECK(pump({&host, &c1}, [&] { return c1.my_id() == 1; }));
    pump({&host, &c1}, [&] { return false; }); // 3 s of polling: a dozen ping exchanges
    std::printf("  clock offset %.2f ms (true -5000)\n", c1.clock_offset_ms());
    CHECK(std::fabs(c1.clock_offset_ms() + 5000) < 5);
    host.load();
    pump({&host, &c1}, [&] { return !c1.in_lobby(); });
    host.go(500);
    CHECK(pump({&host, &c1}, [&] { return c1.race_start_ms() > 0; }));
    double real_host = host.race_start_ms(), real_client = c1.race_start_ms() - 5000;
    std::printf("  start instants differ by %.2f ms\n", real_client - real_host);
    CHECK(std::fabs(real_client - real_host) < 5);
}

TEST(net_old_protocol_told_to_update) {
    // A game of the previous online protocol (3: another physics' states) joins: the host refuses it with a text that
    // tells it to update (it shows the host's text as it is), instead of a race whose states it cannot read.
    FakeClock clock;
    net::Session host;
    if (!host.host(0, "Host", "a", 0)) SKIP("no UDP sockets here");
    net::Address ha;
    CHECK(net::resolve("127.0.0.1:" + std::to_string(host.port()), 0, ha));
    net::UdpSocket old;
    CHECK(old.open_for(ha));
    // Its first datagram: "GT3N", protocol 3, ack 0, one reliable message (seq 0: Hello), no unreliable ones.
    net::Writer hello;
    hello.u8_(u8(net::Msg::Hello));
    hello.u16_(3);
    hello.str("Old"), hello.str("r34_gtr_v_99"), hello.u8_(0), hello.str("0.3.0"), hello.u8_(0);
    net::Writer w;
    w.bytes(ByteSpan(reinterpret_cast<const u8*>("GT3N"), 4));
    w.u16_(3), w.u16_(0), w.u8_(1), w.u16_(0), w.u16_(u16(hello.data.size()));
    w.bytes(hello.data);
    w.u8_(0);
    CHECK(old.send_to(ha, w.data));
    std::string reply;
    CHECK(pump({&host}, [&] {
        u8 buf[2048];
        net::Address from;
        while (size_t n = old.recv_from(from, buf, sizeof buf)) reply.append(reinterpret_cast<const char*>(buf), n);
        return reply.find("update") != std::string::npos;
    }));
    CHECK(reply.find("update required: the host has a newer version of the game (online protocol 4, yours 3)") !=
          std::string::npos);
    CHECK(host.players().size() == 1); // not let in
    // The other way round: a newer game is told the host must update.
    CHECK(net::protocol_mismatch(net::kProtocolVersion + 1).find("the host needs to update") != std::string::npos);
}

TEST(net_session_rejects_when_racing) {
    FakeClock clock;
    net::Session host, c1, late;
    if (!host.host(0, "Host", "a", 0)) SKIP("no UDP sockets here");
    std::string addr = "127.0.0.1:" + std::to_string(host.port());
    c1.join(addr, "One", "b", 0);
    CHECK(pump({&host, &c1}, [&] { return c1.my_id() == 1; }));
    host.load();
    late.join(addr, "Late", "c", 0);
    std::vector<net::Event> ev;
    CHECK(pump({&host, &c1, &late}, [&] {
        for (auto& e : drain(late)) ev.push_back(e);
        return has(ev, net::Event::Kind::Rejected);
    }));
    CHECK(!late.active());
    CHECK(host.players().size() == 2);
}

TEST(net_remote_car_prediction) {
    race::RemoteCar rc;
    net::CarSnapshot s;
    s.tick = 60;
    s.race_time = 1.0f;
    s.state.position = {0, 0, 0};
    s.state.velocity = {20, 0, 0};
    s.body_x = 0, s.body_z = 0, s.heading = 0, s.yaw_rate = 0;
    rc.receive(s);
    // 0.1 s later: 2 m along its velocity.
    race::RemoteCar::Pose p = rc.predict(1.1);
    CHECK(std::fabs(p.state.position.x - 2.0f) < 1e-4f && std::fabs(p.body_x - 2.0f) < 1e-4f);
    // Never extrapolated beyond the cap.
    p = rc.predict(5.0);
    CHECK(std::fabs(p.state.position.x - 20.0f * float(race::RemoteCar::kMaxExtrapolation)) < 1e-3f);
    // A correction is blended in, not snapped: the shown car starts at the old prediction.
    net::CarSnapshot s2 = s;
    s2.tick = 66;
    s2.race_time = 1.1f;
    s2.state.position = {3, 0, 0}; // 1 m further than predicted
    s2.body_x = 3;
    rc.receive(s2, 1.1);
    p = rc.predict(1.1);
    CHECK(std::fabs(p.body_x - 3.0f) < 1e-4f);              // collisions use the newest truth
    CHECK(std::fabs(p.state.position.x - 2.0f) < 1e-3f);    // the shown car has not jumped
    p = rc.predict(1.6);
    CHECK(std::fabs(p.state.position.x - (3.0f + 20.0f * float(race::RemoteCar::kMaxExtrapolation))) < 0.05f); // converged
    // Older snapshots are ignored.
    net::CarSnapshot old = s;
    old.tick = 10;
    old.state.position = {-100, 0, 0};
    rc.receive(old, 1.6);
    CHECK(rc.latest().tick == 66);
}

// --- rendezvous / relay server ----------------------------------------------------

namespace {

// Pumps the sessions and the in-process relay server until `done` or 4 s of simulated time pass.
bool pump_rv(net::RelayServer& srv, std::vector<net::Session*> s, const std::function<bool()>& done) {
    for (int i = 0; i < 800; ++i) {
        srv.step(0);
        for (net::Session* x : s) x->poll();
        if (done()) return true;
        step_clock(5);
    }
    return false;
}

struct RvParty {
    FakeClock clock; // first: outlives the sessions
    net::RelayServer srv;
    net::Session host, c1, c2;
    std::string server;
    bool start(bool block_c1 = false) {
        if (!srv.open(0)) return false;
        server = "127.0.0.1:" + std::to_string(srv.port());
        c1.set_block_direct(block_c1);
        c1.set_punch_give_up_ms(800), c2.set_punch_give_up_ms(800);
        if (!host.host_via(server, "Host", "a", 0)) return false;
        if (!pump_rv(srv, {&host}, [&] { return !host.code().empty(); })) return false;
        // One first, so it is player 1 and Two player 2 (CI saw a relayed One join after Two).
        c1.join_code(server, host.code(), "One", "b", 0);
        if (!pump_rv(srv, {&host, &c1}, [&] { return c1.my_id() == 1; })) return false;
        c2.join_code(server, host.code(), "Two", "c", 0);
        return pump_rv(srv, {&host, &c1, &c2}, [&] { return c1.players().size() == 3 && c2.players().size() == 3; });
    }
    std::vector<net::Session*> all() { return {&host, &c1, &c2}; }
};

// A raw rendezvous client poking the server directly: one socket, its cookie, the last reply.
struct RvProbe {
    net::RelayServer& srv;
    net::Address sa;
    net::UdpSocket sock;
    u64 cookie = 0;
    net::GameVersion version; // sent in every request (0.0.0 unless a test sets one)
    Bytes keep;
    size_t sent = 0; // size of the last request
    net::Rv type{};
    net::Reader body{ByteSpan{}};
    explicit RvProbe(net::RelayServer& s) : srv(s) {
        net::resolve("127.0.0.1:" + std::to_string(srv.port()), 0, sa);
        sock.open_for(sa);
    }
    net::Address addr() const {
        net::Address a;
        net::resolve("127.0.0.1:" + std::to_string(sock.local_port()), 0, a);
        return a;
    }
    // The next control datagram for us (false: none within wall_ms of real time). The server
    // runs meanwhile; its clock (FakeClock) stands still.
    bool recv(int wall_ms = 10000) {
        bool got = false;
        settle([&] {
            srv.step(1);
            u8 buf[2048];
            net::Address from;
            while (size_t n = sock.recv_from(from, buf, sizeof buf)) {
                if (from != sa) continue; // late mail for an earlier test's socket on this port
                keep.assign(buf, buf + n);
                body = net::Reader(keep);
                if (net::rv_parse(keep, type, body)) return got = true;
            }
            return false;
        }, wall_ms);
        return got;
    }
    void send(net::Rv req, const std::function<void(net::Writer&)>& fill) {
        net::Writer w = net::rv_request(req, cookie, version);
        fill(w);
        net::rv_pad(w);
        sent = w.data.size();
        sock.send_to(sa, w.data);
    }
    // A request with the cookie handshake: the first reply that is not a Challenge.
    bool ask(net::Rv req, const std::function<void(net::Writer&)>& fill) {
        for (int round = 0; round < 2; ++round) {
            send(req, fill);
            if (!recv()) return false;
            if (type != net::Rv::Challenge) return true;
            body.u8_();
            cookie = net::get_u64(body);
        }
        return false;
    }
    bool ask_for(net::Rv req, net::Rv want, const std::function<void(net::Writer&)>& fill) {
        return ask(req, fill) && type == want;
    }
    u8 error_code() { return type == net::Rv::Error ? body.u8_() : 0; }
    // Registers a session with `token`: its code ("" on failure).
    std::string reg(u64 token) {
        if (!ask_for(net::Rv::Register, net::Rv::Registered, [&](net::Writer& w) {
                net::put_u64(w, token);
                net::put_endpoints(w, {});
            }))
            return "";
        return body.str();
    }
    bool lookup(const std::string& code) {
        return ask(net::Rv::Lookup, [&](net::Writer& w) {
            w.str(code);
            w.u16_(net::kProtocolVersion);
            net::put_endpoints(w, {});
        });
    }
};

void fill_bind(net::Writer& w, u64 key, const std::string& code, u64 token, u8 a, u8 b) {
    net::put_u64(w, key);
    w.str(code);
    net::put_u64(w, token);
    w.u8_(a), w.u8_(b);
}

} // namespace

TEST(net_code_helpers) {
    std::string c = net::make_code(0x123456789ABCDEFull);
    CHECK(net::valid_code(c) && c.size() == 8);
    CHECK(net::normalize_code("ab-c 2de") == "ABC2DE");
    CHECK(net::normalize_code("abcd-2345") == "ABCD2345" && net::valid_code("ABCD2345"));
    CHECK(net::format_code("ABCD2345") == "ABCD-2345" && net::format_code("abc") == "abc");
    CHECK(!net::valid_code("ABCD0EFG") && !net::valid_code("ABCDEFG") && !net::valid_code("ABCIDEFG") &&
          !net::valid_code("ABC234")); // the old 6-character codes were too easy to guess
    // SipHash-2-4 reference vectors (key 00..0f; message 00..0e, and empty).
    u8 key[16], msg[15];
    for (int i = 0; i < 16; ++i) key[i] = u8(i);
    for (int i = 0; i < 15; ++i) msg[i] = u8(i);
    CHECK(net::siphash24(key, ByteSpan(msg, 15)) == 0xa129ca6149be45e5ull);
    CHECK(net::siphash24(key, ByteSpan{}) == 0x726fdb47dd0e0e31ull);
    CHECK(net::pair_key(42, 1, 2) == net::pair_key(42, 2, 1) && net::pair_key(42, 1, 2) != net::pair_key(43, 1, 2));
}

TEST(net_relay_code_join_direct) {
    // Host and players behind no NAT (loopback): joined by code, every pair punches through.
    RvParty p;
    if (!p.start()) SKIP("no UDP sockets here");
    std::printf("  session code %s\n", p.host.code().c_str());
    CHECK(p.c1.link(0).direct && p.c2.link(0).direct);
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.c1.link(2).direct && p.c2.link(1).direct; }));
    CHECK(p.srv.stats().relayed_datagrams == 0);
}

TEST(net_relay_fallback_through_server) {
    // One player cannot punch at all (symmetric NAT): its link to the host and to the other
    // player go through the relay server; states still flow both ways.
    RvParty p;
    if (!p.start(true)) SKIP("no UDP sockets here");
    CHECK(p.c1.link(0).relay && !p.c1.link(0).direct);
    CHECK(p.c2.link(0).direct);
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.c1.link(2).relay && p.c2.link(1).relay; }));
    for (u32 t = 1; t <= 10; ++t) {
        net::CarSnapshot s;
        s.tick = t;
        p.c1.send_state(s);
        s.tick = t + 100;
        p.c2.send_state(s);
        p.srv.step(0);
        for (net::Session* x : p.all()) x->poll();
    }
    CHECK(pump_rv(p.srv, p.all(), [&] {
        return p.c2.remote(1) && p.c2.remote(1)->snap.tick == 10 && p.c1.remote(2) && p.c1.remote(2)->snap.tick == 110 &&
               p.host.remote(1) && p.host.remote(1)->snap.tick == 10;
    }));
    net::RelayServer::Stats st = p.srv.stats();
    std::printf("  relay: %llu datagrams, %llu bytes, %zu channels\n", (unsigned long long)st.relayed_datagrams,
                (unsigned long long)st.relayed_bytes, st.channels);
    CHECK(st.relayed_datagrams > 0 && st.channels >= 2); // c1-host, c1-c2 (+ c2's unused join channel until it expires)
    // The lobby still works over the relay.
    p.c1.set_ready(true);
    p.c2.set_ready(true);
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.host.all_ready(); }));
}

TEST(net_relay_unknown_code) {
    FakeClock clock;
    net::RelayServer srv;
    if (!srv.open(0)) SKIP("no UDP sockets here");
    net::Session c;
    std::string err;
    CHECK(!c.join_code("127.0.0.1:" + std::to_string(srv.port()), "abc", "x", "y", 0, &err)); // malformed
    CHECK(c.join_code("127.0.0.1:" + std::to_string(srv.port()), "ABCD-2345", "x", "y", 0, &err));
    std::vector<net::Event> ev;
    CHECK(pump_rv(srv, {&c}, [&] {
        for (auto& e : drain(c)) ev.push_back(e);
        return has(ev, net::Event::Kind::Rejected);
    }));
    CHECK(!c.active());
}

TEST(net_relay_server_rejects_abuse) {
    FakeClock clock;
    net::RelayServer srv;
    net::RelayServer::Limits lim;
    lim.max_sessions_per_ip = 2;
    srv.set_limits(lim);
    if (!srv.open(0)) SKIP("no UDP sockets here");
    // Two sessions from one address are fine, the third is refused (a different source port
    // each: the per-address limit counts the IP).
    std::string code;
    for (u64 tok = 1; tok <= 3; ++tok) {
        RvProbe p(srv);
        std::string c = p.reg(tok * 1000);
        if (tok < 3) {
            CHECK(net::valid_code(c));
            if (tok == 1) code = c;
        } else {
            CHECK(c.empty() && p.error_code() == u8(net::RvError::Busy));
        }
    }
    CHECK(srv.stats().sessions == 2);
    RvProbe p(srv);
    // Binding a player pair with the wrong session token is denied.
    CHECK(p.ask(net::Rv::Bind, [&](net::Writer& w) { fill_bind(w, net::pair_key(999, 1, 2), code, 999, 1, 2); }));
    CHECK(p.error_code() == u8(net::RvError::Denied));
    // So are player ids beyond a race's, even with the token (a session cannot fill the channel table).
    CHECK(p.ask(net::Rv::Bind, [&](net::Writer& w) { fill_bind(w, net::pair_key(1000, 1, 200), code, 1000, 1, 200); }));
    CHECK(p.error_code() == u8(net::RvError::Denied));
    // Errors never echo what the request said.
    CHECK(p.lookup("ZZZZ-ZZZZ") && p.error_code() == u8(net::RvError::NotFound));
    CHECK(p.body.str().find("ZZZZ") == std::string::npos);
    // Relay datagrams for unknown channels, or that are not game traffic, are dropped silently.
    net::Writer junk;
    junk.bytes(ByteSpan(reinterpret_cast<const u8*>(net::kRelayMagic), 4));
    net::put_u64(junk, 12345);
    junk.bytes(ByteSpan(reinterpret_cast<const u8*>("HTTP/1.1 200"), 12));
    u64 dropped = srv.stats().dropped;
    p.sock.send_to(p.sa, junk.data);
    CHECK(settle([&] {
        srv.step(1);
        return srv.stats().dropped != dropped;
    }));
    CHECK(srv.stats().dropped == dropped + 1);
    // Unknown datagrams get no answer at all (no reflection / amplification).
    p.sock.send_to(p.sa, ByteSpan(reinterpret_cast<const u8*>("hello"), 5));
    CHECK(!p.recv(50));
}

TEST(net_relay_cookie_challenge) {
    // Without a valid cookie a request only gets a Challenge, never larger than the request.
    FakeClock clock;
    net::RelayServer srv;
    if (!srv.open(0)) SKIP("no UDP sockets here");
    RvProbe p(srv);
    auto reg = [](net::Writer& w) {
        net::put_u64(w, 5);
        net::put_endpoints(w, {});
    };
    p.send(net::Rv::Register, reg);
    CHECK(p.recv() && p.type == net::Rv::Challenge);
    CHECK(p.keep.size() <= p.sent && p.sent == net::kMinRequest);
    CHECK(p.body.u8_() == u8(net::Rv::Register));
    u64 cookie = net::get_u64(p.body);
    CHECK(cookie == srv.cookie_for(p.addr(), net::now_ms()));
    CHECK(srv.stats().sessions == 0 && srv.stats().challenges == 1);
    // A request shorter than kMinRequest (unpadded) gets nothing at all.
    net::Writer w = net::rv_request(net::Rv::Register, cookie);
    reg(w);
    p.sock.send_to(p.sa, w.data);
    CHECK(!p.recv(50));
    // The cookie of another address does not work from here.
    RvProbe other(srv);
    p.cookie = srv.cookie_for(other.addr(), net::now_ms());
    p.send(net::Rv::Register, reg);
    CHECK(p.recv() && p.type == net::Rv::Challenge && srv.stats().sessions == 0);
    // With the right one it registers.
    CHECK(net::valid_code(p.reg(5)) && srv.stats().sessions == 1);
}

TEST(net_relay_spoofed_bind_ignored) {
    // An attacker who learnt a join nonce binds one end of its relay channel from a spoofed
    // victim address, the other end itself, and pushes traffic: the victim must get nothing
    // but a Challenge per spoofed request (no larger than it).
    FakeClock clock;
    net::RelayServer srv;
    if (!srv.open(0)) SKIP("no UDP sockets here");
    RvProbe host(srv), attacker(srv), victim(srv);
    std::string code = host.reg(77);
    CHECK(attacker.lookup(code) && attacker.type == net::Rv::HostInfo);
    u64 nonce = net::get_u64(attacker.body);
    CHECK(host.recv() && host.type == net::Rv::Incoming);
    // The spoofed bind, from the victim's address without the victim's cookie (the attacker
    // never sees it): with the attacker's own cookie, then with none.
    for (u64 c : {attacker.cookie, u64(0)}) {
        net::Writer w = net::rv_request(net::Rv::Bind, c);
        fill_bind(w, nonce, code, 0, 0, 0);
        net::rv_pad(w);
        srv.receive(victim.addr(), w.data, net::now_ms());
        CHECK(victim.recv() && victim.type == net::Rv::Challenge && victim.keep.size() <= w.data.size());
    }
    CHECK(srv.stats().channels == 0);
    // The attacker binds its own end (that works: it knows the nonce) and pushes data.
    CHECK(attacker.ask_for(net::Rv::Bind, net::Rv::Bound, [&](net::Writer& w) { fill_bind(w, nonce, code, 0, 0, 0); }));
    net::Writer d;
    d.bytes(ByteSpan(reinterpret_cast<const u8*>(net::kRelayMagic), 4));
    net::put_u64(d, nonce);
    d.bytes(ByteSpan(reinterpret_cast<const u8*>("GT3N"), 4));
    d.data.resize(400, 0);
    u64 dropped = srv.stats().dropped;
    for (int i = 0; i < 20; ++i) attacker.sock.send_to(attacker.sa, d.data);
    CHECK(settle([&] { // the server has seen every one (and dropped it: the victim's end is not bound)
        srv.step(1);
        return srv.stats().dropped >= dropped + 20;
    }));
    u8 buf[2048];
    net::Address from;
    bool forwarded = false;
    while (victim.sock.recv_from(from, buf, sizeof buf)) forwarded = forwarded || from == victim.sa;
    CHECK(!forwarded);
    CHECK(srv.stats().relayed_datagrams == 0);
}

TEST(net_relay_rate_limits) {
    FakeClock clock;
    net::RelayServer::Limits lim;
    lim.ip_msgs_per_s = 1, lim.ip_msgs_burst = 6;
    lim.ip_misses_burst = 3;
    {
        // Control datagrams per source IP: a burst, then about one a second.
        net::RelayServer srv;
        srv.set_limits(lim);
        if (!srv.open(0)) SKIP("no UDP sockets here");
        RvProbe p(srv);
        for (int i = 0; i < 20; ++i)
            p.send(net::Rv::Register, [](net::Writer& w) {
                net::put_u64(w, 9);
                net::put_endpoints(w, {});
            });
        // The server's clock stands still: exactly the burst is answered, the rest refused.
        int replies = 0;
        while (replies < 6 && p.recv()) ++replies;
        CHECK(settle([&] {
            srv.step(1);
            return srv.stats().challenges + srv.stats().limited >= 20;
        }));
        while (p.recv(50)) ++replies;
        CHECK(replies == 6);
        CHECK(srv.stats().limited == 14);
    }
    // Guessing codes: a few misses per IP, then every lookup is refused for a while (before
    // the lookup, so a refusal does not tell whether a code exists).
    lim.ip_msgs_per_s = 100, lim.ip_msgs_burst = 100;
    net::RelayServer srv;
    srv.set_limits(lim);
    CHECK(srv.open(0));
    RvProbe host(srv), guesser(srv);
    std::string code = host.reg(31);
    std::vector<u8> errors;
    for (int i = 0; i < 4; ++i) {
        CHECK(guesser.lookup(net::make_code(u64(i) * 7919)));
        errors.push_back(guesser.error_code());
    }
    CHECK(errors[0] == u8(net::RvError::NotFound) && errors[2] == u8(net::RvError::NotFound));
    CHECK(errors[3] == u8(net::RvError::Busy));
    CHECK(guesser.lookup(code) && guesser.error_code() == u8(net::RvError::Busy)); // even the right one
    RvProbe other(srv); // the same IP (loopback): the budget is the address's, not the socket's
    CHECK(other.lookup(code) && other.error_code() == u8(net::RvError::Busy));
}

TEST(net_relay_pending_join_caps) {
    FakeClock clock;
    // Joins waiting to bind are capped per session and in total; a session's Incoming
    // notifications are rate limited (a known code cannot be used to flood its host).
    net::RelayServer::Limits lim;
    lim.max_pending = 4, lim.max_pending_per_session = 3;
    {
        net::RelayServer srv;
        srv.set_limits(lim);
        if (!srv.open(0)) SKIP("no UDP sockets here");
        RvProbe ha(srv), hb(srv), c(srv);
        std::string a = ha.reg(1), b = hb.reg(2);
        CHECK(!a.empty() && !b.empty());
        for (int i = 0; i < 3; ++i) CHECK(c.lookup(a) && c.type == net::Rv::HostInfo);
        CHECK(c.lookup(a) && c.error_code() == u8(net::RvError::Busy)); // the session's cap
        CHECK(c.lookup(b) && c.type == net::Rv::HostInfo);
        CHECK(c.lookup(b) && c.error_code() == u8(net::RvError::Busy)); // the global cap
        CHECK(srv.stats().pending == 4);
        int incoming = 0;
        while (incoming < 3 && ha.recv()) incoming += ha.type == net::Rv::Incoming;
        while (ha.recv(50)) incoming += ha.type == net::Rv::Incoming; // and no more
        CHECK(incoming == 3);
    }
    lim = {};
    lim.joins_burst = 2, lim.joins_per_s = 0.01;
    net::RelayServer srv;
    srv.set_limits(lim);
    CHECK(srv.open(0));
    RvProbe h(srv), j(srv);
    std::string code = h.reg(3);
    for (int i = 0; i < 2; ++i) CHECK(j.lookup(code) && j.type == net::Rv::HostInfo);
    CHECK(j.lookup(code) && j.error_code() == u8(net::RvError::Busy));
}

TEST(net_relay_old_protocol_rejected) {
    FakeClock clock;
    net::RelayServer srv;
    if (!srv.open(0)) SKIP("no UDP sockets here");
    RvProbe p(srv);
    // A version 1 game's Register (no cookie, no padding): an Error in version 1's header,
    // which that game shows, no larger than the request.
    net::Writer old = net::rv_begin(net::Rv::Register, 1);
    net::put_u64(old, 1234);
    net::put_endpoints(old, {"10.0.0.20:47630"});
    p.sock.send_to(p.sa, old.data);
    bool got = settle([&] {
        srv.step(1);
        u8 buf[256];
        net::Address from;
        size_t n = p.sock.recv_from(from, buf, sizeof buf);
        if (n && from != p.sa) return false; // late mail for an earlier test's socket on this port
        if (n) {
            CHECK(n <= old.data.size() && net::rv_version(ByteSpan(buf, n)) == 1);
            net::Reader r(ByteSpan(buf, n));
            r.bytes(6);
            CHECK(r.u8_() == u8(net::Rv::Error) && r.u8_() == u8(net::RvError::Version));
            CHECK(r.str().find("update") != std::string::npos);
        }
        return n != 0;
    });
    CHECK(got && srv.stats().sessions == 0);

    // The game against an older server: a clear error instead of silence.
    net::UdpSocket fake;
    if (!fake.open(0)) SKIP("no UDP sockets here");
    std::string server = "127.0.0.1:" + std::to_string(fake.local_port());
    for (bool hosting : {true, false}) {
        net::Session s;
        CHECK(hosting ? s.host_via(server, "H", "a", 0) : s.join_code(server, "ABCD2345", "J", "b", 0));
        std::vector<net::Event> ev;
        CHECK(pump({&s}, [&] {
            u8 buf[256];
            net::Address from;
            if (fake.recv_from(from, buf, sizeof buf)) { // answers as a version 1 server does
                net::Writer e = net::rv_begin(net::Rv::Error, 1);
                e.u8_(u8(net::RvError::Version));
                e.str("this relay speaks protocol 1");
                fake.send_to(from, e.data);
            }
            for (auto& e : drain(s)) ev.push_back(e);
            return has(ev, net::Event::Kind::Disconnected);
        }));
        for (const net::Event& e : ev)
            if (e.kind == net::Event::Kind::Disconnected) CHECK(e.text.find("relay server is out of date") != std::string::npos);
        CHECK(!s.active());
    }
}

// --- cars ----------------------------------------------------------------------------

TEST(net_online_car_roundtrip) {
    game::OnlineCar g;
    g.kind = game::OnlineCar::Kind::Garage;
    g.car = "r34_gtr_v_99";
    g.name = "SKYLINE GT-R V-spec";
    g.paint = 3;
    for (size_t i = 0; i < g.parts.size(); ++i) g.parts[i] = 0x1234567800000000ull + i;
    g.settings.gears = {-3000, 3500, 2200, 1500, 1100, 900, -999, -999};
    g.settings.final_drive = 3900;
    g.settings.spring = {85, 72};
    g.settings.lsd = {10, 12, 20, 25, 5, 8};
    g.settings.tcs = 4;
    g.engine_wear_m = 12000.5f;
    g.oil_m = -250000;
    g.wheel = "wheel_rays_ce28";
    Bytes b = game::encode_online_car(g);
    std::printf("  garage car descriptor: %zu bytes\n", b.size());
    CHECK(b.size() < game::kMaxOnlineCarBytes);
    std::optional<game::OnlineCar> d = game::decode_online_car(b);
    CHECK(d && game::encode_online_car(*d) == b);
    CHECK(d && d->kind == game::OnlineCar::Kind::Garage && d->parts[26] == g.parts[26] && d->settings.gears[6] == -999 &&
          d->settings.lsd[3] == 25 && d->engine_wear_m == 12000.5f && d->wheel == g.wheel && d->paint == 3);

    game::OnlineCar m;
    m.kind = game::OnlineCar::Kind::Mod;
    m.car = "tesla_model_y_perf_26";
    m.package = std::string(64, 'a');
    m.package_size = 1234567;
    std::optional<game::OnlineCar> md = game::decode_online_car(game::encode_online_car(m));
    CHECK(md && md->package == m.package && md->package_size == 1234567);
    game::OnlineCar a;
    a.kind = game::OnlineCar::Kind::Arcade;
    a.car = "nsx_s_zero_97";
    a.arcade = "nsx_s_zero_97_290";
    std::optional<game::OnlineCar> ad = game::decode_online_car(game::encode_online_car(a));
    CHECK(ad && ad->kind == game::OnlineCar::Kind::Arcade && ad->car == a.car && ad->arcade == a.arcade);
    a.arcade.clear(); // an arcade car without its label
    CHECK(!game::decode_online_car(game::encode_online_car(a)));
    // Malformed: truncated, bad hash, unknown kind, empty label.
    CHECK(!game::decode_online_car(ByteSpan(b.data(), b.size() - 5)));
    m.package = "zz";
    CHECK(!game::decode_online_car(game::encode_online_car(m)));
    Bytes k = game::encode_online_car(g);
    k[1] = 9;
    CHECK(!game::decode_online_car(k));
    game::OnlineCar e;
    CHECK(!game::decode_online_car(game::encode_online_car(e)));
}

TEST(net_car_info_reaches_everyone) {
    FakeClock clock;
    net::Session host, c1, c2;
    if (!host.host(0, "Host", "a", 0)) SKIP("no UDP sockets here");
    std::string addr = "127.0.0.1:" + std::to_string(host.port());
    host.set_car_info(Bytes{1, 2, 3});
    c1.join(addr, "One", "b", 0);
    c1.set_car_info(Bytes{4, 5}); // before the Welcome: sent once joined
    bool swapped = pump({&host, &c1}, [&] { return host.car_info(1) && c1.car_info(0); });
    CHECK(swapped);
    if (!swapped) return;
    CHECK(*host.car_info(1) == (Bytes{4, 5}) && *c1.car_info(0) == (Bytes{1, 2, 3}));
    // A later player gets everyone's.
    c2.join(addr, "Two", "c", 0);
    CHECK(pump({&host, &c1, &c2}, [&] { return c2.car_info(0) && c2.car_info(1) && c1.car_info(2) == nullptr; }));
    c2.set_car_info(Bytes{9});
    CHECK(pump({&host, &c1, &c2}, [&] { return c1.car_info(2) && *c1.car_info(2) == Bytes{9}; }));
    // Changes replace the old one.
    c1.set_car_info(Bytes{7, 7});
    CHECK(pump({&host, &c1, &c2}, [&] { return c2.car_info(1) && *c2.car_info(1) == (Bytes{7, 7}); }));
}

TEST(net_garage_car_spec_disc) {
    // A garage car rebuilt from its descriptor on "another machine" is GT Mode's own spec.
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));
    const data::Car* car = db.car("r34_gtr_v_99");
    if (!car) SKIP("no R34 on this disc");
    game::GarageCar g;
    g.car = car->label;
    g.parts = car->parts;
    g.settings = sim::stock_settings(db, g.car, g.parts);
    g.settings.spring = {g.settings.spring[0] + 10, g.settings.spring[1] + 5};
    g.settings.ride_height = {g.settings.ride_height[0] - 5, g.settings.ride_height[1] - 5};
    g.cond.engine_wear_m = -250000; // still running in: below full power
    std::optional<sim::CarSpec> direct = sim::build_car_spec(db, g.car, g.parts, g.settings);
    CHECK(direct.has_value());
    float f = game::GtMode::engine_factor(g.cond);
    CHECK(f < 1.0f);
    for (float& t : direct->engine.curve_torque) t *= f;
    std::optional<game::OnlineCar> sent = game::decode_online_car(game::encode_online_car(game::online_car_from_garage(db, g)));
    CHECK(sent.has_value());
    std::optional<sim::CarSpec> rebuilt = game::online_car_spec(db, *sent);
    CHECK(rebuilt.has_value());
    CHECK(rebuilt->mass == direct->mass && rebuilt->engine.curve_torque == direct->engine.curve_torque);
    CHECK(rebuilt->axle[0].ride_height == direct->axle[0].ride_height && rebuilt->axle[0].ride_height > 0);
    std::printf("  garage R34: %.0f kg, peak torque x%.3f (engine condition)\n", rebuilt->mass, f);
}

// --- mod car packages ------------------------------------------------------------------

TEST(net_sha256_vectors) {
    auto hex = [](const std::string& s) { return sha256_hex(ByteSpan(reinterpret_cast<const u8*>(s.data()), s.size())); };
    CHECK(hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Bytes million(1000000, u8('a'));
    CHECK(sha256_hex(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // Streaming in odd pieces gives the same digest.
    Sha256 st;
    for (size_t i = 0; i < million.size(); i += 777) st.update(ByteSpan(million.data() + i, std::min<size_t>(777, million.size() - i)));
    std::array<u8, 32> a = st.finish(), b = sha256(million);
    CHECK(a == b);
}

namespace {

game::ModPackage sample_package() {
    game::ModPackage p;
    p.label = "test_ev_26";
    std::string ini = "[car]\nlabel = test_ev_26\n";
    p.files.push_back({"cars/test_ev_26.ini", Bytes(ini.begin(), ini.end())});
    p.files.push_back({"images/thumb.png", Bytes(100, 7)});
    return p;
}

} // namespace

TEST(net_mod_package_structure) {
    CHECK(game::safe_package_path("cars/my_car.ini") && game::safe_package_path("models/body-2.glb"));
    for (const char* bad : {"", "/etc/passwd", "../x.png", "a/../b.png", "a//b.png", ".hidden.png", "a/.b/c.png",
                            "C:\\x.png", "a\\b.png", "a/b/c/d.png", "sp ace.png", "x\n.png"})
        CHECK(!game::safe_package_path(bad));
    game::ModPackage p = sample_package();
    Bytes blob = game::serialize_mod_package(p);
    std::string err;
    std::optional<game::ModPackage> q = game::parse_mod_package(blob, &err);
    CHECK(q && q->label == p.label && q->files.size() == 2 && q->files[0].path == "cars/test_ev_26.ini");
    // The same car in any file order hashes the same.
    game::ModPackage r = p;
    std::swap(r.files[0], r.files[1]);
    CHECK(sha256_hex(game::serialize_mod_package(r)) == sha256_hex(blob));
    auto rejects = [&](game::ModPackage m, const char* why) {
        std::string e;
        bool ok = game::parse_mod_package(game::serialize_mod_package(m), &e).has_value();
        if (ok) std::printf("  accepted but should not: %s\n", why);
        CHECK(!ok);
    };
    { game::ModPackage m = p; m.files[1].path = "../evil.png"; rejects(m, "path escape"); }
    { game::ModPackage m = p; m.files[1].path = "files/menu/title.img"; rejects(m, "game file override"); }
    { game::ModPackage m = p; m.files[1].path = "run.sh"; rejects(m, "other file type"); }
    { game::ModPackage m = p; m.files.push_back(m.files[1]); rejects(m, "duplicate"); }
    { game::ModPackage m = p; m.files[1].path = "IMAGES/THUMB.PNG"; m.files.push_back(p.files[1]); rejects(m, "duplicate by case"); }
    { game::ModPackage m = p; m.files[0].path = "cars/other_car.ini"; rejects(m, "definition for another label"); }
    { game::ModPackage m = p; m.files.erase(m.files.begin()); rejects(m, "no definition"); }
    { game::ModPackage m = p; m.files[1].data.assign((4u << 20) + 1, 0); rejects(m, "oversized png"); }
    { game::ModPackage m = p; for (int i = 0; i < 9; ++i) m.files.push_back({"images/x" + std::to_string(i) + ".png", Bytes(4, 0)}); rejects(m, "too many files"); }
    Bytes trailing = blob;
    trailing.push_back(0);
    CHECK(!game::parse_mod_package(trailing));
    CHECK(!game::parse_mod_package(ByteSpan(blob.data(), blob.size() - 10)));
    Bytes notpkg = blob;
    notpkg[0] = 'X';
    CHECK(!game::parse_mod_package(notpkg));
}

TEST(net_package_transfer) {
    // A client's package reaches another client through the host (which keeps a copy), and a
    // package that does not match its advertised hash is refused.
    FakeClock clock;
    net::Session host, c1, c2;
    if (!three(host, c1, c2)) SKIP("no UDP sockets here");
    Bytes blob(150000);
    for (size_t i = 0; i < blob.size(); ++i) blob[i] = u8(i * 31 + (i >> 7));
    std::string hash = sha256_hex(blob);
    c1.offer_package(hash, blob);
    c2.request_package(hash, 1);
    std::vector<net::Event> ev;
    CHECK(pump({&host, &c1, &c2}, [&] {
        for (auto& e : drain(c2)) ev.push_back(e);
        return has(ev, net::Event::Kind::PackageDone) || has(ev, net::Event::Kind::PackageFailed);
    }));
    CHECK(has(ev, net::Event::Kind::PackageDone));
    std::optional<Bytes> got = c2.take_package(hash);
    CHECK(got && *got == blob);
    // The host kept it: the host itself can now be served without asking c1.
    c1.close();
    host.request_package(hash, 1); // already offered -> no-op
    CHECK(!host.download(hash));
    // A forged package (the bytes do not hash to the advertised value).
    net::Session h2, a, b;
    if (!three(h2, a, b)) SKIP("no UDP sockets here");
    std::string fake = std::string(64, 'a');
    a.offer_package(fake, Bytes(5000, 1));
    b.request_package(fake, 1);
    std::vector<net::Event> ev2;
    CHECK(pump({&h2, &a, &b}, [&] {
        for (auto& e : drain(b)) ev2.push_back(e);
        return has(ev2, net::Event::Kind::PackageDone) || has(ev2, net::Event::Kind::PackageFailed);
    }));
    CHECK(has(ev2, net::Event::Kind::PackageFailed) && !has(ev2, net::Event::Kind::PackageDone));
    CHECK(!b.take_package(fake));
}

namespace {

// A one-triangle binary glTF with a "paint" material.
Bytes tiny_glb(u32 texture_dim = 0) {
    std::vector<float> pos = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::vector<u16> idx = {0, 1, 2, 0};
    Bytes bin(pos.size() * 4 + idx.size() * 2);
    std::memcpy(bin.data(), pos.data(), pos.size() * 4);
    std::memcpy(bin.data() + pos.size() * 4, idx.data(), idx.size() * 2);
    std::string img;
    Bytes png;
    if (texture_dim) {
        png = encode_png(texture_dim, texture_dim, std::vector<u32>(size_t(texture_dim) * texture_dim, 0xFF0000FFu));
        bin.insert(bin.end(), png.begin(), png.end());
        while (bin.size() % 4) bin.push_back(0);
        img = R"(,"images":[{"bufferView":2,"mimeType":"image/png"}],"textures":[{"source":0}])";
    }
    std::string views = R"([{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6})";
    if (texture_dim) views += R"(,{"buffer":0,"byteOffset":44,"byteLength":)" + std::to_string(png.size()) + "}";
    views += "]";
    std::string mat = texture_dim ? R"({"name":"paint","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}})" : R"({"name":"paint"})";
    std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":)" + std::to_string(bin.size()) +
                       R"(}],"bufferViews":)" + views +
                       R"(,"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
                       R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],)"
                       R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],)"
                       R"("materials":[)" + mat + "]" + img + R"(,"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    while (json.size() % 4) json += ' ';
    Bytes glb;
    auto u32le = [&](u32 v) { for (int i = 0; i < 4; ++i) glb.push_back(u8(v >> (8 * i))); };
    glb.insert(glb.end(), {'g', 'l', 'T', 'F'});
    u32le(2);
    u32le(u32(12 + 8 + json.size() + 8 + bin.size()));
    u32le(u32(json.size()));
    u32le(0x4E4F534A);
    glb.insert(glb.end(), json.begin(), json.end());
    u32le(u32(bin.size()));
    u32le(0x004E4942);
    glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

game::ModPackage ev_package(const std::string& extra = "") {
    game::ModPackage p;
    p.label = "online_test_ev";
    std::string ini = "[car]\nlabel = online_test_ev\nname = Online Test EV\nmaker = Testco\ndonor = r34_gtr_v_99\n"
                      "[body]\nweight_kg = 1900\n[powertrain]\ntype = electric\npower_kw = 300\ntorque_nm = 500\nmax_rpm = 16000\n"
                      "[model]\nfile = models/body.glb\n[dealer]\nthumb = images/thumb.png\n" + extra;
    p.files.push_back({"cars/online_test_ev.ini", Bytes(ini.begin(), ini.end())});
    p.files.push_back({"models/body.glb", tiny_glb()});
    p.files.push_back({"images/thumb.png", encode_png(51, 27, std::vector<u32>(51 * 27, 0xFF00FF00u))});
    return p;
}

} // namespace

TEST(net_mod_package_validation_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));
    std::string err;
    game::ModPackage good = ev_package();
    bool ok = game::validate_mod_package(good, db, "online_test_ev", &err);
    if (!ok) std::printf("  good package refused: %s\n", err.c_str());
    CHECK(ok);
    auto refuses = [&](const game::ModPackage& p, const char* label, const char* why) {
        std::string e;
        bool v = game::validate_mod_package(p, db, label, &e);
        std::printf("  %-28s -> %s\n", why, v ? "ACCEPTED" : e.c_str());
        CHECK(!v);
    };
    refuses(good, "other_label", "advertised as another car");
    refuses(ev_package("[body]\nweight_kg = 50\n"), "online_test_ev", "implausible weight");
    refuses(ev_package("[powertrain]\npower_kw = 90000\n"), "online_test_ev", "implausible power");
    { game::ModPackage p = good; p.files.pop_back(); refuses(p, "online_test_ev", "referenced file missing"); }
    { game::ModPackage p = good; p.files.push_back({"images/extra.png", p.files[2].data}); refuses(p, "online_test_ev", "unreferenced extra file"); }
    { game::ModPackage p = good; p.files[2].data = encode_png(4096, 8, std::vector<u32>(4096 * 8, 0)); refuses(p, "online_test_ev", "oversized picture"); }
    { game::ModPackage p = good; p.files[1].data = Bytes(64, 0x41); refuses(p, "online_test_ev", "corrupt body"); }
    { game::ModPackage p = good; p.files[1].data = tiny_glb(4096); refuses(p, "online_test_ev", "oversized body texture"); }
    {
        game::ModPackage p = good;
        std::string ini(p.files[0].data.begin(), p.files[0].data.end());
        ini.replace(ini.find("r34_gtr_v_99"), 12, "no_such_car_");
        p.files[0].data.assign(ini.begin(), ini.end());
        refuses(p, "online_test_ev", "donor not on this disc");
    }
    // Install: the files land in mods/dl-<hash>/ and nowhere else.
    std::string dir = test::temp_path("mods").string();
    std::string hash = sha256_hex(game::serialize_mod_package(good));
    std::optional<std::string> where = game::install_mod_package(good, hash, dir, "Online Test EV", "Matt", &err);
    CHECK(where.has_value());
    if (where) {
        CHECK(read_host_file(*where + "/cars/online_test_ev.ini").has_value());
        CHECK(read_host_file(*where + "/models/body.glb").has_value());
        std::optional<Bytes> mi = read_host_file(*where + "/mod.ini");
        CHECK(mi && std::string(mi->begin(), mi->end()).find("priority = -100") != std::string::npos);
    }
    std::filesystem::remove_all(dir);
}

TEST(net_mod_packages_of_installed_mods) {
    // Every mod car in GT3R_MODS_DIR (e.g. the user's mods folder) must build into a package
    // that validation accepts: validation exists to stop hostile files, never real cars.
    Disc* disc = test::test_disc();
    const char* mods = std::getenv("GT3R_MODS_DIR");
    if (!disc || !mods) SKIP("needs GT3R_DISC and GT3R_MODS_DIR");
    Vfs vfs(&disc->volume());
    vfs.scan_mods(mods, nullptr);
    data::CarDatabase db;
    CHECK(db.load(vfs));
    int n = 0;
    for (const data::ModCar& m : db.mod_cars()) {
        std::string err;
        std::optional<Bytes> blob = game::build_mod_package(db, vfs, m.label, &err);
        std::optional<game::ModPackage> p = blob ? game::parse_mod_package(*blob, &err) : std::nullopt;
        bool ok = p && game::validate_mod_package(*p, db, m.label, &err);
        std::printf("  %-32s %8zu bytes  %s\n", m.label.c_str(), blob ? blob->size() : 0, ok ? "ok" : err.c_str());
        CHECK(ok);
        ++n;
    }
    CHECK(n > 0);
}

// --- contact messages ------------------------------------------------------------------

TEST(net_contact_routing) {
    // c1 hit c2 (direct link), c1 hit the host, and with the direct link blocked c2's message
    // reaches c1 through the host. A client cannot pretend to be someone else.
    for (bool blocked : {false, true}) {
        FakeClock clock;
        net::Session host, c1, c2;
        if (blocked) c1.set_block_direct(true), c1.set_punch_give_up_ms(300), c2.set_punch_give_up_ms(300);
        if (!three(host, c1, c2)) SKIP("no UDP sockets here");
        if (!blocked) CHECK(pump({&host, &c1, &c2}, [&] { return c1.link(2).direct && c2.link(1).direct; }));
        else pump({&host, &c1, &c2}, [&] { return false; }); // punching gives up
        net::Event::Hit h{120, 10, -5, 2500, -800};
        c1.send_contact(2, h);
        c1.send_contact(0, h);
        net::Event::Hit h2{130, 1, 2, -100, 50};
        c2.send_contact(1, h2);
        std::vector<net::Event> e2, eh, e1;
        CHECK(pump({&host, &c1, &c2}, [&] {
            for (auto& e : drain(c2)) e2.push_back(e);
            for (auto& e : drain(host)) eh.push_back(e);
            for (auto& e : drain(c1)) e1.push_back(e);
            return has(e2, net::Event::Kind::Contact) && has(eh, net::Event::Kind::Contact) && has(e1, net::Event::Kind::Contact);
        }));
        for (const net::Event& e : e2)
            if (e.kind == net::Event::Kind::Contact)
                CHECK(e.player == 1 && e.contact.tick == 120 && e.contact.jx == 2500 && e.contact.jz == -800 && e.contact.point_z == -5);
        for (const net::Event& e : eh)
            if (e.kind == net::Event::Kind::Contact) CHECK(e.player == 1);
        for (const net::Event& e : e1)
            if (e.kind == net::Event::Kind::Contact) CHECK(e.player == 2 && e.contact.jx == -100);
    }
}

// --- lobby car rules -------------------------------------------------------------------

TEST(net_car_rules_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));
    game::OnlineCar r34 = game::online_car_from_db(db, "r34_gtr_v_99", 0);
    std::optional<sim::CarSpec> spec = game::online_car_spec(db, r34);
    CHECK(spec.has_value());
    float ps = game::spec_power_ps(*spec);
    std::printf("  R34 V-spec: %.0f PS\n", ps);
    CHECK(ps > 250 && ps < 400);
    std::string why;
    game::CarFacts f = game::online_car_facts(db, nullptr, r34);
    CHECK(f.known && f.built && std::fabs(f.power_ps - ps) < 0.01f);
    CHECK(f.drive == 3 && f.turbo && f.category == 0); // 4WD, turbo, a production car
    game::CarRules r;
    CHECK(game::car_meets_rules(r34, f, r, &why));
    r.stock_only = true, r.max_ps = 500;
    CHECK(game::car_meets_rules(r34, f, r, &why)); // a disc car is stock
    r = {};
    r.max_ps = 200;
    CHECK(!game::car_meets_rules(r34, f, r, &why));
    std::printf("  200 PS limit: %s\n", why.c_str());
    r = {};
    r.drive = 2; // FR only
    CHECK(!game::car_meets_rules(r34, f, r, &why));
    std::printf("  FR only: %s\n", why.c_str());
    r.drive = 3; // 4WD
    CHECK(game::car_meets_rules(r34, f, r, &why));
    r = {};
    r.aspiration = 1; // NA only
    CHECK(!game::car_meets_rules(r34, f, r, &why));
    r.aspiration = 2;
    CHECK(game::car_meets_rules(r34, f, r, &why));
    r = {};
    r.category = 2; // race and rally cars only
    CHECK(!game::car_meets_rules(r34, f, r, &why));
    r.category = 1;
    CHECK(game::car_meets_rules(r34, f, r, &why));
    // A disc car is in no class list: classed as GT3 classes a garage car, by kg / PS on a
    // road course (the R34 V-spec: 1560 kg / 336 PS = 4.6, Class A), a rally car on dirt.
    std::printf("  R34 V-spec: %.2f kg/PS, %s\n", double(f.kg_per_ps), game::car_class_text(f, false).c_str());
    CHECK(f.arcade_class == 0 && f.weight_class == 3 && game::car_class(f, false) == 3 && game::car_class(f, true) == 5);
    r = {};
    r.car_class = 3;
    CHECK(game::car_meets_rules(r34, f, r, &why));
    r.car_class = 4;
    CHECK(!game::car_meets_rules(r34, f, r, &why));
    std::printf("  Class S only: %s\n", why.c_str());
    r.dirt_course = true; // on dirt: a rally car, and not refused there
    CHECK(!game::car_meets_rules(r34, f, r, &why));
    r.car_class = 5;
    CHECK(game::car_meets_rules(r34, f, r, &why));
    r.car_class = 0;
    CHECK(game::car_meets_rules(r34, f, r, &why));
    // A garage car counts as tuned only when its parts or settings differ from the stock car's.
    game::OnlineCar garage = r34;
    garage.kind = game::OnlineCar::Kind::Garage;
    garage.parts = db.car("r34_gtr_v_99")->parts;
    garage.settings = sim::stock_settings(db, "r34_gtr_v_99", garage.parts);
    r = {};
    r.stock_only = true;
    CHECK(game::car_meets_rules(garage, game::online_car_facts(db, nullptr, garage), r, &why));
    garage.settings.final_drive += 100;
    CHECK(!game::car_meets_rules(garage, game::online_car_facts(db, nullptr, garage), r, &why));
    // A mod car this machine lacks passes what needs the car; mods off refuses it.
    game::OnlineCar mod;
    mod.kind = game::OnlineCar::Kind::Mod;
    mod.car = "not_on_this_disc";
    r = {};
    r.max_ps = 200, r.drive = 2, r.car_class = 1;
    CHECK(game::car_meets_rules(mod, game::online_car_facts(db, nullptr, mod), r, &why));
    r.allow_mods = false;
    CHECK(!game::car_meets_rules(mod, game::online_car_facts(db, nullptr, mod), r, &why));
}

TEST(net_car_rules_arcade_classes) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));
    game::GtData gd;
    const Volume& vol = disc->volume();
    CHECK(gd.load([&](const std::string& p) -> std::optional<Bytes> {
        if (p == Vfs::kProgramFile) return vol.read_program();
        const Volume::Entry* e = vol.find(p);
        if (!e) return std::nullopt;
        return vol.read(*e);
    }));
    // Every car of the road and rally class lists is an arcade car on its base car.
    int total = 0;
    for (int cls = 1; cls <= game::kArcadeClasses; ++cls) {
        auto it = gd.arcade_classes.find(game::arcade_class_tag(cls));
        CHECK(it != gd.arcade_classes.end());
        if (it == gd.arcade_classes.end()) continue;
        for (const game::GtData::ArcadeCar& a : it->second) {
            game::OnlineCar oc = game::online_car_from_arcade(db, a.label, 0);
            if (oc.car.empty()) continue; // region-hidden labels (the Vauxhalls on NTSC-U)
            ++total;
            CHECK(oc.kind == game::OnlineCar::Kind::Arcade && db.car(oc.car));
            CHECK(game::arcade_class_of(gd, oc.arcade) == cls);
            game::CarFacts f = game::online_car_facts(db, &gd, oc);
            CHECK(f.built && f.arcade_class == cls);
        }
    }
    std::printf("  %d arcade class cars\n", total);
    CHECK(total >= 60);
    // The GT-One of Class S: its arcade racing tyres ("_k", arc_racing) grip more than its stock ones.
    game::OnlineCar gt1 = game::online_car_from_arcade(db, "arm_gt_one_race_99", 0);
    CHECK(gt1.kind == game::OnlineCar::Kind::Arcade && gt1.car == "gt_one_race_99");
    std::optional<sim::CarSpec> arc = game::online_car_spec(db, gt1, nullptr, "trialmt");
    CHECK(arc.has_value());
    const race::ExactCarSource src = race::exact_online_source(db, gt1, "trialmt");
    CHECK(src.tire_stage == 10); // (its stage-10 tyres, "_k": arc_racing)
    std::printf("  GT-One: %s, tyre stage %d\n", src.label.c_str(), src.tire_stage);
    game::CarFacts f = game::online_car_facts(db, &gd, gt1);
    CHECK(f.arcade_class == 4 && f.category != 0);
    std::string why;
    game::CarRules r;
    r.car_class = 4;
    CHECK(game::car_meets_rules(gt1, f, r, &why));
    r.car_class = 2;
    CHECK(!game::car_meets_rules(gt1, f, r, &why));
    std::printf("  Class B only: %s\n", why.c_str());
    // The class page's surface rule: road classes not on dirt, the rally class not on roads.
    r = {};
    r.dirt_course = true;
    CHECK(!game::car_meets_rules(gt1, f, r, &why));
    std::printf("  on dirt: %s\n", why.c_str());
    game::OnlineCar wrc = game::online_car_from_arcade(db, "impreza_wrc_99_304", 0);
    game::CarFacts fw = game::online_car_facts(db, &gd, wrc);
    CHECK(fw.arcade_class == 5);
    CHECK(game::car_meets_rules(wrc, fw, r, &why));
    r.dirt_course = false;
    CHECK(!game::car_meets_rules(wrc, fw, r, &why));
    // The stock car of an arcade car is in no list: it has its weight's class.
    game::OnlineCar stock_gt1 = game::online_car_from_db(db, "gt_one_race_99", 0);
    game::CarFacts fs = game::online_car_facts(db, &gd, stock_gt1);
    CHECK(fs.arcade_class == 0 && fs.weight_class == 4 && game::car_class(fs, false) == 4);
    // The weight rule against the road class lists: nearly every listed car is in the class
    // its weight per power gives (the misses sit near a boundary).
    int same = 0, listed = 0;
    for (int cls = 1; cls < game::kArcadeClasses; ++cls)
        for (const game::GtData::ArcadeCar& a : gd.arcade_classes[game::arcade_class_tag(cls)]) {
            game::OnlineCar oc = game::online_car_from_arcade(db, a.label, 0);
            if (oc.car.empty()) continue;
            game::CarFacts fa = game::online_car_facts(db, &gd, oc);
            ++listed;
            same += fa.weight_class == cls;
        }
    std::printf("  road class cars in their weight's class: %d of %d\n", same, listed);
    CHECK(listed >= 50 && same >= listed - 6);
    // Every car of the disc has a class.
    int classed = 0;
    for (const data::Car& c : db.cars())
        classed += game::car_class(game::online_car_facts(db, &gd, game::online_car_from_db(db, c.label, 0)), false) > 0;
    std::printf("  cars with a class: %d of %zu\n", classed, db.cars().size());
    CHECK(classed + 2 >= int(db.cars().size()));
}

TEST(net_settings_carry_rules) {
    FakeClock clock;
    net::Session host, c1;
    if (!host.host(0, "Host", "a", 0)) SKIP("no UDP sockets here");
    c1.join("127.0.0.1:" + std::to_string(host.port()), "One", "b", 0);
    CHECK(pump({&host, &c1}, [&] { return c1.my_id() == 1; }));
    net::Settings st;
    st.course = "trialmt";
    st.stock_only = true;
    st.max_ps = 350;
    st.allow_mods = false;
    st.car_class = 2, st.category = 1, st.drive = 4, st.aspiration = 2;
    st.ghost_ping_ms = 0;
    host.set_settings(st);
    CHECK(pump({&host, &c1}, [&] { return c1.settings().max_ps == 350; }));
    CHECK(c1.settings().stock_only && !c1.settings().allow_mods && c1.settings().ghost_ping_ms == 0);
    CHECK(c1.settings().car_class == 2 && c1.settings().category == 1 && c1.settings().drive == 4 &&
          c1.settings().aspiration == 2);
}

// --- cheat warnings --------------------------------------------------------------------

namespace {
net::CarSnapshot snap_at(u32 tick, sim::Vec3 pos, sim::Vec3 vel, bool respawned = false) {
    net::CarSnapshot s;
    s.tick = tick;
    s.race_time = float(tick / 60.0);
    s.state.position = pos;
    s.state.velocity = vel;
    s.respawned = respawned;
    return s;
}
} // namespace

TEST(net_plausibility_flags) {
    const float top = 70; // m/s (252 km/h)
    // An honest straight at top speed: nothing.
    {
        race::Plausibility p;
        p.set_car(top);
        float x = 0;
        for (u32 t = 1; t < 600; ++t) {
            x += top / 60;
            CHECK(!p.on_state(snap_at(t, {x, 0, 0}, {top, 0, 0}), false));
        }
    }
    // 30 % over its top speed for a second: Speed, once.
    {
        race::Plausibility p;
        p.set_car(top);
        float x = 0;
        int flags = 0;
        for (u32 t = 1; t < 240; ++t) {
            float v = t > 60 ? top * 1.3f + 5 : top;
            x += v / 60;
            if (auto f = p.on_state(snap_at(t, {x, 0, 0}, {v, 0, 0}), false)) {
                CHECK(f->kind == net::FlagKind::Speed);
                ++flags;
            }
        }
        CHECK(flags == 1);
    }
    // A brief spike (a shove) does not count.
    {
        race::Plausibility p;
        p.set_car(top);
        float x = 0;
        for (u32 t = 1; t < 240; ++t) {
            float v = t > 60 && t < 75 ? top * 1.5f : top * 0.9f;
            x += v / 60;
            CHECK(!p.on_state(snap_at(t, {x, 0, 0}, {v, 0, 0}), false));
        }
    }
    // A 20 m jump: Teleport; the same jump announced as a respawn: nothing.
    for (bool resp : {false, true}) {
        race::Plausibility p;
        p.set_car(top);
        CHECK(!p.on_state(snap_at(10, {0, 0, 0}, {20, 0, 0}), false));
        auto f = p.on_state(snap_at(11, {20.33f + 20, 0, 0}, {20, 0, 0}, resp), false);
        if (resp) CHECK(!f);
        else CHECK(f && f->kind == net::FlagKind::Teleport && f->value > 15);
    }
    // Gaining 20 m/s in half a second (4 g): Accel; braking that hard: nothing.
    for (bool gain : {true, false}) {
        race::Plausibility p;
        p.set_car(top);
        float v = gain ? 10.0f : 50.0f, x = 0;
        bool flagged = false;
        for (u32 t = 1; t < 120; ++t) {
            if (t > 30 && t <= 60) v += (gain ? 40.0f : -40.0f) / 60;
            x += v / 60;
            if (auto f = p.on_state(snap_at(t, {x, 0, 0}, {v, 0, 0}), false)) flagged = flagged || f->kind == net::FlagKind::Accel;
        }
        CHECK(flagged == gain);
    }
    // Physics disagreeing by 8 m for 3 s: Motion; for half a second (a pile-up): nothing.
    {
        race::Plausibility p;
        int motion = 0;
        for (int i = 0; i < 30; ++i)
            if (p.on_prediction_error(i < 30 ? 8.0f : 0.0f, 1.0 / 60, false)) ++motion;
        CHECK(motion == 0);
        for (int i = 0; i < 200; ++i)
            if (auto f = p.on_prediction_error(8.0f, 1.0 / 60, false)) motion += f->kind == net::FlagKind::Motion;
        CHECK(motion == 1);
    }
    // Finish claimed 5 s before we saw it: FinishTime; 0.3 s (latency): nothing.
    {
        race::Plausibility p;
        CHECK(!p.on_finish(100.0, 100.3));
        race::Plausibility q;
        auto f = q.on_finish(95.0, 100.0);
        CHECK(f && f->kind == net::FlagKind::FinishTime);
    }
}

TEST(net_flags_and_kick) {
    FakeClock clock;
    net::Session host, c1, c2;
    if (!three(host, c1, c2)) SKIP("no UDP sockets here");
    c1.report_flag(2, net::FlagKind::Speed, 412);
    host.report_flag(2, net::FlagKind::Teleport, 30);
    std::vector<net::Event> eh;
    CHECK(pump({&host, &c1, &c2}, [&] {
        for (auto& e : drain(host)) eh.push_back(e);
        return host.flags().size() == 2;
    }));
    CHECK(host.flags()[0].suspect == 2 || host.flags()[1].suspect == 2);
    bool by_c1 = false;
    for (const auto& f : host.flags()) by_c1 = by_c1 || (f.reporter == 1 && f.kind == net::FlagKind::Speed && f.reporter_name == "One" && f.suspect_name == "Two");
    CHECK(by_c1);
    // The same report again adds no line.
    c1.report_flag(2, net::FlagKind::Speed, 415);
    pump({&host, &c1, &c2}, [&] { return false; });
    CHECK(host.flags().size() == 2);
    // The host removes Two: Two is told, everyone's roster drops it.
    host.kick(2);
    std::vector<net::Event> e2;
    CHECK(pump({&host, &c1, &c2}, [&] {
        for (auto& e : drain(c2)) e2.push_back(e);
        return has(e2, net::Event::Kind::Rejected) && c1.players().size() == 2;
    }));
    for (const net::Event& e : e2)
        if (e.kind == net::Event::Kind::Rejected) CHECK(e.text == "removed by the host");
    CHECK(!c2.active() && host.players().size() == 2);
}

TEST(net_relay_rebind_after_nonce_expiry) {
    // A join's relay channel outlives the join's nonce: its bound ends may refresh the binding
    // (the periodic re-bind used to be refused once the server forgot the nonce).
    FakeClock clock;
    net::RelayServer srv;
    net::RelayServer::Limits lim;
    lim.nonce_timeout_ms = 200;
    srv.set_limits(lim);
    if (!srv.open(0)) SKIP("no UDP sockets here");
    RvProbe hs(srv), cs(srv), other(srv);
    std::string code = hs.reg(777);
    CHECK(!code.empty());
    CHECK(cs.lookup(code) && cs.type == net::Rv::HostInfo);
    u64 nonce = net::get_u64(cs.body);
    CHECK(hs.recv() && hs.type == net::Rv::Incoming);
    auto bind = [&](RvProbe& p) {
        return p.ask_for(net::Rv::Bind, net::Rv::Bound, [&](net::Writer& w) { fill_bind(w, nonce, code, 0, 0, 0); });
    };
    CHECK(bind(hs) && bind(cs));
    FakeClock::advance(1500); // > 1 s: the nonce is expired
    srv.step(0);
    CHECK(srv.stats().pending == 0);
    CHECK(bind(hs)); // still allowed: it is one of the channel's ends
    CHECK(!bind(other)); // a stranger with the (now stale) nonce is not
}

TEST(net_relay_cookie_rollover) {
    // Cookies go stale every window: the game answers each Challenge and resends (register,
    // lookup, binds, keepalives) without the player noticing.
    RvParty p;
    net::RelayServer::Limits lim;
    lim.cookie_window_ms = 150;
    lim.session_timeout_ms = 6000;
    p.srv.set_limits(lim);
    if (!p.start(true)) SKIP("no UDP sockets here");
    CHECK(p.c1.link(0).relay);
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.c1.link(2).relay && p.c2.link(1).relay; }));
    // The host's keepalive (every 5 s) is challenged too, and still keeps the session.
    double until = net::now_ms() + 7000;
    while (net::now_ms() < until) pump_rv(p.srv, p.all(), [&] { return net::now_ms() >= until; });
    CHECK(p.srv.stats().sessions == 1);
    std::printf("  %llu challenges\n", (unsigned long long)p.srv.stats().challenges);
    CHECK(p.srv.stats().challenges >= 5);
    CHECK(p.host.players().size() == 3 && p.c1.active() && p.c2.active());
}

// --- relay link slots and the queue -------------------------------------------------

namespace {

// Six players through a relay server, every pair relayed (all behind symmetric NATs): 15 links.
struct RvSix {
    FakeClock clock; // first: outlives the sessions
    net::RelayServer srv;
    net::Session s[6];
    std::string server;
    std::vector<net::Session*> all() { return {&s[0], &s[1], &s[2], &s[3], &s[4], &s[5]}; }
    // pair_channels false: the players' pairs never get their own channel (still punching), so
    // the host forwards every state: its channels carry five streams.
    bool start(const net::RelayServer::Limits& lim, bool pair_channels = true) {
        srv.set_limits(lim);
        if (!srv.open(0)) return false;
        server = "127.0.0.1:" + std::to_string(srv.port());
        if (!s[0].host_via(server, "Host", "a", 0)) return false;
        if (!pump_rv(srv, {&s[0]}, [&] { return !s[0].code().empty(); })) return false;
        for (int i = 1; i < 6; ++i) {
            s[i].set_block_direct(true);
            s[i].set_punch_give_up_ms(pair_channels ? 300 : 1e9);
            s[i].join_code(server, s[0].code(), "P" + std::to_string(i), "b", 0);
            std::vector<net::Session*> some = all();
            some.resize(size_t(i) + 1);
            if (!pump_rv(srv, some, [&] { return s[i].my_id() == i; })) return false;
        }
        return pump_rv(srv, all(), [&] {
            for (int i = 1; i < 6; ++i)
                for (int j = 1; j < 6; ++j)
                    if (i != j && (pair_channels ? !s[i].link(u8(j)).relay : !s[i].link(u8(j)).known)) return false;
            return s[0].relay_links_needed() == 15;
        });
    }
    // Everyone ready, the host starts: true once every client got the Load.
    bool load() {
        for (int i = 1; i < 6; ++i) s[i].set_ready(true);
        if (!pump_rv(srv, all(), [&] { return s[0].all_ready(); })) return false;
        for (net::Session* x : all()) drain(*x);
        s[0].load();
        bool loaded[6] = {};
        return pump_rv(srv, all(), [&] {
            bool every = true;
            for (int i = 0; i < 6; ++i) {
                loaded[i] = loaded[i] || has(drain(s[i]), net::Event::Kind::Load);
                every = every && loaded[i];
            }
            return every;
        });
    }
};

// A car state as a race sends it: every field moving, three different frames of history (analog steering).
net::CarSnapshot race_state(u32 tick, int player) {
    net::CarSnapshot s;
    s.tick = tick;
    s.clock = s32(tick * 50);
    float t = float(tick) / 60.0f + float(player);
    net::CarMotion& m = s.motion;
    m.pos = {fbits(100 * std::sin(t * 0.1f)), fbits(-100 * std::cos(t * 0.1f)), fbits(2 + 0.1f * std::sin(t))};
    m.vel = {fbits(30 * std::cos(t * 0.1f)), fbits(30 * std::sin(t * 0.1f)), fbits(0.2f)};
    m.heading = fbits(t * 0.1f), m.pitch = fbits(0.01f * std::sin(t)), m.roll = fbits(0.02f * std::cos(t));
    m.yaw_rate = fbits(0.1f), m.omega = fbits(600 + 50 * std::sin(t)), m.steer_cmd = fbits(0.1f * std::sin(t));
    m.axle_speed = {fbits(90), fbits(91)};
    m.pitch_rate = s16(100 * std::sin(t)), m.roll_rate = s16(50 * std::cos(t));
    for (size_t w = 0; w < 4; ++w) m.rim_speed[w] = s16(3840 + w), m.susp[w] = s16(-6000 + 100 * std::sin(t));
    m.gear = 4, m.clutch = 1;
    s.laps = 1;
    s.input_count = net::kInputHistory;
    for (int i = 0; i < net::kInputHistory; ++i)
        s.inputs[i] = {0x1F, 2, 0, s16(2000 * std::sin(t - 0.02f * float(i))), 3800, 0, 0, 0};
    return s;
}

} // namespace

// A fully relayed six-player race at 60 Hz (states, pings, a contact now and then): what one
// relay link carries. Returns the busiest channel direction (bytes/s, 1 s windows).
static double relayed_race(bool pair_channels) {
    RvSix p;
    net::RelayServer::Limits lim;
    if (!p.start(lim, pair_channels)) SKIP("no UDP sockets here");
    CHECK(p.load());
    CHECK(p.srv.stats().links_used == 15 && p.srv.stats().reservations == 1);
    for (net::Session* x : p.all()) x->loaded();
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.s[0].all_loaded(); }));
    p.s[0].go(100);
    // 60 ticks per simulated second, each also given a tick of real time (never caught up in
    // bursts: a slow machine stretches the race instead of crowding the relay's windows).
    auto race = [&](int ticks, u32& tick) {
        for (int n = 0; n < ticks; ++n) {
            ++tick;
            for (int i = 0; i < 6; ++i) {
                p.s[i].send_state(race_state(tick, i));
                if (tick % 30 == u32(i) * 5) // a contact every half second from each player
                    p.s[i].send_contact(u8((i + 1) % 6), {tick, 1, 2, 300, -40});
                p.s[i].poll();
            }
            p.srv.step(0);
            step_clock(1000.0 / 60);
        }
    };
    u32 tick = 0;
    race(60, tick); // warm up
    net::RelayServer::Stats a = p.srv.stats();
    p.srv.reset_peak();
    double t0 = net::now_ms();
    race(180, tick);
    p.srv.step(0);
    double secs = (net::now_ms() - t0) / 1000;
    net::RelayServer::Stats b = p.srv.stats();
    double datagrams = double(b.relayed_datagrams - a.relayed_datagrams);
    double bytes = double(b.relayed_bytes - a.relayed_bytes);
    int channels = pair_channels ? 15 : 5;
    double per_dir = bytes / (channels * 2 * secs); // bytes/s per channel and direction
    std::printf("  %s: %.0f datagrams/s relayed (%.1f per channel and direction), %.0f bytes each (relay header "
                "included)\n",
                pair_channels ? "15 channels" : "5 host channels, the host forwarding", datagrams / secs,
                datagrams / (channels * 2 * secs), bytes / datagrams);
    std::printf("  per channel and direction: %.1f kbit/s average, %.1f kbit/s peak (1 s windows); "
                "both directions + IP/UDP headers: %.0f kbit/s\n",
                per_dir * 8 / 1e3, b.peak_channel_bytes_per_s * 8 / 1e3,
                2 * (bytes + 28 * datagrams) / (channels * 2 * secs) * 8 / 1e3);
    CHECK(b.dropped == a.dropped);
    if (pair_channels) CHECK(datagrams / (30 * secs) > 55 && datagrams / (30 * secs) < 75); // one per tick and direction
    // Back in the lobby the slots are free.
    p.s[0].lobby();
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.srv.stats().links_used == 0; }));
    return b.peak_channel_bytes_per_s;
}

TEST(net_relay_traffic_per_link) {
    // Every pair on its own channel (a race once punching settled): one stream each way.
    double link = relayed_race(true);
    // The busiest legitimate channel: the host still forwarding four other players' states to a
    // player (their pair channels not up yet). The racing guard sits at about twice that.
    double busiest = relayed_race(false);
    double guard = net::RelayServer::Limits{}.channel_bytes_per_s;
    CHECK(busiest > link * 3);
    CHECK(busiest * 1.6 < guard && busiest * 2.5 > guard);
}

namespace {

// What a relay server driven by RelayServer::receive / tick sends, per destination, in order
// (RelayServer::set_send_hook): no sockets, so nothing arrives late or out of turn.
std::map<std::string, std::deque<Bytes>> g_mail;

void use_mailboxes(net::RelayServer& srv) {
    g_mail.clear();
    srv.set_send_hook([](const net::Address& to, ByteSpan d) { g_mail[to.to_string()].emplace_back(d.begin(), d.end()); });
}

// A host (or player) talking to a relay server on a test clock: requests go in through
// RelayServer::receive with `now`, replies land in its mailbox (use_mailboxes) synchronously.
struct SlotHost {
    net::RelayServer& srv;
    net::Address addr;
    std::string code;
    u64 token = 0;
    net::GameVersion version; // sent in every request
    net::RvSlot state{};
    int links = 0, position = 0, wait_s = -1;
    std::string text;
    explicit SlotHost(net::RelayServer& s) : srv(s) {
        static int port = 40000;
        net::resolve("127.0.0.1:" + std::to_string(port++), 0, addr);
    }
    void send(net::Rv type, double now, const std::function<void(net::Writer&)>& fill) {
        net::Writer w = net::rv_request(type, srv.cookie_for(addr, now), version);
        fill(w);
        net::rv_pad(w);
        srv.receive(addr, w.data, now);
    }
    // The newest reply of this type since the last call (every other datagram is dropped).
    bool reply(net::Rv want, Bytes* out = nullptr) {
        bool got = false;
        std::deque<Bytes>& box = g_mail[addr.to_string()];
        for (; !box.empty(); box.pop_front()) {
            const Bytes& b = box.front();
            net::Rv type;
            net::Reader r(b);
            if (!net::rv_parse(b, type, r) || type != want) continue;
            got = true;
            if (out) *out = b;
            if (type == net::Rv::Reservation) {
                state = net::RvSlot(r.u8_());
                links = r.u8_(), position = r.u16_();
                u16 w = r.u16_();
                wait_s = w == net::kWaitUnknown ? -1 : w;
                text = state == net::RvSlot::Refused ? r.str() : "";
            }
        }
        return got;
    }
    bool reg(u64 tok, double now) {
        token = tok;
        send(net::Rv::Register, now, [&](net::Writer& w) {
            net::put_u64(w, tok);
            net::put_endpoints(w, {});
        });
        Bytes b;
        if (!reply(net::Rv::Registered, &b)) return false;
        net::Rv type;
        net::Reader r(b);
        net::rv_parse(b, type, r);
        code = r.str();
        return !code.empty();
    }
    bool reserve(int n, double now) {
        send(net::Rv::Reserve, now, [&](net::Writer& w) {
            w.str(code);
            net::put_u64(w, token);
            w.u8_(u8(n));
        });
        return reply(net::Rv::Reservation);
    }
    void keepalive(double now) {
        send(net::Rv::Keepalive, now, [&](net::Writer& w) {
            w.str(code);
            net::put_u64(w, token);
        });
    }
    void release(double now) {
        send(net::Rv::Release, now, [&](net::Writer& w) {
            w.str(code);
            net::put_u64(w, token);
        });
    }
};

} // namespace

TEST(net_relay_slots_grant_release) {
    // Slots are granted when they fit and freed by Release, by the session closing, and when the
    // reservation's channels carry nothing for reservation_idle_ms. Traffic keeps them.
    net::RelayServer srv;
    net::RelayServer::Limits lim;
    lim.relay_links = 4;
    lim.reservation_idle_ms = 10000;
    srv.set_limits(lim);
    use_mailboxes(srv); // never opened: driven by receive / tick with the test clock `t`
    double t = 1e6;
    SlotHost a(srv), b(srv), c(srv), player(srv);
    CHECK(a.reg(11, t) && b.reg(12, t) && c.reg(13, t));
    CHECK(a.reserve(3, t) && a.state == net::RvSlot::Granted && a.links == 3);
    CHECK(srv.stats().links_used == 3 && srv.links_of(a.code) == 3);
    // Asking again (a resend) changes nothing; the keepalive reply says granted too.
    CHECK(a.reserve(3, t) && a.state == net::RvSlot::Granted && srv.stats().links_used == 3);
    a.keepalive(t);
    CHECK(a.reply(net::Rv::Reservation) && a.state == net::RvSlot::Granted);
    // Malformed and foreign requests: refused without touching the slots.
    CHECK(!b.reserve(0, t) && srv.stats().links_used == 3);  // 0 links: BadRequest (an Error)
    b.token = 999;
    CHECK(b.reserve(1, t) && b.state == net::RvSlot::Refused && b.text == "no such session");
    b.token = 12;
    // Release frees them; Release needs the token (another session's is ignored).
    b.send(net::Rv::Release, t, [&](net::Writer& w) {
        w.str(a.code);
        net::put_u64(w, 12);
    });
    CHECK(srv.stats().links_used == 3);
    a.release(t);
    CHECK(srv.stats().links_used == 0 && srv.stats().reservations == 0);
    // Unregister frees them.
    CHECK(b.reserve(2, t) && b.state == net::RvSlot::Granted);
    b.send(net::Rv::Unregister, t, [&](net::Writer& w) {
        w.str(b.code);
        net::put_u64(w, b.token);
    });
    CHECK(srv.stats().links_used == 0 && srv.stats().sessions == 2);
    // Idle: a granted reservation whose channel carries traffic stays; silent, it goes.
    CHECK(c.reserve(1, t) && c.state == net::RvSlot::Granted);
    player.send(net::Rv::Lookup, t, [&](net::Writer& w) {
        w.str(c.code);
        w.u16_(net::kProtocolVersion);
        net::put_endpoints(w, {});
    });
    Bytes hi;
    CHECK(player.reply(net::Rv::HostInfo, &hi));
    net::Rv type;
    net::Reader r(hi);
    net::rv_parse(hi, type, r);
    u64 nonce = net::get_u64(r);
    for (SlotHost* end : {&c, &player})
        end->send(net::Rv::Bind, t, [&](net::Writer& w) { fill_bind(w, nonce, c.code, 0, 0, 0); });
    CHECK(srv.stats().channels == 1);
    net::Writer d;
    d.bytes(ByteSpan(reinterpret_cast<const u8*>(net::kRelayMagic), 4));
    net::put_u64(d, nonce);
    d.bytes(ByteSpan(reinterpret_cast<const u8*>("GT3N"), 4));
    d.data.resize(120, 0);
    for (int s = 1; s <= 30; ++s) { // 30 s of a race: one datagram a second is enough to hold
        t += 1000;
        srv.receive(player.addr, d.data, t);
        c.keepalive(t);
        srv.tick(t);
    }
    CHECK(srv.links_of(c.code) == 1 && srv.stats().relayed_datagrams == 30);
    t += 11000; // the race is over and nobody said so
    c.keepalive(t);
    srv.tick(t);
    CHECK(srv.stats().links_used == 0 && srv.stats().reservations == 0);
    // The race measured itself: the typical hold time moved from 4 min towards 30 s.
    CHECK(srv.stats().race_estimate_ms < lim.race_estimate_ms);
    // Lobby traffic after that still flows (the channel is idle, not closed).
    srv.receive(player.addr, d.data, t + 1);
    CHECK(srv.stats().relayed_datagrams == 31);
}

TEST(net_relay_queue_order_and_skip) {
    // FIFO, but a smaller lobby may start ahead of the head when the head still starts within
    // a minute (estimated from the running races' expected ends).
    net::RelayServer srv;
    net::RelayServer::Limits lim;
    lim.relay_links = 10;
    lim.race_estimate_ms = 240000;
    lim.reservation_idle_ms = 1e9; // no channels in this test
    srv.set_limits(lim);
    use_mailboxes(srv); // never opened: driven by receive / tick with the test clock `t`
    double t0 = 1e6, t = t0;
    SlotHost a(srv), b(srv), c(srv), d(srv);
    CHECK(a.reg(1, t) && b.reg(2, t) && c.reg(3, t) && d.reg(4, t));
    CHECK(a.reserve(8, t) && a.state == net::RvSlot::Granted);
    CHECK(b.reserve(6, t) && b.state == net::RvSlot::Queued && b.position == 1);
    // B starts when A ends: about 4 min.
    CHECK(b.wait_s >= 235 && b.wait_s <= 241);
    CHECK(net::relay_wait_text({true, b.position, b.wait_s}) == "Relay busy: 1st in line, about 4 min");
    // C needs 2 and 2 are free, but B would then still wait 4 min: C queues behind B.
    CHECK(c.reserve(2, t) && c.state == net::RvSlot::Queued && c.position == 2);
    CHECK(srv.queue_info(c.code, t).position == 2 && srv.queue_info(c.code, t).wait_ms > 200000);
    // 3 min 10 s later B is due within a minute: C may go first now.
    auto keep = [&](double until) { // the queued hosts keep their places with keepalives
        while (t < until) {
            t = std::min(until, t + 10000);
            for (SlotHost* h : {&a, &b, &c, &d}) h->keepalive(t);
            srv.tick(t);
        }
    };
    keep(t0 + 190000);
    CHECK(srv.links_of(c.code) == 2 && srv.queue_info(c.code, t).position == 0);
    CHECK(c.reply(net::Rv::Reservation) && c.state == net::RvSlot::Granted); // told unasked
    CHECK(srv.queue_info(b.code, t).position == 1);
    // A's race ends: B gets its 6 (8 free, C holds 2).
    a.release(t);
    CHECK(srv.links_of(b.code) == 6 && srv.stats().queued == 0 && srv.stats().links_used == 8);
    CHECK(b.reply(net::Rv::Reservation) && b.state == net::RvSlot::Granted);
    b.release(t), c.release(t);
    CHECK(srv.stats().links_used == 0);

    // Unknown estimates (every running race overdue): smaller lobbies may go first only while the
    // head has waited less than a minute.
    CHECK(a.reserve(8, t) && a.state == net::RvSlot::Granted);
    t += 241000; // A is now longer than a typical race: its end is unknown
    keep(t);
    CHECK(b.reserve(6, t) && b.state == net::RvSlot::Queued && b.wait_s == -1);
    CHECK(net::relay_wait_text({true, b.position, b.wait_s}) == "Relay busy: 1st in line");
    CHECK(c.reserve(1, t) && c.state == net::RvSlot::Granted); // B has waited 0 s
    keep(t + 61000);
    CHECK(d.reserve(1, t) && d.state == net::RvSlot::Queued && d.position == 2); // B has waited 61 s
    CHECK(srv.queue_info(b.code, t).position == 1);
}

TEST(net_relay_queue_cap_and_expiry) {
    // The queue holds max_queue lobbies; a queued host must keep sending keepalives, and nobody
    // waits longer than queue_timeout_ms.
    net::RelayServer srv;
    net::RelayServer::Limits lim;
    lim.relay_links = 1;
    lim.max_queue = 2;
    lim.reservation_idle_ms = 1e9;
    srv.set_limits(lim);
    use_mailboxes(srv); // never opened: driven by receive / tick with the test clock `t`
    double t = 1e6;
    SlotHost a(srv), b(srv), c(srv), d(srv);
    CHECK(a.reg(1, t) && b.reg(2, t) && c.reg(3, t) && d.reg(4, t));
    CHECK(a.reserve(1, t) && a.state == net::RvSlot::Granted);
    CHECK(b.reserve(1, t) && b.state == net::RvSlot::Queued && b.position == 1);
    CHECK(c.reserve(1, t) && c.state == net::RvSlot::Queued && c.position == 2);
    CHECK(d.reserve(1, t) && d.state == net::RvSlot::Refused && d.text.find("busy") != std::string::npos);
    CHECK(srv.stats().queued == 2);
    // Too large for the whole relay: refused outright, not queued.
    CHECK(d.reserve(2, t) && d.state == net::RvSlot::Refused && srv.stats().queued == 2);
    // B keeps pinging, C stops: after the ping timeout C has lost its place, B moved up.
    for (int i = 0; i < 3; ++i) {
        t += 6000;
        a.keepalive(t), b.keepalive(t), d.keepalive(t);
        srv.tick(t);
    }
    CHECK(srv.stats().queued == 1 && srv.queue_info(c.code, t).position == 0);
    CHECK(srv.queue_info(b.code, t).position == 1 && srv.stats().queue_expired == 1);
    CHECK(b.reply(net::Rv::Reservation) && b.state == net::RvSlot::Queued); // the keepalive's answer
    // C's session itself has not expired (60 s): asking again puts it at the back.
    CHECK(c.reserve(1, t) && c.state == net::RvSlot::Queued && c.position == 2);
    // Ten minutes in the queue at most, even while pinging.
    for (double end = t + 600000; t < end;) {
        t += 10000;
        for (SlotHost* h : {&a, &b, &c, &d}) h->keepalive(t);
        srv.tick(t);
    }
    CHECK(srv.queue_info(b.code, t).position == 0 && srv.stats().queue_expired >= 2);
    // A Release takes a lobby out of the queue at once.
    CHECK(d.reserve(1, t) && d.state == net::RvSlot::Queued);
    d.release(t);
    CHECK(srv.queue_info(d.code, t).position == 0);
    // A session that ends while queued leaves the queue too.
    CHECK(d.reserve(1, t) && d.state == net::RvSlot::Queued);
    d.send(net::Rv::Unregister, t, [&](net::Writer& w) {
        w.str(d.code);
        net::put_u64(w, d.token);
    });
    CHECK(srv.queue_info(d.code, t).position == 0);
}

TEST(net_relay_bench_flag_loopback_only) {
    // --bench lets loopback sources past the per-IP limits (a local load test), nobody else.
    for (bool bench : {false, true}) {
        net::RelayServer srv; // never opened: replies go nowhere
        net::RelayServer::Limits lim;
        lim.max_sessions_per_ip = 1;
        lim.ip_msgs_burst = 4;
        lim.bench = bench;
        srv.set_limits(lim);
        double t = 1e6;
        for (const char* ip : {"127.0.0.1", "10.1.2.3"})
            for (int i = 0; i < 6; ++i) {
                net::Address from;
                net::resolve(std::string(ip) + ":" + std::to_string(3000 + i), 0, from);
                net::Writer w = net::rv_request(net::Rv::Register, srv.cookie_for(from, t));
                net::put_u64(w, u64(100 + i + (ip[0] == '1' && ip[1] == '0' ? 50 : 0)));
                net::put_endpoints(w, {});
                net::rv_pad(w);
                srv.receive(from, w.data, t);
            }
        CHECK(srv.stats().sessions == (bench ? 7u : 2u)); // 10.1.2.3: one, as always
    }
}

TEST(net_relay_direct_lobby_never_queues) {
    // Every pair connects directly: the race needs no slots and starts even with the relay full.
    RvParty p;
    net::RelayServer::Limits lim;
    lim.relay_links = 1;
    p.srv.set_limits(lim);
    if (!p.start()) SKIP("no UDP sockets here");
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.host.relay_links_needed() == 0; })); // both reported direct
    RvProbe other(p.srv); // another lobby holds the only slot
    std::string code = other.reg(4242);
    CHECK(other.ask(net::Rv::Reserve, [&](net::Writer& w) {
        w.str(code);
        net::put_u64(w, 4242);
        w.u8_(1);
    }));
    CHECK(p.srv.stats().links_used == 1);
    p.c1.set_ready(true), p.c2.set_ready(true);
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.host.all_ready(); }));
    drain(p.host);
    p.host.load();
    CHECK(!p.host.relay_wait().waiting && has(drain(p.host), net::Event::Kind::Load));
    CHECK(p.srv.stats().queued == 0 && p.srv.stats().granted == 1);
}

TEST(net_relay_lobby_waits_for_full_relay) {
    // One player behind a symmetric NAT: its link to the host and to the other player go through
    // the relay (2 links). The relay is full: the race waits in line, everyone sees it, and it
    // starts by itself when another lobby's race ends. The host can also give up waiting.
    RvParty p;
    net::RelayServer::Limits lim;
    lim.relay_links = 3;
    lim.reservation_idle_ms = 60000; // the other lobby's reservation has no channels
    p.srv.set_limits(lim);
    if (!p.start(true)) SKIP("no UDP sockets here");
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.host.relay_links_needed() == 2; }));
    RvProbe other(p.srv);
    std::string code = other.reg(4343);
    auto other_reserve = [&](int n) {
        return other.ask(net::Rv::Reserve, [&](net::Writer& w) {
            w.str(code);
            net::put_u64(w, 4343);
            w.u8_(u8(n));
        });
    };
    CHECK(other_reserve(2) && p.srv.stats().links_used == 2);
    p.c1.set_ready(true), p.c2.set_ready(true);
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.host.all_ready(); }));
    for (net::Session* x : p.all()) drain(*x);
    p.host.load();
    CHECK(p.host.relay_wait().waiting);
    // In line: the host hears its place; the players see the host waiting (roster).
    CHECK(pump_rv(p.srv, p.all(), [&] {
        return p.host.relay_wait().position == 1 && p.c1.relay_wait().position == 1 && p.c2.relay_wait().position == 1;
    }));
    std::printf("  host: \"%s\"\n", net::relay_wait_text(p.host.relay_wait()).c_str());
    CHECK(net::relay_wait_text(p.c1.relay_wait()).rfind("Relay busy: 1st in line", 0) == 0);
    CHECK(p.srv.stats().queued == 1);
    for (net::Session* x : p.all()) CHECK(!has(drain(*x), net::Event::Kind::Load));
    // The other race ends: the slots are granted and the race loads everywhere.
    other.send(net::Rv::Release, [&](net::Writer& w) {
        w.str(code);
        net::put_u64(w, 4343);
    });
    std::vector<net::Event> eh, e1, e2;
    CHECK(pump_rv(p.srv, p.all(), [&] {
        for (auto& e : drain(p.host)) eh.push_back(e);
        for (auto& e : drain(p.c1)) e1.push_back(e);
        for (auto& e : drain(p.c2)) e2.push_back(e);
        return has(eh, net::Event::Kind::Load) && has(e1, net::Event::Kind::Load) && has(e2, net::Event::Kind::Load);
    }));
    CHECK(!p.host.relay_wait().waiting && p.srv.links_of(p.host.code()) == 2);
    CHECK(pump_rv(p.srv, p.all(), [&] { return !p.c2.relay_wait().waiting; }));
    // Back in the lobby: released.
    p.host.lobby();
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.srv.stats().links_used == 0; }));
    // Full again; this time the host gives up waiting: it leaves the queue.
    CHECK(other_reserve(2) && p.srv.stats().links_used == 2);
    p.c1.set_ready(true), p.c2.set_ready(true);
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.host.all_ready(); }));
    p.host.load();
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.srv.stats().queued == 1 && p.c1.relay_wait().waiting; }));
    p.host.cancel_start();
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.srv.stats().queued == 0 && !p.c1.relay_wait().waiting; }));
    CHECK(!p.host.relay_wait().waiting && p.host.in_lobby());
    // A relay that cannot fit the race at all: the host is told, the lobby stays.
    lim.relay_links = 1;
    p.srv.set_limits(lim);
    drain(p.host);
    p.host.load();
    std::vector<net::Event> eq;
    CHECK(pump_rv(p.srv, p.all(), [&] {
        for (auto& e : drain(p.host)) eq.push_back(e);
        return has(eq, net::Event::Kind::Queue);
    }));
    CHECK(!p.host.relay_wait().waiting && p.host.active() && !has(eq, net::Event::Kind::Load));
}

// --- game versions and updates (docs/dev/ONLINE.md "Versions and updates") -------------

namespace {

net::GameVersion ver(const char* text) { return net::parse_game_version(text).value(); }

// A VersionQuery as the game sends it (padded to kVersionQuerySize unless `pad` says otherwise).
Bytes version_query(const net::GameVersion& v, net::RvPlatform platform, size_t pad = net::kVersionQuerySize) {
    net::Writer w = net::rv_request(net::Rv::VersionQuery, 0, v);
    w.u8_(u8(platform));
    net::rv_pad(w, pad);
    return w.data;
}

net::RelayServer::Versions versions(const char* latest, const char* min_online, const char* url) {
    net::RelayServer::Versions v;
    v.latest = ver(latest);
    v.min_online = ver(min_online);
    v.release_url = url;
    return v;
}

const char* const kUrl = "https://example.org/gt3r/releases/latest";

} // namespace

TEST(net_version_compare) {
    net::GameVersion v = ver("0.2.0-dev");
    CHECK(v.major == 0 && v.minor == 2 && v.patch == 0 && v.suffix == "-dev");
    CHECK(v.number() == "0.2.0" && v.to_string() == "0.2.0-dev");
    CHECK(ver("10.20.30").to_string() == "10.20.30" && ver("1.2.3-rc.1").suffix == "-rc.1");
    for (const char* bad : {"", "1", "1.2", "1.2.", "1..3", "a.b.c", "1.2.3-", "1.2.3 ", "1.2.3x", "65536.0.0", "1.2.3-a b",
                            "-1.2.3", "1.2.3.4"})
        CHECK(!net::parse_game_version(bad));
    CHECK(net::compare_core(ver("0.10.0"), ver("0.9.9")) > 0); // numeric, not text
    CHECK(net::compare_core(ver("1.0.0"), ver("0.99.99")) > 0);
    CHECK(net::compare_core(ver("0.2.0-dev"), ver("0.2.0")) == 0);
    // min_online: x.y.z counts; a suffix does not; 0.0.0 allows everything.
    CHECK(net::meets_minimum(ver("0.2.0"), ver("0.2.0")) && net::meets_minimum(ver("0.3.0"), ver("0.2.0")));
    CHECK(!net::meets_minimum(ver("0.1.9"), ver("0.2.0")));
    CHECK(net::meets_minimum(ver("0.2.0-dev"), ver("0.2.0")) && !net::meets_minimum(ver("0.1.0-dev"), ver("0.2.0")));
    CHECK(net::meets_minimum(ver("0.0.1"), net::GameVersion{}) && net::meets_minimum(net::GameVersion{}, net::GameVersion{}));
    // The notice: a newer release; for a suffixed build, also the release of its own number.
    CHECK(net::update_available(ver("0.1.0"), ver("0.2.0")) && !net::update_available(ver("0.2.0"), ver("0.2.0")));
    CHECK(!net::update_available(ver("0.3.0"), ver("0.2.0")));
    CHECK(net::update_available(ver("0.2.0-dev"), ver("0.2.0")) && !net::update_available(ver("0.3.0-dev"), ver("0.2.0")));
    CHECK(!net::update_available(ver("0.1.0"), net::GameVersion{})); // no release announced
    // Wire: 6 bytes, the suffix stays home.
    net::Writer w;
    net::put_version(w, ver("1.2.3-dev"));
    CHECK(w.data.size() == 6);
    net::Reader r(w.data);
    net::GameVersion back = net::get_version(r);
    CHECK(back.number() == "1.2.3" && back.suffix.empty());
    // This build's version is valid and is what CMake says.
    CHECK(net::this_game_version().known());
}

TEST(net_relay_version_query) {
    net::RelayServer srv;
    use_mailboxes(srv);
    std::vector<std::string> logged;
    srv.set_log([&](const std::string& s) { logged.push_back(s); });
    double t = 1e6;
    SlotHost game(srv);
    auto ask = [&](const Bytes& q, Bytes* reply) {
        g_mail[game.addr.to_string()].clear();
        srv.receive(game.addr, q, t);
        return game.reply(net::Rv::VersionInfo, reply);
    };
    // Unconfigured: 0.0.0 / 0.0.0 / no page (every version may play). No cookie needed.
    Bytes q = version_query(ver("0.1.0"), net::RvPlatform::MacOS), a;
    CHECK(q.size() == net::kVersionQuerySize);
    CHECK(ask(q, &a) && a.size() <= q.size());
    {
        net::Rv type;
        net::Reader r(a);
        CHECK(net::rv_parse(a, type, r) && type == net::Rv::VersionInfo);
        CHECK(!net::get_version(r).known() && !net::get_version(r).known() && r.str().empty() && r.ok());
    }
    // Configured: the latest, min_online and the page; the reply is never larger than the query,
    // even with the longest URL a server accepts.
    srv.set_versions(versions("0.2.0", "0.2.0", kUrl));
    CHECK(ask(q, &a) && a.size() <= q.size());
    {
        net::Rv type;
        net::Reader r(a);
        net::rv_parse(a, type, r);
        net::GameVersion latest = net::get_version(r), min_online = net::get_version(r);
        CHECK(latest.number() == "0.2.0" && min_online.number() == "0.2.0" && r.str() == kUrl && r.ok());
    }
    net::RelayServer::Versions longest = versions("0.2.0", "0.1.0", "");
    longest.release_url = "https://" + std::string(net::kMaxReleaseUrl - 8, 'a');
    CHECK(net::check_relay_versions(longest));
    srv.set_versions(longest);
    CHECK(ask(q, &a) && a.size() <= q.size());
    std::printf("  query %zu bytes, longest reply %zu bytes\n", q.size(), a.size());
    longest.release_url += "a";
    CHECK(!net::check_relay_versions(longest));
    // A short (unpadded) query, or one as long as other requests, gets nothing: the reply could be larger.
    CHECK(!ask(version_query(ver("0.1.0"), net::RvPlatform::MacOS, net::kMinRequest), nullptr));
    Bytes cut = q;
    cut.resize(net::kVersionQuerySize - 1);
    CHECK(!ask(cut, nullptr));
    // Counted per platform; never logged; and nothing is created.
    srv.receive(game.addr, version_query(ver("0.1.0"), net::RvPlatform::Android), t);
    srv.receive(game.addr, version_query(ver("0.1.0"), net::RvPlatform(77)), t); // unknown: counted as such
    net::RelayServer::Stats st = srv.stats();
    CHECK(st.version_queries == 5 && st.queries_by_platform[int(net::RvPlatform::MacOS)] == 3 &&
          st.queries_by_platform[int(net::RvPlatform::Android)] == 1 && st.queries_by_platform[0] == 1);
    CHECK(logged.empty() && st.sessions == 0 && st.challenges == 0);
    // Rate limited like every control datagram (per source IP).
    net::RelayServer::Limits lim;
    lim.ip_msgs_per_s = 1, lim.ip_msgs_burst = 3;
    srv.set_limits(lim);
    int answered = 0;
    for (int i = 0; i < 10; ++i) answered += ask(q, nullptr);
    CHECK(answered == 3 && srv.stats().limited == 7);
}

TEST(net_relay_logs_ip_tags) {
    // The log names a source by a keyed hash of its IP, never the IP: one source keeps its tag
    // (players can still be counted), another gets another, and another relay (a restart or a
    // new UTC day) draws another key.
    std::vector<std::string> logged;
    auto run = [&](net::RelayServer& srv, SlotHost& a, SlotHost& b, SlotHost& c) {
        use_mailboxes(srv);
        srv.set_log([&](const std::string& s) { logged.push_back(s); });
        net::resolve("203.0.113.7:5000", 0, c.addr);
        double t = 1e6;
        CHECK(a.reg(1, t) && b.reg(2, t) && c.reg(3, t)); // a, b: 127.0.0.1 from two ports
        c.send(net::Rv::Lookup, t, [&](net::Writer& w) {
            w.str(a.code);
            w.u16_(net::kProtocolVersion);
            net::put_endpoints(w, {});
        });
    };
    auto tag = [&](const std::string& prefix) {
        for (const std::string& l : logged)
            if (l.rfind(prefix, 0) == 0) return l.substr(prefix.size());
        return std::string("?");
    };
    net::RelayServer srv;
    SlotHost a(srv), b(srv), c(srv);
    run(srv, a, b, c);
    CHECK(!logged.empty() && logged[0].rfind("ip tags: new key", 0) == 0);
    std::string ta = tag("session " + a.code + " registered by "), tc = tag("session " + c.code + " registered by ");
    CHECK(ta.size() == 8 && tc.size() == 8 && ta != tc);
    CHECK(tag("session " + b.code + " registered by ") == ta);
    CHECK(tag("join " + a.code + " from ") == tc);
    for (const std::string& l : logged) CHECK(l.find("127.0.0.1") == std::string::npos && l.find("203.0.113") == std::string::npos);
    logged.clear();
    net::RelayServer srv2;
    SlotHost a2(srv2), b2(srv2), c2(srv2);
    run(srv2, a2, b2, c2);
    CHECK(tag("session " + c2.code + " registered by ") != tc);
    // A server run with --log-ips logs the IPs instead (no port).
    logged.clear();
    net::RelayServer srv3;
    srv3.set_log_ips(true);
    SlotHost a3(srv3), b3(srv3), c3(srv3);
    run(srv3, a3, b3, c3);
    CHECK(tag("session " + a3.code + " registered by ") == "127.0.0.1");
    CHECK(tag("join " + a3.code + " from ") == "203.0.113.7");
    CHECK(tag("ip tags:") == "?");
    // With --geoip each player's country follows ("??" when the list has no range for it).
    logged.clear();
    net::RelayServer srv4;
    srv4.set_log_country([](const net::Address& a) { return a.to_string().rfind("203.", 0) == 0 ? "NZ" : ""; });
    SlotHost a4(srv4), b4(srv4), c4(srv4);
    run(srv4, a4, b4, c4);
    std::string ta4 = tag("session " + a4.code + " registered by ");
    CHECK(ta4.size() == 13 && ta4.substr(8) == " (\?\?)");
    CHECK(tag("join " + a4.code + " from ").substr(8) == " (NZ)");
}

TEST(net_geoip_ranges) {
    net::GeoIp geo;
    auto at = [&](const char* ip) {
        net::Address a;
        CHECK(net::resolve(ip, 1, a));
        return geo.country(a);
    };
    CHECK(!geo.parse("garbage\n1.2.3.4,BR\n"));
    CHECK(geo.ranges() == 0 && at("1.2.3.4").empty());
    CHECK(geo.parse("0.0.0.0,0.255.255.255,ZZ\r\n"
                    "\"1.0.0.0\",\"1.0.0.255\",\"AU\"\r\n"
                    "1.0.1.0,1.0.3.255,CN\n"
                    "1.0.4.0,1.0.7.255,CN\n" // joins the one before
                    "2.0.0.0,1.0.0.0,US\n"   // backwards: skipped
                    "5.0.0.0,5.0.0.255,us\n" // not a country code: skipped
                    "8.8.8.0,8.8.8.255,US,extra\n"
                    "2001:db8::,2001:db8:0:ffff:ffff:ffff:ffff:ffff,DE\n"
                    "2001:db8:1::,2001:db8:1:ffff:ffff:ffff:ffff:ffff,FR\n"
                    "::ffff:9.9.9.0,::ffff:9.9.9.255,CH\n"
                    "255.255.255.0,255.255.255.255,ZZ")); // no newline at the end
    CHECK(geo.ranges() == 8);
    CHECK(at("1.0.0.7") == "AU" && at("1.0.2.1") == "CN" && at("1.0.7.255") == "CN" && at("1.0.8.0").empty());
    CHECK(at("8.8.8.8") == "US" && at("9.9.9.9") == "CH" && at("0.1.2.3") == "ZZ" && at("255.255.255.255") == "ZZ");
    CHECK(at("5.0.0.1").empty() && at("1.5.0.0").empty());
    CHECK(at("[2001:db8::1]:5") == "DE" && at("[2001:db8:0:5::9]:5") == "DE" && at("[2001:db8:1:2::]:5") == "FR");
    CHECK(at("[2001:db8:2::1]:5").empty());
    // Ranges smaller than a /64 (the lookup's unit): a split /64 goes to one country, the same
    // whatever order the list (or the sort) has them in; an overlap keeps what lies past the
    // range before.
    for (const char* csv : {"2001:db8:5::100,2001:db8:5:0:ffff:ffff:ffff:ffff,CA\n"
                            "2001:db8:5::,2001:db8:5::ff,US\n"
                            "2001:db8:6::,2001:db8:6:0:ffff:ffff:ffff:ffff,US\n"
                            "10.0.0.0,10.0.0.255,AA\n10.0.0.128,10.0.1.255,BB\n",
                            "10.0.0.128,10.0.1.255,BB\n10.0.0.0,10.0.0.255,AA\n"
                            "2001:db8:6::,2001:db8:6:0:ffff:ffff:ffff:ffff,US\n"
                            "2001:db8:5::,2001:db8:5::ff,US\n"
                            "2001:db8:5::100,2001:db8:5:0:ffff:ffff:ffff:ffff,CA\n"}) {
        CHECK(geo.parse(csv) && geo.ranges() == 4);
        CHECK(at("[2001:db8:5::1]:5") == "CA" && at("[2001:db8:5::200]:5") == "CA" && at("[2001:db8:6::1]:5") == "US");
        CHECK(at("10.0.0.200") == "AA" && at("10.0.1.0") == "BB");
    }
    // A failed load keeps the list it had.
    CHECK(!geo.load("/nonexistent/gt3r-geoip.csv") && geo.ranges() == 4);
}

TEST(net_relay_min_online_enforced) {
    // Every request that creates state carries the game's version; below min_online it gets
    // Error(UpdateRequired) with the latest version and the page (after the cookie check).
    net::RelayServer srv;
    net::RelayServer::Limits lim;
    lim.ip_msgs_burst = 1000; // every test host is 127.0.0.1, at one instant
    srv.set_limits(lim);
    use_mailboxes(srv);
    srv.set_versions(versions("0.3.0", "0.2.0", kUrl));
    double t = 1e6;
    auto update_required = [&](SlotHost& h) {
        Bytes b;
        if (!h.reply(net::Rv::Error, &b)) return false;
        net::Rv type;
        net::Reader r(b);
        net::rv_parse(b, type, r);
        if (r.u8_() != u8(net::RvError::UpdateRequired)) return false;
        std::string text = r.str();
        net::GameVersion latest = net::get_version(r);
        std::string url = r.str();
        return r.ok() && text == "update required to play online" && latest.number() == "0.3.0" && url == kUrl;
    };
    u64 token = 100;
    for (const char* v : {"0.1.9", "0.2.0", "0.2.0-dev", "0.2.1", "1.0.0"}) {
        const bool below = !net::meets_minimum(ver(v), ver("0.2.0"));
        SlotHost h(srv), j(srv);
        h.version = j.version = ver(v);
        // Register.
        ++token;
        if (below) {
            h.send(net::Rv::Register, t, [&](net::Writer& w) { net::put_u64(w, token), net::put_endpoints(w, {}); });
            CHECK(update_required(h) && srv.stats().sessions == 0);
            // Without a valid cookie: only a Challenge (the version is checked after it).
            net::Writer w = net::rv_request(net::Rv::Register, 1, h.version);
            net::put_u64(w, token), net::put_endpoints(w, {});
            net::rv_pad(w);
            srv.receive(h.addr, w.data, t);
            CHECK(h.reply(net::Rv::Challenge) && srv.stats().sessions == 0);
            continue;
        }
        CHECK(h.reg(token, t));
        // Lookup and Bind from an outdated player into this session: refused.
        SlotHost old(srv);
        old.version = ver("0.1.0");
        old.send(net::Rv::Lookup, t, [&](net::Writer& w) {
            w.str(h.code), w.u16_(net::kProtocolVersion), net::put_endpoints(w, {});
        });
        CHECK(update_required(old));
        size_t channels = srv.stats().channels;
        old.send(net::Rv::Bind, t, [&](net::Writer& w) { fill_bind(w, net::pair_key(token, 1, 2), h.code, token, 1, 2); });
        CHECK(update_required(old) && srv.stats().channels == channels);
        // A current player: Lookup, Bind; the host Reserves.
        j.send(net::Rv::Lookup, t, [&](net::Writer& w) {
            w.str(h.code), w.u16_(net::kProtocolVersion), net::put_endpoints(w, {});
        });
        CHECK(j.reply(net::Rv::HostInfo));
        j.send(net::Rv::Bind, t, [&](net::Writer& w) { fill_bind(w, net::pair_key(token, 1, 2), h.code, token, 1, 2); });
        CHECK(j.reply(net::Rv::Bound));
        CHECK(h.reserve(1, t) && h.state == net::RvSlot::Granted);
        h.release(t);
        // An outdated host's Reserve on this session: refused (no slots).
        net::GameVersion keep = h.version;
        h.version = ver("0.1.0");
        h.send(net::Rv::Reserve, t, [&](net::Writer& w) { w.str(h.code), net::put_u64(w, h.token), w.u8_(1); });
        CHECK(update_required(h) && srv.stats().links_used == 0);
        // Its keepalive (no new state) still works, and so does refreshing a channel end it holds.
        h.keepalive(t);
        j.version = ver("0.1.0");
        j.send(net::Rv::Bind, t, [&](net::Writer& w) { fill_bind(w, net::pair_key(token, 1, 2), h.code, token, 1, 2); });
        CHECK(j.reply(net::Rv::Bound));
        h.version = keep;
    }
    CHECK(srv.stats().outdated == 1 /* register */ + 4 * 3 /* lookup, bind, reserve per session */);
}

TEST(net_relay_versions_reload) {
    // The versions file: parsing and checks.
    net::RelayServer::Versions v = versions("0.1.0", "0.0.0", "");
    CHECK(net::parse_relay_versions("# set-versions.sh\nlatest = 0.3.0\n  min_online=0.2.0  \r\nrelease_url = " +
                                        std::string(kUrl) + "\n\n",
                                    v));
    CHECK(v.latest.number() == "0.3.0" && v.min_online.number() == "0.2.0" && v.release_url == kUrl);
    net::RelayServer::Versions base = versions("0.4.0", "0.4.0", kUrl), w = base;
    CHECK(net::parse_relay_versions("latest = 0.5.0\n", w)); // a key left out keeps its value (the flags)
    CHECK(w.latest.number() == "0.5.0" && w.min_online.number() == "0.4.0" && w.release_url == kUrl);
    std::string err;
    for (const char* bad : {"latest = 0.3\n", "lates = 0.3.0\n", "latest 0.3.0\n", "latest = 0.2.0\nmin_online = 0.3.0\n",
                            "release_url = ftp://x\n", "release_url = https://a b\n", "latest = 0.3.0-dev\n"}) {
        w = base;
        CHECK(!net::parse_relay_versions(bad, w, &err) && !err.empty());
        CHECK(w.latest.number() == "0.4.0"); // unchanged
    }
    CHECK(net::parse_relay_versions("", w)); // empty: the flags (or defaults) stand

    // A reload (set_versions; gt3r-relay does it on SIGHUP) keeps sessions and channels; only new
    // state is gated by the new min_online.
    net::RelayServer srv;
    use_mailboxes(srv);
    double t = 1e6;
    SlotHost h(srv), j(srv);
    h.version = j.version = ver("0.1.0");
    CHECK(h.reg(7, t));
    j.send(net::Rv::Bind, t, [&](net::Writer& w2) { fill_bind(w2, net::pair_key(7, 1, 2), h.code, 7, 1, 2); });
    CHECK(j.reply(net::Rv::Bound));
    srv.set_versions(versions("0.2.0", "0.2.0", kUrl));
    CHECK(srv.stats().sessions == 1 && srv.stats().channels == 1);
    h.keepalive(t + 30000);
    srv.tick(t + 59000);
    CHECK(srv.stats().sessions == 1); // kept alive
    SlotHost late(srv);
    late.version = ver("0.1.0");
    CHECK(!late.reg(8, t) && srv.stats().sessions == 1);
    late.version = ver("0.2.0");
    CHECK(late.reg(8, t) && srv.stats().sessions == 2);
    // Back to unconfigured: everything is allowed again.
    srv.set_versions({});
    late.version = ver("0.0.1");
    CHECK(late.reg(9, t) && srv.stats().sessions == 3);
}

TEST(net_relay_unconfigured_allows_all) {
    net::RelayServer srv;
    use_mailboxes(srv);
    double t = 1e6;
    u64 token = 500;
    for (const char* v : {"0.0.0", "0.0.1", "0.1.0", "9.9.9"}) {
        SlotHost h(srv), j(srv);
        h.version = j.version = net::parse_game_version(v).value_or(net::GameVersion{});
        CHECK(h.reg(++token, t));
        j.send(net::Rv::Lookup, t, [&](net::Writer& w) {
            w.str(h.code), w.u16_(net::kProtocolVersion), net::put_endpoints(w, {});
        });
        CHECK(j.reply(net::Rv::HostInfo));
        CHECK(h.reserve(1, t) && h.state == net::RvSlot::Granted);
        h.release(t);
    }
    CHECK(srv.stats().outdated == 0);
}

TEST(net_relay_update_required_client) {
    // The game below the relay's min_online: hosting and joining end at once with the reason,
    // and the session remembers the latest version and the page for the lobby.
    FakeClock clock;
    net::RelayServer srv;
    if (!srv.open(0)) SKIP("no UDP sockets here");
    srv.set_versions(versions("0.3.0", "0.3.0", kUrl));
    std::string server = "127.0.0.1:" + std::to_string(srv.port());
    for (bool hosting : {true, false}) {
        net::Session s;
        s.set_game_version(ver("0.2.0"));
        CHECK(hosting ? s.host_via(server, "H", "a", 0) : s.join_code(server, "ABCD2345", "J", "b", 0));
        std::vector<net::Event> ev;
        CHECK(pump_rv(srv, {&s}, [&] {
            for (auto& e : drain(s)) ev.push_back(e);
            return has(ev, net::Event::Kind::Disconnected) || has(ev, net::Event::Kind::Rejected);
        }));
        CHECK(!s.active() && s.update_required());
        if (s.update_required()) CHECK(s.update_required()->latest.number() == "0.3.0" && s.update_required()->url == kUrl);
        for (const net::Event& e : ev)
            if (e.kind == net::Event::Kind::Disconnected || e.kind == net::Event::Kind::Rejected)
                CHECK(e.text == "update required to play online: version 0.3.0");
    }
    // Up to date: hosting works (and a new start forgets the old refusal).
    net::Session h;
    h.set_game_version(ver("0.3.0-dev"));
    CHECK(h.host_via(server, "H", "a", 0));
    CHECK(pump_rv(srv, {&h}, [&] { return !h.code().empty(); }) && !h.update_required());
    // An outdated player cannot join it.
    net::Session j;
    j.set_game_version(ver("0.2.9"));
    CHECK(j.join_code(server, h.code(), "J", "b", 0));
    std::vector<net::Event> ev;
    CHECK(pump_rv(srv, {&h, &j}, [&] {
        for (auto& e : drain(j)) ev.push_back(e);
        return has(ev, net::Event::Kind::Disconnected);
    }));
    CHECK(j.update_required() && h.players().size() == 1);
}

TEST(net_relay_update_required_at_race_start) {
    // A lobby made before the relay raised min_online: it keeps running, but its race cannot
    // reserve link slots: the host hears why (a Queue event) and stays in the lobby.
    RvParty p;
    if (!p.start(true)) SKIP("no UDP sockets here"); // c1 through the relay: the race needs slots
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.host.relay_links_needed() > 0; }));
    net::GameVersion next = net::this_game_version();
    next.suffix.clear();
    ++next.minor;
    net::RelayServer::Versions v;
    v.latest = v.min_online = next;
    v.release_url = kUrl;
    p.srv.set_versions(v); // as a SIGHUP reload would
    p.c1.set_ready(true), p.c2.set_ready(true);
    CHECK(pump_rv(p.srv, p.all(), [&] { return p.host.all_ready(); }));
    drain(p.host);
    p.host.load();
    std::vector<net::Event> eh;
    CHECK(pump_rv(p.srv, p.all(), [&] {
        for (auto& e : drain(p.host)) eh.push_back(e);
        return has(eh, net::Event::Kind::Queue);
    }));
    CHECK(!has(eh, net::Event::Kind::Load) && p.host.active() && p.host.in_lobby() && !p.host.relay_wait().waiting);
    CHECK(p.host.update_required() && p.host.update_required()->latest.number() == next.number());
    CHECK(p.srv.stats().links_used == 0 && p.srv.stats().sessions == 1);
}

TEST(net_version_check_roundtrip) {
    // The game's background check against a relay on loopback (real time: the check never waits
    // on the test clock).
    net::RelayServer srv;
    if (!srv.open(0)) SKIP("no UDP sockets here");
    srv.set_versions(versions("0.3.0", "0.2.0", kUrl));
    net::VersionCheck check;
    CHECK(!check.running() && !check.take());
    check.start("127.0.0.1:" + std::to_string(srv.port()), ver("0.2.0"), net::RvPlatform::Linux);
    std::optional<net::VersionCheck::Result> r;
    CHECK(settle([&] {
        srv.step(1);
        r = check.take();
        return r.has_value();
    }));
    CHECK(r && r->answered && r->latest.number() == "0.3.0" && r->min_online.number() == "0.2.0" && r->url == kUrl);
    CHECK(!check.take()); // once
    CHECK(srv.stats().version_queries >= 1 && srv.stats().queries_by_platform[int(net::RvPlatform::Linux)] >= 1);
    // Nobody answers: it gives up after its time-out with a reason.
    net::UdpSocket silent;
    CHECK(silent.open(0));
    check.start("127.0.0.1:" + std::to_string(silent.local_port()), ver("0.2.0"), net::RvPlatform::Linux, 300);
    CHECK(settle([&] { return (r = check.take()).has_value(); }, 5000));
    CHECK(r && !r->answered && r->error.find("no answer") != std::string::npos);
    u8 buf[512];
    net::Address from;
    size_t n = 0, got = 0;
    while ((n = silent.recv_from(from, buf, sizeof buf))) { // padded queries
        ++got;
        CHECK(n == net::kVersionQuerySize);
    }
    CHECK(got >= 1 && got <= 3);
}

TEST(net_direct_ip_needs_same_version) {
    // Host / Join by address (no relay to check versions): the same release only, with a message
    // that says who must update. A suffix does not matter.
    FakeClock clock;
    struct Case {
        const char *host, *joiner;
        bool joins;
        const char* says;
    };
    for (const Case& c : {Case{"0.2.0", "0.1.0", false, "update required: the host has version 0.2.0 (you have 0.1.0)"},
                          Case{"0.2.0", "0.3.0", false, "the host has an older version (0.2.0, you have 0.3.0): the host needs to update"},
                          Case{"0.2.0", "0.2.0-dev", true, ""}}) {
        net::Session h, j;
        h.set_game_version(ver(c.host));
        j.set_game_version(ver(c.joiner));
        if (!h.host(0, "H", "a", 0)) SKIP("no UDP sockets here");
        CHECK(j.join("127.0.0.1:" + std::to_string(h.port()), "J", "b", 0));
        std::vector<net::Event> ev;
        CHECK(pump({&h, &j}, [&] {
            for (auto& e : drain(j)) ev.push_back(e);
            return has(ev, net::Event::Kind::Joined) || has(ev, net::Event::Kind::Rejected);
        }));
        CHECK(has(ev, net::Event::Kind::Joined) == c.joins);
        for (const net::Event& e : ev)
            if (e.kind == net::Event::Kind::Rejected) CHECK(e.text == c.says);
    }
    // The joiner checks too: a host that does not send its version (older games) is refused.
    CHECK(net::version_mismatch(ver("0.2.0"), net::GameVersion{}).find("host needs to update") != std::string::npos);
}

TEST(net_update_prompt_rules) {
    using P = net::UpdatePrompt;
    const net::GameVersion none{};
    // Which dialog: Required below min_online, else Available when a newer release exists.
    CHECK(net::update_prompt(ver("0.1.0"), ver("0.2.0"), ver("0.2.0")) == P::Required);
    CHECK(net::update_prompt(ver("0.1.0"), ver("0.2.0"), none) == P::Available);
    CHECK(net::update_prompt(ver("0.2.0"), ver("0.2.0"), ver("0.2.0")) == P::None);
    CHECK(net::update_prompt(ver("0.2.0-dev"), ver("0.2.0"), ver("0.2.0")) == P::Available); // plays, but older
    CHECK(net::update_prompt(ver("0.1.0"), none, none) == P::None);                         // nothing known
    CHECK(net::prompt_version(ver("0.3.0"), ver("0.2.0")).number() == "0.3.0");
    CHECK(net::prompt_version(none, ver("0.2.0")).number() == "0.2.0");
    // At startup: once per release. Later on 0.2.0 hides 0.2.0, not 0.2.1; nothing to show, nothing shown.
    CHECK(net::show_startup_prompt(ver("0.1.0"), ver("0.2.0"), none, none));
    CHECK(!net::show_startup_prompt(ver("0.1.0"), ver("0.2.0"), none, ver("0.2.0")));
    CHECK(net::show_startup_prompt(ver("0.1.0"), ver("0.2.1"), none, ver("0.2.0")));
    CHECK(!net::show_startup_prompt(ver("0.1.0"), ver("0.2.0"), ver("0.2.0"), ver("0.3.0"))); // dismissed a newer one
    CHECK(net::show_startup_prompt(ver("0.1.0"), ver("0.2.0"), ver("0.2.0"), ver("0.1.5")));   // required, not dismissed
    CHECK(!net::show_startup_prompt(ver("0.2.0"), ver("0.2.0"), none, none));                  // up to date
    CHECK(!net::show_startup_prompt(ver("0.1.0"), none, none, none));
}

TEST(net_release_asset_per_platform) {
    using A = net::CpuArch;
    using P = net::RvPlatform;
    CHECK(net::release_asset(P::Windows, A::X64) == "gt3r-windows-x64.zip");
    CHECK(net::release_asset(P::Windows, A::Arm64).empty()); // no Windows arm64 package
    CHECK(net::release_asset(P::Windows, A::Other).empty());
    CHECK(net::release_asset(P::MacOS, A::Arm64) == "gt3r-macos-arm64.zip");
    CHECK(net::release_asset(P::MacOS, A::X64).empty()); // no Intel package
    CHECK(net::release_asset(P::MacOS, A::Other).empty());
    CHECK(net::release_asset(P::Linux, A::X64) == "gt3r-linux-x86_64.tar.gz");
    CHECK(net::release_asset(P::Linux, A::Arm64).empty());
    CHECK(net::release_asset(P::Linux, A::Other).empty());
    CHECK(net::release_asset(P::Android, A::Arm64) == "gt3r-android.apk");
    CHECK(net::release_asset(P::Android, A::X64) == "gt3r-android.apk"); // one APK for both ABIs
    CHECK(net::release_asset(P::Android, A::Other).empty());
    CHECK(net::release_asset(P::Android, A::Arm64, true) == "gt3r-quest.apk"); // the VR build
    CHECK(net::release_asset(P::Android, A::X64, true).empty());
    CHECK(net::release_asset(P::Other, A::Arm64).empty()); // iOS and anything else
    CHECK(net::release_asset(P::Unknown, A::X64).empty());
#if defined(__APPLE__) && defined(__aarch64__)
    CHECK(net::this_release_asset() == "gt3r-macos-arm64.zip");
#elif defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    CHECK(net::this_release_asset() == "gt3r-windows-x64.zip");
#elif defined(__linux__) && !defined(__ANDROID__) && defined(__x86_64__)
    CHECK(net::this_release_asset() == "gt3r-linux-x86_64.tar.gz");
#endif
}

TEST(net_update_links) {
    const std::string gh = "https://github.com/o/r/releases/latest/download/{asset}";
    net::UpdateLinks l = net::update_links(gh, "gt3r-windows-x64.zip");
    CHECK(l.download == "https://github.com/o/r/releases/latest/download/gt3r-windows-x64.zip");
    CHECK(l.notes == "https://github.com/o/r/releases/latest");
    l = net::update_links(gh, ""); // no package for this platform: the release page
    CHECK(l.download == "https://github.com/o/r/releases/latest" && l.notes == l.download);
    l = net::update_links("https://example.org/gt3r/releases", "gt3r-android.apk"); // no placeholder: the page
    CHECK(l.download == "https://example.org/gt3r/releases" && l.notes == l.download);
    l = net::update_links("https://example.org/files/{asset}", "gt3r-android.apk"); // not GitHub's layout
    CHECK(l.download == "https://example.org/files/gt3r-android.apk" && l.notes == "https://example.org/files");
    l = net::update_links("", "gt3r-android.apk");
    CHECK(l.download.empty() && l.notes.empty());
    // A typical GitHub release URL (a long owner and repository name) fits the server's limit.
    net::RelayServer::Versions v;
    v.release_url = "https://github.com/an-owner-name-of-length/GT3-revisited/releases/latest/download/{asset}";
    CHECK(net::check_relay_versions(v));
}
