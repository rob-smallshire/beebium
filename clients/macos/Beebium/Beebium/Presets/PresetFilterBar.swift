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

/// Spike UI: the filter chips and search field above the preset catalogue. One
/// row of chips per tag group, each chip carrying a live count; a search field
/// beside them; and a Clear affordance once any chip is on. Toggling a chip or
/// typing drives the shared `PresetFilterModel`, which the catalogue reads to
/// decide what to show.
struct PresetFilterBar: View {
    @ObservedObject var model: PresetFilterModel
    /// The groups to show, computed by the owner from the full catalogue so the
    /// counts are faceted against the current selection.
    let groups: [PresetFilterModel.ChipGroup]

    /// The width of the group labels, so every group's chips start at the same x.
    private let labelWidth: CGFloat = 92

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Image(systemName: "magnifyingglass")
                    .foregroundColor(.secondary)
                    .font(.caption)
                TextField("Filter presets", text: $model.searchText)
                    .textFieldStyle(.roundedBorder)
                    .frame(maxWidth: 240)

                Spacer()

                if !model.selected.isEmpty {
                    Button("Clear") { model.clearChips() }
                        .buttonStyle(.link)
                }
            }

            ForEach(groups) { group in
                HStack(alignment: .top, spacing: 8) {
                    Text(group.group.rawValue)
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .frame(width: labelWidth, alignment: .leading)
                        .padding(.top, 3)

                    FlowLayout(spacing: 6, lineSpacing: 6) {
                        ForEach(group.chips) { chip in
                            ChipView(chip: chip) { model.toggle(chip.tag) }
                        }
                    }
                }
            }
        }
    }
}

/// A single filter chip: its value, a count, and selected / greyed state.
private struct ChipView: View {
    let chip: PresetFilterModel.Chip
    let toggle: () -> Void

    var body: some View {
        Button(action: toggle) {
            HStack(spacing: 4) {
                Text(chip.tag.value)
                    .font(.caption)
                Text("\(chip.count)")
                    .font(.caption.monospacedDigit())
                    .opacity(0.65)
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 3)
            .background(
                Capsule().fill(fill)
            )
            .overlay(
                Capsule().stroke(stroke, lineWidth: 1)
            )
            .foregroundColor(foreground)
        }
        .buttonStyle(.plain)
        .disabled(!chip.isEnabled)
        .help(chip.isEnabled
              ? "\(chip.count) preset\(chip.count == 1 ? "" : "s")"
              : "No presets match with the current filters")
    }

    private var fill: Color {
        chip.isSelected ? Color.accentColor.opacity(0.22) : Color.secondary.opacity(0.12)
    }
    private var stroke: Color {
        chip.isSelected ? Color.accentColor.opacity(0.6) : Color.clear
    }
    private var foreground: Color {
        if !chip.isEnabled { return .secondary.opacity(0.5) }
        return chip.isSelected ? .accentColor : .primary
    }
}

/// A minimal wrapping (flow) layout: lays subviews left to right, wrapping to the
/// next line when the proposed width runs out. macOS 13's `Layout` protocol.
struct FlowLayout: Layout {
    var spacing: CGFloat = 6
    var lineSpacing: CGFloat = 6

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        layout(subviews: subviews, maxWidth: proposal.width ?? .infinity).size
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize,
                       subviews: Subviews, cache: inout ()) {
        let origins = layout(subviews: subviews, maxWidth: bounds.width).origins
        for (index, origin) in origins.enumerated() {
            subviews[index].place(
                at: CGPoint(x: bounds.minX + origin.x, y: bounds.minY + origin.y),
                anchor: .topLeading, proposal: .unspecified)
        }
    }

    private func layout(subviews: Subviews, maxWidth: CGFloat) -> (origins: [CGPoint], size: CGSize) {
        var origins: [CGPoint] = []
        var x: CGFloat = 0, y: CGFloat = 0, rowHeight: CGFloat = 0, maxX: CGFloat = 0
        for subview in subviews {
            let size = subview.sizeThatFits(.unspecified)
            if x > 0 && x + size.width > maxWidth {
                x = 0
                y += rowHeight + lineSpacing
                rowHeight = 0
            }
            origins.append(CGPoint(x: x, y: y))
            x += size.width + spacing
            rowHeight = max(rowHeight, size.height)
            maxX = max(maxX, x - spacing)
        }
        return (origins, CGSize(width: maxX, height: y + rowHeight))
    }
}
