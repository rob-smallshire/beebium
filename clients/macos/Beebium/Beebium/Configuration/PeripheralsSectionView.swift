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

/// The Peripherals section: one group per attachment point, listing the draft's
/// extension instances on it, with Add (the extensions that attach there, minus
/// those excluded by occupancy) and Remove. Points an extension provides (e.g.
/// the SCSI bus) nest under their provider instance.
///
/// A projection over the draft via ExtensionInstanceModel; every edit writes
/// through. The Econet socket and the Tube appear here too for now; Network
/// (Stage 3) and Processor (Stage 4) will take those groups, so the group view
/// is kept reusable.
struct PeripheralsSectionView: View {
    @Binding var draft: MachineDraft
    let schema: ExtensionsSchemaSection
    /// The sideways schema, so a cartridge's group can name and link to the banks
    /// it provides in the Memory section. nil on a model with no cartridge slots.
    var sidewaysSchema: SidewaysSchemaSection? = nil
    var diagnostics: [PresetManager.PresetDiagnostic] = []
    var serialPorts: [String] = []

    /// Reveals (and the Memory view highlights) a point when another section's
    /// affordance sends the user here.
    @EnvironmentObject private var navigator: ConfigNavigator

    private var model: ExtensionInstanceModel { ExtensionInstanceModel(schema: schema) }

    /// The model points to show here. A caller (Processor) owns some points
    /// (e.g. the tube), which it passes to exclude.
    var excludedPointIds: Set<String> = []

    private var shownPoints: [AttachmentPoint] {
        model.modelPoints.filter { !excludedPointIds.contains($0.id) }
    }

    var body: some View {
        ScrollViewReader { proxy in
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    ForEach(shownPoints) { point in
                        AttachmentPointGroup(
                            draft: $draft, schema: schema, sidewaysSchema: sidewaysSchema,
                            point: point, diagnostics: diagnostics, serialPorts: serialPorts)
                            .id(point.id)
                    }
                }
                .padding(12)
            }
            .onAppear { revealIfNeeded(proxy) }
            .onChange(of: navigator.revealPointId) { _ in revealIfNeeded(proxy) }
        }
    }

    /// Scroll a point into view when the navigator asks (a Memory "No cartridge"
    /// row sending the user to the slot), then clear the one-shot cue.
    private func revealIfNeeded(_ proxy: ScrollViewProxy) {
        guard let point = navigator.revealPointId else { return }
        withAnimation { proxy.scrollTo(point, anchor: .top) }
        navigator.revealPointId = nil
    }
}
