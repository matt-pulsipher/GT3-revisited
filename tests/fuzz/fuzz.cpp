// Fuzz targets for every reader that sees untrusted bytes: files in downloaded mod car
// packages (PNG, glTF, JSON, car definitions, the package container) and network input
// (game datagram payloads, car descriptors, rendezvous messages, version strings and the relay's
// versions file). docs/dev/ONLINE.md "Fuzzing".
//
// Build in a separate tree with sanitizers:
//   cmake -S . -B build-fuzz -DGT3R_FUZZ=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
//         -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" \
//         -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
//   cmake --build build-fuzz --target gt3r_fuzz
//   build-fuzz/gt3r_fuzz all 60          # every target, 60 s each (built-in mutation fuzzer)
//   build-fuzz/gt3r_fuzz glb 600 crash.bin  # one target; extra files are replayed first
//
// With clang's libFuzzer (Linux): add -DGT3R_LIBFUZZER=ON and run
//   GT3R_FUZZ_TARGET=glb build-fuzz/gt3r_fuzz corpus/  (coverage guided)

#include "formats/json.h"
#include "formats/model/gltf.h"
#include "formats/png_reader.h"
#include "formats/png_writer.h"
#include "game/data/mod_cars.h"
#include "game/mod_package.h"
#include "game/net_car.h"
#include "net/protocol.h"
#include "net/relay_server.h"
#include "net/rendezvous.h"
#include "net/session.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace gt3r;

namespace {

struct Target {
    const char* name;
    std::function<void(ByteSpan)> run;
    std::function<std::vector<Bytes>()> seeds;
};

Bytes text(const std::string& s) { return Bytes(s.begin(), s.end()); }

Bytes tiny_glb() {
    std::vector<float> pos = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::vector<u16> idx = {0, 1, 2, 0};
    Bytes png = encode_png(4, 4, std::vector<u32>(16, 0xFF0000FFu));
    Bytes bin(pos.size() * 4 + idx.size() * 2);
    std::memcpy(bin.data(), pos.data(), pos.size() * 4);
    std::memcpy(bin.data() + pos.size() * 4, idx.data(), idx.size() * 2);
    bin.insert(bin.end(), png.begin(), png.end());
    while (bin.size() % 4) bin.push_back(0);
    std::string json =
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":)" + std::to_string(bin.size()) +
        R"(}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6},)"
        R"({"buffer":0,"byteOffset":44,"byteLength":)" + std::to_string(png.size()) +
        R"(}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
        R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],)"
        R"("images":[{"bufferView":2,"mimeType":"image/png"}],"textures":[{"source":0}],)"
        R"("materials":[{"name":"paint","pbrMetallicRoughness":{"baseColorTexture":{"index":0},"baseColorFactor":[1,1,1,1]}}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],)"
        R"("nodes":[{"mesh":0,"translation":[0,1,0],"children":[]}],"scenes":[{"nodes":[0]}],"scene":0})";
    while (json.size() % 4) json += ' ';
    Bytes glb;
    auto u32le = [&](u32 v) {
        for (int i = 0; i < 4; ++i) glb.push_back(u8(v >> (8 * i)));
    };
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

const char* kIni = "[car]\nlabel = fuzz_car\nname = Fuzz\nmaker = Testco\ndonor = r34_gtr_v_99\nyear = 2026\n"
                   "[body]\nweight_kg = 1900\nlength_mm = 4750\n[powertrain]\ntype = electric\npower_kw = 300\n"
                   "torque_nm = 500\nmax_rpm = 16000\ncurve = 0:500 8000:400 16000:200\n[drivetrain]\ndrive = AWD\n"
                   "gears = 9.04\nfinal_drive = 1.0\n[tires]\nfront = 255/35R21\nrear = 275/35R21\ngrip_pct = 95\n"
                   "[model]\nfile = models/body.glb\nlower_mm = 10\n[paints]\nwhite = #FFFFFF\nred = #C00000\n"
                   "[raw.engine]\nh:0x20 = 100\n[arcade]\nclass = s\n";

