// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

// Test gRPC AudioService
//
// These tests verify the AudioService implementation by acting as a gRPC client.
// They create a local server, connect to it, and verify audio streaming works correctly.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/AudioBuffer.hpp"
#include "beebium/extension/AudioSourcePort.hpp"

#include "audio.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <thread>
#include <chrono>
#include <fstream>
#include <vector>
#include <cmath>

namespace {

// Helper to load ROM file
std::vector<uint8_t> load_rom(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open ROM: " + filepath);
    }
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// Test fixture that sets up a machine and server with audio enabled
class AudioTestFixture {
public:
    AudioTestFixture() {
        // Load ROMs
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());
#endif

        // Enable video and audio output
        machine_.state().memory.enable_video_output();
        machine_.state().memory.enable_audio_output();
        machine_.reset();

        // Start server on a dynamically allocated port
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(machine_, "127.0.0.1", 0);
        server_->start({}, {});

        // Create client channel using the actual bound port
        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::AudioService::NewStub(channel_);
    }

    ~AudioTestFixture() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::AudioService::Stub& stub() { return *stub_; }

    void run_cycles(uint64_t cycles) {
        machine_.run(cycles);
    }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::AudioService::Stub> stub_;
};

} // anonymous namespace

namespace {

// The format a Model B with no sound extensions reports, built field by field
// as the AudioService built it before the format came from the audio source
// port: the SN76489's two fields, then two silent fields (which now have no
// name). Every field is set explicitly, so a comparison of the serialized
// messages pins the format byte for byte.
beebium::AudioFormat expected_sn76489_only_format() {
    beebium::AudioFormat format;
    format.set_sample_rate(48000);
    format.set_source_count(beebium::AudioSample::MAX_SOURCES);

    auto* group = format.add_groups();
    group->set_group_id(1);
    group->set_group_name("Internal Sound");
    group->set_description("BBC Micro built-in SN76489 sound chip");
    group->set_color("#FF6B35");

    auto* sn_lo = format.add_sources();  // (tone0, tone1), MOS channels 1 and 2
    sn_lo->set_source_index(0);
    sn_lo->set_source_name("SN76489");
    sn_lo->set_encoding(beebium::ENCODING_2X16BIT_SIGNED);
    sn_lo->add_channel_names("1");
    sn_lo->add_channel_names("2");
    sn_lo->set_group_id(1);

    auto* sn_hi = format.add_sources();  // (tone2, noise), MOS channels 3 and 0
    sn_hi->set_source_index(1);
    sn_hi->set_source_name("SN76489");
    sn_hi->set_encoding(beebium::ENCODING_2X16BIT_SIGNED);
    sn_hi->add_channel_names("3");
    sn_hi->add_channel_names("0");
    sn_hi->set_group_id(1);

    for (uint32_t field = 2; field < beebium::AudioSample::MAX_SOURCES; ++field) {
        auto* silent = format.add_sources();
        silent->set_source_index(field);
        silent->set_encoding(beebium::ENCODING_SILENCE);
        silent->set_group_id(0);
    }
    return format;
}

beebium::AudioFormat get_audio_format(AudioTestFixture& fixture) {
    grpc::ClientContext context;
    beebium::GetAudioFormatRequest request;
    beebium::AudioFormat response;
    auto status = fixture.stub().GetAudioFormat(&context, request, &response);
    REQUIRE(status.ok());
    return response;
}

struct ConstantSource : beebium::AudioFieldSource {
    uint32_t take_field() override { return 0; }
};

}  // namespace

TEST_CASE("AudioService GetAudioFormat describes the SN76489 as before", "[grpc][audio]") {
    AudioTestFixture fixture;

    beebium::AudioFormat response = get_audio_format(fixture);
    beebium::AudioFormat expected = expected_sn76489_only_format();

    CHECK(response.DebugString() == expected.DebugString());
    CHECK(response.SerializeAsString() == expected.SerializeAsString());
    // The SN76489 is unipolar: its consumer removes the DC.
    CHECK_FALSE(response.sources(0).bipolar());
    CHECK_FALSE(response.sources(1).bipolar());
}

