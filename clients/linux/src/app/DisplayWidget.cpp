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

#include "app/DisplayWidget.hpp"

namespace beebium::linux_client {

QString describe_rhi(QRhi* rhi, bool* software_renderer) {
    const QRhiDriverInfo info = rhi->driverInfo();
    if (software_renderer) {
        *software_renderer = info.deviceType == QRhiDriverInfo::CpuDevice;
    }
    return QStringLiteral("%1 on %2").arg(QString::fromLatin1(rhi->backendName()),
                                          QString::fromUtf8(info.deviceName));
}

DisplayWidget::DisplayWidget(FrameMailbox& mailbox, QWidget* parent)
    : QRhiWidget(parent), mailbox_(mailbox) {
    setMinimumSize(320, 256);
}

void DisplayWidget::frame_ready() {
    update();
}

void DisplayWidget::initialize(QRhiCommandBuffer*) {
    if (initialised_for_ != rhi()) {
        renderer_.initialize(rhi(), renderTarget()->renderPassDescriptor());
        initialised_for_ = rhi();
        bool software = false;
        graphics_description_ = describe_rhi(rhi(), &software);
        emit graphics_ready(graphics_description_, software);
    }
}

void DisplayWidget::render(QRhiCommandBuffer* command_buffer) {
    if (auto frame = mailbox_.take()) {
        const auto now = std::chrono::steady_clock::now();
        receive_to_render_.record(
            std::chrono::duration<double, std::milli>(now - frame->received_at).count());
        if (previous_new_frame_render_) {
            render_intervals_.record(
                std::chrono::duration<double, std::milli>(now - *previous_new_frame_render_)
                    .count());
        }
        previous_new_frame_render_ = now;
        renderer_.set_frame(std::move(frame));
    }
    renderer_.render(command_buffer, renderTarget());
}

void DisplayWidget::releaseResources() {
    renderer_.release();
    initialised_for_ = nullptr;
}

}  // namespace beebium::linux_client
