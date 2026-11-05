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

#ifndef BEEBIUM_LINUX_APP_CONFIGURATION_ATTACHMENT_CHOOSER_HPP
#define BEEBIUM_LINUX_APP_CONFIGURATION_ATTACHMENT_CHOOSER_HPP

#include "app/NewMachineDialog.hpp"
#include "preset/PresetSchema.hpp"

#include <QDialog>

#include <string>

class QLabel;
class QListWidget;
class QPushButton;

namespace beebium::linux_client {

// The entry for an extension, from its catalogue text.
ChooserEntry chooser_entry(const SchemaExtension& extension, const PresetSchema& schema);

// The attachment chooser (client specification 5.2), as the macOS
// client's: the candidates by name on the left, the selected one's
// description, needs and provides on the right, and the commit disabled
// until one is selected.
class AttachmentChooser : public QDialog {
    Q_OBJECT

public:
    AttachmentChooser(AttachmentChoice choice, QWidget* parent = nullptr);

    std::string chosen() const;
    QListWidget* list() const { return list_; }
    QPushButton* commit() const { return commit_; }
    QLabel* detail() const { return detail_; }

private:
    void show_detail();

    AttachmentChoice choice_;
    QListWidget* list_;
    QLabel* title_;
    QLabel* detail_;
    QPushButton* commit_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CONFIGURATION_ATTACHMENT_CHOOSER_HPP