std::vector<Target> targets() {
    return {
        {"png",
         [](ByteSpan d) {
             try {
                 decode_png(d, 2048);
             } catch (const std::exception&) {}
             png_size(d);
         },
         [] {
             return std::vector<Bytes>{encode_png(3, 2, std::vector<u32>(6, 0x80FF00FFu)),
                                       encode_png(17, 9, std::vector<u32>(17 * 9, 0xFFFFFFFFu))};
         }},
        {"glb",
         [](ByteSpan d) {
             try {
                 GlbOptions o;
                 o.max_texture_dim = 2048;
                 load_glb(d, o, "fuzz");
             } catch (const std::exception&) {}
         },
         [] { return std::vector<Bytes>{tiny_glb()}; }},
        {"json",
         [](ByteSpan d) {
             try {
                 json::Value v = json::Value::parse(std::string_view(reinterpret_cast<const char*>(d.data()), d.size()));
                 (void)v["a"][size_t(3)]["b"].integer();
             } catch (const std::exception&) {}
         },
         [] {
             return std::vector<Bytes>{text(R"({"a":[1,2,{"b":-1e300}],"s":"é\n","t":true,"n":null})"),
                                       text("[[[[[[[[[[1]]]]]]]]]]")};
         }},
        {"mod_ini",
         [](ByteSpan d) {
             std::string err;
             std::vector<std::string> warnings;
             data::parse_mod_car(std::string_view(reinterpret_cast<const char*>(d.data()), d.size()), &err, &warnings);
             data::parse_tire_size(std::string_view(reinterpret_cast<const char*>(d.data()), std::min<size_t>(d.size(), 32)));
         },
         [] { return std::vector<Bytes>{text(kIni)}; }},
        {"package",
         [](ByteSpan d) {
             std::string err;
             if (std::optional<game::ModPackage> p = game::parse_mod_package(d, &err))
                 for (const game::ModPackage::File& f : p->files) game::safe_package_path(f.path);
         },
         [] {
             game::ModPackage p;
             p.label = "fuzz_car";
             p.files.push_back({"cars/fuzz_car.ini", text(kIni)});
             p.files.push_back({"models/body.glb", tiny_glb()});
             return std::vector<Bytes>{game::serialize_mod_package(p)};
         }},
        {"snapshot",
         [](ByteSpan d) {
             net::Reader r(d);
             net::CarSnapshot s;
             net::read_snapshot(r, s);
         },
         [] {
             net::CarSnapshot s;
             s.tick = 77;
             s.motion.pos = {0x3F800000u, 0x40000000u, 0x40400000u};
             s.inputs[0] = {0x1F, 2, 1, -300, 4096, 0, 0, 0};
             s.inputs[2] = {0x1F, 2, 0, 0, 0, 0, 4096, 4096};
             s.input_count = 3;
             net::Writer w;
             net::write_snapshot(w, s);
             return std::vector<Bytes>{w.data};
         }},
        {"online_car",
         [](ByteSpan d) { game::decode_online_car(d); },
         [] {
             game::OnlineCar g;
             g.kind = game::OnlineCar::Kind::Garage;
             g.car = "r34_gtr_v_99";
             game::OnlineCar m;
             m.kind = game::OnlineCar::Kind::Mod;
             m.car = "x";
             m.package = std::string(64, 'b');
             game::OnlineCar a;
             a.kind = game::OnlineCar::Kind::Arcade;
             a.car = "nsx_s_zero_97";
             a.arcade = "nsx_s_zero_97_290";
             a.digest.emplace(); // the trailing spec digest
             a.digest->fill(0x5A);
             game::OnlineCar gm = g; // a garage mod car: its package and donor after the digest
             gm.car = "x";
             gm.digest = a.digest;
             gm.package = std::string(64, 'c');
             gm.package_size = 4096;
             gm.donor = "legacy_gtb_96";
             game::OnlineCar md = m; // a mod car's donor after the digest
             md.digest = a.digest;
             md.donor = "legacy_gtb_96";
             return std::vector<Bytes>{game::encode_online_car(g), game::encode_online_car(m), game::encode_online_car(a),
                                       game::encode_online_car(gm), game::encode_online_car(md)};
         }},
        {"rendezvous",
         [](ByteSpan d) {
             net::Rv type;
             net::Reader r(d);
             if (!net::rv_parse(d, type, r)) return;
             if (type == net::Rv::VersionInfo) { // as the game's update check reads it
                 net::GameVersion latest = net::get_version(r), min_online = net::get_version(r);
                 std::string url = r.str();
                 (void)net::update_available(net::this_game_version(), latest);
                 (void)net::meets_minimum(net::this_game_version(), min_online);
                 return;
             }
             if (type == net::Rv::Error) { // UpdateRequired carries the latest version and the page
                 r.u8_();
                 r.str();
                 net::get_version(r);
                 r.str();
                 return;
             }
             net::get_u64(r);
             r.str();
             net::get_endpoints(r);
             net::normalize_code(r.str());
         },
         [] {
             net::Writer w = net::rv_begin(net::Rv::HostInfo);
             net::put_u64(w, 42);
             w.str("1.2.3.4:5");
             net::put_endpoints(w, {"10.0.0.2:4", "[::1]:7"});
             net::Writer info = net::rv_begin(net::Rv::VersionInfo);
             net::put_version(info, *net::parse_game_version("0.3.0"));
             net::put_version(info, *net::parse_game_version("0.2.0"));
             info.str("https://example.org/releases/latest");
             net::Writer err = net::rv_begin(net::Rv::Error);
             err.u8_(u8(net::RvError::UpdateRequired));
             err.str("update required to play online");
             net::put_version(err, *net::parse_game_version("0.3.0"));
             err.str("https://example.org/releases/latest");
             return std::vector<Bytes>{w.data, info.data, err.data};
         }},
        {"versions", // game version strings and the relay's versions file
         [](ByteSpan d) {
             std::string_view t(reinterpret_cast<const char*>(d.data()), d.size());
             if (std::optional<net::GameVersion> v = net::parse_game_version(t)) {
                 if (!net::parse_game_version(v->to_string())) std::abort(); // round trip
                 (void)net::update_available(*v, net::this_game_version());
             }
             net::RelayServer::Versions out;
             std::string err;
             if (net::parse_relay_versions(t, out, &err) && !net::check_relay_versions(out)) std::abort();
         },
         [] {
             return std::vector<Bytes>{text("0.2.0-dev"), text("65535.65535.65535"),
                                       text("# x\nlatest = 0.3.0\nmin_online = 0.2.0\nrelease_url = https://e.org/r\n")};
         }},
        {"relay_server", // byte 0: the source (bits 0-4; bit 7: fill in its valid cookie; bit 6: put a live
                         // session's code for "ABCD2345"; bit 5: 11 s pass first), then a datagram
         [](ByteSpan d) {
             static net::RelayServer& srv = *[] { // never opened: replies go nowhere
                 net::RelayServer::Limits lim;
                 lim.relay_links = 4; // small, so reservations queue, skip and expire
                 lim.max_queue = 3;
                 lim.bench = true;    // 127.0.0.x skips the per-IP limits (deeper paths); 10.0.0.x does not
                 auto* s = new net::RelayServer;
                 s->set_limits(lim);
                 net::RelayServer::Versions v; // requests below 0.2.0 get UpdateRequired
                 net::parse_relay_versions("latest = 0.3.0\nmin_online = 0.2.0\nrelease_url = https://e.org/r\n", v);
                 s->set_versions(v);
                 return s;
             }();
             static double now = 1e6;
             static u64 inputs = 0;
             if (d.empty()) return;
             now += 7;
             if (d[0] & 0x20) now += 11000;
             if (d[0] & 0x20 || ++inputs % 64 == 0) srv.tick(now); // expiry, the queue
             net::Address from;
             std::string ip = (d[0] & 2 ? "10.0.0." : "127.0.0.") + std::to_string(1 + (d[0] & 1));
             net::resolve(ip + ":" + std::to_string(1000 + (d[0] >> 2 & 7)), 0, from);
             Bytes b(d.begin() + 1, d.end());
             if (std::string code = srv.any_code(); d[0] & 0x40 && !code.empty())
                 for (size_t i = 0; i + net::kCodeLength <= b.size(); ++i)
                     if (std::memcmp(b.data() + i, "ABCD2345", net::kCodeLength) == 0) std::memcpy(b.data() + i, code.data(), net::kCodeLength);
             if (d[0] & 0x80 && b.size() >= net::kRvHeader + 8) {
                 u64 c = srv.cookie_for(from, now);
                 for (int i = 0; i < 8; ++i) b[net::kRvHeader + i] = u8(c >> (8 * i));
             }
             srv.receive(from, b, now);
         },
         [] {
             std::vector<Bytes> seeds;
             auto req = [&](net::Rv t, const std::function<void(net::Writer&)>& fill) {
                 for (const char* v : {"0.2.0", "0.1.0"}) { // allowed, and below min_online
                     net::Writer w = net::rv_request(t, 0, *net::parse_game_version(v));
                     fill(w);
                     net::rv_pad(w);
                     Bytes b{0xC0};
                     b.insert(b.end(), w.data.begin(), w.data.end());
                     seeds.push_back(b);
                 }
             };
             const u64 pair = net::pair_key(7, 1, 2);
             req(net::Rv::Register, [](net::Writer& w) { net::put_u64(w, 7), net::put_endpoints(w, {"10.0.0.2:4"}); });
             req(net::Rv::Lookup, [](net::Writer& w) { w.str("ABCD2345"), w.u16_(3), net::put_endpoints(w, {}); });
             req(net::Rv::Bind, [&](net::Writer& w) {
                 net::put_u64(w, pair), w.str("ABCD2345"), net::put_u64(w, 7), w.u8_(1), w.u8_(2);
             });
             req(net::Rv::Keepalive, [](net::Writer& w) { w.str("ABCD2345"), net::put_u64(w, 7); });
             req(net::Rv::Reserve, [](net::Writer& w) { w.str("ABCD2345"), net::put_u64(w, 7), w.u8_(3); });
             req(net::Rv::Release, [](net::Writer& w) { w.str("ABCD2345"), net::put_u64(w, 7); });
             req(net::Rv::Unregister, [](net::Writer& w) { w.str("ABCD2345"), net::put_u64(w, 7); });
             net::Writer vq = net::rv_request(net::Rv::VersionQuery, 0, *net::parse_game_version("0.2.0"));
             vq.u8_(u8(net::RvPlatform::Linux));
             net::rv_pad(vq, net::kVersionQuerySize);
             Bytes vb{0x02}; // from 10.0.0.x: the per-IP limits apply
             vb.insert(vb.end(), vq.data.begin(), vq.data.end());
             seeds.push_back(vb);
             Bytes relay{0x01, 'G', 'T', '3', 'R', 42, 0, 0, 0, 0, 0, 0, 0, 'G', 'T', '3', 'N', 0, 0};
             seeds.push_back(relay);
             net::Writer data; // a race datagram on the pair's channel
             data.bytes(ByteSpan(reinterpret_cast<const u8*>(net::kRelayMagic), 4));
             net::put_u64(data, pair);
             data.bytes(ByteSpan(reinterpret_cast<const u8*>("GT3N"), 4));
             data.data.resize(126, 0);
             Bytes d{0x05};
             d.insert(d.end(), data.data.begin(), data.data.end());
             seeds.push_back(d);
             return seeds;
         }},
    };
}

