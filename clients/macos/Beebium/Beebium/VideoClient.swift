// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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
import NIO
import SwiftProtobuf

/// Connection state for the video client
enum ConnectionState: Equatable {
    case disconnected
    case connecting
    case connected
    case error(String)
}

/// Client for streaming video frames from beebium-server via gRPC
@MainActor
final class VideoClient: ObservableObject, Disconnectable {
    /// Current connection state
    @Published private(set) var connectionState: ConnectionState = .disconnected

    // The per-frame state below is deliberately NOT @Published.
    //
    // Frames arrive fifty times a second and go straight to the renderer, which
    // is the whole point of holding a MetalRenderer here. Publishing anything
    // that changes per frame fires objectWillChange at that rate, and every
    // view observing this client -- ContentView, and so the entire window tree
    // and every focused value it exports -- is invalidated with it, whether or
    // not it reads what changed. SwiftUI must hear about connection state,
    // which changes when something actually happens; it must not hear about
    // frames.

    /// Frame dimensions (logical pixels)
    private(set) var frameWidth: Int = 736
    private(set) var frameHeight: Int = 576

    /// Display dimensions (target after scaling)
    private(set) var displayWidth: Int = 736
    private(set) var displayHeight: Int = 576

    /// Frame counter for debugging
    private(set) var frameCount: UInt64 = 0

    /// A single event loop group shared by every VideoClient and reused across
    /// reconnects. It is intentionally created once and never shut down.
    ///
    /// Shutting an event loop group down while gRPC-Swift still has a
    /// reconnection scheduled on it trips a SwiftNIO precondition
    /// (EventLoopFuture.deinit) and aborts the app in debug builds. That state
    /// arises whenever the server vanishes mid-stream (e.g. it crashes or is
    /// killed) and the window is then closed: GRPCChannelPool.close() does not
    /// reliably cancel the pending reconnect timer, so a subsequent
    /// syncShutdownGracefully() destroys an unresolved future. An event loop
    /// group is meant to be long-lived; keeping one for the process lifetime
    /// (one thread, reclaimed at exit) sidesteps the teardown race entirely.
    private static let sharedGroup: EventLoopGroup =
        MultiThreadedEventLoopGroup(numberOfThreads: 1)

    private var _channel: GRPCChannel?
    private var streamTask: Task<Void, Never>?

    private var host: String
    private var port: Int

    /// Current connection target
    @Published private(set) var target: ConnectionTarget

    /// Metal renderer for direct frame updates (bypasses SwiftUI update batching)
    weak var renderer: MetalRenderer?

    /// When true, frames stop reaching the renderer so the last one stays on
    /// screen while a selection is drawn over it. The stream keeps arriving and
    /// the emulator keeps running; only the display is held still. Set through
    /// the DisplayFreezer conformance, which the selection coordinator drives.
    private var isDisplayFrozen = false

    /// Expose the gRPC channel for other clients (e.g., KeyboardClient) to share
    var channel: GRPCChannel? { _channel }

    init(target: ConnectionTarget = .localhost) {
        self.target = target
        self.host = target.host
        self.port = target.port
    }

    /// Hold the screen as it stands, so every read a selection makes describes
    /// one still rather than whatever the machine has drawn since.
    ///
    /// The emulator keeps running -- a hold is a copy, not a pause -- and the
    /// grid comes back with it, so this costs no more round trips than fetching
    /// the geometry alone used to. Asking for the frame gets the captured still
    /// as well, which the client shows so the picture and the reading are the
    /// same frame by construction.
    func holdScreen(includeFrame: Bool) async -> ScreenHold? {
        guard let channel = _channel else { return nil }
        let client = Beebium_VideoServiceClient(channel: channel)

        var request = Beebium_HoldScreenRequest()
        request.includeFrame = includeFrame

        do {
            let hold = try await client.holdScreen(request).response.get()
            let bands = hold.geometry.bands.map { band in
                ScreenBandGeometry(
                    top: Int(band.top),
                    bottom: Int(band.bottom),
                    cellWidth: Int(band.cellWidth),
                    cellHeight: Int(band.cellHeight),
                    columnPitch: Int(band.columnPitch),
                    rowPitch: Int(band.rowPitch),
                    originX: Int(band.originX),
                    originY: Int(band.originY)
                )
            }
            var still: HeldFrame?
            if hold.hasFrame {
                let frame = hold.frame
                still = HeldFrame(
                    pixels: frame.pixels,
                    width: Int(frame.width),
                    height: Int(frame.height),
                    displayWidth: Int(frame.displayWidth),
                    displayHeight: Int(frame.displayHeight),
                    leftBorder: Int(frame.leftBorder),
                    rightBorder: Int(frame.rightBorder),
                    topBorder: Int(frame.topBorder),
                    bottomBorder: Int(frame.bottomBorder),
                    interlaced: frame.fieldOrder != .progressive,
                    regions: frame.regions.map { region in
                        DisplayRegion(
                            startLine: Int(region.startLine),
                            endLine: Int(region.endLine),
                            pixelWidth: Int(region.pixelWidth))
                    })
            }
            return ScreenHold(holdID: hold.holdID, bands: bands, frame: still)
        } catch {
            print("[VideoClient] holdScreen failed: \(error)")
            return nil
        }
    }

