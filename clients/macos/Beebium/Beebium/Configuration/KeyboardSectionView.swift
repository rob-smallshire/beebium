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
/// `startup`.
///
/// The startup links are one byte, shown in four live presentations that all
/// edit the same byte: the friendly screen mode and auto-boot controls, a check
/// box per link, a hex field and a decimal field. Change any one and the others
/// follow, because each is a projection of the single byte (held in the draft).
/// The byte's structure -- which bits are the screen mode, which is auto-boot,
/// what each link is called -- comes entirely from the schema's links layout, so
/// no BBC bit knowledge lives here.
///
/// The draft keeps whichever form the preset uses: the friendly
/// screen_mode/auto_boot pair while every bit outside those is at its default,
/// switching to the whole `links` byte the moment one of the other bits differs,
/// and back again when they return to the default (the two forms are mutually
/// exclusive, as the schema says).
///
/// Key mapping, Caps/Shift-Lock policy and the like are client-side app
/// preferences, not part of the preset, so they are not here.
struct KeyboardSectionView: View {
    @Binding var draft: MachineDraft
    let schema: StartupSchemaSection
    var diagnostics: [PresetManager.PresetDiagnostic] = []

    /// Shared focus for the hex and decimal fields, so a check box, the picker or
    /// a background click releases whichever is focused (it then commits or
    /// refreshes). Without this SwiftUI leaves a byte field focused and stale when
    /// another control changes the byte.
    @FocusState private var focusedField: String?

    private var layout: StartupSchemaSection.LinksLayout { schema.links }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text("Startup").font(.headline)
                Text("The keyboard startup links the machine reads at power-on. "
                     + "These are four views of one byte: change any and the rest follow.")
                    .font(.caption).foregroundColor(.secondary)
                    .fixedSize(horizontal: false, vertical: true)

                friendlyControls
                Divider()
                linksControls

