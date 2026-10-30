#pragma once

// In-race HUD, reproducing GT3's layout (facts from the race workstream's
// HUD research, summarised in docs/dev/RACE.md "HUD"):
//
// - The game lays the HUD out on a 640x448 frame; elements aligned with bit 4
//   ("left/right + wide") are pushed outward by 106.67 units in 16:9 mode.
//   We draw that 16:9 layout onto the 640x360 UI: x' = (x + 106.67) * 0.75,
//   y' = y * 360 / 448.
// - Meter cluster per view: remote (chase/TV cameras) = revb_NN tachometer at
//   the bottom right with digital speed and gear inside; onboard (bumper/roof)
//   = rev_NN tachometer + speed_XXX speedometer at the bottom centre.
// - Round meters: face stretched to its box; pivot = box centre + offset;
//   angle = a0 + 90 + t (a1 - a0) degrees (y down, clockwise); tapered red
//   needle from n0 r to n1 r; a thin segmented band at 0.95..0.98 r, red from
//   the shift-up rpm.
// - Tach scale = floor((rev limit + 1500) / 1000) * 1000; face = first of
//   06..12, 18 that covers it.
// - Gear: font "gearr", unlit "8" behind the digit, amber (R in red), plus a
//   blinking shift light (3 frames on / 3 off) above the shift-up rpm.
// - Digital speed: font "speed", amber, "%3d", unit label in sw_12.
// - Mini-map: the course's GTCM image and transform (see set_course_map),
//   player dot 1pcar_dot on top of ecar_dot opponents.
// - Which parts a run shows and where come from its display mode's rows (race/hud_layout.h).

#include "race/hud_layout.h"
#include "race/lap_events.h"
#include "text/game_font.h"

#include <SDL3/SDL_render.h>

#include <map>
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace gt3r {
class Vfs;
}

