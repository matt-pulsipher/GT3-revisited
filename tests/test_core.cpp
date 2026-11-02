// Unit tests that need no game data, so they run in CI.

#include "test_framework.h"

#include "base/config.h"
#include "formats/gs_memory.h"
#include "formats/ps_adpcm.h"
#include "input/bindings.h"
#include "vfs/vfs.h"

#include <cstring>

using namespace gt3r;

TEST(gs_roundtrip) {
    // Uploading and sampling in the same format must be the identity, for
    // every format, including odd sizes and non-zero base pointers. Because
    // the rect spans several pages, this also catches address collisions.
    for (u32 psm : {0x00u, 0x02u, 0x13u, 0x14u}) {
        GsMemory gs;
        const u32 w = 200, h = 130, bw = 4, bp = 96;
        u32 bits = psm_bits(psm);
        Bytes src(size_t(w) * h * bits / 8);
        for (size_t i = 0; i < src.size(); ++i) src[i] = u8(i * 131 + 7);
        gs.upload(bp, bw, psm, w, h, src);
        bool ok = true;
        for (u32 y = 0; y < h && ok; ++y)
            for (u32 x = 0; x < w && ok; ++x) {
                size_t i = size_t(y) * w + x;
                u32 expect = 0;
                switch (bits) {
                case 32: std::memcpy(&expect, &src[i * 4], 4); break;
                case 16: std::memcpy(&expect, &src[i * 2], 2); break;
                case 8: expect = src[i]; break;
                case 4: expect = (src[i / 2] >> ((i & 1) * 4)) & 0xF; break;
                }
                ok = gs.read_texel(bp, bw, psm, x, y) == expect;
            }
        CHECK(ok);
    }
}

TEST(gs_cross_format) {
    // Known fact: 8-bit texel (0,0) of a page is byte 0 of 32-bit word 0, and
    // texel (8,0) is byte 2 of word 0 (columns interleave pairs of rows).
    GsMemory gs;
    u32 word = 0x44332211;
    gs.upload(0, 1, 0x00, 1, 1, ByteSpan(reinterpret_cast<u8*>(&word), 4));
    CHECK(gs.read_texel(0, 2, 0x13, 0, 0) == 0x11);
    CHECK(gs.read_texel(0, 2, 0x13, 8, 0) == 0x33);
    CHECK(gs.read_texel(0, 2, 0x13, 4, 2) == 0x22);
    CHECK(gs.read_texel(0, 2, 0x14, 0, 0) == 0x1);
    CHECK(gs.read_texel(0, 2, 0x14, 4, 2) == 0x1); // high nibble of byte 0 (row-pair swap)
}

TEST(gs_ct16_expand) {
    // A 16-bit texel's 5-bit channels become c << 3 with the low bits 0, as the GS reads them
    // (a 16-bit CLUT the same way): 31 -> 248, not 255.
    GsMemory gs;
    const u16 px[2] = {u16(31 | (16 << 5) | (1 << 10) | 0x8000), u16(23 << 5)};
    gs.upload(0, 1, 0x02, 2, 1, ByteSpan(reinterpret_cast<const u8*>(px), sizeof(px)));
    Tex0 t;
    t.psm = 0x02, t.tbw = 1, t.tcc = true;
    std::vector<u32> c = gs.decode_rgba(t, 2, 1);
    CHECK(c[0] == (248u | (128u << 8) | (8u << 16) | 0xFF000000u));
    CHECK(c[1] == (184u << 8));
}

TEST(tex0_decode) {
    // TEX0 from a real GT3 texture header: 256x256 T4, CBP 0x22, TCC on.
    Tex0 t = Tex0::decode(0x2000044621410000ull);
    CHECK(t.psm == 0x14);
    CHECK(t.tbw == 4);
    CHECK(t.width() == 256 && t.height() == 256);
    CHECK(t.cbp == 0x22);
    CHECK(t.tcc);
}

TEST(adpcm) {
    PsAdpcmDecoder d;
    s16 out[28];
    u8 frame[16] = {0x00, 0x00};
    frame[2] = 0x71; // samples 0 = 1, 1 = 7  (shift 0)
    d.decode_frame(frame, out);
    CHECK(out[0] == 4096);
    CHECK(out[1] == 28672);
    CHECK(out[2] == 0);
    // Filter 1 (60/64) carries history forward.
    u8 f2[16] = {0x10, 0x00};
    d.reset();
    d.hist1 = 6400;
    d.decode_frame(f2, out);
    CHECK(out[0] == (6400 * 60 + 32) / 64);
}

TEST(bindings) {
    auto bs = Binding::parse_line("Cross", "pad.righttrigger curve=1.5 deadzone=0.1 | pad.south | key.Return");
    CHECK(bs.size() == 3);
    CHECK(bs[0].target.kind == PadTarget::Kind::Button && bs[0].target.button == PadButton::Cross);
    CHECK(bs[0].source.kind == InputSource::Kind::PadAxis);
    CHECK(bs[0].options.curve == 1.5f && bs[0].options.deadzone == 0.1f);
    CHECK(bs[1].source.kind == InputSource::Kind::PadButton);
    CHECK(bs[2].source.kind == InputSource::Kind::Key);

    auto ax = Binding::parse_line("LX-", "joy.axis2+ pedal invert");
    CHECK(ax.size() == 1);
    CHECK(ax[0].target.kind == PadTarget::Kind::Axis && ax[0].target.half == InputSource::Half::Negative);
    CHECK(ax[0].source.kind == InputSource::Kind::JoyAxis && ax[0].source.code == 2);
    CHECK(ax[0].options.pedal && ax[0].options.invert);

    // Formatting must round-trip through the parser.
    for (const char* line : {"pad.leftx deadzone=0.08", "joy.hat0.left", "tilt+ curve=2", "key.Left_Shift ramp=0.2",
                             "pad.dpup", "mouse.right"}) {
        auto b = Binding::parse_line("L1", line);
        CHECK(b.size() == 1);
        if (b.size() != 1) continue;
        auto again = Binding::parse_line("L1", b[0].source_with_options());
        CHECK(again.size() == 1 && again[0].source == b[0].source);
    }
    CHECK(Binding::parse_line("Cross", "pad.nonsense").empty());
    CHECK(Binding::parse_line("NotAButton", "pad.south").empty());

    for (auto name : {"pad", "quest", "wheel", "keyboard"}) CHECK(!make_preset(name).bindings.empty());
}

