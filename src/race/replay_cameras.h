#pragma once

// GT3's replay camera script ("GTRC": course file slot 34, also
// cameras/start.cam and cameras/attach.cam). Layout and behaviour as found by
// the race workstream's research (summary in docs/dev/RACE.md):
//
//   header (offsets from "GTRC"): +0x14 u32 course id, +0x18 f32 lap length L,
//     +0x1C f32 script end T, +0x24 u32 record count, +0x28 rel-ptr records
//     (0x80 bytes each, sorted by start), +0x2C/+0x30 a second list (course stills: the
//     program shows them only in a slideshow nothing reachable starts).
//   record: +0x00 u32 kind, +0x04 f32 start D, +0x08 f32 end D, +0x0C f32 duration,
//     +0x18 u32 on-board mount, +0x1C f32 the heat haze (> 0: on; presentation §L6),
//     +0x28/+0x2C f32 zoom min/max, rel-ptr curves:
//     +0x34 zoom, +0x40/+0x44/+0x48 camera x/y/z, +0x58/+0x5C/+0x60 target x/y/z,
//     +0x64 f32 the cut's dissolve (seconds, 0 none; presentation §G16).
//   curve: u32 n, n x {f32 t, f32 value}; piecewise linear in t (0..1), linear
//     extrapolation, n = 1 is constant.
//   kind: &3 mode (0 trackside world coordinates, 3 on-board, 1 car-local),
//     &0xC != 0 look at the car (+0.4 m), else at the target curves;
//     0x10 zoom = |camera - target| * zoom(0), else zoom(t); 0x400 a shot a change of the
//     followed car always ends.
//
// Records are laid out by track distance D = k * L + s, where s is the car's
// distance along the lap and k a "lap variant" (1..nvar; nvar = max(1,
// int(T / L) - 1 - int(first.start / L))) so consecutive laps use different
// cuts; before its first crossing of the start line a car has k = 0 (D = s: the
// script's first records, from first.start, cover the start). Within a record
// t = clamp((D - start) / (end - start), 0, 1).

#include "base/bytes.h"
#include "base/game_random.h"
#include "render/math3d.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace gt3r::race {

class MusicMarkers;

struct GtrcCurve {
    std::vector<std::pair<float, float>> keys; // (t, value)
    float eval(float t) const;
    bool empty() const { return keys.empty(); }
};

struct GtrcRecord {
    u32 kind = 0;
    float start = 0, end = 0, duration = 0;
    u32 mount = 0;
    float zoom_min = 1, zoom_max = 100;
    float dissolve = 0; // +0x64: seconds of dissolve at the cut to it (modes 0, 2, 3)
    // +0x1C: the shot's camera flag 0x2000 (the designer camera, > 0.0): the heat haze
    // lenses on courses with Env0 flag 0x20 (presentation §L6). 0.0 or 1.0 on the disc.
    float haze = 0;
    GtrcCurve zoom, pos[3], target[3];

    bool hazy() const { return haze > 0; }
    int mode() const { return int(kind & 3); }
    bool looks_at_car() const { return (kind & 0xC) != 0; }
    bool zoom_by_distance() const { return (kind & 0x10) != 0; }
    render::Vec3 position(float t) const { return {pos[0].eval(t), pos[1].eval(t), pos[2].eval(t)}; }
};

struct GtrcScript {
    u32 course_id = 0;
    float lap_length = 0, script_end = 0;
    std::vector<GtrcRecord> records;
    std::vector<GtrcRecord> stills; // second list

    static bool is_gtrc(ByteSpan d) { return has_magic(d, "GTRC") && d.size() >= 0x40; }
    static GtrcScript parse(ByteSpan d); // throws FormatError
    int variants() const;
    float period() const { return float(variants()) * lap_length; }
    // The lap variant k of a car on lap `lap` (its lap counter, race::lap_counter: 0 before
    // the start line): 0 before the start line, then ((lap - 1) mod nvar) + 1.
    int lap_variant(int lap) const { return lap > 0 ? (lap - 1) % variants() + 1 : 0; }
    // The records the game takes as containing D (presentation §H6): the last one starting at
    // or before D when D is before its end; past the end of the list D - period(), scanned
    // from the first record while they start at or before it. At most 32, in that order.
    std::vector<int> records_at(float D) const;
    // The first of records_at(D) with u = (D - start) / (end - start) in [0, u_max]; -1 none.
    int record_at(float D, float u_max) const;
    // Another lap variant's record for a car at s on variant k: variants k + 1, k + 2, ...
    // (past nvar: 1), nvar tries, the first record at D = v L + s with u in [0, 0.6].
    // {record, variant}.
    std::optional<std::pair<int, int>> other_variant(int k, float s) const;
};


