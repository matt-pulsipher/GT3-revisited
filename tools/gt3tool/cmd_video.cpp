// gt3tool movie commands: PSS stream info, frame/audio export for checking
// the MPEG-2 decoder against independent decoders, and a decode survey.
// Exports are for verification in scratch directories only.

#include "tool.h"

#include "formats/png_writer.h"
#include "formats/pss.h"
#include "video/field_layout.h"
#include "video/ipu.h"
#include "video/movie.h"
#include "video/yuv.h"

#include <SDL3/SDL_timer.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace gt3r::tool {

namespace {

using video::Frame;
using video::Movie;

std::unique_ptr<Movie> open(Volume& vol, const std::string& path) {
    const Volume::Entry& e = need(vol, path);
    if (e.compressed) throw FormatError(path + " is compressed; movies are stored plain");
    Volume::Entry entry = e;
    return std::make_unique<Movie>(
        [&vol, entry](u64 off, void* out, size_t len) { vol.read_range(entry, off, out, len); }, entry.size);
}

double seconds(s64 ticks) { return double(ticks) / 90000.0; }

int cmd_video_info(Disc& disc, const Args& a) {
    Volume& vol = disc.volume();
    const Volume::Entry& e = need(vol, a[0]);
    auto movie = open(vol, a[0]);
    const video::MovieInfo& mi = movie->info();
    std::printf("%s: %u bytes\n", a[0].c_str(), e.size);
    std::printf("video: %dx%d @ %.3f fps, %.1f Mbit/s, first PTS %.4f s\n", mi.width, mi.height, mi.frame_rate,
                mi.bit_rate / 1e6, seconds(mi.start_pts));
    if (mi.has_audio)
        std::printf("audio: PS-ADPCM %u Hz x %u ch, %.2f s, first PTS %.4f s\n", mi.audio_rate, mi.audio_channels,
                    mi.audio_seconds, seconds(mi.audio_start_pts));
    else
        std::printf("audio: none\n");

    // Full packet scan (no decoding).
    Volume::Entry entry = e;
    PssDemuxer demux([&vol, entry](u64 off, void* out, size_t len) { vol.read_range(entry, off, out, len); },
                     entry.size);
    struct StreamStats {
        u64 packets = 0, bytes = 0;
        s64 first_pts = -1, last_pts = -1;
    };
    std::map<int, StreamStats> streams;
    PssPacket p;
    u64 pictures = 0;
    u32 window = 0; // rolling bytes to spot picture start codes across packets
    while (demux.next(p)) {
        int key = p.stream_id == PssDemuxer::kPrivateStream1 ? 0xBD00 | p.sub_id : p.stream_id;
        StreamStats& s = streams[key];
        ++s.packets;
        s.bytes += p.size;
        if (p.pts >= 0) {
            if (s.first_pts < 0) s.first_pts = p.pts;
            s.last_pts = p.pts;
        }
        if (p.stream_id == PssDemuxer::kVideoStream)
            for (size_t i = 0; i < p.size; ++i) {
                window = (window << 8) | p.data[i];
                if (window == 0x00000100u) ++pictures;
            }
    }
    for (auto& [key, s] : streams)
        std::printf("stream %s%02X: %llu packets, %llu bytes, PTS %.3f .. %.3f s\n", key > 0xFF ? "BD/" : "",
                    key & 0xFF, (unsigned long long)s.packets, (unsigned long long)s.bytes, seconds(s.first_pts),
                    seconds(s.last_pts));
    std::printf("pictures: %llu (%.2f s at %.3f fps)\n", (unsigned long long)pictures,
                mi.frame_rate > 0 ? pictures / mi.frame_rate : 0.0, mi.frame_rate);
    if (demux.resyncs()) std::printf("resync: skipped %llu bytes\n", (unsigned long long)demux.resyncs());
    return 0;
}

// video-frames <vol-path> <outdir> [first] [count] [step]
int cmd_video_frames(Disc& disc, const Args& a) {
    auto movie = open(disc.volume(), a[0]);
    movie->set_audio_enabled(false);
    u32 first = a.size() > 2 ? u32(std::atoi(a[2].c_str())) : 0;
    u32 count = a.size() > 3 ? u32(std::atoi(a[3].c_str())) : 10;
    u32 step = a.size() > 4 ? u32(std::max(1, std::atoi(a[4].c_str()))) : 1;
    Frame f;
    u32 written = 0;
    while (written < count && movie->next_frame(f)) {
        if (f.index < first || (f.index - first) % step) continue;
        char name[64];
        std::snprintf(name, sizeof(name), "/frame%05u_%c.png", f.index, f.type);
        if (!save(a[1] + name, encode_png(f.width, f.height, video::frame_to_rgba(f)))) return 1;
        std::printf("frame %u (%c) pts %.4f\n", f.index, f.type, seconds(f.pts));
        ++written;
    }
    return 0;
}

// video-fields <vol-path> <outdir> [first] [count]: the full pictures the movie player shows for
// each field of a field-split movie (video::deinterlace_fields), two PNGs per frame.
int cmd_video_fields(Disc& disc, const Args& a) {
    auto movie = open(disc.volume(), a[0]);
    movie->set_audio_enabled(false);
    u32 first = a.size() > 2 ? u32(std::atoi(a[2].c_str())) : 0;
    u32 count = a.size() > 3 ? u32(std::atoi(a[3].c_str())) : 10;
    Frame prev, cur, next, out;
    u32 written = 0;
    bool more = movie->next_frame(cur);
    while (more && written < count) {
        more = movie->next_frame(next);
        if (cur.index >= first) {
            if (!video::can_deinterlace(cur)) {
                std::fprintf(stderr, "frame %u: %dx%d cannot be deinterlaced\n", cur.index, cur.width, cur.height);
                return 1;
            }
            video::deinterlace_fields(prev.width ? prev : cur, cur, more ? next : cur, out);
            for (int field = 0; field < 2; ++field) {
                Frame pic = out;
                pic.height = cur.height;
                const size_t ny = size_t(pic.width) * size_t(pic.height), nc = size_t(pic.chroma_width()) * size_t(pic.chroma_height());
                pic.y.assign(out.y.begin() + ptrdiff_t(field * ny), out.y.begin() + ptrdiff_t((field + 1) * ny));
                pic.u.assign(out.u.begin() + ptrdiff_t(field * nc), out.u.begin() + ptrdiff_t((field + 1) * nc));
                pic.v.assign(out.v.begin() + ptrdiff_t(field * nc), out.v.begin() + ptrdiff_t((field + 1) * nc));
                char name[64];
                std::snprintf(name, sizeof(name), "/field%05u_%c.png", cur.index, field ? 'b' : 'a');
                if (!save(a[1] + name, encode_png(pic.width, pic.height, video::frame_to_rgba(pic)))) return 1;
            }
            ++written;
        }
        prev = std::move(cur);
        cur = std::move(next);
        next = Frame{};
    }
    std::printf("%u frames, %u pictures\n", written, written * 2);
    return 0;
}

// video-yuv <vol-path> <out.yuv> [count]: raw yuv420p in display order.
int cmd_video_yuv(Disc& disc, const Args& a) {
    auto movie = open(disc.volume(), a[0]);
    movie->set_audio_enabled(false);
    u32 count = a.size() > 2 ? u32(std::atoi(a[2].c_str())) : ~0u;
    std::FILE* fp = std::fopen(a[1].c_str(), "wb");
    if (!fp) {
        std::fprintf(stderr, "cannot write %s\n", a[1].c_str());
        return 1;
    }
    Frame f;
    u32 n = 0;
    while (n < count && movie->next_frame(f)) {
        std::fwrite(f.y.data(), 1, f.y.size(), fp);
        std::fwrite(f.u.data(), 1, f.u.size(), fp);
        std::fwrite(f.v.data(), 1, f.v.size(), fp);
        ++n;
    }
    std::fclose(fp);
    std::printf("%u frames %dx%d\n", n, f.width, f.height);
    return 0;
}

// video-psnr <vol-path> <ref.yuv|-> [quiet]: compares our frames with raw
// yuv420p frames from another decoder, e.g.
//   ffmpeg -i x.pss -f rawvideo -pix_fmt yuv420p - | gt3tool video-psnr <iso> stream/x.pss -
int cmd_video_psnr(Disc& disc, const Args& a) {
    auto movie = open(disc.volume(), a[0]);
    movie->set_audio_enabled(false);
    bool quiet = a.size() > 2;
#ifdef _WIN32
    if (a[1] == "-") _setmode(_fileno(stdin), _O_BINARY); // raw frames: no CRLF translation
#endif
    std::FILE* fp = a[1] == "-" ? stdin : std::fopen(a[1].c_str(), "rb");
    if (!fp) {
        std::fprintf(stderr, "cannot read %s\n", a[1].c_str());
        return 1;
    }
    Frame f;
    std::vector<u8> ref;
    u32 n = 0, exact = 0, ref_frames = 0;
    double sum = 0, worst = 1e9;
    int max_diff = 0;
    bool ref_done = false;
    while (movie->next_frame(f)) {
        size_t bytes = f.y.size() + f.u.size() + f.v.size();
        ref.resize(bytes);
        if (ref_done || std::fread(ref.data(), 1, bytes, fp) != bytes) {
            ref_done = true;
            ++n;
            continue;
        }
        ++ref_frames;
        const u8* planes[3] = {f.y.data(), f.u.data(), f.v.data()};
        size_t sizes[3] = {f.y.size(), f.u.size(), f.v.size()};
        double se = 0;
        size_t off = 0;
        for (int p = 0; p < 3; ++p) {
            for (size_t i = 0; i < sizes[p]; ++i) {
                int d = int(planes[p][i]) - int(ref[off + i]);
                se += double(d * d);
                max_diff = std::max(max_diff, std::abs(d));
            }
            off += sizes[p];
        }
        double psnr = se == 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 * double(bytes) / se);
        if (se == 0) ++exact;
        sum += psnr;
        worst = std::min(worst, psnr);
        if (!quiet) std::printf("frame %5u %c  PSNR %.2f dB\n", n, f.type, psnr);
        ++n;
    }
    // Count leftover reference frames.
    u32 extra = 0;
    if (!ref_done && !ref.empty())
        while (std::fread(ref.data(), 1, ref.size(), fp) == ref.size()) ++extra;
    if (fp != stdin) std::fclose(fp);
    std::printf("%s: ours %u frames, reference %u frames; PSNR min %.2f mean %.2f dB (99 = identical), "
                "%u identical, max sample diff %d\n",
                a[0].c_str(), n, ref_frames + extra, ref_frames ? worst : 0.0, ref_frames ? sum / ref_frames : 0.0,
                exact, max_diff);
    return n == ref_frames + extra ? 0 : 1;
}