namespace gt3r::race {

// HUD parts shown (the REPLAY OPTIONS ITEM bits; presentation §G4). Live play shows all but
// the steering and accel / brake monitors.
enum HudItem : u8 {
    kItemMeters = 1, kItemCarName = 2, kItemTimes = 4, kItemMap = 8, kItemSteering = 16, kItemTires = 32,
    kItemAccelBrake = 64,
};
constexpr u8 kLiveItems = u8(~(kItemSteering | kItemAccelBrake));

struct HudData {
    // The race display's layout (presentation §D4): what is shown and where (race/hud_layout.h).
    HudMode mode = HudMode::Race;
    u8 items = kLiveItems;
    bool live = true; // live play: the position and the message display (WRONG WAY, AUTO DRIVE)
    float speed_kmh = 0;
    float rpm = 0;
    float rev_limit_rpm = 7000; // tach scale and red band
    float shift_rpm = 6800;     // red band start, shift light
    float top_speed_kmh = 250;  // estimate; selects the speedometer scale
    bool imperial = false;      // mph (NTSC-U): speed_120mp/160mp/200mp faces, "mph" label
    bool has_turbo = false;     // boost meter shown
    float boost = 0;            // kgf/cm^2 (needle 0..2)
    int gear = 1;          // -1 R, 0 N
    bool automatic = true;
    bool rev_limiter = false;
    bool onboard = false;  // onboard cluster (bumper/roof cameras) instead of the remote one
    float throttle = 0, brake = 0;
    float steer = 0;       // -1..1, positive to the right (the steering monitor)
    int lap = 0;           // current lap number (1-based; 0 = not started)
    double lap_time = 0, last_lap = -1, best_lap = -1;
    // Total / Time (presentation §D4): the run's recorded laps and the current one (their sum
    // alone once finished); < 0 none.
    double total_time = -1;
    // The lap-time list: the followed car's recorded laps (the last 8, oldest first), how many
    // were recorded in all (their numbers) and how many lines the list shows.
    std::vector<double> lap_times;
    int laps_recorded = 0;
    int lap_list_lines = 3;
    // The session's fastest lap driven (Record Lap, P.P., Fastest; no stored record seeds it);
    // < 0 none. P.P. shows the picture of the car that holds it (the race screen draws it in
    // pole_icon_rect()).
    double session_best = -1;
    bool pole_icon = false;
    bool finished = false;  // the followed car has finished: the Lap line goes (with the list or Last)
    bool wrong_way = false;
    int total_laps = 0;     // race: laps to run (0 = time trial)
    int position = 0, cars = 0; // race: current position of `cars`
    double countdown = -1;  // race: seconds to the start (> 0), 0 = "GO", < 0 = none
    std::string car_name;    // replay item: the database name as game text (Car::latin_name's bytes)
    std::string course_name;
    std::string camera;
    // Mini-map: world x/z of the player and of the other cars.
    float player_x = 0, player_z = 0;
    std::vector<std::pair<float, float>> others;
    bool split = false;      // 2P half (racemode 7 layout)
    double finish_t = -1;    // seconds since this player finished (FINISH / PLAYER n WIN! for 4 s)
    int winner_player = 0;   // 2P: 1/2 = this half's player won
    // Part of the game area (Hud::set_screen) no touch control covers (App::touch().safe_area()) and
    // the visible controls' bounds (UI units; none while hidden). The meters and the tyre
    // gauge move inside the area; the top corner blocks (rank and map, laps and times) stay
    // in their corners unless a control would cover them there. Full area = the normal layout.
    SDL_FRect safe_area{0, 0, 640, 360};
    std::vector<SDL_FRect> touch_controls;
    // Licence test: gold / silver / bronze target times (s) shown under the clock.
    std::array<double, 3> license_targets{-1, -1, -1};
    // The top-speed machine test's "Best" speed line: the highest speed so far (km/h; 0 or
    // less: dashes). Its "Record" line is a value the game never sets (dashes).
    float best_kmh = -1;
    // Tyre gauge (races with tyre wear): per-wheel level -63..127 (FL, FR, RL, RR).
    bool show_tires = false;
    std::array<int, 4> tire_levels{};
    int layer = 0;            // 0 all, 1 all but the lap / time block, 2 that block only
    bool four_three = false;  // drawn into a 4:3 viewport (original resolution): no 16:9 widening
                              // (a 4:3 screen has none to give anyway)
    bool auto_drive = false;  // "AUTO DRIVE" (the computer drives: pit lane, flying start run-up)
    // Odometer cluster: the car's total distance (km or miles, by `imperial`) and this race's
    // trip distance (m); indicator lamps: 0 off, 1 on, 2 acting (blinks).
    double odometer_km = 0, trip_m = 0;
    int asm_lamp = 0, tcs_lamp = 0;
    bool handbrake_lamp = false, oil_lamp = false;
    bool clutch_engaged = true; // gear digit at half brightness when false
    int hint_gear = -1;         // recommended gear ("> n" in red after the digit), -1 = none
    bool hint_blink = false;    // blinks red / black in braking zones
    std::string intro_title;  // the course name in capitals over a run's or replay's start (game text)
    float title_alpha = 1.0f; // its opacity (race::TitleClock)
    // The event display (presentation §D6): lap time, splits, gaps, "Pitting in...", drawn at
    // its anchor; the gap's car picture is drawn by the race screen in event_icon_rect().
    std::vector<EventDisplay::Message> events;
    bool event_icon = false;
    float event_icon_dx = 0, event_icon_dy = 0;
};

class Hud {
public:
    Hud();
    ~Hud();
    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;

