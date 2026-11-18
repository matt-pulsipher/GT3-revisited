// Sound commands: engine (ENGN) and instrument (INST) banks, the GTSE effect
// index, WAV export and offline renders through the voice mixer.

#include "tool.h"

#include "audio/engine_sound.h"
#include "audio/motor_sound.h"
#include "audio/sequencer.h"
#include "audio/voice_mixer.h"
#include "formats/png_writer.h"
#include "formats/sound/engn.h"
#include "formats/sound/inst.h"
#include "formats/sound/seq.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace gt3r::tool {

namespace {

using namespace gt3r::sound;

Bytes read_vol(Disc& disc, const std::string& path) { return disc.volume().read(need(disc.volume(), path)); }

// Sample once through, then the loop twice more (as the SPU plays it), so
// loop seams can be checked.
std::vector<s16> with_loops(const std::shared_ptr<const Bytes>& data, u32 offset) {
    return render_spu_loops(*data, offset, 2);
}

u32 pitch_rate(u32 pitch) { return u32(std::lround(game_pitch_rate(pitch))); }

int cmd_engn(Disc& disc, const Args& a) {
    EngineBank b = EngineBank::parse(read_vol(disc, a[0]));
    std::printf("%zu layers, %zu bytes ADPCM, unknown 0x28/0x2C = %u/%u\n", b.layers.size(), b.adpcm->size(),
                b.unknown_28, b.unknown_2c);
    std::printf("  #  base    lo     hi   vol   pitch   rate  offset   loop   seconds\n");
    for (size_t i = 0; i < b.layers.size(); ++i) {
        const EngineLayer& l = b.layers[i];
        SpuSample s = b.decode_layer(i);
        double rate = b.layer_rate(i);
        std::printf("%3zu %5u %5d %6d %5d  0x%04X %6.0f 0x%06X %6zu %7.3f\n", i, l.base_rpm, l.lo_rpm, l.hi_rpm,
                    l.volume, l.pitch, rate, l.offset, s.pcm.size() - s.loop_start,
                    (s.pcm.size() - s.loop_start) / rate);
    }
    if (a.size() > 1) {
        for (size_t i = 0; i < b.layers.size(); ++i) {
            char name[64];
            std::snprintf(name, sizeof(name), "/layer%02zu_%05urpm.wav", i, b.layers[i].base_rpm);
            if (!save(a[1] + name, encode_wav(pitch_rate(b.layers[i].pitch), 1, with_loops(b.adpcm, b.layers[i].offset))))
                return 1;
        }
    }
    return 0;
}

int cmd_ins(Disc& disc, const Args& a) {
    InstBank b = InstBank::parse(read_vol(disc, a[0]));
    std::printf("%zu regions, %zu programs, %zu bytes ADPCM\n", b.regions.size(), b.programs.size(), b.adpcm->size());
    std::printf("  #   addr    vol center keys    pan  pitch   rate  adsr1 adsr2 samples loop\n");
    for (size_t i = 0; i < b.regions.size(); ++i) {
        const InstRegion& r = b.regions[i];
        SpuSample s = b.decode_region(i);
        std::printf("%3zu 0x%06X %5d 0x%04X %3u-%-3u 0x%02X 0x%04X %6u 0x%04X 0x%04X %7zu %s\n", i, r.address,
                    r.volume, r.center, r.key_lo, r.key_hi, r.pan, r.pitch(), pitch_rate(r.pitch()), r.adsr1,
                    r.adsr2, s.pcm.size(), s.looped ? "yes" : "no");
    }
    for (size_t i = 0; i < b.programs.size(); ++i) {
        std::printf("program %zu (pan %u vol %d):", i, b.programs[i].pan, b.programs[i].volume);
        for (u8 r : b.programs[i].regions) std::printf(" %u", r);
        std::printf("\n");
    }
    if (a.size() > 1) {
        for (size_t i = 0; i < b.regions.size(); ++i) {
            char name[48];
            std::snprintf(name, sizeof(name), "/region%03zu.wav", i);
            if (!save(a[1] + name, encode_wav(pitch_rate(b.regions[i].pitch()), 1, with_loops(b.adpcm, b.regions[i].address))))
                return 1;
        }
    }
    return 0;
}

int cmd_se(Disc& disc, const Args&) {
    for (const SoundEffect& e : parse_se_index(read_vol(disc, "sound/se.inf")))
        std::printf("%-14s bank %u (%s) regions %u..%u\n", e.name.c_str(), e.bank,
                    se_bank_path(e.bank) ? se_bank_path(e.bank) : "?", e.first, e.last);
    return 0;
}

// Renders a named effect through the mixer (stereo, 48 kHz).
int cmd_se_wav(Disc& disc, const Args& a) {
    for (const SoundEffect& e : parse_se_index(read_vol(disc, "sound/se.inf"))) {
        if (e.name != a[0]) continue;
        if (!se_bank_path(e.bank)) {
            std::fprintf(stderr, "%s: unknown bank %u\n", e.name.c_str(), e.bank);
            return 1;
        }
        InstBank b = InstBank::parse(read_vol(disc, se_bank_path(e.bank)));
        VoiceMixer mixer;
        mixer.set_reverb(SpuReverb::StudioC, 0x0FFF, 0x0FFF); // the game's menu setting
        std::vector<VoiceId> ids;
        for (u32 r = e.first; r <= e.last && r < b.regions.size(); ++r) {
            const InstRegion& reg = b.regions[r];
            int l, rr;
            pan_gains(reg.pan, l, rr);
            VoiceParams p{b.adpcm, reg.address, reg.pitch(), s16((reg.volume * l) >> 14), s16((reg.volume * rr) >> 14),
                          reg.adsr1, reg.adsr2, reg.reverb(), reg.priority};
            ids.push_back(mixer.key_on(p));
        }
        double seconds = a.size() > 2 ? std::atof(a[2].c_str()) : 4.0;
        std::vector<s16> out(size_t(seconds * VoiceMixer::kRate) * 2);
        mixer.mix(out.data(), out.size() / 2);
        return save(a[1], encode_wav(VoiceMixer::kRate, 2, out)) ? 0 : 1;
    }
    std::fprintf(stderr, "no effect named %s\n", a[0].c_str());
    return 1;
}

// Offline engine render: idle, a throttle blip, a full-throttle sweep to the
// top layer, then a lift-off coast down.
int cmd_engine_wav(Disc& disc, const Args& a) {
    Vfs vfs(&disc.volume());
    EngineSound es;
    std::string err;
    if (a[0].rfind("ev", 0) == 0) { // "ev" or "ev:<whine order>": a synthesised mod-EV motor
        MotorSoundParams p;
        if (a[0].size() > 3) p.whine_order = float(std::atof(a[0].c_str() + 3));
        MotorBanks b = make_motor_banks(p);
        es.set_banks(b.whine, b.inverter);
    } else if (!es.load(vfs, std::atoi(a[0].c_str()), std::atoi(a[1].c_str()), &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    if (a.size() > 3) es.set_view(EngineSound::View(std::clamp(std::atoi(a[3].c_str()), 0, 4)));
    const auto* ref = es.engine_bank() ? es.engine_bank() : es.exhaust_bank();
    float idle = std::max(800.0f, float(ref->layers.front().base_rpm));
    float top = float(ref->layers.back().base_rpm) + 500.0f;
    VoiceMixer mixer;
    std::vector<s16> out;
    const double dt = 1.0 / 60.0;
    const size_t per_tick = VoiceMixer::kRate / 60;
    std::vector<s16> buf(per_tick * 2);
    double t = 0, total = 14.0;
    std::FILE* log = a.size() > 4 ? std::fopen(a[4].c_str(), "w") : nullptr;
    while (t < total) {
        float rpm, thr;
        if (t < 2) rpm = idle, thr = 0;                                            // idle
        else if (t < 3) thr = 1, rpm = idle + float(t - 2) * (top - idle) * 0.4f; // blip up
        else if (t < 4) thr = 0, rpm = idle + float(4 - t) * (top - idle) * 0.4f;  // fall back
        else if (t < 10) thr = 1, rpm = idle + float(t - 4) / 6.0f * (top - idle); // full sweep
        else thr = 0, rpm = top - float(t - 10) / 4.0f * (top - idle);             // coast down
        es.update(mixer, rpm, thr, dt);
        mixer.mix(buf.data(), per_tick);
        out.insert(out.end(), buf.begin(), buf.end());
        if (log) {
            const auto& st = es.state();
            std::fprintf(log, "%.4f %.0f %.2f %d %d %d %d %.3f %.3f %d %d %.3f %.3f\n", t, rpm, thr, st.level[0],
                         st.level[1], st.bank[0].layer[0], st.bank[0].layer[1], st.bank[0].gain[0],
                         st.bank[0].gain[1], st.bank[1].layer[0], st.bank[1].layer[1], st.bank[1].gain[0],
                         st.bank[1].gain[1]);
        }
        t += dt;
    }
    if (log) std::fclose(log);
    return save(a[2], encode_wav(VoiceMixer::kRate, 2, out)) ? 0 : 1;
}

// Parses every sound bank on the disc and reports coverage.
int cmd_sound_survey(Disc& disc, const Args&) {
    size_t engn = 0, engn_ok = 0, inst = 0, inst_ok = 0, layers = 0, regions = 0, other = 0;
    for (const auto& e : disc.volume().entries()) {
        if (e.path.rfind("sound/", 0) != 0 && e.path.rfind("music/", 0) != 0) continue;
        Bytes d = disc.volume().read(e);
        try {
            if (has_magic(d, "ENGN")) {
                ++engn;
                EngineBank b = EngineBank::parse(d);
                for (size_t i = 0; i < b.layers.size(); ++i) {
                    SpuSample s = b.decode_layer(i);
                    if (!s.looped || s.frames * 16 != b.layers[i].size) throw FormatError("layer is not one looped sample");
                }
                layers += b.layers.size();
                ++engn_ok;
            } else if (has_magic(d, "INST")) {
                ++inst;
                InstBank b = InstBank::parse(d);
                for (size_t i = 0; i < b.regions.size(); ++i)
                    if (b.decode_region(i).pcm.empty()) throw FormatError("empty region");
                regions += b.regions.size();
                ++inst_ok;
            } else if (has_magic(d, "GTSE")) {
                // An effect resolves when its bank is on the disc and holds its regions.
                std::vector<SoundEffect> effects = parse_se_index(d);
                std::map<u8, size_t> bank_regions;
                std::string unresolved;
                for (const SoundEffect& fx : effects) {
                    const char* path = se_bank_path(fx.bank);
                    if (path && !bank_regions.count(fx.bank))
                        bank_regions[fx.bank] = disc.volume().find(path) ? InstBank::parse(read_vol(disc, path)).regions.size() : 0;
                    if (!path || fx.last >= bank_regions[fx.bank]) unresolved += " " + fx.name;
                }
                std::printf("%s: %zu effects\n", e.path.c_str(), effects.size());
                if (!unresolved.empty()) std::printf("  unresolved:%s\n", unresolved.c_str());
            } else {
                ++other;
                std::printf("%s: not a bank (%.4s)\n", e.path.c_str(), reinterpret_cast<const char*>(d.data()));
            }
        } catch (const std::exception& ex) {
            std::printf("%s: FAILED %s\n", e.path.c_str(), ex.what());
        }
    }
    std::printf("ENGN %zu/%zu banks (%zu layers), INST %zu/%zu banks (%zu regions), %zu other files\n", engn_ok, engn,
                layers, inst_ok, inst, regions, other);
    return engn_ok == engn && inst_ok == inst ? 0 : 1;
}


// ---- Sequenced music ----------------------------------------------------

struct MusicData {
    std::vector<MusicEntry> index;
    std::shared_ptr<const SeqBank> seqs;
    std::shared_ptr<const InstBank> bank;
};

MusicData load_music(Disc& disc) {
    MusicData m;
    m.index = parse_music_index(read_vol(disc, "music/music.inf"));
    m.seqs = std::make_shared<const SeqBank>(SeqBank::parse(read_vol(disc, "music/music.seq")));
    m.bank = std::make_shared<const InstBank>(InstBank::parse(read_vol(disc, "music/music.ins")));
    return m;
}

std::string seq_names(const MusicData& m, size_t seq) {
    std::string names;
    for (const MusicEntry& e : m.index)
        if (e.sequence == seq) names += (names.empty() ? "" : " ") + e.name;
    return names;
}

std::string seq_title(const MusicData& m, size_t seq) {
    for (const MusicEntry& e : m.index)
        if (e.sequence == seq) return e.title;
    return "?";
}

std::string fmt_time(double s) {
    char t[32];
    std::snprintf(t, sizeof(t), "%d:%05.2f", int(s) / 60, s - 60 * int(s / 60));
    return t;
}

void print_event(const SeqTimedEvent& te) {
    const SeqEvent& e = te.ev;
    std::printf("  %7u  @%06X  %-8s", te.tick, te.offset, seq_op_name(e.op));
    switch (e.op) {
    case SeqOp::Note: std::printf(" key %3u vel %3u len %u", e.key, e.velocity, e.length); break;
    case SeqOp::PitchBend: std::printf(" %+d", e.bend); break;
    case SeqOp::LoopStart:
        if (e.value == kSeqLoopEndless) std::printf(" endless");
        else std::printf(" x%u", e.value);
        break;
    case SeqOp::Program:
    case SeqOp::Volume:
    case SeqOp::Pan:
    case SeqOp::Nop: std::printf(" %u", e.value); break;
    case SeqOp::Tempo: std::printf(" %u", e.tempo); break;
    default: break;
    }
    std::printf("\n");
}

// Lists the sequenced music; with a sequence (and track) number, dumps events.
int cmd_seq(Disc& disc, const Args& a) {
    MusicData m = load_music(disc);
    if (a.empty()) {
        std::printf("music.inf: %zu entries\n", m.index.size());
        for (const MusicEntry& e : m.index)
            std::printf("  group %2u  %-12s -> sequence %u  \"%s\" (%s, %s)\n", e.group, e.name.c_str(), e.sequence,
                        e.title.c_str(), e.artist.c_str(), e.file.c_str());
        std::printf("music.seq: %zu sequences\n", m.seqs->sequences.size());
        std::printf("  #  tempo     bpm   length   loop at   notes tracks  title / names\n");
        for (size_t i = 0; i < m.seqs->sequences.size(); ++i) {
            const Sequence& s = m.seqs->sequences[i];
            SeqSummary sum = summarize_sequence(*m.seqs, i);
            double len = seq_ticks_to_seconds(sum.length_ticks, s.tempo);
            double loop = sum.loop_start_tick >= 0 ? seq_ticks_to_seconds(u64(sum.loop_start_tick), s.tempo) : -1;
            std::printf("  %u %8u %6.2f %9s %9s %6u %4d    %s / %s\n", unsigned(i), s.tempo, 60e6 / s.tempo,
                        fmt_time(len).c_str(), loop >= 0 ? fmt_time(loop).c_str() : "-", sum.notes, sum.tracks_used,
                        seq_title(m, i).c_str(), seq_names(m, i).c_str());
        }
        return 0;
    }
    size_t i = size_t(std::atoi(a[0].c_str()));
    const Sequence& s = m.seqs->sequences.at(i);
    std::printf("sequence %zu: volume 0x%X tempo %u\n", i, s.volume, s.tempo);
    int only = a.size() > 1 ? std::atoi(a[1].c_str()) : -1;
    for (int t = 0; t < kSeqTracks; ++t) {
        if (!s.tracks[size_t(t)] || (only >= 0 && t != only)) continue;
        auto evs = decode_seq_track(*m.seqs->data, s.tracks[size_t(t)]);
        std::printf("track %d @0x%X: %zu events\n", t, s.tracks[size_t(t)], evs.size());
        if (only < 0) continue;
        for (const auto& te : evs) print_event(te);
    }
    return 0;
}

// Renders a sequence (intro + loops + release/reverb tail) to a WAV.
int cmd_seq_wav(Disc& disc, const Args& a) {
    MusicData m = load_music(disc);
    int index = std::atoi(a[0].c_str());
    SequenceRenderOptions opt;
    for (size_t i = 2; i < a.size(); ++i) {
        if (a[i] == "--cubic") opt.interpolation = VoiceMixer::Interpolation::Cubic;
        else if (a[i] == "--dry") opt.reverb_mode = SpuReverb::Off;
        else if (a[i].rfind("--reverb=", 0) == 0) opt.reverb_mode = std::atoi(a[i].c_str() + 9);
        else if (a[i].rfind("--depth=", 0) == 0) opt.reverb_depth = s16(std::strtol(a[i].c_str() + 8, nullptr, 0));
        else if (a[i].rfind("--loops=", 0) == 0) opt.loops = std::atoi(a[i].c_str() + 8);
        else if (a[i].rfind("--rate=", 0) == 0) opt.update_rate = std::atoi(a[i].c_str() + 7);
        else if (a[i].rfind("--trim=", 0) == 0) opt.master = float(std::atof(a[i].c_str() + 7));
        else {
            std::fprintf(stderr, "unknown option %s\n", a[i].c_str());
            return 1;
        }
    }
    SequenceRender r = render_sequence(m.bank, m.seqs, index, opt);
    if (r.pcm.empty()) {
        std::fprintf(stderr, "no sequence %d\n", index);
        return 1;
    }
    const double rate = VoiceMixer::kRate;
    std::printf("sequence %d \"%s\": %s rendered, stopped at %s, %d voices max, peak %d (%.1f dBFS), %zu clipped\n",
                index, seq_title(m, size_t(index)).c_str(), fmt_time(r.pcm.size() / 2 / rate).c_str(),
                fmt_time(r.end_frame / rate).c_str(), r.max_voices, r.peak, 20 * std::log10(std::max(1, r.peak) / 32768.0),
                r.clipped);
    for (size_t i = 0; i < r.loop_frames.size(); ++i)
        std::printf("  loop end %zu at %s (frame %llu)\n", i + 1, fmt_time(r.loop_frames[i] / rate).c_str(),
                    (unsigned long long)r.loop_frames[i]);
    return save(a[1], encode_wav(VoiceMixer::kRate, 2, r.pcm)) ? 0 : 1;
}

// Decodes every sequence and checks it against the instrument bank.
int cmd_seq_survey(Disc& disc, const Args&) {
    MusicData m = load_music(disc);
    const InstBank& b = *m.bank;
    size_t tracks = 0, events = 0, notes = 0, unmapped = 0, bad_program = 0, nops = 0, unbalanced = 0;
    std::map<std::string, size_t> ops;
    std::set<int> programs_used;
    int max_vel = 0, max_bend_range = 0;
    for (size_t i = 0; i < m.seqs->sequences.size(); ++i) {
        const Sequence& s = m.seqs->sequences[i];
        for (int t = 0; t < kSeqTracks; ++t) {
            if (!s.tracks[size_t(t)]) continue;
            ++tracks;
            auto evs = decode_seq_track(*m.seqs->data, s.tracks[size_t(t)]);
            int program = 0, depth = 0;
            for (const auto& te : evs) {
                const SeqEvent& e = te.ev;
                ++events;
                ++ops[seq_op_name(e.op)];
                if (e.op == SeqOp::Program) {
                    program = e.value;
                    programs_used.insert(program);
                    if (program >= int(b.programs.size())) ++bad_program;
                } else if (e.op == SeqOp::LoopStart) {
                    ++depth;
                } else if (e.op == SeqOp::LoopEnd) {
                    if (--depth < 0) ++unbalanced;
                } else if (e.op == SeqOp::Nop) {
                    ++nops;
                } else if (e.op == SeqOp::Note) {
                    ++notes;
                    max_vel = std::max<int>(max_vel, e.velocity);
                    bool hit = false;
                    if (program < int(b.programs.size()))
                        for (u8 r : b.programs[size_t(program)].regions) {
                            const InstRegion& reg = b.regions[r];
                            hit |= e.key >= reg.key_lo && e.key <= reg.key_hi;
                            max_bend_range = std::max<int>(max_bend_range, std::max(reg.bend_down, reg.bend_up));
                        }
                    if (!hit) ++unmapped;
                }
            }
            if (depth > 0) ++unbalanced;
        }
    }
    std::printf("%zu sequences, %zu tracks, %zu events, %zu notes\n", m.seqs->sequences.size(), tracks, events, notes);
    for (const auto& [name, n] : ops) std::printf("  %-9s %zu\n", name.c_str(), n);
    std::printf("programs used: %zu of %zu; out of range: %zu\n", programs_used.size(), b.programs.size(), bad_program);
    std::printf("notes with no region for their key: %zu; max velocity %d; max bend range %d semitones\n", unmapped,
                max_vel, max_bend_range);
    std::printf("undefined event bytes: %zu; unbalanced loops: %zu\n", nops, unbalanced);
    return bad_program || unbalanced ? 1 : 0;
}

} // namespace

GT3TOOL_COMMAND(engn, "engn", "<vol-path> [outdir]", "list an ENGN engine bank; export layers as WAV", 1, cmd_engn);
GT3TOOL_COMMAND(ins, "ins", "<vol-path> [outdir]", "list an INST bank; export regions as WAV", 1, cmd_ins);
GT3TOOL_COMMAND(se, "se", "", "list named sound effects (sound/se.inf)", 0, cmd_se);
GT3TOOL_COMMAND(se_wav, "se-wav", "<name> <out.wav> [seconds]", "render a named sound effect", 2, cmd_se_wav);
GT3TOOL_COMMAND(engine_wav, "engine-wav", "<sound-id|ev[:order]> <variant 0-7> <out.wav> [view 0-4] [log.txt]",
                "render an engine rev sequence through the voice mixer", 3, cmd_engine_wav);
GT3TOOL_COMMAND(seq, "seq", "[sequence [track]]", "list the sequenced music; dump a track's events", 0, cmd_seq);
GT3TOOL_COMMAND(seq_wav, "seq-wav",
                "<sequence> <out.wav> [--loops=N] [--dry] [--reverb=MODE] [--depth=EVOL] [--cubic] [--rate=HZ]",
                "render a sequenced music track through the sequencer and voice mixer", 2, cmd_seq_wav);
GT3TOOL_COMMAND(seq_survey, "seq-survey", "", "decode every sequence and check it against music.ins", 0,
                cmd_seq_survey);
GT3TOOL_COMMAND(sound_survey, "sound-survey", "", "parse every sound bank and report coverage", 0, cmd_sound_survey);

} // namespace gt3r::tool
