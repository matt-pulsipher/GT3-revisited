// Asset format variants of other games on the engine (Gran Turismo Concept): AI lines without
// a build stamp, course files without a scenery model, skinned GTM1 packets, string tables with
// more language pools, the Korean and Chinese cell fonts and the fourth effect bank. The first
// tests need no disc and use INVENTED data; the concept_assets_disc test takes
// GT3R_DISC_CONCEPT (disc images separated by ';').

#include "test_framework.h"

#include "disc/disc.h"
#include "formats/course/ai_lines.h"
#include "formats/course/course_file.h"
#include "formats/model/gtm1.h"
#include "formats/sound/inst.h"
#include "formats/text/charset.h"
#include "formats/text/fnt.h"
#include "formats/text/mltx.h"
#include "track/track.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <sstream>

using namespace gt3r;

namespace {

void put16(Bytes& b, size_t o, u16 v) {
    if (b.size() < o + 2) b.resize(o + 2);
    std::memcpy(&b[o], &v, 2);
}
void put32(Bytes& b, size_t o, u32 v) {
    if (b.size() < o + 4) b.resize(o + 4);
    std::memcpy(&b[o], &v, 4);
}
void putf(Bytes& b, size_t o, float v) {
    if (b.size() < o + 4) b.resize(o + 4);
    std::memcpy(&b[o], &v, 4);
}

// An AI line section of `version` with one line of `points` control points (x = 4096 k).
Bytes ai_section(u32 version, u32 points) {
    const u32 first = version == 8 ? 0x48 : 0x28;
    Bytes b(first + 4 + 40 * size_t(points), 0);
    put32(b, 0, u32(b.size()));
    put32(b, 4, version);
    put32(b, 0x0C, first);
    if (version == 8) std::memcpy(&b[0x28], "build 01/02/03 04:05:06\n", 24);
    put32(b, first, points);
    for (u32 k = 0; k < points; ++k) put32(b, first + 4 + 40 * size_t(k) + 4, 4096 * k);
    return b;
}

// An MLTX table of one key ("K") with `pools` pools of 4 bytes: pool p holds the letter 'a' + p.
Bytes mltx_with_pools(int pools) {
    Bytes b(0x20 + 4 * size_t(pools), 0);
    std::memcpy(b.data(), "MLTX", 4);
    put32(b, 4, 2);
    put32(b, 8, 0x20);  // pool
    put32(b, 12, 1);    // count
    put32(b, 16, 4);    // pool size
    put32(b, 0x14, 0x1C), put32(b, 0x18, 0);
    b[0x1C] = 'K';
    for (int p = 0; p < pools; ++p) b[0x20 + 4 * size_t(p)] = u8('a' + p);
    return b;
}

std::vector<std::string> concept_discs() {
    std::vector<std::string> out;
    const char* list = std::getenv("GT3R_DISC_CONCEPT");
    if (!list) return out;
    std::istringstream in(list);
    for (std::string path; std::getline(in, path, ';');)
        if (!path.empty()) out.push_back(path);
    return out;
}

} // namespace

