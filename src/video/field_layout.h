#pragma once

#include "video/mpeg2_decoder.h"

namespace gt3r::video {

// Most GT3 movies are "field-split": each 640x448 MPEG frame holds the two
// fields of an interlaced 60 Hz picture as two 640x224 halves (top half =
// first field, bottom half = second field, 1/60 s later). The PS2 displays
// them as interlaced fields; on a progressive display we show each half
// line-doubled ("bob"), giving 60 pictures per second. Other movies (the
// ending credits) are ordinary progressive frames. There is no flag in the
// stream, so the layout is detected from the picture content. Survey of all
// 51 files (gt3tool video-survey): 48 field-split (>= 94% of frames have a
// seam ratio > 3; the rest are busy or flat frames), 3 progressive
// (ending_long_full, ending_short_full, menu_bg2; <= 1.6% above 3). Concept
// discs: the openings (opening.pss, opening_GTCJP.pss) and car_gtc /
// status_gtc are progressive (<= 2.3% above 3); the rest are field-split.

// Discontinuity at the half-height seam: the mean difference of the rows
// across it divided by the mean difference of the neighbouring row pairs.
// Field-split frames join the bottom of one picture to the top of the next
// (large ratio); progressive frames are continuous there (~1). A one-line
// horizontal stripe lying on the seam row (the grid that opens the Concept
// openings) is skipped over: the smallest of the jumps row h/2-1 -> h/2,
// h/2-1 -> h/2+1 and h/2-2 -> h/2 is used. Returns 0 for flat rows.
double seam_ratio(const Frame& f);

// Decides once per movie: majority of the first kVotes non-flat frames, by
// seam_ratio() > kSeamThreshold. Until then, the majority so far.
class FieldLayoutDetector {
public:
    static constexpr int kVotes = 16;
    static constexpr double kSeamThreshold = 3.0;

    void add(const Frame& f);
    bool field_split() const { return split_ * 2 > total_; }
    bool decided() const { return total_ >= kVotes; }

private:
    int split_ = 0, total_ = 0;
};

// Motion-adaptive deinterlacing of a field-split frame. Showing each field
// line-doubled ("bob") makes fine horizontal detail (text, thin edges) flicker
// between the two fields 60 times a second, which a CRT hid but a sharp
// progressive display shows as shaking. This rebuilds each field as a full
// picture: where the picture is still, the missing lines come from the
// neighbouring fields (as sharp as a woven frame); where it moves, from the
// field's own lines (as bob), so motion keeps its 60 pictures per second.
// The rule is the usual one of the "yadif" filter, without its edge search.
//
// `cur` is field-split (height a multiple of 4); `prev` and `next` are the
// frames around it (pass `cur` itself at the ends of a movie). `out` gets
// the two pictures stacked: rows [0, H) the first field's, [H, 2H) the
// second's, each the full frame size (chroma likewise).
void deinterlace_fields(const Frame& prev, const Frame& cur, const Frame& next, Frame& out);
bool can_deinterlace(const Frame& f);

} // namespace gt3r::video