int cmd_video_wav(Disc& disc, const Args& a) {
    auto movie = open(disc.volume(), a[0]);
    const video::MovieInfo& mi = movie->info();
    if (!mi.has_audio) {
        std::printf("%s has no audio track\n", a[0].c_str());
        return 1;
    }
    movie->set_video_enabled(false);
    std::vector<s16> pcm;
    s16 buf[4096 * 8];
    while (size_t n = movie->read_audio(buf, 4096)) pcm.insert(pcm.end(), buf, buf + n * mi.audio_channels);
    std::printf("%u Hz, %u ch, %.2f s\n", mi.audio_rate, mi.audio_channels,
                double(pcm.size()) / mi.audio_channels / mi.audio_rate);
    return save(a[1], encode_wav(mi.audio_rate, mi.audio_channels, pcm)) ? 0 : 1;
}

// The raw audio track as an .ads file (for cross-checking with other tools).
int cmd_video_ads(Disc& disc, const Args& a) {
    Volume& vol = disc.volume();
    Volume::Entry entry = need(vol, a[0]);
    PssDemuxer demux([&vol, entry](u64 off, void* out, size_t len) { vol.read_range(entry, off, out, len); },
                     entry.size);
    Bytes ads;
    PssPacket p;
    while (demux.next(p))
        if (p.stream_id == PssDemuxer::kPrivateStream1) ads.insert(ads.end(), p.data, p.data + p.size);
    std::printf("%zu bytes of audio\n", ads.size());
    return !ads.empty() && save(a[1], ads) ? 0 : 1;
}