                DiagnosticLabels(diagnostics: diagnostics.withPrefix("/startup"))
            }
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
            // A click on the section background releases a focused byte field.
            .contentShape(Rectangle())
            .onTapGesture { focusedField = nil }
        }
    }

    // MARK: - Friendly controls (screen mode + auto-boot)

    private var friendlyControls: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Text("Screen mode")
                Picker("", selection: Binding(
                    get: { screenMode },
                    set: { focusedField = nil; setByte(layout.byte(currentByte, settingScreenMode: $0)) })) {
                    ForEach(schema.screenMode.min...schema.screenMode.max, id: \.self) { mode in
                        Text("\(mode)").tag(mode)
                    }
                }
                .labelsHidden()
                .frame(width: 70)
            }
            Toggle(isOn: Binding(
                get: { autoBoot },
                set: { focusedField = nil; setByte(layout.byte(currentByte, settingAutoBoot: $0)) })) {
                VStack(alignment: .leading, spacing: 1) {
                    Text("Auto-boot")
                    Text(Self.autoBootBehaviour(layout: layout))
                        .font(.caption2).foregroundColor(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .toggleStyle(.checkbox)
            .help(Self.autoBootBehaviour(layout: layout))
        }
    }

    // MARK: - Links (check boxes + hex + decimal)

    private var linksControls: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Links").font(.subheadline).fontWeight(.medium)

            ForEach(layout.bits) { link in
                Toggle(isOn: Binding(
                    get: { layout.isLinkMade(bit: link.bit, in: currentByte) },
                    set: { focusedField = nil; setByte(layout.byte(currentByte, settingLink: link.bit, made: $0)) })) {
                    VStack(alignment: .leading, spacing: 1) {
                        Text("\(link.link): \(link.label)")
                        if !link.description.isEmpty {
                            Text(link.description)
                                .font(.caption2).foregroundColor(.secondary)
                                .fixedSize(horizontal: false, vertical: true)
                        }
                    }
                }
                .toggleStyle(.checkbox)
                .help(link.description)
            }

            HStack(spacing: 16) {
                HStack(spacing: 6) {
                    Text("Hex")
                    CommittingField(value: currentByte, format: Self.formatHexByte,
                                    parse: Self.parseHexByte, width: 80,
                                    focusedField: $focusedField, fieldID: "hex", onCommit: setByte)
                }
                HStack(spacing: 6) {
                    Text("Decimal")
                    CommittingField(value: currentByte, format: { String($0) },
                                    parse: Self.parseDecByte, width: 80,
                                    focusedField: $focusedField, fieldID: "decimal", onCommit: setByte)
                }
            }
            .padding(.top, 4)
        }
    }

    // MARK: - The one byte (derived from the draft)

    /// The current links byte: the explicit `links` value, else the byte the
    /// friendly screen_mode/auto_boot pair stands for, else the layout default.
    private var currentByte: Int {
        if let links = draft.startup?.links {
            return Swift.min(Swift.max(links, layout.min), layout.max)
        }
        let mode = draft.startup?.screenMode ?? schema.screenMode.default ?? schema.screenMode.min
        let boot = draft.startup?.autoBoot ?? schema.autoBoot.default
        return layout.byte(screenMode: mode, autoBoot: boot)
    }

    private var screenMode: Int { layout.screenMode(fromByte: currentByte) }
    private var autoBoot: Bool { layout.autoBoot(fromByte: currentByte) }

    /// Write a new byte back to the draft, choosing the form by the rule: the
    /// friendly screen_mode/auto_boot pair when every bit outside those is at its
    /// default (lossless), otherwise the whole `links` byte.
    private func setByte(_ byte: Int) {
        draft.startup = Self.startup(forByte: byte, layout: layout,
                                     extra: draft.startup?.extra ?? [:])
    }

    /// The draft startup form for a links byte, by the writing rule: the friendly
    /// screen_mode/auto_boot pair when the byte is losslessly representable that
    /// way, otherwise the whole `links` byte. Pure, so the rule is testable.
    static func startup(forByte byte: Int, layout: StartupSchemaSection.LinksLayout,
                        extra: [String: JSONValue] = [:]) -> MachineDraft.Startup {
        let clamped = Swift.min(Swift.max(byte, layout.min), layout.max)
        if layout.isFriendlyRepresentable(clamped) {
            return MachineDraft.Startup(
                screenMode: layout.screenMode(fromByte: clamped),
                autoBoot: layout.autoBoot(fromByte: clamped),
                extra: extra)
        }
        return MachineDraft.Startup(links: clamped, extra: extra)
    }

    // MARK: - Byte field parsing (pure, testable)

    /// Parse a hex byte: 00..FF, with an optional `&` or `0x` prefix. nil when
    /// malformed or out of range.
    static func parseHexByte(_ text: String) -> Int? {
        var t = text.trimmingCharacters(in: .whitespaces).lowercased()
        if t.hasPrefix("&") { t.removeFirst() }
        else if t.hasPrefix("0x") { t.removeFirst(2) }
        guard !t.isEmpty, let value = Int(t, radix: 16), (0...255).contains(value) else { return nil }
        return value
    }

    /// Parse a decimal byte: 0..255. nil when malformed or out of range.
    static func parseDecByte(_ text: String) -> Int? {
        let t = text.trimmingCharacters(in: .whitespaces)
        guard let value = Int(t), (0...255).contains(value) else { return nil }
        return value
    }

    /// The hex byte as two digits, with no radix prefix: the field's "Hex" label
    /// already says the radix, so a leading `&` would only be noise. Input still
    /// accepts a `&` or `0x` prefix (see parseHexByte).
    static func formatHexByte(_ byte: Int) -> String { String(format: "%02X", byte) }

    /// The explanatory text shown beneath the friendly auto-boot toggle. It comes
    /// from the schema's auto_boot link entry, so the toggle and that bit's own
    /// link check box describe the bit from one source; a built-in sentence
    /// stands in only when the schema gives no description. Either way it names
    /// the power-on reset as well as BREAK, which the link also governs.
    static func autoBootBehaviour(layout: StartupSchemaSection.LinksLayout) -> String {
        let behaviour = layout.autoBootBitEntry?.description ?? ""
        return behaviour.isEmpty
            ? "reset or BREAK boots; SHIFT+reset or SHIFT+BREAK does not"
            : behaviour
    }
}
