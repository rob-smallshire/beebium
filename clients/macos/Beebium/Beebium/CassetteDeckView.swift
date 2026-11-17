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

import AVFoundation
import SwiftUI
import UniformTypeIdentifiers

/// The tape counter, formatted as a real deck's minutes and seconds. Pure, so
/// the rounding and clamping are tested without a view.
enum CassettePosition {
    /// `seconds` as `m:ss` (seconds zero-padded, minutes not): "0:00", "1:05",
    /// "90:00". A negative or non-finite value reads as the start, and the
    /// seconds are floored, as a mechanical counter steps rather than rounds.
    static func format(_ seconds: Double) -> String {
        guard seconds.isFinite, seconds > 0 else { return "0:00" }
        let whole = Int(seconds)
        return String(format: "%d:%02d", whole / 60, whole % 60)
    }
}

/// The deck's status line: the transport in words, carrying the motor relay
/// where its being open is the ordinary waiting state rather than motion.
/// Pure, so the phrasing is tested without a view.
enum CassetteTransportText {
    /// "Stopped", "Playing", "Rewinding", ...; and for play and record, where a
    /// closed relay is what actually moves the tape, "Playing, motor off" /
    /// "Recording, motor off" while the machine has not yet asked for the tape
    /// -- the ordinary waiting state, not a fault (section 9.0). A wind runs
    /// whatever the relay does, so it never carries the motor.
    static func label(transport: Beebium_TapeTransport, motorOn: Bool) -> String {
        switch transport {
        case .stopped: return "Stopped"
        case .playing: return motorOn ? "Playing" : "Playing, motor off"
        case .recording: return motorOn ? "Recording" : "Recording, motor off"
        case .rewinding: return "Rewinding"
        case .fastForwarding: return "Fast forwarding"
        case .UNRECOGNIZED: return "Stopped"
        }
    }
}

/// The one cassette deck, shown at the top of the storage sidebar above the
/// floppy drives when the model has a cassette interface. A transport with a
/// counter and a scrubbing slider, as a real deck is: Play, Stop, Rewind, Fast
/// forward, Back to the beginning (Seek 0), Record and Eject, driven from the
/// deck-status stream (CassetteClient). Insert mirrors a floppy drive (Browse,
/// drag and drop, Recent). No block catalogue, waveform, decoded filenames or
/// tape audio: the deck is a transport, and what is on the tape is the
/// machine's business.
struct CassetteDeckView: View {
    @ObservedObject var cassetteClient: CassetteClient
    /// The tape-specific recent list (kept apart from recent discs).
    @ObservedObject private var recentTapes = RecentTapesManager.shared
    /// See StorageModeView.isServerLocal.
    let isServerLocal: Bool

    @State private var isDropTargeted = false
    @State private var isProcessing = false
    @State private var dropRefusal: DiscDropRefusal?
    @StateObject private var deckError = TransientMessage()
    /// Non-nil while the user drags the position slider: the scrub position the
    /// thumb follows. Nil the rest of the time, when the slider follows the
    /// deck. A seek happens on release, never while the slider moves.
    @State private var scrubSeconds: Double?

    private var tapeInserted: Bool { cassetteClient.tapeInserted }
    private var transport: Beebium_TapeTransport { cassetteClient.transport }
    private var duration: Double { cassetteClient.tape?.durationSeconds ?? 0 }

    /// A live audio input: no length, limited keys, a ring meter in place of a
    /// duration. See CassetteClient.isLive.
    private var isLive: Bool { cassetteClient.isLive }