// The replay camera manager's cut requests and shot length (presentation §G14), kept every
// 60 Hz frame of a replay whatever the view mode (mode 1 cuts on them; mode 2's shot picker
// and TOP / AUTO switch open their gates on them, ReplayDirector::set_cut_gate, presentation §H8).
//
// Shot length x (before the view's handler): mode 1 the interval to the next marker of
// track 4 (music-timed cuts on) or 3 (off); other modes track 3's, shortened to track 2's
// (phrases) and 1's (beats) once those have been used; over 80 s (no marked song) the
// countdown / 60. Request (after the handler, for the next frame's handler): 0, then the
// countdown when no relevant marker track is active (tracks 4, or 2 and 3, under 80 s):
// -1 a frame, below 0 request 1 and 120 frames again; held at 120 during the standing-start
// hold; then in mode 1 a track-4 event gives request 4 (music-timed cuts on), a track-3 event
// request 3 (off); in the other modes track 3 gives 3, track 2 gives 2 (when phrases are in
// use). Tracks 3 and 4 hold the same markers on the disc: the switch changes the dissolve's
// odds, not the timing. Tracks 5..9 (sections, beat use) are empty on the disc.
class CutClock {
public:
    // A replay's set-up: request 1 (the first frame cuts, on the car alone), x 4 s until the
    // first frame, the music's section and phrase state cleared; then reset().
    void setup(float level, float music_cuts);
    // reset() (also every REPLAY OPTIONS apply): the countdown 60 frames, the Up / Down level
    // and the Left / Right music-timed cuts from the settings (clamped 0..1).
    void reset(float level, float music_cuts);
    // One frame, before the view's handler: the shot length (s).
    float update_shot_length(const MusicMarkers* m, int mode);
    // One frame, after the handler: the request for the next frame. The director's generator
    // steps once (and once more at the music's tracks 6 / 7, never on the disc).
    void update_request(const MusicMarkers* m, int mode, bool hold, GameRandom& rng);
    int request() const { return request_; }
    float shot_length() const { return x_; }
    int countdown() const { return countdown_; }

    // Mode 1's pad (presentation §G3): Up / Down (+1 / -1) +-0.1 on the level, Right / Left
    // (+1 / -1) +-1.1 on the music-timed cuts, both clamped 0..1. True when the cuts' switch
    // (>= 0.5) changed.
    void step_level(int dir);
    bool step_music_cuts(int dir);
    float level() const { return level_; }
    float music_cuts() const { return music_cuts_; }
    bool music_cuts_on(int mode) const { return mode == 1 && music_cuts_ >= 0.5f; }
    // The race-sound level in force: the Up / Down level inside a section the music marks
    // (its tracks 9 / 8), else 1; the disc's songs mark none.
    float sound_level() const { return outside_ ? 1.0f : level_; }

private:
    bool event(const MusicMarkers* m, int track) const;
    float interval(const MusicMarkers* m, int track) const;

    int request_ = 0;
    float x_ = 4.0f;
    int countdown_ = 60;
    float level_ = 0.1f, music_cuts_ = 0;
    bool beats_ = false, phrases_ = true;            // beat / phrase intervals in use (modes 0, 2, 3)
    bool beat_cuts_ = false, phrase_skip_ = false;   // tracks 6 / 7, 5 (never on the disc)
    bool outside_ = true;                            // outside a marked section
};

