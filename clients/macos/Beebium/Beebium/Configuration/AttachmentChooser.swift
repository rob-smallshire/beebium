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

/// One option offered when attaching hardware to a point: an extension the user
/// can read about before adding it. Every field is catalogue text; nothing here
/// is spelled out per-extension in Swift.
struct AttachmentCandidate: Identifiable, Equatable {
    /// The extension identifier (e.g. "rfc2217-serial-client").
    let name: String
    /// The friendly name (e.g. "RFC 2217 Serial (client)").
    let displayName: String
    /// The catalogue description, shown in full for the selected candidate.
    let description: String
    /// A short summary of the settings it takes (its parameters' labels, e.g.
    /// "Host, Port"); empty when it takes none.
    let needs: String
    /// A short summary of what it provides (the display names of the attachment
    /// points it adds, e.g. "SCSI Bus"); empty when it provides none.
    let provides: String

    var id: String { name }
}

/// Turns a point's candidate extensions into `AttachmentCandidate`s, reading the
/// friendly name, description, parameters and provided points from the catalogue.
/// Pure, so the chooser's content is tested without the view.
enum AttachmentChooser {
    /// The candidates for a point, by friendly name, each with the text the chooser
    /// shows. `names` are the extensions the point currently accepts.
    static func candidates(names: [String], schema: ExtensionsSchemaSection) -> [AttachmentCandidate] {
        names.map { name in
            let entry = schema.entry(forExtension: name)
            return AttachmentCandidate(
                name: name,
                displayName: schema.displayName(forExtension: name),
                description: schema.description(forExtension: name),
                needs: needsText(entry),
                provides: providesText(entry, schema: schema))
        }
        .sorted { $0.displayName.localizedCaseInsensitiveCompare($1.displayName) == .orderedAscending }
    }

    /// The settings an extension takes, as a comma-joined list of its parameters'
    /// labels ("Host, Port"); empty when it has no parameters.
    static func needsText(_ entry: ExtensionCatalogueEntry?) -> String {
        guard let entry else { return "" }
        return entry.parameters.map(\.displayLabel).filter { !$0.isEmpty }.joined(separator: ", ")
    }

    /// What an extension provides, as a comma-joined list of the display names of
    /// the attachment points it adds ("SCSI Bus"); empty when it provides none.
    static func providesText(_ entry: ExtensionCatalogueEntry?, schema: ExtensionsSchemaSection) -> String {
        guard let entry else { return "" }
        return entry.provides.map { pointId in
            schema.attachmentPoints.first { $0.id == pointId }?.displayName ?? pointId
        }.joined(separator: ", ")
    }
}

/// A chooser for attaching hardware to a point: a list of candidates on the left
/// (friendly name with the identifier beneath), and for the selected one its full
/// description plus what it needs and what it provides. One component for every
/// point -- Add on any point, Change on a single-occupancy one -- shown as a
/// popover. Arrows move the selection, Return commits, Escape cancels.
struct AttachmentChooserView: View {
    let title: String
    let candidates: [AttachmentCandidate]
    let commitTitle: String
    /// The candidate to highlight when the chooser opens (the current choice, so
    /// Change opens on it); nil, or absent from `candidates`, falls back to the
    /// first row.
    var initialSelection: String? = nil
    let onCommit: (String) -> Void
    let onCancel: () -> Void

    @State private var selection: String?

    private var selected: AttachmentCandidate? {
        candidates.first { $0.id == selection }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title).font(.headline)

            HStack(alignment: .top, spacing: 12) {
                candidateList
                    .frame(width: 200)
                Divider()
                detail
                    .frame(maxWidth: .infinity, alignment: .topLeading)
            }
            .frame(height: 220)

            HStack {
                Spacer()
                Button("Cancel", action: onCancel)
                    .keyboardShortcut(.cancelAction)
                Button(commitTitle) { if let selection { onCommit(selection) } }
                    .keyboardShortcut(.defaultAction)
                    .disabled(selection == nil)
            }
        }
        .padding(16)
        .frame(width: 480)
        .onAppear {
            if selection == nil {
                if let initialSelection, candidates.contains(where: { $0.id == initialSelection }) {
                    selection = initialSelection
                } else {
                    selection = candidates.first?.id
                }
            }
        }
    }

    private var candidateList: some View {
        // Single click selects a row (native List selection), arrows move it, and
        // Return or the commit button chooses it. Do NOT add an .onTapGesture to a
        // row to get double-click-to-commit: a tap gesture on a List row -- even a
        // simultaneous count:2 one -- suppresses the List's own single-click
        // selection, so clicks stop selecting (verified in the real popover). The
        // row needs only its tag and a full-row hit area.
        List(candidates, selection: $selection) { candidate in
            VStack(alignment: .leading, spacing: 1) {
                Text(candidate.displayName)
                    .font(.body)
                Text(candidate.name)
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
            .tag(candidate.id)
            .contentShape(Rectangle())
        }
        .listStyle(.bordered(alternatesRowBackgrounds: true))
    }

    @ViewBuilder
    private var detail: some View {
        if let selected {
            ScrollView {
                VStack(alignment: .leading, spacing: 8) {
                    Text(selected.displayName).font(.headline)
                    if !selected.description.isEmpty {
                        Text(selected.description)
                            .font(.callout)
                            .foregroundColor(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    if !selected.needs.isEmpty {
                        summary("Needs", selected.needs)
                    }
                    if !selected.provides.isEmpty {
                        summary("Provides", selected.provides)
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        } else {
            Text("Select an option to see what it is.")
                .font(.callout)
                .foregroundColor(.secondary)
        }
    }

    private func summary(_ label: String, _ value: String) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 6) {
            Text("\(label):").font(.caption).foregroundColor(.secondary)
            Text(value).font(.caption).fixedSize(horizontal: false, vertical: true)
        }
    }
}