    /// Whether the running bundle carries the microphone usage string, so the
    /// input-device picker may be offered at all (the string is Rob's to
    /// supply; without it the app must never ask the system for the microphone).
    private var liveInputAvailable: Bool {
        LiveAudioInput.usageStringPresent(in: Bundle.main.infoDictionary)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            header

            if dropRefusal != nil || deckError.kind != nil {
                DriveStatusLine(refusalMessage: dropRefusal?.message, message: deckError)
            } else if tapeInserted {
                loadedContent
            } else {
                emptyContent
            }

            transportControls
        }
        .padding(12)
        .background(isDropTargeted ? Color.accentColor.opacity(0.1) : Color.clear)
        .contentShape(Rectangle())
        // A fresh, acceptable drag makes any lingering error beside the point.
        .onChange(of: isDropTargeted) { targeted in
            if targeted { deckError.clear() }
        }
        // A live stream that was refused or failed shows its sentence here, the
        // way an insert failure does; the client reports it once.
        .onChange(of: cassetteClient.liveError) { message in
            if let message {
                deckError.showFailure("Live input ended", detail: message)
                cassetteClient.clearLiveError()
            }
        }
        .onDrop(of: [.fileURL], delegate: DiscImageDropDelegate(
            isSlotEmpty: !tapeInserted,
            occupiedReason: "Eject tape first",
            isServerLocal: isServerLocal,
            isTargeted: $isDropTargeted,
            refusal: $dropRefusal,
            accept: { url in insertTape(url: url) },
            report: { message in deckError.show(message) }
        ))
    }

    // MARK: - Header

    private var header: some View {
        HStack {
            Text("Cassette")
                .font(.headline)
            Spacer()
            // The motor relay, shown as deck state. The cassette motor LED in
            // the indicator row is the hardware latch; this is the deck's own
            // read of it, as a floppy row shows its motor dot.
            if cassetteClient.motorOn {
                Image(systemName: "circle.fill")
                    .font(.system(size: 6))
                    .foregroundColor(.green)
                    .help("Motor running")
            }
        }
    }

    // MARK: - Content

    private var emptyContent: some View {
        Text(isServerLocal
             ? "Empty - Drop tape image here"
             : "Empty - server is on another host")
            .font(.caption)
            .foregroundColor(.secondary)
            .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var loadedContent: some View {
        VStack(alignment: .leading, spacing: 4) {
            if let tape = cassetteClient.tape, !tape.name.isEmpty {
                Text(tape.name)
                    .font(.subheadline)
                    .fontWeight(.medium)
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .help(tapeHelp(tape))
                    .contextMenu { tapeContextMenu(tape) }
            }

            metadataLine

            Text(CassetteTransportText.label(transport: transport,
                                             motorOn: cassetteClient.motorOn))
                .font(.caption)
                .foregroundColor(.secondary)

            counterAndSlider
            liveMeter
        }
    }

    /// For a live tape: the server's ring fill as a thin bar, and the overrun
    /// and underrun counts beside it (section 9.0's LATITUDE: a small meter and
    /// two numbers). An empty ring is the ordinary priming state, not a fault;
    /// overruns and underruns grow only when the host and emulated clocks drift
    /// apart faster than the ring absorbs.
    @ViewBuilder
    private var liveMeter: some View {
        if isLive {
            VStack(alignment: .leading, spacing: 2) {
                ProgressView(value: min(max(cassetteClient.liveRingFill, 0), 1))
                    .progressViewStyle(.linear)
                    .controlSize(.small)
                Text("Buffer \(Int((min(max(cassetteClient.liveRingFill, 0), 1) * 100).rounded()))%"
                     + "  -  over \(cassetteClient.liveOverruns), under \(cassetteClient.liveUnderruns)")
                    .font(.caption2)
                    .foregroundColor(.secondary)
                    .help("The live input buffer's fill, and the samples dropped (overrun) "
                          + "and made up as silence (underrun)")
            }
        }
    }

    /// Format and, where the server reports them, the image's own title, origin
    /// and target machine -- one muted line, so the tape says what it is without
    /// a catalogue.
    @ViewBuilder
    private var metadataLine: some View {
        if let tape = cassetteClient.tape {
            let parts = [tape.format, tape.title, tape.origin, tape.targetMachine]
                .filter { !$0.isEmpty }
            if !parts.isEmpty {
                Text(parts.joined(separator: " - "))
                    .font(.caption2)
                    .foregroundColor(.secondary)
                    .lineLimit(1)
                    .truncationMode(.tail)
                    .help(parts.joined(separator: " - "))
            }
        }
    }

    /// The counter, and -- when the server reports a duration -- the scrubbing
    /// slider beneath it. With an unknown duration (0) there is no proportional
    /// slider, only the counter, as a real deck with no tape length to show.
    @ViewBuilder
    private var counterAndSlider: some View {
        let position = scrubSeconds ?? cassetteClient.positionSeconds
        HStack {
            Text(CassettePosition.format(position))
                .font(.system(.caption, design: .monospaced))
                .foregroundColor(.secondary)
            if duration > 0 {
                Spacer()
                Text(CassettePosition.format(duration))
                    .font(.system(.caption, design: .monospaced))
                    .foregroundColor(.secondary)
            }
        }

        if duration > 0 {
            Slider(
                value: Binding(
                    get: { min(scrubSeconds ?? cassetteClient.positionSeconds, duration) },
                    set: { scrubSeconds = $0 }
                ),
                in: 0...duration,
                onEditingChanged: { editing in
                    if editing {
                        scrubSeconds = cassetteClient.positionSeconds
                    } else if let target = scrubSeconds {
                        scrubSeconds = nil
                        seek(to: target)
                    }
                }
            )
            .controlSize(.small)
            .disabled(isProcessing)
        }
    }

    private func tapeHelp(_ tape: Beebium_TapeMetadata) -> String {
        tape.url.isEmpty ? tape.name : tape.url
    }

    @ViewBuilder
    private func tapeContextMenu(_ tape: Beebium_TapeMetadata) -> some View {
        let path = tapePath(tape)
        Button {
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString(path, forType: .string)
        } label: {
            Label("Copy Path", systemImage: "doc.on.doc")
        }
        if isServerLocal {
            Button {
                NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: path)])
            } label: {
                Label("Reveal in Finder", systemImage: "folder")
            }
        }
    }

    /// The tape's path on the server's host, from its URL (a file:// URL or a
    /// plain path, as the floppy row resolves its own).
    private func tapePath(_ tape: Beebium_TapeMetadata) -> String {
        if let url = URL(string: tape.url), let scheme = url.scheme, !scheme.isEmpty {
            return url.path
        }
        return tape.url
    }

    // MARK: - Transport controls

    private var transportControls: some View {
        HStack(spacing: 8) {
            if !tapeInserted {
                browseButton
            }
            rewindButton
            backToStartButton
            playButton
            stopButton
            recordButton
            fastForwardButton
            Spacer()
            ejectButton
        }
    }

    private var browseButton: some View {
        // Browsing, Recent and New Blank Tape need a shared filesystem; a live
        // input device is captured on this host and streamed, so it is offered
        // even when the server is on another host. The control is enabled when
        // either source is possible.
        let canBrowse = isServerLocal
        let isEnabled = !tapeInserted && !isProcessing && (canBrowse || liveInputAvailable)
        return Menu {
            if canBrowse {
                Button("Browse...") { browseForTape() }
                recentMenu
                newBlankTapeMenu
            }
            inputDeviceMenu
        } label: {
            Image(systemName: "folder")
                .font(.system(size: 16))
                .foregroundColor(isEnabled ? .primary.opacity(0.6) : .secondary.opacity(0.3))
        } primaryAction: {
            // The one-click action is Browse where it is possible; otherwise the
            // user opens the menu for the Input Device submenu.
            if canBrowse { browseForTape() }
        }
        .menuStyle(.button)
        .buttonStyle(.borderless)
        .fixedSize()
        .disabled(!isEnabled)
        .help(browseHelp)
    }

    private var browseHelp: String {
        if isServerLocal {
            return liveInputAvailable
                ? "Browse for a tape image, or choose a live input device"
                : "Browse for a tape image"
        }
        return liveInputAvailable
            ? "Choose a live input device (the server is on another host, so it cannot open a tape image from this one)"
            : "The server is on another host, so it cannot open a tape image from this one"
    }

    /// The Input Device submenu: one entry per host audio input device, named as
    /// the system names it, with no filtering by kind. Shown only when the
    /// microphone usage string is present (so the app never asks for the
    /// microphone without it). Choosing one asks for authorisation, then opens
    /// the live stream; the deck then shows the live tape, stopped, and the user
    /// presses Play.
    @ViewBuilder
    private var inputDeviceMenu: some View {
        if liveInputAvailable {
            let devices = LiveAudioDevices.inputs()
            Menu("Input Device") {
                ForEach(devices) { device in
                    Button(device.name) { startLiveInput(device) }
                }
            }
            .disabled(devices.isEmpty)
        }
    }

    @ViewBuilder
    private var recentMenu: some View {
        let tapes = recentTapes.tapes
        let labels = RecentTapesManager.menuLabels(for: tapes)
        Menu("Recent") {
            ForEach(Array(zip(tapes, labels)), id: \.0.id) { tape, label in
                Button(label) {
                    insertTape(url: URL(fileURLWithPath: tape.filepath))
                }
                .disabled(!FileManager.default.fileExists(atPath: tape.filepath))
            }
            Divider()
            Button("Clear Recent Tapes") { recentTapes.clear() }
        }
        .disabled(tapes.isEmpty)
    }

    /// The New Blank Tape submenu: one entry per shipped template, labelled by
    /// the server (the label already carries the length, "Blank C15 (15
    /// minutes)"). Choosing one saves a copy to a path the user picks and
    /// inserts it, stopped at the start, ready to Record. The labels come from
    /// the server (nothing hard-coded), so the submenu is disabled when the
    /// server offers none.
    @ViewBuilder
    private var newBlankTapeMenu: some View {
        let blanks = cassetteClient.blankTapes
        Menu("New Blank Tape") {
            ForEach(blanks, id: \.label) { tape in
                Button(tape.label) { newBlankTape(tape) }
            }
        }
        .disabled(blanks.isEmpty)
    }

    private var rewindButton: some View {
        transportKey("backward.fill", help: isLive ? "A live input cannot be wound" : "Rewind",
                     enabled: tapeInserted && !isLive && transport != .rewinding) {
            run { await cassetteClient.rewind() }
        }
    }

    private var fastForwardButton: some View {
        transportKey("forward.fill", help: isLive ? "A live input cannot be wound" : "Fast forward",
                     enabled: tapeInserted && !isLive && transport != .fastForwarding) {
            run { await cassetteClient.fastForward() }
        }
    }

    private var backToStartButton: some View {
        transportKey("backward.end.fill",
                     help: isLive ? "A live input cannot be sought" : "Back to the beginning",
                     enabled: tapeInserted && !isLive && cassetteClient.positionSeconds > 0) {
            seek(to: 0)
        }
    }

    private var playButton: some View {
        transportKey("play.fill", help: "Play", enabled: tapeInserted && transport != .playing) {
            run { await cassetteClient.play() }
        }
    }

    private var stopButton: some View {
        transportKey("stop.fill", help: "Stop", enabled: tapeInserted && transport != .stopped) {
            run { await cassetteClient.stop() }
        }
    }

    /// The red Record key. Disabled with an explanation for a tape that is not
    /// recordable (a CSW): recording stays the user's deliberate choice, and the
    /// disabled key says why rather than letting the server refuse after a press.
    private var recordButton: some View {
        let recordable = cassetteClient.tape?.recordable ?? false
        let enabled = tapeInserted && !isLive && recordable && transport != .recording && !isProcessing
        return Button {
            run { await cassetteClient.record() }
        } label: {
            Image(systemName: "record.circle.fill")
                .font(.system(size: 14))
                .foregroundColor(enabled ? .red : .secondary.opacity(0.4))
        }
        .buttonStyle(.borderless)
        .disabled(!enabled)
        .help(recordHelp(recordable: recordable))
    }

    private func recordHelp(recordable: Bool) -> String {
        if !tapeInserted { return "Insert a tape to record" }
        if isLive { return "A live input cannot be recorded onto" }
        if !recordable {
            return "This tape is a CSW image, and Beebium records only on UEF tapes"
        }
        return "Record"
    }

    private var ejectButton: some View {
        Button {
            // Ejecting a live tape stops the stream: the server plays out what
            // its ring holds and then ejects (section 9.0), rather than cutting
            // it off at once. A file tape ejects straight away.
            if isLive {
                cassetteClient.stopLiveInput()
            } else {
                run { await cassetteClient.ejectTape() }
            }
        } label: {
            Image(systemName: "eject.fill")
                .font(.system(size: 14))
        }
        .buttonStyle(.bordered)
        .controlSize(.regular)
        .disabled(!tapeInserted || isProcessing)
        .help(isLive ? "Stop the live input" : "Eject tape")
    }

    /// One transport key: a borderless glyph button, dimmed when disabled.
    private func transportKey(_ symbol: String, help: String, enabled: Bool,
                              action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: 14))
                .foregroundColor(enabled ? .primary.opacity(0.7) : .secondary.opacity(0.3))
        }
        .buttonStyle(.borderless)
        .disabled(!enabled || isProcessing)
        .help(help)
    }

    // MARK: - Actions

    private func browseForTape() {
        Task { @MainActor in
            let panel = NSOpenPanel()
            panel.title = "Select Tape Image"
            // Filtering by the tape extensions is a convenience only; the
            // server still judges the format by content, so a mislabelled file
            // is accepted by the panel and refused by the server (section 9.0).
            panel.allowedContentTypes = ["uef", "csw", "wav"]
                .compactMap { UTType(filenameExtension: $0) }
            panel.allowsMultipleSelection = false
            panel.canChooseDirectories = false
            if panel.runModal() == .OK, let url = panel.url {
                insertTape(url: url)
            }
        }
    }

    /// Make a blank tape from a shipped template: suggest the template's own
    /// file name in a save panel (keeping its .uef extension), then copy and
    /// insert. Picking the path on this host is why the menu is gated on a local
    /// server. Create and insert are distinct, and the created tape is inserted
    /// stopped, never recording -- recording stays the user's choice.
    private func newBlankTape(_ tape: Beebium_BlankTape) {
        Task { @MainActor in
            let panel = NSSavePanel()
            panel.title = "New Blank Tape"
            panel.nameFieldStringValue = tape.filename
            let templateExtension = (tape.filename as NSString).pathExtension
            if let type = UTType(filenameExtension: templateExtension) {
                panel.allowedContentTypes = [type]
            }
            guard panel.runModal() == .OK, let url = panel.url else { return }
            // Belt and braces: if the returned name still does not carry the
            // template's extension, add it; the server refuses a wrong one.
            let destination = BlankDiscNaming.url(forChosen: url, templateFilename: tape.filename)
            createTape(label: tape.label, destination: destination)
        }
    }

    /// Copy the template to `destination` and, on success, insert it -- which
    /// also records it in Recent. The save panel has already confirmed any
    /// replacement, so overwrite is passed true. A failure takes the deck's
    /// marker and popover, the server's lower-case fragment completed into a
    /// sentence as the disc creation does.
    private func createTape(label: String, destination: URL) {
        dropRefusal = nil
        deckError.clear()
        isProcessing = true
        Task {
            let result = await cassetteClient.createTape(
                label: label, destinationPath: destination.path, overwrite: true)
            await MainActor.run {
                switch result {
                case .success(let createdPath):
                    isProcessing = false
                    insertTape(url: URL(fileURLWithPath: createdPath))
                case .failure(let error):
                    isProcessing = false
                    NSLog("[CassetteDeckView] Create tape failed: \(error.localizedDescription)")
                    deckError.showFailure(
                        error.kind.markerLabel ?? "Couldn't create tape",
                        detail: SaveClient.sentence(error.kind.sentenceLead,
                                                    detail: error.localizedDescription))
                }
            }
        }
    }

    private func insertTape(url: URL) {
        guard !tapeInserted else {
            deckError.show("Eject tape first")
            return
        }
        dropRefusal = nil
        deckError.clear()
        isProcessing = true
        Task {
            let result = await cassetteClient.insertTape(url: url)
            await MainActor.run {
                isProcessing = false
                switch result {
                case .success:
                    recentTapes.record(filepath: url.path)
                case .failure(let error):
                    NSLog("[CassetteDeckView] Insert failed: \(error.localizedDescription)")
                    deckError.showFailure(error.kind.markerLabel ?? "Insert failed",
                                          detail: error.localizedDescription)
                }
            }
        }
    }

    private func seek(to seconds: Double) {
        run { await cassetteClient.seek(positionSeconds: seconds) }
    }

    /// Ask for microphone authorisation, then open the live stream for `device`.
    /// The menu is shown only when the usage string is present, so asking here
    /// is safe. A denied request or a start failure takes the deck's popover,
    /// the way an insert failure does.
    private func startLiveInput(_ device: LiveAudioDevice) {
        dropRefusal = nil
        deckError.clear()
        isProcessing = true
        Task {
            let granted = await AVCaptureDevice.requestAccess(for: .audio)
            await MainActor.run {
                isProcessing = false
                guard granted else {
                    deckError.showFailure(
                        "Microphone access denied",
                        detail: "Beebium needs permission to use the microphone to stream a live "
                            + "tape. Grant it in System Settings, under Privacy & Security, Microphone.")
                    return
                }
                if case .failure(let error) = cassetteClient.startLiveInput(device: device) {
                    NSLog("[CassetteDeckView] Live input failed: \(error.localizedDescription)")
                    deckError.showFailure(error.kind.markerLabel ?? "Live input failed",
                                          detail: error.localizedDescription)
                }
            }
        }
    }

    /// Run a deck key and surface the server's sentence on refusal. The server's
    /// message is already a full sentence, so it is shown verbatim (section 3.4).
    private func run(_ operation: @escaping () async -> Result<Void, CassetteError>) {
        deckError.clear()
        isProcessing = true
        Task {
            let result = await operation()
            await MainActor.run {
                isProcessing = false
                if case .failure(let error) = result {
                    NSLog("[CassetteDeckView] Deck key failed: \(error.localizedDescription)")
                    deckError.showFailure(error.kind.markerLabel ?? "Cassette error",
                                          detail: error.localizedDescription)
                }
            }
        }
    }
}
