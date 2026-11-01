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

#include "metrics/ThreadCpu.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace beebium::linux_client {

std::optional<ThreadStat> parse_thread_stat(std::string_view line) {
    const auto open = line.find('(');
    const auto close = line.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open) {
        return std::nullopt;
    }
    ThreadStat stat;
    stat.name = std::string(line.substr(open + 1, close - open - 1));

    // After the name: field 3 (state) onwards; utime and stime are fields 14
    // and 15, so the 12th and 13th whitespace-separated tokens from here.
    std::istringstream rest{std::string(line.substr(close + 1))};
    std::string token;
    std::uint64_t utime = 0;
    std::uint64_t stime = 0;
    for (int field = 3; field <= 15; ++field) {
        if (!(rest >> token)) {
            return std::nullopt;
        }
        if (field == 14 || field == 15) {
            std::uint64_t value = 0;
            const auto [end, error] =
                std::from_chars(token.data(), token.data() + token.size(), value);
            if (error != std::errc{} || end != token.data() + token.size()) {
                return std::nullopt;
            }
            (field == 14 ? utime : stime) = value;
        }
    }
    stat.cpu_ticks = utime + stime;
    return stat;
}

std::vector<ThreadCpuShare> ThreadCpuSampler::sample() {
    std::vector<ThreadCpuShare> shares;
#if defined(__linux__)
    namespace fs = std::filesystem;
    const auto now = std::chrono::steady_clock::now();
    std::map<int, ThreadStat> current;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator("/proc/self/task", error)) {
        std::ifstream file(entry.path() / "stat");
        std::string line;
        if (!std::getline(file, line)) {
            continue;  // the thread ended between listing and reading
        }
        if (auto stat = parse_thread_stat(line)) {
            current[std::stoi(entry.path().filename().string())] = *stat;
        }
    }

    if (previous_at_) {
        const double elapsed_seconds =
            std::chrono::duration<double>(now - *previous_at_).count();
        const double ticks_per_second = static_cast<double>(sysconf(_SC_CLK_TCK));
        std::map<std::string, std::uint64_t> ticks_by_name;
        for (const auto& [tid, stat] : current) {
            const auto before = previous_.find(tid);
            const std::uint64_t base = before != previous_.end() ? before->second.cpu_ticks : 0;
            ticks_by_name[stat.name] += stat.cpu_ticks >= base ? stat.cpu_ticks - base : 0;
        }
        if (elapsed_seconds > 0.0) {
            for (const auto& [name, ticks] : ticks_by_name) {
                shares.push_back({name, 100.0 * static_cast<double>(ticks) /
                                            ticks_per_second / elapsed_seconds});
            }
        }
        std::sort(shares.begin(), shares.end(), [](const auto& a, const auto& b) {
            return a.percent_of_core > b.percent_of_core;
        });
    }
    previous_ = std::move(current);
    previous_at_ = now;
#endif
    return shares;
}

}  // namespace beebium::linux_client
