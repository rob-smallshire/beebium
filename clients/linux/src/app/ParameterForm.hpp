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

#ifndef BEEBIUM_LINUX_APP_PARAMETER_FORM_HPP
#define BEEBIUM_LINUX_APP_PARAMETER_FORM_HPP

#include "app/ExtensionViewWidget.hpp"
#include "preset/ParameterFormModel.hpp"

#include <QWidget>

#include <functional>
#include <optional>

namespace beebium::linux_client {

// An extension's (or transport's) parameters as a form over a machine
// draft (client specification 12.2): the model's View in the live views'
// renderer, each action applied to the draft at once and the form redrawn
// after it, so an edit in progress lands before another control acts.
// Browse asks the platform's chooser of the kind the parameter needs.
class ParameterForm : public QWidget {
    Q_OBJECT

public:
    // Asks for a path; nothing when the user cancels. The platform's
    // choosers by default; tests answer it.
    using Chooser = std::function<std::optional<QString>(const FormBrowse& browse)>;

    ParameterForm(ParameterFormModel model, MachineDraft& draft, QWidget* parent = nullptr);

    void set_chooser(Chooser chooser) { chooser_ = std::move(chooser); }
    void set_serial_ports(const QStringList& ports);
    void set_diagnostics(std::vector<PresetDiagnostic> diagnostics);
    // Redraw from the draft as it is now.
    void refresh();

    ExtensionViewWidget* renderer() const { return renderer_; }

Q_SIGNALS:
    // The draft changed through this form.
    void edited();

private:
    void act(const beebium::DispatchRequest& request);
    std::optional<QString> choose(const FormBrowse& browse);

    ParameterFormModel model_;
    MachineDraft& draft_;
    std::vector<PresetDiagnostic> diagnostics_;
    Chooser chooser_;
    ExtensionViewWidget* renderer_;
    bool refresh_pending_ = false;
    std::uint64_t revision_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_PARAMETER_FORM_HPP