// Version 7 of the AI line section has no build stamp: its first line starts where version 8's stamp is.
TEST(course_ai_lines_versions) {
    course::AiLines v8 = course::AiLines::parse(ai_section(8, 5));
    CHECK(v8.build == "build 01/02/03 04:05:06" && v8.lines.size() == 7 && v8.lines[0].points.size() == 5);
    course::AiLines v7 = course::AiLines::parse(ai_section(7, 5));
    CHECK(v7.build.empty() && v7.lines.size() == 7 && v7.lines[0].points.size() == 5);
    if (v7.lines[0].points.size() == 5) CHECK(v7.lines[0].points[3].pos.x == -3.0f && v7.lines[0].points[3].raw_x == 3 * 4096);
    CHECK(v7.lines[1].points.empty());
    bool threw = false;
    try {
        course::AiLines::parse(ai_section(6, 5));
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
    // A line that runs past the section is still refused.
    Bytes cut = ai_section(7, 5);
    cut.resize(cut.size() - 8);
    put32(cut, 0, u32(cut.size()));
    threw = false;
    try {
        course::AiLines::parse(cut);
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

// A file with a course's offset table but no scenery model in slot 1 parses as sections and is not a course.
TEST(course_file_without_scenery_is_not_a_course) {
    Bytes b(course::kSlotCount * 4 + 0x80, 0);
    put32(b, size_t(course::kSlotSceneryVisMain) * 4, course::kSlotCount * 4);
    std::memcpy(&b[course::kSlotCount * 4], "Vls0", 4);
    put32(b, size_t(course::kSlotAiLines) * 4, course::kSlotCount * 4 + 0x40);
    CHECK(!course::CourseFile::looks_like_course(b));
    course::CourseFile f = course::CourseFile::parse(b);
    CHECK(f.sections().size() == 2 && f.find(course::kSlotScenery) == nullptr && f.find(course::kSlotRoad) == nullptr);
    CHECK(f.sections()[0].tag == "Vls0" && f.sections()[0].size == 0x40);
    bool threw = false;
    try {
        track::Track::from_course_file(b, "x");
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
    // With a model in slot 1 it is one.
    Bytes c(course::kSlotCount * 4 + 0x40, 0);
    put32(c, size_t(course::kSlotScenery) * 4, course::kSlotCount * 4);
    std::memcpy(&c[course::kSlotCount * 4], "GTM1", 4);
    CHECK(course::CourseFile::looks_like_course(c));
}

// A skinned packet: GIF tag, strip lengths and S,T in the VIF data, then the vertex block with
// positions, normals and four bone weights per vertex, in groups that share their bones.
TEST(gtm1_skinned_packet_synthetic) {
    auto build = [](u16 normal_address) {
        Bytes b(0x40, 0); // the packet starts at 0x40
        auto word = [&](u32 v) { put32(b, b.size(), v); };
        word(0x6801C0C0); // V3-32 x1 @0xC0: the GIF tag's first 12 bytes
        word(0x00008004), word(0x30004000 | (0x5Cu << 15)), word(0x00000412);
        word(0x6202C040); // S-8 x2 @0x40: one strip of 4 vertices
        b.push_back(1), b.push_back(6), b.push_back(0), b.push_back(0);
        word(0x6404C040); // V2-32 x4 @0x40: S,T
        for (int i = 0; i < 4; ++i) putf(b, b.size(), 0.25f * float(i)), putf(b, b.size(), 1.0f - 0.25f * float(i));
        while (b.size() % 16) word(0);
        const u32 qwc = u32(b.size() - 0x40) / 16;
        // The vertex block: 3 vertices on bones (1, 2), then 1 on bone 3.
        put16(b, b.size(), 4), put16(b, b.size(), normal_address);
        auto group = [&](u16 count, bool more, std::array<u8, 4> bones, int first) {
            put16(b, b.size(), u16(count | (more ? 0x8000 : 0))), put16(b, b.size(), 4);
            for (u8 bone : bones) b.push_back(bone);
            for (int i = first; i < first + count; ++i) {
                putf(b, b.size(), float(i)), putf(b, b.size(), 2.0f), putf(b, b.size(), -float(i)); // position
                putf(b, b.size(), 0), putf(b, b.size(), 1), putf(b, b.size(), 0);                   // normal
                putf(b, b.size(), 0.75f), putf(b, b.size(), 0.25f), putf(b, b.size(), 0), putf(b, b.size(), 0);
            }
        };
        group(3, true, {1, 2, 0, 0}, 0);
        group(1, false, {3, 0, 0, 0}, 3);
        return std::make_pair(b, qwc);
    };
    {
        auto [bytes, qwc] = build(0x80);
        gtm1::Gtm1 g;
        g.data = bytes;
        gtm1::DecodedPacket p = g.decode_packet(gtm1::Packet{0x40, qwc, 0});
        CHECK(p.vertices.size() == 4 && p.skin.size() == 4 && p.strips == std::vector<u32>{4});
        CHECK(p.has_normals && p.has_uv && !p.env_normals && !p.has_colors);
        if (p.vertices.size() == 4 && p.skin.size() == 4) {
            CHECK(p.vertices[2].pos[0] == 2.0f && p.vertices[2].pos[1] == 2.0f && p.vertices[2].pos[2] == -2.0f);
            CHECK(p.vertices[3].normal[1] == 1.0f && p.vertices[1].uv[0] == 0.25f && p.vertices[1].uv[1] == 0.75f);
            CHECK(p.skin[0].bones[0] == 1 && p.skin[2].bones[1] == 2 && p.skin[3].bones[0] == 3);
            CHECK(p.skin[1].weights[0] == 0.75f && p.skin[1].weights[1] == 0.25f);
        }
    }
    {
        // The reflection pass: the normals go to the UV slot.
        auto [bytes, qwc] = build(0x40);
        gtm1::Gtm1 g;
        g.data = bytes;
        gtm1::DecodedPacket p = g.decode_packet(gtm1::Packet{0x40, qwc, 0});
        CHECK(p.vertices.size() == 4 && p.env_normals && !p.has_normals);
    }
    {
        // Without the vertex block (another count there) the packet has no positions: an error as before.
        auto [bytes, qwc] = build(0x80);
        put16(bytes, 0x40 + size_t(qwc) * 16, 5);
        gtm1::Gtm1 g;
        g.data = bytes;
        bool threw = false;
        try {
            g.decode_packet(gtm1::Packet{0x40, qwc, 0});
        } catch (const FormatError&) {
            threw = true;
        }
        CHECK(threw);
    }
}

// A file's pool count tells the order of its pools after SP (formats/text/mltx.h).
TEST(text_mltx_pool_orders) {
    using namespace gt3r::text;
    auto value = [](const StringTable& t, Language l) { return t.raw(0, l); };
    StringTable seven = StringTable::parse(mltx_with_pools(7));
    CHECK(seven.language_count() == 7 && seven.languages().size() == 7 && seven.languages().back() == Language::SP);
    CHECK(value(seven, Language::SP) == "g" && !seven.has(Language::KR) && value(seven, Language::KR).empty());
    CHECK(seven.filled(Language::TW) == 0);

    StringTable nine = StringTable::parse(mltx_with_pools(9));
    CHECK(nine.language_count() == 9);
    CHECK(value(nine, Language::SP) == "g" && value(nine, Language::KR) == "h" && value(nine, Language::TW) == "i");
    CHECK(!nine.has(Language::PT) && !nine.has(Language::CN) && value(nine, Language::PT).empty());
    CHECK(nine.filled(Language::KR) == 1 && nine.filled(Language::PT) == 0);

    StringTable eleven = StringTable::parse(mltx_with_pools(11));
    CHECK(eleven.language_count() == 11);
    CHECK(value(eleven, Language::PT) == "h" && value(eleven, Language::KR) == "i" && value(eleven, Language::CN) == "j" &&
          value(eleven, Language::TW) == "k");
    // More pools than languages: the rest is not read.
    CHECK(StringTable::parse(mltx_with_pools(13)).language_count() == 11);

    CHECK(std::string(language_code(Language::KR)) == "KR" && language_from_code("TW") == Language::TW &&
          language_from_code("PT") == Language::PT && !language_from_code("XX"));
    CHECK(int(Language::SP) == 6 && int(Language::PT) == 7 && int(Language::TW) == 10 && kLanguageCount == 7);
}

// TW text: Big5's characters in Big5 order, 94 to a row from 0x2121.
TEST(text_tw_codes) {
    using namespace gt3r::text;
    CHECK(tw_code_to_big5(0x2121) == 0xA140 && big5_to_tw_code(0xA140) == 0x2121);
    CHECK(tw_code_to_big5(0x5963) == 0xC2F7 && big5_to_tw_code(0xC2F7) == 0x5963);
    CHECK(big5_to_tw_code(0xA17E) == 0x215F && big5_to_tw_code(0xA1A1) == 0x2160); // the trail byte's gap
    CHECK(big5_to_tw_code(0xA17F) == 0 && big5_to_tw_code(0xA0A1) == 0 && big5_to_tw_code(0x4141) == 0);
    CHECK(tw_code_to_big5(0x2020) == 0 && tw_code_to_big5(0x7F21) == 0);
    int round = 0, codes = 0;
    for (u32 row = 0x21; row <= 0x7E; ++row)
        for (u32 col = 0x21; col <= 0x7E; ++col, ++codes)
            round += big5_to_tw_code(tw_code_to_big5(u16(row << 8 | col))) == (row << 8 | col);
    CHECK(round == codes);
    CHECK(std::string(kanji_font_path(Language::JP)) == "font/system.kf" && std::string(kanji_font_path(Language::US)) == "font/system.kf");
    CHECK(std::string(kanji_font_path(Language::KR)) == "font/system.hf" && std::string(kanji_font_path(Language::TW)) == "font/system.tf");
}

TEST(sound_effect_banks) {
    CHECK(std::string(sound::se_bank_path(0)) == "sound/gt3sys.ins" && std::string(sound::se_bank_path(2)) == "sound/gt3se02.ins");
    CHECK(std::string(sound::se_bank_path(3)) == "sound/gt3pod.ins" && sound::se_bank_path(4) == nullptr);
}

// The discs' own files: every course-folder file is a course or told apart from one, the rider
// models decode, the string table's Korean and Chinese pools are what their fonts draw, and
// every named effect resolves to audible data.
TEST(concept_assets_disc) {
    const std::vector<std::string> discs = concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    for (const std::string& path : discs) {
        std::printf("  %s\n", path.c_str());
        std::unique_ptr<Disc> disc;
        try {
            disc = Disc::open(path);
        } catch (const std::exception& e) {
            std::printf("    %s\n", e.what());
        }
        CHECK(disc != nullptr);
        if (!disc) continue;
        Volume& vol = disc->volume();

        // Courses: a file with a course's table but no scenery model is not a course; its AI lines still read.
        size_t stubs = 0;
        for (const auto& e : vol.entries()) {
            if (e.path.rfind("courses/", 0) != 0 || e.path.back() == '/' || e.path.find("/.") != std::string::npos) continue;
            if (e.size > 200000) continue; // (courses are megabytes)
            Bytes d = vol.read(e);
            CHECK(!course::CourseFile::looks_like_course(d));
            course::CourseFile f = course::CourseFile::parse(d);
            CHECK(f.find(course::kSlotScenery) == nullptr && f.find(course::kSlotRoad) == nullptr);
            ByteSpan ai = f.section(d, course::kSlotAiLines);
            CHECK(!ai.empty());
            if (!ai.empty()) {
                course::AiLines lines = course::AiLines::parse(ai);
                CHECK(lines.build.empty() && lines.lines.size() == 7 && lines.lines[0].points.size() > 10);
            }
            ++stubs;
        }
        std::printf("    %zu course-folder files that are not courses\n", stubs);

        // Skinned rider models.
        size_t riders = 0;
        for (const auto& e : vol.entries()) {
            if (e.path.rfind("bike/", 0) != 0 || e.path.size() < 4 || e.path.compare(e.path.size() - 4, 4, ".mdl") != 0) continue;
            Bytes d = vol.read(e);
            gtm1::Gtm1 g = gtm1::Gtm1::parse(d);
            size_t vertices = 0, skinned = 0, bad_weights = 0;
            int max_bone = -1;
            for (const gtm1::Chunk& c : g.chunks) {
                size_t chunk_vertices = 0;
                for (const gtm1::Packet& pk : c.packets) {
                    gtm1::DecodedPacket p = g.decode_packet(pk);
                    chunk_vertices += p.vertices.size(), skinned += p.skin.size();
                    for (const gtm1::SkinWeights& w : p.skin) {
                        float sum = w.weights[0] + w.weights[1] + w.weights[2] + w.weights[3];
                        // (a vertex on more than four bones keeps its four largest weights: sums from 0.9)
                        bad_weights += sum > 1.01f || sum < 0.85f;
                        for (int k = 0; k < 4; ++k)
                            if (w.weights[k] > 0) max_bone = std::max(max_bone, int(w.bones[k]));
                    }
                }
                CHECK(chunk_vertices == c.vertex_count);
                vertices += chunk_vertices;
            }
            CHECK(vertices > 0);
            if (!g.inverse_bind.empty()) {
                ++riders;
                CHECK(skinned == vertices && max_bone + 1 == int(g.inverse_bind.size()) && bad_weights == 0);
                scene::Model m = gtm1::decode_gtm1(d);
                // The stored pose stands upright with its arms out: taller than deep, as wide as tall.
                CHECK(m.bounds_max[1] - m.bounds_min[1] > 1.4f && m.bounds_max[0] - m.bounds_min[0] > 1.4f &&
                      m.bounds_max[2] - m.bounds_min[2] < 0.5f);
            } else {
                CHECK(skinned == 0);
            }
        }
        std::printf("    %zu skinned models in bike/\n", riders);

        // The string table and the fonts of its Korean and Chinese pools.
        text::StringTable st = text::StringTable::parse(vol.read(*vol.find("text/realtime.dat")));
        std::printf("    text/realtime.dat: %d pools\n", st.language_count());
        for (text::Language l : {text::Language::KR, text::Language::TW}) {
            if (!st.has(l)) continue;
            const Volume::Entry* fe = vol.find(text::kanji_font_path(l));
            CHECK(fe != nullptr && st.filled(l) + 5 > st.size());
            if (!fe) continue;
            text::KanjiFont font = text::KanjiFont::parse(vol.read(*fe));
            size_t pairs = 0, drawn = 0, big5 = 0;
            for (size_t i = 0; i < st.size(); ++i) {
                const std::string& raw = st.raw(i, l);
                for (size_t k = 0; k + 1 < raw.size(); ++k) {
                    if (u8(raw[k]) < 0x80) continue;
                    if (u8(raw[k]) >= 0xA1) {
                        u16 code = u16((u8(raw[k]) & 0x7F) << 8 | (u8(raw[k + 1]) & 0x7F));
                        ++pairs, drawn += font.has(code), big5 += text::tw_code_to_big5(code) != 0;
                    }
                    ++k;
                }
            }
            std::printf("    %s: %zu double-byte characters, %zu with a glyph in %s\n", text::language_code(l), pairs, drawn,
                        text::kanji_font_path(l));
            // (the later discs' Korean pool has one code five times that is not a KS X 1001 character)
            CHECK(pairs > 500 && drawn + 8 > pairs);
            if (l == text::Language::TW) CHECK(big5 == pairs);
        }
        if (st.has(text::Language::PT)) CHECK(st.filled(text::Language::PT) > 100); // Latin text
        // The other languages' pools are where GT3 has them.
        int brake = st.find("DATABASEPARTS_PTEXT_BRAKE");
        CHECK(brake >= 0);
        if (brake >= 0) CHECK(st.raw(size_t(brake), text::Language::US).rfind("Brake", 0) == 0);

        // Every named effect has its bank and regions; the fourth bank's effects are audible.
        std::vector<sound::SoundEffect> effects = sound::parse_se_index(vol.read(*vol.find("sound/se.inf")));
        size_t resolved = 0, pod = 0;
        for (const sound::SoundEffect& fx : effects) {
            const char* bank_path = sound::se_bank_path(fx.bank);
            const Volume::Entry* be = bank_path ? vol.find(bank_path) : nullptr;
            if (!be) continue;
            sound::InstBank bank = sound::InstBank::parse(vol.read(*be));
            if (fx.last >= bank.regions.size()) continue;
            ++resolved;
            if (fx.bank != 3) continue;
            ++pod;
            sound::SpuSample s = bank.decode_region(fx.first);
            s64 energy = 0;
            for (s16 v : s.pcm) energy += s64(v) * v;
            CHECK(s.pcm.size() > 1000 && energy / s64(s.pcm.size()) > 100 * 100);
        }
        std::printf("    sound/se.inf: %zu effects, %zu resolved, %zu in bank 3\n", effects.size(), resolved, pod);
        CHECK(resolved == effects.size() && pod > 0);
    }
}
