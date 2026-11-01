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

// beebium-pacing-probe HOST:PORT SECONDS
//
// A server's pace without the GUI: consumes its audio stream as a client
// would, and each second prints the samples delivered and their rate over
// monotonic time beside the server's own GetPacingStats (achieved speed
// multiplier, skipped ticks). Separates a server falling behind real time
// from a client consuming at the wrong rate (the #126 method).

#include "connection/AudioStream.hpp"

#include "system.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: beebium-pacing-probe HOST:PORT SECONDS\n");
        return 2;
    }
    using namespace beebium::linux_client;
    using namespace std::chrono;
    const auto channel = grpc::CreateChannel(argv[1], grpc::InsecureChannelCredentials());
    const int run_seconds = std::atoi(argv[2]);
    AudioStream audio(channel, {});
    audio.start();
    auto system = beebium::SystemService::NewStub(channel);
    auto last_time = steady_clock::now();
    std::uint64_t last_samples = 0;
    for (int second = 1; second <= run_seconds; ++second) {
        std::this_thread::sleep_until(last_time + seconds(1));
        const auto now = steady_clock::now();
        const std::uint64_t samples = audio.samples_received();
        const double rate = static_cast<double>(samples - last_samples) /
                            duration<double>(now - last_time).count();
        last_time = now;
        last_samples = samples;
        grpc::ClientContext context;
        context.set_deadline(system_clock::now() + milliseconds(500));
        beebium::PacingStats pacing;
        const bool ok = system->GetPacingStats(&context, {}, &pacing).ok();
        std::printf("%3d s: %llu samples, %.0f samples/s, server speed %.4f, %llu ticks skipped, "
                    "%llu wire drops\n",
                    second, static_cast<unsigned long long>(samples), rate,
                    ok ? pacing.achieved_speed_multiplier() : -1.0,
                    static_cast<unsigned long long>(ok ? pacing.ticks_skipped() : 0),
                    static_cast<unsigned long long>(audio.wire_dropped_samples()));
        std::fflush(stdout);
    }
    audio.stop();
    return 0;
}
