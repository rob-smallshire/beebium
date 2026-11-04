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

#include "app/ExtensionViewWidget.hpp"

#include "app/FileReveal.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <memory>

namespace beebium::linux_client {

namespace {

QString text_of(const std::string& text) {
    return QString::fromStdString(text);
}

void make_secondary(QLabel* label, double scale = 1.0) {
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * scale);
    label->setFont(font);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
}

QPixmap dot(const QColor& colour, int size = 8) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawEllipse(0, 0, size, size);
    return pixmap;
}

// A "?" beside a field or list, its help as the tooltip: help is never
// rendered inline.
QToolButton* help_button(QWidget* parent) {
    auto* help = new QToolButton(parent);
    help->setText(QStringLiteral("?"));
    help->setAutoRaise(true);
    help->setFocusPolicy(Qt::NoFocus);
    return help;
}

void set_help(QToolButton* button, const std::string& help) {
    button->setToolTip(text_of(help));
    button->setVisible(!help.empty());
}

// --- the live primitives -------------------------------------------------------------

class LabelNode : public ExtensionNode {
public:
    LabelNode(ExtensionViewWidget& owner, QWidget* parent)
        : ExtensionNode(owner, beebium::Control::kLabel, parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        text_ = new QLabel(this);
        text_->setWordWrap(true);
        secondary_ = new QLabel(this);
        make_secondary(secondary_, 0.9);
        layout->addWidget(text_, 1);
        layout->addWidget(secondary_);
    }
    void update(const beebium::Control& control) override {
        text_->setText(text_of(control.label().text()));
        secondary_->setText(text_of(control.label().secondary_text()));
        secondary_->setVisible(!control.label().secondary_text().empty());
    }

private:
    QLabel* text_;
    QLabel* secondary_;
};

class IndicatorNode : public ExtensionNode {
public:
    IndicatorNode(ExtensionViewWidget& owner, QWidget* parent)
        : ExtensionNode(owner, beebium::Control::kIndicator, parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        dot_ = new QLabel(this);
        text_ = new QLabel(this);
        make_secondary(text_);
        layout->addWidget(dot_);
        layout->addWidget(text_, 1);
    }
    void update(const beebium::Control& control) override {
        dot_->setPixmap(dot(indicator_colour(control.indicator().state())));
        text_->setText(text_of(control.indicator().text()));
    }

private:
    QLabel* dot_;
    QLabel* text_;
};

class ToggleNode : public ExtensionNode {
public:
    ToggleNode(ExtensionViewWidget& owner, QWidget* parent)
        : ExtensionNode(owner, beebium::Control::kToggle, parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        box_ = new QCheckBox(this);
        box_->setFocusPolicy(Qt::NoFocus);  // the keys stay the machine's
        layout->addWidget(box_);
        connect(box_, &QCheckBox::clicked, this, [this](bool checked) {
            // Fire and observe: the box shows the server's value until the
            // next view says otherwise.
            box_->setChecked(server_value_);
            owner_.act(toggle_action(owner_.view(), id_, checked));
        });
    }
    void update(const beebium::Control& control) override {
        server_value_ = control.toggle().value();
        box_->setText(text_of(control.toggle().label()));
        box_->setChecked(server_value_);
    }

private:
    QCheckBox* box_;
    bool server_value_ = false;
};

class ButtonNode : public ExtensionNode {
public:
    ButtonNode(ExtensionViewWidget& owner, QWidget* parent)
        : ExtensionNode(owner, beebium::Control::kButton, parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        button_ = new QPushButton(this);
        button_->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button_);
        layout->addStretch(1);
        connect(button_, &QPushButton::clicked, this,
                [this] { owner_.act(button_action(owner_.view(), id_)); });
    }
    void update(const beebium::Control& control) override {
        button_->setText(text_of(control.button().label()));
        button_->setEnabled(control.button().enabled());
    }

private:
    QPushButton* button_;
};