namespace {
// The race-pad targets a source drives in a profile.
std::vector<std::string> targets_of(const Profile& p, std::string_view source) {
    std::vector<std::string> out;
    for (const Binding& b : p.bindings)
        if (b.source.to_string() == source) out.push_back(b.target.to_string());
    return out;
}
} // namespace

TEST(race_layout_presets) {
    using V = std::vector<std::string>;
    // The menu pad is the DualShock 2 1:1: triggers on L2/R2, Triangle on north, and the back
    // paddles shift (right R2, left L2), so they shift under the "gt3" race layout too.
    Profile menu = make_preset("pad");
    CHECK(targets_of(menu, "pad.righttrigger") == V{"R2"});
    CHECK(targets_of(menu, "pad.north") == V{"Triangle"});
    CHECK(targets_of(menu, "pad.paddle1") == V{"R2"} && targets_of(menu, "pad.paddle2") == V{"L2"});
    CHECK(targets_of(make_preset("quest"), "pad.paddle1") == V{"R2"});
    // Modern race layout (targets in GT3's race meaning): triggers throttle / brake, Cross /
    // Square shift, Triangle change view (never respawn), R3 look back, L1 reverse; the
    // paddles shift here too.
    Profile modern = make_race_preset("modern", "pad");
    CHECK(targets_of(modern, "pad.paddle1") == V{"R2"} && targets_of(modern, "pad.paddle2") == V{"L2"});
    CHECK(targets_of(modern, "pad.righttrigger") == V{"Cross"});
    CHECK(targets_of(modern, "pad.lefttrigger") == V{"Square"});
    CHECK(targets_of(modern, "pad.south") == V{"R2"});
    CHECK(targets_of(modern, "pad.west") == V{"L2"});
    CHECK(targets_of(modern, "pad.east") == V{"Circle"});
    CHECK(targets_of(modern, "pad.north") == V{"R1"});
    CHECK(targets_of(modern, "pad.rightstick") == V{"L1"});
    CHECK(targets_of(modern, "pad.leftshoulder") == V{"Triangle"});
    // Select has no race action (no respawn, as in GT3): nothing binds it.
    CHECK(targets_of(modern, "pad.back").empty());
    CHECK(targets_of(modern, "key.Backspace").empty());
    CHECK(targets_of(make_race_preset("modern", "wheel"), "joy.button8").empty());
    CHECK(targets_of(modern, "key.Escape") == V{"Start"});
    CHECK(targets_of(modern, "key.W") == V{"Cross"});
    // No developer options by default: nothing binds the performance overlay (R3).
    CHECK(targets_of(modern, "pad.leftstick").empty());
    CHECK(targets_of(modern, "key.G").empty());
    for (const Binding& b : modern.bindings) CHECK(b.target.kind != PadTarget::Kind::Button || b.target.button != PadButton::R3);
    // Every source drives one action only.
    for (const Binding& b : modern.bindings) CHECK(targets_of(modern, b.source.to_string()).size() == 1);
    CHECK(race_action_name(modern.bindings[0].target) == "Accelerate");
    // Wheels keep their pedals; the keyboard preset has no pad bindings.
    CHECK(targets_of(make_race_preset("modern", "wheel"), "joy.axis1") == V{"Cross"});
    CHECK(targets_of(make_race_preset("modern", "keyboard"), "pad.righttrigger").empty());
    // GT3's own layout: the race reads the menu pad itself.
    Profile gt3 = make_race_preset("gt3", "pad");
    CHECK(gt3.name == "gt3" && gt3.bindings.empty());
}

TEST(ini) {
    IniDocument d = IniDocument::parse("top = 1\n[a]\n x = hello world \n; comment\n[b]\ny=2\n");
    CHECK(d.get("", "top") == "1");
    CHECK(d.get("a", "x") == "hello world");
    CHECK(d.get("b", "y") == "2");
    d.set("a", "x", "changed");
    IniDocument again = IniDocument::parse(d.serialize());
    CHECK(again.get("a", "x") == "changed");
    // A section header may carry a trailing comment (docs/MODDING.md writes them that way).
    IniDocument c = IniDocument::parse("[paints]   ; in menu order\nRed = #FF0000\n[dealer] # GT Mode\nfor_sale = yes\n");
    CHECK(c.get("paints", "Red") == "#FF0000");
    CHECK(c.get("dealer", "for_sale") == "yes");
}

TEST(vfs_paths) {
    CHECK(normalize_vfs_path("menu\\\\JP//x.img") == "menu/JP/x.img");
    CHECK(normalize_vfs_path("/bgm/a.ads") == "bgm/a.ads");
}