// Built-in mutator (no coverage feedback): random edits of the seeds and of earlier inputs.
Bytes mutate(const Bytes& in, std::mt19937_64& rng, const std::vector<Bytes>& pool) {
    Bytes b = in;
    int edits = 1 + int(rng() % 8);
    for (int e = 0; e < edits; ++e) {
        size_t n = b.size();
        switch (rng() % 9) {
        case 0: if (n) b[rng() % n] ^= u8(1u << (rng() % 8)); break;                // bit flip
        case 1: if (n) b[rng() % n] = u8(rng()); break;                              // random byte
        case 2: b.insert(b.begin() + ptrdiff_t(n ? rng() % (n + 1) : 0), u8(rng())); break;
        case 3: if (n) b.erase(b.begin() + ptrdiff_t(rng() % n)); break;
        case 4: { // interesting 32-bit values
            static const u32 k[] = {0, 1, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu};
            if (n >= 4) {
                u32 v = k[rng() % (sizeof k / sizeof k[0])];
                std::memcpy(b.data() + rng() % (n - 3), &v, 4);
            }
            break;
        }
        case 5: if (n > 2) b.resize(rng() % n); break;                               // truncate
        case 6: { // duplicate a run
            if (n > 4) {
                size_t a = rng() % n, len = std::min<size_t>(n - a, 1 + rng() % 64);
                Bytes run(b.begin() + ptrdiff_t(a), b.begin() + ptrdiff_t(a + len));
                b.insert(b.begin() + ptrdiff_t(rng() % (n + 1)), run.begin(), run.end());
            }
            break;
        }
        case 7: { // splice another input
            const Bytes& o = pool[rng() % pool.size()];
            if (!o.empty() && n) {
                size_t a = rng() % n, c = rng() % o.size();
                b.resize(a);
                b.insert(b.end(), o.begin() + ptrdiff_t(c), o.end());
            }
            break;
        }
        case 8: { // digits in text formats: a huge or negative number
            static const char* k[] = {"99999999999999999999", "-1", "1e308", "-0", "NaN", "4294967296"};
            const char* s = k[rng() % 6];
            if (n) b.insert(b.begin() + ptrdiff_t(rng() % n), s, s + std::strlen(s));
            break;
        }
        }
        if (b.size() > (1u << 20)) b.resize(1u << 20);
    }
    return b;
}