class ChoiceNode : public ExtensionNode {
public:
    ChoiceNode(ExtensionViewWidget& owner, QWidget* parent)
        : ExtensionNode(owner, beebium::Control::kChoice, parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        label_ = new QLabel(this);
        combo_ = new QComboBox(this);
        combo_->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(label_);
        layout->addWidget(combo_, 1);
        connect(combo_, &QComboBox::activated, this, [this](int index) {
            const int server = server_index_;
            combo_->setCurrentIndex(server);
            if (index != server && index >= 0) {
                owner_.act(choice_action(owner_.view(), id_, static_cast<std::uint32_t>(index)));
            }
        });
    }
    void update(const beebium::Control& control) override {
        const auto& choice = control.choice();
        label_->setText(text_of(choice.label()));
        label_->setVisible(!choice.label().empty());
        QStringList options;
        for (const auto& option : choice.options()) {
            options << text_of(option);
        }
        QStringList current;
        for (int i = 0; i < combo_->count(); ++i) {
            current << combo_->itemText(i);
        }
        if (current != options) {
            combo_->clear();
            combo_->addItems(options);
        }
        server_index_ = static_cast<int>(choice.selected_index());
        combo_->setCurrentIndex(server_index_);
    }

private:
    QLabel* label_;
    QComboBox* combo_;
    int server_index_ = 0;
};

// A text field: edits stay local and commit on Return or focus loss; a
// view arriving mid-edit leaves the user's text alone.
class TextNode : public ExtensionNode {
public:
    TextNode(ExtensionViewWidget& owner, QWidget* parent)
        : ExtensionNode(owner, beebium::Control::kTextInput, parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        auto* heading = new QHBoxLayout;
        label_ = new QLabel(this);
        help_ = help_button(this);
        heading->addWidget(label_);
        heading->addWidget(help_);
        heading->addStretch(1);
        layout->addLayout(heading);
        edit_ = new QLineEdit(this);
        edit_->setFocusPolicy(Qt::ClickFocus);
        layout->addWidget(edit_);
        note_ = new QLabel(this);
        note_->setWordWrap(true);
        make_secondary(note_, 0.9);
        layout->addWidget(note_);
        connect(edit_, &QLineEdit::editingFinished, this, [this] {
            if (pending_edit()) {
                commit_edit();
            }
        });
    }
    void update(const beebium::Control& control) override {
        const auto& input = control.text_input();
        label_->setText(text_of(input.label()));
        label_->setVisible(!input.label().empty());
        set_help(help_, input.help());
        edit_->setPlaceholderText(text_of(input.placeholder()));
        note_->setText(text_of(input.note()));
        note_->setVisible(!input.note().empty());
        const QString value = text_of(input.value());
        // Mid-edit, the user's text stands; otherwise the server's.
        if (!edit_->hasFocus() || edit_->text() == server_value_) {
            edit_->setText(value);
        }
        server_value_ = value;
        sent_value_.reset();
    }
    bool pending_edit() const override {
        return edit_->text() != server_value_ && edit_->text() != sent_value_;
    }
    void commit_edit() override {
        sent_value_ = edit_->text();
        owner_.commit_text(id_, edit_->text().toStdString());
    }

private:
    QLabel* label_;
    QToolButton* help_;
    QLineEdit* edit_;
    QLabel* note_;
    QString server_value_;
    std::optional<QString> sent_value_;
};

