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

#include "app/LiveInput.hpp"

#include <QCoreApplication>

#if BEEBIUM_HAVE_QT_MULTIMEDIA
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#endif

#include <algorithm>

namespace beebium::linux_client {

std::optional<LiveInputFormat> live_input_format(const AudioInputDevice& device,
                                                 const QString& channel) {
    const int rate = device.preferred_rate;
    if (rate < LIVE_RATE_MIN || rate > LIVE_RATE_MAX || device.preferred_channels < 1) {
        return std::nullopt;
    }
    const auto supports = [&](int channels, AudioInputDevice::Sample sample) {
        return device.supports && device.supports(rate, channels, sample);
    };
    // One channel or two: a device of more is asked for two, else one.
    int channels = std::min(device.preferred_channels, 2);
    if (device.preferred_channels > 2 && !supports(2, device.preferred_sample) &&
        !supports(2, AudioInputDevice::Sample::f32) && !supports(2, AudioInputDevice::Sample::int16)) {
        channels = 1;
    }
    // Its own sample format where the server takes it; else float, then
    // 16-bit, as the device allows.
    std::optional<LiveInputFormat::Sample> sample;
    const bool own_channels = channels == device.preferred_channels;
    if (device.preferred_sample == AudioInputDevice::Sample::int16 &&
        (own_channels || supports(channels, AudioInputDevice::Sample::int16))) {
        sample = LiveInputFormat::Sample::s16;
    } else if (device.preferred_sample == AudioInputDevice::Sample::f32 &&
               (own_channels || supports(channels, AudioInputDevice::Sample::f32))) {
        sample = LiveInputFormat::Sample::f32;
    } else if (supports(channels, AudioInputDevice::Sample::f32)) {
        sample = LiveInputFormat::Sample::f32;
    } else if (supports(channels, AudioInputDevice::Sample::int16)) {
        sample = LiveInputFormat::Sample::s16;
    }
    if (!sample) {
        return std::nullopt;
    }
    LiveInputFormat format;
    format.sample_rate = static_cast<unsigned>(rate);
    format.sample = *sample;
    format.channels = static_cast<unsigned>(channels);
    format.channel = channels == 2 ? (channel == QStringLiteral("right") ? QStringLiteral("right")
                                                                         : QStringLiteral("left"))
                                   : QString();
    format.device_name = device.name;
    return format;
}

LiveChunker::LiveChunker(const LiveInputFormat& format, int milliseconds)
    : frame_bytes_(std::max(1, format.bytes_per_frame())) {
    const int frames = std::max(1, static_cast<int>(format.sample_rate) * milliseconds / 1000);
    chunk_bytes_ = frames * frame_bytes_;
}

std::vector<QByteArray> LiveChunker::feed(const QByteArray& bytes) {
    held_.append(bytes);
    std::vector<QByteArray> chunks;
    qsizetype taken = 0;
    while (held_.size() - taken >= chunk_bytes_) {
        chunks.push_back(held_.mid(taken, chunk_bytes_));
        taken += chunk_bytes_;
    }
    held_.remove(0, taken);
    return chunks;
}

QByteArray LiveChunker::rest() {
    const qsizetype whole = held_.size() - held_.size() % frame_bytes_;
    QByteArray rest = held_.left(whole);
    held_.clear();
    return rest;
}

#if BEEBIUM_HAVE_QT_MULTIMEDIA

namespace {

AudioInputDevice::Sample sample_of(QAudioFormat::SampleFormat format) {
    switch (format) {
    case QAudioFormat::UInt8: return AudioInputDevice::Sample::uint8;
    case QAudioFormat::Int16: return AudioInputDevice::Sample::int16;
    case QAudioFormat::Int32: return AudioInputDevice::Sample::int32;
    case QAudioFormat::Float: return AudioInputDevice::Sample::f32;
    default: return AudioInputDevice::Sample::unknown;
    }
}

QAudioFormat::SampleFormat qt_sample(AudioInputDevice::Sample sample) {
    switch (sample) {
    case AudioInputDevice::Sample::uint8: return QAudioFormat::UInt8;
    case AudioInputDevice::Sample::int16: return QAudioFormat::Int16;
    case AudioInputDevice::Sample::int32: return QAudioFormat::Int32;
    case AudioInputDevice::Sample::f32: return QAudioFormat::Float;
    default: return QAudioFormat::Unknown;
    }
}

QAudioFormat qt_format(const LiveInputFormat& format) {
    QAudioFormat qt;
    qt.setSampleRate(static_cast<int>(format.sample_rate));
    qt.setChannelCount(static_cast<int>(format.channels));
    qt.setSampleFormat(format.sample == LiveInputFormat::Sample::s16 ? QAudioFormat::Int16
                                                                     : QAudioFormat::Float);
    return qt;
}

QAudioDevice find_input(const QByteArray& id) {
    for (const QAudioDevice& device : QMediaDevices::audioInputs()) {
        if (device.id() == id) {
            return device;
        }
    }
    return {};
}

}  // namespace

std::vector<AudioInputDevice> audio_input_devices() {
    std::vector<AudioInputDevice> devices;
    for (const QAudioDevice& qt : QMediaDevices::audioInputs()) {
        const QAudioFormat preferred = qt.preferredFormat();
        AudioInputDevice device;
        device.id = qt.id();
        device.name = qt.description();
        device.preferred_rate = preferred.sampleRate();
        device.preferred_channels = preferred.channelCount();
        device.preferred_sample = sample_of(preferred.sampleFormat());
        device.supports = [qt](int rate, int channels, AudioInputDevice::Sample sample) {
            QAudioFormat format;
            format.setSampleRate(rate);
            format.setChannelCount(channels);
            format.setSampleFormat(qt_sample(sample));
            return qt.isFormatSupported(format);
        };
        devices.push_back(std::move(device));
    }
    return devices;
}

struct LiveCapture::Impl {
    std::unique_ptr<QAudioSource> source;
    QIODevice* io = nullptr;
    // Where a device leaving the host is heard of.
    std::unique_ptr<QMediaDevices> devices;
};

LiveCapture::LiveCapture(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>()) {}

LiveCapture::~LiveCapture() {
    stop();
}

bool LiveCapture::start(const AudioInputDevice& device, const LiveInputFormat& format,
                        QString* refusal) {
    stop();
    const QAudioDevice input = find_input(device.id);
    if (input.isNull()) {
        if (refusal) {
            *refusal = tr("The audio input %1 is no longer available.").arg(device.name);
        }
        return false;
    }
    // The plain input, in the device's own format: QAudioSource applies no
    // gain control, noise suppression or echo cancellation of its own.
    impl_->source = std::make_unique<QAudioSource>(input, qt_format(format));
    impl_->source->setVolume(1.0);
    impl_->io = impl_->source->start();
    if (!impl_->io || impl_->source->error() != QAudio::NoError) {
        if (refusal) {
            *refusal = tr("The audio input %1 could not be opened.").arg(device.name);
        }
        impl_->source.reset();
        impl_->io = nullptr;
        return false;
    }
    connect(impl_->io, &QIODevice::readyRead, this, [this] {
        if (impl_->io) {
            const QByteArray bytes = impl_->io->readAll();
            if (!bytes.isEmpty()) {
                Q_EMIT chunk(bytes);
            }
        }
    });
    // Stopped with an error (an IOError when the device's format changes
    // or it goes): gone, if the host no longer lists it, else changed.
    connect(impl_->source.get(), &QAudioSource::stateChanged, this,
            [this, id = device.id](QAudio::State state) {
                if (state == QAudio::StoppedState && impl_->source &&
                    impl_->source->error() != QAudio::NoError) {
                    stop();
                    Q_EMIT lost(find_input(id).isNull() ? Loss::disconnected : Loss::changed);
                }
            });
    if (!impl_->devices) {
        impl_->devices = std::make_unique<QMediaDevices>();
    }
    impl_->devices->disconnect(this);
    connect(impl_->devices.get(), &QMediaDevices::audioInputsChanged, this, [this, id = device.id] {
        if (impl_->source && find_input(id).isNull()) {
            stop();
            Q_EMIT lost(Loss::disconnected);
        }
    });
    return true;
}

void LiveCapture::stop() {
    if (impl_->source) {
        impl_->io = nullptr;
        impl_->source->disconnect(this);
        impl_->source->stop();
        impl_->source.reset();
    }
}

bool LiveCapture::capturing() const {
    return impl_->source != nullptr;
}

#else

std::vector<AudioInputDevice> audio_input_devices() {
    return {};
}

struct LiveCapture::Impl {};

LiveCapture::LiveCapture(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>()) {}

LiveCapture::~LiveCapture() = default;

bool LiveCapture::start(const AudioInputDevice& device, const LiveInputFormat&, QString* refusal) {
    if (refusal) {
        *refusal = tr("This build of Beebium cannot capture sound, so %1 cannot be used.")
                       .arg(device.name);
    }
    return false;
}

void LiveCapture::stop() {}

bool LiveCapture::capturing() const {
    return false;
}

#endif

}  // namespace beebium::linux_client
