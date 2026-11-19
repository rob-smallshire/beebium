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
#include "app/DialogLocations.hpp"
#include "app/ElidedLabel.hpp"
#include "app/FileReveal.hpp"
#include "app/LiveLevelMeter.hpp"
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
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <memory>

namespace beebium::linux_client {

QStyle::StandardPixmap cassette_error_icon(int kind) {
    switch (kind) {
    case beebium::CASSETTE_ERROR_KIND_DECK_EMPTY:
    case beebium::CASSETTE_ERROR_KIND_DECK_OCCUPIED:
    case beebium::CASSETTE_ERROR_KIND_READ_ONLY:
    case beebium::CASSETTE_ERROR_KIND_INVALID_POSITION:
    case beebium::CASSETTE_ERROR_KIND_LIVE_SOURCE:
    case beebium::CASSETTE_ERROR_KIND_NOT_READY:
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

// The Record key, drawn rather than themed: in the keys' own colour, grey
// while down with the tape still, and faint while it cannot be pressed. It
// is red only while tape is being recorded, drawn so by tinted() (client
// specification 9.0).
QIcon record_icon(const QColor& key_colour) {
    QIcon icon;
    icon.addPixmap(dot(key_colour, 20), QIcon::Normal, QIcon::Off);
    icon.addPixmap(dot(QColor(0x80, 0x80, 0x80), 20), QIcon::Normal, QIcon::On);
    for (QIcon::State state : {QIcon::Off, QIcon::On}) {
        icon.addPixmap(dot(QColor(0xa0, 0xa0, 0xa0, 0x80), 20), QIcon::Disabled, state);
    }
    return icon;
}

// A key's icon drawn solid in one colour, its shape kept: how a key shows
// tape moving, on any theme's icons and on light and dark palettes alike.
QIcon tinted(const QIcon& plain, const QColor& colour) {
    QIcon icon;
    for (int size : {16, 20, 24, 32, 40}) {
        for (QIcon::State state : {QIcon::Off, QIcon::On}) {
            QPixmap pixmap = plain.pixmap(QSize(size, size), QIcon::Normal, state);
            if (pixmap.isNull()) {
                continue;
            }
            QPainter painter(&pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(pixmap.rect(), colour);
            painter.end();
            icon.addPixmap(pixmap, QIcon::Normal, state);
        }
    }
    return icon;
}

// Whether a key is down for the deck as the server reports it: the
// current transport's key, and Play with Record, since Record implies
// Play. Stop, Back to the beginning and Eject are momentary.
bool key_down(CassetteKey key, const DeckState& deck) {
    if (!deck.tape_inserted) {
        return false;
    }
    using Transport = DeckState::Transport;
    switch (key) {
    case CassetteKey::play:
        return deck.transport == Transport::playing || deck.transport == Transport::recording;
    case CassetteKey::record: return deck.transport == Transport::recording;
    case CassetteKey::rewind: return deck.transport == Transport::rewinding;
    case CassetteKey::fast_forward: return deck.transport == Transport::fast_forwarding;
    default: return false;
    }
}

QIcon themed(const char* name, QStyle::StandardPixmap fallback, const QWidget* widget) {
    return QIcon::fromTheme(QString::fromLatin1(name), widget->style()->standardIcon(fallback));
}

// Where the head is on the tape: a marker above a bar (client
// specification 9.0). A slider in all but its look, so it drags, steps on
// a click and seeks on release as one does; but it colours neither side of
// the bar, since a tape has no played and unplayed portion. The bar is in
// the accent colour where the signal map says the tape carries signal, and
// plain grey elsewhere and until there is a map.
class TapeTimeline : public QSlider {
public:
    explicit TapeTimeline(QWidget* parent) : QSlider(Qt::Horizontal, parent) {}

    void set_signal_map(const SignalMap& map) {
        if (map == map_) {
            return;
        }
        map_ = map;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QStyleOptionSlider option;
        initStyleOption(&option);
        // The style's own handle, so the marker sits where a press takes it.
        const QRect handle =
            style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        const qreal x = handle.center().x() + 0.5;
        const qreal half = handle.width() / 2.0;
        const qreal bar_y = height() - BAR_THICKNESS - 2.0;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        const QPalette::ColorGroup group = isEnabled() ? QPalette::Active : QPalette::Disabled;
        const QRectF bar(half, bar_y, width() - 2.0 * half, BAR_THICKNESS);
        QPainterPath bar_path;
        bar_path.addRoundedRect(bar, BAR_THICKNESS / 2.0, BAR_THICKNESS / 2.0);
        painter.setBrush(palette().color(group, QPalette::Mid));
        painter.drawPath(bar_path);
        // The signal, a run of lit columns at a time, within the bar's ends.
        const std::vector<bool> lit =
            signal_columns(map_.seconds, map_.levels, static_cast<int>(bar.width()));
        painter.save();
        painter.setClipPath(bar_path);
        const QColor accent = palette().color(group, QPalette::Highlight);
        for (std::size_t from = 0; from < lit.size();) {
            if (!lit[from]) {
                ++from;
                continue;
            }
            std::size_t to = from;
            while (to < lit.size() && lit[to]) {
                ++to;
            }
            painter.fillRect(QRectF(bar.left() + static_cast<qreal>(from), bar.top(),
                                    static_cast<qreal>(to - from), bar.height()),
                             accent);
            from = to;
        }
        painter.restore();
        // A pointer down onto the bar, its tip at the head.
        const qreal tip = bar_y - 1.0;
        const qreal top = std::max(1.0, tip - MARKER_HEIGHT);
        const qreal spread = (tip - top) * 0.6;
        const QPointF marker[] = {{x - spread, top}, {x + spread, top}, {x, tip}};
        painter.setBrush(palette().color(group, QPalette::WindowText));
        painter.drawPolygon(marker, 3);
    }

private:
    static constexpr qreal BAR_THICKNESS = 3.0;
    static constexpr qreal MARKER_HEIGHT = 9.0;
    SignalMap map_;
};

}  // namespace

const QColor CassetteDeckRow::PLAYING_COLOUR = QColor(0x2e, 0xb8, 0x4d);
const QColor CassetteDeckRow::RECORDING_COLOUR = QColor(0xd7, 0x26, 0x1e);

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
    // The ways in for an empty deck, on the header rather than among the
    // keys, so the key row keeps its edges for the two keys that lose the
    // user's place.
    browse_ = new QToolButton(this);
    browse_->setObjectName(QStringLiteral("browseTape"));
    browse_->setText(tr("Browse..."));
    browse_->setIcon(themed("folder", QStyle::SP_DirIcon, this));
    browse_->setPopupMode(QToolButton::MenuButtonPopup);
    menu_ = new QMenu(browse_);
    browse_->setMenu(menu_);
    header->addWidget(browse_);
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
    // the timeline the user scrubs with: the only guide to where anything
    // is on the tape, as on a real deck.
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
    slider_ = new TapeTimeline(position_line_);
    slider_->setObjectName(QStringLiteral("tapePosition"));
    slider_->setToolTip(tr("Drag to wind the tape; it moves when you let go"));
    position->addWidget(slider_);
    level_ = new LiveLevelMeter(position_line_);
    level_->setObjectName(QStringLiteral("liveLevel"));
    level_->setToolTip(tr("The level of the live input, in decibels. Keep the held peak near the "
                          "tick; turn the source down if the red mark appears."));
    level_->hide();
    position->addWidget(level_);
    layout->addWidget(position_line_);

    message_line_ = new QWidget(this);
    auto* message = new QHBoxLayout(message_line_);
    message->setContentsMargins(0, 0, 0, 0);
    message_icon_ = new QLabel(message_line_);
    message_icon_->setAlignment(Qt::AlignTop);
    // The server's sentence in full: it wraps rather than ending in an
    // ellipsis, and it and its detail beneath, in the secondary colour, can
    // be selected and copied (3.4).
    auto* words = new QVBoxLayout;
    words->setContentsMargins(0, 0, 0, 0);
    words->setSpacing(0);
    message_text_ = new QLabel(message_line_);
    message_text_->setObjectName(QStringLiteral("deckMessage"));
    message_text_->setWordWrap(true);
    message_text_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    message_text_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    message_detail_ = new QLabel(message_line_);
    message_detail_->setObjectName(QStringLiteral("deckMessageDetail"));
    message_detail_->setWordWrap(true);
    message_detail_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    message_detail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    message_detail_->setForegroundRole(QPalette::PlaceholderText);
    words->addWidget(message_text_);
    words->addWidget(message_detail_);
    eject_offer_ = new QPushButton(tr("Eject"), message_line_);
    eject_offer_->setToolTip(tr("Eject the tape in the deck"));
    dismiss_ = new QToolButton(message_line_);
    dismiss_->setIcon(themed("window-close", QStyle::SP_TitleBarCloseButton, this));
    dismiss_->setAutoRaise(true);
    dismiss_->setToolTip(tr("Dismiss this message"));
    copy_ = new QToolButton(message_line_);
    copy_->setObjectName(QStringLiteral("copyDeckMessage"));
    copy_->setIcon(themed("edit-copy", QStyle::SP_FileDialogDetailedView, this));
    copy_->setAutoRaise(true);
    copy_->setToolTip(tr("Copy this message"));
    message->addWidget(message_icon_, 0, Qt::AlignTop);
    message->addLayout(words, 1);
    message->addWidget(eject_offer_, 0, Qt::AlignTop);
    message->addWidget(copy_, 0, Qt::AlignTop);
    message->addWidget(dismiss_, 0, Qt::AlignTop);
    layout->addWidget(message_line_);
    message_line_->hide();

    // The keys (client specification 9.0), in the tape's own order from its
    // start on the left to its end on the right, as one group from the left
    // edge: Back to the beginning, Rewind, Stop, Play, Record, Fast forward,
    // To the end of the recordings; Eject, which takes the tape away, alone
    // at the right edge. They appear with the tape: an empty deck shows
    // only the ways to fill it.
    key_row_ = new QWidget(this);
    auto* keys = new QHBoxLayout(key_row_);
    keys->setContentsMargins(0, 0, 0, 0);
    keys->setSpacing(0);
    const auto add_key = [&](CassetteKey key, const QString& name, const QString& tip, QIcon icon) {
        auto* button = new QToolButton(key_row_);
        button->setObjectName(name);
        button->setText(tip);
        button->setToolTip(tip);
        button->setIcon(icon);
        keys->addWidget(button);
        keys_[key] = button;
        plain_icons_[key] = icon;
    };
    // Back to the beginning: a seek to 0, the instant return to the start.
    add_key(CassetteKey::seek, QStringLiteral("beginning"), tr("Back to the beginning"),
            themed("media-skip-backward", QStyle::SP_MediaSkipBackward, this));
    add_key(CassetteKey::rewind, QStringLiteral("rewind"), tr("Rewind"),
            themed("media-seek-backward", QStyle::SP_MediaSeekBackward, this));
    add_key(CassetteKey::stop, QStringLiteral("stop"), tr("Stop"),
            themed("media-playback-stop", QStyle::SP_MediaStop, this));
    add_key(CassetteKey::play, QStringLiteral("play"), tr("Play"),
            themed("media-playback-start", QStyle::SP_MediaPlay, this));
    add_key(CassetteKey::record, QStringLiteral("record"), tr("Record"),
            record_icon(palette().color(QPalette::Active, QPalette::ButtonText)));
    add_key(CassetteKey::fast_forward, QStringLiteral("fastForward"), tr("Fast forward"),
            themed("media-seek-forward", QStyle::SP_MediaSeekForward, this));
    // To the end of the recordings: the server seeks to where they end,
    // ready to record after them. Momentary, as Back to the beginning.
    add_key(CassetteKey::append, QStringLiteral("toEnd"), tr("To the end of the recordings"),
            themed("media-skip-forward", QStyle::SP_MediaSkipForward, this));
    // The transport keys latch, as a mechanical deck's do: the one down is
    // the deck's status display.
    for (CassetteKey latching :
         {CassetteKey::rewind, CassetteKey::play, CassetteKey::fast_forward, CassetteKey::record}) {
        keys_[latching]->setCheckable(true);
    }
    keys->addStretch(1);
    add_key(CassetteKey::eject, QStringLiteral("ejectTape"), tr("Eject tape"),
            themed("media-eject", QStyle::SP_ArrowUp, this));
    layout->addWidget(key_row_);
    for (QToolButton* button : findChildren<QToolButton*>()) {
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        if (button != dismiss_ && button != copy_) {
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
            // A key is down only as the server reports the deck, never for
            // the click; one already down does nothing more.
            const bool down = key_down(key, deck());
            latch_keys();
            if (down) {
                return;
            }
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
    connect(copy_, &QToolButton::clicked, this, [this] {
        QStringList whole{message_.text};
        if (!message_.detail.isEmpty()) {
            whole << message_.detail;
        }
        QApplication::clipboard()->setText(whole.join(QLatin1Char('\n')));
    });
    // The timeline seeks when let go, not while it moves; while it is held the
    // counter shows where the tape would go.
    connect(slider_, &QSlider::sliderMoved, this, [this](int value) {
        counter_->setText(tape_counter_text(static_cast<double>(value) / SLIDER_SCALE));
    });
    connect(slider_, &QSlider::sliderReleased, this, &CassetteDeckRow::seek_to_slider);
    // A click on the bar steps the marker without a drag: a seek too.
    connect(slider_, &QSlider::actionTriggered, this, [this](int action) {
        if (action != QAbstractSlider::SliderMove && action != QAbstractSlider::SliderNoAction) {
            QTimer::singleShot(0, this, &CassetteDeckRow::seek_to_slider);
        }
    });

    connect(&cassette_, &SessionCassette::deck_changed, this, &CassetteDeckRow::refresh);
    // The map comes ready, or after a recording anew, or goes with the tape.
    connect(&cassette_, &SessionCassette::signal_map_changed, this, [this] {
        static_cast<TapeTimeline*>(slider_)->set_signal_map(cassette_.signal_map());
    });
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
                    // A file the server will not take as a tape, a blank
                    // tape template among them (9.0.1), is not offered
                    // again; a refusal of the moment (an occupied deck)
                    // leaves it be.
                    if (outcome.kind == beebium::CASSETTE_ERROR_KIND_INVALID_IMAGE && settings_) {
                        settings_->forget_tape(path);
                    }
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
    inputs_ = [] { return audio_input_devices(); };
    set_capture(new LiveCapture(this));
    connect(&cassette_, &SessionCassette::live_ended, this, [this](const LiveSummary& summary) {
        if (capture_) {
            capture_->stop();
        }
        chunker_.reset();
        if (summary.outcome.ok) {
            return;
        }
        CassetteMessage failure;
        failure.kind = CassetteMessage::Kind::failure;
        if (!summary.outcome.reached) {
            failure.text = tr("Beebium could not reach the cassette deck to play the live input.");
            failure.detail = summary.outcome.message;
        } else {
            failure.text = summary.outcome.message;
            failure.icon = cassette_error_icon(summary.outcome.kind);
            failure.offer_eject =
                summary.outcome.kind == beebium::CASSETTE_ERROR_KIND_DECK_OCCUPIED;
        }
        show_failure(failure);
    });
    starve_timer_.setInterval(1000);
    connect(&starve_timer_, &QTimer::timeout, this, &CassetteDeckRow::take_status_second);
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
    // the counter and the timeline are all it redraws (on a Pi every
    // repaint beside the picture copies the window).
    DeckState still = deck();
    still.position_seconds = 0.0;
    still.live_ring_fill = 0.0;
    still.live_overruns = 0;
    still.live_underruns = 0;
    const bool local = shares_ && shares_();
    if (presented_ && still == *presented_ && local == presented_local_) {
        present_position();
        return;
    }
    const bool inserted = deck().tape_inserted;
    // An eject ends what a failure on the old tape said.
    if (presented_ && presented_->tape_inserted && !inserted &&
        message_.kind == CassetteMessage::Kind::failure && !message_.outlives_tape) {
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
    key_row_->setVisible(inserted);
    const double duration = deck.tape.duration_seconds;
    // An unknown length has the counter alone, with no proportional timeline.
    duration_->setVisible(duration > 0.0);
    slider_->setVisible(duration > 0.0 && !deck.live());
    level_->setVisible(deck.live());
    duration_->setText(duration > 0.0 ? tape_counter_text(duration) : QString());
    if (duration > 0.0) {
        slider_->setRange(0, static_cast<int>(std::lround(duration * SLIDER_SCALE)));
    }
    present_position();

    const bool messaging = message_.kind != CassetteMessage::Kind::none;
    message_line_->setVisible(messaging);
    message_text_->setText(message_.text);
    message_detail_->setText(message_.detail);
    message_detail_->setVisible(messaging && !message_.detail.isEmpty());
    eject_offer_->setVisible(message_.kind == CassetteMessage::Kind::failure && message_.offer_eject &&
                             inserted);
    dismiss_->setVisible(message_.kind == CassetteMessage::Kind::failure);
    copy_->setVisible(message_.kind == CassetteMessage::Kind::failure);

    // Browse hands the server a path, so its files want a server sharing
    // this filesystem (3.2); its menu's live inputs do not, so it is
    // offered for any empty deck.
    browse_->setEnabled(!inserted);
    browse_->setToolTip(local ? tr("Choose a tape image to insert; the arrow offers recent and "
                                   "blank tapes, and live inputs")
                              : tr("The server is on another host, so it cannot open files here; "
                                   "the arrow offers live inputs"));
    // A live input plays as it comes: no winding, seeking or recording.
    const bool live = deck.live();
    const QString live_reason = tr("A live input plays as it comes, so it cannot be wound, sought "
                                   "or recorded on");
    // A key is greyed only for what the deck cannot do, never to show the
    // transport: the key down shows that (client specification 9.0).
    key(CassetteKey::rewind)->setEnabled(inserted && !live);
    key(CassetteKey::rewind)->setToolTip(live ? live_reason : tr("Rewind"));
    key(CassetteKey::play)->setEnabled(inserted);
    key(CassetteKey::stop)->setEnabled(inserted);
    key(CassetteKey::fast_forward)->setEnabled(inserted && !live);
    key(CassetteKey::fast_forward)->setToolTip(live ? live_reason : tr("Fast forward"));
    key(CassetteKey::seek)->setEnabled(inserted && !live);
    key(CassetteKey::seek)->setToolTip(live ? live_reason : tr("Back to the beginning"));
    // Greyed whenever the server says it would refuse (a live input, the
    // signal map not ready yet), with its sentence as the reason.
    key(CassetteKey::append)->setEnabled(inserted && deck.append_refusal.isEmpty());
    key(CassetteKey::append)->setToolTip(deck.append_refusal.isEmpty()
                                             ? tr("To the end of the recordings")
                                             : deck.append_refusal);
    // Ejecting this client's own live input closes its stream, and the deck
    // lets the tape go once it has played what the server holds.
    const bool finishing = live && cassette_.live_finishing();
    key(CassetteKey::eject)->setEnabled(inserted && !finishing);
    key(CassetteKey::eject)->setToolTip(
        finishing ? tr("The live input ends once the deck has played what the server holds")
        : live && cassette_.streaming() ? tr("End the live input")
                                        : tr("Eject tape"));
    // Disabled with its reason for a tape Beebium only reads.
    QToolButton* record = key(CassetteKey::record);
    record->setEnabled(inserted && !live && deck.tape.recordable);
    // Why not, in the server's own sentence: the format, or a place a
    // recording could not be kept (9.0, The Record key).
    record->setToolTip(!inserted ? tr("Insert a tape to record")
                       : !deck.tape.record_refusal.isEmpty() ? deck.tape.record_refusal
                       : live                                ? live_reason
                       : !deck.tape.recordable ? tr("Beebium cannot record on this tape")
                                               : tr("Record"));
    // A live tape's counts are watched from its start, afresh for each.
    if (live && !starve_timer_.isActive()) {
        starve_.reset();
        starve_timer_.start();
    } else if (!live && starve_timer_.isActive()) {
        starve_timer_.stop();
    }
    latch_keys();
}

void CassetteDeckRow::latch_keys() {
    // Down for the transport selected; coloured while tape moves (9.0):
    // a play or a recording only with the relay closed, a wind always.
    const DeckState& deck = this->deck();
    for (const auto& [key, button] : keys_) {
        if (!button->isCheckable()) {
            continue;
        }
        const bool down = key_down(key, deck);
        button->setChecked(down);
        QColor colour;
        if (down) {
            const bool recording = deck.transport == DeckState::Transport::recording;
            if (key == CassetteKey::rewind || key == CassetteKey::fast_forward) {
                colour = palette().color(QPalette::Active, QPalette::Highlight);
            } else if (deck.motor_on) {
                colour = key == CassetteKey::record || recording ? RECORDING_COLOUR : PLAYING_COLOUR;
            }
        }
        const QRgb drawn = colour.isValid() ? colour.rgba() : 0;
        if (drawn_colours_[key] != drawn) {
            drawn_colours_[key] = drawn;
            button->setIcon(colour.isValid() ? tinted(plain_icons_[key], colour) : plain_icons_[key]);
        }
    }
}

void CassetteDeckRow::present_position() {
    // Held, the marker and the counter are the user's until let go.
    if (slider_->isSliderDown()) {
        return;
    }
    const double position = deck().position_seconds;
    counter_->setText(tape_counter_text(position));
    if (deck().tape.duration_seconds > 0.0) {
        slider_->setValue(static_cast<int>(std::lround(position * SLIDER_SCALE)));
    }
}

void CassetteDeckRow::seek_to_slider() {
    clear_message();
    cassette_.seek(static_cast<double>(slider_->sliderPosition()) / SLIDER_SCALE);
}

bool CassetteDeckRow::level_clip_lit() const {
    return level_->clip_lit();
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

QString CassetteDeckRow::message_detail_text() const {
    return message_detail_->isVisibleTo(this) ? message_detail_->text() : QString();
}

bool CassetteDeckRow::motor_shown() const {
    return motor_->isVisibleTo(this);
}

void CassetteDeckRow::show_failure(const CassetteMessage& message) {
    notice_timer_.stop();
    starve_shown_ = LiveStarveDetector::Condition::none;
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
    starve_shown_ = LiveStarveDetector::Condition::none;
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
    if (key == CassetteKey::eject && deck().live() && cassette_.streaming()) {
        // The last whole frames, then the end of the stream.
        if (capture_) {
            capture_->stop();
        }
        if (chunker_) {
            const QByteArray rest = chunker_->rest();
            if (!rest.isEmpty()) {
                cassette_.push_live(rest);
            }
        }
        cassette_.finish_live();
        return;
    }
    cassette_.press(key);
}

void CassetteDeckRow::take_status_second() {
    const DeckState& deck = this->deck();
    // This client's own stream only: its counts are this capture's.
    if (!deck.live() || !cassette_.streaming()) {
        return;
    }
    const auto condition = starve_.feed(deck.live_overruns, deck.live_underruns);
    // One message at a time, and never the same condition over itself.
    if (condition == LiveStarveDetector::Condition::none ||
        (message_.kind == CassetteMessage::Kind::failure && starve_shown_ == condition)) {
        return;
    }
    CassetteMessage failure;
    failure.kind = CassetteMessage::Kind::failure;
    failure.text = live_starve_sentence(condition);
    failure.detail = live_starve_detail(condition, deck.live_overruns, deck.live_underruns,
                                        static_cast<int>(deck.tape.sample_rate));
    show_failure(failure);
    starve_shown_ = condition;
}

void CassetteDeckRow::set_capture(LiveCapture* capture) {
    if (capture_ && capture_->parent() == this) {
        capture_->stop();
        delete capture_;
    }
    capture_ = capture;
    capture_->setParent(this);
    connect(capture_, &LiveCapture::chunk, this, [this](const QByteArray& samples) {
        if (!chunker_) {
            return;
        }
        if (live_format_) {
            const ChunkLevel level = chunk_level(samples, *live_format_, clip_carry_);
            clip_carry_ = level.end_run;
            level_->take(level);
        }
        for (const QByteArray& chunk : chunker_->feed(samples)) {
            cassette_.push_live(chunk);
        }
    });
    connect(capture_, &LiveCapture::lost, this, &CassetteDeckRow::input_lost);
}

void CassetteDeckRow::input_lost(LiveCapture::Loss loss) {
    // Nothing still held is worth playing out: the stream ends at once, the
    // server lets the tape go, and the starve this caused is no story.
    chunker_.reset();
    cassette_.cancel_live();
    starve_.reset();
    CassetteMessage failure;
    failure.kind = CassetteMessage::Kind::failure;
    failure.outlives_tape = true;
    const QString device = live_format_ ? live_format_->device_name : QString();
    if (loss == LiveCapture::Loss::disconnected) {
        failure.text = tr("The input device was disconnected while the live tape was playing.");
        failure.detail = device;
    } else {
        failure.text = tr("The input device changed while the live tape was playing; choose it "
                          "again from Input Device.");
        if (live_format_) {
            const auto described = [this](const LiveInputFormat& format) {
                return tr("%1 Hz, %2, %3")
                    .arg(format.sample_rate)
                    .arg(format.channels == 1 ? tr("1 channel")
                                              : tr("%1 channels").arg(format.channels))
                    .arg(format.sample == LiveInputFormat::Sample::s16 ? tr("16-bit")
                                                                       : tr("32-bit float"));
            };
            failure.detail = tr("%1: %2").arg(device, described(*live_format_));
            // And the format the host now gives, where it lists the device
            // in one the deck could take, and it differs.
            if (const auto now = current_format(live_device_id_, live_format_->channel);
                now && (now->sample_rate != live_format_->sample_rate ||
                        now->channels != live_format_->channels ||
                        now->sample != live_format_->sample)) {
                failure.detail = tr("%1, now %2").arg(failure.detail, described(*now));
            }
        }
    }
    show_failure(failure);
}

std::optional<LiveInputFormat> CassetteDeckRow::current_format(const QByteArray& id,
                                                               const QString& channel) const {
    if (!inputs_) {
        return std::nullopt;
    }
    for (const AudioInputDevice& device : inputs_()) {
        if (device.id == id) {
            return live_input_format(device, channel);
        }
    }
    return std::nullopt;
}

void CassetteDeckRow::choose_live_input(const AudioInputDevice& device, const QString& channel) {
    clear_message();
    const auto format = live_input_format(device, channel);
    if (!format) {
        CassetteMessage failure;
        failure.text = tr("The audio input %1 gives no sound in a form the cassette deck takes.")
                           .arg(device.name);
        show_failure(failure);
        return;
    }
    QString refusal;
    if (!capture_ || !capture_->start(device, *format, &refusal)) {
        CassetteMessage failure;
        failure.text = refusal;
        show_failure(failure);
        return;
    }
    chunker_ = std::make_unique<LiveChunker>(*format);
    live_format_ = *format;
    live_device_id_ = device.id;
    level_->reset();
    clip_carry_ = 0;
    cassette_.start_live(*format);
}

void CassetteDeckRow::browse() {
    if (!(shares_ && shares_())) {
        show_notice(tr("The server is on another host, so it cannot open files here"));
        return;
    }
    // A convenience: the server judges the format by content, so any file
    // can still be chosen.
    const QString filter = tr("Tape images (*.uef *.csw *.wav);;All files (*)");
    // Where Insert Tape last chose, else Documents (3.6).
    const QString path = open_file(this, DialogLocations(settings_), QStringLiteral("insert-tape"),
                                   tr("Insert a Tape"), filter);
    if (!path.isEmpty()) {
        insert(path);
    }
}

void CassetteDeckRow::rebuild_menu() {
    menu_->clear();
    const bool local = shares_ && shares_();
    const QString remote = tr("The server is on another host, so it cannot open files here");
    QAction* browse = menu_->addAction(tr("Browse..."), this, &CassetteDeckRow::browse);
    browse->setEnabled(local);
    browse->setToolTip(local ? QString() : remote);
    QMenu* recent = menu_->addMenu(tr("Recent"));
    recent->menuAction()->setEnabled(local);
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
    // length ("C30").
    QMenu* blank = menu_->addMenu(tr("New Blank Tape"));
    for (const BlankTapeTemplate& tape : cassette_.blank_tapes()) {
        blank->addAction(tape.label, this, [this, tape] { new_blank_tape(tape); });
    }
    blank->setEnabled(local && !cassette_.blank_tapes().empty());
    menu_->addSeparator();
    // The host's audio inputs by the platform's own names, every kind: a
    // line-in and a microphone are not told apart on every platform.
    QMenu* inputs = menu_->addMenu(tr("Input Device"));
    inputs->setToolTipsVisible(true);
    const std::vector<AudioInputDevice> devices = inputs_ ? inputs_() : std::vector<AudioInputDevice>{};
    for (const AudioInputDevice& device : devices) {
        if (device.preferred_channels >= 2) {
            // Either channel of two, the left the default as the server's.
            QMenu* channels = inputs->addMenu(device.name);
            channels->addAction(tr("Left Channel"), this,
                                [this, device] { choose_live_input(device, QStringLiteral("left")); });
            channels->addAction(tr("Right Channel"), this,
                                [this, device] { choose_live_input(device, QStringLiteral("right")); });
        } else {
            inputs->addAction(device.name, this, [this, device] { choose_live_input(device, {}); });
        }
    }
    if (devices.empty()) {
        inputs->addAction(tr("No audio inputs"))->setEnabled(false);
    }
}

void CassetteDeckRow::new_blank_tape(const BlankTapeTemplate& blank) {
    // Only the template's own extension is offered, and added if the name
    // lacks it; the server refuses any other (9.0.1).
    const QString extension = blank.extension();
    // The template's label as the name, where New Blank Tape last saved,
    // else Documents; never among the templates (3.6).
    QString path = save_file(this, DialogLocations(settings_), QStringLiteral("new-blank-tape"),
                             tr("New Blank Tape"), blank.label + extension,
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