// A shot's grade and effects (presentation §G6, §G16), rolled at every mode-1 cut and drawn by
// the post-process pass: levels (black / white stretched to 0 / 1), the colour filter (a
// curve per channel and a saturation; kept across cuts while on) and the motion trail.
struct ShotLook {
    bool levels = false;
    float black = 0, white = 1;
    bool filter = false;
    float curve[3] = {0, 0, 0};
    float saturation = 1;
    bool trail = false;
    float trail_timer = 0; // 0.5 .. 2.0 at the cut
    bool trail_fade = false; // -0.02 a frame until below 0
    // The previous picture's weight: min(clamp(timer, 0, 1), 0.8).
    float trail_weight() const;
    // One mode-1 frame: the fading trail's timer.
    void step();
};

// The dissolve (presentation §G16): the last picture shown frozen at a request and faded over
// the new one in `seconds` (0.5 at a mode-1 cut and at a replay's start, 1 at a manual car
// change in view modes 0 / 3, a course record's own time): x starts at 1 - 1 / (60 d) and
// falls by 1 / (60 d) a frame; the frozen picture's weight is trunc(128 x^3) / 128.
struct DissolveClock {
    float x = 0, d = 0.5f;
    bool snapshot = false; // the next draw freezes the last picture
    void start(float seconds);
    void step();           // one 60 Hz frame (not the snapshot's own)
    float weight() const { return std::floor(128.0f * x * x * x) / 128.0f; }
};

// Mode 1's procedural camera for the current cut (presentation §G15): its kind 0..4
// (Roadside, Hover, Matrix, Telephoto, a camera mount on the car), the car mount (kind 4),
// the shot length it was planned for and its own generator's seed (a clock value). Modes 0, 2
// and 3 use kind 4 for their on-board shots (an on-board record's mount, the in-car mount 0).
struct ProceduralShot {
    int kind = -1;
    int mount = 0;
    float x = 2.0f;
    u32 seed = 0;
};

// Picks and evaluates the current replay camera by view mode (presentation §G2, §H8).
//
// Modes 0, 2 and 3 share the course camera's shot picker (§H8), run every frame on the
// followed car's D = k L + s (k = GtrcScript::lap_variant of its lap counter: 0 before the
// start line):
//   - setup() (every replay and demonstration) leaves the camera on the script's last record
//     and sets the view's reset, so the first frame finds the shot: the camera re-arms its
//     record and, as D is outside it, takes record_at(D, 1) (any progress), or with none turns
//     on board (the followed car's in-car view, its camera mount 0).
//   - The view's reset (reset(): REPLAY OPTIONS, the driver's view off) and a new followed car
//     (car_changed) re-arm the shot the same way; so does a new target (target_set: a new car
//     or pair) when the shot is on board, has kind 0x400, or D is outside its range.
//   - Otherwise a shot runs until it runs out (a timed record's time, else its progress u past
//     0..1) and the gate opens: modes 0 / 3 at once, mode 2 (and 0x400 records in their last
//     tenth) once the shot length x is 1.3 s or more or a cut request is pending
//     (set_cut_gate). Then the first of record_at(D, 0.6) (1 for a while after a manual car
//     change) unless it is the current one, else other_variant() (the shot then holds at least
//     3 s), else on board; each with the outgoing shot's dissolve (its record's own, 1 s on
//     board; none for a second on-board turn).
// Mode 2 also draws at every new shot: with p 0.07, while the followed car has a battle
// partner (set_pair), the camera turns on board the partner's car until the shot ends.
// Mode 1 (avant-garde) has no such picker: avant_garde_cut() makes each cut (on the music's
// requests, the countdown, the view's reset or the pad) and the shot holds until the next one.
//
// With a pair (the followed car and its battle partner, modes 1 and 2) the camera aims at
// the point between them (`car` is that point) and widens to keep both in view (presentation §G3).
//
// A record that aims at its target curves and has a duration of 1 s or more advances by
// time (t = seconds since the cut / duration); every other record by the car's track
// distance.
class ReplayDirector {
public:
    explicit ReplayDirector(const GtrcScript* s = nullptr) : s_(s) { setup(); }
    // A replay's (or demonstration's) set-up: no look, mode 1's state cleared, the course
    // camera on the script's last record, and the view's reset.
    void setup();
    // The view's reset: modes 0, 2, 3 re-arm the shot at the next update; mode 1 cuts at its
    // next frame.
    void reset();
    // A new followed car (modes 0, 2, 3): the view's reset; `manual_seconds` > 0 (a manual
    // change: the aim's move) lets the search take a record at any progress meanwhile;
    // `search_first` (the attract demo's car change every 300 frames, mode 2's manual change)
    // searches at once, the current record included, instead of re-arming.
    void car_changed(double manual_seconds, bool search_first = false);
    // The cameras' target was set this frame (a new car or pair): an on-board or 0x400 shot,
    // or one the car is outside of, ends.
    void target_set() { target_set_ = true; }
    // Mode 2's TOP / AUTO switch to another car: a target set that searches at once when it
    // ends the shot.
    void target_switched() { target_set_ = switched_ = true; }
    // Mode 2's gate (every 60 Hz frame): the shot length x and whether a cut request is
    // pending (the camera manager's, presentation §G14).
    void set_cut_gate(float x, bool request) { gate_x_ = x, gate_request_ = request; }
    // The view mode 0..3 (a change is the view's reset).
    void set_view_mode(int mode);
    int view_mode() const { return mode_; }
    // The director's random generator: mode 1's cuts, mode 2's on-board cuts, the shot
    // colour filter and the attract demo's view all draw from it. The game seeds it from a
    // clock value when a replay starts.
    GameRandom& rng() { return rng_; }
    void seed(u32 x) { rng_.x = x; }
    // Counts the cuts (+1 at every new shot of any mode).
    int cut_serial() const { return serial_; }

