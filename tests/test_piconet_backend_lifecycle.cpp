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

#include <catch2/catch_test_macros.hpp>

#include "beebium/econet/PiconetBackend.hpp"

#include "piconet/MockPiconetSerial.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

using namespace beebium;
using namespace std::chrono_literals;
using namespace beebium::piconet;
using beebium::piconet::test::MockPiconetSerial;

TEST_CASE("PiconetBackend constructor sends SET_STATION then SET_MODE LISTEN in order",
          "[piconet][backend][lifecycle]") {
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    auto* mock = mock_owner.get();
    PiconetBackend backend(PiconetConfig{"/dev/null", /*initial_station=*/32},
                           std::move(mock_owner));

    REQUIRE(mock->write_count() == 2);
    CHECK(mock->write_as_string(0) == "SET_STATION 32\r");
    CHECK(mock->write_as_string(1) == "SET_MODE LISTEN\r");
}

TEST_CASE("PiconetBackend constructor with a closed port does not start the reader thread",
          "[piconet][backend][lifecycle]") {
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    auto* mock = mock_owner.get();
    mock->set_open(false);  // Simulate device that failed to open.
    PiconetBackend backend(PiconetConfig{"/dev/null", 32},
                           std::move(mock_owner));

    CHECK_FALSE(backend.is_connected());
    // No SET_STATION / SET_MODE writes should have been attempted.
    CHECK(mock->write_count() == 0);
    // Receive returns nullopt; reader thread is not running.
    CHECK_FALSE(backend.receive_frame().has_value());
}

TEST_CASE("PiconetBackend on_station_id_changed re-issues SET_STATION",
          "[piconet][backend][lifecycle]") {
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    auto* mock = mock_owner.get();
    PiconetBackend backend(PiconetConfig{"/dev/null", 32}, std::move(mock_owner));
    mock->clear_writes();  // Forget the construction-time handshake.

    backend.on_station_id_changed(42);
    REQUIRE(mock->write_count() == 1);
    CHECK(mock->write_as_string(0) == "SET_STATION 42\r");

    backend.on_station_id_changed(254);
    REQUIRE(mock->write_count() == 2);
    CHECK(mock->write_as_string(1) == "SET_STATION 254\r");
}

TEST_CASE("PiconetBackend on_station_id_changed is a no-op after the port closes",
          "[piconet][backend][lifecycle]") {
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    auto* mock = mock_owner.get();
    PiconetBackend backend(PiconetConfig{"/dev/null", 32}, std::move(mock_owner));
    mock->clear_writes();
    mock->close();
    backend.on_station_id_changed(99);
    CHECK(mock->write_count() == 0);
}

TEST_CASE("PiconetBackend destructor joins the reader thread within 200ms",
          "[piconet][backend][lifecycle]") {
    // No incoming bytes are staged. The reader's read() call will block
    // (returning would_block) for up to its internal timeout. The
    // destructor must close the serial port to unblock the reader and
    // join promptly. Without timed reads or a close-driven exit, this
    // test would hang.
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    auto backend = std::make_unique<PiconetBackend>(
        PiconetConfig{"/dev/null", 32}, std::move(mock_owner));

    auto start = std::chrono::steady_clock::now();
    backend.reset();  // Triggers destructor.
    auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(elapsed < std::chrono::milliseconds(200));
}

