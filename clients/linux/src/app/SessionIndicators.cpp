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

#include "app/SessionIndicators.hpp"

#include <QMetaObject>
#include <QtGlobal>

#include <cstdio>

namespace beebium::linux_client {

SessionIndicators::SessionIndicators(QObject* parent)
    : QObject(parent),
      // BEEBIUM_LED_TRACE=1: each LED's changes, for diagnosis.
      trace_(qEnvironmentVariable("BEEBIUM_LED_TRACE") == QLatin1String("1")) {}

SessionIndicators::~SessionIndicators() {
    stop();
}

bool SessionIndicators::is_lock_led(const IndicatorInfo& info) {
    return info.related_key == "Caps Lock" || info.related_key == "Shift Lock";
}

void SessionIndicators::start(std::shared_ptr<grpc::Channel> channel) {
    stop();
    const unsigned generation = ++generation_;
    stream_ = std::make_unique<IndicatorStream>(
        std::move(channel),
        IndicatorStream::Callbacks{
            [this, generation](const std::vector<IndicatorInfo>& infos) {
                QMetaObject::invokeMethod(
                    this, [this, generation, infos] { on_listed(generation, infos); },
                    Qt::QueuedConnection);
            },
            [this, generation](const IndicatorStream::Values& values) {
                QMetaObject::invokeMethod(
                    this, [this, generation, values] { on_values(generation, values); },
                    Qt::QueuedConnection);
            },
            {},
        });
    stream_->start();
}

void SessionIndicators::stop() {
    ++generation_;
    if (stream_) {
        stream_->stop();
        stream_.reset();
    }
}

void SessionIndicators::on_listed(unsigned generation, std::vector<IndicatorInfo> infos) {
    if (generation != generation_) {
        return;
    }
    indicators_ = std::move(infos);
    values_.clear();
    lock_leds_.clear();
    for (const auto& info : indicators_) {
        if (is_lock_led(info)) {
            lock_leds_.insert(QString::fromStdString(info.name));
        }
    }
    Q_EMIT listed();
}

void SessionIndicators::on_values(unsigned generation, const IndicatorStream::Values& values) {
    if (generation != generation_) {
        return;
    }
    for (const auto& [name_bytes, raw] : values) {
        const QString name = QString::fromStdString(name_bytes);
        const int value = static_cast<int>(std::min<std::uint32_t>(raw, 255));
        if (values_.value(name, -1) == value) {
            continue;
        }
        values_.insert(name, value);
        if (trace_) {
            std::fprintf(stderr, "beebium: LED %s %d\n", qPrintable(name), value);
        }
        Q_EMIT value_changed(name, value);
        if (is_lock_led(name)) {
            Q_EMIT lock_led_changed(name);
        }
    }
}

}  // namespace beebium::linux_client
