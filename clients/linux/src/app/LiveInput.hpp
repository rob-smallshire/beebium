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

#ifndef BEEBIUM_LINUX_APP_LIVE_INPUT_HPP
#define BEEBIUM_LINUX_APP_LIVE_INPUT_HPP

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace beebium::linux_client {

// A live input's samples, as CassetteService.StreamAudio takes them
// (client specification 9.0, Live input): the device's own rate, signed
// 16-bit or 32-bit float, one or two channels, and for two the channel the
// deck takes. Little-endian, as this host's are.
struct LiveInputFormat {
    enum class Sample { s16, f32 };
    unsigned sample_rate = 0;
    Sample sample = Sample::s16;
    unsigned channels = 1;
    QString channel;  // "left" or "right" for two channels, else empty
    QString device_name;

    int bytes_per_frame() const { return static_cast<int>(channels) * (sample == Sample::s16 ? 2 : 4); }
    friend bool operator==(const LiveInputFormat&, const LiveInputFormat&) = default;
};

// An audio input the host reports, by the platform's own name, with the
// format it prefers and a way to ask whether it takes another.
struct AudioInputDevice {
    enum class Sample { unknown, uint8, int16, int32, f32 };
    QByteArray id;
    QString name;
    int preferred_rate = 0;
    int preferred_channels = 0;
    Sample preferred_sample = Sample::unknown;
    std::function<bool(int rate, int channels, Sample sample)> supports;
};

// The stream's format for a device: its own rate and, where the server
// takes it, its own sample format and channels. A device of more than two
// channels is asked for two; one in 8- or 32-bit integers is asked for
// float, then 16-bit. None when the server could take nothing it gives.
// `channel` is the one taken from two, "left" by default.
std::optional<LiveInputFormat> live_input_format(const AudioInputDevice& device,
                                                 const QString& channel = QString());

// The stream's sample rates, as the server bounds them.
constexpr int LIVE_RATE_MIN = 8000;
constexpr int LIVE_RATE_MAX = 192000;

// Captured bytes cut into chunks of whole frames, `milliseconds` of sound
// each, as they arrive: what is left over waits for the next bytes.
class LiveChunker {
public:
    static constexpr int CHUNK_MILLISECONDS = 10;

    explicit LiveChunker(const LiveInputFormat& format, int milliseconds = CHUNK_MILLISECONDS);

    int chunk_bytes() const { return chunk_bytes_; }
    std::vector<QByteArray> feed(const QByteArray& bytes);
    // What is held, whole frames only, to send as the stream ends.
    QByteArray rest();

private:
    int chunk_bytes_;
    int frame_bytes_;
    QByteArray held_;
};

// The host's audio inputs, every kind, as Qt reports them; none without
// Qt Multimedia.
std::vector<AudioInputDevice> audio_input_devices();

// A capture from one input with no voice processing: the plain input
// format, as the device gives it, with no gain control, noise suppression
// or echo cancellation applied here. Chunks come on the GUI thread.
class LiveCapture : public QObject {
    Q_OBJECT

public:
    explicit LiveCapture(QObject* parent = nullptr);
    ~LiveCapture() override;

    // False, with the reason as a sentence, if the input would not open.
    virtual bool start(const AudioInputDevice& device, const LiveInputFormat& format,
                       QString* refusal);
    virtual void stop();
    virtual bool capturing() const;

    // How an input was lost under the capture: it stopped with an error,
    // as a change of its format surfaces here (PipeWire renegotiates an
    // open stream by stopping it), or it left the host's list of inputs.
    enum class Loss { changed, disconnected };
    Q_ENUM(Loss)

Q_SIGNALS:
    void chunk(const QByteArray& samples);
    // The input stopped by itself; the capture has stopped with it.
    void lost(beebium::linux_client::LiveCapture::Loss loss);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_LIVE_INPUT_HPP
