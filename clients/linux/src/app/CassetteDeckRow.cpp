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

#include "app/CassetteDeckRow.hpp"

#include "app/ClientSettings.hpp"
#include "app/ElidedLabel.hpp"
#include "app/FileReveal.hpp"
#include "app/SessionDiscs.hpp"
#include "app/StorageSidebar.hpp"

#include "cassette.pb.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <memory>

namespace beebium::linux_client {

QStyle::StandardPixmap cassette_error_icon(int kind) {
    switch (kind) {
    case beebium::CASSETTE_ERROR_KIND_DECK_EMPTY:
    case beebium::CASSETTE_ERROR_KIND_DECK_OCCUPIED:
    case beebium::CASSETTE_ERROR_KIND_READ_ONLY:
    case beebium::CASSETTE_ERROR_KIND_INVALID_POSITION:
        return QStyle::SP_MessageBoxInformation;
    default:
        return QStyle::SP_MessageBoxWarning;
    }
}

QString create_tape_error_sentence(int kind) {
    switch (kind) {
    case beebium::CREATE_TAPE_ERROR_KIND_UNKNOWN_LABEL:
        return QObject::tr("That blank tape template is not available.");
    case beebium::CREATE_TAPE_ERROR_KIND_DESTINATION_EXISTS:
        return QObject::tr("A file already exists there.");
    case beebium::CREATE_TAPE_ERROR_KIND_WRITE_FAILED:
        return QObject::tr("The tape image could not be written.");
    case beebium::CREATE_TAPE_ERROR_KIND_WRONG_EXTENSION:
        return QObject::tr("That name must end in .uef for a tape.");
    default:
        return QObject::tr("The tape image could not be created.");
    }
}

namespace {

QPixmap dot(const QColor& colour, int size) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawEllipse(1, 1, size - 2, size - 2);
    return pixmap;
}

// The Record key's red, drawn rather than themed, so it is red everywhere;
// grey while it cannot be pressed.
QIcon record_icon() {
    QIcon icon;
    icon.addPixmap(dot(QColor(0xd7, 0x26, 0x1e), 20), QIcon::Normal);
    icon.addPixmap(dot(QColor(0xa0, 0xa0, 0xa0, 0x80), 20), QIcon::Disabled);
    return icon;
}

QIcon themed(const char* name, QStyle::StandardPixmap fallback, const QWidget* widget) {
    return QIcon::fromTheme(QString::fromLatin1(name), widget->style()->standardIcon(fallback));
}

}  // namespace

