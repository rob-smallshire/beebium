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

#ifndef BEEBIUM_LINUX_METRICS_INTERVAL_STATS_HPP
#define BEEBIUM_LINUX_METRICS_INTERVAL_STATS_HPP

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace beebium::linux_client {

// Summary of a set of durations, in milliseconds.
struct IntervalSummary {
    std::size_t count = 0;
    double mean = 0.0;
    double stddev = 0.0;
    double min = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};

// Summarise samples (milliseconds); nearest-rank percentiles. Empty in,
// all-zero out.
IntervalSummary summarise(std::vector<double> samples_ms);

// One line: "n=.. mean=.. sd=.. min=.. p50=.. p95=.. p99=.. max=.. ms".
std::string format_summary(const IntervalSummary& summary);

// Counts per bucket of `bucket_ms` from zero, `bucket_count` buckets, the
// last also taking everything beyond. One line, empty buckets left out:
// "[16,17):3 [17,18):48 [40,inf):1".
std::string format_histogram(const std::vector<double>& samples_ms, double bucket_ms,
                             std::size_t bucket_count);

// Durations recorded from one thread and summarised from another, for frame
// pacing: frame inter-arrival times, receive-to-present latency.
class IntervalRecorder {
public:
    void record(double milliseconds) {
        std::lock_guard lock(mutex_);
        samples_.push_back(milliseconds);
    }

    // What was recorded since the last drain, forgotten here.
    std::vector<double> drain_samples() {
        std::vector<double> taken;
        std::lock_guard lock(mutex_);
        taken.swap(samples_);
        return taken;
    }

    // Summarise what was recorded since the last drain, and forget it.
    IntervalSummary drain() { return summarise(drain_samples()); }

private:
    std::mutex mutex_;
    std::vector<double> samples_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_METRICS_INTERVAL_STATS_HPP
