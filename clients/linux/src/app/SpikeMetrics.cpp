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

#include "app/SpikeMetrics.hpp"

#include "app/DisplayWidget.hpp"
#include "app/MachineSession.hpp"
#include "app/MainWindow.hpp"

#include <cstdio>

namespace beebium::linux_client {

SpikeMetrics::SpikeMetrics(MainWindow& window, int interval_seconds, QObject* parent)
    : QObject(parent), window_(window) {
    cpu_.sample();
    timer_.setInterval(interval_seconds * 1000);
    connect(&timer_, &QTimer::timeout, this, &SpikeMetrics::report);
    timer_.start();
}

void SpikeMetrics::report() {
    auto& session = window_.session();
    auto& display = window_.display();

    std::fprintf(stderr, "== %s\n", qPrintable(display.graphics_description()));
    if (auto* video = session.video_stream()) {
        std::fprintf(stderr, "arrival interval    %s\n",
                     format_summary(video->arrival_intervals().drain()).c_str());
    }
    std::fprintf(stderr, "render interval     %s\n",
                 format_summary(display.render_intervals().drain()).c_str());
    std::fprintf(stderr, "receive to render   %s\n",
                 format_summary(display.receive_to_render().drain()).c_str());

    const std::uint64_t superseded = session.mailbox().superseded_count();
    std::fprintf(stderr, "frames dropped      %llu\n",
                 static_cast<unsigned long long>(superseded - superseded_before_));
    superseded_before_ = superseded;

    double total = 0.0;
    std::string threads;
    for (const auto& share : cpu_.sample()) {
        total += share.percent_of_core;
        if (share.percent_of_core >= 0.5) {
            char item[80];
            std::snprintf(item, sizeof(item), " %s=%.1f", share.name.c_str(),
                          share.percent_of_core);
            threads += item;
        }
    }
    std::fprintf(stderr, "cpu %% of one core   total=%.1f |%s\n", total, threads.c_str());
}

}  // namespace beebium::linux_client
