#include "race/race_audio.h"

#include "audio/audio_engine.h"
#include "audio/bgm.h"
#include "audio/car_sound_model.h"
#include "race/car_sfx.h"
#include "base/game_random.h"
#include "base/log.h"
#include "formats/bgm_index.h"
#include "formats/db/gt_db.h"
#include "vfs/vfs.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gt3r::race {

float ReverbAmbience::target_for(u32 flags) {
    switch (flags & 0x18) {
    case 0x08: return 0.5f;
    case 0x10:
    case 0x18: return 0.25f;
    default: return 0.125f;
    }
}

void ReverbAmbience::tick(VoiceMixer& mixer, u32 flags) {
    constexpr float kStep = 1.0f / 36.0f;
    depth_ += std::clamp(target_for(flags) - depth_, -kStep, kStep);
    s16 evol = s16(std::lround(depth_ * 32767.0f));
    mixer.set_reverb_depth(evol, evol);
}

void ReverbAmbience::reset(VoiceMixer& mixer) {
    depth_ = 0.125f;
    mixer.set_reverb_depth(0x0FFF, 0x0FFF);
}

void RaceMusic::stop(AudioEngine& audio) {
    if (active_) {
        audio.bgm().stop(audio);
        level_.stop();
    }
    active_ = false;
    seen_ = false;
    race_piece_ = marked_request_ = false;
    follow_piece(audio);
    // The next run here starts with this run's last song (set_first_song).
    if (same_song_ && run_song_ && at_ < history_.size()) first_ = history_[at_];
    run_song_ = false;
}

std::vector<RaceMusic::Action> RaceMusic::decide(const Cues& c, bool music_playing) {
    using Op = Action::Op;
    std::vector<Action> out;
    // A new run: no jingle or pause state carried over (a retry from the pause window stopped the
    // paused piece: nothing to resume). (The run's BGM switch does not change the requests: off,
    // they are made at level 0, RaceMusicLevel.)
    const bool first = !seen_;
    if (first) {
        jingle_ = start_jingle_ = medal_played_ = demo_played_ = was_paused_ = false;
        was_finished_ = c.finished;
        was_holding_ = false;
    }
    seen_ = true;
    // Quick menus: their music at the menu level, looping, when the Menu BGM switch is
    // on (the race switch does not apply); closing one stops the BGM.
    if (c.menu != was_menu_ || (first && c.menu != Menu::None)) {
        if (c.menu == Menu::None || !c.menu_music_on) {
            out.push_back({Op::Stop});
        } else {
            out.push_back({Op::PlayIfDifferent, c.menu == Menu::Result ? bgm::kResult : bgm::kQuickMenu, true,
                           bgm::kMenuVolume});
        }
        was_menu_ = c.menu;
        jingle_ = start_jingle_ = false;
        was_finished_ = c.finished;
        was_paused_ = c.paused;
        medal_played_ = false;
        // The run starts when its menu closes (the pre-race menu's Start, the result menu's
        // Retry): a hold already on behind the menu is the start of the run, so the start
        // jingle plays now, after the stop (music §2.2, §3.3).
        was_holding_ = false;
        if (c.menu != Menu::None || !c.holding) return out;
        music_playing = false;
    }
    if (c.menu != Menu::None) return out;
    if (c.paused != was_paused_) {
        out.push_back({c.paused ? Op::Pause : Op::Resume});
        was_paused_ = c.paused;
    }
    if (c.paused) return out;
    // Race songs: 0.5 in a live run, 0.7 in a replay; none in licence tests or when the
    // playlist has no song in the list.
    const bool songs = c.songs && c.kind != Kind::License && c.kind != Kind::LicenseReplay;
    const Action song{Op::Play, bgm::kRace, false, c.replay ? bgm::kReplaySongVolume : bgm::kRaceSongVolume};
    // The start: standing starts play a jingle while the cars are held (type 1,
    // the 4 s hold: "jg04e"; type 2, the 10 s hold: "jg11"); the race music
    // replaces it at the signal (licence tests: the music stops instead).
    if (c.holding && !was_holding_ && c.kind != Kind::Demo && (c.start_type == 1 || c.start_type == 2)) {
        out.push_back({Op::Play, c.start_type == 1 ? bgm::kStartShort : bgm::kStartLong, false});
        jingle_ = start_jingle_ = true;
    } else if (!c.holding && was_holding_ && start_jingle_) {
        start_jingle_ = jingle_ = false;
        if (!songs) out.push_back({Op::Stop});
        else out.push_back(song), music_playing = true;
    }
    was_holding_ = c.holding;
    if (c.finished && !was_finished_) {
        // The finish: "jg12" for every finish, whatever the place (2P: only the
        // winner's); "fail" for a failed licence test or a voided time trial. A
        // licence pass has no finish jingle, only the medal's (below).
        start_jingle_ = false;
        if (c.failed) out.push_back({Op::Play, bgm::kFail, false}), jingle_ = true;
        else if (c.kind == Kind::License) jingle_ = true; // silence until the medal jingle
        else if (c.kind != Kind::Demo && (c.kind != Kind::TwoPlayer || c.position == 1))
            out.push_back({Op::Play, bgm::kFinish, false}), jingle_ = true;
        medal_played_ = false;
    } else if (!c.finished && was_finished_) {
        jingle_ = false; // restart: race music again
        medal_played_ = false;
    }
    was_finished_ = c.finished;
    if (c.kind == Kind::License && c.finished && !c.failed && c.medal >= 0 && !medal_played_ &&
        c.since_finish >= kMedalDelay) {
        static const int kMedal[3] = {bgm::kLicenseGold, bgm::kLicenseSilver, bgm::kLicenseBronze};
        out.push_back({Op::Play, kMedal[std::min(c.medal, 2)], false});
        medal_played_ = true;
    }
    // Race songs from the start signal (the demo track in attract races), one after another.
    if (!jingle_ && !music_playing && !c.holding) {
        if (c.kind == Kind::Demo) {
            if (!demo_played_) out.push_back({Op::Play, bgm::kDemo, false}), demo_played_ = true;
        } else if (songs) {
            out.push_back(song);
        }
    }
    return out;
}

