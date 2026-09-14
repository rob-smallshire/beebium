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

// High-quality image resampling for the capture-screenshot `--crop auto` path.
//
// A cropped logical region is resampled straight to the output display size in
// ONE separable pass with per-axis scale factors, so the non-square logical
// pixels (the display is 640 wide whatever the mode's logical width, and the
// same height as the logical frame) come out with the correct on-screen aspect.
// The filter is Lanczos-3; on any axis that downscales its support is widened by
// 1/scale, which turns the same code into an area-average on downscale while it
// stays a sharp Lanczos interpolation on upscale. Values are clamped to 0-255
// after each pass so the kernel's negative lobes cannot wrap around.
//
// Everything here is a pure function of the pixels and dimensions: no machine,
// no framebuffer object, unit-testable in isolation.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace beebium::server {

namespace detail {

constexpr double kPi = 3.14159265358979323846;
constexpr double kLanczosA = 3.0;

inline double sinc(double x) {
    if (x == 0.0) return 1.0;
    const double px = kPi * x;
    return std::sin(px) / px;
}

// Lanczos kernel with a = 3: sinc(x) * sinc(x/a) for |x| < a, else 0.
inline double lanczos3(double x) {
    if (x <= -kLanczosA || x >= kLanczosA) return 0.0;
    return sinc(x) * sinc(x / kLanczosA);
}

// Resampling weights mapping every destination index on one axis to a run of
// source samples. `weights` is flattened; destination i uses count[i] weights
// starting at offset[i], applied to source samples first[i] .. first[i]+count-1
// (indices are clamped into the source extent at sample time, replicating the
// edge). Each destination's weights are normalised to sum to 1.
struct AxisWeights {
    std::vector<int32_t> first;
    std::vector<int32_t> count;
    std::vector<int32_t> offset;
    std::vector<double> weights;
};

inline AxisWeights compute_axis_weights(uint32_t src_size, uint32_t dst_size) {
    AxisWeights w;
    w.first.resize(dst_size);
    w.count.resize(dst_size);
    w.offset.resize(dst_size);

    const double scale = static_cast<double>(dst_size) / static_cast<double>(src_size);
    // Widen the kernel on a downscaling axis so it low-passes (area-average);
    // leave it at unit width on an upscaling axis (sharp Lanczos).
    const double filter_scale = scale < 1.0 ? 1.0 / scale : 1.0;
    const double support = kLanczosA * filter_scale;

    for (uint32_t i = 0; i < dst_size; ++i) {
        // Centre of destination pixel i mapped back to source coordinates.
        const double centre = (static_cast<double>(i) + 0.5) / scale - 0.5;
        const int32_t left = static_cast<int32_t>(std::ceil(centre - support));
        const int32_t right = static_cast<int32_t>(std::floor(centre + support));
        w.first[i] = left;
        w.count[i] = right - left + 1;
        w.offset[i] = static_cast<int32_t>(w.weights.size());

        double wsum = 0.0;
        for (int32_t j = left; j <= right; ++j) {
            const double t = (centre - static_cast<double>(j)) / filter_scale;
            const double weight = lanczos3(t);
            w.weights.push_back(weight);
            wsum += weight;
        }
        if (wsum != 0.0) {
            for (int32_t k = 0; k < w.count[i]; ++k) {
                w.weights[static_cast<std::size_t>(w.offset[i]) + k] /= wsum;
            }
        }
    }
    return w;
}

inline double clamp_channel(double v) {
    if (v < 0.0) return 0.0;
    if (v > 255.0) return 255.0;
    return v;
}

}  // namespace detail

// Resample the BGRA32 sub-rectangle (src_x, src_y, src_width, src_height) of a
// row-major image (stride_pixels uint32s per row) to a tightly packed
// dst_width x dst_height BGRA32 image, with a separable Lanczos-3 filter and
// independent per-axis scale. Returns dst_width*dst_height pixels (row-major).
inline std::vector<uint32_t> resample_bgra_lanczos3(
        const uint32_t* pixels, std::size_t stride_pixels,
        uint32_t src_x, uint32_t src_y, uint32_t src_width, uint32_t src_height,
        uint32_t dst_width, uint32_t dst_height) {
    std::vector<uint32_t> out(static_cast<std::size_t>(dst_width) * dst_height, 0);
    if (src_width == 0 || src_height == 0 || dst_width == 0 || dst_height == 0) {
        return out;
    }

    const detail::AxisWeights hw = detail::compute_axis_weights(src_width, dst_width);
    const detail::AxisWeights vw = detail::compute_axis_weights(src_height, dst_height);

    // Pass 1 (horizontal): source (src_width x src_height) -> tmp (dst_width x
    // src_height), four channels held as clamped floats.
    std::vector<float> tmp(static_cast<std::size_t>(dst_width) * src_height * 4);
    for (uint32_t sy = 0; sy < src_height; ++sy) {
        const uint32_t* srow =
            pixels + (static_cast<std::size_t>(src_y) + sy) * stride_pixels + src_x;
        for (uint32_t dx = 0; dx < dst_width; ++dx) {
            const int32_t first = hw.first[dx];
            const int32_t cnt = hw.count[dx];
            const int32_t off = hw.offset[dx];
            double acc[4] = {0.0, 0.0, 0.0, 0.0};
            for (int32_t k = 0; k < cnt; ++k) {
                int32_t sx = first + k;
                if (sx < 0) sx = 0;
                if (sx >= static_cast<int32_t>(src_width)) sx = static_cast<int32_t>(src_width) - 1;
                const uint32_t px = srow[sx];
                const double weight = hw.weights[static_cast<std::size_t>(off) + k];
                acc[0] += weight * static_cast<double>(px & 0xFF);          // B
                acc[1] += weight * static_cast<double>((px >> 8) & 0xFF);   // G
                acc[2] += weight * static_cast<double>((px >> 16) & 0xFF);  // R
                acc[3] += weight * static_cast<double>((px >> 24) & 0xFF);  // A
            }
            float* t = &tmp[(static_cast<std::size_t>(sy) * dst_width + dx) * 4];
            for (int c = 0; c < 4; ++c) t[c] = static_cast<float>(detail::clamp_channel(acc[c]));
        }
    }

    // Pass 2 (vertical): tmp (dst_width x src_height) -> out (dst_width x
    // dst_height), rounding to bytes at the end.
    for (uint32_t dy = 0; dy < dst_height; ++dy) {
        const int32_t first = vw.first[dy];
        const int32_t cnt = vw.count[dy];
        const int32_t off = vw.offset[dy];
        for (uint32_t dx = 0; dx < dst_width; ++dx) {
            double acc[4] = {0.0, 0.0, 0.0, 0.0};
            for (int32_t k = 0; k < cnt; ++k) {
                int32_t sy = first + k;
                if (sy < 0) sy = 0;
                if (sy >= static_cast<int32_t>(src_height)) sy = static_cast<int32_t>(src_height) - 1;
                const float* t = &tmp[(static_cast<std::size_t>(sy) * dst_width + dx) * 4];
                const double weight = vw.weights[static_cast<std::size_t>(off) + k];
                for (int c = 0; c < 4; ++c) acc[c] += weight * static_cast<double>(t[c]);
            }
            uint32_t px = 0;
            for (int c = 0; c < 4; ++c) {
                const double v = detail::clamp_channel(acc[c]);
                px |= static_cast<uint32_t>(std::lround(v)) << (c * 8);
            }
            out[static_cast<std::size_t>(dy) * dst_width + dx] = px;
        }
    }

    return out;
}

}  // namespace beebium::server