int run_target(const Target& t, double seconds, const std::vector<std::string>& files) {
    for (const std::string& f : files) { // replay crashers first
        if (FILE* fp = std::fopen(f.c_str(), "rb")) {
            Bytes b;
            int c;
            while ((c = std::fgetc(fp)) != EOF) b.push_back(u8(c));
            std::fclose(fp);
            t.run(b);
        }
    }
    std::vector<Bytes> pool = t.seeds();
    for (const Bytes& s : pool) t.run(s);
    std::mt19937_64 rng(std::random_device{}());
    auto start = std::chrono::steady_clock::now();
    u64 runs = 0;
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < seconds) {
        Bytes b = mutate(pool[rng() % pool.size()], rng, pool);
        // Keep the input around while it runs, so a sanitizer report can be reproduced.
        if (FILE* fp = std::fopen("fuzz-last-input.bin", "wb")) std::fwrite(b.data(), 1, b.size(), fp), std::fclose(fp);
        t.run(b);
        if (pool.size() < 256 && rng() % 16 == 0) pool.push_back(b); // drift away from the seeds
        ++runs;
    }
    std::remove("fuzz-last-input.bin");
    std::printf("%-10s %10llu runs in %.0f s, no crash\n", t.name, (unsigned long long)runs, seconds);
    return 0;
}

} // namespace