    /// Let a held screen go. Holds expire on their own, so a lost release is
    /// survivable; saying so returns the memory at once.
    func releaseScreen(_ holdID: UInt64) async {
        guard let channel = _channel else { return }
        let client = Beebium_VideoServiceClient(channel: channel)
        var request = Beebium_ReleaseScreenRequest()
        request.holdID = holdID
        do {
            _ = try await client.releaseScreen(request).response.get()
        } catch {
            print("[VideoClient] releaseScreen failed: \(error)")
        }
    }

    /// Read the character grid the display currently implies, per band, in
    /// frame pixel coordinates.
    ///
    /// Fetched once when a selection gesture begins: the frozen frame's grid is
    /// fixed for the selection's life, and snapping a drag needs the cell
    /// boundaries before there is any text to ask for. Returns an empty array
    /// when the display has no band to report or the call fails.
    func screenGeometry(holdID: UInt64? = nil) async -> [ScreenBandGeometry] {
        guard let channel = _channel else { return [] }
        let client = Beebium_VideoServiceClient(channel: channel)
        var request = Beebium_GetScreenGeometryRequest()
        if let holdID { request.holdID = holdID }
        do {
            let geometry = try await client.getScreenGeometry(request).response.get()
            return geometry.bands.map { band in
                ScreenBandGeometry(
                    top: Int(band.top),
                    bottom: Int(band.bottom),
                    cellWidth: Int(band.cellWidth),
                    cellHeight: Int(band.cellHeight),
                    columnPitch: Int(band.columnPitch),
                    rowPitch: Int(band.rowPitch),
                    originX: Int(band.originX),
                    originY: Int(band.originY)
                )
            }
        } catch {
            print("[VideoClient] getScreenGeometry failed: \(error)")
            return []
        }
    }

    /// Read the text on the display, whatever mode the machine is in.
    ///
    /// `region` is in frame pixel coordinates; nil reads the whole display. The
    /// server picks the strategy per band of scanlines, so the client never
    /// learns the mode -- it gets back the runs it found and the cell geometry
    /// they were read with. Returns nil only when the call fails; a readable
    /// screen with no text comes back supported with no runs.
    func screenText(region: FramePixelRect?,
                    search: ScreenTextSearchMode,
                    layout: ScreenTextJoinLayout,
                    characters: ScreenTextCharactersMode,
                    holdID: UInt64? = nil) async -> ScreenTextReading? {
        guard let channel = _channel else { return nil }
        let client = Beebium_VideoServiceClient(channel: channel)

        var request = Beebium_GetScreenTextRequest()
        request.search = search.proto
        request.layout = layout.proto
        request.characters = characters.proto
        if let holdID { request.holdID = holdID }
        if let region {
            var pixelRegion = Beebium_PixelRegion()
            pixelRegion.x = UInt32(max(0, region.x))
            pixelRegion.y = UInt32(max(0, region.y))
            pixelRegion.width = UInt32(max(0, region.width))
            pixelRegion.height = UInt32(max(0, region.height))
            request.region = pixelRegion
        }

        do {
            let screen = try await client.getScreenText(request).response.get()
            return ScreenTextReading(
                supported: screen.supported,
                text: screen.text,
                unreadableCells: Int(screen.unreadableCells),
                ambiguousCells: Int(screen.ambiguousCells),
                runs: screen.runs.map { run in
                    ScreenTextRun(
                        text: run.text,
                        bounds: FramePixelRect(
                            x: Int(run.bounds.x), y: Int(run.bounds.y),
                            width: Int(run.bounds.width), height: Int(run.bounds.height)),
                        cellWidth: Int(run.cellWidth),
                        cellHeight: Int(run.cellHeight),
                        cells: run.cells.map { cell in
                            ScreenTextRunCell(
                                bounds: FramePixelRect(
                                    x: Int(cell.bounds.x), y: Int(cell.bounds.y),
                                    width: Int(cell.bounds.width),
                                    height: Int(cell.bounds.height)),
                                matched: cell.matched)
                        }
                    )
                }
            )
        } catch {
            print("[VideoClient] getScreenText failed: \(error)")
            return nil
        }
    }