    // Loads fonts and the display images (race/<language>/display.imgs, else the US, UK or
    // JP set); missing assets fall back to placeholders.
    void load(const Vfs& vfs, SDL_Renderer* r, const std::string& language = "US");
    // Course map (course file slot 33, "GTCM"); empty span clears it.
    void set_course_map(SDL_Renderer* r, ByteSpan gtcm);
    // The game area (UI units): 640x360 on 16:9, 480x360 at 4:3, wider with fill (App::aspect).
    // The draws without a viewport fill it; the 640 x 448 layouts keep their shape in the middle
    // and the elements GT3 pushes outward in 16:9 go to its sides.
    void set_screen(const SDL_FRect& r) { screen_ = r; }
    // 2P battle half (digital speed, bar tachometer "meter_mini", mini-map at 70 %, no round
    // meters). 16:9: racemode 8 (426.667x448 per half); horizontal: racemode 7 (640x224).
    // `horizontal`: GT3's 4:3 top/bottom split layout (racemode 7, 640x224 per half). Each
    // layout is as wide as on the 16:9 screen at most (narrower halves squeeze it, as GT3 does
    // on a 4:3 one); a wider half spreads its left, middle and right parts apart.
    void draw_split(SDL_Renderer* r, const HudData& d, const SDL_FRect& half, bool horizontal = false);
    // Replays: the "GRAN TURISMO 3" mark (gt_logo_mini) at the bottom right, at `alpha`
    // (race::ReplayMarkTimer).
    void draw_replay_mark(SDL_Renderer* r, float alpha, const SDL_FRect* viewport = nullptr, bool four_three = false);
    // The title row alone (d.intro_title at d.title_alpha): replays show it whatever ITEM says.
    void draw_title(SDL_Renderer* r, const HudData& d, const SDL_FRect* viewport = nullptr);
    // GT3's race pause: CONTINUE / EXIT buttons over the running scene (`selected` 0 / 1).
    void draw_pause(SDL_Renderer* r, int selected, const SDL_FRect* viewport = nullptr, bool four_three = false);
    // GT3's replay pause: CONTINUE / EXIT / REPLAY OPTIONS on a dark strip (`selected` 0..2).
    void draw_replay_pause(SDL_Renderer* r, int selected, const SDL_FRect* viewport = nullptr, bool four_three = false);
    // `viewport` (UI coordinates): draw into that rectangle instead of the whole screen (split screen).
    void draw(SDL_Renderer* r, const HudData& d, const SDL_FRect* viewport = nullptr);
    text::GameFonts& fonts() { return fonts_; }
    const text::GameFonts& fonts() const { return fonts_; }
    // Where the last draw() / draw_split() put the event display's car picture (UI units;
    // w = 0: none).
    const SDL_FRect& event_icon_rect() const { return event_icon_rect_; }
    // Where the last draw() put the P.P. line's car picture (UI units; w = 0: none).
    const SDL_FRect& pole_icon_rect() const { return pole_icon_rect_; }