// An open choice: one of the options, or whatever is typed; committed as
// text, as a field is.
class EditableChoiceNode : public ExtensionNode {
public:
    EditableChoiceNode(ExtensionViewWidget& owner, QWidget* parent)
        : ExtensionNode(owner, beebium::Control::kEditableChoice, parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        label_ = new QLabel(this);
        layout->addWidget(label_);
        combo_ = new QComboBox(this);
        combo_->setEditable(true);
        combo_->setInsertPolicy(QComboBox::NoInsert);
        combo_->setFocusPolicy(Qt::ClickFocus);
        layout->addWidget(combo_);
        connect(combo_->lineEdit(), &QLineEdit::editingFinished, this, [this] {
            if (pending_edit()) {
                commit_edit();
            }
        });
        connect(combo_, &QComboBox::activated, this, [this](int) {
            if (pending_edit()) {
                commit_edit();
            }
        });
    }
    void update(const beebium::Control& control) override {
        const auto& choice = control.editable_choice();
        label_->setText(text_of(choice.label()));
        label_->setVisible(!choice.label().empty());
        combo_->lineEdit()->setPlaceholderText(text_of(choice.placeholder()));
        const QString typed = combo_->currentText();
        const bool editing = combo_->hasFocus() && typed != server_value_;
        combo_->clear();
        for (const auto& option : choice.options()) {
            combo_->addItem(text_of(option));
        }
        server_value_ = text_of(choice.value());
        combo_->setEditText(editing ? typed : server_value_);
        sent_value_.reset();
    }
    bool pending_edit() const override {
        return combo_->currentText() != server_value_ && combo_->currentText() != sent_value_;
    }
    void commit_edit() override {
        sent_value_ = combo_->currentText();
        owner_.commit_text(id_, combo_->currentText().toStdString());
    }

private:
    QLabel* label_;
    QComboBox* combo_;
    QString server_value_;
    std::optional<QString> sent_value_;
};

class GroupNode : public ExtensionNode {
public:
    GroupNode(ExtensionViewWidget& owner, QWidget* parent)
        : ExtensionNode(owner, beebium::Control::kGroup, parent) {
        layout_ = new QVBoxLayout(this);
        layout_->setContentsMargins(0, 0, 0, 0);
        layout_->setSpacing(6);
        heading_ = new QLabel(this);
        QFont bold = heading_->font();
        bold.setBold(true);
        heading_->setFont(bold);
        make_secondary(heading_);
        layout_->addWidget(heading_);
    }
    void update(const beebium::Control& control) override {
        const auto& group = control.group();
        heading_->setText(group.has_label() ? text_of(group.label()) : QString());
        heading_->setVisible(group.has_label() && !group.label().empty());
        // The children in the view's order, each its own node patched in
        // place where its id and type hold.
        while (layout_->count() > 1) {
            delete layout_->takeAt(1);
        }
        for (const auto& child : group.controls()) {
            ExtensionNode* node = owner_.node_for(child, this);
            layout_->addWidget(node);
            node->show();
        }
    }

private:
    QVBoxLayout* layout_;
    QLabel* heading_;
};

// The current value, always on show, and an edit button opening the
// editor tree; its fields commit together, or not at all.
class ModalEditorNode : public ExtensionNode {
public:
    ModalEditorNode(ExtensionViewWidget& owner, QWidget* parent)
        : ExtensionNode(owner, beebium::Control::kModalEditor, parent) {
        layout_ = new QHBoxLayout(this);
        layout_->setContentsMargins(0, 0, 0, 0);
        edit_ = new QToolButton(this);
        edit_->setIcon(QIcon::fromTheme(QStringLiteral("document-edit"),
                                        style()->standardIcon(QStyle::SP_FileDialogDetailedView)));
        edit_->setAutoRaise(true);
        edit_->setFocusPolicy(Qt::NoFocus);
        edit_->setToolTip(tr("Edit"));
        edit_->setObjectName(QStringLiteral("edit"));
        layout_->addWidget(edit_);
        connect(edit_, &QToolButton::clicked, this, [this] { open_editor(); });
    }
    void update(const beebium::Control& control) override {
        editor_ = control.modal_editor();
        if (anchor_) {
            layout_->removeWidget(anchor_);
        }
        anchor_ = owner_.node_for(editor_.anchor(), this);
        layout_->insertWidget(0, anchor_, 1);
        anchor_->show();
        edit_->setVisible(editor_.editable());
    }
    void open_editor() {
        const QString commit = editor_.commit_role() == beebium::ModalEditor::ADD ? tr("Add")
                                                                                  : tr("Save");
        if (const auto values = owner_.run_editor(tr("Edit"), editor_.editor(), commit)) {
            owner_.act(editor_action(owner_.view(), id_, *values));
        }
    }

private:
    QHBoxLayout* layout_;
    QToolButton* edit_;
    ExtensionNode* anchor_ = nullptr;
    beebium::ModalEditor editor_;
};

