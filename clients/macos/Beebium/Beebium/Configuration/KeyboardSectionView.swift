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

/// The Keyboard section: the startup keyboard links, edited on the draft's
/// `startup`. Either a screen mode + auto-boot pair, or (advanced) the whole
/// links byte -- the two forms are mutually exclusive, as the schema says.
///
/// Key mapping, Caps/Shift-Lock policy and the like are client-side app
/// preferences, not part of the preset, so they are not here.
struct KeyboardSectionView: View {
    @Binding var draft: MachineDraft
    let schema: StartupSchemaSection
    var diagnostics: [PresetManager.PresetDiagnostic] = []

    private var usingLinks: Bool { draft.startup?.links != nil }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                Text("Startup").font(.headline)
                Text("The keyboard startup links the machine reads at power-on.")
                    .font(.caption).foregroundColor(.secondary)

                Picker("", selection: Binding(get: { usingLinks }, set: setUsingLinks)) {
                    Text("Screen mode & boot").tag(false)
                    Text("Raw links").tag(true)
                }
                .pickerStyle(.segmented)
                .fixedSize()

                if usingLinks {
                    linksEditor
                } else {
                    modeBootEditor
                }

                ForEach(Array(startupDiagnostics.enumerated()), id: \.offset) { _, diagnostic in
                    Label(diagnostic.message, systemImage: "exclamationmark.octagon.fill")
                        .font(.caption2).foregroundColor(.red)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .padding(12)
        }
    }

    // MARK: - Forms

    private var modeBootEditor: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Text("Screen mode")
                Picker("", selection: Binding(get: { screenMode }, set: { setModeBoot(screenMode: $0, autoBoot: autoBoot) })) {
                    ForEach(schema.screenMode.min...schema.screenMode.max, id: \.self) { mode in
                        Text("\(mode)").tag(mode)
                    }
                }
                .labelsHidden()
                .frame(width: 70)
            }
            Toggle("Auto-boot (SHIFT+BREAK)", isOn: Binding(
                get: { autoBoot },
                set: { setModeBoot(screenMode: screenMode, autoBoot: $0) }))
                .toggleStyle(.checkbox)
        }
    }

    private var linksEditor: some View {
        HStack(spacing: 6) {
            Text("Links byte")
            TextField("\(schema.links.min)", text: Binding(
                get: { String(links) },
                set: { setLinks(clampLinks($0)) }))
                .frame(maxWidth: 80)
            Text("(\(schema.links.min)-\(schema.links.max))")
                .font(.caption2).foregroundColor(.secondary)
        }
    }

    // MARK: - Reads

    private var screenMode: Int {
        draft.startup?.screenMode ?? schema.screenMode.default ?? schema.screenMode.min
    }
    private var autoBoot: Bool {
        draft.startup?.autoBoot ?? schema.autoBoot.default
    }
    private var links: Int {
        draft.startup?.links ?? schema.links.min
    }

    private var startupDiagnostics: [PresetManager.PresetDiagnostic] {
        diagnostics.filter { $0.path.hasPrefix("/startup") }
    }

    // MARK: - Writes (mutually exclusive forms)

    private func setUsingLinks(_ on: Bool) {
        if on {
            draft.startup = MachineDraft.Startup(links: links)
        } else {
            draft.startup = MachineDraft.Startup(screenMode: screenMode, autoBoot: autoBoot)
        }
    }

    private func setModeBoot(screenMode: Int, autoBoot: Bool) {
        draft.startup = MachineDraft.Startup(screenMode: screenMode, autoBoot: autoBoot)
    }

    private func setLinks(_ value: Int) {
        draft.startup = MachineDraft.Startup(links: value)
    }

    private func clampLinks(_ text: String) -> Int {
        let value = Int(text.trimmingCharacters(in: .whitespaces)) ?? schema.links.min
        return Swift.min(Swift.max(value, schema.links.min), schema.links.max)
    }
}
