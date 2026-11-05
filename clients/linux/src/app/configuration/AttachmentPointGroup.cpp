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

#include "app/configuration/AttachmentPointGroup.hpp"

#include "app/NewMachineDialog.hpp"
#include "app/ParameterForm.hpp"
#include "app/configuration/AttachmentChooser.hpp"
#include "app/configuration/SectionWidgets.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

const int CHILD_INDENT = 14;
const int FORM_INDENT = 8;

QString text(const std::string& value) {
    return QString::fromStdString(value);
}

}  // namespace

AttachmentPointGroup::AttachmentPointGroup(NewMachineDialog& dialog, ExtensionsDraft& extensions,
                                           SchemaAttachmentPoint point,
                                           std::optional<ExtensionInstance> provider,
                                           QWidget* parent)
    : AttachmentPointGroup(dialog, extensions, std::move(point), std::move(provider), std::nullopt,
                           parent) {}

AttachmentPointGroup* AttachmentPointGroup::unattached(NewMachineDialog& dialog,
                                                       ExtensionsDraft& extensions, QWidget* parent) {
    SchemaAttachmentPoint none;
    none.display_name = tr("Unattached").toStdString();
    return new AttachmentPointGroup(dialog, extensions, std::move(none), std::nullopt,
                                    extensions.unattached(), parent);
}

AttachmentPointGroup::AttachmentPointGroup(NewMachineDialog& dialog, ExtensionsDraft& extensions,
                                           SchemaAttachmentPoint point,
                                           std::optional<ExtensionInstance> provider,
                                           std::optional<std::vector<ExtensionInstance>> fixed,
                                           QWidget* parent)
    : QWidget(parent), dialog_(dialog), extensions_(extensions), point_(std::move(point)),
      provider_(std::move(provider)) {
    setObjectName(QStringLiteral("point:%1").arg(text(point_.id)));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    const auto attached = fixed ? *fixed : extensions_.attached(point_, this->provider());
    const bool single = !fixed && point_.max_occupancy && *point_.max_occupancy == 1;

    auto* header = new QHBoxLayout;
    header->addWidget(section_heading(text(point_.display_name.empty() ? point_.id : point_.display_name), this));
    occupancy_ = section_caption(this);
    occupancy_->setWordWrap(false);
    occupancy_->setVisible(!fixed);
    occupancy_->setText(point_.max_occupancy
                            ? tr("%1 of %2").arg(attached.size()).arg(*point_.max_occupancy)
                            : QString::number(attached.size()));
    header->addWidget(occupancy_);
    header->addStretch(1);
    if (!fixed && !extensions_.addable(point_, this->provider()).empty()) {
        add_ = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Add"), this);
        add_->setFlat(true);
        connect(add_, &QPushButton::clicked, this, [this] { add(); });
        header->addWidget(add_);
    }
    layout->addLayout(header);
    if (!point_.description.empty()) {
        QLabel* description = section_caption(this);
        description->setText(text(point_.description));
        layout->addWidget(description);
    }

    auto* children = new QVBoxLayout;
    children->setContentsMargins(CHILD_INDENT, 0, 0, 0);
    layout->addLayout(children);
    if (attached.empty()) {
        QLabel* nothing = section_caption(this);
        nothing->setText(tr("Nothing attached."));
        children->addWidget(nothing);
    }
    for (const auto& instance : attached) {
        Card card;
        card.instance = instance;
        auto* frame = new QFrame(this);
        frame->setObjectName(QStringLiteral("instance:%1").arg(text(instance.id)));
        frame->setFrameShape(QFrame::StyledPanel);
        card.widget = frame;
        auto* rows = new QVBoxLayout(frame);
        rows->setContentsMargins(8, 8, 8, 8);
        auto* top = new QHBoxLayout;
        auto* names = new QVBoxLayout;
        const QString display = text(extensions_.display_name(instance.name));
        QLabel* title = new QLabel(display, frame);
        QFont medium = title->font();
        medium.setWeight(QFont::Medium);
        title->setFont(medium);
        names->addWidget(title);
        QLabel* identifier = section_caption(frame);
        identifier->setText(text(instance.name));
        names->addWidget(identifier);
        top->addLayout(names, 1);
        if (single && !extensions_.alternatives(point_, instance).empty()) {
            card.change = new QPushButton(tr("Change"), frame);
            card.change->setFlat(true);
            connect(card.change, &QPushButton::clicked, this, [this, instance] { change(instance); });
            top->addWidget(card.change);
        }
        card.remove = new QToolButton(frame);
        card.remove->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
        if (card.remove->icon().isNull()) {
            card.remove->setText(tr("Remove"));
        }
        card.remove->setAutoRaise(true);
        card.remove->setToolTip(tr("Remove %1").arg(display));
        card.remove->setAccessibleName(tr("Remove %1").arg(display));
        connect(card.remove, &QToolButton::clicked, this, [this, instance] {
            extensions_.remove(instance);
            Q_EMIT restructured();
        });
        top->addWidget(card.remove);
        rows->addLayout(top);

        const auto& schema = dialog_.schema();
        const auto extension = schema ? schema->extension(instance.name) : std::nullopt;
        if (extension && !extension->description.empty()) {
            QLabel* description = section_caption(frame);
            description->setText(text(extension->description));
            rows->addWidget(description);
        }
        if (extension && !extension->parameters.empty()) {
            card.form = new ParameterForm(
                ParameterFormModel(*extension, "/extensions/" + std::to_string(instance.index) + "/config",
                                   *schema),
                dialog_.draft(), frame);
            card.form->set_chooser([this](const FormBrowse& browse) { return dialog_.choose(browse); });
            card.form->set_serial_ports(dialog_.serial_ports());
            connect(&dialog_, &NewMachineDialog::serial_ports_listed, card.form,
                    [this, form = card.form] { form->set_serial_ports(dialog_.serial_ports()); });
            connect(card.form, &ParameterForm::edited, this, &AttachmentPointGroup::edited);
            auto* indented = new QVBoxLayout;
            indented->setContentsMargins(FORM_INDENT, 0, 0, 0);
            indented->addWidget(card.form);
            rows->addLayout(indented);
        }
        card.diagnostic = new DiagnosticLine(frame);
        rows->addWidget(card.diagnostic);
        for (const auto& provided : extensions_.provided_points(instance)) {
            auto* group = new AttachmentPointGroup(dialog_, extensions_, provided, instance, frame);
            connect(group, &AttachmentPointGroup::restructured, this, &AttachmentPointGroup::restructured);
            connect(group, &AttachmentPointGroup::edited, this, &AttachmentPointGroup::edited);
            card.nested.push_back(group);
            rows->addWidget(group);
        }
        children->addWidget(frame);
        cards_.push_back(std::move(card));
    }
}

