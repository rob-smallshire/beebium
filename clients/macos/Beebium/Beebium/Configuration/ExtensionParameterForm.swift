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
                ForEach(manifest.parameters) { parameter in
                    parameterRow(parameter)
                }
            }
        }
    }

    @ViewBuilder
    private func parameterRow(_ parameter: ExtensionParameter) -> some View {
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
            ForEach(Array(parameterDiagnostics(parameter.key).enumerated()), id: \.offset) { _, diagnostic in
                Label(diagnostic.message, systemImage: "exclamationmark.octagon.fill")
                    .font(.caption2)
                    .foregroundColor(.red)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    @ViewBuilder
    private func control(_ parameter: ExtensionParameter) -> some View {
        if !parameter.choices.isEmpty {
            choicePicker(parameter)
        } else if parameter.isList {
            StringListEditor(values: listValue(parameter.key),
                             placeholder: parameter.defaultValue,
                             setValues: { setValue(parameter.key, $0.isEmpty ? nil : .array($0.map(JSONValue.string))) })
        } else {
            switch parameter.type {
            case "boolean":
                Toggle("", isOn: Binding(
                    get: { boolValue(parameter.key) },
                    set: { setValue(parameter.key, .bool($0)) }))
                    .labelsHidden()
            case "integer":
                integerField(parameter)
            case "filepath":
                filepathControl(parameter)
            default: // "string"
                if parameter.choicesFrom == "serial-ports" {
                    serialPortCombo(parameter)
                } else {
                    TextField(parameter.defaultValue, text: Binding(
                        get: { stringValue(parameter.key) },
                        set: { setValue(parameter.key, $0.isEmpty ? .string("") : .string($0)) }))
                        .frame(maxWidth: 260)
                }
            }
        }
    }

    /// A free-text field with a menu of host serial ports to fill it in.
    private func serialPortCombo(_ parameter: ExtensionParameter) -> some View {
        HStack(spacing: 6) {
            TextField(parameter.defaultValue, text: Binding(
                get: { stringValue(parameter.key) },
                set: { setValue(parameter.key, $0.isEmpty ? .string("") : .string($0)) }))
                .frame(maxWidth: 220)
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
        switch config[key] {
        case .string(let s): return !s.isEmpty
        case .array(let a):  return !a.isEmpty
        case .bool(let b):   return b
        case .int, .double:  return true
        case .object(let o): return !o.isEmpty
        case .null, .none:   return false
        }
    }

    private func choicePicker(_ parameter: ExtensionParameter) -> some View {
        let current = stringValue(parameter.key).isEmpty ? parameter.defaultValue : stringValue(parameter.key)
        return Picker("", selection: Binding(
            get: { current },
            set: { setValue(parameter.key, .string($0)) }
        )) {
            ForEach(parameter.choices) { choice in
                Text(choice.label).tag(choice.value)
            }
        }
        .labelsHidden()
        .frame(maxWidth: 260)
    }

    private func integerField(_ parameter: ExtensionParameter) -> some View {
        HStack(spacing: 6) {
            TextField(parameter.defaultValue, text: Binding(
                get: { stringValue(parameter.key) },
                set: { setValue(parameter.key, intJSON(from: $0, parameter: parameter)) }))
                .frame(maxWidth: 120)
            if let min = parameter.minimum, let max = parameter.maximum {
                Text("(\(min)-\(max))")
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
        }
    }

    private func filepathControl(_ parameter: ExtensionParameter) -> some View {
        let value = stringValue(parameter.key)
        let masters = parameter.bundled.isEmpty ? [] : (bundledImages[parameter.bundled] ?? [])
            .filter { $0.availableTo(extensionName: manifest.name) }
        // A bare name is a bundled master (per-machine working copy); a path is a file.
        let isBare = !value.isEmpty && !value.contains("/")
        let display = value.isEmpty ? "No file"
            : (isBare ? value : URL(fileURLWithPath: value).lastPathComponent)
        return HStack(spacing: 8) {
            Text(display)
                .font(.subheadline)
                .foregroundColor(value.isEmpty ? .secondary : .primary)
                .lineLimit(1).truncationMode(.middle)
                .help(value)
                .frame(maxWidth: .infinity, alignment: .leading)
            Menu {
                if !masters.isEmpty {
                    Section("Bundled discs") {
                        ForEach(masters, id: \.image) { master in
                            Button(master.displayName) { setValue(parameter.key, .string(master.image)) }
                        }
                    }
                }
                Button("Choose File…") { browse(parameter.key) }
                if !value.isEmpty {
                    Button("Clear") { setValue(parameter.key, .string("")) }
                }
            } label: {
                Text(value.isEmpty ? "Choose…" : "Change…")
            }
            .menuStyle(.borderlessButton)
            .fixedSize()
        }
        .onDrop(of: [.fileURL], isTargeted: nil) { providers in
            loadDroppedFile(providers) { url in setValue(parameter.key, .string(url.path)) }
        }
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

    /// An integer JSONValue from a field string, clamped to the parameter's
    /// bounds; a non-number clears the key so the default applies rather than
    /// writing an invalid value.
    private func intJSON(from text: String, parameter: ExtensionParameter) -> JSONValue? {
        let trimmed = text.trimmingCharacters(in: .whitespaces)
        if trimmed.isEmpty { return nil }
        guard var value = Int(trimmed) else { return nil }
        if let lower = parameter.minimum { value = Swift.max(value, lower) }
        if let upper = parameter.maximum { value = Swift.min(value, upper) }
        return .int(value)
    }

    private func parameterDiagnostics(_ key: String) -> [PresetManager.PresetDiagnostic] {
        diagnostics.filter { PresetDiagnosticLocation.configKey(path: $0.path) == key }
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
                    TextField(placeholder, text: Binding(
                        get: { value },
                        set: { var v = values; v[index] = $0; setValues(v) }))
                        .frame(maxWidth: 260)
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
