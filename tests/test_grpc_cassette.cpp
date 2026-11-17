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

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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

    void start_running() {
        if (thread_.joinable()) return;
        done_ = false;
        thread_ = std::thread([this] {
            while (!done_.load()) {
                machine_.wait_if_paused();
                if (done_.load()) break;
                beebium::ModelB::EmulationBusyScope busy(machine_);
                if (!busy.active()) continue;
                machine_.run(10'000);
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
