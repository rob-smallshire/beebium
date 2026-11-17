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

/// Error type for creating a blank tape from a shipped template.
enum CreateTapeError: LocalizedError {
    case notConnected
    case operationFailed(String, kind: Beebium_CreateTapeErrorKind)

    var errorDescription: String? {
        switch self {
        case .notConnected:
            return "Not connected to server"
        case .operationFailed(let message, _):
            return message
        }
    }

    /// The server's failure classification, used to choose the short inline
    /// marker and the user-facing sentence. Unclassified (including a
    /// client-side not-connected) leaves the caller's own generic in place.
    var kind: Beebium_CreateTapeErrorKind {
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
    /// the scrubbing timeline.
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

    /// A live stream's refusal or failure, as a sentence (the server's verbatim
    /// when it has one), for the deck to surface. Nil when there is nothing to
    /// show. A lost connection is left to the status stream's staleness, so this
    /// carries only a genuine stream error.
    @Published private(set) var liveError: String?

    /// For a live tape (an audio input streamed from this host): how full the
    /// server's ring is, from 0 to 1, and the samples it has dropped on overrun
    /// and made up as silence on underrun. Zero and meaningless for a
    /// file-backed tape; shown as the deck's small live meter (section 9.0).
    @Published private(set) var liveRingFill: Double = 0
    @Published private(set) var liveOverruns: UInt64 = 0
    @Published private(set) var liveUnderruns: UInt64 = 0

    /// Whether the loaded tape is a live audio input (its format reads "LIVE"),
    /// rather than a file-backed UEF, CSW or WAV. A live tape has no length, so
    /// its Seek, Rewind, Fast forward and Record keys are disabled (the server
    /// refuses them with LIVE_SOURCE), and its ring state is shown in place of a
    /// duration.
    var isLive: Bool { tapeInserted && tape?.format == "LIVE" }

    /// The blank tape templates the server ships, in file-name order, behind the
    /// Browse icon's New Blank Tape submenu. Fetched once on connect; nothing is
    /// hard-coded, so the submenu is simply empty (and disabled) if the server
    /// has none or the call fails.
    @Published private(set) var blankTapes: [Beebium_BlankTape] = []

    /// The loaded tape's signal map: its total length in seconds and one level
    /// byte per second (0 silent .. 255 signal throughout), for colouring the
    /// position timeline. Empty until the server reports the map ready and it has
    /// been fetched; dropped on eject and disconnect. A live tape has none.
    @Published private(set) var signalMapSeconds: Int = 0
    @Published private(set) var signalMapLevels: [UInt8] = []

    /// The signal-map generation last fetched (or begun fetching), so the map is
    /// fetched once when ready and again only when the generation changes (after
    /// a recording) -- never polled. Nil when there is no current map.
    private var signalMapGeneration: UInt32?

    /// Why a seek to the tape's append point would be refused, as a sentence
    /// (the server's verbatim), or empty when it would be accepted. Greys the
    /// deck's To the end of the recordings key and gives its help, as
    /// `TapeMetadata.recordRefusal` does for Record.
    @Published private(set) var appendRefusal: String = ""

    /// How a status stream ended, reduced to what the client state depends on.
    enum StreamEnd: Equatable {
        /// We cancelled it (disconnect / reconnect), or the server closed it OK.
        case finished
        /// The stream was lost: the server is gone or unreachable.
        case lost(String)
    }

    private var client: Beebium_CassetteServiceNIOClient?

    /// The live audio capture while a host input is streaming, else nil. Held
    /// until the stream ends (its completion clears it), so a graceful close can
    /// play out before the capture is released.
    private var liveCapture: LiveAudioCapture?

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
        fetchBlankTapes()
    }

    func disconnect() {
        streamGeneration &+= 1
        liveCapture?.cancel()
        liveCapture = nil
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
        liveRingFill = 0
        liveOverruns = 0
        liveUnderruns = 0
        errorMessage = nil
        liveError = nil
        blankTapes = []
        signalMapGeneration = nil
        signalMapSeconds = 0
        signalMapLevels = []
        appendRefusal = ""
    }

    // MARK: - Live input

    /// Stream a host input device to the deck as the live tape. The capture runs
    /// until `stopLiveInput()` (a graceful close, after which the server plays
    /// out its ring and ejects) or the connection is lost. The caller has
    /// already obtained microphone authorisation.
    func startLiveInput(device: LiveAudioDevice) -> Result<Void, CassetteError> {
        guard let client = client else { return .failure(.notConnected) }
        liveCapture?.cancel()
        liveError = nil
        let capture = LiveAudioCapture()
        do {
            try capture.start(device: device, client: client) { [weak self] result in
                Task { @MainActor [weak self] in self?.liveInputEnded(result) }
            }
            liveCapture = capture
            return .success(())
        } catch {
            liveCapture = nil
            return .failure(.operationFailed(error.localizedDescription, kind: .unspecified))
        }
    }

    /// Half-close the live stream: stop capturing and let the server play out
    /// its ring and eject the live tape. Does nothing when no live stream runs.
    func stopLiveInput() {
        liveCapture?.stop()
    }

    func clearLiveError() {
        liveError = nil
    }

