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

#include "app/MemorySidebar.hpp"

#include "app/SessionSideways.hpp"

#include <QContextMenuEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace beebium::linux_client {

namespace {

constexpr int PRIORITY_WIDTH = 22;
constexpr int PADLOCK_WIDTH = 20;
constexpr int STATUS_WIDTH = 34;

void make_secondary(QLabel* label, double scale) {
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * scale);
    label->setFont(font);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
}

// A one-line name elided in the middle, as macOS truncates it, never
// widening the sidebar.
class NameLabel : public QLabel {
public:
    NameLabel(QString whole, QWidget* parent) : QLabel(parent), whole_(std::move(whole)) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
        setText(whole_);
    }
    const QString& whole() const { return whole_; }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        setText(fontMetrics().elidedText(whole_, Qt::ElideMiddle, contentsRect().width()));
    }

private:
    QString whole_;
};

// A padlock, closed or open, from the theme or drawn.
QIcon padlock_icon(bool closed, const QColor& colour) {
    const QIcon themed = QIcon::fromTheme(closed ? QStringLiteral("object-locked")
                                                 : QStringLiteral("object-unlocked"));
    if (!themed.isNull()) {
        return themed;
    }
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(colour, 1.6));
    painter.setBrush(Qt::NoBrush);
    // The shackle: closed sits on both sides; open lifts one.
    if (closed) {
        painter.drawArc(QRectF(4.5, 1.5, 7, 9), 0, 180 * 16);
        painter.drawLine(QPointF(4.5, 6), QPointF(4.5, 8));
        painter.drawLine(QPointF(11.5, 6), QPointF(11.5, 8));
    } else {
        painter.drawArc(QRectF(7.5, 0.5, 7, 9), 0, 180 * 16);
        painter.drawLine(QPointF(14.5, 5), QPointF(14.5, 7));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawRoundedRect(QRectF(3, 7.5, 10, 7.5), 1.5, 1.5);
    painter.end();
    return QIcon(pixmap);
}

QString display_name(const SidewaysSocket& socket) {
    if (socket.rom.recognised) {
        const QString title = QString::fromStdString(socket.rom.title);
        return socket.rom.version.empty()
                   ? title
                   : title + QLatin1Char(' ') + QString::fromStdString(socket.rom.version);
    }
    if (socket.populated && !socket.image_filepath.empty()) {
        return QFileInfo(QString::fromStdString(socket.image_filepath)).fileName();
    }
    return {};
}

std::uint32_t priority(const SidewaysSocket& socket) {
    return socket.slots.empty() ? socket.index : socket.slots.back();
}

QString kind_name(const std::string& kind) {
    if (kind == "romfs") {
        return QStringLiteral("ROMFS");
    }
    QString name = QString::fromStdString(kind);
    if (!name.isEmpty()) {
        name[0] = name[0].toUpper();
    }
    return name;
}

QString tooltip(const SidewaysSocket& socket) {
    QStringList lines;
    if (!socket.label.empty()) {
        lines << QString::fromStdString(socket.label);
    }
    QStringList slots;
    for (auto slot : socket.slots) {
        slots << QString::number(slot);
    }
    lines << (slots.size() == 1 ? QObject::tr("Slot %1") : QObject::tr("Slots %1"))
                     .arg(slots.join(QStringLiteral(", "))) +
                 QObject::tr("  (priority %1)").arg(priority(socket));
    if (socket.rom.recognised && !socket.rom.kinds.empty()) {
        QStringList kinds;
        for (const auto& kind : socket.rom.kinds) {
            kinds << kind_name(kind);
        }
        lines << kinds.join(QStringLiteral(" / "));
    }
    if (!socket.image_filepath.empty()) {
        lines << QString::fromStdString(socket.image_filepath);
    }
    return lines.join(QLatin1Char('\n'));
}

}  // namespace

MemoryRow::MemoryRow(SessionSideways& sideways, const SidewaysSocket& socket, QWidget* parent)
    : QWidget(parent), sideways_(sideways), socket_(socket) {
    for (const SidewaysProtectionGroup* group : sideways_.topology().groups_covering(socket_)) {
        if (group->supports_write_protect && !write_group_) {
            write_group_ = *group;
        }
        if (group->supports_hide && !hide_group_) {
            hide_group_ = *group;
        }
    }
    protected_ = write_group_ && write_group_->write_protected;
    setToolTip(tooltip(socket_));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(6);
    priority_ = new QLabel(QString::number(priority(socket_)), this);
    make_secondary(priority_, 0.85);
    priority_->setFixedWidth(PRIORITY_WIDTH);
    priority_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(priority_);
    name_ = new NameLabel(display_name(socket_), this);
    layout->addWidget(name_, 1);

    // Only sideways RAM is writable, so only it has a padlock.
    padlock_ = new QToolButton(this);
    padlock_->setAutoRaise(true);
    padlock_->setFocusPolicy(Qt::NoFocus);  // the keys stay the machine's
    padlock_->setFixedWidth(PADLOCK_WIDTH);
    QSizePolicy keep = padlock_->sizePolicy();
    keep.setRetainSizeWhenHidden(true);
    padlock_->setSizePolicy(keep);
    const bool lockable = write_group_ && socket_.type == SidewaysSlotType::ram;
    padlock_->setVisible(lockable);
    if (lockable) {
        const QColor colour = protected_ ? palette().color(QPalette::Highlight)
                                         : palette().color(QPalette::PlaceholderText);
        padlock_->setIcon(padlock_icon(protected_, colour));
        padlock_->setToolTip(protected_ ? tr("Allow writes to this sideways RAM")
                                        : tr("Write-protect this sideways RAM"));
        // An action, not a toggle: the glyph is the server's state, read
        // back after the switch moves (client specification 11).
        connect(padlock_, &QToolButton::clicked, this,
                [this, id = write_group_->id, engaged = !protected_] {
                    sideways_.set_protection(id, SidewaysProtection::write_protect, engaged);
                });
    }
    layout->addWidget(padlock_);

    status_ = new QLabel(this);
    status_->setText(socket_.type == SidewaysSlotType::rom   ? QStringLiteral("ROM")
                     : socket_.type == SidewaysSlotType::ram ? QStringLiteral("RAM")
                                                             : QStringLiteral("—"));
    status_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    make_secondary(status_, 0.85);
    status_->setFixedWidth(STATUS_WIDTH);
    status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(status_);

    actions_ = new QToolButton(this);
    actions_->setAutoRaise(true);
    actions_->setFocusPolicy(Qt::NoFocus);
    actions_->setIcon(QIcon::fromTheme(QStringLiteral("view-more"),
                                       QIcon::fromTheme(QStringLiteral("open-menu"))));
    if (actions_->icon().isNull()) {
        actions_->setText(QStringLiteral("…"));
    }
    actions_->setToolTip(tr("Slot actions"));
    QSizePolicy keep_actions = actions_->sizePolicy();
    keep_actions.setRetainSizeWhenHidden(true);
    actions_->setSizePolicy(keep_actions);
    actions_->setVisible(actionable());
    connect(actions_, &QToolButton::clicked, this, [this] {
        std::unique_ptr<QMenu> menu(make_menu());
        menu->exec(actions_->mapToGlobal(QPoint(0, actions_->height())));
    });
    layout->addWidget(actions_);

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        if (actionable()) {
            std::unique_ptr<QMenu> menu(make_menu());
            menu->exec(mapToGlobal(at));
        }
    });
}

