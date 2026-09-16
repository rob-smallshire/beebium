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

extension Beebium_DiscErrorKind {
    /// The short, honest inline marker label for this failure kind, or nil for
    /// an unclassified failure -- in which case the caller keeps its own
    /// context-appropriate generic ("Insert failed", "Eject failed", ...), so a
    /// future server kind we don't yet map still reads honestly. The popover
    /// always shows the server's verbatim message regardless.
    var markerLabel: String? {
        switch self {
        case .cannotOpen: return "Cannot open"
        case .empty: return "Empty disc image"
        case .readError: return "Read error"
        case .unrecognised: return "Unrecognised format"
        case .loadFailed: return "Couldn't load disc"
        case .noController: return "No disc controller"
        case .invalidDrive: return "Invalid drive"
        case .driveOccupied: return "Drive occupied"
        case .unspecified, .UNRECOGNIZED: return nil
        }
    }

    /// Map the lower_snake token the `describe-disc-image` CLI emits (the
    /// pre-launch path) to the same enum, so both paths share one classification.
    init(describeToken token: String) {
        switch token {
        case "cannot_open": self = .cannotOpen
        case "empty": self = .empty
        case "read_error": self = .readError
        case "unrecognised": self = .unrecognised
        case "load_failed": self = .loadFailed
        default: self = .unspecified  // "none" and any unknown token
        }
    }
}

/// The "should the failure popover open now?" decision, factored out of the
/// SwiftUI view so it can be unit-tested directly. Popover-first means it opens
/// once per failure: on a real failure whose token differs from the one already
/// presented -- which covers the first failure, a replacement failure, and a
/// re-created view (presentedToken back to 0) with an existing failure -- but
/// not a re-layout after the user closed the popover but kept the marker (same
/// token), and never when there is no failure (a notice, a refusal, or nothing).
enum FailurePopoverDecision {
    static func decide(hasFailure: Bool, failureToken: Int,
                       presentedToken: Int) -> (present: Bool, presentedToken: Int) {
        guard hasFailure, failureToken != presentedToken else {
            return (false, presentedToken)
        }
        return (true, failureToken)
    }
}

/// The one status line a floppy drive row shows when something needs saying:
/// a live drop refusal, a self-clearing notice, or a persistent failure. Shared
/// by the live sidebar (StorageModeView) and the New Machine dialog
/// (FloppyDriveConfigView) so both behave identically.
///
/// A failure is POPOVER-FIRST: the server's full, verbatim message opens
/// immediately, anchored to this drive's row -- so a disc dropped on the wrong
/// drive visibly pops there. The inline part never carries the server text
/// (which would truncate); it is a fixed, honest marker -- "Insert failed",
/// a "Details..." re-entry, and an "x" to dismiss -- and it persists until the
/// user acts, so an accidental drop can neither be missed nor sit forever.
struct DriveStatusLine: View {
    /// A live drag's refusal, shown while it hovers. Passing, non-interactive.
    let refusalMessage: String?
    @ObservedObject var message: TransientMessage

    @State private var showDetail = false
    /// The failureToken whose popover we've already auto-opened, so we open once
    /// per failure -- not again when the view re-appears after the user closed
    /// the popover but kept the marker (close-but-keep must survive re-layout).
    @State private var presentedToken = 0

    var body: some View {
        content
            // Popover-first. onAppear covers the first failure (the view does
            // not exist until a message appears, so it can't observe the change
            // that created it); onChange covers a later failure replacing an
            // earlier one while the row is already on screen.
            .onAppear { presentIfNewFailure() }
            .onChange(of: message.failureToken) { _ in presentIfNewFailure() }
    }

    private func presentIfNewFailure() {
        let decision = FailurePopoverDecision.decide(
            hasFailure: message.failure != nil,
            failureToken: message.failureToken,
            presentedToken: presentedToken)
        presentedToken = decision.presentedToken
        if decision.present { showDetail = true }
    }

    @ViewBuilder
    private var content: some View {
        if let refusal = refusalMessage {
            plainLine(icon: "nosign", text: refusal)
        } else if let notice = message.noticeText {
            plainLine(icon: "exclamationmark.triangle.fill", text: notice)
        } else if let failure = message.failure {
            failureMarker(brief: failure.brief, detail: failure.detail)
        }
    }

    /// A refusal or a self-clearing notice: one line, non-interactive.
    private func plainLine(icon: String, text: String) -> some View {
        HStack(spacing: 4) {
            Image(systemName: icon)
                .font(.caption2)
            Text(text)
                .font(.caption)
                .lineLimit(1)
                .truncationMode(.middle)
        }
        .foregroundColor(.red)
        .frame(maxWidth: .infinity, alignment: .leading)
        .help(text)
    }

    /// A persistent failure: a fixed marker (never the server text) with a
    /// re-entry to the full message and an explicit dismiss.
    private func failureMarker(brief: String, detail: String) -> some View {
        HStack(spacing: 6) {
            Image(systemName: "exclamationmark.triangle.fill")
                .font(.caption2)
            Text(brief)
                .font(.caption)
            Button("Details...") { showDetail = true }
                .buttonStyle(.link)
                .font(.caption)
            Spacer(minLength: 0)
            Button {
                message.clear()
            } label: {
                Image(systemName: "xmark.circle.fill")
                    .foregroundColor(.red)
            }
            .buttonStyle(.plain)
            .help("Dismiss this error")
        }
        .foregroundColor(.red)
        .frame(maxWidth: .infinity, alignment: .leading)
        .popover(isPresented: $showDetail, arrowEdge: .bottom) {
            VStack(alignment: .trailing, spacing: 8) {
                // ServerErrorView provides the selectable text + Copy; Clear is
                // the explicit "understood, remove it" that also drops the
                // marker. Closing the popover any other way (click-away, Esc)
                // keeps the marker as the re-entry point.
                ServerErrorView(message: detail)
                    .frame(width: 360)
                Button("Clear") {
                    message.clear()
                    showDetail = false
                }
            }
            .padding(12)
        }
    }
}
