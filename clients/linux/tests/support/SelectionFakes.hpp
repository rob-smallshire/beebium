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

#ifndef BEEBIUM_LINUX_TESTS_SELECTION_FAKES_HPP
#define BEEBIUM_LINUX_TESTS_SELECTION_FAKES_HPP

// Stand-ins for what a screen text selection asks of the window: the
// server's hold and reads, the display, the machine's keys and the
// clipboard, each recording what it was asked and answering when drained.

#include "app/SelectionCoordinator.hpp"

#include "video.pb.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace beebium::linux_client::testing {

inline ScreenContent filling_content() {
    ScreenContent content;
    content.width = 640;
    content.height = 256;
    content.display_width = 640;
    content.display_height = 256;
    content.regions = {{0, 256, 640}};
    return content;
}

// Fills the view exactly at square pixels: 1250 by 1000 for 640 by 512.
inline ScreenPresentation filling(double width = 1250, double height = 1000) {
    ScreenPresentation presentation;
    presentation.width = width;
    presentation.height = height;
    presentation.settings.pixels = PixelShape::crisp;
    presentation.settings.edge_margin_percent = 0;
    return presentation;
}

inline ViewPoint at(double u, double v) {
    return {u * 1250, v * 1000};
}

inline ScreenBand plain_band() {
    ScreenBand band;
    band.bottom = 256;
    band.cell_width = 8;
    band.cell_height = 8;
    band.column_pitch = 8;
    band.row_pitch = 8;
    return band;
}

struct Fakes {
    bool connected = true;
    std::optional<ScreenContent> live = filling_content();
    bool refuse_hold = false;
    std::uint64_t next_hold = 42;
    std::vector<ScreenBand> bands{plain_band()};
    bool hold_frame = true;
    ScreenReading reading = [] {
        ScreenReading r;
        r.supported = true;
        r.text = "HELLO";
        return r;
    }();
    std::optional<ScreenReadFailure> read_failure;
    Mode7Characters mode7 = Mode7Characters::codes;

    int holds = 0;
    std::vector<std::uint64_t> released;
    std::vector<beebium::GetScreenTextRequest> reads;
    std::deque<SelectionServices::HoldDone> pending_holds;
    std::deque<std::function<void()>> pending_reads;
    int frozen = 0;
    int resumed = 0;
    std::vector<std::uint32_t> shown;  // the stills' widths
    int keys_released = 0;
    std::vector<std::string> clipboard;
    int nothing_to_copy = 0;
    std::vector<std::string> failures;
    int holds_lost = 0;
    int changes = 0;

    SelectionServices services() {
        SelectionServices s;
        s.connected = [this] { return connected; };
        s.hold = [this](SelectionServices::HoldDone done) {
            pending_holds.push_back(std::move(done));
        };
        s.read = [this](const beebium::GetScreenTextRequest& request, SelectionServices::ReadDone done) {
            reads.push_back(request);
            // Answered with the reading scripted when the call was made.
            pending_reads.push_back([done = std::move(done), reading = reading,
                                     failure = read_failure] {
                if (failure) {
                    done(*failure);
                } else {
                    done(reading);
                }
            });
        };
        s.release = [this](std::uint64_t id) { released.push_back(id); };
        s.live_content = [this] { return live; };
        s.freeze = [this] { ++frozen; };
        s.show_still = [this](const DecodedFrame& still) { shown.push_back(still.width); };
        s.resume = [this] { ++resumed; };
        s.release_machine_keys = [this] { ++keys_released; };
        s.mode7_characters = [this] { return mode7; };
        s.write_clipboard = [this](const std::string& text) { clipboard.push_back(text); };
        s.nothing_to_copy = [this] { ++nothing_to_copy; };
        s.copy_failed = [this](const std::string& message) { failures.push_back(message); };
        s.hold_lost = [this] { ++holds_lost; };
        s.changed = [this] { ++changes; };
        return s;
    }

    // Answer every call made so far, and those the answers make.
    void drain() {
        while (!pending_holds.empty() || !pending_reads.empty()) {
            while (!pending_holds.empty()) {
                auto done = std::move(pending_holds.front());
                pending_holds.pop_front();
                if (refuse_hold) {
                    done(std::nullopt);
                    continue;
                }
                ++holds;
                HeldScreen held;
                held.hold_id = next_hold;
                held.grid.bands = bands;
                if (hold_frame) {
                    held.frame = std::make_unique<DecodedFrame>();
                    held.frame->width = 640;
                    held.frame->height = 256;
                    held.frame->display_width = 640;
                    held.frame->display_height = 256;
                    held.frame->regions = {{0, 256, 640}};
                }
                done(std::move(held));
            }
            while (!pending_reads.empty()) {
                auto answer = std::move(pending_reads.front());
                pending_reads.pop_front();
                answer();
            }
        }
    }
};


}  // namespace beebium::linux_client::testing

#endif  // BEEBIUM_LINUX_TESTS_SELECTION_FAKES_HPP