constexpr int ITEM_ID_ROLE = Qt::UserRole;

class EditableListNode : public ExtensionNode {
public:
    EditableListNode(ExtensionViewWidget& owner, QWidget* parent)
        : ExtensionNode(owner, beebium::Control::kEditableList, parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        auto* heading = new QHBoxLayout;
        title_ = new QLabel(this);
        QFont small = title_->font();
        small.setWeight(QFont::DemiBold);
        title_->setFont(small);
        make_secondary(title_, 0.85);
        help_ = help_button(this);
        heading->addWidget(title_);
        heading->addWidget(help_);
        heading->addStretch(1);
        layout->addLayout(heading);
        list_ = new QListWidget(this);
        list_->setObjectName(QStringLiteral("items"));
        list_->setFocusPolicy(Qt::NoFocus);
        list_->setSelectionMode(QAbstractItemView::SingleSelection);
        list_->setContextMenuPolicy(Qt::CustomContextMenu);
        list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        layout->addWidget(list_);
        empty_ = new QLabel(this);
        make_secondary(empty_, 0.9);
        layout->addWidget(empty_);
        auto* buttons = new QHBoxLayout;
        buttons->setSpacing(0);
        add_ = new QToolButton(this);
        add_->setObjectName(QStringLiteral("add"));
        add_->setText(QStringLiteral("+"));
        add_->setToolTip(tr("Add"));
        remove_ = new QToolButton(this);
        remove_->setObjectName(QStringLiteral("remove"));
        remove_->setText(QStringLiteral("−"));
        remove_->setToolTip(tr("Remove"));
        for (QToolButton* button : {add_, remove_}) {
            button->setFocusPolicy(Qt::NoFocus);
            buttons->addWidget(button);
        }
        buttons->addStretch(1);
        layout->addLayout(buttons);

        connect(add_, &QToolButton::clicked, this, [this] { add(); });
        connect(remove_, &QToolButton::clicked, this, [this] {
            if (const auto* item = selected()) {
                owner_.act(list_action(owner_.view(), id_, beebium::EditableListEvent::REMOVE,
                                       item->id(), "", nullptr));
            }
        });
        connect(list_, &QListWidget::itemSelectionChanged, this, [this] { update_remove(); });
        connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
            edit(item->data(ITEM_ID_ROLE).toString().toStdString());
        });
        connect(list_, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
            QListWidgetItem* item = list_->itemAt(at);
            if (!item) {
                return;
            }
            std::unique_ptr<QMenu> menu(make_menu(item->data(ITEM_ID_ROLE).toString().toStdString()));
            if (menu && !menu->isEmpty()) {
                menu->exec(list_->viewport()->mapToGlobal(at));
            }
        });
    }

    void update(const beebium::Control& control) override {
        list_proto_ = control.editable_list();
        title_->setText(text_of(list_proto_.title()).toUpper());
        set_help(help_, list_proto_.help());
        const QString selected_id = list_->currentItem()
                                        ? list_->currentItem()->data(ITEM_ID_ROLE).toString()
                                        : QString();
        list_->clear();
        for (const auto& item : list_proto_.items()) {
            auto* row = new QListWidgetItem(list_);
            row->setData(ITEM_ID_ROLE, text_of(item.id()));
            auto* widget = row_widget(item);
            row->setSizeHint(widget->sizeHint());
            list_->setItemWidget(row, widget);
            if (text_of(item.id()) == selected_id) {
                list_->setCurrentItem(row);
            }
        }
        // As tall as its rows: the free space is the panel's.
        int height = 2 * list_->frameWidth();
        for (int row = 0; row < list_->count(); ++row) {
            height += list_->sizeHintForRow(row);
        }
        list_->setFixedHeight(height);
        const bool empty = list_proto_.items().empty();
        empty_->setText(text_of(list_proto_.empty_text()));
        empty_->setVisible(empty && !list_proto_.empty_text().empty());
        list_->setVisible(!empty);
        add_->setEnabled(list_proto_.can_add());
        update_remove();
    }

    // For tests and the context menu.
    QMenu* make_menu(const std::string& item_id) override {
        const auto* item = find(item_id);
        if (!item) {
            return nullptr;
        }
        auto* menu = new QMenu;
        if (item->editable()) {
            menu->addAction(tr("Edit..."), menu, [this, item_id] { edit(item_id); });
        }
        for (const auto& action : item->actions()) {
            menu->addAction(text_of(action.title()), menu,
                            [this, item_id, action] { run_action(item_id, action); });
        }
        return menu;
    }
    void add() {
        if (!list_proto_.can_add()) {
            return;
        }
        if (const auto values = owner_.run_editor(tr("Add"), list_proto_.add_editor(), tr("Add"))) {
            owner_.act(list_action(owner_.view(), id_, beebium::EditableListEvent::ADD, "", "",
                                   &*values));
        }
    }
    void edit(const std::string& item_id) {
        const auto* item = find(item_id);
        if (!item || !item->editable()) {
            return;
        }
        const beebium::Control editor = item->editor();
        if (const auto values = owner_.run_editor(tr("Edit"), editor, tr("Save"))) {
            owner_.act(list_action(owner_.view(), id_, beebium::EditableListEvent::EDIT, item_id,
                                   "", &*values));
        }
    }
    QListWidget* list() const { return list_; }

