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

#ifndef BEEBIUM_LINUX_APP_EXTENSION_VIEW_WIDGET_HPP
#define BEEBIUM_LINUX_APP_EXTENSION_VIEW_WIDGET_HPP

#include "connection/ExtensionDispatch.hpp"

#include "extension_ui.pb.h"

#include <QHash>
#include <QWidget>

#include <functional>
#include <optional>

class QMenu;
class QVBoxLayout;

namespace beebium::linux_client {

class ExtensionNode;

// An extension's view rendered by control type, recursively (client
// specification 12.1): the eleven primitives, each a widget named by its
// control id, kept across views so a new view patches the panel in place.
// Every action is dispatched with the revision of the view on show, after
// any text edit in progress has been committed; nothing changes here until
// the server's next view says so.
class ExtensionViewWidget : public QWidget {
    Q_OBJECT

public:
    using Dispatch = std::function<void(const beebium::DispatchRequest&)>;
    using Shares = std::function<bool()>;
    // Opens an editor tree for the user and returns the values they commit,
    // or nothing when they cancel: a dialog by default; tests answer it.
    using EditorRunner = std::function<std::optional<EditorValues>(
        const QString& title, const beebium::Control& editor, const QString& commit_label)>;
    // Asks the user to confirm an action that carries a warning.
    using Confirmer = std::function<bool(const QString& title, const QString& warning)>;

    ExtensionViewWidget(Dispatch dispatch, Shares shares, QWidget* parent = nullptr);
    ~ExtensionViewWidget() override;

    void set_view(const beebium::View& view);
    // The server is lost: the last view stays, dimmed and inert.
    void set_stale(bool stale);
    bool stale() const { return stale_; }

    void set_editor_runner(EditorRunner runner) { run_editor_ = std::move(runner); }
    void set_confirmer(Confirmer confirm) { confirm_ = std::move(confirm); }

    // The widget showing a control, by id; for tests.
    QWidget* control_widget(const QString& id) const;

    // For the nodes.
    void act(const beebium::DispatchRequest& request);
    // A text field's edit, committed on Return or focus loss.
    void commit_text(const std::string& control_id, const std::string& value);
    const beebium::View& view() const { return view_; }
    bool server_shares_filesystem() const { return shares_ && shares_(); }
    std::optional<EditorValues> run_editor(const QString& title, const beebium::Control& editor,
                                           const QString& commit_label);
    bool confirm(const QString& title, const QString& warning);
    // The node for a control, reused when its id and type are unchanged.
    ExtensionNode* node_for(const beebium::Control& control, QWidget* parent);

private:
    void flush_pending_edits(const std::string& except);

    Dispatch dispatch_;
    Shares shares_;
    EditorRunner run_editor_;
    Confirmer confirm_;
    beebium::View view_;
    bool stale_ = false;
    QVBoxLayout* layout_;
    ExtensionNode* root_ = nullptr;
    QHash<QString, ExtensionNode*> nodes_;
    QHash<QString, ExtensionNode*> previous_;
};

// One control's widget. Patched in place by update() as views arrive.
class ExtensionNode : public QWidget {
    Q_OBJECT

public:
    ExtensionNode(ExtensionViewWidget& owner, beebium::Control::ControlCase kind, QWidget* parent);

    beebium::Control::ControlCase kind() const { return kind_; }
    virtual void update(const beebium::Control& control) = 0;
    // A text edit not yet sent; committed before any other action.
    virtual bool pending_edit() const { return false; }
    virtual void commit_edit() {}
    // The node's menu (a file reference's, or an editable list item's),
    // built for showing; the caller owns it. Null where there is none.
    virtual QMenu* make_menu(const std::string& item_id = {}) {
        (void)item_id;
        return nullptr;
    }

protected:
    friend class ExtensionViewWidget;
    ExtensionViewWidget& owner_;
    std::string id_;

private:
    beebium::Control::ControlCase kind_;
};

// The colour an indicator state is drawn in, and its dot.
QColor indicator_colour(beebium::Indicator::State state);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_EXTENSION_VIEW_WIDGET_HPP