// video-survey [prefix] [max-frames]: decode every .pss; report counts and speed.
int cmd_video_survey(Disc& disc, const Args& a) {
    std::string prefix = a.empty() ? "stream/" : a[0];
    u32 max_frames = a.size() > 1 ? u32(std::atoi(a[1].c_str())) : ~0u;
    Volume& vol = disc.volume();
    size_t files = 0, ok = 0;
    u64 total_frames = 0;
    double total_time = 0, total_video = 0;
    for (const Volume::Entry& e : vol.entries()) {
        if (e.path.compare(0, prefix.size(), prefix) != 0) continue;
        if (e.path.size() < 4 || e.path.substr(e.path.size() - 4) != ".pss") continue;
        ++files;
        try {
            auto movie = open(vol, e.path);
            Uint64 t0 = SDL_GetTicksNS();
            Frame f;
            u32 n = 0;
            std::map<char, u32> types;
            u32 split = 0, flat = 0;
            video::FieldLayoutDetector layout;
            s64 last_pts = -1;
            bool monotonic = true;
            while (n < max_frames && movie->next_frame(f)) {
                ++n;
                ++types[f.type];
                layout.add(f);
                double r = video::seam_ratio(f);
                if (r == 0.0) ++flat;
                else if (r > video::FieldLayoutDetector::kSeamThreshold) ++split;
                if (f.pts <= last_pts) monotonic = false;
                last_pts = f.pts;
            }
            // Drain audio so its decode is part of the measurement.
            size_t audio_frames = 0;
            if (movie->info().has_audio && max_frames == ~0u) {
                s16 buf[4096 * 2];
                while (size_t k = movie->read_audio(buf, 4096)) audio_frames += k;
            }
            double dt = double(SDL_GetTicksNS() - t0) / 1e9;
            const auto& st = movie->decoder().stats();
            double video_s = movie->info().frame_rate > 0 ? n / movie->info().frame_rate : 0;
            std::printf("%-32s %dx%d %5u frames (I%u P%u B%u) slices %llu err %llu skip %llu%s audio %.1fs  "
                        "%s (seam %u/%u, flat %u)  %.2fs = %.0f fps\n",
                        e.path.c_str(), movie->info().width, movie->info().height, n, types['I'], types['P'],
                        types['B'], (unsigned long long)st.slices, (unsigned long long)st.slice_errors,
                        (unsigned long long)st.skipped_pictures, monotonic ? "" : " PTS-NONMONOTONIC",
                        movie->info().has_audio ? double(audio_frames) / movie->info().audio_rate : 0.0,
                        layout.field_split() ? "field-split" : "progressive", split,
                        n - flat, flat, dt,
                        dt > 0 ? n / dt : 0.0);
            total_frames += n;
            total_time += dt;
            total_video += video_s;
            if (n > 0 && st.slice_errors == 0) ++ok;
        } catch (const std::exception& ex) {
            std::printf("%-32s FAILED: %s\n", e.path.c_str(), ex.what());
        }
    }
    std::printf("%zu/%zu movies decode without errors; %llu frames (%.1f s of video) in %.1f s = %.0f fps, "
                "%.1fx real time\n",
                ok, files, (unsigned long long)total_frames, total_video, total_time,
                total_time > 0 ? total_frames / total_time : 0.0, total_time > 0 ? total_video / total_time : 0.0);
    return ok == files ? 0 : 1;
}

