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

#include "AudioBuffer.hpp"
#include "extension/AudioSourcePort.hpp"

#include <cstddef>
#include <cstdint>

namespace beebium {

// Builds each AudioSample frame from the machine's audio sources and pushes it
// to the AudioBuffer. The sources own the encoding within their 32-bit fields;
// the assembler owns which field each source occupies.
//
// The SN76489 sets the frame rate: each of its output samples completes a
// frame, filling fields 0 (tone0, tone1) and 1 (tone2, noise). The remaining
// fields come from the sources registered on the AudioSourcePort, each asked
// for its value once per frame; a field with no source is silent.
//
// Owned by the hardware policy, which attaches it to the AudioBuffer when
// audio output is enabled. Runs on the emulation thread only.
class AudioFrameAssembler {
public:
    AudioFrameAssembler() = default;
    explicit AudioFrameAssembler(AudioBuffer& buffer) : buffer_(&buffer) {}
    explicit AudioFrameAssembler(AudioSourcePort& sources) : sources_(&sources) {}

    // Attach to the buffer frames are pushed to, or detach with nullptr.
    void attach(AudioBuffer* buffer) { buffer_ = buffer; }

    // Complete a frame with the SN76489's two fields and push it. Does
    // nothing while detached.
    void push_frame(uint32_t sn76489_field0, uint32_t sn76489_field1) {
        if (!buffer_) {
            return;
        }
        AudioSample sample;
        sample.sources[0] = sn76489_field0;
        sample.sources[1] = sn76489_field1;
        if (sources_) {
            for (std::size_t field = 2; field < AudioSample::MAX_SOURCES; ++field) {
                sample.sources[field] = sources_->take_field(field);
            }
        }
        buffer_->push(sample);
    }

private:
    AudioBuffer* buffer_ = nullptr;
    AudioSourcePort* sources_ = nullptr;
};

}  // namespace beebium
