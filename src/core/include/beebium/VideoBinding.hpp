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
template<typename Hardware>
struct VideoBinding {
    Hardware& hardware;
    VideoRenderer<decltype(hardware.main_ram)> renderer;

    explicit VideoBinding(Hardware& hw)
        : hardware(hw)
        , renderer(hw.main_ram, hw.addressable_latch, hw.video_ula, hw.saa5050, hw.video_output)
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

        // Render pixels only if video output enabled
        if (hardware.video_output.has_value()) {
            renderer.render(output);
        }
    }

    void reset() {
        renderer.reset();
    }
};

} // namespace beebium