    // Mode 2: whether the followed car has a battle partner now (its on-board cuts need one).
    void set_pair(bool on) { pair_ = on; }
    // Mode 2's on-board cut on the partner's car, in its first 3 s (180 frames).
    bool onboard_cut() const { return onboard_ && on_partner_ && onboard_time_ < kOnboardCutSeconds - 1e-6; }
    // The course camera's shot (or turn on board) is under 3 s (180 frames) old: mode 2's
    // TOP / AUTO waits (presentation §G3).
    bool camera_fresh() const { return armed_time_ < kOnboardCutSeconds - 1e-6; }
    // The camera turned on board (the followed car's in-car view or mode 2's cut).
    bool on_board() const { return onboard_; }
    // A course shot in its first 2 s (120 frames): the battles take no new partners meanwhile.
    bool settling() const { return cur_ >= 0 && !onboard_ && shot_time_ < 2.0 - 1e-6; }
    // The two cars of a pair, for the zoom that keeps both in view.
    struct Pair {
        render::Vec3 followed, partner;
    };
    static constexpr float kOnboardCutSeconds = 3.0f; // (the on-board turn's 180 frames)
    static constexpr double kVariantHold = 3.0;       // a shot of another lap variant holds 180 frames

    struct Shot {
        bool valid = false;   // false: no record covers the car (use a fallback camera)
        bool onboard = false; // mode 2's cut: on board the battle partner's car
        bool on_partner = false; // (the same)
        int record = -1;
        int procedural = -1;  // mode 1: the cut's procedural camera 0..4 (no record); modes 0,
                              // 2, 3: 4 (a car mount) for an on-board record or turn
        render::Vec3 eye, target;
        float fovy = 0.8f;    // radians, vertical
        float roll = 0;       // radians about the view axis (mode 1's course shots)
        float seconds = 0;    // since the cut
        // A course shot (its record's camera, on-board records included) whose record has the
        // haze flag (GtrcRecord::haze); never a turn on board or a procedural camera, whose
        // flags are 0 or 0x4000 (presentation §L6).
        bool haze = false;
    };
    // A GT3 zoom (1 / tan of the half horizontal angle over the 640-pixel frame) as our
    // vertical field of view: the 448-line frame (tan(half) = 0.7 / zoom) after GT3's
    // Scale(1, 1 / kYScale, 1) (presentation §G8), tan(half) = 0.7 x 1.098 / zoom.
    static constexpr float kYScale = 1.098f; // program constant
    static float zoom_fovy(float zoom) { return 2.0f * std::atan(0.7f * kYScale / std::max(zoom, 1e-3f)); }
    // `s` = car's track distance (0..L), `lap` = its lap counter (lap_counter: 0 before the
    // start line), `car` = the point the camera aims at (the followed car, or between a pair),
    // `dt` = frame time (zoom smoothing, time-based records), `aspect` = viewport width /
    // height, `pair` = the pair's cars.
    Shot update(float s, int lap, const render::Vec3& car, double dt, float aspect, const Pair* pair = nullptr);
    // The view's horizontal angle (radians) widened to keep a pair in view from `eye`: 1.1 x the
    // cars' angle apart plus the angle of 5 m at the partner's distance (when the partner is the
    // nearer car: that distance less 60 x the difference, at least 1 m), at most 70 degrees.
    static float pair_angle(const render::Vec3& eye, const Pair& pair, float angle);

