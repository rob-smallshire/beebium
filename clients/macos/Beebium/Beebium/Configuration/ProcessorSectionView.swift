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

/// The Processor section: the Tube coprocessor (when the model has a tube point),
/// the emulation speed, and the MOS (operating-system) ROM. Shown for every
/// model, since speed and the MOS are universal.
struct ProcessorSectionView: View {
    @Binding var draft: MachineDraft
    /// The preset as loaded, so the MOS field can show what the user changed and
    /// Use Default can revert to it.
    var originalDraft: MachineDraft = MachineDraft()
    /// The extensions schema, for the tube attachment point's coprocessors.
    let extensionsSchema: ExtensionsSchemaSection
    /// The MOS schema (the model's default MOS ROM); nil hides the MOS field.
    var mosSchema: MosSchemaSection?
    /// Resolves a ROM image to its header (describe-rom), to show the MOS's title.
    var romResolver: (@MainActor (String) async -> RomHeaderInfo?)?
    var diagnostics: [PresetManager.PresetDiagnostic] = []
    var serialPorts: [String] = []

    /// Titles for the chosen/default MOS image, so the field shows "MOS 3.20"
    /// rather than a filename. Transient UI adornment, like the Memory section.
    @StateObject private var mosTitles = RomTitleCache()

    /// The Configurator's shared focus owner: the speed picker commits and
    /// releases a focused field before it acts.
    @Environment(\.fieldEditCoordinator) private var fieldEditCoordinator

    /// The fixed BBC second-processor bus. The Processor section owns the Tube
    /// buses (Peripherals excludes them) and presents each as a single-occupancy
    /// picker via AttachmentPointGroup.
    static let tubePointId = "tube"

    /// The Tube buses a coprocessor can sit on: the fixed `tube` bus plus every
    /// alternative its coprocessors list (the Master 128's `internal-tube`), so
    /// the internal Tube is reachable here and not adrift in Peripherals. Derived
    /// from the schema (the coprocessors' `attaches_to_one_of`), not hard-coded,
    /// so a model that adds another Tube bus needs no change here. In catalogue
    /// order; `tube` first.
    static func tubePointIds(in schema: ExtensionsSchemaSection) -> [String] {
        guard let tube = schema.attachmentPoints.first(where: { $0.id == tubePointId }) else {
            return []
        }
        var ids = [tubePointId]
        for name in tube.extensions {
            for alternative in schema.alternatives(forExtension: name) where !ids.contains(alternative) {
                ids.append(alternative)
            }
        }
        return ids
    }

    /// The model-built Tube points to show, in catalogue order.
    private var tubePoints: [AttachmentPoint] {
        let ids = Set(Self.tubePointIds(in: extensionsSchema))
        return extensionsSchema.attachmentPoints.filter { ids.contains($0.id) && $0.providedBy == nil }
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                if !tubePoints.isEmpty {
                    // Each Tube attachment-point node heads itself (its name and
                    // description), so no separate "Coprocessor" label. On the
                    // Master 128 both the external and the internal Tube appear.
                    ForEach(tubePoints) { tubePoint in
                        AttachmentPointGroup(
                            draft: $draft, schema: extensionsSchema, point: tubePoint,
                            diagnostics: diagnostics, serialPorts: serialPorts)
                    }
                    Divider()
                }
                speedControl
                if mosSchema != nil {
                    Divider()
                    mosControl
                }
            }
            .padding(12)
        }
        .onAppear { mosTitles.resolver = romResolver }
        .task(id: effectiveMos) {
            if let image = effectiveMos, !image.isEmpty { await mosTitles.resolve(image) }
        }
    }

    // MARK: - MOS ROM

    /// The MOS image the machine will boot with: the draft's own, else the
    /// model's default from the schema. nil only when there is no MOS schema.
    private var effectiveMos: String? {
        if let image = draft.mosImageUri, !image.isEmpty { return image }
        return mosSchema?.default
    }

    /// Whether the MOS differs from the preset as loaded.
    private var mosChanged: Bool {
        draft.mosImageUri != originalDraft.mosImageUri
    }

    /// The MOS ROM control: the current MOS (as a title when it resolves, else a
    /// filename), with Browse for another ROM image and Use Default to put the
    /// preset's MOS back. A bare ROM-name picker is a follow-up (#297); Browse
    /// plus Use Default is the mandatory behaviour.
    private var mosControl: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(mosSchema?.label.isEmpty == false ? mosSchema!.label : "MOS ROM")
                .font(.headline)

            HStack(spacing: 8) {
                Text(effectiveMos.map { mosTitles.displayText(forImage: $0) } ?? "Default")
                    .font(.subheadline)
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .frame(maxWidth: .infinity, alignment: .leading)

                if mosChanged {
                    Image(systemName: "pencil.circle.fill")
                        .font(.caption)
                        .foregroundColor(.accentColor)
                        .help("Changed from the preset's MOS ROM")
                }

                Button("Browse…") { browseForMos() }

                Button("Use Default") { draft.mosImageUri = originalDraft.mosImageUri }
                    .disabled(!mosChanged)
            }
            .frame(maxWidth: 360, alignment: .leading)

            Text("The operating-system ROM. Leave as the model's default unless you have a reason to change it.")
                .font(.caption)
                .foregroundColor(.secondary)

            DiagnosticLabels(diagnostics: diagnostics.atPath("/mos_image_uri"))
        }
    }

    /// Browse for a MOS ROM image file and record its path in the draft.
    private func browseForMos() {
        fieldEditCoordinator?.commitActiveEdit()
        let panel = NSOpenPanel()
        panel.title = "Select MOS ROM Image"
        panel.allowedContentTypes = [UTType(filenameExtension: "rom") ?? .data]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.directoryURL = DialogLocations.shared.startDirectory(for: .chooseRom)

        guard panel.runModal() == .OK, let url = panel.url else { return }
        DialogLocations.shared.remember(url, for: .chooseRom)
        draft.mosImageUri = url.path
        Task { await mosTitles.resolve(url.path) }
    }

    // MARK: - Speed

    private enum SpeedChoice: Hashable {
        case multiplier(Double)
        case unlimited
    }

    private var currentChoice: SpeedChoice {
        switch draft.speed {
        case .string(let s) where s == SpeedPresentation.unlimitedToken:
            return .unlimited
        case .int(let n):    return .multiplier(Double(n))
        case .double(let d): return .multiplier(d)
        default:             return .multiplier(1)
        }
    }

    /// The multipliers to offer: the presets, plus the current one if it is some
    /// other value from a loaded preset, so it shows and round-trips.
    private var offeredMultipliers: [Double] {
        var values = SpeedPresentation.presetMultipliers
        if case .multiplier(let m) = currentChoice, !values.contains(m) {
            values.append(m)
            values.sort()
        }
        return values
    }

    private var speedControl: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Emulation Speed").font(.headline)
            Picker("", selection: Binding(get: { currentChoice }, set: setChoice)) {
                ForEach(offeredMultipliers, id: \.self) { m in
                    Text(SpeedPresentation.label(forMultiplier: m)).tag(SpeedChoice.multiplier(m))
                }
                Text("Unlimited").tag(SpeedChoice.unlimited)
            }
            .labelsHidden()
            .frame(maxWidth: 220)

            DiagnosticLabels(diagnostics: diagnostics.atPath("/speed"))
        }
    }

    private func setChoice(_ choice: SpeedChoice) {
        fieldEditCoordinator?.commitActiveEdit()
        switch choice {
        case .unlimited:         draft.speed = .string(SpeedPresentation.unlimitedToken)
        case .multiplier(let m): draft.speed = .double(m)
        }
    }
}