#if defined(GT3R_LIBFUZZER)
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static const std::vector<Target> ts = targets();
    static const Target* t = [] {
        const char* name = std::getenv("GT3R_FUZZ_TARGET");
        for (const Target& x : ts)
            if (name && std::strcmp(name, x.name) == 0) return &x;
        std::fprintf(stderr, "set GT3R_FUZZ_TARGET to one of:");
        for (const Target& x : ts) std::fprintf(stderr, " %s", x.name);
        std::fprintf(stderr, "\n");
        std::abort();
    }();
    t->run(ByteSpan(data, size));
    return 0;
}
#else
int main(int argc, char** argv) {
    std::vector<Target> ts = targets();
    if (argc < 2) {
        std::printf("usage: gt3r_fuzz <target|all> [seconds] [inputs...]\ntargets:");
        for (const Target& t : ts) std::printf(" %s", t.name);
        std::printf("\n");
        return 2;
    }
    std::string which = argv[1];
    double seconds = argc > 2 ? std::atof(argv[2]) : 30;
    std::vector<std::string> files(argv + std::min(argc, 3), argv + argc);
    bool found = false;
    for (const Target& t : ts)
        if (which == "all" || which == t.name) {
            found = true;
            run_target(t, seconds, files);
        }
    if (!found) std::fprintf(stderr, "unknown target %s\n", which.c_str());
    return found ? 0 : 2;
}
#endif
