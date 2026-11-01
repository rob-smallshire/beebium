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

#ifndef BEEBIUM_LINUX_APP_SESSION_INDICATORS_HPP
#define BEEBIUM_LINUX_APP_SESSION_INDICATORS_HPP

#include "connection/IndicatorStream.hpp"

#include <QHash>
#include <QObject>
#include <QString>

#include <memory>
#include <vector>

namespace grpc {
class Channel;
}

namespace beebium::linux_client {

// A machine window's LEDs (client specification 10): whatever the server
// registers, in its order, with brightness 0 to 255; set up and torn down
// with the connection.
class SessionIndicators : public QObject {
    Q_OBJECT

public:
    explicit SessionIndicators(QObject* parent = nullptr);
    ~SessionIndicators() override;

    void start(std::shared_ptr<grpc::Channel> channel);
    void stop();

    const std::vector<IndicatorInfo>& indicators() const { return indicators_; }
    // Brightness 0 to 255; 0 for one not heard of.
    int value(const QString& name) const { return values_.value(name, 0); }

    // Whether a lock LED's name: its change is the moment to re-read the
    // machine's lock latch (7.3).
    static bool is_lock_led(const QString& name);

Q_SIGNALS:
    void listed();
    void value_changed(const QString& name, int value);
    // A lock LED changed (7.3); the keyboard re-reads the latch on it.
    void lock_led_changed(const QString& name);

private:
    void on_listed(unsigned generation, std::vector<IndicatorInfo> infos);
    void on_values(unsigned generation, const IndicatorStream::Values& values);

    std::unique_ptr<IndicatorStream> stream_;
    std::vector<IndicatorInfo> indicators_;
    QHash<QString, int> values_;
    unsigned generation_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SESSION_INDICATORS_HPP
