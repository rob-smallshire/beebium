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
import AppKit

/// Shared presentation for a server startup / diagnostic error message.
///
/// A server diagnostic (bad ROM, port in use, protocol mismatch) can be a
/// long line with absolute paths. This view shows the message in full: it
/// wraps, and for text taller than the bound it scrolls rather than clipping
/// or pushing surrounding controls off-screen. The text is selectable for
/// partial copies, and an explicit Copy button copies the whole message so
/// the user never has to retype a diagnostic into a bug report.
struct ServerErrorView: View {
    let message: String
    /// Optional heading naming what failed (e.g. the preset being launched).
    /// Shown in bold above the diagnostic and included in the copied text so a
    /// pasted bug report is self-contained. Call sites without a machine
    /// context (a plain connect error) omit it rather than inventing a name.
    var title: String? = nil
    /// Tint for the icon and container. Red for an error, yellow for a warning.
    var tint: Color = .red
    /// When non-nil, a dismiss button is shown that invokes this.
    var onDismiss: (() -> Void)?

    /// Measured natural height of the message, so the scroll area hugs short
    /// messages and only caps (and scrolls) once they exceed the bound.
    @State private var textHeight: CGFloat = 0
    @State private var didCopy = false

    /// Upper bound on the message area; beyond this it scrolls.
    private let maxTextHeight: CGFloat = 140

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: "exclamationmark.triangle.fill")
                .foregroundColor(tint)
                .imageScale(.large)

            VStack(alignment: .leading, spacing: 4) {
                if let title {
                    Text(title)
                        .font(.callout)
                        .fontWeight(.semibold)
                        .foregroundColor(.primary)
                        .textSelection(.enabled)
                        .fixedSize(horizontal: false, vertical: true)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }

                ScrollView(.vertical) {
                    Text(message)
                        .font(.callout)
                        .foregroundColor(.primary)
                        .textSelection(.enabled)
                        .fixedSize(horizontal: false, vertical: true)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(GeometryReader { geo in
                            Color.clear.preference(key: ErrorTextHeightKey.self,
                                                   value: geo.size.height)
                        })
                }
                .frame(height: min(textHeight, maxTextHeight))
                .onPreferenceChange(ErrorTextHeightKey.self) { textHeight = $0 }
            }

            VStack(spacing: 6) {
                copyButton
                if let onDismiss {
                    Button(action: onDismiss) {
                        Image(systemName: "xmark.circle.fill")
                            .foregroundColor(.secondary)
                    }
                    .buttonStyle(.plain)
                    .help("Dismiss this message")
                }
            }
        }
        .padding(10)
        .background(tint.opacity(0.08))
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .stroke(tint.opacity(0.25), lineWidth: 1)
        )
        .cornerRadius(6)
    }

    /// Text placed on the clipboard: the heading (when present) followed by the
    /// verbatim diagnostic, so a pasted bug report names both the machine and
    /// the reason.
    private var copyableText: String {
        if let title {
            return title + "\n" + message
        }
        return message
    }

    private var copyButton: some View {
        Button {
            let pasteboard = NSPasteboard.general
            pasteboard.clearContents()
            pasteboard.setString(copyableText, forType: .string)
            withAnimation { didCopy = true }
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                withAnimation { didCopy = false }
            }
        } label: {
            Label(didCopy ? "Copied" : "Copy",
                  systemImage: didCopy ? "checkmark" : "doc.on.doc")
                .font(.caption)
        }
        .buttonStyle(.bordered)
        .controlSize(.small)
        .help("Copy the full message to the clipboard")
    }
}

/// Measures the natural height of the message text so the scroll area can
/// hug it until it reaches the bound.
private struct ErrorTextHeightKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) {
        value = max(value, nextValue())
    }
}

