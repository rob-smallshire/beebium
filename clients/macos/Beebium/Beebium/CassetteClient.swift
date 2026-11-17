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

import Foundation
import GRPC

/// Error type for a cassette deck key (insert, eject, play, stop, seek, wind,
/// record). The server's `error` is already a full sentence, shown verbatim.
enum CassetteError: LocalizedError {
    case notConnected
    case operationFailed(String, kind: Beebium_CassetteErrorKind)

    var errorDescription: String? {
        switch self {
        case .notConnected:
            return "Not connected to server"
        case .operationFailed(let message, _):
            return message
        }
    }

    /// The server's failure classification, used to choose the short inline
    /// marker label and any recovery control. Unclassified (including a
    /// client-side not-connected) leaves the caller's own generic in place.
    var kind: Beebium_CassetteErrorKind {
        switch self {
        case .notConnected:
            return .unspecified
        case .operationFailed(_, let kind):
            return kind
        }
    }
}

/// Client for the one cassette deck, via gRPC.
///
/// Unlike the floppy drives (which stream discrete events applied
/// incrementally), the deck streams a full `DeckStatus` snapshot on every
/// change -- so this mirrors EconetClient's snapshot-replacing stream, with a
/// generation guard against a superseded stream, while the connect/channel
/// wiring and the Result-returning key wrappers mirror DiscClient.
@MainActor
final class CassetteClient: ObservableObject, Disconnectable {
    /// Whether the machine's model has a cassette interface. The storage
    /// sidebar shows the deck only while this is true.
    @Published private(set) var hasCassetteInterface: Bool = false

    /// Whether a tape is in the deck.
    @Published private(set) var tapeInserted: Bool = false

    /// The loaded tape's metadata (name, format, title, origin, target machine,
    /// duration, recordable), or nil when the deck is empty.
    @Published private(set) var tape: Beebium_TapeMetadata?

    /// The transport the deck is in (stopped, playing, recording, winding).
    @Published private(set) var transport: Beebium_TapeTransport = .stopped

    /// The tape position in seconds of the ULA clock; drives the counter and
    /// the scrubbing slider.
    @Published private(set) var positionSeconds: Double = 0

    /// The machine's motor relay (Serial ULA latch bit 7), independent of the
    /// transport: the tape moves only while the deck plays or records and this
    /// is closed. Shown as deck state; the motor LED is a separate indicator.
    @Published private(set) var motorOn: Bool = false

    /// Whether the first status snapshot has arrived.
    @Published private(set) var isLoaded: Bool = false

    /// The status stream ended for a reason other than our own cancellation, so
    /// the published status is the last the server reported. Cleared by the next
    /// snapshot from a fresh stream, or by `disconnect()`.
    @Published private(set) var isStale: Bool = false

    @Published private(set) var errorMessage: String?

    /// How a status stream ended, reduced to what the client state depends on.
    enum StreamEnd: Equatable {
        /// We cancelled it (disconnect / reconnect), or the server closed it OK.
        case finished
        /// The stream was lost: the server is gone or unreachable.
        case lost(String)
    }

    private var client: Beebium_CassetteServiceNIOClient?
    private var statusStreamCall:
        ServerStreamingCall<Beebium_SubscribeDeckStatusRequest, Beebium_DeckStatus>?

    /// Identifies the current status stream, so the completion of a superseded
    /// stream -- which can arrive after a reconnect has started its successor --
    /// cannot mark the new stream's status stale.
    private(set) var streamGeneration: UInt64 = 0

    /// The deck streams a snapshot no more often than this, in milliseconds of
    /// emulated time, while it plays, records or winds (the server's default).
    private static let statusIntervalMs: UInt32 = 100

    func connect(channel: GRPCChannel) {
        client = Beebium_CassetteServiceNIOClient(channel: channel)
        startStatusStream()
    }

    func disconnect() {
        streamGeneration &+= 1
        statusStreamCall?.cancel(promise: nil)
        statusStreamCall = nil
        client = nil
        isLoaded = false
        isStale = false
        hasCassetteInterface = false
        tapeInserted = false
        tape = nil
        transport = .stopped
        positionSeconds = 0
        motorOn = false
        errorMessage = nil
    }

    // MARK: - Deck keys

    /// Insert a UEF or CSW tape the server can reach. The server judges the
    /// format by content; the client never refuses a file itself.
    func insertTape(url: URL) async -> Result<Beebium_TapeMetadata, CassetteError> {
        guard let client = client else { return .failure(.notConnected) }
        var request = Beebium_InsertTapeRequest()
        // Resolve a dropped Finder alias to its target, as the floppy path does;
        // the server cannot read an alias.
        request.url = url.resolvingDiscImageReference().absoluteString
        do {
            let response = try await client.insertTape(request).response.get()
            if response.success {
                return .success(response.tape)
            }
            return .failure(.operationFailed(response.error, kind: response.kind))
        } catch {
            return .failure(.operationFailed(error.localizedDescription, kind: .unspecified))
        }
    }