// video-ipu <vol-path> [outdir]: list an ipua archive (or one ipum picture)
// and optionally decode every picture to PNG (raw and field-woven).
int cmd_video_ipu(Disc& disc, const Args& a) {
    Volume& vol = disc.volume();
    Bytes data = vol.read(need(vol, a[0]));
    std::vector<video::IpuArchiveEntry> entries;
    if (has_magic(data, "ipum")) entries.push_back({"picture", 0, u32(data.size())});
    else entries = video::parse_ipua(data);
    int failed = 0;
    for (const auto& e : entries) {
        try {
            ByteSpan blob = ByteSpan(data).subspan(e.offset, e.size);
            video::IpuHeader h = video::IpuHeader::parse(blob);
            Frame f = video::decode_ipum(blob);
            double seam = video::seam_ratio(f);
            bool split = seam > video::FieldLayoutDetector::kSeamThreshold;
            std::printf("%-16s @0x%07x %6u bytes  %dx%d frames %u flags 0x%02x  seam %.1f%s\n", e.name.c_str(),
                        e.offset, h.data_size, h.width, h.height, h.frames, blob[video::IpuHeader::kBytes],
                        seam, split ? " (2 fields)" : "");
            if (a.size() > 1) {
                if (!save(a[1] + "/" + e.name + ".png", encode_png(f.width, f.height, video::frame_to_rgba(f))))
                    return 1;
                if (split) {
                    Frame w = video::weave_fields(f);
                    if (!save(a[1] + "/" + e.name + "_woven.png", encode_png(w.width, w.height, video::frame_to_rgba(w))))
                        return 1;
                }
            }
        } catch (const std::exception& ex) {
            std::printf("%-16s FAILED: %s\n", e.name.c_str(), ex.what());
            ++failed;
        }
    }
    std::printf("%zu/%zu pictures decode\n", entries.size() - failed, entries.size());
    return failed ? 1 : 0;
}

} // namespace

