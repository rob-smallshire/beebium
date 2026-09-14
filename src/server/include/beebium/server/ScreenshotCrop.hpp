// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#pragma once

// Auto-crop geometry for the capture-screenshot subcommand's `--crop auto`.
//
// A boot-banner thumbnail sits top-left in a mostly black frame, so most of the
// image is void. `--crop auto` finds the content, pads it, expands it to the
// frame's aspect ratio, and (see compute_auto_crop) hands back a sub-rectangle
// of the LOGICAL frame -- the framebuffer as copy_frame produces it, before the
// nearest-neighbour scale to display dimensions. The subcommand then scales that
// rectangle to the same display size a full thumbnail would have, so the banner
// is enlarged rather than the image shrunk, and a thumbnail set stays uniform in
// aspect and pixel size.
//
// Everything here is a pure function of the pixels and dimensions: no machine,
// no framebuffer object, unit-testable in isolation.

#include <cstddef>
#include <cstdint>
#include <optional>

namespace beebium::server {

// A rectangle in logical frame pixels.
struct CropRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    bool operator==(const CropRect& o) const {
        return x == o.x && y == o.y && width == o.width && height == o.height;
    }
};

// A pixel counts as content if any colour channel exceeds this. A few levels
// above pure black, so renderer noise near black does not register as content;
// 16 of 255 is well below any real glyph stroke.
inline constexpr uint8_t kContentThreshold = 16;

// The expanded crop box covering more than this fraction of the frame is not
// worth cropping: return the whole frame instead.
inline constexpr double kMaxCropAreaFraction = 0.90;

// Bounding box of the content pixels in a BGRA32 logical frame (row-major,
// `stride_pixels` uint32s per row). Returns nullopt when the frame is entirely
// at or below the threshold (all black).
inline std::optional<CropRect> content_bounding_box(
        const uint32_t* pixels, uint32_t width, uint32_t height,
        std::size_t stride_pixels, uint8_t threshold = kContentThreshold) {
    bool found = false;
    uint32_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t* row = pixels + static_cast<std::size_t>(y) * stride_pixels;
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t px = row[x];
            const uint8_t b = static_cast<uint8_t>(px & 0xFF);
            const uint8_t g = static_cast<uint8_t>((px >> 8) & 0xFF);
            const uint8_t r = static_cast<uint8_t>((px >> 16) & 0xFF);
            if (b > threshold || g > threshold || r > threshold) {
                if (!found) {
                    min_x = max_x = x;
                    min_y = max_y = y;
                    found = true;
                } else {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (y < min_y) min_y = y;
                    if (y > max_y) max_y = y;
                }
            }
        }
    }
    if (!found) return std::nullopt;
    return CropRect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
}

namespace detail {

// Grow `box` about its centre to at least (want_w, want_h), then shift it to lie
// within [0,W) x [0,H); if a dimension cannot fit, it is clamped to the frame.
inline CropRect grow_centred(CropRect box, uint32_t want_w, uint32_t want_h,
                             uint32_t W, uint32_t H) {
    uint32_t w = want_w > box.width ? want_w : box.width;
    uint32_t h = want_h > box.height ? want_h : box.height;
    if (w > W) w = W;
    if (h > H) h = H;
    // Centre on the current box's centre, then clamp into the frame.
    const int64_t cx = box.x + static_cast<int64_t>(box.width) / 2;
    const int64_t cy = box.y + static_cast<int64_t>(box.height) / 2;
    int64_t x = cx - static_cast<int64_t>(w) / 2;
    int64_t y = cy - static_cast<int64_t>(h) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + static_cast<int64_t>(w) > W) x = static_cast<int64_t>(W) - w;
    if (y + static_cast<int64_t>(h) > H) y = static_cast<int64_t>(H) - h;
    return CropRect{static_cast<uint32_t>(x), static_cast<uint32_t>(y), w, h};
}

}  // namespace detail

