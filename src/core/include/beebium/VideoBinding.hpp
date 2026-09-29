// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include "ClockTypes.hpp"
#include "VideoRenderer.hpp"

namespace beebium {

// VideoBinding wraps CRTC + video rendering for clock subscription.
//
// The video system operates on the falling edge at a dynamic rate
// determined by the CRTC (1MHz or 2MHz depending on video mode).
//
// Key behaviors:
// - CRTC is always ticked (needed for VSYNC timing to system VIA)
// - VSYNC is always updated to system VIA peripheral
// - Pixel rendering only occurs if video_output is enabled
//
// The Hardware type must provide:
// - peek_video(addr): Read from currently configured video RAM
// - crtc, video_ula, saa5050, addressable_latch: Video device references
// - video_output: Optional output queue
// - system_via_peripheral: For VSYNC signaling
//
template<typename Hardware>
struct VideoBinding {
    Hardware& hardware;
    VideoRenderer<Hardware> renderer;

    explicit VideoBinding(Hardware& hw)
        : hardware(hw)
        , renderer(hw)
    {}

    static constexpr ClockEdge clock_edges = ClockEdge::Falling;

    // Dynamic rate from CRTC
    ClockRate clock_rate() const { return hardware.crtc.clock_rate(); }

    void tick_falling() {
        // Set CRTC clock rate based on video ULA mode
        hardware.crtc.set_fast_clock(hardware.video_ula.fast_clock());

        // Tick CRTC to advance timing state
        auto output = hardware.crtc.tick();

        // Always update VSYNC for system VIA timing (CA1 line)
        hardware.system_via_peripheral.set_vsync(output.vsync != 0);

        // Note where the beam is: each vsync rising edge starts a field.
        if (output.vsync && !last_vsync_) {
            last_vsync_cycle_ = cycle_source_ ? *cycle_source_ : 0;
            // odd_field toggles at the end of the vertical displayed area, so
            // at vsync it already names the field that is starting.
            field_odd_ = output.odd_field != 0;
        }
        last_vsync_ = output.vsync != 0;

        // Render pixels only if video output enabled
        if (hardware.video_output.has_value()) {
            renderer.render(output);
        }
    }

    void reset() {
        renderer.reset();
    }

    // The machine's cycle counter, read to note when each field starts.
    void set_cycle_source(const uint64_t* cycle_count) {
        cycle_source_ = cycle_count;
        renderer.set_cycle_source(cycle_count);
    }

    // The emulated cycle of the latest vsync rising edge (the start of the
    // field in progress), or 0 if there has been none.
    uint64_t last_vsync_cycle() const { return last_vsync_cycle_; }

    // Whether the field in progress is the odd (first) field of an
    // interlaced frame. Meaningful only when the CRTC interlaces (R8 bit 0).
    bool field_odd() const { return field_odd_; }

private:
    const uint64_t* cycle_source_ = nullptr;
    uint64_t last_vsync_cycle_ = 0;
    bool last_vsync_ = false;
    bool field_odd_ = true;
};

} // namespace beebium
