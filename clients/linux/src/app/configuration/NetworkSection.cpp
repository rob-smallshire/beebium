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

#include "app/configuration/NetworkSection.hpp"

#include "app/ParameterForm.hpp"
#include "app/configuration/AttachmentChooser.hpp"
#include "app/configuration/SectionWidgets.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace beebium::linux_client {

using nlohmann::ordered_json;

namespace {

const char* ECONET = "/econet";
const char* STATION = "/econet/station";
const char* TRANSPORT = "/econet/transport";

QLabel* subheading(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont medium = label->font();
    medium.setWeight(QFont::Medium);
    label->setFont(medium);
    return label;
}

QFrame* rule(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

std::vector<std::string> transports(const PresetSchema& schema) {
    std::vector<std::string> out;
    if (const auto* econet = schema.section("econet")) {
        for (const auto& name : econet->value("transports", ordered_json::array())) {
            if (name.is_string()) {
                out.push_back(name.get<std::string>());
            }
        }
    }
    return out;
}

}  // namespace

NetworkSection::NetworkSection(NewMachineDialog& dialog) : dialog_(dialog) {
    setObjectName(QStringLiteral("networkSection"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto* inner = new QWidget(scroll);
    auto* layout = new QVBoxLayout(inner);

    econet_ = new QCheckBox(tr("Econet"), inner);
    econet_->setObjectName(QStringLiteral("econetFitted"));
    QFont bold = econet_->font();
    bold.setBold(true);
    econet_->setFont(bold);
    layout->addWidget(econet_);
    econet_diagnostic_ = new DiagnosticLine(inner);
    layout->addWidget(econet_diagnostic_);

    body_ = new QWidget(inner);
    auto* body = new QVBoxLayout(body_);
    body->setContentsMargins(0, 0, 0, 0);

    // The station.
    body->addWidget(subheading(tr("Station"), body_));
    auto* mode_row = new QHBoxLayout;
    modes_ = new QButtonGroup(this);
    const std::pair<EconetStation::Mode, QString> modes[] = {
        {EconetStation::Mode::number, tr("Number")},
        {EconetStation::Mode::automatic, tr("Automatic")},
        {EconetStation::Mode::range, tr("Automatic in range")},
    };
    for (const auto& [mode, label] : modes) {
        auto* button = new QRadioButton(label, body_);
        modes_->addButton(button, static_cast<int>(mode));
        mode_row->addWidget(button);
    }
    mode_row->addStretch(1);
    body->addLayout(mode_row);
    mode_pages_ = new QStackedWidget(body_);
    {
        auto* page = new QWidget(mode_pages_);
        auto* row = new QHBoxLayout(page);
        row->setContentsMargins(0, 0, 0, 0);
        number_ = new QLineEdit(page);
        number_->setObjectName(QStringLiteral("econetStation"));
        number_->setAccessibleName(tr("Station"));
        number_->setMaximumWidth(number_->fontMetrics().horizontalAdvance(QStringLiteral("00000")) + 16);
        row->addWidget(number_);
        bounds_label_ = section_caption(page);
        bounds_label_->setWordWrap(false);
        row->addWidget(bounds_label_);
        row->addStretch(1);
        mode_pages_->addWidget(page);
    }
    {
        QLabel* chosen = section_caption(mode_pages_);
        chosen->setText(tr("A free station number is chosen on the network at launch."));
        mode_pages_->addWidget(chosen);
    }
    {
        auto* page = new QWidget(mode_pages_);
        auto* row = new QHBoxLayout(page);
        row->setContentsMargins(0, 0, 0, 0);
        auto* from = new QLabel(tr("from"), page);
        row->addWidget(from);
        low_ = new QLineEdit(page);
        low_->setObjectName(QStringLiteral("econetStationLow"));
        low_->setMaximumWidth(number_->maximumWidth());
        from->setBuddy(low_);
        row->addWidget(low_);
        auto* to = new QLabel(tr("to"), page);
        row->addWidget(to);
        high_ = new QLineEdit(page);
        high_->setObjectName(QStringLiteral("econetStationHigh"));
        high_->setMaximumWidth(number_->maximumWidth());
        to->setBuddy(high_);
        row->addWidget(high_);
        row->addStretch(1);
        mode_pages_->addWidget(page);
    }
    body->addWidget(mode_pages_);
    station_diagnostic_ = new DiagnosticLine(body_);
    body->addWidget(station_diagnostic_);
    body->addWidget(rule(body_));

    // The transport.
    body->addWidget(subheading(tr("Transport"), body_));
    auto* transport_row = new QHBoxLayout;
    auto* names = new QVBoxLayout;
    transport_name_ = new QLabel(body_);
    names->addWidget(transport_name_);
    transport_identifier_ = section_caption(body_);
    names->addWidget(transport_identifier_);
    transport_row->addLayout(names, 1);
    change_ = new QPushButton(tr("Change"), body_);
    change_->setObjectName(QStringLiteral("changeTransport"));
    change_->setFlat(true);
    transport_row->addWidget(change_);
    body->addLayout(transport_row);
    transport_description_ = section_caption(body_);
    body->addWidget(transport_description_);
    form_layout_ = new QVBoxLayout;
    form_layout_->setContentsMargins(8, 0, 0, 0);
    body->addLayout(form_layout_);
    transport_diagnostic_ = new DiagnosticLine(body_);
    body->addWidget(transport_diagnostic_);

    layout->addWidget(body_);
    layout->addStretch(1);
    scroll->setWidget(inner);

    connect(econet_, &QCheckBox::clicked, this, [this](bool fitted) {
        if (fitted) {
            // A station the schema allows, and its first transport.
            ordered_json econet{{"station", bounds_.automatic ? ordered_json("auto")
                                                              : ordered_json(bounds_.min)}};
            const auto& schema = dialog_.schema();
            const auto names = schema ? transports(*schema) : std::vector<std::string>{};
            econet["transport"] = names.empty() ? ordered_json()
                                                : ordered_json{{"name", names.front()},
                                                               {"parameters", ordered_json::object()}};
            dialog_.draft().set(ECONET, econet);
        } else {
            dialog_.draft().set(ECONET, ordered_json());
        }
        show_station();
        show_transport();
        Q_EMIT edited();
    });
    connect(modes_, &QButtonGroup::idClicked, this, [this](int id) {
        EconetStation edited = station();
        edited.mode = static_cast<EconetStation::Mode>(id);
        write_station(edited);
    });
    connect(number_, &QLineEdit::editingFinished, this, [this] {
        EconetStation edited = station();
        edited.number = EconetStation::clamp(number_->text().toStdString(), edited.number, bounds_);
        write_station(edited);
    });
    connect(low_, &QLineEdit::editingFinished, this, [this] {
        EconetStation edited = station();
        edited.low = EconetStation::clamp(low_->text().toStdString(), edited.low, bounds_);
        write_station(edited);
    });
    connect(high_, &QLineEdit::editingFinished, this, [this] {
        EconetStation edited = station();
        edited.high = EconetStation::clamp(high_->text().toStdString(), edited.high, bounds_);
        write_station(edited);
    });
    connect(change_, &QPushButton::clicked, this, [this] { choose_transport(); });
}

bool NetworkSection::on() const {
    const auto econet = dialog_.draft().get(ECONET);
    return econet && econet->is_object();
}

EconetStation NetworkSection::station() const {
    return EconetStation::from_json(dialog_.draft().get(STATION).value_or(ordered_json()), bounds_);
}

void NetworkSection::write_station(const EconetStation& edited) {
    if (!on()) {
        return;
    }
    const ordered_json value = edited.to_json();
    if (dialog_.draft().get(STATION) != value) {
        dialog_.draft().set(STATION, value);
        Q_EMIT this->edited();
    }
    show_station();
}

void NetworkSection::reload() {
    const auto& schema = dialog_.schema();
    bounds_ = schema && schema->station() ? *schema->station() : SchemaStation{};
    modes_->button(static_cast<int>(EconetStation::Mode::automatic))->setVisible(bounds_.automatic);
    modes_->button(static_cast<int>(EconetStation::Mode::range))->setVisible(bounds_.automatic);
    bounds_label_->setText(QStringLiteral("(%1-%2)").arg(bounds_.min).arg(bounds_.max));
    show_station();
    show_transport();
}

void NetworkSection::show_station() {
    const bool fitted = on();
    econet_->setChecked(fitted);
    body_->setEnabled(fitted);
    const EconetStation now = station();
    modes_->button(static_cast<int>(now.mode))->setChecked(true);
    mode_pages_->setCurrentIndex(static_cast<int>(now.mode));
    number_->setText(QString::number(now.number));
    low_->setText(QString::number(now.low));
    high_->setText(QString::number(now.high));
}

void NetworkSection::show_transport() {
    if (form_) {
        form_->hide();
        form_->deleteLater();
        form_ = nullptr;
    }
    const auto transport = dialog_.draft().get(TRANSPORT);
    const std::string name = transport && transport->is_object()
                                 ? transport->value("name", std::string())
                                 : std::string();
    const auto& schema = dialog_.schema();
    const auto extension = schema && !name.empty() ? schema->extension(name) : std::nullopt;
    if (name.empty()) {
        transport_name_->setText(tr("None"));
        transport_identifier_->clear();
    } else {
        transport_name_->setText(QString::fromStdString(
            extension && !extension->display_name.empty() ? extension->display_name : name));
        transport_identifier_->setText(QString::fromStdString(name));
    }
    transport_identifier_->setVisible(!name.empty());
    transport_description_->setText(extension ? QString::fromStdString(extension->description) : QString());
    transport_description_->setVisible(!transport_description_->text().isEmpty());
    if (on() && extension && !extension->parameters.empty()) {
        form_ = new ParameterForm(ParameterFormModel(*extension, "/econet/transport/parameters", *schema),
                                  dialog_.draft(), body_);
        form_->set_chooser([this](const FormBrowse& browse) { return dialog_.choose(browse); });
        form_->set_serial_ports(dialog_.serial_ports());
        connect(&dialog_, &NewMachineDialog::serial_ports_listed, form_,
                [this, form = form_] { form->set_serial_ports(dialog_.serial_ports()); });
        connect(form_, &ParameterForm::edited, this, &ConfigurationSection::edited);
        form_->set_diagnostics(diagnostics_);
        form_layout_->addWidget(form_);
    }
}

void NetworkSection::choose_transport() {
    const auto& schema = dialog_.schema();
    if (!schema || !on()) {
        return;
    }
    AttachmentChoice choice;
    choice.title = tr("Change Transport");
    choice.commit = tr("Change");
    ChooserEntry none;
    none.display_name = tr("None").toStdString();
    none.description = tr("Econet hardware is fitted but not connected to any network.").toStdString();
    choice.entries.push_back(none);
    for (const auto& name : transports(*schema)) {
        if (const auto extension = schema->extension(name)) {
            choice.entries.push_back(chooser_entry(*extension, *schema));
        }
    }
    const auto transport = dialog_.draft().get(TRANSPORT);
    choice.selected = transport && transport->is_object() ? transport->value("name", std::string())
                                                          : std::string();
    const auto chosen = dialog_.pick(choice);
    if (!chosen || *chosen == choice.selected) {
        return;
    }
    dialog_.draft().set(TRANSPORT, chosen->empty()
                                       ? ordered_json()
                                       : ordered_json{{"name", *chosen},
                                                      {"parameters", ordered_json::object()}});
    show_transport();
    Q_EMIT edited();
}

QRadioButton* NetworkSection::station_mode(EconetStation::Mode mode) const {
    return static_cast<QRadioButton*>(modes_->button(static_cast<int>(mode)));
}

void NetworkSection::set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) {
    diagnostics_ = diagnostics;
    std::vector<PresetDiagnostic> econet;
    std::vector<PresetDiagnostic> transport;
    const std::string parameters = std::string(TRANSPORT) + "/parameters";
    for (const auto& d : diagnostics) {
        if (d.path == ECONET) {
            econet.push_back(d);
        } else if (d.path.rfind(TRANSPORT, 0) == 0 && d.path.rfind(parameters, 0) != 0) {
            transport.push_back(d);
        }
    }
    econet_diagnostic_->show_for(econet, ECONET);
    station_diagnostic_->show_for(diagnostics, STATION);
    transport_diagnostic_->show_for(transport, TRANSPORT);
    if (form_) {
        form_->set_diagnostics(diagnostics);
    }
}

}  // namespace beebium::linux_client
