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

#ifndef BEEBIUM_LINUX_APP_DISPLAY_WIDGET_HPP
#define BEEBIUM_LINUX_APP_DISPLAY_WIDGET_HPP

#include "app/HostLockState.hpp"
#include "app/KeyboardInput.hpp"
#include "connection/FrameMailbox.hpp"
#include "display/DisplayRenderer.hpp"
#include "metrics/IntervalStats.hpp"

#include <QRhiWidget>

#include <chrono>
#include <optional>

namespace beebium::linux_client {

// The emulator display: a QRhiWidget drawing frames from the mailbox with
// DisplayRenderer. It repaints when a frame arrives (frame_ready), on resize
// and on exposure, never from a timer or a vsync callback, so a sleeping
// display cannot stall it (client specification 6.1, 4.4).
class DisplayWidget : public QRhiWidget {
    Q_OBJECT

public:
    explicit DisplayWidget(FrameMailbox& mailbox, QWidget* parent = nullptr);

    // Time from a frame's arrival to the render that drew it, and between
    // renders that drew a new frame.
    IntervalRecorder& receive_to_render() { return receive_to_render_; }
    IntervalRecorder& render_intervals() { return render_intervals_; }

    // Key events typed into the display go to the machine through this.
    KeyboardInput& keyboard() { return keyboard_; }

    // The device QRhi is drawing with, once initialised.
    QString graphics_description() const { return graphics_description_; }

public slots:
    // A new frame is waiting in the mailbox. Call on the GUI thread (queue it
    // there from the reader thread).
    void frame_ready();

signals:
    void graphics_ready(const QString& description, bool software_renderer);

protected:
    void initialize(QRhiCommandBuffer* command_buffer) override;
    void render(QRhiCommandBuffer* command_buffer) override;
    void releaseResources() override;

    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    // Tab and Shift+Tab belong to the machine, not to widget focus.
    bool focusNextPrevChild(bool next) override;

private:
    FrameMailbox& mailbox_;
    DisplayRenderer renderer_;
    KeyboardInput keyboard_;
    std::unique_ptr<HostLockState> host_locks_;
    QRhi* initialised_for_ = nullptr;
    QString graphics_description_;
    IntervalRecorder receive_to_render_;
    IntervalRecorder render_intervals_;
    std::optional<std::chrono::steady_clock::time_point> previous_new_frame_render_;
};

// A one-line description of the device a QRhi drives, and whether it is a
// software rasteriser (llvmpipe) rather than a GPU.
QString describe_rhi(QRhi* rhi, bool* software_renderer);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_DISPLAY_WIDGET_HPP