TEST_CASE("PiconetBackend::set_mode bumps backend_status_sequence on Listen <-> non-Listen transitions",
          "[piconet][backend][lifecycle]") {
    // is_connected() reflects (serial_open AND current_mode_ == Listen), so
    // any Listen <-> non-Listen change is visible to EconetService clients
    // via WatchEconetStatus. The poll loop pushes on EconetSocket::status_sequence()
    // advancing, which folds in NetworkBackend::backend_status_sequence().
    // Without the bump, WatchEconetStatus misses the change and macOS
    // "Disable" leaves the header stuck on "Connected" (github issue #35).
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    PiconetBackend backend(PiconetConfig{"/dev/null", 32}, std::move(mock_owner));
    REQUIRE(backend.is_connected());
    const auto seq_after_ctor = backend.backend_status_sequence();

    // Listen -> Stop: is_connected() flips, sequence must advance.
    backend.set_mode(piconet::Mode::Stop);
    CHECK_FALSE(backend.is_connected());
    CHECK(backend.backend_status_sequence() > seq_after_ctor);

    // Stop -> Stop: no change in is_connected(), no bump.
    const auto seq_after_stop = backend.backend_status_sequence();
    backend.set_mode(piconet::Mode::Stop);
    CHECK(backend.backend_status_sequence() == seq_after_stop);

    // Stop -> Listen: flips back, sequence advances again.
    backend.set_mode(piconet::Mode::Listen);
    CHECK(backend.is_connected());
    CHECK(backend.backend_status_sequence() > seq_after_stop);

    // Listen -> Monitor: both non-Listen from is_connected's perspective is
    // coming, but Listen -> Monitor is still a Listen <-> non-Listen transition.
    const auto seq_after_listen = backend.backend_status_sequence();
    backend.set_mode(piconet::Mode::Monitor);
    CHECK_FALSE(backend.is_connected());
    CHECK(backend.backend_status_sequence() > seq_after_listen);

    // Monitor -> Stop: both non-Listen. is_connected() stays false, no bump.
    const auto seq_after_monitor = backend.backend_status_sequence();
    backend.set_mode(piconet::Mode::Stop);
    CHECK(backend.backend_status_sequence() == seq_after_monitor);
}

TEST_CASE("PiconetBackend can be constructed and destructed many times in a row",
          "[piconet][backend][lifecycle]") {
    // Smoke test for thread leakage / double-close. If destructor cleanup
    // were buggy, this would either hang or surface a thread sanitiser
    // error.
    for (int i = 0; i < 5; ++i) {
        auto mock_owner = std::make_unique<MockPiconetSerial>();
        PiconetBackend backend(PiconetConfig{"/dev/null", 32},
                               std::move(mock_owner));
        // Just construct and destruct.
    }
    SUCCEED("constructed and destructed 5 times without hang or crash");
}

TEST_CASE("PiconetBackend::request_reopen swaps serial and sends SET_STATION + SET_MODE STOP",
          "[piconet][backend][lifecycle][reopen]") {
    // Keep a pointer to the replacement mock so the test can inspect the
    // writes issued on the reopen path.
    MockPiconetSerial* replacement = nullptr;
    auto factory = [&](const std::string& /*path*/)
        -> std::unique_ptr<SerialPort> {
        auto mock = std::make_unique<MockPiconetSerial>();
        replacement = mock.get();
        return mock;
    };

    auto initial_owner = std::make_unique<MockPiconetSerial>();
    PiconetBackend backend(PiconetConfig{"/dev/old", /*initial_station=*/64},
                           std::move(initial_owner),
                           factory);

    REQUIRE(backend.config().device_path == "/dev/old");

    backend.request_reopen("/dev/new");
    // Reopen happens at the top of receive_frame(); drive one tick.
    (void)backend.receive_frame();

    REQUIRE(backend.config().device_path == "/dev/new");
    REQUIRE(backend.open_error_message().empty());
    REQUIRE(backend.is_serial_open());
    REQUIRE(backend.mode() == piconet::Mode::Stop);

    // Replacement mock should have seen SET_STATION <initial_station>
    // followed by SET_MODE STOP. The factory's mock is freshly created,
    // so write_count reflects only the reopen-time writes.
    REQUIRE(replacement != nullptr);
    REQUIRE(replacement->write_count() == 2);
    CHECK(replacement->write_as_string(0) == "SET_STATION 64\r");
    CHECK(replacement->write_as_string(1) == "SET_MODE STOP\r");
}

TEST_CASE("PiconetBackend::request_reopen with Listen brings a disconnected backend live",
          "[piconet][backend][lifecycle][reopen]") {
    // Models the discovery Retry path: a backend constructed disconnected
    // (serial=nullptr, as create_backend now does when discovery finds
    // nothing) is brought fully live -- serial open AND Listen -- by a
    // single reopen carrying the target mode. is_connected() then answers
    // true without a separate Enable step.
    MockPiconetSerial* replacement = nullptr;
    auto factory = [&](const std::string& /*path*/)
        -> std::unique_ptr<SerialPort> {
        auto mock = std::make_unique<MockPiconetSerial>();
        replacement = mock.get();
        return mock;
    };

    PiconetBackend backend(PiconetConfig{/*device_path=*/"", /*station=*/81},
                           /*serial=*/nullptr, factory);
    REQUIRE_FALSE(backend.is_serial_open());
    REQUIRE_FALSE(backend.is_connected());

    backend.request_reopen("/dev/discovered", piconet::Mode::Listen);
    (void)backend.receive_frame();  // reopen runs at the top of the tick

    CHECK(backend.config().device_path == "/dev/discovered");
    CHECK(backend.is_serial_open());
    CHECK(backend.mode() == piconet::Mode::Listen);
    CHECK(backend.is_connected());

    REQUIRE(replacement != nullptr);
    REQUIRE(replacement->write_count() == 2);
    CHECK(replacement->write_as_string(0) == "SET_STATION 81\r");
    CHECK(replacement->write_as_string(1) == "SET_MODE LISTEN\r");
}