    /// Freeze or resume forwarding frames to the renderer. See `isDisplayFrozen`.
    func setDisplayFrozen(_ frozen: Bool) {
        isDisplayFrozen = frozen
    }

    /// Reconnect to a different target
    func reconnect(to newTarget: ConnectionTarget) {
        disconnect()
        target = newTarget
        host = newTarget.host
        port = newTarget.port
        connect()
    }

    /// Connect to the beebium-server and start streaming frames
    func connect() {
        guard connectionState == .disconnected || connectionState != .connecting else { return }

        connectionState = .connecting

        streamTask = Task { [weak self] in
            await self?.runConnection()
        }
    }

    /// Disconnect from the server
    func disconnect() {
        streamTask?.cancel()
        streamTask = nil

        let channelToClose = _channel
        _channel = nil
        connectionState = .disconnected

        // Close the channel (best effort) on a background thread so a vanished
        // server cannot block the UI. The shared event loop group is left
        // running on purpose -- see sharedGroup for why we never shut it down.
        if let channel = channelToClose {
            DispatchQueue.global().async {
                try? channel.close().wait()
            }
        }
    }

    private func runConnection() async {
        do {
            let grpcChannel = try GRPCChannelPool.with(
                target: .host(host, port: port),
                transportSecurity: .plaintext,
                eventLoopGroup: Self.sharedGroup
            )

            await MainActor.run {
                self._channel = grpcChannel
            }

            // Get video config first
            let client = Beebium_VideoServiceNIOClient(channel: grpcChannel)

            do {
                let config = try await client.getConfig(Beebium_GetConfigRequest()).response.get()
                await MainActor.run {
                    self.frameWidth = Int(config.width)
                    self.frameHeight = Int(config.height)
                    self.connectionState = .connected
                }
            } catch {
                await MainActor.run {
                    self.connectionState = .error("Failed to get config: \(error.localizedDescription)")
                }
                return
            }

            // Start streaming frames
            let call = client.subscribeFrames(Beebium_SubscribeFramesRequest()) { [weak self] frame in
                Task { @MainActor [weak self] in
                    self?.handleFrame(frame)
                }
            }

            // Wait for stream to complete or be cancelled
            do {
                _ = try await call.status.get()
            } catch {
                await MainActor.run {
                    if case .connecting = self.connectionState {
                        self.connectionState = .error("Stream ended: \(error.localizedDescription)")
                    } else if case .connected = self.connectionState {
                        self.connectionState = .error("Connection lost: \(error.localizedDescription)")
                    }
                }
            }

        } catch {
            await MainActor.run {
                self.connectionState = .error("Connection failed: \(error.localizedDescription)")
            }
        }
    }

    private func handleFrame(_ frame: Beebium_Frame) {
        frameCount = frame.frameNumber
        frameWidth = Int(frame.width)
        frameHeight = Int(frame.height)
        displayWidth = Int(frame.displayWidth)
        displayHeight = Int(frame.displayHeight)

        // Hold the displayed frame still while a selection is live. The frame
        // keeps arriving -- the counters above stay current -- but the renderer
        // is not handed the new pixels, so the frozen still remains on screen
        // and the selection maps to the geometry the user is looking at.
        if isDisplayFrozen { return }

        // Determine interlace state from field_order
        // PROGRESSIVE = non-interlaced (bitmap modes), EVEN_FIRST/ODD_FIRST = interlaced (MODE 7)
        let isInterlaced = frame.fieldOrder != .progressive

        // Extract display regions for split-screen modes
        let regions: [DisplayRegion] = frame.regions.map { region in
            DisplayRegion(
                startLine: Int(region.startLine),
                endLine: Int(region.endLine),
                pixelWidth: Int(region.pixelWidth)
            )
        }

        // Update renderer directly to bypass SwiftUI update batching
        renderer?.updateFrame(
            data: frame.pixels,
            width: Int(frame.width),
            height: Int(frame.height),
            displayWidth: Int(frame.displayWidth),
            displayHeight: Int(frame.displayHeight),
            leftBorder: Int(frame.leftBorder),
            rightBorder: Int(frame.rightBorder),
            topBorder: Int(frame.topBorder),
            bottomBorder: Int(frame.bottomBorder),
            interlaced: isInterlaced,
            regions: regions
        )
    }
}