private:
    const beebium::EditableListItem* find(const std::string& item_id) const {
        for (const auto& item : list_proto_.items()) {
            if (item.id() == item_id) {
                return &item;
            }
        }
        return nullptr;
    }
    const beebium::EditableListItem* selected() const {
        return list_->currentItem()
                   ? find(list_->currentItem()->data(ITEM_ID_ROLE).toString().toStdString())
                   : nullptr;
    }
    void update_remove() {
        const auto* item = selected();
        remove_->setEnabled(item && item->removable());
    }
    void run_action(const std::string& item_id, const beebium::EditableListAction& action) {
        if (action.has_editor()) {
            // The action's own prefilled editor is the gate.
            if (const auto values =
                    owner_.run_editor(text_of(action.title()), action.editor(), text_of(action.title()))) {
                owner_.act(list_action(owner_.view(), id_, beebium::EditableListEvent::ACTION,
                                       item_id, action.id(), &*values));
            }
            return;
        }
        if (!action.warning().empty() &&
            !owner_.confirm(text_of(action.title()), text_of(action.warning()))) {
            return;
        }
        owner_.act(list_action(owner_.view(), id_, beebium::EditableListEvent::ACTION, item_id,
                               action.id(), nullptr));
    }
    QWidget* row_widget(const beebium::EditableListItem& item) {
        auto* widget = new QWidget;
        auto* layout = new QVBoxLayout(widget);
        layout->setContentsMargins(6, 3, 6, 3);
        layout->setSpacing(1);
        auto* top = new QHBoxLayout;
        auto* primary = new QLabel(text_of(item.primary()), widget);
        top->addWidget(primary, 1);
        if (!item.secondary().empty()) {
            auto* secondary = new QLabel(text_of(item.secondary()), widget);
            make_secondary(secondary, 0.9);
            top->addWidget(secondary);
        }
        if (item.state() != beebium::Indicator::UNKNOWN) {
            auto* state = new QLabel(widget);
            state->setPixmap(dot(indicator_colour(item.state())));
            top->addWidget(state);
        }
        layout->addLayout(top);
        if (!item.subtitle().empty()) {
            auto* subtitle = new QLabel(text_of(item.subtitle()), widget);
            make_secondary(subtitle, 0.9);
            layout->addWidget(subtitle);
        }
        if (!item.note().empty()) {
            auto* note = new QLabel(QStringLiteral("⚠ ") + text_of(item.note()), widget);
            note->setWordWrap(true);
            make_secondary(note, 0.9);
            layout->addWidget(note);
        }
        return widget;
    }

    QLabel* title_;
    QToolButton* help_;
    QListWidget* list_;
    QLabel* empty_;
    QToolButton* add_;
    QToolButton* remove_;
    beebium::EditableList list_proto_;
};

