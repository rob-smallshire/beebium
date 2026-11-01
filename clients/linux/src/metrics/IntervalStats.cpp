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

#include "metrics/IntervalStats.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace beebium::linux_client {

namespace {

double nearest_rank(const std::vector<double>& sorted, double percentile) {
    const auto rank = static_cast<std::size_t>(
        std::ceil(percentile / 100.0 * static_cast<double>(sorted.size())));
    return sorted[std::clamp<std::size_t>(rank, 1, sorted.size()) - 1];
}

}  // namespace

IntervalSummary summarise(std::vector<double> samples_ms) {
    IntervalSummary summary;
    if (samples_ms.empty()) {
        return summary;
    }
    std::sort(samples_ms.begin(), samples_ms.end());
    summary.count = samples_ms.size();
    const double total = std::accumulate(samples_ms.begin(), samples_ms.end(), 0.0);
    summary.mean = total / static_cast<double>(summary.count);
    double squares = 0.0;
    for (double sample : samples_ms) {
        squares += (sample - summary.mean) * (sample - summary.mean);
    }
    summary.stddev = std::sqrt(squares / static_cast<double>(summary.count));
    summary.min = samples_ms.front();
    summary.max = samples_ms.back();
    summary.p50 = nearest_rank(samples_ms, 50);
    summary.p95 = nearest_rank(samples_ms, 95);
    summary.p99 = nearest_rank(samples_ms, 99);
    return summary;
}

std::string format_summary(const IntervalSummary& summary) {
    char text[200];
    std::snprintf(text, sizeof(text),
                  "n=%zu mean=%.2f sd=%.2f min=%.2f p50=%.2f p95=%.2f p99=%.2f max=%.2f ms",
                  summary.count, summary.mean, summary.stddev, summary.min, summary.p50,
                  summary.p95, summary.p99, summary.max);
    return text;
}

std::string format_histogram(const std::vector<double>& samples_ms, double bucket_ms,
                             std::size_t bucket_count) {
    if (bucket_count == 0 || bucket_ms <= 0.0) {
        return {};
    }
    std::vector<std::size_t> counts(bucket_count, 0);
    for (double sample : samples_ms) {
        const double index = std::floor(std::max(sample, 0.0) / bucket_ms);
        ++counts[std::min(static_cast<std::size_t>(index), bucket_count - 1)];
    }
    std::string text;
    char item[64];
    for (std::size_t i = 0; i < bucket_count; ++i) {
        if (counts[i] == 0) {
            continue;
        }
        const double low = static_cast<double>(i) * bucket_ms;
        if (i + 1 == bucket_count) {
            std::snprintf(item, sizeof(item), "%s[%g,inf):%zu", text.empty() ? "" : " ", low,
                          counts[i]);
        } else {
            std::snprintf(item, sizeof(item), "%s[%g,%g):%zu", text.empty() ? "" : " ", low,
                          low + bucket_ms, counts[i]);
        }
        text += item;
    }
    return text;
}

}  // namespace beebium::linux_client
