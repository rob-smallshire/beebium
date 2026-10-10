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

/// The Network section: Econet on/off, the station number, and the transport
/// and its parameters. Edits the draft's `econet` key directly (station and
/// transport live there, not in extensions[]); the transport's parameters reuse
/// the generic ExtensionParameterForm over econet.transport.parameters.
///
/// Shown when the model has an Econet socket (the schema has an econet section).
/// Presets are starting points, not fences: a model with Econet can gain it here
/// even if the chosen preset had none.
struct NetworkSectionView: View {
    @Binding var draft: MachineDraft
    let schema: EconetSchemaSection
    /// The extensions schema, whose catalogue supplies the transport's parameter
    /// manifest (a transport is a kind of extension). nil only on an older server.
    let extensionsSchema: ExtensionsSchemaSection?
    var diagnostics: [PresetManager.PresetDiagnostic] = []
    var serialPorts: [String] = []

    private enum StationMode: Hashable { case number, auto, range }

    private var transportName: String? { draft.econet?.transport?.name }

    /// A transport's friendly name from the catalogue, falling back to its id.
    private func transportDisplayName(_ name: String) -> String {
        extensionsSchema?.displayName(forExtension: name) ?? name
    }

    /// A transport's description from the catalogue, or "" when none.
    private func transportDescription(_ name: String) -> String {
        extensionsSchema?.description(forExtension: name) ?? ""
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Toggle("Econet", isOn: Binding(get: { draft.econet != nil }, set: setEnabled))
                    .toggleStyle(.switch)
                    .font(.headline)

                if draft.econet != nil {
                    stationEditor
                    Divider()
                    transportEditor
                } else {
                    Text("Econet is off. Turn it on to give this machine a station number and a transport.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
            .padding(12)
        }
    }

    // MARK: - Station

    private var stationEditor: some View {
        let mode = currentMode
        return VStack(alignment: .leading, spacing: 6) {
            Text("Station")
                .font(.subheadline).fontWeight(.medium)
            Picker("", selection: Binding(get: { mode }, set: setMode)) {
                Text("Number").tag(StationMode.number)
                if schema.station.auto {
                    Text("Automatic").tag(StationMode.auto)
                    Text("Automatic in range").tag(StationMode.range)
                }
            }
            .pickerStyle(.segmented)
            .fixedSize()

            stationInput(mode)

            // The server validates the station and reports it at /econet/station,
            // the single source of truth; the clamped number field above is only
            // a UI nicety, not a second copy of the rules.
            DiagnosticLabels(diagnostics: stationDiagnostics)
        }
    }

    // The per-mode input, type-erased: a @ViewBuilder switch returning several
    // concrete View types crashes the Release whole-module optimiser with no
    // diagnostic (see ExtensionViewRenderer).
    private func stationInput(_ mode: StationMode) -> AnyView {
        switch mode {
        case .number:
            return AnyView(HStack(spacing: 6) {
                TextField("\(schema.station.min)", text: Binding(
                    get: { numberText },
                    set: { setStation(.number(clampStation($0))) }))
                    .frame(maxWidth: 100)
                Text("(\(schema.station.min)-\(schema.station.max))")
                    .font(.caption2).foregroundColor(.secondary)
            })
        case .auto:
            return AnyView(Text("A free station number is chosen on the network at launch.")
                .font(.caption2).foregroundColor(.secondary))
        case .range:
            return AnyView(HStack(spacing: 6) {
                Text("from")
                TextField("\(schema.station.autoDefaultLow)", text: Binding(
                    get: { String(rangeLow) }, set: { setRange(low: clampStation($0), high: rangeHigh) }))
                    .frame(maxWidth: 70)
                Text("to")
                TextField("\(schema.station.autoDefaultHigh)", text: Binding(
                    get: { String(rangeHigh) }, set: { setRange(low: rangeLow, high: clampStation($0)) }))
                    .frame(maxWidth: 70)
            })
        }
    }

    // MARK: - Transport

    private var transportEditor: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Transport")
                .font(.subheadline).fontWeight(.medium)
            Picker("", selection: Binding(
                get: { transportName ?? "" },
                set: setTransport
            )) {
                Text("None").tag("")
                ForEach(schema.transports, id: \.self) { name in
                    Text(transportDisplayName(name)).tag(name)
                }
            }
            .labelsHidden()
            .frame(maxWidth: 220)

            if let name = transportName, !transportDescription(name).isEmpty {
                Text(transportDescription(name))
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            if let name = transportName {
                ExtensionParameterForm(
                    manifest: extensionsSchema?.manifest(forExtension: name)
                        ?? ExtensionManifest(name: name, parameters: []),
                    config: draft.econet?.transport?.parameters ?? [:],
                    diagnostics: [],   // econet param diagnostics show below, section-level
                    serialPortSuggestions: serialPorts,
                    setValue: setTransportParameter)
                    .padding(.leading, 8)
            }

            DiagnosticLabels(diagnostics: transportDiagnostics)
        }
    }

