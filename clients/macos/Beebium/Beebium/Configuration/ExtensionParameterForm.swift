// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

import SwiftUI
import UniformTypeIdentifiers

/// A generic editor over one extension instance's config, driven by its
/// manifest (describe-extension). A control per parameter by type, with the
/// label/description, the default shown as a placeholder, required marked, and
/// each edit written straight through to the instance's config.
///
/// It does not reuse the server-driven ExtensionViewRenderer controls: those are
/// bound to the runtime Beebium_Control proto tree and a gRPC dispatch model
/// (the server owns the value and the client posts events), whereas this form
/// binds directly to the draft the client owns. It reuses the generic
/// primitives instead (plain fields, a file picker).
struct ExtensionParameterForm: View {
    let manifest: ExtensionManifest
    /// The instance's current config.
    let config: [String: JSONValue]
    /// Diagnostics for this instance (paths under /extensions/<n>/config/...).
    var diagnostics: [PresetManager.PresetDiagnostic] = []
    /// Bundled disc-image masters by kind, for a filepath parameter that draws
    /// from a catalogue (its `bundled` kind).
    var bundledImages: [String: [BundledImage]] = [:]
    /// Host serial-port paths, offered as suggestions for a choices_from
    /// "serial-ports" parameter.
    var serialPortSuggestions: [String] = []
    /// Set (or, with nil, clear) one config key, written through to the draft.
    let setValue: (String, JSONValue?) -> Void

    var body: some View {
        if manifest.parameters.isEmpty {
            Text("No settings.")
                .font(.caption)
                .foregroundColor(.secondary)
        } else {
            VStack(alignment: .leading, spacing: 10) {
                ForEach(renderItems) { item in
                    switch item {
                    case .parameter(let parameter):
                        parameterRow(parameter)
                    case .group(let group):
                        GroupControl(group: group, members: members(of: group),
                                     config: config, diagnostics: diagnostics,
                                     setValue: setValue, control: { control($0) })
                    }
                }
            }
        }
    }

    /// What to render, in declared order: each ungrouped parameter as its own
    /// row, and each group once, at the position of its first member. A group's
    /// other members are folded into the group control, not shown as rows.
    private var renderItems: [RenderItem] {
        var items: [RenderItem] = []
        var placed: Set<String> = []
        for parameter in manifest.parameters {
            if let group = manifest.group(forParameter: parameter.key) {
                if placed.insert(group.key).inserted {
                    items.append(.group(group))
                }
            } else {
                items.append(.parameter(parameter))
            }
        }
        return items
    }

    /// The member parameters of a group, in the group's declared option order.
    private func members(of group: ExtensionGroup) -> [ExtensionParameter] {
        group.parameters.compactMap { key in manifest.parameters.first { $0.key == key } }
    }

    private enum RenderItem: Identifiable {
        case parameter(ExtensionParameter)
        case group(ExtensionGroup)
        var id: String {
            switch self {
            case .parameter(let p): return "param:\(p.key)"
            case .group(let g): return "group:\(g.key)"
            }
        }
    }

    @ViewBuilder
    private func parameterRow(_ parameter: ExtensionParameter) -> some View {
        if parameter.isStructuredList {
            // The structured list brings its own titled box, help affordance and
            // per-row/field diagnostics (it reuses the sidebar's EditableList
            // stack), so it stands alone without the scalar label wrapper. A
            // diagnostic on the parameter itself (not an item) still shows below.
            VStack(alignment: .leading, spacing: 4) {
                control(parameter)
                DiagnosticLabels(diagnostics: diagnostics.forConfigKey(parameter.key))
            }
        } else {
            scalarParameterRow(parameter)
        }
    }