    // t (0..1) of record `r` at progress u = (D - start) / (end - start), `seconds` after its cut.
    static float shot_t(const GtrcRecord& r, float u, double seconds);

    // --- Mode 1 (avant-garde) ---
    // One draw of the director's generator as the original makes it (a step, then a unit
    // draw that steps again).
    float draw();
    // One mode-1 frame, the handler's start (60 Hz): steps the shot's trail timer and says
    // whether to cut: a request (`request` > 0, dropped during the standing-start hold
    // `hold`), the view's reset flag (taken) or a pad cut (Cross, Circle, L1, R1).
    bool avant_garde_due(int request, bool hold, bool pad);
    struct FieldCar {
        float s = 0;     // distance along the lap (m)
        int lap = 0;     // its lap counter (lap_counter)
        float speed = 0; // along the track (m/s, negative backwards)
        int mounts = 0;  // its camera mounts (GTCI)
    };
    struct Cut {
        int car = 0;        // the car the cut follows (indexes the cars)
        bool pair = true;   // aimed with its battle partner when it has one
        float dissolve = 0; // seconds of dissolve from the last picture (0 none)
    };
    // One cut (presentation §G2), the original's draws in order. `followed` indexes `cars`;
    // `pick_other` (TARGET AUTO in a field): a random other car first. Then the first car from
    // there with a free course shot (find_free_shot, lasting `x` s), with a roll of -10 .. 10
    // degrees, a wide zoom (p 0.17), a yaw seed (p 0.3, no effect) and a roll swing from
    // +-100 degrees (p 0.1); with p 0.25, or when no car has one, a procedural camera
    // (procedural_kind; the standing-start `hold`: always the car mount). Then the look
    // (roll_look; the orbit camera raises the trail's odds to 0.6, request 3 lowers the
    // dissolve's to 0.1). The car's partner is left out for request 1 when the first car
    // tried had a course shot. `camera_seed`: the procedural camera's own generator.
    Cut avant_garde_cut(const std::vector<FieldCar>& cars, int followed, bool pick_other, float x, int request,
                        bool hold, u32 camera_seed);
    // A course shot for a car: {record, lap variant}. Variants from the car's own (at least 1)
    // on (past nvar: 1);
    // a record containing D = k L + s at u in [0, 0.8], not the last course shot, lasting at
    // least `min_seconds` at the car's speed (at least 3 m/s), and whose camera starts at
    // least 15 m (horizontally) from the last course shot's.
    std::optional<std::pair<int, int>> find_free_shot(const FieldCar& car, float min_seconds) const;
    // The procedural camera for r in [0, 1): 0 .. 4 with p 0 / 0.3 / 0.2 / 0.2 / 0.3 for
    // shots under 3 s, 0.35 / 0.25 / 0.1 / 0.15 / 0.15 otherwise, always 4 in the hold.
    static int procedural_kind(float r, float shot_seconds, bool hold = false);
    const ProceduralShot& procedural_shot() const { return proc_; }
    // The shot's look: levels (p 0.25: black 0.175 r, white 0.2 + 0.8 r', swapped when the
    // first draw is under 0.0125), the trail (r - trail_bias < 0.1: timer (150 r + 50) / 100,
    // fading with p 0.5), the dissolve (r - dissolve_bias < 0.4: returned true), the colour
    // filter (on: kept with p 0.8; off: on with p 0.1, saturation 1 / r / 0 at 0.7 / 0.85,
    // preset int(5 r) mod 5). Mode 1's cuts roll it; also our fallback cameras' cuts.
    bool roll_look(float trail_bias = 0, float dissolve_bias = 0);
    const ShotLook& look() const { return look_; }
    // The dissolve a cut of modes 0, 2 and 3 asked for since the last call (s, 0 none).
    float take_dissolve() { return std::exchange(pending_dissolve_, 0.0f); }
    // No look (a replay's set-up and every REPLAY OPTIONS apply, presentation §G6).
    void clear_look() { look_ = {}; }