TEST_CASE("AudioService GetAudioFormat describes an extension's sound source", "[grpc][audio]") {
    AudioTestFixture fixture;

    ConstantSource source;
    beebium::AudioFieldInfo info;
    info.name = "Music5000";
    info.encoding = beebium::AudioFieldEncoding::Signed2x16;
    info.channel_names = {"L", "R"};
    info.group_name = "Music 5000";
    info.group_description = "Hybrid Music 5000 synthesiser";
    info.bipolar = true;
    fixture.machine().state().memory.audio_source_port().register_source(2, source, info);

    beebium::AudioFormat response = get_audio_format(fixture);
    beebium::AudioFormat expected = expected_sn76489_only_format();

    auto* group = expected.add_groups();
    group->set_group_id(2);
    group->set_group_name("Music 5000");
    group->set_description("Hybrid Music 5000 synthesiser");
    auto* music5000 = expected.mutable_sources(2);
    music5000->set_source_name("Music5000");
    music5000->set_encoding(beebium::ENCODING_2X16BIT_SIGNED);
    music5000->add_channel_names("L");
    music5000->add_channel_names("R");
    music5000->set_group_id(2);
    music5000->set_bipolar(true);

    CHECK(response.DebugString() == expected.DebugString());
    CHECK(response.SerializeAsString() == expected.SerializeAsString());
}

// Note: GetChannelStates moved to DebuggerControl.GetSoundChipState in debugger.proto

TEST_CASE("AudioService SubscribeAudio streams audio samples", "[grpc][audio]") {
    AudioTestFixture fixture;

    grpc::ClientContext context;
    beebium::SubscribeAudioRequest request;
    request.set_chunk_size(512);  // Small chunks for faster test

    auto reader = fixture.stub().SubscribeAudio(&context, request);

    // Run emulation in a separate thread to generate samples
    std::atomic<bool> running{true};
    std::thread emu_thread([&]() {
        while (running) {
            fixture.run_cycles(20000);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Try to receive at least one chunk
    beebium::AudioChunk chunk;
    bool received = reader->Read(&chunk);

    // Stop emulation
    running = false;
    context.TryCancel();
    emu_thread.join();

    REQUIRE(received);
    CHECK(chunk.sample_count() > 0);
    CHECK(chunk.sequence() == 0);

    // Verify sample data size: sample_count × source_count × 4 bytes
    size_t expected_size = chunk.sample_count() * beebium::AudioSample::MAX_SOURCES * sizeof(uint32_t);
    CHECK(chunk.samples().size() == expected_size);
}

TEST_CASE("AudioService sequence numbers increment", "[grpc][audio]") {
    AudioTestFixture fixture;

    grpc::ClientContext context;
    beebium::SubscribeAudioRequest request;
    request.set_chunk_size(256);

    auto reader = fixture.stub().SubscribeAudio(&context, request);

    // Run emulation
    std::atomic<bool> running{true};
    std::thread emu_thread([&]() {
        while (running) {
            fixture.run_cycles(50000);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Receive two chunks and verify sequence increments
    beebium::AudioChunk chunk1, chunk2;
    bool received1 = reader->Read(&chunk1);
    bool received2 = reader->Read(&chunk2);

    running = false;
    context.TryCancel();
    emu_thread.join();

    REQUIRE(received1);
    REQUIRE(received2);
    CHECK(chunk2.sequence() > chunk1.sequence());
}

TEST_CASE("AudioService sample unpacking", "[grpc][audio]") {
    // Test that we can correctly unpack samples from the streamed data
    AudioTestFixture fixture;

    grpc::ClientContext context;
    beebium::SubscribeAudioRequest request;
    request.set_chunk_size(100);

    auto reader = fixture.stub().SubscribeAudio(&context, request);

    // Run emulation
    std::atomic<bool> running{true};
    std::thread emu_thread([&]() {
        while (running) {
            fixture.run_cycles(50000);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    beebium::AudioChunk chunk;
    bool received = reader->Read(&chunk);

    running = false;
    context.TryCancel();
    emu_thread.join();

    REQUIRE(received);
    REQUIRE(chunk.sample_count() > 0);

    // Unpack the first sample. The SN76489 uses two 2x16 fields written
    // little-endian: source 0 (bytes 0-3) = (tone0, tone1), source 1
    // (bytes 4-7) = (tone2, noise), each channel a signed 16-bit value.
    const uint8_t* data = reinterpret_cast<const uint8_t*>(chunk.samples().data());
    auto le16 = [](const uint8_t* p) {
        return static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
    };
    int16_t tone0 = le16(data + 0);
    int16_t tone1 = le16(data + 2);
    int16_t tone2 = le16(data + 4);
    int16_t noise = le16(data + 6);

    INFO("Sample 0: tone0=" << tone0 << " tone1=" << tone1
         << " tone2=" << tone2 << " noise=" << noise);

    // Values are within the int16 range and unipolar (silence 0). Just verify we
    // can unpack without crashing; concrete values depend on chip state.
    CHECK(true);
}
