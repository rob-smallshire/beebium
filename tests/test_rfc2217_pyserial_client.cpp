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

// Opt-in interop test: a REAL pySerial client (serial_for_url('rfc2217://...'))
// drives our rfc2217-server-serial endpoint -- the universal-client validation.
// It SKIPs cleanly when no pySerial-capable Python is available, so the normal
// suite stays green without it. Run where pySerial is installed (or where `uv`
// can fetch it): the runner is auto-detected ($BEEBIUM_PYSERIAL, then
// `uv run --with pyserial python3`, then `python3`).

#include "Rfc2217ServerEndpoint.hpp"
#include "test_temp_paths.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

using namespace beebium;
using namespace std::chrono_literals;

namespace {

// Returns a command prefix that runs Python with pySerial importable, or "".
std::string find_pyserial_runner() {
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("BEEBIUM_PYSERIAL")) {
        if (env[0]) candidates.emplace_back(env);
    }
    candidates.emplace_back("uv run --with pyserial python3");
    candidates.emplace_back("python3");
    for (const auto& c : candidates) {
        const std::string check = c + " -c \"import serial\" >/dev/null 2>&1";
        if (std::system(check.c_str()) == 0) return c;
    }
    return "";
}

int exit_status(int system_rc) {
#ifndef _WIN32
    return WIFEXITED(system_rc) ? WEXITSTATUS(system_rc) : -1;
#else
    return system_rc;
#endif
}

}  // namespace

TEST_CASE("rfc2217-server-serial interoperates with a pySerial client",
          "[rfc2217][pyserial]") {
    const std::string runner = find_pyserial_runner();
    if (runner.empty()) {
        SKIP("no pySerial-capable Python found (set BEEBIUM_PYSERIAL or install uv/pyserial)");
    }

    rfc2217::Rfc2217ServerEndpoint::Options opts;
    opts.bind = "127.0.0.1";
    opts.port = 0;  // ephemeral
    rfc2217::Rfc2217ServerEndpoint server(std::move(opts));
    REQUIRE(server.is_listening());
    const std::uint16_t port = server.local_port();

    // Echo whatever the BBC side receives straight back (so the pySerial client
    // gets its bytes returned through the full RFC 2217 round-trip), and watch
    // for a BREAK handed to the BBC's receiver (break_pending_ is sticky until
    // take_break() consumes it).
    std::atomic<bool> stop{false};
    std::atomic<bool> saw_break{false};
    std::thread worker([&] {
        while (!stop.load()) {
            bool did = false;
            if (server.has_data()) { server.add_byte(server.next_byte()); did = true; }
            if (server.take_break()) { saw_break.store(true); did = true; }
            if (!did) std::this_thread::sleep_for(1ms);
        }
    });

    // A tiny pySerial client: connect, echo-check PING, send a real break, then
    // echo-check DONE. TCP order guarantees the break is processed by the server
    // before DONE (which follows it on the stream) comes back echoed.
    const std::filesystem::path script =
        beebium::test::unique_temp_path("beebium_rfc2217_pyserial_client_", ".py");
    {
        std::ofstream f(script);
        f << "import sys, serial\n"
             "s = serial.serial_for_url(sys.argv[1], baudrate=9600, timeout=5)\n"
             "s.write(b'PING')\n"
             "if s.read(4) != b'PING': s.close(); sys.exit(2)\n"
             "s.send_break(0.05)\n"
             "s.write(b'DONE')\n"
             "ok = s.read(4) == b'DONE'\n"
             "s.close()\n"
             "sys.exit(0 if ok else 3)\n";
    }

    const std::string cmd = runner + " " + script.string() +
                            " rfc2217://127.0.0.1:" + std::to_string(port);
    const int rc = std::system(cmd.c_str());

    // The break arrives before DONE on the wire, but the server processes it
    // asynchronously; give the worker a moment to observe the sticky flag.
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!saw_break.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(2ms);
    }

    stop.store(true);
    worker.join();
    std::filesystem::remove(script);

    CHECK(exit_status(rc) == 0);
    CHECK(saw_break.load());
}
