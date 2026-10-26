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

#include "HybridMusic5000.hpp"

#include <beebium/AudioBuffer.hpp>
#include <beebium/extension/OneMHzBusPort.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace beebium {

namespace {

struct UnitWiring {
    uint8_t first_page;
    uint8_t last_page;
    std::size_t field_index;
    const char* source_name;
    const char* group_name;
    const char* group_description;
};

// The Music 5000 answers to page nibble 3 and the Music 3000 to nibble 5
// (hardware-model.md 1.1, 8); each fills its own audio frame field.
UnitWiring wiring_for(HybridMusic5000::Unit unit) {
    if (unit == HybridMusic5000::Unit::Music3000) {
        return {0x50, 0x5F, 3, "Music3000", "Music 3000",
                "Hybrid Music 3000 expander, sixteen more channels"};
    }
    return {0x30, 0x3F, 2, "Music5000", "Music 5000",
            "Hybrid Music 5000 synthesiser, sixteen channels"};
}

}  // namespace

HybridMusic5000::HybridMusic5000() = default;
HybridMusic5000::~HybridMusic5000() = default;

std::unique_ptr<HybridMusic5000> HybridMusic5000::create() {
    auto ext = std::make_unique<HybridMusic5000>();
    ext->set_manifest(ExtensionManifest{
        .name = "hybrid-music-5000",
        .display_name = "Hybrid Music 5000",
        .description = "The Hybrid Music 5000 synthesiser (or Music 3000 expander) on the 1 MHz bus",
        .library_stem = "hybrid-music-5000",
    });
    return ext;
}

HybridMusic5000::Unit HybridMusic5000::unit() const {
    const auto value = config_value("unit");
    if (!value || value->empty() || *value == "music-5000") {
        return Unit::Music5000;
    }
    if (*value == "music-3000") {
        return Unit::Music3000;
    }
    throw std::runtime_error("Hybrid Music 5000: unknown unit '" + std::string(*value)
                             + "' (expected music-5000 or music-3000)");
}

void HybridMusic5000::init(ExtensionContext& ctx) {
    const UnitWiring wiring = wiring_for(unit());
    ctx.get<OneMHzBusPort>().claim_jim_pages(wiring.first_page, wiring.last_page, *this);

    if (ctx.has<AudioSourcePort>()) {
        AudioFieldInfo info;
        info.name = wiring.source_name;
        info.encoding = AudioFieldEncoding::Signed2x16;
        info.channel_names = {"Left", "Right"};
        info.group_name = wiring.group_name;
        info.group_description = wiring.group_description;
        info.bipolar = true;
        ctx.get<AudioSourcePort>().register_source(wiring.field_index, *this, std::move(info));
    }
}

void HybridMusic5000::tick() {
    // 46875 Hz is 3/64 of the 1 MHz bus clock (hardware-model.md 2).
    tick_accumulator_ += 3;
    while (tick_accumulator_ >= 64) {
        tick_accumulator_ -= 64;
        int32_t left = 0;
        int32_t right = 0;
        synth_.render_frame(left, right);
        ++frames_rendered_;

        // The analogue output filter, then 46875 to 48000 Hz. The two sides'
        // resamplers run in lockstep, so their outputs pair up.
        std::array<double, 2> left_out{};
        std::array<double, 2> right_out{};
        std::size_t left_count = 0;
        std::size_t right_count = 0;
        resamplers_[0].push(filters_[0].process(left),
                            [&](double y) { left_out[left_count++] = y; });
        resamplers_[1].push(filters_[1].process(right),
                            [&](double y) { right_out[right_count++] = y; });
        for (std::size_t i = 0; i < left_count; ++i) {
            produce(AudioSample::field_2x16bit(to_int16(left_out[i]), to_int16(right_out[i])));
        }
    }
}

void HybridMusic5000::produce(uint32_t field) {
    if (ring_count_ == kRingSize) {
        // Nobody has been taking samples (audio output disabled), or the
        // consumer stalled: drop the oldest.
        ring_read_ = (ring_read_ + 1) % kRingSize;
        --ring_count_;
        if (delivering_) {
            ++overruns_;
            delivering_ = false;  // re-prime on the next take
        }
    }
    ring_[(ring_read_ + ring_count_) % kRingSize] = field;
    ++ring_count_;
    ++samples_produced_;
}

uint32_t HybridMusic5000::take_field() {
    if (!delivering_) {
        if (ring_count_ < kPrimeLevel) {
            return last_field_;
        }
        // Start from the prime level, discarding anything older (a full
        // ring left while audio output was disabled).
        while (ring_count_ > kPrimeLevel) {
            ring_read_ = (ring_read_ + 1) % kRingSize;
            --ring_count_;
        }
        delivering_ = true;
    }
    if (ring_count_ == 0) {
        ++underruns_;
        delivering_ = false;
        return last_field_;
    }
    last_field_ = ring_[ring_read_];
    ring_read_ = (ring_read_ + 1) % kRingSize;
    --ring_count_;
    return last_field_;
}

int16_t HybridMusic5000::to_int16(double current) {
    const double value = std::round(current * 32767.0 / kFullScaleCurrent);
    if (value > 32767.0) return 32767;
    if (value < -32768.0) return -32768;
    return static_cast<int16_t>(value);
}

}  // namespace beebium
