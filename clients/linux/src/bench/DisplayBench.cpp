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

// beebium-display-bench: the display renderer against a live server, drawing
// into an offscreen texture instead of a window. It measures what one frame
// costs (upload, draw, and waiting for the GPU to finish) and the client's
// CPU use, with no compositor and no display: the half of the phase 0 spike
// that a headless machine can answer. Presentation pacing needs a window and
// is measured with the beebium executable itself.
//
// Headless Vulkan: QT_QPA_PLATFORM=vkkhrdisplay provides a Vulkan instance
// without a display.

#include "app/ApplicationInfo.hpp"
#include "connection/FrameMailbox.hpp"
#include "connection/Handshake.hpp"
#include "connection/VideoStream.hpp"
#include "display/DisplayRenderer.hpp"
#include "metrics/IntervalStats.hpp"
#include "metrics/ThreadCpu.hpp"

#include "beebium/host_identity/HostFingerprint.hpp"

#include "keyboard.grpc.pb.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QVulkanInstance>

#include <rhi/qrhi.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace beebium::linux_client;
using Clock = std::chrono::steady_clock;

namespace {

double milliseconds_between(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

}  // namespace

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption connect_option(QStringLiteral("connect"),
                                            QStringLiteral("Server <host:port>."),
                                            QStringLiteral("host:port"));
    const QCommandLineOption api_option(QStringLiteral("graphics-api"),
                                        QStringLiteral("vulkan (default) or opengl."),
                                        QStringLiteral("api"), QStringLiteral("vulkan"));
    const QCommandLineOption size_option(QStringLiteral("size"),
                                         QStringLiteral("Target <width>x<height>."),
                                         QStringLiteral("size"), QStringLiteral("1920x1080"));
    const QCommandLineOption seconds_option(QStringLiteral("seconds"),
                                            QStringLiteral("Measure for <seconds>."),
                                            QStringLiteral("seconds"), QStringLiteral("20"));
    const QCommandLineOption type_option(
        QStringLiteral("type"),
        QStringLiteral("Type <text> first (\\r for RETURN), e.g. \"MODE 1\\r\" for a 50 Hz "
                       "progressive mode; MODE 7 sends 25 composited frames a second."),
        QStringLiteral("text"));
    const QCommandLineOption settle_option(
        QStringLiteral("settle"), QStringLiteral("Wait <seconds> after typing before measuring."),
        QStringLiteral("seconds"), QStringLiteral("3"));
    const QCommandLineOption save_option(
        QStringLiteral("save"),
        QStringLiteral("Read the last drawn frame back and save it to <filepath> (PNG), to "
                       "check what was drawn as well as how fast."),
        QStringLiteral("filepath"));
    parser.addOptions({connect_option, api_option, size_option, seconds_option, type_option,
                       settle_option, save_option});
    parser.process(application);

    const QStringList size_parts = parser.value(size_option).split(QLatin1Char('x'));
    const QSize target_size(size_parts.value(0).toInt(), size_parts.value(1).toInt());
    const bool use_vulkan = parser.value(api_option).toLower() == QStringLiteral("vulkan");
    const int seconds = parser.value(seconds_option).toInt();

    // --- The device -----------------------------------------------------
    QVulkanInstance vulkan_instance;
    std::unique_ptr<QOffscreenSurface> fallback_surface;
    std::unique_ptr<QRhi> rhi;
    if (use_vulkan) {
        vulkan_instance.setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
        if (!vulkan_instance.create()) {
            std::fprintf(stderr, "bench: no Vulkan instance (try QT_QPA_PLATFORM=vkkhrdisplay)\n");
            return 1;
        }
        QRhiVulkanInitParams params;
        params.inst = &vulkan_instance;
        rhi.reset(QRhi::create(QRhi::Vulkan, &params));
    } else {
        fallback_surface.reset(QRhiGles2InitParams::newFallbackSurface());
        QRhiGles2InitParams params;
        params.fallbackSurface = fallback_surface.get();
        rhi.reset(QRhi::create(QRhi::OpenGLES2, &params));
    }
    if (!rhi) {
        std::fprintf(stderr, "bench: could not create the QRhi\n");
        return 1;
    }
    const QRhiDriverInfo driver = rhi->driverInfo();
    std::fprintf(stderr, "bench: %s on %s%s, target %dx%d\n", rhi->backendName(),
                 driver.deviceName.constData(),
                 driver.deviceType == QRhiDriverInfo::CpuDevice ? " (SOFTWARE RENDERER)" : "",
                 target_size.width(), target_size.height());

    std::unique_ptr<QRhiTexture> colour(
        rhi->newTexture(QRhiTexture::RGBA8, target_size, 1, QRhiTexture::RenderTarget));
    colour->create();
    std::unique_ptr<QRhiTextureRenderTarget> target(
        rhi->newTextureRenderTarget({QRhiColorAttachment(colour.get())}));
    std::unique_ptr<QRhiRenderPassDescriptor> render_pass(
        target->newCompatibleRenderPassDescriptor());
    target->setRenderPassDescriptor(render_pass.get());
    target->create();

    DisplayRenderer renderer;
    renderer.initialize(rhi.get(), render_pass.get());

