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
#include <QTimer>
#include <QtGlobal>

#include <chrono>
#include <cstdio>

#if BEEBIUM_HAVE_QT_MULTIMEDIA
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QByteArray>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <vector>
#endif

namespace beebium::linux_client {

#if BEEBIUM_HAVE_QT_MULTIMEDIA

namespace {

// Writes the renderer's sound into the sink as fast as the device takes
// it: whatever bytesFree() says the device has consumed since last time,
// silence included, so playback follows the device's own clock. (Qt's pull
// mode asked for some 2% too few frames on PipeWire, so the ring overfilled
// and the device was short.)
class Feeder {
public:
    Feeder(std::shared_ptr<AudioRenderer> renderer, bool float_samples)
        : renderer_(std::move(renderer)), float_samples_(float_samples) {}

    std::size_t frame_bytes() const {
        return float_samples_ ? 2 * sizeof(float) : 2 * sizeof(qint16);
    }

    // Writes what is still owed first; renders more only once the device
    // has taken all of it, so a short write holds its frames for the next
    // tick instead of losing them.
    void feed(QIODevice* device, qint64 bytes_free) {
        if (device == nullptr) {
            return;
        }
        if (!pending_.isEmpty()) {
            const qint64 written = device->write(pending_);
            if (written > 0) {
                pending_.remove(0, written);
            }
            if (!pending_.isEmpty()) {
                return;
            }
            bytes_free -= std::max<qint64>(written, 0);
        }
        const std::size_t frames = static_cast<std::size_t>(std::max<qint64>(bytes_free, 0)) /
                                   frame_bytes();
        if (frames == 0) {
            return;
        }
        samples_.resize(2 * frames);
        renderer_->render(samples_.data(), frames);
        const char* bytes = reinterpret_cast<const char*>(samples_.data());
        if (!float_samples_) {
            words_.resize(2 * frames);
            for (std::size_t i = 0; i < 2 * frames; ++i) {
                words_[i] = static_cast<qint16>(
                    std::lround(std::clamp(samples_[i], -1.0f, 1.0f) * 32767.0f));
            }
            bytes = reinterpret_cast<const char*>(words_.data());
        }
        const qint64 size = static_cast<qint64>(frames * frame_bytes());
        const qint64 written = device->write(bytes, size);
        if (written < size) {
            pending_.append(bytes + std::max<qint64>(written, 0), size - std::max<qint64>(written, 0));
            ++short_writes_;
        }
    }

    // How often the device took less than it said it had room for.
    std::uint64_t short_writes() const { return short_writes_; }

private:
    std::shared_ptr<AudioRenderer> renderer_;
    bool float_samples_;
    std::vector<float> samples_;
    std::vector<qint16> words_;
    QByteArray pending_;
    std::uint64_t short_writes_ = 0;
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
        feeder_ = std::make_unique<Feeder>(std::move(renderer), float_samples);
        sink_ = std::make_unique<QAudioSink>(device, format);
        // A short device buffer: the latency is the pre-roll's to set.
        sink_->setBufferSize(format.bytesForDuration(40000));
        output_ = sink_->start();
        // Topped up every 5 ms, well inside the 40 ms buffer.
        feed_ = std::make_unique<QTimer>();
        feed_->setTimerType(Qt::PreciseTimer);
        feed_->setInterval(5);
        connect(feed_.get(), &QTimer::timeout, this, [this] {
            if (sink_ && output_) {
                feeder_->feed(output_, sink_->bytesFree());
            }
        });
        feeder_->feed(output_, sink_->bytesFree());
        feed_->start();
        if (qEnvironmentVariable("BEEBIUM_AUDIO_TRACE") == QLatin1String("1")) {
            // The sink's side of the trace, beside the ring's: a sink gone
            // Idle, or pulling sparsely, shows here.
            trace_ = std::make_unique<QTimer>();
            trace_->setInterval(1000);
            connect(trace_.get(), &QTimer::timeout, this, [this] {
                if (!sink_) {
                    return;
                }
                static const char* const states[] = {"active", "suspended", "stopped", "idle"};
                const int state = static_cast<int>(sink_->state());
                const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count();
                std::fprintf(stderr,
                             "beebium: sink trace at %lld ms: %s, %lld of %lld bytes free, %lld us "
                             "elapsed, %lld us processed, error %d, %llu short writes\n",
                             static_cast<long long>(now_ms),
                             state >= 0 && state < 4 ? states[state] : "?",
                             static_cast<long long>(sink_->bytesFree()),
                             static_cast<long long>(sink_->bufferSize()),
                             static_cast<long long>(sink_->elapsedUSecs()),
                             static_cast<long long>(sink_->processedUSecs()),
                             static_cast<int>(sink_->error()),
                             static_cast<unsigned long long>(feeder_ ? feeder_->short_writes() : 0));
            });
            trace_->start();
        }
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
        trace_.reset();
        feed_.reset();
        if (feeder_ && feeder_->short_writes() > 0) {
            std::fprintf(stderr, "beebium: the sound output took less than it had room for %llu "
                                 "times; nothing was lost\n",
                         static_cast<unsigned long long>(feeder_->short_writes()));
        }
        if (sink_) {
            sink_->stop();
            sink_.reset();
        }
        output_ = nullptr;
        feeder_.reset();
#endif
    }

Q_SIGNALS:
    void status(const QString& status);

private:
#if BEEBIUM_HAVE_QT_MULTIMEDIA
    std::unique_ptr<Feeder> feeder_;
    std::unique_ptr<QAudioSink> sink_;
    QIODevice* output_ = nullptr;  // the sink's, while it runs
    std::unique_ptr<QTimer> feed_;
    std::unique_ptr<QTimer> trace_;
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