void RaceMusic::load_playlist(AudioEngine& audio, Vfs& vfs, const std::optional<std::string>& stored) {
    song_ids_.clear();
    for (const std::string& f : audio.bgm().files(vfs, bgm::kRace)) song_ids_.push_back(db::id_hash(f));
    if (stored && !RacePlaylist::from_text(*stored))
        LOG_WARN("race music: the stored playlist is not valid; a new one is made");
    playlist_ = RacePlaylist::stored_or_new(stored, song_ids_);
    playlist_changed_ = false;
}

std::optional<std::string> RaceMusic::take_playlist_change() {
    if (!playlist_changed_) return std::nullopt;
    playlist_changed_ = false;
    return playlist_.to_text();
}

void RaceMusic::set_custom(MusicLibrary* library, MusicSource source, bool mix_shuffle) {
    library_ = library;
    chosen_ = source;
    mix_shuffle_ = mix_shuffle;
}

MusicSource RaceMusic::source() const { return effective_music_source(chosen_, library_ && library_->has_songs()); }

bool RaceMusic::has_songs() const {
    switch (source()) {
    case MusicSource::Original: return playlist_.has_songs();
    case MusicSource::Custom: return library_->playlist().has_songs();
    case MusicSource::Both: return playlist_.has_songs() || library_->playlist().has_songs();
    }
    return false;
}

std::optional<RaceMusic::Played> RaceMusic::take_next() {
    const MusicSource src = source();
    bool custom = src == MusicSource::Custom;
    if (src == MusicSource::Both) {
        RacePlaylist& mine = library_->playlist();
        custom = pick_custom(mix_shuffle_, playlist_.remaining(), mine.remaining(), playlist_.has_songs(),
                             mine.has_songs(), last_custom_, std::uniform_real_distribution<double>(0, 1)(rng_));
    }
    RacePlaylist& list = custom ? library_->playlist() : playlist_;
    std::optional<size_t> k = list.next();
    if (!k) return std::nullopt;
    (custom ? custom_changed_ : playlist_changed_) = true;
    last_custom_ = custom;
    return Played{custom, list.songs()[*k].id};
}