    func ejectTape() async -> Result<Void, CassetteError> {
        await call { try await $0.ejectTape(Beebium_EjectTapeRequest()).response.get() }
    }

    func play() async -> Result<Void, CassetteError> {
        await call { try await $0.play(Beebium_PlayRequest()).response.get() }
    }

    func stop() async -> Result<Void, CassetteError> {
        await call { try await $0.stop(Beebium_StopRequest()).response.get() }
    }

    func rewind() async -> Result<Void, CassetteError> {
        await call { try await $0.rewind(Beebium_RewindRequest()).response.get() }
    }

    func fastForward() async -> Result<Void, CassetteError> {
        await call { try await $0.fastForward(Beebium_FastForwardRequest()).response.get() }
    }

    func record() async -> Result<Void, CassetteError> {
        await call { try await $0.record(Beebium_RecordRequest()).response.get() }
    }

    /// Move the tape at once (ends a wind). Back to the beginning is Seek to 0.
    func seek(positionSeconds: Double) async -> Result<Void, CassetteError> {
        var request = Beebium_SeekRequest()
        request.positionSeconds = positionSeconds
        return await call { try await $0.seek(request).response.get() }
    }

    /// Run one of the deck keys that returns a plain CassetteResponse, mapping
    /// its success/sentence/kind to a Result. Keeps the nine wrappers uniform.
    private func call(
        _ body: (Beebium_CassetteServiceNIOClient) async throws -> Beebium_CassetteResponse
    ) async -> Result<Void, CassetteError> {
        guard let client = client else { return .failure(.notConnected) }
        do {
            let response = try await body(client)
            if response.success {
                return .success(())
            }
            return .failure(.operationFailed(response.error, kind: response.kind))
        } catch {
            return .failure(.operationFailed(error.localizedDescription, kind: .unspecified))
        }
    }

    // MARK: - Status stream

    /// Subscribe to the deck's status: a snapshot at once, then on every change
    /// of presence, transport, relay or tape, and the position while it moves.
    private func startStatusStream() {
        guard let client = client else {
            NSLog("[CassetteClient] Status stream not started: not connected")
            return
        }

        streamGeneration &+= 1
        let generation = streamGeneration

        var request = Beebium_SubscribeDeckStatusRequest()
        request.minIntervalMs = Self.statusIntervalMs
        let call = client.subscribeDeckStatus(request) { [weak self] status in
            Task { @MainActor [weak self] in
                self?.handleDeckStatus(status, generation: generation)
            }
        }
        statusStreamCall = call

        // A server-streaming call's status future succeeds with a non-OK code
        // when the server goes away; it only fails for local errors. Both are a
        // loss unless the code says we cancelled the stream ourselves.
        call.status.whenComplete { [weak self] result in
            let end: StreamEnd
            switch result {
            case .success(let status):
                end = (status.code == .ok || status.code == .cancelled)
                    ? .finished
                    : .lost(status.description)
            case .failure(let error):
                end = .lost(error.localizedDescription)
            }
            Task { @MainActor [weak self] in
                self?.handleStreamEnd(end, generation: generation)
            }
        }
    }

    /// Apply a deck-status snapshot from the stream identified by `generation`.
    /// A snapshot from the current stream is by definition fresh, so it clears
    /// staleness; one from a superseded stream is ignored.
    func handleDeckStatus(_ status: Beebium_DeckStatus, generation: UInt64) {
        guard generation == streamGeneration else {
            NSLog("[CassetteClient] Ignoring status from superseded status stream")
            return
        }
        hasCassetteInterface = status.hasCassetteInterface_p
        tapeInserted = status.tapeInserted
        tape = status.tapeInserted ? status.tape : nil
        transport = status.transport
        positionSeconds = status.positionSeconds
        motorOn = status.motorOn
        isLoaded = true
        isStale = false
        errorMessage = nil
    }

    /// Apply the end of the status stream identified by `generation`. A lost
    /// stream leaves the last-known tape in place but marks it stale and clears
    /// the live motion, so nothing reads as a live link to a server that is
    /// gone. The end of a superseded stream is ignored.
    func handleStreamEnd(_ end: StreamEnd, generation: UInt64) {
        guard generation == streamGeneration else {
            NSLog("[CassetteClient] Ignoring end of superseded status stream")
            return
        }
        statusStreamCall = nil
        switch end {
        case .finished:
            break
        case .lost(let reason):
            NSLog("[CassetteClient] Status stream lost: %@", reason)
            isStale = true
            motorOn = false
            transport = .stopped
            errorMessage = "Cassette status stream ended: \(reason)"
        }
    }
}