// A file on the server's host: its name, state and the server's actions,
// with Copy Path and Open Containing Folder of the client's own.
class FileReferenceNode : public ExtensionNode {
public:
    FileReferenceNode(ExtensionViewWidget& owner, QWidget* parent)
        : ExtensionNode(owner, beebium::Control::kFileReference, parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(1);
        auto* top = new QHBoxLayout;
        auto* icon = new QLabel(this);
        icon->setPixmap(QIcon::fromTheme(QStringLiteral("text-x-generic"),
                                         style()->standardIcon(QStyle::SP_FileIcon))
                            .pixmap(16, 16));
        name_ = new QLabel(this);
        state_ = new QLabel(this);
        menu_ = new QToolButton(this);
        menu_->setObjectName(QStringLiteral("actions"));
        menu_->setText(QStringLiteral("…"));
        menu_->setAutoRaise(true);
        menu_->setFocusPolicy(Qt::NoFocus);
        top->addWidget(icon);
        top->addWidget(name_, 1);
        top->addWidget(state_);
        top->addWidget(menu_);
        layout->addLayout(top);
        state_text_ = new QLabel(this);
        make_secondary(state_text_, 0.9);
        layout->addWidget(state_text_);
        connect(menu_, &QToolButton::clicked, this, [this] {
            std::unique_ptr<QMenu> menu(make_menu());
            menu->exec(menu_->mapToGlobal(QPoint(0, menu_->height())));
        });
    }
    void update(const beebium::Control& control) override {
        file_ = control.file_reference();
        const QString path = text_of(file_.path());
        const QString name = !file_.display_name().empty() ? text_of(file_.display_name())
                             : !path.isEmpty()            ? QFileInfo(path).fileName()
                                                          : QString();
        name_->setText(name);
        setToolTip(path);
        state_->setPixmap(dot(indicator_colour(file_.state())));
        state_->setVisible(file_.state() != beebium::Indicator::UNKNOWN);
        state_text_->setText(text_of(file_.state_text()));
        state_text_->setVisible(!file_.state_text().empty());
        std::unique_ptr<QMenu> menu(make_menu());
        menu_->setVisible(!menu->actions().isEmpty());
    }
    QMenu* make_menu(const std::string& = {}) override {
        auto* menu = new QMenu;
        menu->setToolTipsVisible(true);
        for (const auto& action : file_.actions()) {
            menu->addAction(text_of(action.title()), menu, [this, id = action.id()] {
                owner_.act(file_action(owner_.view(), id_, id));
            });
        }
        const QString path = text_of(file_.path());
        if (path.isEmpty()) {
            return menu;
        }
        if (!menu->actions().isEmpty()) {
            menu->addSeparator();
        }
        menu->addAction(tr("Copy Path"), menu,
                        [path] { QApplication::clipboard()->setText(path); });
        QAction* reveal =
            menu->addAction(tr("Open Containing Folder"), menu, [path] { reveal_file(path); });
        const QString refusal = reveal_refusal(path, owner_.server_shares_filesystem());
        reveal->setEnabled(refusal.isEmpty());
        reveal->setToolTip(refusal);
        return menu;
    }

private:
    QLabel* name_;
    QLabel* state_;
    QToolButton* menu_;
    QLabel* state_text_;
    beebium::FileReference file_;
};

class EmptyNode : public ExtensionNode {
public:
    EmptyNode(ExtensionViewWidget& owner, QWidget* parent)
        : ExtensionNode(owner, beebium::Control::CONTROL_NOT_SET, parent) {}
    void update(const beebium::Control&) override {}
};

// --- the editor form, for modal editors and list editors -------------------------------