bool RaceMusic::play(AudioEngine& audio, Vfs& vfs, const Played& song, float volume, bool resume) {
    bool ok;
    trim_id_ = 0;
    if (!song.custom) {
        const int track = RacePlaylist::track_index(song.id, song_ids_);
        ok = audio.bgm().play(audio, vfs, bgm::kRace, track, false, volume, resume);
    } else {
        const CustomSong* s = library_ ? library_->find(song.id) : nullptr;
        trim_ = s ? library_->gain(*s) : 1.0f;
        ok = s && audio.bgm().play_file(audio, s->path, bgm::kRace, false, volume, resume, trim_);
        if (ok) trim_id_ = song.id, trim_generation_ = audio.bgm().generation(), trim_ticks_ = SDL_GetTicks();
    }
    song_changed_ |= ok;
    run_song_ |= ok;
    // The player's songs are measured as they come: the next one of the list ahead of time,
    // this one (if it isn't yet) before it.
    if (ok && library_ && source() != MusicSource::Original) {
        if (std::optional<size_t> k = library_->playlist().peek()) library_->want_measured(library_->playlist().songs()[*k].id);
        if (song.custom) library_->want_measured(song.id);
    }
    return ok;
}

void RaceMusic::ease_trim(AudioEngine& audio) {
    const u64 now = SDL_GetTicks();
    const double dt = std::min(double(now - trim_ticks_) / 1000.0, 0.1);
    trim_ticks_ = now;
    if (!trim_id_ || !library_ || audio.bgm().generation() != trim_generation_) return;
    const CustomSong* s = library_->find(trim_id_);
    if (!s) return;
    const float target = library_->gain(*s);
    if (target == trim_) return;
    const float step = std::pow(10.0f, float(6.0 * dt / 20.0)); // 6 dB a second
    trim_ = target > trim_ ? std::min(target, trim_ * step) : std::max(target, trim_ / step);
    audio.set_stream_trim(trim_);
}

void RaceMusic::set_first_song(std::string_view text) {
    same_song_ = true;
    first_.reset();
    const bool custom = text.substr(0, 6) == "yours:";
    if (!custom && text.substr(0, 5) != "disc:") return;
    const std::string hex(text.substr(custom ? 6 : 5));
    char* end = nullptr;
    const u64 id = std::strtoull(hex.c_str(), &end, 16);
    if (!hex.empty() && end == hex.c_str() + hex.size()) first_ = Played{custom, id};
}

std::optional<std::string> RaceMusic::take_song_change() {
    if (!std::exchange(song_changed_, false) || at_ >= history_.size()) return std::nullopt;
    const Played& p = history_[at_];
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%s:%llx", p.custom ? "yours" : "disc", static_cast<unsigned long long>(p.id));
    return std::string(buf);
}

bool RaceMusic::can_play(const Played& song) const {
    const MusicSource src = source();
    const RacePlaylist* list = nullptr;
    if (song.custom && src != MusicSource::Original && library_ && library_->find(song.id)) list = &library_->playlist();
    if (!song.custom && src != MusicSource::Custom &&
        std::find(song_ids_.begin(), song_ids_.end(), song.id) != song_ids_.end())
        list = &playlist_;
    return list && std::any_of(list->songs().begin(), list->songs().end(),
                               [&](const RacePlaylist::Song& s) { return s.id == song.id && s.listed; });
}

void RaceMusic::play_race_song(AudioEngine& audio, Vfs& vfs, float volume, bool resume) {
    // Forward through the songs played first (after a Previous), then new ones from the playlists.
    // A song that no longer plays (a file gone) is passed over.
    for (int guard = 0; guard < 8; ++guard) {
        std::optional<Played> first = run_song_ ? std::nullopt : std::exchange(first_, std::nullopt);
        if (first && can_play(*first)) {
            // The run's first song: the one the last run ended on, from its start (set_first_song).
            history_.resize(std::min(history_.size(), at_ + 1));
            history_.push_back(*first);
            at_ = history_.size() - 1;
        } else if (at_ + 1 < history_.size()) {
            ++at_;
        } else {
            std::optional<Played> next = take_next();
            if (!next) { // no song in the list: no race music
                audio.bgm().stop(audio);
                level_.stop();
                return;
            }
            history_.push_back(*next);
            constexpr size_t kKept = 200;
            if (history_.size() > kKept) history_.erase(history_.begin(), history_.end() - std::ptrdiff_t(kKept));
            at_ = history_.size() - 1;
        }
        if (play(audio, vfs, history_[at_], volume, resume)) return;
    }
}

