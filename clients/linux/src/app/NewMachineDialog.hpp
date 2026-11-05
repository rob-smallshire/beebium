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

#ifndef BEEBIUM_LINUX_APP_NEW_MACHINE_DIALOG_HPP
#define BEEBIUM_LINUX_APP_NEW_MACHINE_DIALOG_HPP

#include "app/PresetResolver.hpp"
#include "app/SidebarMode.hpp"
#include "preset/MachineDraft.hpp"
#include "preset/ParameterFormModel.hpp"
#include "preset/PresetResolution.hpp"
#include "preset/PresetSchema.hpp"
#include "servers/CoreCli.hpp"

#include <QDialog>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace beebium::linux_client {

class ClientSettings;
class NewMachineDialog;

// The configuration editor's own sections (the storage, memory,
// peripherals, keyboard, processor and network editors).
std::vector<std::function<class ConfigurationSection*(NewMachineDialog&)>>
configuration_section_makers();

// One extension the attachment chooser offers: catalogue text throughout.
struct ChooserEntry {
    std::string name;  // the identifier; empty for "None"
    std::string display_name;
    std::string description;
    std::vector<std::string> needs;     // its parameters' labels
    std::vector<std::string> provides;  // the points it adds, by display name
};

// The attachment chooser's question: pick one of the entries.
struct AttachmentChoice {
    QString title;
    QString commit;
    std::vector<ChooserEntry> entries;
    std::string selected;
};

// What the New Machine dialog needs of this host's servers, as functions so
// tests can stand in for them.
struct NewMachineHost {
    using Presets = std::function<void(std::vector<PresetListing> presets, QString error)>;
    using Schema = std::function<void(std::optional<PresetSchema> schema)>;
    using Imported = std::function<void(bool ok, QString error)>;
    using RomDescribed = std::function<void(std::optional<RomDescription> rom)>;
    using SerialPorts = std::function<void(QStringList paths)>;

    // list-presets: every preset this host can launch.
    std::function<void(Presets done)> list_presets;
    // describe-preset-schema for a model.
    std::function<void(const QString& model, Schema done)> describe_schema;
    // The model's executable, which resolves its presets.
    std::function<QString(const QString& model)> executable;
    // resolve-preset's runner; CoreCli when empty.
    PresetResolver::Runner resolve_runner;
    // import-preset of a preset file, for Save as new preset.
    std::function<void(const QString& model, const QString& filepath, Imported done)> import_preset;
    // describe-rom of a ROM library name or image path.
    std::function<void(const QString& model, const QString& rom, RomDescribed done)> describe_rom;
    // list-serial-ports: the host's, for a parameter that offers them.
    std::function<void(SerialPorts done)> list_serial_ports;
    // Start a machine from a preset file, as a preset card does: the listing
    // names the preset it was made from, for the machine's name and ordinal.
    std::function<void(const PresetListing& preset, const QString& filepath)> launch;
};

// One section of the configuration editor: an editor over part of the
// draft, in the sidebar's order.
class ConfigurationSection : public QWidget {
    Q_OBJECT

public:
    using QWidget::QWidget;
    virtual SidebarMode mode() const = 0;
    // The draft's parts this section edits, for its changed marker.
    virtual std::vector<std::string> pointers() const = 0;
    // The draft or the schema is new: show them afresh.
    virtual void reload() = 0;
    virtual void set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) {
        (void)diagnostics;
    }

Q_SIGNALS:
    void edited();
};

// File > New Machine... (client specification 5.1, 5.2), as the macOS
// client's: pick a preset, its resolved document becomes the draft, edit it
// section by section in the sidebar's order with the server judging each
// edit, and Create launches it, optionally saving it as a new preset
// first. Errors block Create; warnings do not.
class NewMachineDialog : public QDialog {
    Q_OBJECT

public:
    static constexpr const char* LAST_PRESET_KEY = "newMachine/lastPreset";