TEST_CASE("PiconetBackend::request_reopen records open_error_message on factory-returned closed port",
          "[piconet][backend][lifecycle][reopen]") {
    auto factory = [](const std::string& /*path*/)
        -> std::unique_ptr<SerialPort> {
        auto mock = std::make_unique<MockPiconetSerial>();
        mock->set_open(false);  // Simulate "open failed" at the OS level.
        return mock;
    };

    auto initial_owner = std::make_unique<MockPiconetSerial>();
    PiconetBackend backend(PiconetConfig{"/dev/old", 32},
                           std::move(initial_owner),
                           factory);

    backend.request_reopen("/dev/absent");
    (void)backend.receive_frame();

    CHECK(backend.config().device_path == "/dev/absent");
    CHECK_FALSE(backend.is_serial_open());
    CHECK_FALSE(backend.open_error_message().empty());
    // MockPiconetSerial does not supply an OS error, so the backend
    // falls back to the "unknown error" placeholder.
    CHECK(backend.open_error_message() == "unknown error");
}

TEST_CASE("PiconetBackend::request_reopen is a no-op without a SerialFactory",
          "[piconet][backend][lifecycle][reopen]") {
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    auto* initial_mock = mock_owner.get();
    PiconetBackend backend(PiconetConfig{"/dev/old", 32},
                           std::move(mock_owner));
    const auto initial_write_count = initial_mock->write_count();

    backend.request_reopen("/dev/new");
    (void)backend.receive_frame();

    // Device path stays unchanged; no writes against the initial serial
    // above what the constructor already produced.
    CHECK(backend.config().device_path == "/dev/old");
    CHECK(initial_mock->write_count() == initial_write_count);
}

TEST_CASE("PiconetBackend::request_reopen coalesces successive requests",
          "[piconet][backend][lifecycle][reopen]") {
    std::vector<std::string> factory_calls;
    auto factory = [&](const std::string& path)
        -> std::unique_ptr<SerialPort> {
        factory_calls.push_back(path);
        return std::make_unique<MockPiconetSerial>();
    };

    auto initial_owner = std::make_unique<MockPiconetSerial>();
    PiconetBackend backend(PiconetConfig{"/dev/old", 32},
                           std::move(initial_owner),
                           factory);

    backend.request_reopen("/dev/intermediate");
    backend.request_reopen("/dev/final");
    (void)backend.receive_frame();

    // Only the latest request is consumed; the intermediate one was
    // superseded before the emulation thread woke up to process it.
    CHECK(backend.config().device_path == "/dev/final");
    REQUIRE(factory_calls.size() == 1);
    CHECK(factory_calls[0] == "/dev/final");
}

TEST_CASE("PiconetBackend::request_reopen is safe under concurrent posters",
          "[piconet][backend][lifecycle][reopen][race]") {
    // gRPC serialises Dispatch per stream, but a misbehaving client
    // could open multiple streams and race them; this test pins down
    // the atomic-pointer coalescing under contention. Spawn N threads
    // each posting M paths, then drive a single tick. The slot
    // ultimately holds whichever path won the final exchange; the
    // factory should be invoked exactly once with that path, no
    // double-frees of the heap-allocated string slot, no missed
    // deletes of superseded posts.
    constexpr int kThreads = 8;
    constexpr int kPostsPerThread = 50;

    std::vector<std::string> factory_calls;
    auto factory = [&](const std::string& path)
        -> std::unique_ptr<SerialPort> {
        factory_calls.push_back(path);
        return std::make_unique<MockPiconetSerial>();
    };

    auto initial_owner = std::make_unique<MockPiconetSerial>();
    PiconetBackend backend(PiconetConfig{"/dev/initial", 32},
                           std::move(initial_owner),
                           factory);

    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kPostsPerThread; ++i) {
                backend.request_reopen("/dev/path-" + std::to_string(t) +
                                       "-" + std::to_string(i));
            }
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& th : threads) th.join();

    // Single tick consumes whatever pending request happened to be
    // last-written. All other posts must have been delete'd by the
    // exchange path; if any were leaked or double-freed, ASan/UBSan
    // would catch it (the test runs under sanitizers in CI).
    (void)backend.receive_frame();

    REQUIRE(factory_calls.size() == 1);
    // The chosen path comes from one of the threads' posts.
    CHECK(factory_calls[0].rfind("/dev/path-", 0) == 0);
    CHECK(backend.config().device_path == factory_calls[0]);
}

