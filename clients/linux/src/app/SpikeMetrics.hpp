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

#ifndef BEEBIUM_LINUX_APP_SPIKE_METRICS_HPP
#define BEEBIUM_LINUX_APP_SPIKE_METRICS_HPP

#include "metrics/ThreadCpu.hpp"

#include <QObject>
#include <QTimer>

#include <cstdint>

namespace beebium::linux_client {

class MainWindow;

// Prints frame pacing and per-thread CPU to stderr at a fixed interval: the
// measurements the phase 0 rendering spike exists to take.
class SpikeMetrics : public QObject {
    Q_OBJECT

public:
    SpikeMetrics(MainWindow& window, int interval_seconds, QObject* parent = nullptr);

private:
    void report();

    MainWindow& window_;
    QTimer timer_;
    ThreadCpuSampler cpu_;
    std::uint64_t superseded_before_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SPIKE_METRICS_HPP
