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

#ifndef BEEBIUM_LINUX_APP_SESSION_SPEED_HPP
#define BEEBIUM_LINUX_APP_SESSION_SPEED_HPP

#include <QObject>

#include <cstdint>
#include <memory>

namespace grpc {
class Channel;
}

namespace beebium::linux_client {

class PacingStatsStream;
class RpcWorker;

// One sample of the server's pacing statistics (client specification 5.6):
// multiples of real time, 0.0 configured meaning unlimited, 0.0 estimated
// meaning no estimate yet.
struct PacingSample {
    double configured = 1.0;
    double achieved = 0.0;
    double estimated_max = 0.0;
};

// A machine window's emulation speed: the multiplier it sets, and the
// pacing statistics, streamed only while something watches them (the
// Processor mode while it shows), so nothing polls. Set up and torn down
// with the connection.
class SessionSpeed : public QObject {
    Q_OBJECT

public:
    // As often as the macOS client polls, so the attainable-speed window
    // (SpeedScale.hpp) spans the same time.
    static constexpr std::uint32_t INTERVAL_MS = 250;

    explicit SessionSpeed(QObject* parent = nullptr);
    ~SessionSpeed() override;

    void start(std::shared_ptr<grpc::Channel> channel);
    void stop();

    // Whether the pacing statistics are wanted; kept across reconnects.
    void set_watching(bool watching);
    bool watching() const { return watching_; }

    // 1.0 is real time, 0.0 unlimited.
    void set_multiplier(double multiplier);

Q_SIGNALS:
    void sampled(const beebium::linux_client::PacingSample& sample);

private:
    void open_stream();
    void close_stream();
    void on_stats(unsigned generation, const PacingSample& sample);

    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<PacingStatsStream> stream_;
    std::unique_ptr<RpcWorker> rpc_;
    bool watching_ = false;
    unsigned generation_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SESSION_SPEED_HPP
