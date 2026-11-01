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

#include "app/AudioOutput.hpp"

#include "audio/AudioRenderer.hpp"

#include <QThread>
#include <QtGlobal>

#include <cstdio>

#if BEEBIUM_HAVE_QT_MULTIMEDIA
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <vector>
#endif

namespace beebium::linux_client {

#if BEEBIUM_HAVE_QT_MULTIMEDIA

namespace {

// The renderer as a device the sink pulls from: always as much as asked,
// silence included, so the sink never starves and never blocks.
class RendererDevice : public QIODevice {
public:
    RendererDevice(std::shared_ptr<AudioRenderer> renderer, bool float_samples)
        : renderer_(std::move(renderer)), float_samples_(float_samples) {}

    qint64 bytesAvailable() const override { return 1 << 20; }
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char* data, qint64 max_size) override {
        const std::size_t frame_bytes = float_samples_ ? 2 * sizeof(float) : 2 * sizeof(qint16);
        const std::size_t frames = static_cast<std::size_t>(max_size) / frame_bytes;
        if (frames == 0) {
            return 0;
        }
        if (float_samples_) {
            renderer_->render(reinterpret_cast<float*>(data), frames);
        } else {
            scratch_.resize(2 * frames);
            renderer_->render(scratch_.data(), frames);
            auto* out = reinterpret_cast<qint16*>(data);
            for (std::size_t i = 0; i < 2 * frames; ++i) {
                out[i] = static_cast<qint16>(
                    std::lround(std::clamp(scratch_[i], -1.0f, 1.0f) * 32767.0f));
            }
        }
        return static_cast<qint64>(frames * frame_bytes);
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    std::shared_ptr<AudioRenderer> renderer_;
    bool float_samples_;
    std::vector<float> scratch_;
};

}  // namespace

#endif

// The sink's own thread's object.
class SinkWorker : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~SinkWorker() override { stop_sink(); }

    void start_sink(std::shared_ptr<AudioRenderer> renderer, int sample_rate) {
        stop_sink();
#if BEEBIUM_HAVE_QT_MULTIMEDIA
        const QAudioDevice device = QMediaDevices::defaultAudioOutput();
        if (device.isNull()) {
            Q_EMIT status(tr("No sound output was found on this computer."));
            return;
        }
        QAudioFormat format;
        format.setSampleRate(sample_rate);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Float);
        if (!device.isFormatSupported(format)) {
            format.setSampleFormat(QAudioFormat::Int16);
        }
        if (!device.isFormatSupported(format)) {
            Q_EMIT status(tr("%1 cannot play %2 Hz stereo.")
                              .arg(device.description())
                              .arg(sample_rate));
            return;
        }
        const bool float_samples = format.sampleFormat() == QAudioFormat::Float;
        device_ = std::make_unique<RendererDevice>(std::move(renderer), float_samples);
        device_->open(QIODevice::ReadOnly);
        sink_ = std::make_unique<QAudioSink>(device, format);
        // A short device buffer: the latency is the pre-roll's to set.
        sink_->setBufferSize(format.bytesForDuration(40000));
        sink_->start(device_.get());
        std::fprintf(stderr, "beebium: sound to %s, %d Hz %s\n", qPrintable(device.description()),
                     sample_rate, float_samples ? "float" : "16-bit");
        Q_EMIT status(QString());
#else
        Q_UNUSED(renderer);
        Q_UNUSED(sample_rate);
#endif
    }

    void stop_sink() {
#if BEEBIUM_HAVE_QT_MULTIMEDIA
        if (sink_) {
            sink_->stop();
            sink_.reset();
        }
        device_.reset();
#endif
    }

Q_SIGNALS:
    void status(const QString& status);

private:
#if BEEBIUM_HAVE_QT_MULTIMEDIA
    std::unique_ptr<RendererDevice> device_;
    std::unique_ptr<QAudioSink> sink_;
#endif
};

bool AudioOutput::available() {
#if BEEBIUM_HAVE_QT_MULTIMEDIA
    return qEnvironmentVariable("BEEBIUM_AUDIO_OUTPUT") != QLatin1String("none");
#else
    return false;
#endif
}

AudioOutput::AudioOutput(QObject* parent) : QObject(parent) {
    if (!available()) {
#if BEEBIUM_HAVE_QT_MULTIMEDIA
        status_ = tr("Sound output is turned off (BEEBIUM_AUDIO_OUTPUT=none).");
#else
        status_ = tr("This build of Beebium has no sound output (it was built without Qt "
                     "Multimedia).");
#endif
        return;
    }
    thread_ = new QThread;
    thread_->setObjectName(QStringLiteral("bbm-sound"));
    worker_ = new SinkWorker;
    worker_->moveToThread(thread_);
    connect(worker_, &SinkWorker::status, this, &AudioOutput::set_status);
    thread_->start();
}

AudioOutput::~AudioOutput() {
    if (!thread_) {
        return;
    }
    // The worker is destroyed on its own thread, stopping the sink there.
    QMetaObject::invokeMethod(worker_, [worker = worker_] { delete worker; }, Qt::QueuedConnection);
    thread_->quit();
    if (thread_->wait(3000)) {
        delete thread_;
    } else {
        // An output stuck in the host's sound system: left to finish rather
        // than block the window from closing.
        std::fprintf(stderr, "beebium: the sound output did not stop in time\n");
    }
}

void AudioOutput::start(std::shared_ptr<AudioRenderer> renderer, int sample_rate) {
    if (!worker_) {
        return;
    }
    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, renderer = std::move(renderer), sample_rate]() mutable {
            worker->start_sink(std::move(renderer), sample_rate);
        },
        Qt::QueuedConnection);
}

void AudioOutput::stop() {
    if (!worker_) {
        return;
    }
    QMetaObject::invokeMethod(worker_, [worker = worker_] { worker->stop_sink(); },
                              Qt::QueuedConnection);
}

void AudioOutput::set_status(const QString& status) {
    if (status != status_) {
        status_ = status;
        Q_EMIT status_changed(status_);
    }
}

}  // namespace beebium::linux_client

#include "AudioOutput.moc"
