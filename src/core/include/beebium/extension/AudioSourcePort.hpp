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

#ifndef BEEBIUM_EXTENSION_AUDIO_SOURCE_PORT_HPP
#define BEEBIUM_EXTENSION_AUDIO_SOURCE_PORT_HPP

#include "Export.hpp"
#include "../AudioBuffer.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace beebium {

// How a source's 32-bit frame field is divided into channels. Mirrors the
// SourceEncoding of audio.proto, which the AudioService maps it to.
enum class AudioFieldEncoding {
    Signed4x8,    // 4 x int8
    Signed2x16,   // 2 x int16, first channel in the low half
    Signed1x32,   // 1 x int32
    Unsigned4x8,  // 4 x uint8
};

// What a client needs to know to play one frame field.
struct AudioFieldInfo {
    std::string name;                        // e.g. "SN76489", "Music5000"
    AudioFieldEncoding encoding = AudioFieldEncoding::Signed2x16;
    std::vector<std::string> channel_names;  // one per channel in the field
    std::string group_name;                  // sources sharing a name form one group
    std::string group_description;
    std::string group_color;                 // optional UI hint, e.g. "#FF6B35"
    // True for a signed source whose zero is silence (e.g. the Music 5000);
    // false for a unipolar source whose DC the consumer must remove (the
    // SN76489).
    bool bipolar = true;
};

// A sound source that fills one field of every audio frame.
//
// The frame assembler calls take_field() once per frame, at the frame rate
// set by the SN76489 (48 kHz), on the emulation thread. A source with no new
// sample ready returns the value it returned last, as a hardware DAC holds its
// output. take_field() is not called while audio output is disabled, so a
// source that buffers samples must tolerate not being drained.
struct BEEBIUM_EXT_API AudioFieldSource {
    virtual ~AudioFieldSource() = default;
    virtual uint32_t take_field() = 0;
};

// Port handle through which extensions add sound sources to the machine's
// audio frames (ExtensionContext::get<AudioSourcePort>()).
//
// Each frame has AudioSample::MAX_SOURCES 32-bit fields. Fields 0 and 1 belong
// to the SN76489 and are reserved from construction; an extension registers
// one AudioFieldSource per further field it fills, naming the field index. The set
// of sources is fixed once the machine's extensions are initialised.
class BEEBIUM_EXT_API AudioSourcePort {
public:
    static constexpr std::size_t kFieldCount = AudioSample::MAX_SOURCES;

    AudioSourcePort();

    // Register a source for a frame field. Throws std::runtime_error if the
    // index is out of range or the field is already taken.
    void register_source(std::size_t field_index, AudioFieldSource& source, AudioFieldInfo info);

    // Whether a field has a source (the SN76489's fields always do).
    bool is_field_taken(std::size_t field_index) const;

    // The metadata for a field, or nullptr for a silent field.
    const AudioFieldInfo* field_info(std::size_t field_index) const;

    // The value of a registered extension source's field for the next frame,
    // or 0 for a field with no extension source.
    uint32_t take_field(std::size_t field_index) {
        AudioFieldSource* source = sources_[field_index];
        return source ? source->take_field() : 0;
    }

private:
    std::array<AudioFieldSource*, kFieldCount> sources_{};
    std::array<bool, kFieldCount> taken_{};
    std::array<AudioFieldInfo, kFieldCount> infos_{};
};

// Concept to detect hardware that offers an audio source port.
template<typename T>
concept HasAudioSourcePort = requires(T& hw) {
    { hw.audio_source_port() } -> std::convertible_to<AudioSourcePort&>;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_AUDIO_SOURCE_PORT_HPP