TEST_CASE("PiconetBackend status_sequence advances on hot-unplug via read-error",
          "[piconet][backend][lifecycle][sequence]") {
    // The reader thread's read-error branch closes the SerialPort
    // and calls notify_state_changed -- which must bump the
    // backend_status_sequence so WatchEconetStatus subscribers
    // notice the transport-level state flip. A regression that
    // dropped the bump (or the gating shutdown_ check that lets it
    // run when the backend is alive) would leave the macOS
    // sidebar's Connection row stuck on Connected after an unplug.
    auto initial_owner = std::make_unique<MockPiconetSerial>();
    auto* initial_mock = initial_owner.get();
    PiconetBackend backend(PiconetConfig{"/dev/initial", 32},
                           std::move(initial_owner));

    REQUIRE(backend.is_connected());
    const auto before = backend.backend_status_sequence();

    // Close the mock from outside the reader thread; the next
    // read() returns error=true. The reader then closes the serial
    // (idempotent), invokes notify_state_changed, and exits.
    initial_mock->close();

    auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (std::chrono::steady_clock::now() < deadline &&
           backend.backend_status_sequence() == before) {
        std::this_thread::sleep_for(5ms);
    }
    REQUIRE(backend.backend_status_sequence() > before);
    REQUIRE_FALSE(backend.is_serial_open());
}

TEST_CASE("PiconetBackend status_sequence advances on reopen success and failure",
          "[piconet][backend][lifecycle][sequence]") {
    // The reopen path (process_pending_reopen -> notify_state_changed)
    // must bump the sequence on both branches so subscribers see the
    // transition regardless of outcome -- otherwise a UI rebuild
    // wouldn't fire when a reopen failed (leaving the user with
    // a stale "Adapter responsive" indicator).
    SECTION("success path") {
        auto factory = [](const std::string& /*path*/)
            -> std::unique_ptr<SerialPort> {
            return std::make_unique<MockPiconetSerial>();
        };
        auto initial = std::make_unique<MockPiconetSerial>();
        PiconetBackend backend(PiconetConfig{"/dev/init", 32},
                              std::move(initial),
                              factory);
        backend.set_mode(piconet::Mode::Stop);
        const auto before = backend.backend_status_sequence();

        backend.request_reopen("/dev/replacement");
        (void)backend.receive_frame();

        REQUIRE(backend.is_serial_open());
        REQUIRE(backend.backend_status_sequence() > before);
    }

    SECTION("failure path") {
        auto failing_factory = [](const std::string& /*path*/)
            -> std::unique_ptr<SerialPort> {
            auto m = std::make_unique<MockPiconetSerial>();
            m->set_open(false);
            return m;
        };
        auto initial = std::make_unique<MockPiconetSerial>();
        PiconetBackend backend(PiconetConfig{"/dev/init", 32},
                              std::move(initial),
                              failing_factory);
        backend.set_mode(piconet::Mode::Stop);
        const auto before = backend.backend_status_sequence();

        backend.request_reopen("/dev/bad");
        (void)backend.receive_frame();

        REQUIRE_FALSE(backend.is_serial_open());
        REQUIRE(backend.backend_status_sequence() > before);
    }
}