    NewMachineDialog(NewMachineHost host, ClientSettings* settings, QWidget* parent = nullptr);
    ~NewMachineDialog() override;

    // Read the presets afresh and load the last one chosen.
    void reload();

    MachineDraft& draft() { return draft_; }
    const std::optional<PresetSchema>& schema() const { return schema_; }
    const std::optional<PresetListing>& preset() const { return preset_; }
    const std::vector<PresetDiagnostic>& diagnostics() const { return diagnostics_; }
    QPushButton* create_button() const { return create_; }
    QComboBox* preset_picker() const { return picker_; }
    QLineEdit* description_field() const { return description_; }
    QCheckBox* save_as_box() const { return save_as_; }
    QLineEdit* save_as_name() const { return save_as_name_; }
    QListWidget* diagnostics_list() const { return diagnostics_list_; }
    QListWidget* section_list() const { return sections_list_; }
    QString launch_error() const;

    // The draft changed through the dialog: ask the server again.
    void draft_edited();

    // A sideways image's description (a ROM library name or file URI),
    // asked of the server once per image: null until it has answered or
    // when it could not, and rom_described() follows each answer.
    const RomDescription* rom_description(const std::string& image_uri);

    // The sections are built for each schema by these makers, in turn:
    // the built-in ones (configuration_section_makers) and any added.
    using SectionMaker = std::function<ConfigurationSection*(NewMachineDialog& dialog)>;
    static void add_section_maker(SectionMaker maker);

    // A path for a Browse: the platform's choosers by default; tests answer.
    using Chooser = std::function<std::optional<QString>(const FormBrowse& browse)>;
    void set_chooser(Chooser chooser) { chooser_ = std::move(chooser); }
    std::optional<QString> choose(const FormBrowse& browse);
    const Chooser& chooser() const { return chooser_; }

    // An extension from the attachment chooser: the platform's dialog by
    // default; tests answer. Nothing when the user cancels.
    using Picker = std::function<std::optional<std::string>(const AttachmentChoice& choice)>;
    void set_picker(Picker picker) { attachment_picker_ = std::move(picker); }
    std::optional<std::string> pick(const AttachmentChoice& choice);

    // The host's serial ports, asked once; serial_ports_listed() follows.
    const QStringList& serial_ports();

    void accept() override;
    void reject() override;

Q_SIGNALS:
    void rom_described();
    void serial_ports_listed();

private:
    void choose_preset(int index);
    void on_resolved(const PresetResolution& resolution);
    void present_diagnostics();
    void rebuild_sections();
    void update_markers();
    void create();
    void launch_draft();
    void show_launch_error(const QString& text);

    NewMachineHost host_;
    Chooser chooser_;
    Picker attachment_picker_;
    QStringList serial_ports_;
    bool serial_ports_asked_ = false;
    ClientSettings* settings_;
    std::unique_ptr<PresetResolver> resolver_;
    std::vector<PresetListing> presets_;
    std::optional<PresetListing> preset_;
    std::optional<PresetSchema> schema_;
    MachineDraft draft_;
    bool loading_ = false;  // the chosen preset's own resolution is awaited
    bool settled_ = false;  // the last resolution is the draft's present one
    std::vector<PresetDiagnostic> diagnostics_;
    std::map<QString, PresetSchema> schemas_;
    std::map<std::string, std::optional<RomDescription>> roms_;  // present once asked

    QLabel* banner_;
    QComboBox* picker_;
    QLabel* preset_description_;
    QLineEdit* description_;
    QListWidget* sections_list_;
    QStackedWidget* sections_stack_;
    std::vector<ConfigurationSection*> sections_;
    QCheckBox* save_as_;
    QLineEdit* save_as_name_;
    QListWidget* diagnostics_list_;
    QLabel* launch_error_;
    QPushButton* create_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_NEW_MACHINE_DIALOG_HPP
