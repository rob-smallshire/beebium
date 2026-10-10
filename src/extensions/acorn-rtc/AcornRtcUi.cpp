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

#include "AcornRtcUi.hpp"

#include "AcornRtcExtension.hpp"

#include "extension_ui.pb.h"

#include <chrono>
#include <cstdio>

namespace beebium {

namespace {

constexpr const char* CONTROL_TIME = "time";

// Pause between background revision bumps. The chip is minute-
// granular, so half-minute gives at-most-30s lag against the actual
// wall-clock time without spamming the poll loop.
constexpr std::chrono::seconds kTickInterval{30};

}  // namespace

AcornRtcUi::AcornRtcUi(AcornRtcExtension& ext) : ext_(ext) {
    worker_ = std::thread([this] { worker_loop(); });
}

AcornRtcUi::~AcornRtcUi() {
    {
        std::lock_guard lk(mu_);
        stopping_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::string AcornRtcUi::format_datetime(int year, int month, int day,
                                        int hour, int minute) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
                  year, month, day, hour, minute);
    return std::string(buf);
}

std::string AcornRtcUi::format_layout(RegisterLayout layout) {
    const auto [first_year, last_year] = Saf3019p::year_range(layout);
    const std::string years = std::to_string(first_year) + "-" + std::to_string(last_year);
    switch (layout) {
        case RegisterLayout::FourBitYearInR1:
            return "4-bit year in R1 (Acorn original, " + years + ")";
        case RegisterLayout::SevenBitYearInR7:
            return "7-bit year in R7 (L3 v1.26, " + years + ")";
        case RegisterLayout::SevenBitYearInR1R5:
            return "7-bit year in R1+R5 (BeebMaster Y2KFIX, " + years + ")";
    }
    return "";  // unreachable; switch above is exhaustive
}

void AcornRtcUi::build_view(View* out) const {
    // Bring the chip's registers up to wall-clock time before
    // reading. The chip is thread-safe internally and idempotent
    // when no time has elapsed since the last call -- this races
    // harmlessly with both BBC user-port reads and the worker's
    // own mark_dirty cycle.
    //
    // Calling a non-const method from a const member function is
    // legal here because ext_ is a non-const reference (the const
    // on build_view applies to *this, not to what ext_ refers to).
    ext_.chip().advance_counters();
    auto dt = ext_.chip().current_datetime();
    auto layout = ext_.chip().layout();

    auto* root = out->mutable_root();
    root->set_id("root");
    auto* group = root->mutable_group();

    auto* time_ctrl = group->add_controls();
    time_ctrl->set_id(CONTROL_TIME);
    time_ctrl->mutable_label()->set_text(
        "Time: " + format_datetime(dt.year, dt.month, dt.day,
                                   dt.hour, dt.minute));
    time_ctrl->set_tooltip(format_layout(layout));
}

void AcornRtcUi::handle_event(const DispatchRequest& /*request*/) {
    // No interactive controls today. Future slices that add a Button
    // for set/freeze time will dispatch here.
}

void AcornRtcUi::worker_loop() {
    while (true) {
        std::unique_lock lk(mu_);
        cv_.wait_for(lk, kTickInterval, [this] {
            return stopping_.load(std::memory_order_acquire);
        });
        if (stopping_.load(std::memory_order_acquire)) {
            break;
        }
        lk.unlock();
        // Pure mark_dirty. The actual chip advance and time read are
        // done by build_view at push time; we just nudge the poll
        // loop into re-pushing.
        mark_dirty();
    }
}

}  // namespace beebium