    // "1'23.456"; race::kHudNoTimeText (the font's dashes, "-'--.---") for negative times.
    static std::string format_time(double seconds);
    // A speed line's value (the unit is drawn after it): "%.1f" of the speed in km/h or mph
    // (x 0.62139), at most 999.9; 0 or less race::kNoSpeedText (the font's "---.-").
    static std::string speed_text(float kmh, bool imperial);
    // Tachometer scale (rpm) for a rev limit, as the game picks it.
    static int tach_scale(float rev_limit_rpm);
    // Speedometer (presentation §D1): N = clamp(int(v_top x 1.1 / 22.2222) + 1, 2, 4), v_top
    // in m/s; the needle's full scale N x 80 km/h or N x 50 mph; the face the first of
    // speed_180km / 240km / 320km (speed_120mp / 160mp / 200mp) that reaches that scale;
    // band segments every 10 km/h or 5 mph.
    struct SpeedScale {
        int n = 2;
        float full = 160;            // the needle's full scale (km/h or mph)
        const char* face = "speed_180km";
        float step = 10;             // band segment (km/h or mph)
    };
    static SpeedScale speed_scale(float top_speed_kmh, bool imperial);
    // The event display's row in a layout and camera view (0 in-car, 1 outside), or null: it
    // is drawn with the time item only (presentation §D6, §D9: live play has it; in replays
    // the REPLAY OPTIONS ITEM time), and only where the layout has it (the demonstration
    // layout's in the in-car view alone). The second event display's rows are never drawn:
    // nothing is ever posted to it.
    static const HudRow* event_row(HudMode m, u8 items, int view);

private:
public:
    static SDL_Color tire_color(int level);

private:
    struct Image {
        SDL_Texture* tex = nullptr;
        float w = 0, h = 0;
    };
    struct Meter {
        const Image* face = nullptr;
        float x, y, w, h;   // box in 640x448 units (already aligned)
        float ox, oy, r;    // pivot offset and radius in face pixels
        float a0, a1;       // degrees
        float n0, n1;       // needle radius fractions
        float scale;        // full scale value
        float red_from;     // band turns red here (0 = no red)
        float step;         // band segment size (0 = no band)
    };
    void draw_meter(SDL_Renderer* r, const Meter& m, float value);
    void draw_meters(SDL_Renderer* r, const HudData& d);
    void draw_panel(SDL_Renderer* r, float x, float y, float w, float h);
    void draw_gear(SDL_Renderer* r, const HudData& d, float gx, float gy, float gw, float gh, float dx, float dy);
    void draw_times(SDL_Renderer* r, const HudData& d, int view);
    void draw_map(SDL_Renderer* r, const HudData& d, int view);
    void draw_rank(SDL_Renderer* r, const HudData& d, int view);
    // What `block` would cover with the current transform: a rectangle per text and picture
    // (UI units). It runs measuring only (text() and blit() draw nothing).
    std::vector<SDL_FRect> measure(const std::function<void()>& block);
    void blit(SDL_Renderer* r, SDL_Texture* t, const SDL_FRect& dst); // or measures it
    // `baseline`: y is the glyph baseline (the game's pen y for the sw_*, odo and speed fonts);
    // otherwise the top of the line. `spacing`: extra pixels after each glyph.
    void text(SDL_Renderer* r, float x, float y, const std::string& s, const char* font, float scale, SDL_Color c,
              text::HAlign a, bool baseline = false, float spacing = 0);
    float text_width(const std::string& s, const char* font, float scale = 1, float spacing = 0);
    // The same for game text (database names: escapes and JIS pairs, drawn as the game draws them).
    void game_text(SDL_Renderer* r, float x, float y, const std::string& s, const char* font, float scale, SDL_Color c,
                   text::HAlign a, bool baseline = false, float spacing = 0);
    float game_text_width(const std::string& s, const char* font, float scale = 1, float spacing = 0);
    void draw_text(SDL_Renderer* r, float x, float y, const std::string& s, bool game, const char* font, float scale,
                   SDL_Color c, text::HAlign a, bool baseline, float spacing);
    float width_of(const std::string& s, bool game, const char* font, float scale, float spacing);
    // Lamp row / glyph runs in individual colours (odometer font glyphs), pen = left baseline.
    void glyph_run(SDL_Renderer* r, float x, float y, const std::vector<std::pair<std::string, SDL_Color>>& run,
                   const char* font);
    void polygon_strip(SDL_Renderer* r, const std::vector<SDL_FPoint>& pts, SDL_Color c); // HUD units
    const Image* image(const std::string& name) const;

    text::GameFonts fonts_;
    std::map<std::string, Image> images_; // display.imgs entries by name
    bool fonts_ok_ = false;
    // Course map.
    Image map_;
    // tire_gage split by its alpha codes: the four tyre bodies and the outlines.
    std::array<Image, 4> tire_body_{};
    Image tire_outline_;
    float map_ox_ = 0, map_oz_ = 0, map_scale_ = 1, map_rot_ = 0;
    int blink_ = 0, shift_counter_ = 0, lamp_counter_ = 0;
    SDL_FRect event_icon_rect_{0, 0, 0, 0}, pole_icon_rect_{0, 0, 0, 0};
    SDL_FRect screen_{0, 0, 640, 360}; // set_screen
    // measure(): text() and blit() add their bounds to measured_ instead of drawing.
    bool measuring_ = false;
    std::vector<SDL_FRect> measured_;
};

} // namespace gt3r::race