std::optional<RaceMusic::SongInfo> RaceMusic::now_playing(const AudioEngine& audio) const {
    if (!active_ || !race_piece_ || !marked_request_ || audio.bgm().current() != bgm::kRace) return std::nullopt;
    if (level_.base(last_.music_on) == 0 || history_.empty() || !audio.bgm().playing(audio)) return std::nullopt;
    const Played& p = history_[at_];
    SongInfo info;
    info.custom = p.custom;
    if (p.custom) {
        const CustomSong* s = library_ ? library_->find(p.id) : nullptr;
        if (!s) return std::nullopt;
        info.title = s->title, info.artist = s->artist;
    } else if (const sound::MadsTrack* t = audio.bgm().current_track()) {
        info.title = t->title, info.artist = t->artist;
    }
    return info;
}

bool RaceMusic::skip(AudioEngine& audio, Vfs& vfs, int dir) {
    if (!now_playing(audio) || dir == 0) return false;
    const float v = level_.request(last_.music_on, last_.replay ? bgm::kReplaySongVolume : bgm::kRaceSongVolume);
    const bool resume = !last_.paused;
    marked_request_ = race_piece_ = true;
    if (dir > 0) {
        play_race_song(audio, vfs, v, resume);
    } else {
        if (at_ > 0) --at_;
        if (!play(audio, vfs, history_[at_], v, resume)) play_race_song(audio, vfs, v, resume);
    }
    follow_piece(audio);
    return true;
}

void RaceMusic::cues(AudioEngine& audio, Vfs& vfs, const Cues& c) {
    active_ = true;
    last_ = c;
    for (const Action& a : decide(c, audio.bgm().playing(audio))) {
        switch (a.op) {
        case Action::Op::Play: {
            const float v = level_.request(c.music_on, a.volume);
            // The race songs and the attract demo's track (the game's race-song request,
            // turned into demo_a01 in the demo) carry markers; the jingles none.
            marked_request_ = a.id == bgm::kRace || (a.id == bgm::kDemo && c.kind == Kind::Demo);
            race_piece_ = true;
            if (a.id == bgm::kRace) play_race_song(audio, vfs, v);
            else audio.bgm().play(audio, vfs, a.id, -1, a.loop, v);
            break;
        }
        case Action::Op::PlayIfDifferent: // the quick menus (the menu path, not the race's level)
            race_piece_ = marked_request_ = false;
            audio.bgm().play_if_different(audio, vfs, a.id, 0, a.loop, a.volume);
            break;
        case Action::Op::Stop:
            race_piece_ = marked_request_ = false;
            audio.bgm().stop(audio);
            level_.stop();
            break;
        case Action::Op::Pause: audio.pause_music(); break;
        case Action::Op::Resume: audio.resume_music(); break;
        }
        follow_piece(audio);
    }
    // Every frame the race's stream plays at the base level: the switch silences it at once.
    if (race_piece_ && c.menu == Menu::None) audio.set_stream_level(level_.base(c.music_on));
    ease_trim(audio);
}

void RaceMusic::follow_piece(AudioEngine& audio) {
    const u32 g = audio.bgm().generation();
    if (g == generation_) return;
    generation_ = g;
    const sound::MadsTrack* t = audio.bgm().current_track();
    // Ours: a race song without markers (one of the player's) gets the fallback ones, so the
    // replay cameras cut at the disc songs' pace rather than on their 2 s countdown.
    if (marked_request_ && audio.bgm().current() == bgm::kRace && (!t || !t->markers.any())) {
        const u64 seed = !history_.empty() ? history_[at_].id : 0;
        fallback_ = fallback_markers(audio.music_length(), seed);
        markers_.start(&fallback_);
        return;
    }
    markers_.start(marked_request_ && t ? &t->markers : nullptr);
}

void RaceMusic::step_markers(AudioEngine& audio) {
    follow_piece(audio);
    markers_.step(audio.music_audible_position());
}

bool FinishCue::update(const std::vector<bool>& finished, int followed) {
    for (size_t i = 0; i < finished.size(); ++i) {
        const bool was = i < last_.size() && last_[i];
        if (finished[i] && !was && int(i) == followed) heard_ = true;
    }
    last_ = finished;
    return heard_;
}

