// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

import Foundation
import GRPC
import SwiftUI

/// Typed payload for an Extension UI dispatch event. Each variant maps
/// to one of the proto DispatchRequest oneof fields (or to "no payload"
/// for fire-only Buttons).
enum ExtensionDispatchPayload: Sendable {
    case none                                  // Button (no payload)
    case bool(Bool)                            // Toggle
    case string(String)                        // TextInput
    case index(UInt32)                         // Choice
    case editorCommit([EditorFieldCommit])     // ModalEditor
    case editableListEvent(EditableListIntent) // EditableList (add/edit/remove/action)
    case fileAction(String)                    // FileReference server action id
}

/// One field's value inside a ModalEditor's EditorCommit. Mirrors the
/// proto EditorFieldValue: the `fieldID` names the sub-control in the
/// editor tree; `value` carries the sub-control's local-buffer state at
/// commit time.
struct EditorFieldCommit: Sendable, Equatable {
    let fieldID: String
    let value: EditorFieldValue

    enum EditorFieldValue: Sendable, Equatable {
        case bool(Bool)
        case string(String)
        case index(UInt32)
    }
}

/// Client for the server-driven Extension UI framework.
///
/// One client subscribes to multiple per-extension View streams (keyed
/// by extension name) and exposes the latest View for each as an
/// observable property. Dispatch is unary: the server validates and
/// runs the event, and the resulting state change appears as the next
/// pushed View on the SubscribeView stream.
@MainActor
final class ExtensionUiClient: ObservableObject, Disconnectable {
    /// Latest View for each subscribed extension, keyed by extension name.
    /// Dropped when the extension's stream finishes or is rejected; kept as
    /// last-known when the stream is lost (see handleSubscriptionEnd).
    @Published private(set) var views: [String: Beebium_View] = [:]

    /// Per-extension error message (e.g. "extension not found", network drop).
    @Published private(set) var errors: [String: String] = [:]

    private var client: Beebium_ExtensionUiServiceNIOClient?
    private var subscriptionTasks: [String: Task<Void, Never>] = [:]

    /// Connect to the server using an existing gRPC channel. After this
    /// the caller can `subscribe(to:)` for each extension whose UI it
    /// wants to display.
    func connect(channel: GRPCChannel) {
        client = Beebium_ExtensionUiServiceNIOClient(channel: channel)
    }

    /// Disconnect: cancel every active subscription and drop state.
    func disconnect() {
        for task in subscriptionTasks.values {
            task.cancel()
        }
        subscriptionTasks.removeAll()
        client = nil
        views.removeAll()
        errors.removeAll()
    }

    /// Open a server-stream of View updates for the given extension.
    /// Idempotent: calling subscribe twice for the same extension is a
    /// no-op (the existing stream stays open). See handleSubscriptionEnd
    /// for what a stream ending does to the cached View.
    func subscribe(to extensionID: String) {
        guard subscriptionTasks[extensionID] == nil else { return }
        guard let client = client else { return }

        var request = Beebium_SubscribeViewRequest()
        request.extensionID = extensionID

        let task = Task<Void, Never> { [weak self] in
            guard let self = self else { return }
            await self.runSubscription(extensionID: extensionID,
                                       request: request,
                                       client: client)
        }
        subscriptionTasks[extensionID] = task
    }

    /// Cancel a single extension's subscription and drop its View.
    func unsubscribe(from extensionID: String) {
        subscriptionTasks[extensionID]?.cancel()
        subscriptionTasks.removeValue(forKey: extensionID)
        views.removeValue(forKey: extensionID)
        errors.removeValue(forKey: extensionID)
    }

