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

#ifndef BEEBIUM_LINUX_APP_ERROR_PRESENTATION_HPP
#define BEEBIUM_LINUX_APP_ERROR_PRESENTATION_HPP

#include "app/MachineSession.hpp"

#include <QDialog>
#include <QString>

class QWidget;

namespace beebium::linux_client {

// The client's sentence for a failed connection (client specification 3.4):
// a full sentence of its own, never the server's words rewritten. The
// server's message is shown beside it, verbatim.
QString connection_failure_sentence(ConnectionFailure failure, const QString& endpoint);

// A client sentence with the server's message beneath it, complete, wrapped,
// selectable and with a Copy button, so a long diagnostic is never truncated
// or retyped (3.4). Modal to `parent`.
class ServerMessageDialog : public QDialog {
    Q_OBJECT

public:
    ServerMessageDialog(const QString& title, const QString& sentence,
                        const QString& server_message, QWidget* parent = nullptr);

    QString server_message() const { return server_message_; }

private:
    QString server_message_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_ERROR_PRESENTATION_HPP