TEST_CASE("PiconetBackend rx_queue contents survive a reopen",
          "[piconet][backend][lifecycle][reopen]") {
    // The rx_queue_ is a member of PiconetBackend, not of SerialPort,
    // so a SerialPort swap during reopen must not lose enqueued
    // frames. A regression that re-created the queue on reopen would
    // silently drop pending bytes the reader had already parsed.
    //
    // Inject a TX_RESULT OK line via the mock; the reader thread
    // parses it and enqueues a synthesised Ack. Trigger the reopen.
    // Drain receive_frame, which first runs process_pending_reopen
    // (closing the old serial, joining the reader, swapping in the
    // factory's mock), then dequeues the surviving Ack.
    auto initial_owner = std::make_unique<MockPiconetSerial>();
    auto* initial_mock = initial_owner.get();

    auto factory = [](const std::string& /*path*/)
        -> std::unique_ptr<SerialPort> {
        return std::make_unique<MockPiconetSerial>();
    };

    PiconetBackend backend(PiconetConfig{"/dev/initial", 32},
                           std::move(initial_owner),
                           factory);

    // Sanity check: drain a synthesised Ack to confirm the reader
    // pipeline is wired up before relying on it for the survives-
    // reopen assertion below. Stage and poll for up to 500ms.
    initial_mock->stage_read_chunk("TX_RESULT OK\n");
    bool sanity_ack = false;
    auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto f = backend.receive_frame(); f.has_value()) {
            REQUIRE(f->type == FrameType::Ack);
            sanity_ack = true;
            break;
        }
        std::this_thread::sleep_for(5ms);
    }
    REQUIRE(sanity_ack);

    // Now stage another Ack and give the reader time to enqueue it,
    // but do NOT drain. Then trigger the reopen.
    initial_mock->stage_read_chunk("TX_RESULT OK\n");
    // 100ms is comfortably above the busy-loop reader's iteration
    // cadence on the mock (no select timeout in MockPiconetSerial).
    std::this_thread::sleep_for(100ms);

    backend.request_reopen("/dev/replacement");

    // First post-reopen receive_frame: process_pending_reopen runs
    // first (closes initial_mock, joins reader, swaps), then
    // try_dequeue picks up the pre-reopen Ack from the queue.
    auto post = backend.receive_frame();
    REQUIRE(post.has_value());
    REQUIRE(post->type == FrameType::Ack);

    // And the reopen actually swapped the serial: backend.config_
    // now points at the replacement path.
    REQUIRE(backend.config().device_path == "/dev/replacement");
    REQUIRE(backend.is_serial_open());
}

TEST_CASE("PiconetBackend recovers from a closed-state initial open via request_reopen",
          "[piconet][backend][lifecycle][reopen][recovery]") {
    // The user's headline scenario: server started with a wrong path
    // (e.g. /dev/tty.usbmodem101 when the device is at usbmodem1101);
    // create_backend produces a closed-state backend with an OS-level
    // error in open_error_message_; the user opens the ModalEditor
    // and saves the corrected path; reopen should put the backend
    // into a fully-open state with the error cleared.
    MockPiconetSerial* replacement = nullptr;
    auto factory = [&](const std::string& /*path*/)
        -> std::unique_ptr<SerialPort> {
        auto mock = std::make_unique<MockPiconetSerial>();
        replacement = mock.get();
        return mock;
    };

    // Initial serial fails to open (set_open(false) before move).
    auto initial_owner = std::make_unique<MockPiconetSerial>();
    initial_owner->set_open(false);
    PiconetBackend backend(PiconetConfig{"/dev/wrong-path", 32},
                           std::move(initial_owner),
                           factory);

    // Closed-state preconditions: is_serial_open is false (no
    // SET_STATION/SET_MODE writes were issued because the ctor's
    // reader-start branch was skipped). open_error_message_ may be
    // empty in this mock-only test (MockPiconetSerial's open_error()
    // returns empty); a real PosixSerialPort would carry strerror
    // text here. The recovery test below doesn't depend on that
    // string -- only that the reopen path clears it on success.
    REQUIRE_FALSE(backend.is_serial_open());
    REQUIRE_FALSE(backend.is_connected());

    // Save the corrected path through the editor's reopen path.
    backend.request_reopen("/dev/correct-path");
    (void)backend.receive_frame();

    REQUIRE(backend.is_serial_open());
    REQUIRE(backend.config().device_path == "/dev/correct-path");
    REQUIRE(backend.open_error_message().empty());
    REQUIRE(backend.mode() == piconet::Mode::Stop);  // reopen lands in Stop

    // The replacement mock should have received SET_STATION 32 +
    // SET_MODE STOP from install_open_serial.
    REQUIRE(replacement != nullptr);
    REQUIRE(replacement->write_count() == 2);
    CHECK(replacement->write_as_string(0) == "SET_STATION 32\r");
    CHECK(replacement->write_as_string(1) == "SET_MODE STOP\r");
}
