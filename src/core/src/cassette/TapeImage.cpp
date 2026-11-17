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

#include "beebium/cassette/TapeImage.hpp"

#include "beebium/cassette/CswTape.hpp"
#include "beebium/cassette/SampledTape.hpp"
#include "beebium/cassette/UefTape.hpp"

#include "Inflate.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace beebium {

namespace {

bool starts_with(std::span<const uint8_t> bytes, const char* text, size_t length) {
    return bytes.size() >= length && std::memcmp(bytes.data(), text, length) == 0;
}

bool is_wav(std::span<const uint8_t> bytes) {
    return bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
           std::memcmp(bytes.data() + 8, "WAVE", 4) == 0;
}

std::unique_ptr<TapeSignal> open_wav(std::unique_ptr<ByteReader> reader, const std::string& name,
                                     std::optional<WavChannel> channel) {
    auto source = WavSource::open(std::move(reader), name, channel);
    const bool stereo = source->channels() == 2;
    const WavChannel played = source->channel();
    auto tape = std::make_unique<SampledTape>(std::move(source), TapeKind::Wav);
    tape->mutable_metadata().channel = !stereo ? "" : (played == WavChannel::Left ? "left" : "right");
    return tape;
}

}  // namespace

std::unique_ptr<TapeSignal> open_tape_image(std::span<const uint8_t> bytes, const std::string& name,
                                            std::optional<WavChannel> channel) {
    if (is_wav(bytes)) return open_wav(ByteReader::of_memory(bytes), name, channel);
    if (starts_with(bytes, "UEF File!", 10) || cassette_detail::has_gzip_magic(bytes)) {
        // A gzipped file is a UEF if anything: UefTape inflates it and says
        // so if it is not.
        return UefTape::open(bytes, name);
    }
    if (starts_with(bytes, "Compressed Square Wave\x1A", 23)) {
        return CswTape::open(bytes, name);
    }
    throw TapeImageError(TapeImageError::Kind::UnsupportedFormat,
                         "The file " + name + " is not a UEF, CSW or WAV tape image.");
}

std::unique_ptr<TapeSignal> open_tape_file(const std::filesystem::path& filepath,
                                           std::optional<WavChannel> channel) {
    const std::string name = filepath.filename().string();
    std::error_code ec;
    if (!std::filesystem::exists(filepath, ec)) {
        throw TapeImageError(TapeImageError::Kind::CannotOpen,
                             "The tape image " + name + " could not be found.");
    }
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw TapeImageError(TapeImageError::Kind::CannotOpen, "The tape image " + name + " could not be opened.");
    }
    // A WAV is read on demand: it may be very long.
    uint8_t head[12] = {};
    file.read(reinterpret_cast<char*>(head), sizeof(head));
    if (file.gcount() == sizeof(head) && is_wav(head)) {
        file.close();
        auto reader = ByteReader::of_file(filepath);
        if (!reader) {
            throw TapeImageError(TapeImageError::Kind::CannotOpen, "The tape image " + name + " could not be opened.");
        }
        return open_wav(std::move(reader), name, channel);
    }
    file.clear();
    file.seekg(0);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        throw TapeImageError(TapeImageError::Kind::Unreadable, "The file " + name + " is empty.");
    }
    return open_tape_image(bytes, name, channel);
}

}  // namespace beebium
