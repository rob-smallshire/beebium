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

// CassetteService over gRPC (cassette round 5, #208): the deck's keys, its
// errors, its status read without stopping the machine, and a MOS 1.20 load
// driven through the service rather than --tape.

#include "support/tape_builder.hpp"
#include "test_temp_paths.hpp"

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include "cassette.grpc.pb.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using beebium::test::FileOptions;
using beebium::test::TapeBuilder;
using Catch::Matchers::ContainsSubstring;

namespace {

const std::filesystem::path ROM_DIRPATH = BEEBIUM_ROM_DIR;
const std::filesystem::path ASSETS_DIRPATH = BEEBIUM_TEST_ASSETS_DIR;

std::vector<uint8_t> read_file(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// A Model B with MOS 1.20 and BASIC 2 behind a server, with an emulation thread
// that runs it as fast as it can when asked to.
class CassetteFixture {
public:
    CassetteFixture() {
        if (std::filesystem::exists(ROM_DIRPATH / "acorn-mos_1_20.rom")) {
            const auto mos = read_file(ROM_DIRPATH / "acorn-mos_1_20.rom");
            const auto basic = read_file(ROM_DIRPATH / "bbc-basic_2.rom");
            machine_.memory().load_mos(mos.data(), mos.size());
            machine_.memory().load_basic(basic.data(), basic.size());
            roms_ = true;
        }
        machine_.reset();
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(machine_, "127.0.0.1", 0);
        server_->set_blank_tapes_dirpath(std::filesystem::path(BEEBIUM_BLANK_TAPES_DIR));
        server_->start({}, {});
        channel_ = grpc::CreateChannel("127.0.0.1:" + std::to_string(server_->port()),
                                       grpc::InsecureChannelCredentials());
        stub_ = beebium::CassetteService::NewStub(channel_);
        dirpath_ = beebium::test::unique_temp_path("beebium_grpc_cassette_", "");
        std::filesystem::create_directories(dirpath_);
    }

    ~CassetteFixture() {
        stop_running();
        server_->stop();
        std::error_code ec;
        std::filesystem::remove_all(dirpath_, ec);
    }

    bool roms() const { return roms_; }
    beebium::ModelB& machine() { return machine_; }
    beebium::CassetteService::Stub& stub() { return *stub_; }

    // Write a tape image into the fixture's directory.
    std::filesystem::path write(const std::string& name, const std::vector<uint8_t>& bytes) {
        const auto filepath = dirpath_ / name;
        TapeBuilder::write(filepath, bytes);
        return filepath;
    }

    // Run the machine on a thread of its own: as fast as it goes, or at real
    // speed (2 MHz of emulated cycles a second of wall time).
    void start_running(bool real_speed = false) {
        if (thread_.joinable()) return;
        done_ = false;
        thread_ = std::thread([this, real_speed] {
            const auto start_wall = std::chrono::steady_clock::now();
            const uint64_t start_cycles = machine_.cycles_since_reset();
            while (!done_.load()) {
                machine_.wait_if_paused();
                if (done_.load()) break;
                {
                    beebium::ModelB::EmulationBusyScope busy(machine_);
                    if (!busy.active()) continue;
                    machine_.run(10'000);
                }
                if (real_speed) {
                    const auto due = start_wall + std::chrono::microseconds(
                        (machine_.cycles_since_reset() - start_cycles) / 2);
                    std::this_thread::sleep_until(due);
                }
            }
        });
    }

    void stop_running() {
        done_ = true;
        if (thread_.joinable()) thread_.join();
    }

    // Wait (in wall time) until `cycles` more emulated cycles have run.
    void run_emulated(uint64_t cycles) {
        const uint64_t until = machine_.cycles_since_reset() + cycles;
        while (machine_.cycles_since_reset() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void type(const std::string& text) {
        REQUIRE(machine_.memory().system_via_peripheral.type_ahead().enqueue(text));
    }

    beebium::DeckStatus status() {
        grpc::ClientContext context;
        beebium::GetDeckStatusRequest request;
        beebium::DeckStatus response;
        REQUIRE(stub_->GetDeckStatus(&context, request, &response).ok());
        return response;
    }

    beebium::InsertTapeResponse insert(const std::string& url) {
        grpc::ClientContext context;
        beebium::InsertTapeRequest request;
        request.set_url(url);
        beebium::InsertTapeResponse response;
        REQUIRE(stub_->InsertTape(&context, request, &response).ok());
        return response;
    }

    template <typename Request, typename Call>
    beebium::CassetteResponse call(Call method, Request request = {}) {
        grpc::ClientContext context;
        beebium::CassetteResponse response;
        REQUIRE(((*stub_).*method)(&context, request, &response).ok());
        return response;
    }

    beebium::CassetteResponse play() { return call<beebium::PlayRequest>(&beebium::CassetteService::Stub::Play); }
    beebium::CassetteResponse stop() { return call<beebium::StopRequest>(&beebium::CassetteService::Stub::Stop); }
    beebium::CassetteResponse rewind() {
        return call<beebium::RewindRequest>(&beebium::CassetteService::Stub::Rewind);
    }
    beebium::CassetteResponse fast_forward() {
        return call<beebium::FastForwardRequest>(&beebium::CassetteService::Stub::FastForward);
    }
    beebium::CassetteResponse eject() {
        return call<beebium::EjectTapeRequest>(&beebium::CassetteService::Stub::EjectTape);
    }
    beebium::CassetteResponse record() {
        return call<beebium::RecordRequest>(&beebium::CassetteService::Stub::Record);
    }
    beebium::ListBlankTapesResponse list_blank_tapes() {
        grpc::ClientContext context;
        beebium::ListBlankTapesResponse response;
        REQUIRE(stub_->ListBlankTapes(&context, {}, &response).ok());
        return response;
    }
    beebium::CreateTapeResponse create_tape(const std::string& label, const std::filesystem::path& destination,
                                            bool overwrite = false) {
        grpc::ClientContext context;
        beebium::CreateTapeRequest request;
        request.set_label(label);
        request.set_destination_path(destination.string());
        request.set_overwrite(overwrite);
        beebium::CreateTapeResponse response;
        REQUIRE(stub_->CreateTape(&context, request, &response).ok());
        return response;
    }
    const std::filesystem::path& dirpath() const { return dirpath_; }

    beebium::CassetteResponse seek(double seconds) {
        beebium::SeekRequest request;
        request.set_position_seconds(seconds);
        return call<beebium::SeekRequest>(&beebium::CassetteService::Stub::Seek, request);
    }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::CassetteService::Stub> stub_;
    std::filesystem::path dirpath_;
    std::thread thread_;
    std::atomic<bool> done_{false};
    bool roms_ = false;
};

// A tape of carrier and a small file: long enough to watch it move.
std::vector<uint8_t> tone_tape() {
    const std::vector<uint8_t> data(200, 0x41);
    TapeBuilder builder;
    builder.file("TONE", 0xFFFF3000, 0xFFFF3000, data, {});
    return builder.uef();
}

}  // namespace

// =============================================================================
// 1. A fresh deck
// =============================================================================

TEST_CASE("CassetteService status on a fresh machine", "[grpc][cassette]") {
    CassetteFixture fixture;
    const auto status = fixture.status();
    CHECK(status.has_cassette_interface());
    CHECK_FALSE(status.tape_inserted());
    CHECK(status.transport() == beebium::TAPE_TRANSPORT_STOPPED);
    CHECK(status.position_seconds() == 0.0);
    CHECK_FALSE(status.motor_on());
    CHECK(status.ula_part() == "ferranti");
}

// =============================================================================
// 2. Insert and eject
// =============================================================================

TEST_CASE("CassetteService inserts a UEF stopped at the start and refuses a second", "[grpc][cassette]") {
    CassetteFixture fixture;
    TapeBuilder builder;
    builder.carrier(2400);  // one second
    const auto filepath = fixture.write("tone.uef", builder.uef());

    const auto inserted = fixture.insert(filepath.string());
    REQUIRE(inserted.success());
    CHECK(inserted.tape().name() == "tone.uef");
    CHECK(inserted.tape().format() == "UEF");
    CHECK(std::abs(inserted.tape().duration_seconds() - 1.0) < 1.0 / 4800 + 1e-6);

    const auto status = fixture.status();
    CHECK(status.tape_inserted());
    CHECK(status.tape().name() == "tone.uef");
    CHECK(status.transport() == beebium::TAPE_TRANSPORT_STOPPED);
    CHECK(status.position_seconds() == 0.0);

    const auto other = fixture.write("other.uef", builder.uef());
    const auto second = fixture.insert("file://" + other.string());
    CHECK_FALSE(second.success());
    CHECK(second.kind() == beebium::CASSETTE_ERROR_KIND_DECK_OCCUPIED);
    CHECK_THAT(second.error(), ContainsSubstring("tone.uef") && ContainsSubstring("other.uef"));

    CHECK(fixture.eject().success());
    CHECK_FALSE(fixture.status().tape_inserted());
    const auto played = fixture.play();
    CHECK_FALSE(played.success());
    CHECK(played.kind() == beebium::CASSETTE_ERROR_KIND_DECK_EMPTY);
    CHECK(played.error() == "There is no tape in the deck.");
}

// =============================================================================
// 3. Insert errors
// =============================================================================

TEST_CASE("CassetteService classifies tapes it cannot insert", "[grpc][cassette]") {
    CassetteFixture fixture;

    const auto missing = fixture.insert((std::filesystem::temp_directory_path() / "no-such-tape.uef").string());
    CHECK(missing.kind() == beebium::CASSETTE_ERROR_KIND_CANNOT_OPEN);
    CHECK_THAT(missing.error(), ContainsSubstring("no-such-tape.uef"));

    const auto empty = fixture.insert(fixture.write("empty.uef", {}).string());
    CHECK(empty.kind() == beebium::CASSETTE_ERROR_KIND_UNREADABLE);
    CHECK_THAT(empty.error(), ContainsSubstring("empty.uef"));

    const auto text = fixture.insert(fixture.write("notes.txt", {'h', 'e', 'l', 'l', 'o'}).string());
    CHECK(text.kind() == beebium::CASSETTE_ERROR_KIND_UNSUPPORTED_FORMAT);
    CHECK_THAT(text.error(), ContainsSubstring("notes.txt"));

    TapeBuilder disc_chunk;
    disc_chunk.carrier(1).raw_chunk(0x0200, {0});
    const auto refused = fixture.insert(fixture.write("disc.uef", disc_chunk.uef()).string());
    CHECK(refused.kind() == beebium::CASSETTE_ERROR_KIND_INVALID_IMAGE);
    CHECK_THAT(refused.error(), ContainsSubstring("disc.uef"));

    CHECK_FALSE(fixture.status().tape_inserted());
}

TEST_CASE("CassetteService refuses a seek before the start", "[grpc][cassette]") {
    CassetteFixture fixture;
    REQUIRE(fixture.insert(fixture.write("tone.uef", tone_tape()).string()).success());
    const auto seek = fixture.seek(-1.0);
    CHECK_FALSE(seek.success());
    CHECK(seek.kind() == beebium::CASSETTE_ERROR_KIND_INVALID_POSITION);
    CHECK(seek.error() == "A position of -1 seconds is before the start of the tape.");
}

// =============================================================================
// 4a. Winding
// =============================================================================

TEST_CASE("CassetteService winds at 30 times real speed with the motor off", "[grpc][cassette][mos]") {
    CassetteFixture fixture;
    if (!fixture.roms()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    CHECK(fixture.rewind().kind() == beebium::CASSETTE_ERROR_KIND_DECK_EMPTY);

    TapeBuilder builder;
    builder.carrier(2400 * 90);
    REQUIRE(fixture.insert(fixture.write("long.uef", builder.uef()).string()).success());
    REQUIRE(fixture.seek(60.0).success());
    fixture.start_running();

    REQUIRE(fixture.rewind().success());
    CHECK(fixture.status().transport() == beebium::TAPE_TRANSPORT_REWINDING);
    fixture.run_emulated(2'000'000);   // one second: thirty seconds of tape
    auto status = fixture.status();
    CHECK_FALSE(status.motor_on());
    CHECK(status.transport() == beebium::TAPE_TRANSPORT_REWINDING);
    CHECK(status.position_seconds() < 31.0);
    CHECK(status.position_seconds() > 0.0);
    fixture.run_emulated(2'000'000);
    status = fixture.status();
    CHECK(status.transport() == beebium::TAPE_TRANSPORT_STOPPED);
    CHECK(status.position_seconds() == 0.0);

    REQUIRE(fixture.fast_forward().success());
    CHECK(fixture.status().transport() == beebium::TAPE_TRANSPORT_FAST_FORWARDING);
    fixture.run_emulated(7'000'000);   // three and a half seconds
    status = fixture.status();
    CHECK(status.transport() == beebium::TAPE_TRANSPORT_STOPPED);
    CHECK(std::abs(status.position_seconds() - status.tape().duration_seconds()) <= 0.0005);
}

// =============================================================================
// 4. The transport with the relay
// =============================================================================

TEST_CASE("CassetteService play moves the tape only with the motor on", "[grpc][cassette][mos]") {
    CassetteFixture fixture;
    if (!fixture.roms()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    REQUIRE(fixture.insert(fixture.write("tone.uef", tone_tape()).string()).success());
    fixture.start_running();

    REQUIRE(fixture.play().success());
    fixture.run_emulated(2'000'000);
    auto status = fixture.status();
    CHECK(status.transport() == beebium::TAPE_TRANSPORT_PLAYING);
    CHECK_FALSE(status.motor_on());
    CHECK(status.position_seconds() == 0.0);

    fixture.type("*TAPE\rCHAIN\"\"\r");
    fixture.run_emulated(6'000'000);
    status = fixture.status();
    CHECK(status.motor_on());
    CHECK(status.position_seconds() > 0.5);

    REQUIRE(fixture.stop().success());
    const double held = fixture.status().position_seconds();
    fixture.run_emulated(1'000'000);
    CHECK(fixture.status().position_seconds() == held);

    const double duration = fixture.status().tape().duration_seconds();
    REQUIRE(fixture.seek(duration + 10.0).success());
    status = fixture.status();
    CHECK(status.transport() == beebium::TAPE_TRANSPORT_STOPPED);
    CHECK(std::abs(status.position_seconds() - duration) <= 0.0005);   // the duration is to the millisecond

    REQUIRE(fixture.play().success());
    CHECK(fixture.status().transport() == beebium::TAPE_TRANSPORT_STOPPED);
}

// =============================================================================
// 5. The status stream
// =============================================================================

TEST_CASE("CassetteService streams the status on each change", "[grpc][cassette][mos]") {
    CassetteFixture fixture;
    if (!fixture.roms()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    fixture.start_running();

    grpc::ClientContext context;
    beebium::SubscribeDeckStatusRequest request;
    request.set_min_interval_ms(50);
    auto reader = fixture.stub().SubscribeDeckStatus(&context, request);

    beebium::DeckStatus status;
    REQUIRE(reader->Read(&status));
    CHECK_FALSE(status.tape_inserted());

    auto read_until = [&](auto predicate) {
        for (int i = 0; i < 2000; ++i) {
            if (!reader->Read(&status)) return false;
            if (predicate(status)) return true;
        }
        return false;
    };

    REQUIRE(fixture.insert(fixture.write("tone.uef", tone_tape()).string()).success());
    CHECK(read_until([](const auto& s) { return s.tape_inserted(); }));
    REQUIRE(fixture.play().success());
    CHECK(read_until([](const auto& s) { return s.transport() == beebium::TAPE_TRANSPORT_PLAYING; }));
    fixture.type("*TAPE\rCHAIN\"\"\r");
    CHECK(read_until([](const auto& s) { return s.motor_on(); }));

    // While playing with the relay closed, positions come at least 50 ms of
    // emulated time apart.
    std::vector<double> positions;
    while (positions.size() < 5 && reader->Read(&status)) {
        if (status.motor_on() && status.transport() == beebium::TAPE_TRANSPORT_PLAYING) {
            positions.push_back(status.position_seconds());
        }
    }
    REQUIRE(positions.size() == 5);
    for (size_t i = 1; i < positions.size(); ++i) {
        if (positions[i] != positions[i - 1]) CHECK(positions[i] - positions[i - 1] >= 0.05 - 0.004);
    }

    REQUIRE(fixture.stop().success());
    CHECK(read_until([](const auto& s) { return s.transport() == beebium::TAPE_TRANSPORT_STOPPED; }));
    context.TryCancel();
}

// =============================================================================
// 6. A MOS 1.20 load through the service
// =============================================================================

TEST_CASE("CassetteService loads a BASIC program through MOS 1.20", "[grpc][cassette][mos]") {
    CassetteFixture fixture;
    if (!fixture.roms()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    const auto tok_filepath = beebium::test::unique_temp_path("beebium_grpc_cassette_tok_", ".tok");
    const std::string command = "uvx oaknut-basic tokenise " +
        (ASSETS_DIRPATH / "cassette" / "tape_ok.bas").string() + " " + tok_filepath.string() +
        " >/dev/null 2>&1";
    if (std::system(command.c_str()) != 0) {
        SKIP("oaknut-basic is not available through uvx, so the test program cannot be tokenised.");
    }
    const auto program = read_file(tok_filepath);
    TapeBuilder builder;
    builder.file("TAPEOK", 0xFFFF0E00, 0xFFFF8023, program, {});
    REQUIRE(fixture.insert(fixture.write("tape_ok.uef", builder.uef()).string()).success());

    fixture.start_running();
    fixture.type("*TAPE\rCHAIN\"\"\r");
    REQUIRE(fixture.play().success());

    // The program writes 42 to &70 when it runs.
    bool ran = false;
    for (int second = 0; second < 30 && !ran; ++second) {
        fixture.run_emulated(2'000'000);
        fixture.machine().with_emulation_paused([&] { ran = fixture.machine().peek(0x70) == 42; });
    }
    CHECK(ran);
}

// =============================================================================
// 9. Record
// =============================================================================

TEST_CASE("CassetteService records on a UEF, refuses a CSW and an empty deck", "[grpc][cassette][recording]") {
    CassetteFixture fixture;
    if (!fixture.roms()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");

    auto record = fixture.record();
    CHECK_FALSE(record.success());
    CHECK(record.kind() == beebium::CASSETTE_ERROR_KIND_DECK_EMPTY);

    TapeBuilder csw_builder;
    csw_builder.carrier(2400);
    REQUIRE(fixture.insert(fixture.write("game.csw", csw_builder.csw()).string()).success());
    CHECK_FALSE(fixture.status().tape().recordable());
    record = fixture.record();
    CHECK_FALSE(record.success());
    CHECK(record.kind() == beebium::CASSETTE_ERROR_KIND_READ_ONLY);
    CHECK(record.error() == "The tape game.csw is a CSW image, and Beebium records only on UEF tapes.");
    REQUIRE(fixture.eject().success());

    const auto filepath = fixture.write("tone.uef", tone_tape());
    const auto before = read_file(filepath);
    const auto inserted = fixture.insert(filepath.string());
    REQUIRE(inserted.success());
    CHECK(inserted.tape().recordable());
    fixture.start_running();
    REQUIRE(fixture.record().success());
    CHECK(fixture.status().transport() == beebium::TAPE_TRANSPORT_RECORDING);
    fixture.type("*MOTOR 1\r");
    fixture.run_emulated(4'000'000);   // two seconds of the relay closed, the Beeb silent
    CHECK(fixture.status().motor_on());
    REQUIRE(fixture.stop().success());
    CHECK(fixture.status().transport() == beebium::TAPE_TRANSPORT_STOPPED);
    CHECK(read_file(filepath) != before);
}

// =============================================================================
// Round 7: a WAV and its channel
// =============================================================================

TEST_CASE("CassetteService inserts a stereo WAV with its rate and the channel chosen", "[grpc][cassette][wav]") {
    CassetteFixture fixture;
    TapeBuilder builder;
    builder.carrier(2400);
    beebium::test::WavOptions o;
    o.channels = 2;
    o.signal_channel = 1;
    o.other_amplitude = 0.05;
    o.shape = beebium::test::WavOptions::Shape::Shifted90;
    const auto filepath = fixture.write("stereo.wav", builder.wav(o));

    auto inserted = fixture.insert(filepath.string());
    REQUIRE(inserted.success());
    CHECK(inserted.tape().format() == "WAV");
    CHECK(inserted.tape().sample_rate() == 48000);
    CHECK(inserted.tape().channel() == "right");
    CHECK_FALSE(inserted.tape().recordable());
    CHECK(fixture.record().kind() == beebium::CASSETTE_ERROR_KIND_READ_ONLY);
    REQUIRE(fixture.eject().success());

    grpc::ClientContext context;
    beebium::InsertTapeRequest request;
    request.set_url(filepath.string());
    request.set_channel("left");
    beebium::InsertTapeResponse response;
    REQUIRE(fixture.stub().InsertTape(&context, request, &response).ok());
    REQUIRE(response.success());
    CHECK(response.tape().channel() == "left");
    REQUIRE(fixture.eject().success());

    grpc::ClientContext context2;
    request.set_channel("centre");
    REQUIRE(fixture.stub().InsertTape(&context2, request, &response).ok());
    CHECK_FALSE(response.success());
    CHECK(response.error() == "A WAV's channel is left or right, not centre.");
}

// =============================================================================
// 10. Blank tapes
// =============================================================================

TEST_CASE("CassetteService lists the blank tapes and creates one that records", "[grpc][cassette][recording]") {
    CassetteFixture fixture;
    const auto listed = fixture.list_blank_tapes();
    REQUIRE(listed.tapes_size() == 4);
    const char* labels[] = {"Blank C15 (15 minutes)", "Blank C30 (30 minutes)", "Blank C60 (60 minutes)",
                            "Blank C90 (90 minutes)"};
    const double seconds[] = {900.0, 1800.0, 3600.0, 5400.0};
    for (int i = 0; i < 4; ++i) {
        CHECK(listed.tapes(i).label() == labels[i]);
        CHECK(listed.tapes(i).filename() == std::string(labels[i]) + ".uef");
        CHECK(listed.tapes(i).kind() == "tape");
        CHECK(listed.tapes(i).duration_seconds() == seconds[i]);   // to the millisecond
    }

    // The extension is added.
    auto created = fixture.create_tape("Blank C15 (15 minutes)", fixture.dirpath() / "mine");
    REQUIRE(created.success());
    CHECK(created.path() == (fixture.dirpath() / "mine.uef").string());
    CHECK(std::filesystem::exists(fixture.dirpath() / "mine.uef"));

    // Another extension is refused.
    created = fixture.create_tape("Blank C15 (15 minutes)", fixture.dirpath() / "mine.csw");
    CHECK_FALSE(created.success());
    CHECK(created.kind() == beebium::CREATE_TAPE_ERROR_KIND_WRONG_EXTENSION);

    // An existing destination is refused, and replaced with overwrite.
    created = fixture.create_tape("Blank C30 (30 minutes)", fixture.dirpath() / "mine.uef");
    CHECK_FALSE(created.success());
    CHECK(created.kind() == beebium::CREATE_TAPE_ERROR_KIND_DESTINATION_EXISTS);
    created = fixture.create_tape("Blank C30 (30 minutes)", fixture.dirpath() / "mine.uef", true);
    CHECK(created.success());

    created = fixture.create_tape("Blank C120", fixture.dirpath() / "other.uef");
    CHECK_FALSE(created.success());
    CHECK(created.kind() == beebium::CREATE_TAPE_ERROR_KIND_UNKNOWN_LABEL);

    const auto inserted = fixture.insert((fixture.dirpath() / "mine.uef").string());
    REQUIRE(inserted.success());
    CHECK(inserted.tape().recordable());
    CHECK(inserted.tape().duration_seconds() == 1800.0);
    CHECK(fixture.record().success());
}

// =============================================================================
// Round 8: StreamAudio, the live tape
// =============================================================================

namespace {

// A client stream of live audio.
struct AudioStream {
    grpc::ClientContext context;
    beebium::StreamAudioSummary summary;
    std::unique_ptr<grpc::ClientWriter<beebium::AudioInputChunk>> writer;

    explicit AudioStream(beebium::CassetteService::Stub& stub)
        : writer(stub.StreamAudio(&context, &summary)) {}

    bool format(uint32_t rate = 48000, beebium::AudioSampleFormat sample_format = beebium::AUDIO_SAMPLE_FORMAT_S16,
                uint32_t channels = 1, const std::string& device = "Test line in") {
        beebium::AudioInputChunk chunk;
        auto* f = chunk.mutable_format();
        f->set_sample_rate(rate);
        f->set_sample_format(sample_format);
        f->set_channels(channels);
        f->set_device_name(device);
        return writer->Write(chunk);
    }

    bool samples(const std::vector<int16_t>& s16) {
        beebium::AudioInputChunk chunk;
        chunk.set_samples(std::string(reinterpret_cast<const char*>(s16.data()), s16.size() * 2));
        return writer->Write(chunk);
    }

    grpc::Status finish() {
        writer->WritesDone();
        return writer->Finish();
    }
};

// 10 ms of a 2400 Hz cosine at amplitude 0.5, 16-bit, from sample `from`.
std::vector<int16_t> carrier_chunk(uint64_t from) {
    std::vector<int16_t> out(480);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<int16_t>(16000.0 * std::cos(2.0 * 3.14159265358979 * 2400.0 * static_cast<double>(from + i) / 48000.0));
    }
    return out;
}

template <typename Predicate>
bool wait_for_status(CassetteFixture& fixture, Predicate predicate, int milliseconds = 2000) {
    for (int i = 0; i < milliseconds / 10; ++i) {
        if (predicate(fixture.status())) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

}  // namespace

TEST_CASE("StreamAudio is refused with a tape in, without a format first, and on a second format",
          "[grpc][cassette][live]") {
    CassetteFixture fixture;
    {
        REQUIRE(fixture.insert(fixture.write("tone.uef", tone_tape()).string()).success());
        AudioStream stream(fixture.stub());
        stream.format();
        REQUIRE(stream.finish().ok());
        CHECK(stream.summary.kind() == beebium::CASSETTE_ERROR_KIND_DECK_OCCUPIED);
        CHECK(stream.summary.error() == "The deck already holds tone.uef; eject it before streaming Test line in.");
        REQUIRE(fixture.eject().success());
    }
    {
        AudioStream stream(fixture.stub());
        stream.samples(carrier_chunk(0));
        REQUIRE(stream.finish().ok());
        CHECK(stream.summary.kind() == beebium::CASSETTE_ERROR_KIND_INVALID_FORMAT);
        CHECK(stream.summary.error() == "A live stream begins with its format.");
        CHECK_FALSE(fixture.status().tape_inserted());
    }
    {
        AudioStream stream(fixture.stub());
        stream.format(44000, beebium::AUDIO_SAMPLE_FORMAT_S16, 3);
        REQUIRE(stream.finish().ok());
        CHECK(stream.summary.kind() == beebium::CASSETTE_ERROR_KIND_INVALID_FORMAT);
        CHECK(stream.summary.error() == "A live stream has one or two channels, not 3.");
    }
    {
        AudioStream stream(fixture.stub());
        stream.format();
        stream.samples(carrier_chunk(0));
        stream.format();
        REQUIRE(stream.finish().ok());
        CHECK(stream.summary.kind() == beebium::CASSETTE_ERROR_KIND_INVALID_FORMAT);
        CHECK(stream.summary.error() == "A live stream's format comes once, at its start.");
        CHECK(wait_for_status(fixture, [](const auto& s) { return !s.tape_inserted(); }));
    }
}

TEST_CASE("StreamAudio inserts a live tape the deck plays, and closing the stream ejects it",
          "[grpc][cassette][live]") {
    CassetteFixture fixture;
    fixture.start_running(true);
    AudioStream stream(fixture.stub());
    REQUIRE(stream.format());
    REQUIRE(wait_for_status(fixture, [](const auto& s) { return s.tape_inserted(); }));
    auto status = fixture.status();
    CHECK(status.tape().format() == "LIVE");
    CHECK(status.tape().name() == "Test line in");
    CHECK(status.tape().sample_rate() == 48000);
    CHECK(status.tape().channel().empty());   // a mono stream has no channel to report
    CHECK(status.tape().duration_seconds() == 0.0);
    CHECK_FALSE(status.tape().recordable());
    CHECK(status.transport() == beebium::TAPE_TRANSPORT_STOPPED);

    // What a live tape cannot do.
    for (auto response : {fixture.seek(1.0), fixture.rewind(), fixture.fast_forward(), fixture.record()}) {
        CHECK_FALSE(response.success());
        CHECK(response.kind() == beebium::CASSETTE_ERROR_KIND_LIVE_SOURCE);
    }
    CHECK(fixture.seek(1.0).error() == "The live input Test line in cannot be seeked.");
    REQUIRE(fixture.play().success());

    // Half a second of carrier, a bit ahead of real time.
    uint64_t sent = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 50; ++i) {
        REQUIRE(stream.samples(carrier_chunk(sent)));
        sent += 480;
        std::this_thread::sleep_until(start + std::chrono::milliseconds(10 * i));
    }
    status = fixture.status();
    CHECK(status.transport() == beebium::TAPE_TRANSPORT_PLAYING);
    CHECK(status.position_seconds() > 0.2);
    CHECK(status.live_ring_fill() >= 0.0);
    CHECK(status.live_ring_fill() < 0.2);
    CHECK(status.live_overruns() == 0);

    REQUIRE(stream.finish().ok());
    CHECK(stream.summary.error().empty());
    CHECK(stream.summary.samples_received() == sent);
    CHECK(stream.summary.samples_dropped() == 0);
    CHECK(wait_for_status(fixture, [](const auto& s) { return !s.tape_inserted(); }));
}

TEST_CASE("A live stream that ends plays out its ring before the tape goes", "[grpc][cassette][live]") {
    CassetteFixture fixture;
    fixture.start_running(true);
    for (const bool playing : {true, false}) {
        CAPTURE(playing);
        AudioStream stream(fixture.stub());
        REQUIRE(stream.format());
        REQUIRE(wait_for_status(fixture, [](const auto& s) { return s.tape_inserted(); }));
        if (playing) REQUIRE(fixture.play().success());
        uint64_t sent = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i) {  // a second, paced
            REQUIRE(stream.samples(carrier_chunk(sent)));
            sent += 480;
            std::this_thread::sleep_until(start + std::chrono::milliseconds(10 * (i + 1)));
        }
        const auto closed = std::chrono::steady_clock::now();
        REQUIRE(stream.finish().ok());
        const auto took = std::chrono::duration<double>(std::chrono::steady_clock::now() - closed).count();
        CHECK(stream.summary.samples_received() == sent);
        if (playing) {
            // The quarter-second cushion in the ring is heard before the tape goes.
            CHECK(took > 0.15);
            CHECK(took < 0.6);
            CHECK(stream.summary.silence_samples_inserted() == 0);
        } else {
            CHECK(took < 0.1);   // a stopped deck plays nothing: the tape goes at once
        }
        CHECK(wait_for_status(fixture, [](const auto& s) { return !s.tape_inserted(); }));
    }
}

TEST_CASE("A live tape reports a channel only for a stereo stream", "[grpc][cassette][live]") {
    CassetteFixture fixture;
    for (const auto& [channels, channel, reported] : {std::tuple{1u, std::string(), std::string()},
                                                      std::tuple{2u, std::string(), std::string("left")},
                                                      std::tuple{2u, std::string("right"), std::string("right")}}) {
        CAPTURE(channels, channel);
        AudioStream stream(fixture.stub());
        beebium::AudioInputChunk chunk;
        auto* f = chunk.mutable_format();
        f->set_sample_rate(48000);
        f->set_sample_format(beebium::AUDIO_SAMPLE_FORMAT_S16);
        f->set_channels(channels);
        f->set_channel(channel);
        REQUIRE(stream.writer->Write(chunk));
        REQUIRE(wait_for_status(fixture, [](const auto& s) { return s.tape_inserted(); }));
        CHECK(fixture.status().tape().channel() == reported);
        REQUIRE(stream.finish().ok());
        REQUIRE(wait_for_status(fixture, [](const auto& s) { return !s.tape_inserted(); }));
    }
}

TEST_CASE("EjectTape ends a live stream from the server", "[grpc][cassette][live]") {
    CassetteFixture fixture;
    fixture.start_running(true);
    AudioStream stream(fixture.stub());
    REQUIRE(stream.format());
    REQUIRE(wait_for_status(fixture, [](const auto& s) { return s.tape_inserted(); }));
    REQUIRE(stream.samples(carrier_chunk(0)));
    REQUIRE(fixture.eject().success());
    // The client's next write finds the stream over, or the server ends it.
    for (int i = 0; i < 100 && stream.samples(carrier_chunk(480 * (i + 1))); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(stream.finish().ok());
    CHECK(stream.summary.samples_received() >= 480);
    CHECK_FALSE(fixture.status().tape_inserted());
}

namespace {

std::optional<std::vector<uint8_t>> tokenised_tape_ok() {
    const auto tok_filepath = beebium::test::unique_temp_path("beebium_grpc_cassette_tok_", ".tok");
    const std::string command = "uvx oaknut-basic tokenise " +
        (ASSETS_DIRPATH / "cassette" / "tape_ok.bas").string() + " " + tok_filepath.string() +
        " >/dev/null 2>&1";
    if (std::system(command.c_str()) != 0) return std::nullopt;
    auto program = read_file(tok_filepath);
    std::error_code ec;
    std::filesystem::remove(tok_filepath, ec);
    return program;
}

// tape_ok as a 48 kHz WAV of 90-degree sines, as 16-bit mono samples.
std::optional<std::vector<int16_t>> tape_ok_samples() {
    const auto program = tokenised_tape_ok();
    if (!program) return std::nullopt;
    TapeBuilder builder;
    builder.gap(0.5).file("TAPEOK", 0xFFFF0E00, 0xFFFF8023, *program, {}).gap(0.5);
    beebium::test::WavOptions o;
    o.shape = beebium::test::WavOptions::Shape::Shifted90;
    const auto wav = builder.wav(o);
    std::vector<int16_t> samples((wav.size() - 44) / 2);
    std::memcpy(samples.data(), wav.data() + 44, samples.size() * 2);
    return samples;
}

// Stream `samples` in 10 ms chunks, paced to wall-clock time, while `during`
// runs at each chunk's index.
template <typename During>
void stream_in_real_time(AudioStream& stream, const std::vector<int16_t>& samples, During during) {
    const auto start = std::chrono::steady_clock::now();
    for (size_t at = 0, chunk = 0; at < samples.size(); at += 480, ++chunk) {
        std::vector<int16_t> part(samples.begin() + static_cast<std::ptrdiff_t>(at),
                                  samples.begin() + static_cast<std::ptrdiff_t>(std::min(samples.size(), at + 480)));
        if (!stream.samples(part)) return;
        during(chunk);
        std::this_thread::sleep_until(start + std::chrono::milliseconds(10 * (chunk + 1)));
    }
}

}  // namespace

TEST_CASE("MOS 1.20 loads tape_ok streamed live in real time", "[grpc][cassette][live][mos]") {
    CassetteFixture fixture;
    if (!fixture.roms()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    const auto samples = tape_ok_samples();
    if (!samples) SKIP("oaknut-basic is not available through uvx, so the test program cannot be tokenised.");
    for (const bool blip : {false, true}) {
        CAPTURE(blip);
        fixture.start_running(true);
        fixture.run_emulated(4'000'000);  // past the start-up
        fixture.type("*TAPE\rCHAIN\"\"\r");
        AudioStream stream(fixture.stub());
        REQUIRE(stream.format());
        REQUIRE(wait_for_status(fixture, [](const auto& s) { return s.tape_inserted(); }));
        REQUIRE(fixture.play().success());
        stream_in_real_time(stream, *samples, [&](size_t chunk) {
            // During the leader, open the relay for 50 ms and close it again
            // a few times: a live tape plays on regardless.
            if (!blip || chunk < 100 || chunk > 400 || chunk % 100 >= 5) return;
            fixture.machine().with_emulation_paused([&] {
                auto& ula = fixture.machine().memory().serial_socket.ula();
                const uint8_t control = ula.control();
                fixture.machine().memory().serial_socket.write_ula(0, chunk % 100 == 4 ? control | 0x80 : control & 0x7F);
            });
        });
        REQUIRE(stream.finish().ok());
        CAPTURE(stream.summary.samples_received(), stream.summary.samples_dropped(),
                stream.summary.silence_samples_inserted());
        CHECK(stream.summary.samples_dropped() == 0);
        bool ran = false;
        for (int i = 0; i < 100 && !ran; ++i) {
            fixture.machine().with_emulation_paused([&] { ran = fixture.machine().peek(0x70) == 42; });
            if (!ran) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        CHECK(ran);
        fixture.stop_running();
        fixture.machine().with_emulation_paused([&] { fixture.machine().write(0x70, 0); });
    }
}

// =============================================================================
// Data-race witnesses for the recording path (meaningful under TSan)
// =============================================================================

namespace {

// The ACIA idle with /RTS low and the Serial ULA on cassette with the motor
// on: the tone synthesiser sends carrier and a recording deck records it.
void send_carrier(CassetteFixture& fixture) {
    fixture.machine().with_emulation_paused([&] {
        fixture.machine().write(0xFE08, 0x15);
        fixture.machine().write(0xFE10, 0x85);
    });
}

}  // namespace

TEST_CASE("Recording through the service is race-free against a client's status stream and keys",
          "[grpc][cassette][race]") {
    CassetteFixture fixture;
    if (!fixture.roms()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    const auto filepath = fixture.dirpath() / "witness.uef";
    std::filesystem::copy_file(std::filesystem::path(BEEBIUM_BLANK_TAPES_DIR) / "Blank C15 (15 minutes).uef", filepath);
    fixture.start_running();
    fixture.run_emulated(2'000'000);
    REQUIRE(fixture.insert(filepath.string()).success());
    send_carrier(fixture);
    REQUIRE(fixture.record().success());

    std::atomic<bool> done{false};
    std::thread watcher([&] {
        grpc::ClientContext context;
        beebium::SubscribeDeckStatusRequest request;
        request.set_min_interval_ms(1);
        auto reader = fixture.stub().SubscribeDeckStatus(&context, request);
        beebium::DeckStatus status;
        while (!done && reader->Read(&status)) {}
        context.TryCancel();
        reader->Finish();
    });
    for (int i = 0; i < 40; ++i) {
        fixture.run_emulated(20'000);
        (void)fixture.status();
        if (i % 4 == 1) (void)fixture.stop();
        if (i % 4 == 2) (void)fixture.seek(0.1 * i);
        if (i % 4 == 3) (void)fixture.record();
        if (i % 8 == 7) (void)fixture.play();
    }
    REQUIRE(fixture.stop().success());
    done = true;
    watcher.join();
    fixture.stop_running();
    CHECK(fixture.status().transport() == beebium::TAPE_TRANSPORT_STOPPED);
}

TEST_CASE("Recording mutated without the pause races (the witness's control)", "[.][race]") {
    // The same, but Stop and Record reach the deck directly from this thread
    // while the emulation thread records: TSan reports it.
    CassetteFixture fixture;
    if (!fixture.roms()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    const auto filepath = fixture.dirpath() / "control.uef";
    std::filesystem::copy_file(std::filesystem::path(BEEBIUM_BLANK_TAPES_DIR) / "Blank C15 (15 minutes).uef", filepath);
    fixture.start_running();
    fixture.run_emulated(2'000'000);
    REQUIRE(fixture.insert(filepath.string()).success());
    send_carrier(fixture);
    auto& deck = fixture.machine().state().memory.cassette_deck;
    for (int i = 0; i < 20; ++i) {
        deck.record();
        fixture.run_emulated(20'000);
        deck.stop();
    }
    fixture.stop_running();
}

// =============================================================================
// The signal map (round 9)
// =============================================================================

namespace {

beebium::TapeSignalMap signal_map(CassetteFixture& fixture) {
    grpc::ClientContext context;
    beebium::GetTapeSignalMapRequest request;
    beebium::TapeSignalMap response;
    REQUIRE(fixture.stub().GetTapeSignalMap(&context, request, &response).ok());
    return response;
}

}  // namespace

TEST_CASE("GetTapeSignalMap reports a deck's map, empty, live or from a file", "[grpc][cassette][signal_map]") {
    CassetteFixture fixture;

    // No tape.
    {
        const auto map = signal_map(fixture);
        CHECK_FALSE(map.success());
        CHECK(map.kind() == beebium::CASSETTE_ERROR_KIND_DECK_EMPTY);
        CHECK(map.error() == "There is no tape in the deck.");
        CHECK_FALSE(fixture.status().signal_map_ready());
    }

    // A UEF: 2.5 s of gap, 3 s of carrier, 2 s of data, 1 s of gap, 1 s of
    // carrier and 2 s of data.
    std::vector<uint8_t> data(240);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i * 37 + 11);
    TapeBuilder builder;
    builder.gap(2.5).carrier(7200).bytes_8n1(data).gap(1.0).carrier(2400).bytes_8n1(data);
    const uint32_t generation_before = fixture.status().signal_map_generation();
    REQUIRE(fixture.insert(fixture.write("two_blocks.uef", builder.uef()).string()).success());
    REQUIRE(wait_for_status(fixture, [](const auto& s) { return s.signal_map_ready(); }));
    const auto status = fixture.status();
    CHECK(status.signal_map_generation() == generation_before + 1);

    const auto map = signal_map(fixture);
    REQUIRE(map.success());
    REQUIRE(map.ready());
    CHECK(map.seconds() == static_cast<uint32_t>(std::ceil(status.tape().duration_seconds())));
    REQUIRE(map.levels().size() == map.seconds());
    REQUIRE(map.seconds() == 12);
    const int expected[12] = {0, 0, 128, 255, 255, 255, 255, 128, 128, 255, 255, 255};
    for (size_t s = 0; s < 12; ++s) {
        const int level = static_cast<uint8_t>(map.levels()[s]);
        CAPTURE(s, level);
        CHECK(std::abs(level - expected[s]) <= 10);
    }

    // Ejected: empty again, and the status says no map.
    REQUIRE(fixture.call<beebium::EjectTapeRequest>(&beebium::CassetteService::Stub::EjectTape).success());
    CHECK_FALSE(fixture.status().signal_map_ready());
    CHECK(signal_map(fixture).kind() == beebium::CASSETTE_ERROR_KIND_DECK_EMPTY);

    // A live tape has no map.
    {
        AudioStream stream(fixture.stub());
        REQUIRE(stream.format());
        REQUIRE(wait_for_status(fixture, [](const auto& s) { return s.tape_inserted(); }));
        const auto live = signal_map(fixture);
        CHECK(live.success());
        CHECK_FALSE(live.ready());
        CHECK(live.seconds() == 0);
        CHECK_FALSE(fixture.status().signal_map_ready());
        REQUIRE(stream.finish().ok());
    }
}

TEST_CASE("The status stream reports a signal map becoming ready", "[grpc][cassette][signal_map]") {
    CassetteFixture fixture;
    TapeBuilder builder;
    builder.gap(1.0).carrier(2400);
    const auto filepath = fixture.write("short.uef", builder.uef());

    grpc::ClientContext context;
    beebium::SubscribeDeckStatusRequest request;
    auto reader = fixture.stub().SubscribeDeckStatus(&context, request);
    beebium::DeckStatus status;
    REQUIRE(reader->Read(&status));  // the current status at once
    CHECK_FALSE(status.signal_map_ready());

    REQUIRE(fixture.insert(filepath.string()).success());
    bool ready = false;
    for (int i = 0; i < 20 && !ready && reader->Read(&status); ++i) ready = status.signal_map_ready();
    CHECK(ready);
    context.TryCancel();
}

// =============================================================================
// A newly created tape, recorded on
// =============================================================================

namespace {

std::map<std::string, std::vector<uint8_t>> files_in(const std::filesystem::path& dirpath) {
    std::map<std::string, std::vector<uint8_t>> files;
    for (const auto& entry : std::filesystem::directory_iterator(dirpath)) {
        if (entry.is_regular_file()) files[entry.path().filename().string()] = read_file(entry.path());
    }
    return files;
}

}  // namespace

TEST_CASE("A program SAVEd to a newly created tape lands in the new tape, not its template",
          "[grpc][cassette][recording][mos]") {
    CassetteFixture fixture;
    if (!fixture.roms()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    // The server's templates here are the repository's own blank tapes.
    const std::filesystem::path templates_dirpath = BEEBIUM_BLANK_TAPES_DIR;
    const auto templates_before = files_in(templates_dirpath);

    // As a client's New Blank Tape does: create, then insert the path returned.
    const auto created = fixture.create_tape("Blank C15 (15 minutes)", fixture.dirpath() / "cass");
    REQUIRE(created.success());
    const std::filesystem::path tape_filepath = created.path();
    CHECK(tape_filepath == fixture.dirpath() / "cass.uef");
    CHECK(read_file(tape_filepath) == templates_before.at("Blank C15 (15 minutes).uef"));
    REQUIRE(fixture.insert(created.path()).success());

    // A short program that leaves its mark in memory, SAVEd as CASS.
    fixture.start_running();
    fixture.run_emulated(4'000'000);
    fixture.type("10 ?&70=42\r*TAPE\rSAVE \"CASS\"\r");
    fixture.run_emulated(4'000'000);   // to "RECORD then RETURN"
    REQUIRE(fixture.record().success());
    fixture.type("\r");
    // The SAVE is over when the relay opens again with the tape moved.
    REQUIRE(wait_for_status(fixture, [](const auto& s) { return s.motor_on(); }, 20'000));
    REQUIRE(wait_for_status(fixture, [](const auto& s) { return !s.motor_on() && s.position_seconds() > 0.0; },
                            60'000));
    REQUIRE(fixture.call<beebium::StopRequest>(&beebium::CassetteService::Stub::Stop).success());

    // The recording is in the new tape: its header block names CASS.
    const auto recorded = read_file(tape_filepath);
    const std::string header = std::string("\x2a") + "CASS" + std::string(1, '\0');
    CHECK(std::search(recorded.begin(), recorded.end(), header.begin(), header.end()) != recorded.end());
    // Every template is as it was.
    CHECK(files_in(templates_dirpath) == templates_before);

    // And the program loads back from the new tape and runs.
    fixture.machine().with_emulation_paused([&] { fixture.machine().write(0x70, 0); });
    REQUIRE(fixture.seek(0.0).success());
    REQUIRE(fixture.play().success());
    fixture.type("NEW\rCHAIN \"CASS\"\r");
    bool ran = false;
    for (int second = 0; second < 40 && !ran; ++second) {
        fixture.run_emulated(2'000'000);
        fixture.machine().with_emulation_paused([&] { ran = fixture.machine().peek(0x70) == 42; });
    }
    CHECK(ran);
}

TEST_CASE("While a SAVE records, the status stream shows the signal map advancing",
          "[grpc][cassette][signal_map][recording][mos]") {
    CassetteFixture fixture;
    if (!fixture.roms()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    const auto created = fixture.create_tape("Blank C15 (15 minutes)", fixture.dirpath() / "wake");
    REQUIRE(created.success());
    REQUIRE(fixture.insert(created.path()).success());
    REQUIRE(wait_for_status(fixture, [](const auto& s) { return s.signal_map_ready(); }, 10'000));

    grpc::ClientContext context;
    beebium::SubscribeDeckStatusRequest request;
    auto reader = fixture.stub().SubscribeDeckStatus(&context, request);
    beebium::DeckStatus status;
    REQUIRE(reader->Read(&status));
    const uint32_t start_generation = status.signal_map_generation();

    fixture.start_running(true);  // real speed: the wake is published a second of tape time apart
    fixture.run_emulated(4'000'000);
    fixture.type("10 ?&70=42\r*TAPE\rSAVE \"WAKE\"\r");
    fixture.run_emulated(4'000'000);
    REQUIRE(fixture.record().success());
    fixture.type("\r");

    // Read the stream until the SAVE is over: the relay closed, then open
    // again with the tape moved.
    std::vector<uint32_t> generations;
    bool motor_was_on = false;
    while (reader->Read(&status)) {
        const uint32_t last = generations.empty() ? start_generation : generations.back();
        if (status.transport() == beebium::TAPE_TRANSPORT_RECORDING && status.signal_map_ready() &&
            status.signal_map_generation() != last) {
            generations.push_back(status.signal_map_generation());
            // Between publishes the map is the last one published.
            const auto first = signal_map(fixture);
            const auto again = signal_map(fixture);
            if (fixture.status().signal_map_generation() == status.signal_map_generation()) {
                CHECK(first.levels() == again.levels());
            }
            REQUIRE(first.ready());
            CHECK(first.seconds() == 900);
        }
        if (status.motor_on()) motor_was_on = true;
        if (motor_was_on && !status.motor_on() && status.position_seconds() > 0.0) break;
    }
    context.TryCancel();
    REQUIRE(fixture.call<beebium::StopRequest>(&beebium::CassetteService::Stub::Stop).success());
    CAPTURE(generations.size());
    CHECK(generations.size() >= 3);
    for (size_t i = 1; i < generations.size(); ++i) CHECK(generations[i] > generations[i - 1]);
    CHECK(generations.front() > start_generation);

    // The map shows the SAVE's leader and blocks in the head's wake.
    const auto map = signal_map(fixture);
    const auto loud = std::count_if(map.levels().begin(), map.levels().end(),
                                    [](char level) { return static_cast<uint8_t>(level) >= 128; });
    CHECK(loud >= 5);
}