// An editor tree's leaves bound to buffered values: nothing is dispatched
// until the whole form is committed.
class EditorForm : public QWidget {
public:
    EditorForm(const beebium::Control& editor, EditorValues& values, QWidget* parent)
        : QWidget(parent), values_(values) {
        auto* layout = new QFormLayout(this);
        add(editor, layout);
    }

private:
    void add(const beebium::Control& control, QFormLayout* layout) {
        const std::string id = control.id();
        switch (control.control_case()) {
        case beebium::Control::kGroup:
            if (control.group().has_label() && !control.group().label().empty()) {
                auto* heading = new QLabel(text_of(control.group().label()), this);
                QFont bold = heading->font();
                bold.setBold(true);
                heading->setFont(bold);
                layout->addRow(heading);
            }
            for (const auto& child : control.group().controls()) {
                add(child, layout);
            }
            break;
        case beebium::Control::kLabel:
            layout->addRow(new QLabel(text_of(control.label().text()), this));
            break;
        case beebium::Control::kTextInput: {
            auto* edit = new QLineEdit(this);
            edit->setObjectName(text_of(id));
            edit->setText(QString::fromStdString(std::get<std::string>(values_[id])));
            edit->setPlaceholderText(text_of(control.text_input().placeholder()));
            edit->setToolTip(text_of(control.text_input().help()));
            connect(edit, &QLineEdit::textChanged, this,
                    [this, id](const QString& text) { values_[id] = text.toStdString(); });
            layout->addRow(text_of(control.text_input().label()), edit);
            break;
        }
        case beebium::Control::kEditableChoice: {
            auto* combo = new QComboBox(this);
            combo->setObjectName(text_of(id));
            combo->setEditable(true);
            for (const auto& option : control.editable_choice().options()) {
                combo->addItem(text_of(option));
            }
            combo->setEditText(QString::fromStdString(std::get<std::string>(values_[id])));
            connect(combo, &QComboBox::editTextChanged, this,
                    [this, id](const QString& text) { values_[id] = text.toStdString(); });
            layout->addRow(text_of(control.editable_choice().label()), combo);
            break;
        }
        case beebium::Control::kChoice: {
            auto* combo = new QComboBox(this);
            combo->setObjectName(text_of(id));
            for (const auto& option : control.choice().options()) {
                combo->addItem(text_of(option));
            }
            combo->setCurrentIndex(static_cast<int>(std::get<std::uint32_t>(values_[id])));
            connect(combo, &QComboBox::currentIndexChanged, this, [this, id](int index) {
                values_[id] = static_cast<std::uint32_t>(std::max(index, 0));
            });
            layout->addRow(text_of(control.choice().label()), combo);
            break;
        }
        case beebium::Control::kToggle: {
            auto* box = new QCheckBox(text_of(control.toggle().label()), this);
            box->setObjectName(text_of(id));
            box->setChecked(std::get<bool>(values_[id]));
            connect(box, &QCheckBox::toggled, this, [this, id](bool on) { values_[id] = on; });
            layout->addRow(box);
            break;
        }
        default: break;
        }
    }

    EditorValues& values_;
};

std::optional<EditorValues> run_editor_dialog(QWidget* parent, const QString& title,
                                              const beebium::Control& editor,
                                              const QString& commit_label) {
    EditorValues values = editor_values(editor);
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new EditorForm(editor, values, &dialog));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* commit = buttons->addButton(commit_label, QDialogButtonBox::AcceptRole);
    commit->setDefault(true);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;  // the server never hears of a cancelled edit
    }
    return values;
}

}  // namespace

QColor indicator_colour(beebium::Indicator::State state) {
    switch (state) {
    case beebium::Indicator::OK: return QColor(0x2e, 0x9e, 0x44);
    case beebium::Indicator::WARN: return QColor(0xd9, 0xa4, 0x00);
    case beebium::Indicator::ERROR: return QColor(0xd0, 0x3b, 0x3b);
    default: return QColor(0x8e, 0x8e, 0x93);
    }
}

ExtensionNode::ExtensionNode(ExtensionViewWidget& owner, beebium::Control::ControlCase kind,
                             QWidget* parent)
    : QWidget(parent), owner_(owner), kind_(kind) {}

ExtensionViewWidget::ExtensionViewWidget(Dispatch dispatch, Shares shares, QWidget* parent)
    : QWidget(parent), dispatch_(std::move(dispatch)), shares_(std::move(shares)) {
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
}

