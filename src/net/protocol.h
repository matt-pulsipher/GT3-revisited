#pragma once

// Online play wire format (docs/dev/ONLINE.md). Little endian.
//
// Datagram: "GT3N" u16 protocol version, u16 ack (the next reliable sequence number the
// sender expects from the receiver: every lower one arrived), u8 reliable count, then
// {u16 seq, u16 length, message} each, u8 unreliable count, then {u16 length, message}.
// A message is u8 type + body. Reliable messages are delivered once, in order.

#include "base/bytes.h"
#include "sim/sim_types.h"

#include <array>
#include <string>
#include <vector>

namespace gt3r::net {

constexpr u16 kProtocolVersion = 4; // 4: races on the exact core (exact car states, pad frames)
constexpr int kMaxPlayers = 6; // as the original's i.LINK battle
constexpr size_t kMaxDatagram = 1200;

enum class Msg : u8 {
    Hello = 1,    // C->H  u16 version, str name, str car, u8 paint, str build
    Welcome = 2,  // H->C  u8 your id
    Reject = 3,   // H->C  str reason
    Roster = 4,   // H->*  u8 n, {u8 id, str name, str car, u8 paint, u8 flags (1 ready, 2 loaded), u16 ping ms}
    Settings = 5, // H->*  str course, u8 laps, u8 mods, u16 ghost ms, u8 stock only, u16 max PS,
                  //       u8 class, u8 category, u8 drive, u8 aspiration (net::Settings)
    Car = 6,      // C->H  str car, u8 paint, u8 ready
    Load = 7,     // H->*  str course, u8 laps, u8 n, {u8 id} grid order (pole first)
    Loaded = 8,   // C->H  (the race scene is ready)
    Go = 9,       // H->*  u32 delay ms (start the countdown that long after the host sent it)
    State = 10,   // *     CarSnapshot (unreliable; the host relays clients' states)
    Finish = 11,  // *     u8 id, f64 race time, f64 best lap (reliable; relayed)
    Ping = 12,    // *     f64 sender time (unreliable)
    Pong = 13,    // *     f64 echoed time (unreliable)
    Bye = 14,     // *     (unreliable, sent a few times)
    Lobby = 15,   // H->*  back to the lobby after a race
    Retire = 16,  // *     u8 id (left the race; reliable; relayed)
    CarInfo = 17, // C->H  bytes (game::OnlineCar); H->* u8 id, bytes
    PkgRequest = 18, // *  str sha256: send me that mod car package (docs/dev/ONLINE.md "Mod cars")
    PkgChunk = 19,   // *  str sha256, u32 total, u32 offset, bytes
    PkgError = 20,   // *  str sha256, str reason
    Contact = 21,    // *  u8 target, u8 from, u32 tick, f32 point x, z, f32 impulse x, z (reliable; relayed)
    Flag = 22,       // C->H u8 suspect, u8 FlagKind, f32 value (reliable); H->C u8 suspect, u8 kind, f32 value, u8 reporter
    Kick = 23,       // H->C str reason (the host removed us)
    Voice = 24,      // *  u8 player, u8 seq, u8 nbytes, bytes μ-law (unreliable; host relays like State)
};

class Writer {
public:
    Bytes data;
    void u8_(u8 v) { data.push_back(v); }
    void u16_(u16 v) { u8_(u8(v)), u8_(u8(v >> 8)); }
    void u32_(u32 v) { u16_(u16(v)), u16_(u16(v >> 16)); }
    void f32_(float v);
    void f64_(double v);
    void str(const std::string& s); // u8 length (truncated to 255) + bytes
    void bytes(ByteSpan b) { data.insert(data.end(), b.begin(), b.end()); }
};

class Reader {
public:
    explicit Reader(ByteSpan d) : d_(d) {}
    bool ok() const { return ok_; }
    bool at_end() const { return pos_ >= d_.size(); }
    size_t remaining() const { return ok_ ? d_.size() - pos_ : 0; }
    u8 u8_();
    u16 u16_();
    u32 u32_();
    float f32_();
    double f64_();
    std::string str();
    ByteSpan bytes(size_t n);

private:
    bool need(size_t n);
    ByteSpan d_;
    size_t pos_ = 0;
    bool ok_ = true;
};

// A driver's frame for one step as the exact core reads it (sim::exact::PadFrame: the pad read's output), bit for bit.
struct PadInput {
    u8 flags = 0;      // analog axes: bit 0 steer, 1 throttle, 2 brake, 3 back, 4 third (bits 0-4 only)
    u8 kind = 0;       // controller kind (0..15)
    s8 shift = 0;      // -1, 0, +1
    s16 steer = 0;     // +-4096 full lock, left positive
    u16 throttle = 0, brake = 0, back = 0, third = 0;
    bool operator==(const PadInput&) const = default;
};
constexpr int kInputHistory = 3; // frames of the last steps in every state (a lost datagram costs none)

// What a car's record holds of its motion, as its owner's machine sends it after each step (docs/dev/ONLINE.md "State"):
// the receiver poses the car in its own race and syncs its prediction from it (race::sync_motion). The physics frame of
// sim::exact (Z up); `pos` .. `axle_speed` are the record's single floats bit for bit, the rest in fixed units.
struct CarMotion {
    std::array<u32, 3> pos{}, vel{};      // F +0x00 CoG, F +0x44C velocity
    u32 heading = 0, pitch = 0, roll = 0; // F +0x470 / +0x468 / +0x46C
    u32 yaw_rate = 0;                     // F +0x47C
    u32 omega = 0;                        // F +0x448 engine speed (rad/s)
    u32 steer_cmd = 0;                    // F +0x41C the steering command (rad)
    std::array<u32, 2> axle_speed{};      // F +0x458 (rad/s)
    s16 pitch_rate = 0, roll_rate = 0;    // F +0x480 / +0x484, 1/1024 rad/s
    std::array<s16, 4> rim_speed{};       // wheel +0x20, 1/128 m/s
    std::array<s16, 4> susp{};            // wheel +0x18 the suspension coordinate, 1/65536 m
    u8 gear = 0, clutch = 0;              // F +0x436 / +0x437
    bool operator==(const CarMotion&) const = default;
};

// The owner's race entry as far as its car's physics read it (the race entry's + 0x20E4 .. + 0x20F4; the pedal style can
// change during the race).
struct CarEntry {
    u8 pedal_style = 0; // 0..7: 0 the automatic gearbox, 1 the manual one (a pad), ...
    u8 wheel_type = 0;  // 0..7: the steering-wheel type
    bool asm_on = true, tcs_on = true;
    u8 control = 0;     // 0..3: the car's control source (F +0x3E9: 0 its pad, 1 the AI with the pad decoded, 2 the AI)
    u8 driver_kind = 0; // 0..15: F +0x534 (1 the AI drives it on, after its finish)
    bool operator==(const CarEntry&) const = default;
};

// One car's state at the end of a step, as its owner ran it on the exact core: 90 to 125 bytes on the wire. What the
// receiver needs besides (the course-frame position and velocity the checks read) it derives (race::complete_snapshot).
struct CarSnapshot {
    u8 player = 0;
    u32 tick = 0;          // the owner's race step (sequence; newer replaces older)
    s32 clock = 0;         // the owner's race time after the step in 1/3000 s (the race clock; negative in the countdown)
    CarMotion motion;
    CarEntry entry;
    u8 laps = 0;           // laps completed
    bool rev_limiter = false;
    bool respawned = false; // the owner put the car back on the track lately (a jump is no teleport)
    // inputs[0] = this step's frame, [1] the step before, ... (input_count valid).
    PadInput inputs[kInputHistory];
    u8 input_count = 0;
    // Derived by the receiver (race::complete_snapshot), not sent.
    float race_time = 0;     // clock / 3000 (s)
    sim::VehicleState state; // the CoG and the velocity in the course frame, the heading's rotation, the speed
    float body_x = 0, body_z = 0, heading = 0, yaw_rate = 0; // the collision body (course frame)
    float brake = 0;         // the brake axis, 0..1 (lamps)
};

// Plausibility checks a player's machine reports about another player's car (the host sees them).
enum class FlagKind : u8 {
    Speed = 1,      // faster than the car's geared top speed allows (value: km/h)
    Teleport = 2,   // moved further than its speed allows, without a respawn (value: metres)
    Accel = 3,      // sustained acceleration beyond what a car can do (value: g)
    FinishTime = 4, // claimed a finish time others did not see (value: seconds off)
    Motion = 5,     // moves unlike its own car under its own inputs (value: metres)
};
const char* flag_text(FlagKind k);

void write_snapshot(Writer& w, const CarSnapshot& s);
bool read_snapshot(Reader& r, CarSnapshot& s);

} // namespace gt3r::net