GT3TOOL_COMMAND(video_ipu, "video-ipu", "<vol-path> [outdir]",
                "list/decode IPU stills (stream/course.ipic) to PNGs (scratch only)", 1, cmd_video_ipu);
GT3TOOL_COMMAND(video_info, "video-info", "<vol-path>", "describe a .pss movie (streams, timing, frame count)", 1,
                cmd_video_info);
GT3TOOL_COMMAND(video_frames, "video-frames", "<vol-path> <outdir> [first] [count] [step]",
                "decode movie frames to PNGs (scratch only)", 2, cmd_video_frames);
GT3TOOL_COMMAND(video_fields, "video-fields", "<vol-path> <outdir> [first] [count]",
                "write the deinterlaced picture the player shows for each field (scratch only)", 2, cmd_video_fields);
GT3TOOL_COMMAND(video_yuv, "video-yuv", "<vol-path> <out.yuv> [count]",
                "decode movie frames to raw yuv420p (for comparison with other decoders)", 2, cmd_video_yuv);
GT3TOOL_COMMAND(video_psnr, "video-psnr", "<vol-path> <ref.yuv|-> [quiet]",
                "compare decoded frames with raw yuv420p from another decoder (stdin with -)", 2, cmd_video_psnr);
GT3TOOL_COMMAND(video_wav, "video-wav", "<vol-path> <out.wav>", "decode a movie's audio track", 2, cmd_video_wav);
GT3TOOL_COMMAND(video_ads, "video-ads", "<vol-path> <out.ads>", "extract a movie's raw audio track (.ads)", 2,
                cmd_video_ads);
GT3TOOL_COMMAND(video_survey, "video-survey", "[prefix] [max-frames]", "decode every .pss movie and time it", 0,
                cmd_video_survey);

} // namespace gt3r::tool