void AttachmentPointGroup::add() {
    const auto& schema = dialog_.schema();
    if (!schema) {
        return;
    }
    AttachmentChoice choice;
    choice.title = tr("Add to %1").arg(text(point_.display_name.empty() ? point_.id : point_.display_name));
    choice.commit = tr("Add");
    for (const auto& extension : extensions_.addable(point_, provider())) {
        choice.entries.push_back(chooser_entry(extension, *schema));
    }
    const auto chosen = dialog_.pick(choice);
    if (chosen && !chosen->empty()) {
        extensions_.add(*chosen, point_, provider());
        Q_EMIT restructured();
    }
}

void AttachmentPointGroup::change(const ExtensionInstance& instance) {
    const auto& schema = dialog_.schema();
    if (!schema) {
        return;
    }
    AttachmentChoice choice;
    choice.title = tr("Change %1").arg(text(point_.display_name.empty() ? point_.id : point_.display_name));
    choice.commit = tr("Change");
    for (const auto& extension : extensions_.alternatives(point_, instance)) {
        choice.entries.push_back(chooser_entry(extension, *schema));
    }
    const auto chosen = dialog_.pick(choice);
    if (chosen && !chosen->empty()) {
        extensions_.change(instance, *chosen);
        Q_EMIT restructured();
    }
}

std::vector<ParameterForm*> AttachmentPointGroup::forms() const {
    std::vector<ParameterForm*> out;
    for (const Card& card : cards_) {
        if (card.form) {
            out.push_back(card.form);
        }
        for (const auto* group : card.nested) {
            const auto inner = group->forms();
            out.insert(out.end(), inner.begin(), inner.end());
        }
    }
    return out;
}

void AttachmentPointGroup::set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) {
    for (Card& card : cards_) {
        // The instance's own (its attachment); its parameters' are the form's.
        const std::string pointer = "/extensions/" + std::to_string(card.instance.index);
        std::vector<PresetDiagnostic> own;
        for (const auto& d : diagnostics) {
            if (d.path == pointer) {
                own.push_back(d);
            }
        }
        card.diagnostic->show_for(own, pointer);
        if (card.form) {
            card.form->set_diagnostics(diagnostics);
        }
        for (auto* group : card.nested) {
            group->set_diagnostics(diagnostics);
        }
    }
}

QWidget* AttachmentPointGroup::card(const std::string& id) const {
    for (const Card& card : cards_) {
        if (card.instance.id == id) {
            return card.widget;
        }
    }
    return nullptr;
}

QPushButton* AttachmentPointGroup::change_button(const std::string& id) const {
    for (const Card& card : cards_) {
        if (card.instance.id == id) {
            return card.change;
        }
    }
    return nullptr;
}

QToolButton* AttachmentPointGroup::remove_button(const std::string& id) const {
    for (const Card& card : cards_) {
        if (card.instance.id == id) {
            return card.remove;
        }
    }
    return nullptr;
}

AttachmentPointGroup* AttachmentPointGroup::nested(const std::string& id, const std::string& point) const {
    for (const Card& card : cards_) {
        if (card.instance.id == id) {
            for (auto* group : card.nested) {
                if (group->point().id == point) {
                    return group;
                }
            }
        }
    }
    return nullptr;
}

}  // namespace beebium::linux_client
