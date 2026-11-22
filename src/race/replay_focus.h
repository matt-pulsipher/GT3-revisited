#pragma once

// The car the replay cameras follow in a field replay, and its battle partner (presentation
// §G3). SDL-free; the race screen feeds it one 60 Hz frame at a time.
//
// Battles: every car has a state (0 none, 1 candidate, 2 battle, 3 after a battle) and a
// partner car. The relation of two cars is 1 within 10 m, else 0 (the original also takes 2 / 3
// from its overtaking pair states, which are not reproduced). A car without a partner searches
// once it has stood 240 frames (4 s): the best-placed car within 20 m related by 1 or more
// becomes its candidate. A candidate becomes a battle when another car within 20 m qualifies
// (or the pair's relation is 2 / 3), turns into state 3 after 121 frames within 10 m, and is
// dropped when the cars are not related or the camera's shot has not settled. A battle (and
// state 3) ends beyond 30 m; a battle turns into state 3 after 240 frames, state 3 ends after
// 241 more. A car the cameras began to follow is held: its state stands still for the hold's
// frames (40 for every car at the start).
//
// Interest: a score per car, recomputed every frame by the target. MANUAL: 1200 for the
// followed car. TOP: 1200 for the leader. AUTO: from the rank's base when the target was set
// (400 / 300 / 200 / 100 / 100 / 100), +10 at random each frame (p 1/60, 1/100, 1/150, 0.0057,
// 0.0047, 1/300 by rank), -1/30 a frame for the followed car and its rival, +150 for a place
// gained, -100 for one lost, clamped 0..1200; plus a bonus by battle state (1: 100, 2: 400,
// 3: 200) times the rank's factor (1.5, 0.95, 0.85, 0.75, 0.7, 0.65). The cameras re-pick the
// car with the highest score + bonus, never in the first 240 frames after a switch, then every
// 30 frames with p (frames - 240) / 1560, and every frame after 1800 frames; a better car
// becomes the pending pick, which the view mode applies when it can (take_pending).

#include "base/bytes.h"
#include "base/game_random.h"
#include "render/math3d.h"

#include <array>
#include <vector>

namespace gt3r::race {

class ReplayFocus {
public:
    static constexpr int kMaxCars = 32; // (GT3: 7; a field of our own has up to 32 cars, ranks past 6th as the 6th)
    static constexpr int kStartHold = 40; // frames every car is held at a replay's start
    enum Target { Manual = 0, Top = 1, Auto = 2 };
    enum Battle { None = 0, Candidate = 1, Fight = 2, After = 3 };
    struct Car {
        render::Vec3 pos; // world position (m)
        int rank = 0;     // race position, 0 = the leader
    };

    // A new replay: no battles, no ranks yet, every car held kStartHold frames.
    void begin();
    // The target was set (a replay's start, REPLAY OPTIONS): the scores start from the target,
    // the pick is the followed car, the re-pick clock restarts and the generator takes `seed`
    // (the original: a clock value).
    void reset_interest(int followed, int target, const std::vector<Car>& cars, u32 seed);
    // One 60 Hz frame. `battles`: the race is running (battle states and ranks stand still
    // before the start); `settled`: the camera's shot has run its first 2 s (new partners wait).
    void step(const std::vector<Car>& cars, int followed, int target, bool battles, bool settled);

    int partner(int car) const; // the rival while in a battle or after one (states 2, 3), else -1
    int battle(int car) const;
    bool battle_changed(int car) const; // a battle began or ended (or a candidate dropped) this frame
    int hold(int car) const;
    void set_hold(int car, int frames);
    // TOP / AUTO: the car the scores want, or -1.
    int pending() const { return pending_ ? pick_ : -1; }
    // Switches to the pending car: its score + 200, every other car's x 0.98 / 0.84 / 0.95 by the
    // new car's battle state 1 / 2 / 3, the left car's back to its rank's base; the re-pick clock
    // restarts. Returns the new car.
    int take_pending(int cars);
    float score(int car) const;
    float bonus(int car) const;
    int frames() const { return frames_; }
    // MANUAL Up / Down: the next (dir 1) or previous (dir -1) car, wrapping.
    static int next_car(int from, int dir, int cars);
    // The relation of two cars (1 within 10 m).
    static int relation(const Car& a, const Car& b);

private:
    struct Entry {
        bool fight = false;   // states 2 and 3 (the partner counts)
        bool changed = false; // set this frame
        int partner = -1;
        int state = None;
        int timer = 0;
    };
    void step_battles(const std::vector<Car>& cars, bool settled);
    void step_scores(const std::vector<Car>& cars, int followed, int target);
    int search(int car, int min_relation, const std::vector<Car>& cars) const;
    int rank_index(int car) const; // the rank clamped 0..5 (no rank yet: 0)

    std::array<Entry, kMaxCars> e_{};
    std::array<int, kMaxCars> hold_{}, rank_{}, prev_rank_{};
    std::array<std::array<int, kMaxCars>, kMaxCars> rel_{};
    std::array<float, kMaxCars> score_{}, bonus_{};
    int pick_ = 0, prev_pick_ = 0;
    bool pending_ = false;
    int frames_ = 0;
    GameRandom rng_;
};

} // namespace gt3r::race