    // --- The server -----------------------------------------------------
    auto channel = grpc::CreateChannel(parser.value(connect_option).toStdString(),
                                       grpc::InsecureChannelCredentials());
    const HandshakeResult handshake =
        perform_handshake(channel, client_protocol_fingerprint(),
                          beebium::host_identity::host_fingerprint(),
                          std::chrono::milliseconds(5000));
    if (!handshake.reached || !is_compatible(handshake.protocol)) {
        std::fprintf(stderr, "bench: %s\n",
                     handshake.reached
                         ? describe_protocol_mismatch(handshake, client_protocol_fingerprint())
                               .c_str()
                         : handshake.error.c_str());
        return 1;
    }
    std::fprintf(stderr, "bench: connected, video %ux%u at %u Hz\n", handshake.video_width,
                 handshake.video_height, handshake.video_framerate_hz);

    if (parser.isSet(type_option)) {
        // Through the hardware path, as any typing is (client specification 7.1).
        std::string text = parser.value(type_option).toStdString();
        for (std::size_t at; (at = text.find("\\r")) != std::string::npos;) {
            text.replace(at, 2, "\r");
        }
        auto keyboard = beebium::KeyboardService::NewStub(channel);
        grpc::ClientContext context;
        beebium::TypeQuicklyRequest request;
        request.set_text(text);
        beebium::TypeQuicklyResponse response;
        const grpc::Status status = keyboard->TypeQuickly(&context, request, &response);
        if (!status.ok() || !response.accepted()) {
            std::fprintf(stderr, "bench: typing refused: %s%s\n", status.error_message().c_str(),
                         response.error().c_str());
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::seconds(parser.value(settle_option).toInt()));
    }

    FrameMailbox mailbox;
    std::mutex ready_mutex;
    std::condition_variable ready;
    bool frame_waiting = false;
    VideoStream stream(channel, mailbox,
                       {[&] {
                            std::lock_guard lock(ready_mutex);
                            frame_waiting = true;
                            ready.notify_one();
                        },
                        {}});

    // --- Measure ----------------------------------------------------------
    IntervalRecorder frame_cost;   // set_frame + upload + draw + GPU finish
    IntervalRecorder receive_to_done;
    ThreadCpuSampler cpu;
    cpu.sample();
    stream.start();

    const auto end_at = Clock::now() + std::chrono::seconds(seconds);
    std::uint64_t drawn = 0;
    while (Clock::now() < end_at) {
        {
            std::unique_lock lock(ready_mutex);
            if (!ready.wait_for(lock, std::chrono::seconds(1), [&] { return frame_waiting; })) {
                continue;
            }
            frame_waiting = false;
        }
        auto frame = mailbox.take();
        if (!frame) {
            continue;
        }
        const auto received_at = frame->received_at;
        const auto started = Clock::now();
        renderer.set_frame(std::move(frame));
        QRhiCommandBuffer* command_buffer = nullptr;
        if (rhi->beginOffscreenFrame(&command_buffer) != QRhi::FrameOpSuccess) {
            std::fprintf(stderr, "bench: beginOffscreenFrame failed\n");
            return 1;
        }
        renderer.render(command_buffer, target.get());
        rhi->endOffscreenFrame();  // waits for the GPU
        const auto finished = Clock::now();
        frame_cost.record(milliseconds_between(started, finished));
        receive_to_done.record(milliseconds_between(received_at, finished));
        ++drawn;
    }
    const auto shares = cpu.sample();
    stream.stop();

    if (parser.isSet(save_option) && renderer.has_frame()) {
        // One more pass outside the measurement, read back from the target.
        QRhiReadbackResult readback;
        QRhiCommandBuffer* command_buffer = nullptr;
        rhi->beginOffscreenFrame(&command_buffer);
        renderer.render(command_buffer, target.get());
        QRhiResourceUpdateBatch* updates = rhi->nextResourceUpdateBatch();
        updates->readBackTexture({colour.get()}, &readback);
        command_buffer->resourceUpdate(updates);
        rhi->endOffscreenFrame();
        QImage image(reinterpret_cast<const uchar*>(readback.data.constData()),
                     readback.pixelSize.width(), readback.pixelSize.height(),
                     QImage::Format_RGBA8888);
        if (rhi->isYUpInFramebuffer()) {
            image = image.mirrored(false, true);
        }
        const QString filepath = parser.value(save_option);
        std::fprintf(stderr, "bench: saved %s: %s\n", qPrintable(filepath),
                     image.copy().save(filepath) ? "ok" : "FAILED");
    }

    std::fprintf(stderr, "frames drawn        %llu in %d s, dropped %llu, refused %llu\n",
                 static_cast<unsigned long long>(drawn), seconds,
                 static_cast<unsigned long long>(mailbox.superseded_count()),
                 static_cast<unsigned long long>(stream.frames_refused()));
    std::fprintf(stderr, "arrival interval    %s\n",
                 format_summary(stream.arrival_intervals().drain()).c_str());
    std::fprintf(stderr, "frame cost          %s\n", format_summary(frame_cost.drain()).c_str());
    std::fprintf(stderr, "receive to drawn    %s\n",
                 format_summary(receive_to_done.drain()).c_str());
    double total = 0.0;
    for (const auto& share : shares) {
        total += share.percent_of_core;
    }
    std::fprintf(stderr, "cpu %% of one core   total=%.1f |", total);
    for (const auto& share : shares) {
        if (share.percent_of_core >= 0.5) {
            std::fprintf(stderr, " %s=%.1f", share.name.c_str(), share.percent_of_core);
        }
    }
    std::fprintf(stderr, "\n");

    renderer.release();
    return 0;
}
