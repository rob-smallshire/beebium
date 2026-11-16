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

#pragma once

#include "TapeSignal.hpp"

namespace beebium {

// Where the Serial ULA's cassette output goes: the edges of its tone
// synthesiser, in ULA clocks of the machine's own count, the same kind of
// signal a TapeSignal delivers. A recorder is a sink; with none attached the
// edges are dropped. Called on the emulation thread.
class TapeSink {
public:
    virtual ~TapeSink() = default;

    // A transition of the cassette output at `time`; `level` is the level after it.
    virtual void edge(TapeTime time, bool level) = 0;

    // The output is silent until `time`.
    virtual void silence_until(TapeTime time) = 0;
};

}  // namespace beebium