    /// Send a Dispatch event to the server. Returns whether the
    /// framework's validation gauntlet accepted the request; the actual
    /// state change shows up on the SubscribeView stream as the next
    /// pushed View, not in this return value.
    @discardableResult
    func dispatch(extension extensionID: String,
                  controlId: String,
                  viewRevision: UInt64,
                  payload: ExtensionDispatchPayload) async -> Bool {
        guard let client = client else { return false }

        var request = Beebium_DispatchRequest()
        request.extensionID = extensionID
        request.controlID = controlId
        request.viewRevision = viewRevision
        switch payload {
        case .none:
            break  // leave the oneof unset (Button)
        case .bool(let value):
            request.boolValue = value
        case .string(let value):
            request.stringValue = value
        case .index(let value):
            request.indexValue = value
        case .editorCommit(let fields):
            request.editorCommit = Self.editorCommit(from: fields)
        case .editableListEvent(let intent):
            var event = Beebium_EditableListEvent()
            switch intent.kind {
            case .add:    event.kind = .add
            case .edit:   event.kind = .edit
            case .remove: event.kind = .remove
            case .action: event.kind = .action
            }
            event.itemID = intent.itemID
            event.actionID = intent.actionID
            if !intent.commit.isEmpty {
                event.commit = Self.editorCommit(from: intent.commit)
            }
            request.editableListEvent = event
        case .fileAction(let actionID):
            request.fileActionID = actionID
        }

        do {
            let response = try await client.dispatch(request).response.get()
            if !response.accepted {
                NSLog("[ExtensionUiClient] dispatch rejected (%@/%@): %@",
                      extensionID, controlId, response.error)
            }
            return response.accepted
        } catch {
            NSLog("[ExtensionUiClient] dispatch error (%@/%@): %@",
                  extensionID, controlId, error.localizedDescription)
            return false
        }
    }

    // MARK: - Private

    /// Build a proto EditorCommit from the renderer's field-commit list, shared
    /// by the ModalEditor and EditableList add/edit dispatches.
    private static func editorCommit(from fields: [EditorFieldCommit]) -> Beebium_EditorCommit {
        var commit = Beebium_EditorCommit()
        for field in fields {
            var proto = Beebium_EditorFieldValue()
            proto.fieldID = field.fieldID
            switch field.value {
            case .bool(let value):
                proto.boolValue = value
            case .string(let value):
                proto.stringValue = value
            case .index(let value):
                proto.indexValue = value
            }
            commit.fields.append(proto)
        }
        return commit
    }

    private func runSubscription(extensionID: String,
                                 request: Beebium_SubscribeViewRequest,
                                 client: Beebium_ExtensionUiServiceNIOClient) async {
        let call = client.subscribeView(request) { [weak self] view in
            Task { @MainActor [weak self] in
                self?.handleView(view)
            }
        }

        // A server-streaming call's status future succeeds with a non-OK code
        // when the server goes away; it only throws for local errors.
        let end: SubscriptionEnd
        do {
            let status = try await call.status.get()
            switch status.code {
            case .ok, .cancelled, .notFound:
                end = .finished
            default:
                end = .lost(status.description)
            }
        } catch {
            end = .lost(error.localizedDescription)
        }
        await MainActor.run { [weak self] in
            self?.handleSubscriptionEnd(end, extensionID: extensionID)
        }
    }

    /// Record a View pushed on an extension's stream.
    func handleView(_ view: Beebium_View) {
        views[view.extensionID] = view
        errors.removeValue(forKey: view.extensionID)
    }

    /// How an extension's View stream ended.
    enum SubscriptionEnd: Equatable {
        /// Closed OK by the server, cancelled by us, or refused because there
        /// is no such extension.
        case finished
        /// The stream was lost: the server is gone or unreachable.
        case lost(String)
    }

    /// Apply the end of an extension's View stream. A finished stream drops the
    /// cached View, since the server no longer vouches for it. A lost stream
    /// keeps the last View so the panel can stay on screen, presented as stale
    /// by its owner, instead of vanishing and reading as "this machine has no
    /// such panel".
    func handleSubscriptionEnd(_ end: SubscriptionEnd, extensionID: String) {
        subscriptionTasks.removeValue(forKey: extensionID)
        switch end {
        case .finished:
            views.removeValue(forKey: extensionID)
        case .lost(let reason):
            NSLog("[ExtensionUiClient] View stream lost (%@): %@", extensionID, reason)
        }
    }
}
