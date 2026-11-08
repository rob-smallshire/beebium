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

#include "app/NetworkSidebar.hpp"

#include "app/ExtensionPanel.hpp"
#include "app/SessionEconet.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

QPixmap dot(const QColor& colour) {
    QPixmap pixmap(8, 8);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawEllipse(0, 0, 8, 8);
    return pixmap;
}

void make_secondary(QLabel* label, double scale = 1.0) {
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * scale);
    label->setFont(font);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
}

QLabel* named_label(const char* name, QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setObjectName(QString::fromLatin1(name));
    return label;
}

}  // namespace

NetworkSidebar::NetworkSidebar(SessionEconet& econet, SessionExtensions& extensions, Shares shares,
                               TapBreak tap_break, BreakKey break_key, QWidget* parent)
    : QWidget(parent), econet_(econet), extensions_(extensions), shares_(std::move(shares)),
      tap_break_(std::move(tap_break)), break_key_(std::move(break_key)) {
    setObjectName(QStringLiteral("networkMode"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFocusPolicy(Qt::NoFocus);
    scroll->viewport()->setAutoFillBackground(false);
    outer->addWidget(scroll);
    auto* content = new QWidget(scroll);
    content->setAutoFillBackground(false);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(10);
    scroll->setWidget(content);

    // Loading, unavailable or not fitted: a statement in place of the rest.
    state_title_ = new QLabel(content);
    state_detail_ = new QLabel(content);
    state_detail_->setWordWrap(true);
    make_secondary(state_detail_, 0.9);
    layout->addWidget(state_title_);
    layout->addWidget(state_detail_);

    status_box_ = new QWidget(content);
    auto* status = new QVBoxLayout(status_box_);
    status->setContentsMargins(0, 0, 0, 0);
    status->setSpacing(10);

    banner_ = new QFrame(status_box_);
    banner_->setObjectName(QStringLiteral("pausedBanner"));
    banner_->setStyleSheet(QStringLiteral(
        "#pausedBanner { background: rgba(255, 204, 0, 40); "
        "border: 1px solid rgba(255, 204, 0, 120); border-radius: 6px; }"));
    auto* banner_layout = new QVBoxLayout(banner_);
    auto* banner_heading = new QHBoxLayout;
    auto* warning = new QLabel(banner_);
    warning->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(16, 16));
    auto* banner_title = new QLabel(tr("Econet paused"), banner_);
    QFont bold = banner_title->font();
    bold.setBold(true);
    banner_title->setFont(bold);
    banner_heading->addWidget(warning);
    banner_heading->addWidget(banner_title, 1);
    auto* banner_body =
        new QLabel(tr("This network bridges to real hardware and needs real-time speed. Set the "
                      "emulation speed to 1x in the Processor mode to resume."),
                   banner_);
    banner_body->setWordWrap(true);
    banner_layout->addLayout(banner_heading);
    banner_layout->addWidget(banner_body);
    status->addWidget(banner_);

    auto* rows = new QGridLayout;
    rows->setColumnStretch(1, 1);
    rows->addWidget(new QLabel(tr("Connection"), status_box_), 0, 0);
    auto* link_box = new QHBoxLayout;
    link_dot_ = new QLabel(status_box_);
    link_ = named_label("connection", status_box_);
    link_box->addStretch(1);
    link_box->addWidget(link_dot_);
    link_box->addWidget(link_);
    rows->addLayout(link_box, 0, 1);

    rows->addWidget(new QLabel(tr("Econet Station"), status_box_), 1, 0);
    auto* station_box = new QHBoxLayout;
    station_box->addStretch(1);
    in_force_ = named_label("stationInForce", status_box_);
    QFont medium = in_force_->font();
    medium.setWeight(QFont::Medium);
    in_force_->setFont(medium);
    pending_ = named_label("stationPending", status_box_);
    make_secondary(pending_);
    break_ = new QToolButton(status_box_);
    break_->setObjectName(QStringLiteral("pressBreak"));
    break_->setFocusPolicy(Qt::NoFocus);
    edit_ = new QToolButton(status_box_);
    edit_->setObjectName(QStringLiteral("editStation"));
    edit_->setIcon(QIcon::fromTheme(QStringLiteral("document-edit"),
                                    style()->standardIcon(QStyle::SP_FileDialogDetailedView)));
    edit_->setAutoRaise(true);
    edit_->setFocusPolicy(Qt::NoFocus);
    edit_->setToolTip(tr("Edit"));
    station_box->addWidget(in_force_);
    station_box->addWidget(pending_);
    station_box->addWidget(break_);
    station_box->addWidget(edit_);
    rows->addLayout(station_box, 1, 1);
    status->addLayout(rows);
    connect(break_, &QToolButton::clicked, this, [this] {
        if (tap_break_) {
            tap_break_();
        }
    });
    connect(edit_, &QToolButton::clicked, this, &NetworkSidebar::open_editor);

    // The station editor, in place of a popover.
    editor_ = new QFrame(status_box_);
    editor_->setObjectName(QStringLiteral("stationEditor"));
    editor_->setFrameShape(QFrame::StyledPanel);
    auto* editor = new QVBoxLayout(editor_);
    auto* editor_title = new QLabel(tr("Econet Station"), editor_);
    editor_title->setFont(bold);
    field_ = new QLineEdit(editor_);
    field_->setObjectName(QStringLiteral("stationField"));
    error_ = named_label("stationError", editor_);
    QPalette red = error_->palette();
    red.setColor(QPalette::WindowText, QColor(0xd0, 0x3b, 0x3b));
    error_->setPalette(red);
    error_->setWordWrap(true);
    info_ = named_label("stationInfo", editor_);
    make_secondary(info_, 0.9);
    info_->setWordWrap(true);
    auto* buttons = new QHBoxLayout;
    auto* cancel = new QPushButton(tr("Cancel"), editor_);
    cancel->setObjectName(QStringLiteral("cancelStation"));
    auto* save = new QPushButton(tr("Save"), editor_);
    save->setObjectName(QStringLiteral("saveStation"));
    save->setDefault(true);
    for (QPushButton* button : {cancel, save}) {
        button->setFocusPolicy(Qt::NoFocus);  // the keys stay the field's, Return saving
    }
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    editor->addWidget(editor_title);
    editor->addWidget(field_);
    editor->addWidget(error_);
    editor->addWidget(info_);
    editor->addLayout(buttons);
    editor_->hide();
    status->addWidget(editor_);
    connect(cancel, &QPushButton::clicked, editor_, &QWidget::hide);
    connect(save, &QPushButton::clicked, this, &NetworkSidebar::save_station);
    connect(field_, &QLineEdit::returnPressed, this, &NetworkSidebar::save_station);

    collision_ = named_label("collision", status_box_);
    collision_->setWordWrap(true);
    QPalette orange = collision_->palette();
    orange.setColor(QPalette::WindowText, QColor(0xd9, 0x7a, 0x00));
    collision_->setPalette(orange);
    collision_->setToolTip(tr("Two stations on the network claim the same number; the first live "
                              "one keeps it. Give the machines distinct station numbers."));
    status->addWidget(collision_);
    stale_ = named_label("staleNote", status_box_);
    make_secondary(stale_, 0.9);
    stale_->setText(tr("Econet status unavailable"));
    status->addWidget(stale_);

    auto* line = new QFrame(status_box_);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    status->addWidget(line);
    transports_unavailable_ = new QLabel(tr("Transport list unavailable"), status_box_);
    make_secondary(transports_unavailable_, 0.9);
    status->addWidget(transports_unavailable_);
    transports_layout_ = new QVBoxLayout;
    status->addLayout(transports_layout_);
    layout->addWidget(status_box_);
    layout->addStretch(1);

    connect(&econet_, &SessionEconet::changed, this, &NetworkSidebar::present);
    connect(&econet_, &SessionEconet::station_refused, this, [this](const QString& message) {
        error_->setText(message);
        error_->show();
        editor_->show();
    });
    present();
}

void NetworkSidebar::showEvent(QShowEvent* event) {
    econet_.set_watching(true);
    QWidget::showEvent(event);
}

void NetworkSidebar::hideEvent(QHideEvent* event) {
    econet_.set_watching(false);
    QWidget::hideEvent(event);
}

void NetworkSidebar::present() {
    const EconetInputs& inputs = econet_.inputs();
    const NetworkContent content = network_content(inputs);
    status_box_->setVisible(content == NetworkContent::status);
    state_title_->setVisible(content != NetworkContent::status);
    state_detail_->setVisible(content == NetworkContent::not_fitted);
    switch (content) {
    case NetworkContent::loading: state_title_->setText(tr("Loading...")); break;
    case NetworkContent::unavailable:
        state_title_->setText(tr("Econet status unavailable"));
        break;
    case NetworkContent::not_fitted:
        state_title_->setText(tr("Econet Not Fitted"));
        state_detail_->setText(tr("Econet hardware is not fitted in this machine configuration."));
        break;
    case NetworkContent::status: break;
    }
    if (content != NetworkContent::status) {
        return;
    }
    const auto& status = *inputs.status;
    const bool current = status_current(inputs);
    banner_->setVisible(paused_banner(inputs));

    link_->setText(QString::fromStdString(link_text(inputs)));
    link_dot_->setPixmap(dot(current && status.connected()
                                 ? QColor(0x2e, 0x9e, 0x44)
                                 : palette().color(QPalette::PlaceholderText)));

    const StationPresentation station = station_presentation(status);
    in_force_->setText(QString::number(station.in_force));
    in_force_->setEnabled(current);  // last-known reads secondary
    pending_->setVisible(station.pending.has_value());
    break_->setVisible(station.pending.has_value());
    if (station.pending) {
        pending_->setText(QStringLiteral("→ %1").arg(*station.pending));
        const QString key = break_key_ ? break_key_() : QString();
        break_->setText(key.isEmpty() ? tr("Break") : key);
        break_->setToolTip(tr("%1 takes effect at the next Break. Click to press Break.")
                               .arg(*station.pending));
    }
    break_->setEnabled(current);
    edit_->setEnabled(current);
    if (!current) {
        editor_->hide();
    }

    const QString collision = QString::fromStdString(collision_text(inputs));
    collision_->setText(collision.isEmpty() ? QString() : QStringLiteral("⚠ ") + collision);
    collision_->setVisible(!collision.isEmpty());
    stale_->setVisible(inputs.server_live && inputs.stream_lost);
    present_transports();
}

void NetworkSidebar::present_transports() {
    const bool current = status_current(econet_.inputs());
    transports_unavailable_->setVisible(econet_.transports_failed());
    // Every listed transport with a UI, by its opaque id.
    QHash<QString, ExtensionPanel*> keep;
    for (const auto& transport : econet_.transports()) {
        if (!transport.has_ui()) {
            continue;
        }
        const QString id = QString::fromStdString(transport.id());
        ExtensionPanel* panel = panels_.take(id);
        if (!panel) {
            panel = new ExtensionPanel(extensions_, transport.id(), shares_, status_box_);
            transports_layout_->addWidget(panel);
        }
        keep.insert(id, panel);
    }
    for (ExtensionPanel* gone : std::as_const(panels_)) {
        gone->hide();
        gone->deleteLater();
    }
    panels_ = keep;
    for (ExtensionPanel* panel : std::as_const(panels_)) {
        panel->setEnabled(current);
    }
}

void NetworkSidebar::open_editor() {
    const auto& status = econet_.inputs().status;
    if (!status) {
        return;
    }
    field_->setPlaceholderText(QString::fromStdString(station_placeholder(*status)));
    field_->setText(QString::number(station_presentation(*status).in_force));
    field_->selectAll();
    error_->clear();
    error_->hide();
    const QString key = break_key_ ? break_key_() : QString();
    info_->setText(key.isEmpty() ? tr("Takes effect on next Break.")
                                 : tr("Takes effect on next Break (%1).").arg(key));
    editor_->show();
    field_->setFocus();
}

void NetworkSidebar::save_station() {
    const auto& status = econet_.inputs().status;
    if (!status) {
        return;
    }
    const auto entry = station_entry(*status, field_->text().toStdString());
    if (!entry.value) {
        error_->setText(QString::fromStdString(entry.error));
        error_->show();
        return;
    }
    editor_->hide();
    econet_.set_station(*entry.value);
}

}  // namespace beebium::linux_client