QString MemoryRow::priority_text() const {
    return priority_->text();
}

QString MemoryRow::name_text() const {
    return static_cast<const NameLabel*>(name_)->whole();
}

QString MemoryRow::status_text() const {
    return status_->text();
}

bool MemoryRow::actionable() const {
    return write_group_.has_value() || hide_group_.has_value() || !socket_.image_filepath.empty();
}

QMenu* MemoryRow::make_menu() {
    auto* menu = new QMenu;
    if (write_group_) {
        QAction* write = menu->addAction(tr("Write-Protect"));
        write->setCheckable(true);
        write->setChecked(write_group_->write_protected);
        const std::string id = write_group_->id;
        const bool engaged = !write_group_->write_protected;
        connect(write, &QAction::triggered, &sideways_, [sideways = &sideways_, id, engaged] {
            sideways->set_protection(id, SidewaysProtection::write_protect, engaged);
        });
    }
    if (hide_group_) {
        QAction* hide = menu->addAction(tr("Hide"));
        hide->setCheckable(true);
        hide->setChecked(hide_group_->hidden);
        const std::string id = hide_group_->id;
        const bool engaged = !hide_group_->hidden;
        connect(hide, &QAction::triggered, &sideways_, [sideways = &sideways_, id, engaged] {
            sideways->set_protection(id, SidewaysProtection::hide, engaged);
        });
    }
    return menu;
}

MemorySidebar::MemorySidebar(SessionSideways& sideways, QWidget* parent)
    : QWidget(parent), sideways_(sideways) {
    setObjectName(QStringLiteral("memoryMode"));
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
    layout->setContentsMargins(16, 8, 12, 8);
    layout->setSpacing(4);

    status_ = new QLabel(content);
    status_->setWordWrap(true);
    make_secondary(status_, 0.9);
    layout->addWidget(status_);
    header_ = new QLabel(tr("Sideways").toUpper(), content);
    make_secondary(header_, 0.85);
    QFont bold = header_->font();
    bold.setWeight(QFont::DemiBold);
    header_->setFont(bold);
    layout->addWidget(header_);
    rows_box_ = new QWidget(content);
    rows_layout_ = new QVBoxLayout(rows_box_);
    rows_layout_->setContentsMargins(0, 0, 0, 0);
    rows_layout_->setSpacing(0);
    layout->addWidget(rows_box_);
    layout->addStretch(1);
    scroll->setWidget(content);

    connect(&sideways_, &SessionSideways::changed, this, &MemorySidebar::rebuild);
    rebuild();
}

void MemorySidebar::showEvent(QShowEvent* event) {
    sideways_.set_watching(true);
    QWidget::showEvent(event);
}

void MemorySidebar::hideEvent(QHideEvent* event) {
    sideways_.set_watching(false);
    QWidget::hideEvent(event);
}

void MemorySidebar::rebuild() {
    for (MemoryRow* row : rows_) {
        row->deleteLater();
    }
    rows_.clear();
    while (QLayoutItem* item = rows_layout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    const SidewaysTopology& topology = sideways_.topology();
    if (!sideways_.loaded()) {
        status_->setText(tr("Reading sideways state…"));
    } else if (topology.sockets.empty()) {
        status_->setText(tr("This machine has no sideways memory."));
    }
    const bool listing = sideways_.loaded() && !topology.sockets.empty();
    status_->setVisible(!listing);
    header_->setVisible(listing);
    rows_box_->setVisible(listing);
    if (!listing) {
        return;
    }
    for (std::size_t i = 0; i < topology.sockets.size(); ++i) {
        if (i > 0) {
            auto* line = new QFrame(rows_box_);
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Sunken);
            rows_layout_->addWidget(line);
        }
        auto* row = new MemoryRow(sideways_, topology.sockets[i], rows_box_);
        rows_layout_->addWidget(row);
        rows_.push_back(row);
    }
}

}  // namespace beebium::linux_client
