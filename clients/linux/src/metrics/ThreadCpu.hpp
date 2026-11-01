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

#ifndef BEEBIUM_LINUX_METRICS_THREAD_CPU_HPP
#define BEEBIUM_LINUX_METRICS_THREAD_CPU_HPP

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::linux_client {

// One line of /proc/<pid>/task/<tid>/stat, the fields we need.
struct ThreadStat {
    std::string name;            // comm, without the parentheses
    std::uint64_t cpu_ticks = 0; // utime + stime, in clock ticks
};

// Parse a stat line. The name may itself contain spaces and parentheses, so
// it runs to the LAST ')'. Nullopt for a line that is not one.
std::optional<ThreadStat> parse_thread_stat(std::string_view line);

// CPU use per thread name, as a percentage of one core, between two samples.
struct ThreadCpuShare {
    std::string name;
    double percent_of_core = 0.0;
};

// Samples this process's threads (Linux /proc); elsewhere reports nothing.
// Threads sharing a name are added together, so a pool reads as one line.
class ThreadCpuSampler {
public:
    // Take a sample; returns each thread name's share since the previous
    // sample, busiest first. The first call only primes the sampler.
    std::vector<ThreadCpuShare> sample();

private:
    std::map<int, ThreadStat> previous_;
    std::optional<std::chrono::steady_clock::time_point> previous_at_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_METRICS_THREAD_CPU_HPP
