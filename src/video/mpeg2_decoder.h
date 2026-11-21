#pragma once

// Compact MPEG-2 video decoder (ISO/IEC 13818-2), written for the FMVs on the
// GT3 disc: Main Profile, 4:2:0, frame pictures with I/P/B coding. It also
// accepts frame pictures that use field DCT (dct_type) for completeness.
// Not supported (throws FormatError): MPEG-1 streams, field pictures, field
// or dual-prime motion compensation, 4:2:2/4:4:4, scalable extensions.
// See docs/dev/MOVIES.md.
//
// Usage: push() elementary-stream bytes as they are demuxed (any chunking),
// optionally with the PES presentation timestamp that applies to the first
// picture starting in that chunk; pop() decoded frames in display order;
// flush() at end of stream to emit the last reference picture.

#include "base/bytes.h"

#include <deque>
#include <memory>
#include <vector>

namespace gt3r::video {

// A decoded picture in display order: planar YUV 4:2:0 (BT.601, limited
// range), each plane tightly packed at the display size.
struct Frame {
    int width = 0, height = 0;
    std::vector<u8> y, u, v; // y: width*height, u/v: ((width+1)/2)*((height+1)/2)
    s64 pts = -1;            // 90 kHz presentation time
    char type = '?';         // 'I', 'P' or 'B'
    u32 index = 0;           // display order index from 0
    bool fields_stacked = false; // deinterlace_fields() output: two full pictures, one per field

    int chroma_width() const { return (width + 1) / 2; }
    int chroma_height() const { return (height + 1) / 2; }
};

struct SequenceInfo {
    bool valid = false;
    int width = 0, height = 0;          // display size from the sequence header
    int aspect_code = 0;
    int frame_rate_code = 0;
    double frame_rate = 0;              // frames per second
    u32 bit_rate = 0;                   // bits/s
    int profile_level = 0;
    bool progressive = false;
    int chroma_format = 1;              // 1 = 4:2:0
};

class Mpeg2Decoder {
public:
    Mpeg2Decoder();
    ~Mpeg2Decoder();
    Mpeg2Decoder(const Mpeg2Decoder&) = delete;
    Mpeg2Decoder& operator=(const Mpeg2Decoder&) = delete;

    // `pts` < 0: no timestamp. Decodes every slice that is complete.
    void push(const u8* data, size_t len, s64 pts = -1);
    // End of stream: decodes the trailing unit and outputs the last reference.
    void flush();
    // Forgets pictures and buffered data but keeps the sequence parameters.
    void reset();

    bool pop(Frame& out);

    // Decodes one PS2 IPU picture (intra-only MPEG-2 macroblocks, see
    // video/ipu.h) of the given size; the frame is then available from pop().
    // Throws FormatError.
    void decode_ipu_picture(const u8* data, size_t len, int width, int height);
    size_t frames_ready() const { return out_.size(); }
    const SequenceInfo& sequence() const { return seq_; }

    struct Stats {
        u64 pictures = 0;      // decoded pictures
        u64 slices = 0;
        u64 slice_errors = 0;  // slices abandoned because of bitstream errors
        u64 skipped_pictures = 0; // B pictures without references, unsupported pictures
        u64 output = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SequenceInfo seq_;
    Stats stats_;
    std::deque<Frame> out_;
    std::vector<Frame> spare_; // recycled frames to avoid reallocating planes
    friend struct Impl;
};

} // namespace gt3r::video