    @ViewBuilder
    private func scalarParameterRow(_ parameter: ExtensionParameter) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 4) {
                Text(parameter.displayLabel)
                    .font(.subheadline)
                if parameter.required {
                    Text("required").font(.caption2).foregroundColor(.secondary)
                }
            }
            control(parameter)
                .disabled(isExcluded(parameter))
            if isExcluded(parameter) {
                Text("Not used with \(excludedBy(parameter) ?? "another setting").")
                    .font(.caption2).foregroundColor(.secondary)
            }
            if !parameter.description.isEmpty {
                Text(parameter.description)
                    .font(.caption2)
                    .foregroundColor(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            DiagnosticLabels(diagnostics: diagnostics.forConfigKey(parameter.key))
        }
    }

    // Returns AnyView (type-erased): a @ViewBuilder returning this many distinct
    // concrete View types crashes the Release whole-module optimiser with no
    // diagnostic (see ExtensionViewRenderer). Type-erasure avoids it.
    private func control(_ parameter: ExtensionParameter) -> AnyView {
        if !parameter.choices.isEmpty {
            return AnyView(choicePicker(parameter))
        }
        if parameter.isStructuredList {
            return AnyView(StructuredListParameterView(
                parameter: parameter,
                items: arrayItems(parameter.key),
                diagnostics: diagnostics,
                setValue: setValue))
        }
        if parameter.isList {
            return AnyView(StringListEditor(
                values: listValue(parameter.key),
                placeholder: parameter.defaultValue,
                setValues: { setValue(parameter.key, $0.isEmpty ? nil : .array($0.map(JSONValue.string))) }))
        }
        switch parameter.type {
        case "boolean":
            return AnyView(Toggle("", isOn: Binding(
                get: { boolValue(parameter.key) },
                set: { setValue(parameter.key, .bool($0)) }))
                .labelsHidden())
        case "integer":
            return AnyView(integerField(parameter))
        case "filepath":
            return AnyView(filepathControl(parameter))
        default: // "string"
            if parameter.choicesFrom == "serial-ports" {
                return AnyView(serialPortCombo(parameter))
            }
            return AnyView(CommittingField(
                value: stringValue(parameter.key),
                format: { $0 },
                parse: { $0 },
                placeholder: parameter.defaultValue,
                width: 260,
                onCommit: { setValue(parameter.key, $0.isEmpty ? .string("") : .string($0)) }))
        }
    }

    /// A free-text field with a menu of host serial ports to fill it in.
    private func serialPortCombo(_ parameter: ExtensionParameter) -> some View {
        HStack(spacing: 6) {
            CommittingField(
                value: stringValue(parameter.key),
                format: { $0 },
                parse: { $0 },
                placeholder: parameter.defaultValue,
                width: 220,
                onCommit: { setValue(parameter.key, $0.isEmpty ? .string("") : .string($0)) })
            if !serialPortSuggestions.isEmpty {
                Menu {
                    ForEach(serialPortSuggestions, id: \.self) { path in
                        Button(path) { setValue(parameter.key, .string(path)) }
                    }
                } label: {
                    Image(systemName: "chevron.down")
                }
                .menuStyle(.borderlessButton)
                .fixedSize()
                .help("Choose a detected serial port")
            }
        }
    }

    /// Whether `parameter` is disabled because a parameter it excludes has a
    /// value (the mutually-exclusive pairs, e.g. time vs offset).
    private func isExcluded(_ parameter: ExtensionParameter) -> Bool {
        excludedBy(parameter) != nil
    }

    private func excludedBy(_ parameter: ExtensionParameter) -> String? {
        parameter.excludes.first { hasValue($0) }
    }

    /// Whether a config key carries an explicitly-given, non-empty value.
    private func hasValue(_ key: String) -> Bool {
        Self.valueIsPresent(config[key])
    }

    // MARK: - Group selection (pure, testable)

    /// Whether a JSON value counts as explicitly given (the rule the exclusion
    /// and group-selection logic share).
    static func valueIsPresent(_ value: JSONValue?) -> Bool {
        switch value {
        case .string(let s): return !s.isEmpty
        case .array(let a):  return !a.isEmpty
        case .bool(let b):   return b
        case .int, .double:  return true
        case .object(let o): return !o.isEmpty
        case .null, .none:   return false
        }
    }

    /// The member key of `group` that currently has a value in `config`, or nil
    /// when none does (the "none" / host-time state). Members are mutually
    /// exclusive, so at most one has a value.
    static func selectedMemberKey(group: ExtensionGroup, config: [String: JSONValue]) -> String? {
        group.parameters.first { valueIsPresent(config[$0]) }
    }

    /// The option selected when the group control first appears: the member with
    /// a value, else the "none" option when the group has one, else the first
    /// member (a group with no "none" state always shows one option chosen).
    static func initialSelection(group: ExtensionGroup, config: [String: JSONValue]) -> String {
        if let key = selectedMemberKey(group: group, config: config) { return key }
        if group.none != nil { return "" }
        return group.parameters.first ?? ""
    }

    /// The members to clear from the draft when `picked` becomes the selection:
    /// every other member that currently has a value. Picking "none" (picked
    /// == "") clears them all.
    static func keysToClear(group: ExtensionGroup, picking picked: String,
                            config: [String: JSONValue]) -> [String] {
        group.parameters.filter { $0 != picked && valueIsPresent(config[$0]) }
    }

    private func choicePicker(_ parameter: ExtensionParameter) -> some View {
        let current = stringValue(parameter.key).isEmpty ? parameter.defaultValue : stringValue(parameter.key)
        let selectedDescription = parameter.choices.first { $0.value == current }?.description ?? ""
        return VStack(alignment: .leading, spacing: 2) {
            Picker("", selection: Binding(
                get: { current },
                set: { setValue(parameter.key, .string($0)) }
            )) {
                ForEach(parameter.choices) { choice in
                    Text(choice.label).tag(choice.value)
                }
            }
            .labelsHidden()
            .frame(maxWidth: 260)
            // The selected choice's own explanation (e.g. a register layout's year
            // range and Y2K note), as the storage FDC picker shows its device.
            if !selectedDescription.isEmpty {
                Text(selectedDescription)
                    .font(.caption2).foregroundColor(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    private func integerField(_ parameter: ExtensionParameter) -> some View {
        HStack(spacing: 6) {
            CommittingField(
                value: stringValue(parameter.key),
                format: { $0 },
                parse: { Self.parseIntegerEntry($0, parameter: parameter) },
                placeholder: parameter.defaultValue,
                width: 120,
                onCommit: { setValue(parameter.key, $0.isEmpty ? nil : .int(Int($0) ?? 0)) })
            if let min = parameter.minimum, let max = parameter.maximum {
                Text("(\(min)-\(max))")
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
        }
    }

    /// Parse an integer field entry to its canonical string, clamped to the
    /// parameter's bounds: "" for a blank (which clears the key, as before), nil
    /// for a non-numeric entry (a marked revert), else the clamped number.
    static func parseIntegerEntry(_ text: String, parameter: ExtensionParameter) -> String? {
        let trimmed = text.trimmingCharacters(in: .whitespaces)
        if trimmed.isEmpty { return "" }
        guard var value = Int(trimmed) else { return nil }
        if let lower = parameter.minimum { value = Swift.max(value, lower) }
        if let upper = parameter.maximum { value = Swift.min(value, upper) }
        return String(value)
    }

    private func filepathControl(_ parameter: ExtensionParameter) -> some View {
        let value = stringValue(parameter.key)
        let masters = parameter.bundled.isEmpty ? [] : (bundledImages[parameter.bundled] ?? [])
            .filter { $0.availableTo(extensionName: manifest.name) }
        // A parameter with a bundled default (a coprocessor's client ROM) comes
        // with a working choice already made. An empty value means "use that
        // default", which resolve-preset fills in -- so show the default by name
        // rather than "No file", and make picking something else the by-exception
        // path.
        let defaultLabel = Self.defaultImageLabel(forParameter: parameter, masters: masters)
        let usingDefault = value.isEmpty && defaultLabel != nil
        // A bare name is a bundled master (per-machine working copy); a path is a file.
        let isBare = !value.isEmpty && !value.contains("/")
        let display: String
        if usingDefault {
            display = "Default: \(defaultLabel ?? "")"
        } else if value.isEmpty {
            display = "No file"
        } else if isBare {
            display = masters.first(where: { $0.image == value })?.displayName ?? value
        } else {
            display = URL(fileURLWithPath: value).lastPathComponent
        }
        return HStack(spacing: 8) {
            Text(display)
                .font(.subheadline)
                .foregroundColor(value.isEmpty && !usingDefault ? .secondary : .primary)
                .lineLimit(1).truncationMode(.middle)
                .help(value)
                .frame(maxWidth: .infinity, alignment: .leading)
            Menu {
                // The way back to the default, when something else is chosen.
                if let defaultLabel = defaultLabel, !usingDefault {
                    Button("Use default (\(defaultLabel))") { setValue(parameter.key, .string("")) }
                    Divider()
                }
                if !masters.isEmpty {
                    Section(Self.bundledSectionTitle(parameter)) {
                        ForEach(masters, id: \.image) { master in
                            Button(master.displayName) { setValue(parameter.key, .string(master.image)) }
                        }
                    }
                }
                Button("Choose File…") { browse(parameter.key) }
                // With a default, clearing IS "use default"; offer Clear only when
                // there is no default to fall back to.
                if !value.isEmpty && defaultLabel == nil {
                    Button("Clear") { setValue(parameter.key, .string("")) }
                }
            } label: {
                Text(usingDefault ? "Use a different \(Self.bundledNoun(parameter))…"
                                  : (value.isEmpty ? "Choose…" : "Change…"))
            }
            .menuStyle(.borderlessButton)
            .fixedSize()
        }
        .onDrop(of: [.fileURL], isTargeted: nil) { providers in
            loadDroppedFile(providers) { url in setValue(parameter.key, .string(url.path)) }
        }
    }

    // MARK: - Bundled default (ROM) helpers

    /// The friendly name of a parameter's bundled default, or nil when it has
    /// none. The default value names a bundled image (a coprocessor's client
    /// ROM); resolve it to that image's display name, falling back to the raw
    /// default when it is not among the offered masters.
    static func defaultImageLabel(forParameter parameter: ExtensionParameter,
                                  masters: [BundledImage]) -> String? {
        guard !parameter.defaultValue.isEmpty else { return nil }
        return masters.first(where: { $0.image == parameter.defaultValue })?.displayName
            ?? parameter.defaultValue
    }

    /// The noun for a bundled kind, for menu wording ("ROM", "disc", "image").
    static func bundledNoun(_ parameter: ExtensionParameter) -> String {
        switch parameter.bundled {
        case "rom": return "ROM"
        case "hard-disc": return "disc"
        default: return "image"
        }
    }

    /// The bundled-section heading in the chooser menu.
    static func bundledSectionTitle(_ parameter: ExtensionParameter) -> String {
        "Bundled \(bundledNoun(parameter))s"
    }

    // MARK: - Value helpers

    private func stringValue(_ key: String) -> String {
        if case .string(let s)? = config[key] { return s }
        if case .int(let i)? = config[key] { return String(i) }
        return ""
    }

    private func boolValue(_ key: String) -> Bool {
        if case .bool(let b)? = config[key] { return b }
        return false
    }

    private func listValue(_ key: String) -> [String] {
        guard case .array(let items)? = config[key] else { return [] }
        return items.compactMap { if case .string(let s) = $0 { return s } else { return nil } }
    }

    /// The raw items of a list parameter (objects or legacy strings), for a
    /// structured list.
    private func arrayItems(_ key: String) -> [JSONValue] {
        if case .array(let items)? = config[key] { return items }
        return []
    }

    private func browse(_ key: String) {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        if panel.runModal() == .OK, let url = panel.url {
            setValue(key, .string(url.path))
        }
    }

    private func loadDroppedFile(_ providers: [NSItemProvider], _ accept: @escaping (URL) -> Void) -> Bool {
        guard let provider = providers.first else { return false }
        provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
            guard let data = item as? Data, let url = URL(dataRepresentation: data, relativeTo: nil) else { return }
            Task { @MainActor in accept(url) }
        }
        return true
    }
}

/// One group of mutually-exclusive parameters, shown as a single choice: the
/// "none" option (when the group has one) then one option per member, in the
/// group's declared order. Picking an option reveals that member's field and
/// clears the others in the draft; picking "none" clears them all and writes
/// nothing (the host-time "exact record" state).
///
/// The selection is kept locally as well as derived from the draft, so a member
/// just picked but not yet given a value keeps its field visible instead of
/// snapping back to "none". An external change to the draft (a preset reload
/// that sets a member) still moves the selection to follow it.
private struct GroupControl: View {
    let group: ExtensionGroup
    let members: [ExtensionParameter]
    let config: [String: JSONValue]
    let diagnostics: [PresetManager.PresetDiagnostic]
    let setValue: (String, JSONValue?) -> Void
    let control: (ExtensionParameter) -> AnyView

    @State private var selection: String

    init(group: ExtensionGroup, members: [ExtensionParameter], config: [String: JSONValue],
         diagnostics: [PresetManager.PresetDiagnostic],
         setValue: @escaping (String, JSONValue?) -> Void,
         control: @escaping (ExtensionParameter) -> AnyView) {
        self.group = group
        self.members = members
        self.config = config
        self.diagnostics = diagnostics
        self.setValue = setValue
        self.control = control
        _selection = State(initialValue: ExtensionParameterForm.initialSelection(group: group, config: config))
    }

    /// The selection the draft alone implies: the member with a value, else the
    /// "none" option (or the first member when the group has no "none").
    private var derivedSelection: String {
        ExtensionParameterForm.selectedMemberKey(group: group, config: config)
            ?? (group.none != nil ? "" : (group.parameters.first ?? ""))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            if !group.label.isEmpty {
                Text(group.label).font(.subheadline)
            }
            Picker("", selection: Binding(get: { selection }, set: { pick($0) })) {
                if let none = group.none {
                    Text(none.label.isEmpty ? "None" : none.label).tag("")
                }
                ForEach(members) { member in
                    Text(member.displayLabel).tag(member.key)
                }
            }
            .labelsHidden()
            .frame(maxWidth: 260)

            if !group.description.isEmpty {
                caption(group.description)
            }

            if selection.isEmpty {
                if let none = group.none, !none.description.isEmpty {
                    caption(none.description)
                }
            } else if let member = members.first(where: { $0.key == selection }) {
                control(member)
                if !member.description.isEmpty {
                    caption(member.description)
                }
                DiagnosticLabels(diagnostics: diagnostics.forConfigKey(member.key))
            }
        }
        .onChange(of: derivedSelection) { newValue in
            // Follow the draft when a member is set elsewhere (e.g. a preset
            // reload); keep a transient pick of an as-yet-empty member.
            if !newValue.isEmpty { selection = newValue }
        }
    }

    private func pick(_ key: String) {
        selection = key
        for member in ExtensionParameterForm.keysToClear(group: group, picking: key, config: config) {
            setValue(member, nil)
        }
    }

    private func caption(_ text: String) -> some View {
        Text(text)
            .font(.caption2).foregroundColor(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }
}

/// A minimal editable list of strings for an `is_list` parameter: one field per
/// value, add and remove.
private struct StringListEditor: View {
    let values: [String]
    let placeholder: String
    let setValues: ([String]) -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            ForEach(Array(values.enumerated()), id: \.offset) { index, value in
                HStack(spacing: 6) {
                    CommittingField(
                        value: value,
                        format: { $0 },
                        parse: { $0 },
                        placeholder: placeholder,
                        width: 260,
                        onCommit: { var v = values; v[index] = $0; setValues(v) })
                    Button {
                        var v = values; v.remove(at: index); setValues(v)
                    } label: { Image(systemName: "minus.circle") }
                        .buttonStyle(.borderless)
                }
            }
            Button {
                setValues(values + [""])
            } label: { Label("Add", systemImage: "plus") }
                .buttonStyle(.borderless)
                .controlSize(.small)
        }
    }
}