    /// Release the live capture when its stream ends, and surface a genuine
    /// refusal or failure. A lost connection is left to the status stream's own
    /// staleness, so it is not doubled as a live error.
    private func liveInputEnded(_ result: Result<Beebium_StreamAudioSummary, Error>) {
        liveCapture = nil
        switch result {
        case .success(let summary):
            if !summary.error.isEmpty {
                NSLog("[CassetteClient] Live stream refused: %@", summary.error)
                liveError = summary.error
            }
        case .failure(let error):
            NSLog("[CassetteClient] Live stream failed: %@", error.localizedDescription)
            liveError = isStale ? nil : error.localizedDescription
        }
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

    /// Seek to the tape's append point -- the end of its recordings -- so a new
    /// recording follows them rather than overwriting. The server refuses with a
    /// sentence on a live tape or while the signal map is not ready; the deck
    /// greys the key from `appendRefusal` first, so this is the backstop. The new
    /// head position and an at-the-end stop arrive through the status stream.
    func seekToAppendPoint() async -> Result<Void, CassetteError> {
        guard let client = client else { return .failure(.notConnected) }
        do {
            let response = try await client.seekToAppendPoint(
                Beebium_SeekToAppendPointRequest()).response.get()
            if response.success {
                return .success(())
            }
            return .failure(.operationFailed(response.error, kind: response.kind))
        } catch {
            return .failure(.operationFailed(error.localizedDescription, kind: .unspecified))
        }
    }

    /// Create a blank tape from a shipped template, copying it to a path on the
    /// server's host. `overwrite` is for when the chosen path already exists;
    /// the caller passes it true once a save panel has confirmed replacement.
    /// Returns the absolute path of the created tape, ready for insertTape.
    func createTape(label: String, destinationPath: String, overwrite: Bool)
        async -> Result<String, CreateTapeError> {
        guard let client = client else { return .failure(.notConnected) }
        var request = Beebium_CreateTapeRequest()
        request.label = label
        request.destinationPath = destinationPath
        request.overwrite = overwrite
        do {
            let response = try await client.createTape(request).response.get()
            if response.success {
                return .success(response.path)
            }
            return .failure(.operationFailed(response.error, kind: response.kind))
        } catch {
            return .failure(.operationFailed(error.localizedDescription, kind: .unspecified))
        }
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

    /// Fetch the shipped blank tape templates once. A failure here is not fatal
    /// to the deck view -- the New Blank Tape submenu is simply left empty.
    private func fetchBlankTapes() {
        guard let client = client else { return }
        Task { [weak self] in
            do {
                let response = try await client.listBlankTapes(Beebium_ListBlankTapesRequest())
                    .response.get()
                await MainActor.run { self?.blankTapes = response.tapes }
            } catch {
                NSLog("[CassetteClient] ListBlankTapes failed: \(error.localizedDescription)")
                await MainActor.run { self?.blankTapes = [] }
            }
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
        liveRingFill = status.liveRingFill
        liveOverruns = status.liveOverruns
        liveUnderruns = status.liveUnderruns
        appendRefusal = status.appendRefusal
        isLoaded = true
        isStale = false
        errorMessage = nil
        reconcileSignalMap(status, streamGeneration: generation)
    }

    // MARK: - Signal map

    /// Keep the tape's signal map in step with the deck status: fetch it once the
    /// server reports it ready, fetch it again when the generation changes (after
    /// a recording), and drop it when the deck empties or the map is not ready.
    /// Never polls -- a fetch is launched only on a change of ready generation.
    private func reconcileSignalMap(_ status: Beebium_DeckStatus, streamGeneration: UInt64) {
        guard status.tapeInserted, status.signalMapReady else {
            dropSignalMap()
            return
        }
        guard signalMapGeneration != status.signalMapGeneration else { return }
        // A new (or first) ready generation: fetch it. The previous map is kept
        // on screen until the new levels arrive and replace it, so a map that
        // republishes every second while recording colours in the recording
        // head's wake without a plain-bar flash between fetches.
        signalMapGeneration = status.signalMapGeneration
        fetchSignalMap(streamGeneration: streamGeneration)
    }

    private func dropSignalMap() {
        signalMapGeneration = nil
        if signalMapSeconds != 0 || !signalMapLevels.isEmpty {
            signalMapSeconds = 0
            signalMapLevels = []
        }
    }

    /// Fetch the whole signal map once for the current ready generation. A
    /// failure leaves the plain bar and is not retried (never poll); a reply that
    /// a reconnect or a newer generation has overtaken is discarded.
    private func fetchSignalMap(streamGeneration: UInt64) {
        guard let client = client else { return }
        let wantedGeneration = signalMapGeneration
        Task { [weak self] in
            do {
                let map = try await client.getTapeSignalMap(Beebium_GetTapeSignalMapRequest())
                    .response.get()
                await MainActor.run {
                    guard let self = self,
                          streamGeneration == self.streamGeneration,
                          wantedGeneration == self.signalMapGeneration else { return }
                    if map.success && map.ready {
                        self.signalMapSeconds = Int(map.seconds)
                        self.signalMapLevels = [UInt8](map.levels)
                    } else {
                        self.signalMapSeconds = 0
                        self.signalMapLevels = []
                    }
                }
            } catch {
                NSLog("[CassetteClient] GetTapeSignalMap failed: \(error.localizedDescription)")
            }
        }
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
