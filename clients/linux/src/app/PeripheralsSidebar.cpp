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

#include "app/PeripheralsSidebar.hpp"

#include "app/ExtensionPanel.hpp"
#include "app/SessionPeripherals.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

constexpr int GROUP_INDENT = 16;
constexpr int NODE_INDENT = 32;
constexpr int LEVEL_INDENT = 16;
const std::string SERIAL_PORT = "serial-port";

QLabel* secondary(const QString& text, QWidget* parent, double scale = 1.0) {
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * scale);
    label->setFont(font);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
    return label;
}

QWidget* indented(QWidget* child, int indent, QWidget* parent) {
    auto* holder = new QWidget(parent);
    auto* layout = new QHBoxLayout(holder);
    layout->setContentsMargins(indent, 0, 0, 0);
    child->setParent(holder);
    layout->addWidget(child);
    return holder;
}

}  // namespace

PeripheralsSidebar::PeripheralsSidebar(SessionPeripherals& peripherals,
                                       SessionExtensions& extensions,
                                       ExtensionViewWidget::Shares shares, QWidget* parent)
    : QWidget(parent), peripherals_(peripherals), extensions_(extensions),
      shares_(std::move(shares)) {
    setObjectName(QStringLiteral("peripheralsMode"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFocusPolicy(Qt::NoFocus);
    scroll->viewport()->setAutoFillBackground(false);
    outer->addWidget(scroll);
    content_ = new QWidget(scroll);
    content_->setAutoFillBackground(false);
    layout_ = new QVBoxLayout(content_);
    layout_->setContentsMargins(0, 8, 12, 8);
    layout_->setSpacing(6);
    scroll->setWidget(content_);

    connect(&peripherals_, &SessionPeripherals::changed, this, &PeripheralsSidebar::rebuild);
    connect(&peripherals_, &SessionPeripherals::serial_changed, this,
            &PeripheralsSidebar::show_serial);
    rebuild();
}

void PeripheralsSidebar::showEvent(QShowEvent* event) {
    peripherals_.set_watching_serial(true);
    QWidget::showEvent(event);
}

void PeripheralsSidebar::hideEvent(QHideEvent* event) {
    peripherals_.set_watching_serial(false);
    QWidget::hideEvent(event);
}

void PeripheralsSidebar::rebuild() {
    // The set is fixed for the server's life: rebuilt on a new listing only.
    while (QLayoutItem* item = layout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    serial_line_ = nullptr;
    const PeripheralTree& tree = peripherals_.tree();
    const auto state = [this](const QString& title, const QString& detail) {
        auto* box = new QWidget(content_);
        auto* layout = new QVBoxLayout(box);
        layout->setContentsMargins(GROUP_INDENT, 4, 0, 0);
        auto* heading = new QLabel(title, box);
        layout->addWidget(heading);
        if (!detail.isEmpty()) {
            layout->addWidget(secondary(detail, box, 0.9));
        }
        layout_->addWidget(box);
        layout_->addStretch(1);
    };
    if (!peripherals_.loaded()) {
        state(tr("Loading..."), {});
        return;
    }
    if (tree.empty()) {
        if (!peripherals_.error().isEmpty()) {
            state(tr("Couldn't load peripherals"), peripherals_.error());
        } else {
            state(tr("No Peripherals"),
                  tr("This machine is configured without any external peripherals."));
        }
        return;
    }
    bool first = true;
    const auto section = [&](const QString& title, const std::vector<PeripheralNode>& nodes,
                             bool serial) {
        if (!first) {
            auto* line = new QFrame(content_);
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Sunken);
            layout_->addWidget(line);
        }
        first = false;
        auto* heading = new QLabel(title, content_);
        QFont bold = heading->font();
        bold.setBold(true);
        heading->setFont(bold);
        layout_->addWidget(indented(heading, GROUP_INDENT, content_));
        if (serial) {
            serial_line_ = secondary(QString(), content_);
            layout_->addWidget(indented(serial_line_, NODE_INDENT, content_));
            show_serial();
        }
        for (const auto& node : nodes) {
            add_node(node, 0, layout_);
        }
    };
    for (const auto& group : tree.groups) {
        section(QString::fromStdString(group.display_name), group.nodes,
                group.point_id == SERIAL_PORT);
    }
    if (!tree.unattached.empty()) {
        section(tr("Unattached"), tree.unattached, false);
    }
    layout_->addStretch(1);
}

void PeripheralsSidebar::add_node(const PeripheralNode& node, int depth, QVBoxLayout* into) {
    const int indent = NODE_INDENT + LEVEL_INDENT * depth;
    auto* row = new QWidget(content_);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(indent, 0, 0, 0);
    auto* name = new QLabel(QString::fromStdString(node.name), row);
    QFont semibold = name->font();
    semibold.setWeight(QFont::DemiBold);
    name->setFont(semibold);
    name->setToolTip(QString::fromStdString(node.description));
    row_layout->addWidget(name, 1);
    for (const auto& point : node.also) {
        auto* badge = secondary(tr("also: %1").arg(QString::fromStdString(point)), row, 0.8);
        badge->setWordWrap(false);
        badge->setFrameShape(QFrame::StyledPanel);
        row_layout->addWidget(badge);
    }
    into->addWidget(row);
    if (node.has_ui) {
        // Subservient to its node: subscribed while it shows.
        auto* panel = new ExtensionPanel(extensions_, node.id, shares_, content_);
        into->addWidget(indented(panel, indent + LEVEL_INDENT, content_));
    }
    for (const auto& child : node.children) {
        add_node(child, depth + 1, into);
    }
}

void PeripheralsSidebar::show_serial() {
    if (!serial_line_) {
        return;
    }
    const QString text = QString::fromStdString(peripherals_.serial().text());
    serial_line_->setText(text);
    serial_line_->parentWidget()->setVisible(!text.isEmpty());
}

}  // namespace beebium::linux_client
