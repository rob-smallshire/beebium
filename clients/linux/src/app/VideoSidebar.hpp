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

#ifndef BEEBIUM_LINUX_APP_VIDEO_SIDEBAR_HPP
#define BEEBIUM_LINUX_APP_VIDEO_SIDEBAR_HPP

#include "app/ColourChoice.hpp"
#include "display/VideoSettings.hpp"

#include <QColor>
#include <QWidget>

#include <functional>
#include <optional>

class QAbstractButton;
class QButtonGroup;
class QSpinBox;
class QToolButton;

namespace beebium::linux_client {

// The Video mode (client specification 6.3, 6.6, 13.1): this window's video
// settings, in the macOS client's order. Pixels, then Display Style, then the
// style's options (Standard's edge margin), then the boxing colour. Each
// value that differs from its default has a reset beside it.
class VideoSidebar : public QWidget {
    Q_OBJECT

public:
    using ColourChooser = ColourChoice::Chooser;

    explicit VideoSidebar(QWidget* parent = nullptr);

    const VideoSettings& settings() const { return settings_; }
    // Show these settings, as a new window's starting ones; not a change.
    void set_settings(const VideoSettings& settings);
    // The platform's colour picker by default.
    void set_colour_chooser(const ColourChooser& chooser);

Q_SIGNALS:
    void changed(const beebium::linux_client::VideoSettings& settings);

private:
    void change(const std::function<void(VideoSettings&)>& edit);
    void present();

    VideoSettings settings_;
    QButtonGroup* pixels_;
    QButtonGroup* style_;
    QWidget* edge_margin_;
    QSpinBox* margin_size_;
    QToolButton* margin_size_reset_;
    ColourChoice* margin_colour_;
    ColourChoice* boxing_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_VIDEO_SIDEBAR_HPP