// Compute the auto-crop rectangle on the logical frame. See the file header.
//
// The returned rectangle is a sub-rectangle of the logical frame carrying the
// frame's own aspect ratio, so scaling it to the display (which is a per-axis
// scale of the frame) keeps the display's pixel aspect. The subcommand owns the
// scale to display dimensions; the geometry here is expressed purely in frame
// pixels and needs no display dimensions.
//
// Output-size policy lives here, in one place: where an integer zoom of the
// uncropped rendering fits, the crop is snapped OUTWARD (enlarged, still centred
// and clamped) to reach it, so nearest-neighbour upscaling keeps glyph strokes
// as even as the uncropped image has them; only if no integer zoom fits does it
// fall back to the non-integer expanded box.
inline CropRect compute_auto_crop(
        const uint32_t* pixels, uint32_t width, uint32_t height,
        std::size_t stride_pixels, uint8_t threshold = kContentThreshold) {
    const CropRect full{0, 0, width, height};
    if (width == 0 || height == 0) return full;

    auto content = content_bounding_box(pixels, width, height, stride_pixels, threshold);
    if (!content) return full;  // all black -> no crop

    // Pad by about one character cell each side. In the logical 640-wide frame a
    // MODE 7 column is 640/40 = 16 px and a row 512/25 ~ 20 px; width/40 and
    // height/25 track that for any mode without needing to know which mode it is.
    const uint32_t hpad = width / 40;
    const uint32_t vpad = height / 25;
    int64_t px0 = static_cast<int64_t>(content->x) - hpad;
    int64_t py0 = static_cast<int64_t>(content->y) - vpad;
    int64_t px1 = static_cast<int64_t>(content->x) + content->width + hpad;
    int64_t py1 = static_cast<int64_t>(content->y) + content->height + vpad;
    if (px0 < 0) px0 = 0;
    if (py0 < 0) py0 = 0;
    if (px1 > width) px1 = width;
    if (py1 > height) py1 = height;
    CropRect padded{static_cast<uint32_t>(px0), static_cast<uint32_t>(py0),
                    static_cast<uint32_t>(px1 - px0), static_cast<uint32_t>(py1 - py0)};

    // Expand to the frame's aspect ratio, growing about the padded box's centre.
    // Compare padded.width/padded.height against width/height by cross-multiply
    // to avoid floating point: too narrow -> widen, else heighten.
    uint32_t want_w = padded.width;
    uint32_t want_h = padded.height;
    if (static_cast<uint64_t>(padded.width) * height
        < static_cast<uint64_t>(padded.height) * width) {
        // Narrower than the frame aspect: widen to padded.height * (W/H).
        want_w = static_cast<uint32_t>(
            (static_cast<uint64_t>(padded.height) * width + height - 1) / height);
    } else {
        // Taller/wider than needed: heighten to padded.width * (H/W).
        want_h = static_cast<uint32_t>(
            (static_cast<uint64_t>(padded.width) * height + width - 1) / width);
    }
    CropRect expanded = detail::grow_centred(padded, want_w, want_h, width, height);

    // Not worth cropping if it already covers most of the frame.
    if (static_cast<double>(expanded.width) * expanded.height
        > kMaxCropAreaFraction * static_cast<double>(width) * height) {
        return full;
    }

    // Snap OUTWARD to an integer zoom where one fits. Logical pixels are NOT
    // square: the display is a per-axis scale of the frame with factors
    // fx = display_width/width and fy = display_height/height (in practice
    // display_width is fixed at 640 while display_height tracks the frame, so fx
    // varies by mode and fy is 1). A crop of cw = width/z, ch = height/z renders
    // to the display with factors fx*z and fy*z -- the SAME pixel aspect as the
    // uncropped image, zoomed by one integer z on both axes, so glyph strokes
    // stay as even as they were. Expressing the snap in display dimensions would
    // instead give the crop the display's aspect in logical pixels and stretch
    // any non-square mode.
    //
    // Restricting z to the common integer divisors of width and height (z >= 2)
    // keeps cw and ch whole and the frame's aspect exact -- no rounding, and a
    // single uniform zoom on both axes. Among those, pick the largest z (the
    // smallest, tightest crop, i.e. the least outward enlargement) whose crop
    // still covers the expanded box.
    //
    // z = 1 is excluded on purpose: it is the whole frame, i.e. no zoom at all.
    // If no z >= 2 covers the box we fall back to the non-integer `expanded` box
    // rather than hand back the uncropped frame.
    {
        CropRect best{};
        bool have_best = false;
        const uint32_t z_max = width < height ? width : height;
        for (uint32_t z = 2; z <= z_max; ++z) {
            if (width % z != 0 || height % z != 0) continue;
            const uint32_t cw = width / z;
            const uint32_t ch = height / z;
            if (cw < expanded.width || ch < expanded.height) continue;  // must cover
            // Larger z -> smaller crop -> tighter (more outward snap). Keep the
            // tightest that still covers.
            if (!have_best || cw < best.width) {
                best = detail::grow_centred(expanded, cw, ch, width, height);
                have_best = true;
            }
        }
        if (have_best) return best;
    }

    return expanded;  // documented fallback: no integer (>= 2x) zoom fits
}

}  // namespace beebium::server
