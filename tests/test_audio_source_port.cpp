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

#include <beebium/AudioFrameAssembler.hpp>
#include <beebium/Machines.hpp>
#include <beebium/extension/AudioSourcePort.hpp>
#include <beebium/extension/ExtensionContext.hpp>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <type_traits>
#include <vector>

using namespace beebium;

namespace {

struct CountingSource : AudioFieldSource {
    uint32_t value = 0;
    int calls = 0;
    uint32_t take_field() override {
        ++calls;
        return value;
    }
};

AudioFieldInfo music5000_info() {
    AudioFieldInfo info;
    info.name = "Music5000";
    info.encoding = AudioFieldEncoding::Signed2x16;
    info.channel_names = {"L", "R"};
    info.group_name = "Music 5000";
    info.bipolar = true;
    return info;
}

}  // namespace

TEST_CASE("AudioSourcePort reserves the SN76489's fields", "[audio][audio-source-port]") {
    AudioSourcePort port;

    REQUIRE(port.is_field_taken(0));
    REQUIRE(port.is_field_taken(1));
    REQUIRE_FALSE(port.is_field_taken(2));
    REQUIRE_FALSE(port.is_field_taken(3));
    REQUIRE_FALSE(port.is_field_taken(4));

    const AudioFieldInfo* lo = port.field_info(0);
    const AudioFieldInfo* hi = port.field_info(1);
    REQUIRE(lo != nullptr);
    REQUIRE(hi != nullptr);
    REQUIRE(lo->name == "SN76489");
    REQUIRE(lo->channel_names == std::vector<std::string>{"1", "2"});
    REQUIRE(hi->channel_names == std::vector<std::string>{"3", "0"});
    REQUIRE_FALSE(lo->bipolar);
    REQUIRE(lo->group_name == "Internal Sound");
    REQUIRE(port.field_info(2) == nullptr);
}

TEST_CASE("AudioSourcePort registers a source and refuses taken fields",
          "[audio][audio-source-port]") {
    AudioSourcePort port;
    CountingSource source;
    CountingSource other;

    port.register_source(2, source, music5000_info());
    REQUIRE(port.is_field_taken(2));
    REQUIRE(port.field_info(2)->name == "Music5000");
    REQUIRE(port.field_info(2)->bipolar);

    REQUIRE_THROWS_AS(port.register_source(2, other, music5000_info()), std::runtime_error);
    REQUIRE_THROWS_AS(port.register_source(0, other, music5000_info()), std::runtime_error);
    REQUIRE_THROWS_AS(port.register_source(1, other, music5000_info()), std::runtime_error);
    REQUIRE_THROWS_AS(port.register_source(4, other, music5000_info()), std::runtime_error);
}

TEST_CASE("AudioFrameAssembler takes registered fields once per frame",
          "[audio][audio-source-port]") {
    AudioSourcePort port;
    CountingSource field3;
    field3.value = 0xCAFEF00D;
    port.register_source(3, field3, music5000_info());

    AudioBuffer buffer(16);
    AudioFrameAssembler frames(port);

    SECTION("not while detached") {
        frames.push_frame(1, 2);
        REQUIRE(field3.calls == 0);
    }

    SECTION("into its own field, leaving unregistered fields silent") {
        frames.attach(&buffer);
        frames.push_frame(1, 2);
        frames.push_frame(3, 4);
        REQUIRE(field3.calls == 2);

        std::array<AudioSample, 2> samples;
        REQUIRE(buffer.read(samples.data(), samples.size()) == 2);
        REQUIRE(samples[0].sources[0] == 1);
        REQUIRE(samples[0].sources[1] == 2);
        REQUIRE(samples[0].sources[2] == 0);
        REQUIRE(samples[0].sources[3] == 0xCAFEF00D);
        REQUIRE(samples[1].sources[0] == 3);
        REQUIRE(samples[1].sources[3] == 0xCAFEF00D);
    }
}

TEST_CASE("ExtensionContext offers the audio source port only when given one",
          "[audio][audio-source-port]") {
    ExtensionContext without;
    REQUIRE_FALSE(without.has<AudioSourcePort>());
    REQUIRE_THROWS_AS(without.get<AudioSourcePort>(), std::runtime_error);

    AudioSourcePort port;
    ExtensionContext with(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &port);
    REQUIRE(with.has<AudioSourcePort>());
    REQUIRE(&with.get<AudioSourcePort>() == &port);
}

TEMPLATE_TEST_CASE("A registered source reaches the machine's audio frames",
                   "[audio][audio-source-port]",
                   ModelB, ModelBPlus, ModelBPlus128K, ModelBRomRamBoard,
                   ModelBAtplSidewise, ModelBWatfordRomRam, ModelBIntegraB) {
    TestType machine;
    std::vector<uint8_t> mos(16384, 0xEA);  // NOPs, reset vector at &0400
    mos[0x3FFC] = 0x00;
    mos[0x3FFD] = 0x04;
    machine.memory().load_mos(mos.data(), mos.size());
    machine.reset();

    STATIC_REQUIRE(HasAudioSourcePort<std::remove_reference_t<decltype(machine.memory())>>);
    CountingSource source;
    source.value = AudioSample::field_2x16bit(-1234, 1234);
    machine.memory().audio_source_port().register_source(2, source, music5000_info());

    // The port accepts registration with audio output disabled, and nothing
    // is taken until output is enabled.
    machine.run(2000);
    REQUIRE(source.calls == 0);

    machine.memory().enable_audio_output();
    machine.run(20000);  // 10 ms of 2 MHz cycles: 480 frames at 48 kHz

    std::vector<AudioSample> samples(1024);
    size_t count = machine.memory().audio_buffer->read(samples.data(), samples.size());
    REQUIRE(count >= 479);
    REQUIRE(count <= 481);
    REQUIRE(source.calls == static_cast<int>(count));
    REQUIRE(std::all_of(samples.begin(), samples.begin() + count, [&](const AudioSample& s) {
        return s.sources[2] == source.value && s.sources[3] == 0;
    }));
}
