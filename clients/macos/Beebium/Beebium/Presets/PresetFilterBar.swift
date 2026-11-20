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

import AppKit
import SwiftUI

/// Spike UI: the filter popups and search field above the preset catalogue. A
/// search field then one single-selection popup per facet, each defaulting to
/// Any; the row wraps to a second line rather than truncating. A Reset appears
/// once any popup is off Any. Choosing drives the shared `PresetFilterModel`,
/// which the catalogue reads to decide what to show.
struct PresetFilterBar: View {
    @ObservedObject var model: PresetFilterModel
    /// The popups to show, computed by the owner from the full catalogue so the
    /// counts are faceted against the current selection.
    let popups: [PresetFilterModel.Popup]

    var body: some View {
        FlowLayout(spacing: 8, lineSpacing: 8) {
            // A real NSSearchField: it shows the insertion caret and the standard
            // clear button, which a SwiftUI TextField in this container does not.
            SearchField(text: $model.searchText, placeholder: "Filter presets")
                .frame(width: 180)

            ForEach(popups) { popup in
                FacetPopupView(popup: popup) { choice in
                    model.select(popup.facet, choice)
                }
            }

            if model.isFiltering {
                Button("Reset") { model.reset() }
                    .buttonStyle(.link)
            }
        }
    }
}

/// One facet's popup: a menu button labelled "Facet: choice", whose items carry
/// the faceted count and disable themselves at zero (Any always enabled).
private struct FacetPopupView: View {
    let popup: PresetFilterModel.Popup
    let select: (FacetChoice) -> Void

    var body: some View {
        Menu {
            ForEach(popup.options) { option in
                Button {
                    select(option.choice)
                } label: {
                    if option.choice == popup.choice {
                        Label(option.label, systemImage: "checkmark")
                    } else {
                        Text(option.label)
                    }
                }
                .disabled(!option.isEnabled)
            }
        } label: {
            Text(buttonLabel).font(.callout)
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
    }

    /// The popup button's text: the facet name on its own while the choice is Any
    /// (the facet reads as a label), and "Facet: choice" once a choice is made, so
    /// the current selection is always visible.
    private var buttonLabel: String {
        switch popup.choice {
        case .any:          return popup.facet.label
        case .none:         return "\(popup.facet.label): None"
        case .value(let v): return "\(popup.facet.label): \(v)"
        }
    }
}

/// A standard macOS search field (caret, clear button, magnifier) bridged into
/// SwiftUI, bound to a string. SwiftUI's own TextField loses its insertion caret
/// in this bar, so the spike uses the AppKit control the rest of macOS uses here.
struct SearchField: NSViewRepresentable {
    @Binding var text: String
    var placeholder: String

    func makeNSView(context: Context) -> NSSearchField {
        let field = NSSearchField()
        field.placeholderString = placeholder
        field.delegate = context.coordinator
        field.sendsWholeSearchString = false
        field.sendsSearchStringImmediately = true
        field.focusRingType = .default
        return field
    }

    func updateNSView(_ nsView: NSSearchField, context: Context) {
        if nsView.stringValue != text { nsView.stringValue = text }
    }

    func makeCoordinator() -> Coordinator { Coordinator(text: $text) }

    final class Coordinator: NSObject, NSSearchFieldDelegate {
        private let text: Binding<String>
        init(text: Binding<String>) { self.text = text }

        func controlTextDidChange(_ notification: Notification) {
            guard let field = notification.object as? NSSearchField else { return }
            text.wrappedValue = field.stringValue
        }
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
