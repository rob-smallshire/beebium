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

#include "app/SelectionCoordinator.hpp"
#include "app/SelectionInput.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>

#include <chrono>

namespace beebium::linux_client {

namespace {

constexpr std::chrono::milliseconds CAPS_LOCK_SETTLE{30};

}  // namespace

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
    setFocusPolicy(Qt::StrongFocus);
    // No input method: a compose sequence or dead key must not swallow
    // presses meant for the machine.
    setAttribute(Qt::WA_InputMethodEnabled, false);

    // Caps Lock synchronisation (client specification 7.3).
    host_locks_ = HostLockState::create(this);
    keyboard_.set_host_caps_lock([this] { return host_locks_->caps_lock(); });
    // After a host Caps Lock press the windowing system applies the toggle
    // after delivering the press, so read the new state a moment later. On
    // Wayland the compositor's report of the change syncs as well; a sync
    // finding host and guest agreed does nothing.
    keyboard_.set_caps_lock_pressed([this] {
        QTimer::singleShot(CAPS_LOCK_SETTLE, this, [this] { keyboard_.sync_caps_lock(); });
    });
    connect(host_locks_.get(), &HostLockState::caps_lock_changed, this,
            [this](bool) { keyboard_.sync_caps_lock(); });
}

DisplayWidget::~DisplayWidget() = default;

void DisplayWidget::focusInEvent(QFocusEvent* event) {
    // The host Caps Lock may have changed while we were away (7.3).
    keyboard_.sync_caps_lock();
    QRhiWidget::focusInEvent(event);
    keyboard_changed();
}

bool DisplayWidget::event(QEvent* event) {
    // A key the machine takes is the machine's, even where an application
    // shortcut uses it: on a BBC Micro, CTRL with a letter is typing.
    if (event->type() == QEvent::ShortcutOverride) {
        const auto& key = *static_cast<QKeyEvent*>(event);
        if (keyboard_.takes(key)) {
            event->accept();
            return true;
        }
        if (keyboard_.window_chord(key)) {
            // The window's shortcut follows, and focus may go with it.
            keyboard_.yield_to_window();
        }
    }
    return QRhiWidget::event(event);
}

void DisplayWidget::keyPressEvent(QKeyEvent* event) {
    if (selection_input_ && selection_input_->key_press(*event)) {
        return;
    }
    if (keyboard_.key_press(*event)) {
        return;
    }
    // Leaving the display by keyboard: Ctrl+Tab and Ctrl+Shift+Tab, Qt's
    // convention for a widget that keeps Tab, and only when the mapping does
    // not give them to the machine (7.1). Plain Tab and Escape are the
    // machine's.
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    if (control && (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab)) {
        QWidget::focusNextPrevChild(event->key() == Qt::Key_Tab &&
                                    !event->modifiers().testFlag(Qt::ShiftModifier));
        return;
    }
    QRhiWidget::keyPressEvent(event);
}

KeysGoTo DisplayWidget::keys_go_to() const {
    return linux_client::keys_go_to(hasFocus(), keyboard_.attached());
}

void DisplayWidget::keyboard_changed() {
    const KeysGoTo now = keys_go_to();
    if (now != published_keys_go_to_) {
        published_keys_go_to_ = now;
        Q_EMIT keys_go_to_changed(now);
    }
    update();
}

void DisplayWidget::keyReleaseEvent(QKeyEvent* event) {
    if (selection_input_ && selection_input_->key_release(*event)) {
        return;
    }
    if (!keyboard_.key_release(*event)) {
        QRhiWidget::keyReleaseEvent(event);
    }
}

void DisplayWidget::focusOutEvent(QFocusEvent* event) {
    // Synchronously, here: the key-ups that follow focus loss go to whoever
    // gains focus, so anything held now would stay down (R4; client
    // specification 7.2).
    keyboard_.release_all();
    QRhiWidget::focusOutEvent(event);
    keyboard_changed();
}

bool DisplayWidget::focusNextPrevChild(bool) {
    return false;
}

void DisplayWidget::mousePressEvent(QMouseEvent* event) {
    if (selection_input_ && selection_input_->mouse_press(*event)) {
        event->accept();
        return;
    }
    QRhiWidget::mousePressEvent(event);
}

void DisplayWidget::mouseMoveEvent(QMouseEvent* event) {
    if (selection_input_ && selection_input_->mouse_move(*event)) {
        event->accept();
        return;
    }
    QRhiWidget::mouseMoveEvent(event);
}

void DisplayWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (selection_input_ && selection_input_->mouse_release(*event)) {
        event->accept();
        return;
    }
    QRhiWidget::mouseReleaseEvent(event);
}

void DisplayWidget::resizeEvent(QResizeEvent* event) {
    QRhiWidget::resizeEvent(event);
    if (selection_) {
        selection_->set_presentation(presentation());
    }
}

void DisplayWidget::attach_selection(SelectionCoordinator* coordinator) {
    selection_input_.reset();
    selection_ = coordinator;
    if (coordinator) {
        selection_input_ = std::make_unique<SelectionInput>(*coordinator, keyboard_,
                                                            [this] { return presentation(); });
    }
}

std::optional<ScreenContent> DisplayWidget::live_content() const {
    if (const DecodedFrame* frame = renderer_.current_frame()) {
        return screen_content(*frame);
    }
    return std::nullopt;
}

ScreenPresentation DisplayWidget::presentation() const {
    return {static_cast<double>(width()), static_cast<double>(height()),
            renderer_.video_settings()};
}

void DisplayWidget::freeze() {
    frozen_ = true;
}

void DisplayWidget::show_still(const DecodedFrame& still) {
    renderer_.set_frame(std::make_unique<DecodedFrame>(still));
    update();
}

void DisplayWidget::resume() {
    frozen_ = false;
    update();
}

void DisplayWidget::set_video_settings(const VideoSettings& settings) {
    if (settings == renderer_.video_settings()) {
        return;
    }
    renderer_.set_video_settings(settings);
    if (selection_) {
        selection_->set_presentation(presentation());
    }
    update();
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
        Q_EMIT graphics_ready(graphics_description_, software);
    }
}

void DisplayWidget::render(QRhiCommandBuffer* command_buffer) {
    // Frozen, the stream's frames wait in the mailbox, the latest replacing
    // the rest, until the display resumes.
    if (auto frame = frozen_ ? nullptr : mailbox_.take()) {
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
