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

#include "app/Sidebar.hpp"

#include <QButtonGroup>
#include <QFont>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

// One line of text that elides with an ellipsis rather than wrapping or
// widening its parent, the whole of it the tooltip when it does not fit.
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget* parent) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
    }

    void set_whole(const QString& text) {
        whole_ = text;
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
        setToolTip(shown == whole_ ? QString() : whole_);
    }

    QString whole_;
};

}  // namespace

Sidebar::Sidebar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("sidebar"));
    setAutoFillBackground(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The machine's description, a subtitle above everything else: data
    // only, smaller and dimmer than the text below it.
    description_ = new ElidedLabel(this);
    description_->setObjectName(QStringLiteral("machineDescription"));
    QFont subtitle = description_->font();
    subtitle.setPointSizeF(subtitle.pointSizeF() * 0.85);
    description_->setFont(subtitle);
    QPalette dimmed = description_->palette();
    dimmed.setColor(QPalette::WindowText, dimmed.color(QPalette::PlaceholderText));
    description_->setPalette(dimmed);
    description_->setContentsMargins(16, 6, 16, 0);
    description_->hide();
    layout->addWidget(description_);

    buttons_row_ = new QWidget(this);
    auto* buttons_layout = new QHBoxLayout(buttons_row_);
    buttons_layout->setContentsMargins(8, 6, 8, 6);
    buttons_layout->setSpacing(2);
    buttons_layout->addStretch(1);
    buttons_ = new QButtonGroup(this);
    buttons_->setExclusive(true);
    layout->addWidget(buttons_row_);

    auto* header = new QWidget(this);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(16, 8, 16, 8);
    header_icon_ = new QLabel(header);
    header_label_ = new QLabel(header);
    QFont font = header_label_->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.15);
    header_label_->setFont(font);
    header_layout->addWidget(header_icon_);
    header_layout->addWidget(header_label_, 1);
    layout->addWidget(header);

    banner_ = new QWidget(this);
    banner_->setObjectName(QStringLiteral("unavailableBanner"));
    banner_->setStyleSheet(QStringLiteral(
        "#unavailableBanner { background: rgba(255, 149, 0, 30); "
        "border: 1px solid rgba(255, 149, 0, 100); border-radius: 6px; }"));
    auto* banner_layout = new QVBoxLayout(banner_);
    banner_title_ = new QLabel(banner_);
    QFont bold = banner_title_->font();
    bold.setBold(true);
    banner_title_->setFont(bold);
    banner_detail_ = new QLabel(banner_);
    banner_detail_->setWordWrap(true);
    banner_detail_->setEnabled(false);  // the secondary text colour
    banner_layout->addWidget(banner_title_);
    banner_layout->addWidget(banner_detail_);
    auto* banner_margin = new QHBoxLayout;
    banner_margin->setContentsMargins(12, 0, 12, 8);
    banner_margin->addWidget(banner_);
    layout->addLayout(banner_margin);
    banner_->hide();

    stack_ = new QStackedWidget(this);
    layout->addWidget(stack_, 1);
    setMinimumWidth(220);
    resize(WIDTH, height());
}

void Sidebar::set_description(const QString& description) {
    static_cast<ElidedLabel*>(description_)->set_whole(description);
    description_->setVisible(!description.isEmpty());
}

void Sidebar::add_mode(SidebarMode mode, QWidget* content) {
    const int id = static_cast<int>(mode);
    pages_.insert(id, content);
    stack_->addWidget(content);
    auto* button = new QToolButton(buttons_row_);
    button->setIcon(sidebar_mode_icon(mode));
    button->setToolTip(sidebar_mode_label(mode));
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setIconSize(QSize(18, 18));
    button->setFocusPolicy(Qt::NoFocus);
    buttons_->addButton(button, id);
    // In the order of 13.1, before the closing stretch.
    auto* buttons_layout = static_cast<QHBoxLayout*>(buttons_row_->layout());
    int position = 0;
    for (int other = 1; other < id; ++other) {
        if (pages_.contains(other)) {
            ++position;
        }
    }
    buttons_layout->insertWidget(position, button);
    connect(button, &QToolButton::clicked, this, [this, mode] {
        set_mode(mode);
    });
    if (pages_.size() == 1) {
        set_mode(mode);
    } else {
        present();
    }
}

void Sidebar::set_mode(SidebarMode mode) {
    if (!has_mode(mode)) {
        return;
    }
    const bool changed = mode != mode_;
    mode_ = mode;
    present();
    if (changed) {
        Q_EMIT mode_changed(mode_);
    }
}

void Sidebar::set_verdict(ConnectionVerdict verdict) {
    verdict_ = verdict;
    present();
}

bool Sidebar::showing_banner() const {
    return !banner_->isHidden();
}

bool Sidebar::content_enabled() const {
    return stack_->isEnabled();
}

void Sidebar::present() {
    if (QWidget* page = pages_.value(static_cast<int>(mode_))) {
        stack_->setCurrentWidget(page);
    }
    if (QAbstractButton* button = buttons_->button(static_cast<int>(mode_))) {
        button->setChecked(true);
    }
    header_icon_->setPixmap(sidebar_mode_icon(mode_).pixmap(20, 20));
    header_label_->setText(sidebar_mode_label(mode_));
    const StalePresentation stale = stale_presentation(mode_, verdict_);
    banner_->setVisible(stale.stale);
    banner_title_->setText(stale.title);
    banner_detail_->setText(stale.detail);
    stack_->setEnabled(!stale.stale);
    // Dimmed as last-known state, not merely disabled.
    if (stale.stale) {
        auto* dim = new QGraphicsOpacityEffect(stack_);
        dim->setOpacity(0.5);
        stack_->setGraphicsEffect(dim);
    } else {
        stack_->setGraphicsEffect(nullptr);
    }
}

}  // namespace beebium::linux_client
