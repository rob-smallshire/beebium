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

#ifndef BEEBIUM_LINUX_APP_LIVE_STARVE_HPP
#define BEEBIUM_LINUX_APP_LIVE_STARVE_HPP

#include <QLocale>
#include <QString>

#include <cstdint>

namespace beebium::linux_client {

// When a live input has been failing to keep up, told once (client
// specification 9.0, Live input): fed the server's overrun and underrun
// counts once a second, it raises a condition when that count has grown in
// each of LIVE_STARVE_SECONDS consecutive seconds. A condition once raised,
// showing or dismissed, is not raised again until its count has stayed
// still for as long and then grows as long again; the other condition may
// be raised meanwhile, and replaces it. A new stream starts clean.
class LiveStarveDetector {
public:
    static constexpr int LIVE_STARVE_SECONDS = 3;

    // Underrun: samples made up as silence, the ring empty. Overrun:
    // samples dropped, the ring full.
    enum class Condition { none, underrun, overrun };

    // One second's counts, since the stream began; the condition this
    // second raises, if any.
    Condition feed(std::uint64_t overruns, std::uint64_t underruns);
    void reset();

private:
    struct Count {
        std::uint64_t last = 0;
        int growing = 0;  // consecutive seconds it grew
        int still = 0;    // consecutive seconds it did not
        bool armed = true;
        // Whether this second raises it.
        bool observe(std::uint64_t count);
    };
    Count overruns_;
    Count underruns_;
};

// The deck's sentence for a condition (client-authored, 3.4).
QString live_starve_sentence(LiveStarveDetector::Condition condition);
// The detail beneath it: the condition's count as it was when raised, in
// samples dropped, or in seconds of silence at the stream's rate.
QString live_starve_detail(LiveStarveDetector::Condition condition, std::uint64_t overruns,
                           std::uint64_t underruns, int sample_rate,
                           const QLocale& locale = QLocale());

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_LIVE_STARVE_HPP
