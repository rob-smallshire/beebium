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

/// Spike UI: the preset catalogue's filter row (client-specification.md 5.1). A
/// search field then one single-selection popup per facet. Each popup carries a
/// small uppercase caption above a fixed-width control, so the row never reflows
/// when a choice changes; it shows "Any" by default and the chosen value
/// otherwise. A Reset appears once anything is off Any or the search is used. The
/// row wraps (FlowLayout) rather than truncating. Choosing drives the shared
/// `PresetFilterModel`, which the catalogue reads to decide what to show.
struct PresetFilterBar: View {
    @ObservedObject var model: PresetFilterModel
    /// The popups to show, computed by the owner from the full catalogue so the
    /// counts are faceted against the current selection.
    let popups: [PresetFilterModel.Popup]

    var body: some View {
        FlowLayout(spacing: 12, lineSpacing: 10) {
            // The search field, captioned blank so its control lines up with the
            // popups' controls beneath their captions.
            captioned(" ") {
                SearchField(text: $model.searchText, placeholder: "Filter presets")
                    .frame(width: 180)
            }

            ForEach(popups) { popup in
                FacetPopupView(popup: popup) { choice in
                    model.select(popup.facet, choice)
                }
            }

            if model.isFiltering {
                captioned(" ") {
                    Button("Reset") { model.reset() }
                        .buttonStyle(.link)
                }
            }
        }
    }

    /// A control under a small uppercase caption, so every item in the row aligns
    /// its control on the same baseline.
    @ViewBuilder
    private func captioned<Content: View>(_ caption: String,
                                          @ViewBuilder _ content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(caption)
                .font(.caption2)
                .foregroundColor(.secondary)
                .lineLimit(1)
            content()
        }
    }

    var isFiltering: Bool { model.isFiltering }
}

/// One facet's popup: a caption above a single-selection menu of fixed width. The
/// button shows "Any" or the chosen value (no "Facet: value" composite); its
/// width is fixed to the longest value so the control never resizes.
private struct FacetPopupView: View {
    let popup: PresetFilterModel.Popup
    let select: (FacetChoice) -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(popup.facet.label.uppercased())
                .font(.caption2)
                .foregroundColor(.secondary)
                .lineLimit(1)

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
                Text(valueLabel)
                    .frame(width: valueWidth, alignment: .leading)
            }
            .fixedSize()
        }
    }

    /// The button's text: "Any" by default, the chosen value otherwise.
    private var valueLabel: String {
        switch popup.choice {
        case .any:          return "Any"
        case .none:         return "None"
        case .value(let v): return v
        }
    }

    /// Every value the button might show, so its width can be fixed to the widest.
    private var buttonValues: [String] {
        popup.options.map { option in
            switch option.choice {
            case .any:          return "Any"
            case .none:         return "None"
            case .value(let v): return v
            }
        }
    }

    /// The fixed width of the value area, measured from the widest value in the
    /// menu's own font, so the control does not resize when the choice changes.
    private var valueWidth: CGFloat {
        let font = NSFont.menuFont(ofSize: NSFont.systemFontSize)
        let widest = buttonValues
            .map { ($0 as NSString).size(withAttributes: [.font: font]).width }
            .max() ?? 40
        return widest.rounded(.up)
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
