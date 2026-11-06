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

#ifndef BEEBIUM_LINUX_APP_ELIDED_LABEL_HPP
#define BEEBIUM_LINUX_APP_ELIDED_LABEL_HPP

#include <QLabel>
#include <QResizeEvent>
#include <QString>

namespace beebium::linux_client {

// One line of text that elides with an ellipsis rather than wrapping or
// widening its parent, the whole of it the tooltip when it does not fit, or
// a note of its own as the tooltip throughout.
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget* parent = nullptr) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
    }

    void set_whole(const QString& text) {
        whole_ = text;
        fit();
    }

    void set_note(const QString& note) {
        note_ = note;
        fit();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        fit();
    }

private:
    void fit() {
        const QString shown = fontMetrics().elidedText(whole_, Qt::ElideRight, contentsRect().width());
        setText(shown);
        setToolTip(!note_.isEmpty() ? note_ : shown == whole_ ? QString() : whole_);
    }

    QString whole_;
    QString note_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_ELIDED_LABEL_HPP
