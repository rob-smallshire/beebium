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

#include "app/LiveStarve.hpp"

#include <QCoreApplication>

namespace beebium::linux_client {

bool LiveStarveDetector::Count::observe(std::uint64_t count) {
    if (!seen) {
        seen = true;
        last = count;
        return false;
    }
    if (count > last) {
        ++growing;
        still = 0;
    } else {
        growing = 0;
        if (++still >= LIVE_STARVE_SECONDS) {
            armed = true;
        }
    }
    last = count;
    if (armed && growing >= LIVE_STARVE_SECONDS) {
        armed = false;
        return true;
    }
    return false;
}

LiveStarveDetector::Condition LiveStarveDetector::feed(std::uint64_t overruns,
                                                       std::uint64_t underruns) {
    const bool over = overruns_.observe(overruns);
    const bool under = underruns_.observe(underruns);
    // Both at once is rare; the silence is the one more often heard.
    return under ? Condition::underrun : over ? Condition::overrun : Condition::none;
}

void LiveStarveDetector::reset() {
    overruns_ = {};
    underruns_ = {};
}

QString live_starve_sentence(LiveStarveDetector::Condition condition) {
    switch (condition) {
    case LiveStarveDetector::Condition::underrun:
        return QCoreApplication::translate("LiveStarve",
                                           "The live input is not keeping up with the Beeb; some "
                                           "of the tape is being made up as silence.");
    case LiveStarveDetector::Condition::overrun:
        return QCoreApplication::translate("LiveStarve",
                                           "The live input is arriving faster than the Beeb can "
                                           "take it; some of the tape is being dropped.");
    case LiveStarveDetector::Condition::none:
        break;
    }
    return {};
}

QString live_starve_detail(LiveStarveDetector::Condition condition, std::uint64_t overruns,
                           std::uint64_t underruns, int sample_rate, const QLocale& locale) {
    const auto samples = [&](std::uint64_t n) {
        return n == 1 ? QCoreApplication::translate("LiveStarve", "1 sample")
                      : QCoreApplication::translate("LiveStarve", "%1 samples")
                            .arg(locale.toString(static_cast<qulonglong>(n)));
    };
    switch (condition) {
    case LiveStarveDetector::Condition::overrun:
        return QCoreApplication::translate("LiveStarve", "Dropped %1 since the stream began.")
            .arg(samples(overruns));
    case LiveStarveDetector::Condition::underrun:
        if (sample_rate <= 0) {
            return QCoreApplication::translate("LiveStarve",
                                               "Made up %1 of silence since the stream began.")
                .arg(samples(underruns));
        }
        return QCoreApplication::translate("LiveStarve",
                                           "Made up %1 s of silence since the stream began.")
            .arg(locale.toString(static_cast<double>(underruns) / sample_rate, 'f', 1));
    case LiveStarveDetector::Condition::none:
        break;
    }
    return {};
}

}  // namespace beebium::linux_client
