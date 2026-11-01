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

#include "app/ErrorPresentation.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace beebium::linux_client {

QString connection_failure_sentence(ConnectionFailure failure, const QString& endpoint) {
    switch (failure) {
    case ConnectionFailure::unreachable:
        return QObject::tr("Beebium could not reach a machine at %1.").arg(endpoint);
    case ConnectionFailure::incompatible:
        return QObject::tr("The machine at %1 cannot be used with this version of Beebium.")
            .arg(endpoint);
    case ConnectionFailure::error:
        break;
    }
    return QObject::tr("The machine at %1 refused the connection.").arg(endpoint);
}

ServerMessageDialog::ServerMessageDialog(const QString& title, const QString& sentence,
                                         const QString& server_message, QWidget* parent)
    : QDialog(parent), server_message_(server_message) {
    setWindowTitle(title);
    auto* layout = new QVBoxLayout(this);

    auto* heading = new QHBoxLayout;
    auto* icon = new QLabel;
    icon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(32, 32));
    icon->setAlignment(Qt::AlignTop);
    heading->addWidget(icon);
    auto* sentence_label = new QLabel(sentence);
    sentence_label->setWordWrap(true);
    sentence_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    heading->addWidget(sentence_label, 1);
    layout->addLayout(heading);

    if (!server_message.isEmpty()) {
        // The server's words, whole: wrapped, selectable, never clipped.
        auto* message = new QPlainTextEdit(server_message);
        message->setObjectName(QStringLiteral("serverMessage"));
        message->setReadOnly(true);
        message->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        message->setMinimumHeight(fontMetrics().lineSpacing() * 4);
        layout->addWidget(message);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    if (!server_message.isEmpty()) {
        auto* copy = buttons->addButton(tr("Copy Message"), QDialogButtonBox::ActionRole);
        // An action-role button would close the dialog; this one only copies.
        copy->setAutoDefault(false);
        disconnect(copy, nullptr, buttons, nullptr);
        connect(copy, &QPushButton::clicked, this,
                [this] { QApplication::clipboard()->setText(server_message_); });
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(520, sizeHint().height());
}

}  // namespace beebium::linux_client