    // MARK: - Current station reading

    private var station: EconetStation { EconetStation(draft.econet?.station) }

    private var currentMode: StationMode {
        switch station {
        case .number: return .number
        case .auto:   return .auto
        case .autoRange: return .range
        }
    }

    private var numberText: String {
        if case .number(let n) = station { return String(n) }
        return ""
    }

    private var rangeLow: Int { rangeComponents.0 }
    private var rangeHigh: Int { rangeComponents.1 }

    private var rangeComponents: (Int, Int) {
        if case .autoRange(let lo, let hi) = station { return (lo, hi) }
        return (schema.station.autoDefaultLow, schema.station.autoDefaultHigh)
    }

    private var stationDiagnostics: [PresetManager.PresetDiagnostic] {
        diagnostics.atPath("/econet/station")
    }

    private var transportDiagnostics: [PresetManager.PresetDiagnostic] {
        diagnostics.withPrefix("/econet/transport")
    }

    // MARK: - Edits (write through to draft.econet)

    private func setEnabled(_ on: Bool) {
        if on {
            let station: JSONValue = schema.station.auto ? .string("auto") : .int(schema.station.min)
            let transport = schema.transports.first.map {
                MachineDraft.Transport(name: $0, parameters: [:])
            }
            draft.econet = MachineDraft.Econet(station: station, transport: transport)
        } else {
            draft.econet = nil
        }
    }

    private func setMode(_ mode: StationMode) {
        switch mode {
        case .number: setStation(.number(currentNumberOrDefault))
        case .auto:   setStation(.auto)
        case .range:  setStation(.autoRange(schema.station.autoDefaultLow, schema.station.autoDefaultHigh))
        }
    }

    private var currentNumberOrDefault: Int {
        if case .number(let n) = station, n >= schema.station.min { return n }
        return schema.station.min
    }

    private func setStation(_ station: EconetStation) {
        guard var econet = draft.econet else { return }
        econet.station = station.jsonValue
        draft.econet = econet
    }

    private func setRange(low: Int, high: Int) {
        setStation(.autoRange(low, high))
    }

    private func clampStation(_ text: String) -> Int {
        let value = Int(text.trimmingCharacters(in: .whitespaces)) ?? schema.station.min
        return Swift.min(Swift.max(value, schema.station.min), schema.station.max)
    }

    private func setTransport(_ name: String) {
        guard var econet = draft.econet else { return }
        // Changing transport starts fresh parameters; resolve-preset fills the
        // new transport's defaults.
        econet.transport = name.isEmpty ? nil : MachineDraft.Transport(name: name, parameters: [:])
        draft.econet = econet
    }

    private func setTransportParameter(_ key: String, _ value: JSONValue?) {
        guard var econet = draft.econet, var transport = econet.transport else { return }
        var params = transport.parameters ?? [:]
        if let value = value {
            params[key] = value
        } else {
            params.removeValue(forKey: key)
        }
        transport.parameters = params.isEmpty ? nil : params
        econet.transport = transport
        draft.econet = econet
    }
}
