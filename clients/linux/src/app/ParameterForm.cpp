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

#include "app/ParameterForm.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QMetaObject>
#include <QVBoxLayout>

namespace beebium::linux_client {

ParameterForm::ParameterForm(ParameterFormModel model, MachineDraft& draft, QWidget* parent)
    : QWidget(parent), model_(std::move(model)), draft_(draft) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    // Paths here are this host's: the machine runs on it.
    renderer_ = new ExtensionViewWidget(
        [this](const beebium::DispatchRequest& request) { act(request); }, [] { return true; },
        this);
    layout->addWidget(renderer_);
    refresh();
}

void ParameterForm::set_serial_ports(const QStringList& ports) {
    std::vector<std::string> paths;
    for (const QString& port : ports) {
        paths.push_back(port.toStdString());
    }
    model_.set_serial_ports(std::move(paths));
    refresh();
}

void ParameterForm::set_diagnostics(std::vector<PresetDiagnostic> diagnostics) {
    diagnostics_ = std::move(diagnostics);
    refresh();
}

void ParameterForm::refresh() {
    refresh_pending_ = false;
    beebium::View view = model_.view(draft_, diagnostics_);
    view.set_view_revision(++revision_);
    renderer_->set_view(view);
}

void ParameterForm::act(const beebium::DispatchRequest& request) {
    FormOutcome outcome = model_.apply(request, draft_);
    if (outcome.browse) {
        if (const auto path = choose(*outcome.browse)) {
            model_.set_path(draft_, outcome.browse->key, path->toStdString());
            outcome.edited = true;
        }
    }
    if (outcome.edited) {
        Q_EMIT edited();
    }
    // Redrawn after the renderer has finished acting, never during.
    if (!refresh_pending_) {
        refresh_pending_ = true;
        QMetaObject::invokeMethod(
            this, [this] {
                if (refresh_pending_) {
                    refresh();
                }
            },
            Qt::QueuedConnection);
    }
}

std::optional<QString> ParameterForm::choose(const FormBrowse& browse) {
    if (chooser_) {
        return chooser_(browse);
    }
    const QString title = QString::fromStdString(browse.label);
    const QString current = QString::fromStdString(browse.current);
    QString chosen;
    switch (browse.chooser) {
    case FormBrowse::Chooser::open_file:
        chosen = QFileDialog::getOpenFileName(this, title, current);
        break;
    case FormBrowse::Chooser::save_file:
        chosen = QFileDialog::getSaveFileName(this, title, current);
        break;
    case FormBrowse::Chooser::directory:
        chosen = QFileDialog::getExistingDirectory(this, title, current);
        break;
    }
    if (chosen.isEmpty()) {
        return std::nullopt;
    }
    return chosen;
}

}  // namespace beebium::linux_client