CassetteDeckRow::CassetteDeckRow(SessionCassette& cassette, ClientSettings* settings, Shares shares,
                                 QWidget* parent)
    : QWidget(parent), cassette_(cassette), settings_(settings), shares_(std::move(shares)) {
    setObjectName(QStringLiteral("cassetteDeck"));
    setAcceptDrops(true);
    setAutoFillBackground(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(4);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Cassette"), this);
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    // The relay as the deck reads it, as a floppy row shows its motor; the
    // cassette motor LED stays in the indicator row, not duplicated here.
    motor_ = new QLabel(this);
    motor_->setPixmap(dot(QColor(0x2e, 0xb8, 0x4d), 10));
    motor_->setToolTip(tr("Motor running"));
    motor_->hide();
    header->addWidget(title);
    header->addWidget(motor_);
    header->addStretch(1);
    layout->addLayout(header);

    name_ = new ElidedLabel(this);
    name_->setTextInteractionFlags(Qt::NoTextInteraction);
    state_ = new ElidedLabel(this);
    metadata_ = new ElidedLabel(this);
    metadata_->setEnabled(false);  // the secondary text colour
    layout->addWidget(name_);
    layout->addWidget(state_);
    layout->addWidget(metadata_);

    // The counter, the tape's length beside it when known, and beneath them
    // the slider the user scrubs with: the only guide to where anything is
    // on the tape, as on a real deck.
    position_line_ = new QWidget(this);
    auto* position = new QVBoxLayout(position_line_);
    position->setContentsMargins(0, 0, 0, 0);
    position->setSpacing(2);
    auto* counters = new QHBoxLayout;
    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    counter_ = new QLabel(position_line_);
    counter_->setObjectName(QStringLiteral("tapeCounter"));
    counter_->setFont(fixed);
    counter_->setToolTip(tr("Tape counter"));
    duration_ = new QLabel(position_line_);
    duration_->setFont(fixed);
    duration_->setEnabled(false);
    duration_->setToolTip(tr("Length of the tape"));
    counters->addWidget(counter_);
    counters->addStretch(1);
    counters->addWidget(duration_);
    position->addLayout(counters);
    slider_ = new QSlider(Qt::Horizontal, position_line_);
    slider_->setObjectName(QStringLiteral("tapePosition"));
    slider_->setToolTip(tr("Drag to wind the tape; it moves when you let go"));
    position->addWidget(slider_);
    layout->addWidget(position_line_);

    message_line_ = new QWidget(this);
    auto* message = new QHBoxLayout(message_line_);
    message->setContentsMargins(0, 0, 0, 0);
    message_icon_ = new QLabel(message_line_);
    message_icon_->setAlignment(Qt::AlignTop);
    // The server's sentence in full: it wraps rather than ending in an
    // ellipsis.
    message_text_ = new QLabel(message_line_);
    message_text_->setWordWrap(true);
    message_text_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    eject_offer_ = new QPushButton(tr("Eject"), message_line_);
    eject_offer_->setToolTip(tr("Eject the tape in the deck"));
    dismiss_ = new QToolButton(message_line_);
    dismiss_->setIcon(themed("window-close", QStyle::SP_TitleBarCloseButton, this));
    dismiss_->setAutoRaise(true);
    dismiss_->setToolTip(tr("Dismiss this message"));
    message->addWidget(message_icon_, 0, Qt::AlignTop);
    message->addWidget(message_text_, 1);
    message->addWidget(eject_offer_, 0, Qt::AlignTop);
    message->addWidget(dismiss_, 0, Qt::AlignTop);
    layout->addWidget(message_line_);
    message_line_->hide();

    // The keys, after the macOS client's deck: Browse with its menu, then
    // Rewind, Back to the beginning, Play, Stop, Record and Fast forward,
    // and Eject at the end.
    auto* keys = new QHBoxLayout;
    keys->setSpacing(0);
    browse_ = new QToolButton(this);
    browse_->setObjectName(QStringLiteral("browseTape"));
    browse_->setText(tr("Browse..."));
    browse_->setIcon(themed("folder", QStyle::SP_DirIcon, this));
    browse_->setPopupMode(QToolButton::MenuButtonPopup);
    menu_ = new QMenu(browse_);
    browse_->setMenu(menu_);
    keys->addWidget(browse_);
    keys->addStretch(1);
    const auto add_key = [&](CassetteKey key, const QString& name, const QString& tip, QIcon icon) {
        auto* button = new QToolButton(this);
        button->setObjectName(name);
        button->setText(tip);
        button->setToolTip(tip);
        button->setIcon(icon);
        keys->addWidget(button);
        keys_[key] = button;
    };
    add_key(CassetteKey::rewind, QStringLiteral("rewind"), tr("Rewind"),
            themed("media-seek-backward", QStyle::SP_MediaSeekBackward, this));
    // Back to the beginning: a seek to 0, the instant return to the start.
    add_key(CassetteKey::seek, QStringLiteral("beginning"), tr("Back to the beginning"),
            themed("media-skip-backward", QStyle::SP_MediaSkipBackward, this));
    add_key(CassetteKey::play, QStringLiteral("play"), tr("Play"),
            themed("media-playback-start", QStyle::SP_MediaPlay, this));
    add_key(CassetteKey::stop, QStringLiteral("stop"), tr("Stop"),
            themed("media-playback-stop", QStyle::SP_MediaStop, this));
    add_key(CassetteKey::record, QStringLiteral("record"), tr("Record"), record_icon());
    add_key(CassetteKey::fast_forward, QStringLiteral("fastForward"), tr("Fast forward"),
            themed("media-seek-forward", QStyle::SP_MediaSeekForward, this));
    keys->addStretch(1);
    add_key(CassetteKey::eject, QStringLiteral("ejectTape"), tr("Eject tape"),
            themed("media-eject", QStyle::SP_ArrowUp, this));
    layout->addLayout(keys);
    for (QToolButton* button : findChildren<QToolButton*>()) {
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        if (button != dismiss_) {
            button->setIconSize(QSize(20, 20));
        }
    }
    // The keys stay the machine's: nothing here takes the focus.
    for (QWidget* widget : findChildren<QWidget*>()) {
        widget->setFocusPolicy(Qt::NoFocus);
    }

    connect(browse_, &QToolButton::clicked, this, &CassetteDeckRow::browse);
    connect(menu_, &QMenu::aboutToShow, this, &CassetteDeckRow::rebuild_menu);
    for (const auto& [key, button] : keys_) {
        connect(button, &QToolButton::clicked, this, [this, key = key] {
            if (key == CassetteKey::seek) {
                clear_message();
                cassette_.seek(0.0);
            } else {
                press(key);
            }
        });
    }
    connect(eject_offer_, &QPushButton::clicked, this, [this] { press(CassetteKey::eject); });
    connect(dismiss_, &QToolButton::clicked, this, &CassetteDeckRow::clear_message);
    // The slider seeks when let go, not while it moves; while it is held the
    // counter shows where the tape would go.
    connect(slider_, &QSlider::sliderMoved, this, [this](int value) {
        counter_->setText(tape_counter_text(static_cast<double>(value) / SLIDER_SCALE));
    });
    connect(slider_, &QSlider::sliderReleased, this, &CassetteDeckRow::seek_to_slider);
    // A click on the groove steps the slider without a drag: a seek too.
    connect(slider_, &QSlider::actionTriggered, this, [this](int action) {
        if (action != QAbstractSlider::SliderMove && action != QAbstractSlider::SliderNoAction) {
            QTimer::singleShot(0, this, &CassetteDeckRow::seek_to_slider);
        }
    });

    connect(&cassette_, &SessionCassette::deck_changed, this, &CassetteDeckRow::refresh);
    connect(&cassette_, &SessionCassette::insert_finished, this,
            [this](const QString& path, const CassetteOutcome& outcome) {
                if (outcome.ok) {
                    // A recent tape is one the server took (9.2).
                    if (settings_) {
                        settings_->remember_tape(path);
                    }
                    return;
                }
                CassetteMessage failure;
                failure.kind = CassetteMessage::Kind::failure;
                if (!outcome.reached) {
                    failure.text = tr("Beebium could not reach the cassette deck to insert %1.")
                                       .arg(QFileInfo(path).fileName());
                    failure.detail = outcome.message;
                } else {
                    failure.text = outcome.message;
                    failure.icon = cassette_error_icon(outcome.kind);
                    failure.offer_eject = outcome.kind == beebium::CASSETTE_ERROR_KIND_DECK_OCCUPIED;
                }
                show_failure(failure);
            });
    connect(&cassette_, &SessionCassette::key_finished, this,
            [this](CassetteKey, const CassetteOutcome& outcome) {
                if (outcome.ok) {
                    return;
                }
                CassetteMessage failure;
                failure.kind = CassetteMessage::Kind::failure;
                if (!outcome.reached) {
                    failure.text = tr("Beebium could not reach the cassette deck.");
                    failure.detail = outcome.message;
                } else {
                    failure.text = outcome.message;
                    failure.icon = cassette_error_icon(outcome.kind);
                }
                show_failure(failure);
            });
    connect(&cassette_, &SessionCassette::create_finished, this,
            [this](const QString&, const CassetteOutcome& outcome) {
                if (!outcome.ok) {
                    CassetteMessage failure;
                    failure.kind = CassetteMessage::Kind::failure;
                    failure.text = outcome.reached ? create_tape_error_sentence(outcome.kind)
                                                   : tr("Beebium could not reach the cassette deck.");
                    failure.detail = outcome.message;
                    show_failure(failure);
                    return;
                }
                // Then the second step: insert what was made, stopped at the
                // start, ready for Record; it enters the recents as any
                // insert does.
                insert(outcome.created_path);
            });
    notice_timer_.setSingleShot(true);
    notice_timer_.setInterval(NOTICE_MS);
    connect(&notice_timer_, &QTimer::timeout, this, [this] {
        if (message_.kind == CassetteMessage::Kind::notice) {
            clear_message();
        }
    });
    refresh();
}

void CassetteDeckRow::refresh() {
    // While the tape moves only its position changes, four times a second:
    // the counter, the slider and Back to the beginning are all it redraws
    // (on a Pi every repaint beside the picture copies the window).
    DeckState still = deck();
    still.position_seconds = 0.0;
    const bool local = shares_ && shares_();
    if (presented_ && still == *presented_ && local == presented_local_) {
        present_position();
        return;
    }
    const bool inserted = deck().tape_inserted;
    // An eject ends what a failure on the old tape said.
    if (presented_ && presented_->tape_inserted && !inserted &&
        message_.kind == CassetteMessage::Kind::failure) {
        message_ = {};
    }
    presented_ = still;
    presented_local_ = local;
    present();
}

void CassetteDeckRow::present() {
    const DeckState& deck = this->deck();
    const bool local = shares_ && shares_();
    const bool inserted = deck.tape_inserted;
    const auto transport = deck.transport;
    motor_->setVisible(deck.motor_on);

    // A tape's name is the deck's main line; an empty deck's hint is set a
    // level down, smaller and in the secondary colour, as an empty drive's.
    QFont font = this->font();
    if (!inserted) {
        font.setPointSizeF(font.pointSizeF() * DriveRow::HINT_SCALE);
    }
    font.setWeight(inserted ? QFont::Medium : QFont::Normal);
    if (name_->font() != font) {
        name_->setFont(font);
    }
    name_->setEnabled(inserted);
    if (inserted) {
        name_->set_whole(deck.tape.name);
        name_->set_note(path_for_disc_url(deck.tape.url));
        metadata_->set_whole(tape_description(deck.tape).join(QStringLiteral(" - ")));
    } else {
        name_->set_whole(local ? tr("Empty - Drop tape image here")
                               : tr("Empty - server is on another host"));
        name_->set_note({});
        metadata_->set_whole({});
    }
    state_->set_whole(deck_state_text(deck));
    state_->setVisible(inserted);
    metadata_->setVisible(inserted && !metadata_->whole().isEmpty());
    position_line_->setVisible(inserted);
    const double duration = deck.tape.duration_seconds;
    // An unknown length has the counter alone, with no proportional slider.
    duration_->setVisible(duration > 0.0);
    slider_->setVisible(duration > 0.0);
    duration_->setText(duration > 0.0 ? tape_counter_text(duration) : QString());
    if (duration > 0.0) {
        slider_->setRange(0, static_cast<int>(std::lround(duration * SLIDER_SCALE)));
    }
    present_position();

    const bool messaging = message_.kind != CassetteMessage::Kind::none;
    message_line_->setVisible(messaging);
    message_text_->setText(message_.text);
    message_text_->setToolTip(message_.detail);
    eject_offer_->setVisible(message_.kind == CassetteMessage::Kind::failure && message_.offer_eject &&
                             inserted);
    dismiss_->setVisible(message_.kind == CassetteMessage::Kind::failure);

    // Browse hands the server a path: an empty deck, a server sharing this
    // filesystem (3.2).
    browse_->setEnabled(!inserted && local);
    browse_->setToolTip(local ? tr("Choose a tape image to insert; the arrow offers recent and "
                                   "blank tapes")
                              : tr("The server is on another host, so it cannot open files here"));
    key(CassetteKey::rewind)->setEnabled(inserted && transport != DeckState::Transport::rewinding);
    key(CassetteKey::play)->setEnabled(inserted && transport != DeckState::Transport::playing);
    key(CassetteKey::stop)->setEnabled(inserted && transport != DeckState::Transport::stopped);
    key(CassetteKey::fast_forward)
        ->setEnabled(inserted && transport != DeckState::Transport::fast_forwarding);
    key(CassetteKey::eject)->setEnabled(inserted);
    // Disabled with its reason for a tape Beebium only reads.
    QToolButton* record = key(CassetteKey::record);
    record->setEnabled(inserted && deck.tape.recordable &&
                       transport != DeckState::Transport::recording);
    record->setToolTip(!inserted ? tr("Insert a tape to record")
                       : !deck.tape.recordable
                           ? tr("This tape is a %1 image, and Beebium records only on UEF tapes")
                                 .arg(deck.tape.format)
                           : tr("Record"));
}

void CassetteDeckRow::present_position() {
    // Held, the slider and the counter are the user's until let go.
    if (slider_->isSliderDown()) {
        return;
    }
    const double position = deck().position_seconds;
    counter_->setText(tape_counter_text(position));
    if (deck().tape.duration_seconds > 0.0) {
        slider_->setValue(static_cast<int>(std::lround(position * SLIDER_SCALE)));
    }
    key(CassetteKey::seek)->setEnabled(deck().tape_inserted && position > 0.0);
}

void CassetteDeckRow::seek_to_slider() {
    clear_message();
    cassette_.seek(static_cast<double>(slider_->sliderPosition()) / SLIDER_SCALE);
}

QString CassetteDeckRow::name_text() const {
    return name_->whole();
}

QString CassetteDeckRow::state_text() const {
    return state_->isVisibleTo(this) ? state_->whole() : QString();
}

QString CassetteDeckRow::metadata_text() const {
    return metadata_->isVisibleTo(this) ? metadata_->whole() : QString();
}

QString CassetteDeckRow::counter_text() const {
    return position_line_->isVisibleTo(this) ? counter_->text() : QString();
}

QString CassetteDeckRow::duration_text() const {
    return duration_->isVisibleTo(this) ? duration_->text() : QString();
}

QString CassetteDeckRow::message_text() const {
    return message_line_->isVisibleTo(this) ? message_text_->text() : QString();
}

bool CassetteDeckRow::motor_shown() const {
    return motor_->isVisibleTo(this);
}

void CassetteDeckRow::show_failure(const CassetteMessage& message) {
    notice_timer_.stop();
    message_ = message;
    message_.kind = CassetteMessage::Kind::failure;
    message_icon_->setPixmap(style()->standardIcon(message_.icon).pixmap(14, 14));
    present();
}

void CassetteDeckRow::show_notice(const QString& text) {
    // A failure still standing is not hidden by a passing notice.
    if (message_.kind == CassetteMessage::Kind::failure) {
        return;
    }
    message_ = {};
    message_.kind = CassetteMessage::Kind::notice;
    message_.text = text;
    message_.icon = QStyle::SP_MessageBoxInformation;
    message_icon_->setPixmap(style()->standardIcon(message_.icon).pixmap(14, 14));
    notice_timer_.start();
    present();
}

void CassetteDeckRow::clear_message() {
    notice_timer_.stop();
    if (message_.kind == CassetteMessage::Kind::none) {
        return;
    }
    message_ = {};
    present();
}

void CassetteDeckRow::insert(const QString& path) {
    clear_message();
    cassette_.insert(path);
}

void CassetteDeckRow::press(CassetteKey key) {
    clear_message();
    cassette_.press(key);
}

void CassetteDeckRow::browse() {
    // A convenience: the server judges the format by content, so any file
    // can still be chosen.
    const QString filter = tr("Tape images (*.uef *.csw *.wav);;All files (*)");
    const QStringList recents = settings_ ? settings_->recent_tapes() : QStringList{};
    const QString start =
        recents.isEmpty() ? QDir::homePath() : QFileInfo(recents.front()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Insert a Tape"), start, filter);
    if (!path.isEmpty()) {
        insert(path);
    }
}

void CassetteDeckRow::rebuild_menu() {
    menu_->clear();
    menu_->addAction(tr("Browse..."), this, &CassetteDeckRow::browse);
    QMenu* recent = menu_->addMenu(tr("Recent"));
    const QStringList paths = settings_ ? settings_->recent_tapes() : QStringList{};
    const QStringList labels = recent_disc_labels(paths);
    for (int i = 0; i < paths.size(); ++i) {
        const QString path = paths.at(i);
        QAction* action = recent->addAction(labels.at(i), this, [this, path] { insert(path); });
        action->setToolTip(path);
        // A tape since moved or deleted is shown, not hidden, but cannot be
        // chosen.
        action->setEnabled(QFileInfo::exists(path));
    }
    if (paths.isEmpty()) {
        recent->addAction(tr("No recent tapes"))->setEnabled(false);
    } else {
        recent->addSeparator();
        recent->addAction(tr("Clear Recent Tapes"), this, [this] {
            if (settings_) {
                settings_->clear_tapes();
            }
        });
    }
    menu_->setToolTipsVisible(true);
    recent->setToolTipsVisible(true);
    // The server's templates, each by its own label, which carries its
    // length ("Blank C30 (30 minutes)").
    QMenu* blank = menu_->addMenu(tr("New Blank Tape"));
    for (const BlankTapeTemplate& tape : cassette_.blank_tapes()) {
        blank->addAction(tape.label, this, [this, tape] { new_blank_tape(tape); });
    }
    blank->setEnabled(!cassette_.blank_tapes().empty());
}

void CassetteDeckRow::new_blank_tape(const BlankTapeTemplate& blank) {
    // Only the template's own extension is offered, and added if the name
    // lacks it; the server refuses any other (9.0.1).
    const QString extension = blank.extension();
    const QString suggested = QDir(QDir::homePath()).filePath(blank.label + extension);
    QString path = QFileDialog::getSaveFileName(
        this, tr("New Blank Tape"), suggested,
        tr("%1 tape image (*%2)").arg(extension.mid(1).toUpper(), extension));
    if (path.isEmpty()) {
        return;
    }
    if (!path.toLower().endsWith(extension)) {
        path += extension;
    }
    clear_message();
    cassette_.create_tape(blank.label, path);
}

QString CassetteDeckRow::drop_refusal() const {
    if (!(shares_ && shares_())) {
        return tr("Server is on another host");
    }
    if (deck().tape_inserted) {
        return tr("Eject tape first");
    }
    return {};
}

void CassetteDeckRow::drop_files(const QStringList& paths) {
    // The state may have changed during the drag.
    const QString refusal = drop_refusal();
    if (!refusal.isEmpty()) {
        show_notice(refusal);
        return;
    }
    if (paths.size() != 1) {
        show_notice(tr("Drop a single tape image"));
        return;
    }
    insert(paths.front());
}

void CassetteDeckRow::set_highlighted(bool highlighted) {
    setBackgroundRole(highlighted ? QPalette::Highlight : QPalette::Window);
    setForegroundRole(highlighted ? QPalette::HighlightedText : QPalette::WindowText);
}

void CassetteDeckRow::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    // Judged on what is known without the file: the deck and the server.
    const QString refusal = drop_refusal();
    if (!refusal.isEmpty()) {
        show_notice(refusal);
        event->ignore();
        return;
    }
    clear_message();
    set_highlighted(true);
    event->acceptProposedAction();
}

void CassetteDeckRow::dragLeaveEvent(QDragLeaveEvent* event) {
    set_highlighted(false);
    QWidget::dragLeaveEvent(event);
}

void CassetteDeckRow::dropEvent(QDropEvent* event) {
    set_highlighted(false);
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        }
    }
    event->acceptProposedAction();
    drop_files(paths);
}

QMenu* CassetteDeckRow::make_path_menu() {
    if (!deck().tape_inserted || deck().tape.url.isEmpty()) {
        return nullptr;
    }
    const QString path = path_for_disc_url(deck().tape.url);
    auto* menu = new QMenu(this);
    menu->setToolTipsVisible(true);
    menu->addAction(tr("Copy Path"), this, [path] { QApplication::clipboard()->setText(path); });
    // Only where the path names this host's file (3.2, 11).
    QAction* reveal =
        menu->addAction(tr("Open Containing Folder"), this, [path] { reveal_file(path); });
    const QString refusal = reveal_refusal(path, shares_ && shares_());
    reveal->setEnabled(refusal.isEmpty());
    reveal->setToolTip(refusal);
    return menu;
}

void CassetteDeckRow::contextMenuEvent(QContextMenuEvent* event) {
    std::unique_ptr<QMenu> menu(make_path_menu());
    if (menu) {
        menu->exec(event->globalPos());
    }
}

}  // namespace beebium::linux_client