    // The course camera's shot state (presentation §H8): 0 running, 1 at its end (a cut may
    // come on the gate), 2 over. Modes 0 / 3: 2 once run out; mode 2: 1 once run out, but for
    // a 0x400 record not on board 1 in its last tenth (t past 0.9, before 0.1 driving
    // backwards) and 2 once run out. 0 while a variant shot's 3 s hold runs.
    static int shot_state(int mode, bool run_out, bool kind_400, bool on_board, float t, bool backwards);
    // The gate on a state: 2 open, 1 open once the shot length x is 1.3 s or more or a
    // request is pending, 0 closed.
    static bool gate_open(int state, float x, bool request) { return state == 2 || (state == 1 && (x >= 1.3f || request)); }
    // The current shot's gate (the attract demo's car change waits for it).
    bool shot_gate() const { return gate_open(state(), gate_x_, gate_request_); }
    // Mode 2's TOP / AUTO switch gate: the shot over, the shot length 1.3 s or more, or a request.
    bool switch_gate() const { return state() == 2 || gate_x_ >= 1.3f || gate_request_; }

private:
    // Modes 0, 2, 3: one frame of the shot picker; true at a new shot (a cut, a turn on
    // board or a re-arm).
    bool pick(int k, float s, float D);
    void cut_to(int record, int variant, int k);
    void rearm(int k);
    void turn_on_board(bool partner);
    void dissolve_out();
    // The shot's own D (a variant's, or the car's carried over a wrap of its lap variants).
    float shot_distance(float D, int k, float s) const;
    // Whether the current shot runs out at D after `seconds` (a timed record's time, else u).
    bool runs_out(float D, int k, float s, double seconds) const;
    int state() const;
    Shot evaluate(const GtrcRecord& r, float D, const render::Vec3& car, double dt, const Pair* pair = nullptr);

    const GtrcScript* s_;
    int mode_ = 0;
    GameRandom rng_;
    int cur_ = -1, k_ = 0;     // the course camera's record (kept by mode 1's procedural cuts); mode 1's variant
    float zoom_ = 0;
    float offset_ = 0;         // mode 1: added to k * L + s (line crossings during a shot)
    int serial_ = 0;
    double shot_time_ = 0;     // seconds since the cut (or re-arm)
    // Modes 0, 2, 3 (the course camera's state).
    int variant_ = -1;         // the shot's own lap variant (-1: the followed car's)
    int k0_ = 0;               // the car's lap variant at the cut
    bool onboard_ = false, on_partner_ = false; // turned on board (the in-car view, or mode 2's cut)
    bool turned_ = false;      // turned on board since the set-up (a later turn has no dissolve)
    double onboard_time_ = 0;
    bool run_out_ = false;     // the shot ran out at its last evaluation
    float last_t_ = 0, last_D_ = 0;
    bool backwards_ = false;
    bool hold_on_ = false;     // a variant's shot (or a turn on board) from the gate: holds hold_ s
    double hold_ = 0;
    double manual_ = 0;        // the manual car change's window (s)
    bool target_set_ = false, attract_change_ = false, switched_ = false;
    double armed_time_ = 0;    // since the last cut, re-arm or turn on board
    float gate_x_ = 4.0f;
    bool gate_request_ = false;
    bool pair_ = false;
    // Mode 1.
    bool reset_flag_ = true;   // the view's reset: the next update (modes 0, 2, 3) or mode-1 frame
    ProceduralShot proc_;      // the cut's procedural camera (kind -1: a course shot or none)
    int last_record_ = -1;     // the last course shot (the 15 m rule)
    float roll_ = 0;           // degrees
    bool wide_ = false;        // the course shot's wide zoom (1.0)
    float swing_ = 0, swing_v_ = 0; // the course shot's roll swing (degrees, car-aimed records)
    double swing_acc_ = 0;
    ShotLook look_;
    float pending_dissolve_ = 0;
};

} // namespace gt3r::race