ExtensionViewWidget::~ExtensionViewWidget() = default;

void ExtensionViewWidget::set_view(const beebium::View& view) {
    view_ = view;
    previous_ = std::move(nodes_);
    nodes_.clear();
    ExtensionNode* root = node_for(view_.root(), this);
    if (root != root_) {
        if (root_ && layout_->indexOf(root_) >= 0) {
            layout_->removeWidget(root_);
        }
        layout_->addWidget(root);
        root_ = root;
    }
    root_->show();
    // What the new view no longer has goes.
    for (ExtensionNode* gone : std::as_const(previous_)) {
        gone->hide();
        gone->setParent(nullptr);
        gone->deleteLater();
    }
    previous_.clear();
}

void ExtensionViewWidget::set_stale(bool stale) {
    stale_ = stale;
    setEnabled(!stale);
}

QWidget* ExtensionViewWidget::control_widget(const QString& id) const {
    return nodes_.value(id, nullptr);
}

ExtensionNode* ExtensionViewWidget::node_for(const beebium::Control& control, QWidget* parent) {
    const QString id = QString::fromStdString(control.id());
    ExtensionNode* node = previous_.take(id);
    if (node && node->kind() != control.control_case()) {
        node->hide();
        node->setParent(nullptr);
        node->deleteLater();
        node = nullptr;
    }
    if (!node) {
        switch (control.control_case()) {
        case beebium::Control::kLabel: node = new LabelNode(*this, parent); break;
        case beebium::Control::kIndicator: node = new IndicatorNode(*this, parent); break;
        case beebium::Control::kToggle: node = new ToggleNode(*this, parent); break;
        case beebium::Control::kButton: node = new ButtonNode(*this, parent); break;
        case beebium::Control::kChoice: node = new ChoiceNode(*this, parent); break;
        case beebium::Control::kTextInput: node = new TextNode(*this, parent); break;
        case beebium::Control::kGroup: node = new GroupNode(*this, parent); break;
        case beebium::Control::kModalEditor: node = new ModalEditorNode(*this, parent); break;
        case beebium::Control::kEditableChoice:
            node = new EditableChoiceNode(*this, parent);
            break;
        case beebium::Control::kEditableList: node = new EditableListNode(*this, parent); break;
        case beebium::Control::kFileReference:
            node = new FileReferenceNode(*this, parent);
            break;
        default: node = new EmptyNode(*this, parent); break;
        }
        node->setObjectName(id);
    } else if (node->parentWidget() != parent) {
        node->setParent(parent);
    }
    node->id_ = control.id();
    node->setToolTip(QString::fromStdString(control.tooltip()));
    nodes_.insert(id, node);
    node->update(control);
    return node;
}

void ExtensionViewWidget::act(const beebium::DispatchRequest& request) {
    if (stale_) {
        return;
    }
    // An edit in progress lands before another control acts.
    flush_pending_edits(request.control_id());
    dispatch_(request);
}

void ExtensionViewWidget::commit_text(const std::string& control_id, const std::string& value) {
    if (stale_) {
        return;
    }
    dispatch_(text_action(view_, control_id, value));
}

void ExtensionViewWidget::flush_pending_edits(const std::string& except) {
    for (ExtensionNode* node : std::as_const(nodes_)) {
        if (node->id_ != except && node->pending_edit()) {
            node->commit_edit();
        }
    }
}

std::optional<EditorValues> ExtensionViewWidget::run_editor(const QString& title,
                                                            const beebium::Control& editor,
                                                            const QString& commit_label) {
    if (run_editor_) {
        return run_editor_(title, editor, commit_label);
    }
    return run_editor_dialog(this, title, editor, commit_label);
}

bool ExtensionViewWidget::confirm(const QString& title, const QString& warning) {
    if (confirm_) {
        return confirm_(title, warning);
    }
    QMessageBox box(QMessageBox::Warning, title, warning, QMessageBox::Cancel, this);
    QPushButton* go = box.addButton(title, QMessageBox::AcceptRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == go;
}

}  // namespace beebium::linux_client