float OpponentEngines::attenuation(float d) { return std::min(1.0f, 32.0f / std::max(d, 0.25f)); }

float OpponentEngines::doppler(const sim::Vec3& dir, const sim::Vec3& v_src, const sim::Vec3& v_lis) {
    constexpr float c = 331.45f;
    // dir points from the listener to the source: a source moving towards the
    // listener has v_src . dir < 0.
    float vs = std::clamp(v_src.dot(dir), -c * 0.5f, c * 0.5f);
    float vl = std::clamp(v_lis.dot(dir), -c * 0.5f, c * 0.5f);
    return std::clamp((c + vl) / (c + vs), 0.25f, 3.99f);
}

void OpponentEngines::load(Vfs& vfs, const std::vector<Car>& cars) {
    engines_.clear();
    winds_.clear();
    cd_.clear();
    active_.assign(cars.size(), false);
    for (const Car& c : cars) {
        auto e = std::make_unique<EngineSound>();
        if (c.sound_id) e->load(vfs, c.sound_id, c.variant);
        engines_.push_back(std::move(e));
        auto w = std::make_unique<EngineUnit>();
        w->load(vfs, EngineUnit::kWindBank);
        winds_.push_back(std::move(w));
        cd_.push_back(c.cd);
    }
}

void OpponentEngines::update(VoiceMixer& mixer, const sim::Vec3& listener, const sim::Vec3& lv, const sim::Vec3& right,
                             const sim::Vec3& forward, const std::vector<const sim::VehicleState*>& states, double dt,
                             const std::vector<float>* slipstream) {
    size_t n = std::min(states.size(), engines_.size());
    std::vector<std::pair<float, size_t>> by_distance;
    for (size_t i = 0; i < n; ++i) by_distance.push_back({(states[i]->position - listener).length(), i});
    std::sort(by_distance.begin(), by_distance.end());
    std::vector<bool> want(n, false), wind(n, false);
    for (size_t k = 0; k < by_distance.size() && int(k) < max_voiced; ++k) want[by_distance[k].second] = true;
    for (size_t k = 0; k < by_distance.size() && int(k) < max_wind; ++k) wind[by_distance[k].second] = true;
    for (size_t i = 0; i < n; ++i) {
        EngineSound& e = *engines_[i];
        if (!want[i] || !e.loaded()) {
            if (active_[i]) {
                e.stop(mixer);
                if (i < winds_.size()) winds_[i]->stop(mixer);
            }
            active_[i] = false;
            continue;
        }
        const sim::VehicleState& s = *states[i];
        sim::Vec3 d = s.position - listener;
        float dist = d.length();
        sim::Vec3 dir = dist > 1e-3f ? d * (1.0f / dist) : sim::Vec3{0, 0, 1};
        e.set_view(view);
        e.set_position(d.dot(right), d.dot(forward)); // pan + front/back (surround cue)
        e.set_volume(attenuation(std::hypot(d.x, d.z)));
        float dop = doppler(dir, s.velocity, lv);
        e.update(mixer, s.engine_rpm, s.throttle, dt, dop);
        if (i < winds_.size() && !wind[i]) winds_[i]->stop(mixer);
        else if (i < winds_.size() && winds_[i]->loaded()) {
            SoundView sv = view == EngineSound::View::OnboardOther ? SoundView::InCarOther
                           : view == EngineSound::View::Replay      ? SoundView::Other
                                                                    : SoundView::ChaseOther;
            float y = slipstream && i < slipstream->size() ? (*slipstream)[i] : 0.0f;
            car_sound::Wind w = car_sound::wind(s.speed_kmh / 3.6f, y, cd_[i]);
            winds_[i]->update_at(mixer, float(w.rpm), sfx_gain(sv, kChWind) * attenuation(std::hypot(d.x, d.z)) * w.gain,
                                 d.dot(right), d.dot(forward), dop);
        }
        active_[i] = true;
    }
}

void OpponentEngines::stop(VoiceMixer& mixer) {
    for (size_t i = 0; i < engines_.size(); ++i) engines_[i]->stop(mixer);
    for (auto& w : winds_) w->stop(mixer);
    active_.assign(active_.size(), false);
}

} // namespace gt3r::race
