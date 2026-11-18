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

/// The geometry of the position timeline: where the pointer sits for a given
/// position, and which position a drag points to. Pure, so the clamping and the
/// unknown-duration case are tested without a view. A tape has no played and
/// unplayed portion, so this maps a position to a point on a plain bar, not a
/// fill.
enum TapeTimelineGeometry {
    /// The pointer's place along the bar as a fraction 0...1 of a known
    /// duration. An unknown or non-positive duration, or a non-finite or
    /// negative position, reads as the start; past the end clamps to the end.
    static func fraction(position: Double, duration: Double) -> Double {
        guard duration > 0, position.isFinite, position > 0 else { return 0 }
        return min(position / duration, 1)
    }

    /// The tape position a drag to `x` on a bar of `width` points to, clamped to
    /// the tape. A non-positive width or duration reads as the start.
    static func position(atX x: Double, width: Double, duration: Double) -> Double {
        guard width > 0, duration > 0 else { return 0 }
        return min(max(x / width, 0), 1) * duration
    }
}

/// Reduces the server's per-second signal map to the timeline's pixel columns.
/// The map is one level byte per second of tape (0 silent .. 255 signal
/// throughout); each pixel column spans the seconds that fall under it and is
/// lit -- drawn in the accent colour -- when the loudest of them reaches the
/// threshold, so a single loud second still lights its column. Pure, so the
/// reduction is tested without a view.
enum CassetteSignalMap {
    /// A second at or above this 0..255 level is drawn in the accent colour;
    /// below it, the bar's plain grey.
    static let accentThreshold: UInt8 = 128

    /// One Bool per pixel column across `width`: true where the tape's signal
    /// reaches the threshold somewhere in the seconds that column spans. An
    /// absent or empty map, no seconds, or a non-positive width yields no columns
    /// -- the caller then draws the plain bar.
    static func litColumns(seconds: Int, levels: [UInt8], width: Int) -> [Bool] {
        let count = min(seconds, levels.count)
        guard width > 0, count > 0 else { return [] }
        return (0..<width).map { column in
            let start = column * count / width
            let end = max(start + 1, (column + 1) * count / width)
            var peak: UInt8 = 0
            for second in start..<min(end, count) {
                peak = max(peak, levels[second])
            }
            return peak >= accentThreshold
        }
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

/// A transport key on the deck row.
enum CassetteKey: CaseIterable {
    case rewind, backToStart, play, stop, record, fastForward, toEnd
}

/// A transport key's colour. Colour means the tape is moving: a key is coloured
/// only while down and only while something turns the tape -- the motor relay
/// closed for Play and Record, a wind for Rewind and Fast forward. A down key
/// that is waiting (relay open) is grey; a key that is not down is never
/// coloured.
enum CassetteKeyColour: Equatable {
    case none
    case grey
    case green
    case red
    case blue
}

/// How a transport key is drawn: whether it accepts a press, whether it is down
/// (the lit status key), and its colour.
struct CassetteKeyAppearance: Equatable {
    var enabled: Bool
    var depressed: Bool
    var colour: CassetteKeyColour
}

/// What the deck shows depends on whether a tape (file or live) is in. An empty
/// deck shows only the ways to fill it -- the browse control and the drop target
/// -- with no transport key row, counter or timeline, so nothing greyed sits
/// waiting to come alive when a tape goes in (section 9.0). Pure, so the rule is
/// tested without a view.
enum CassetteDeckLayout {
    /// Whether the transport key row, the counter and the position timeline are
    /// shown. They appear only with a tape in; an empty deck omits them.
    static func showsTransport(tapeInserted: Bool) -> Bool { tapeInserted }

    /// The transport keys in row order, left to right, as one group at the left
    /// edge in the tape's own geography: the start of the tape at the left, the
    /// end of the recordings at the right of the group (section 9.0). Eject is
    /// not here -- it sits apart at the right edge.
    static let transportRow: [CassetteKey] = [
        .backToStart, .rewind, .stop, .play, .record, .fastForward, .toEnd,
    ]
}

/// The deck row latches, like a mechanical deck (section 9.0): the key for the
/// current transport is drawn depressed and so reads as the status display,
/// every key stays live while a tape is in, and greying is reserved for what the
/// deck cannot do. The row is shown only with a tape in (see CassetteDeckLayout),
/// so there is no empty-deck case here. Pure, so the whole matrix is tested
/// without a view.
enum CassetteKeys {
    /// The appearance of `key` for a deck with a tape in. A key is greyed only
    /// where the deck cannot act -- a wind, seek or Back to the beginning on a
    /// live input, or Record on a live input or a non-recordable tape -- and is
    /// otherwise live and latches when it is the current transport. Play is down
    /// while Playing and also while Recording, since Record implies Play; Stop
    /// and Back to the beginning are momentary and never latch.
    ///
    /// Colour means the tape is moving (section 9.0). A down key that is merely
    /// waiting is grey. Play and Record turn the tape only with the motor relay
    /// closed, so they are grey while it is open and coloured once it closes --
    /// Play green while Playing, red while Recording; Record red. A wind moves
    /// the tape whatever the relay does, so Rewind and Fast forward are blue
    /// while winding. A key that is not down is never coloured.
    static func appearance(of key: CassetteKey,
                           transport: Beebium_TapeTransport,
                           motorOn: Bool,
                           isLive: Bool,
                           recordable: Bool,
                           appendRefusal: String) -> CassetteKeyAppearance {
        switch key {
        case .rewind:
            let winding = transport == .rewinding
            return CassetteKeyAppearance(enabled: !isLive, depressed: winding,
                                         colour: winding ? .blue : .none)
        case .fastForward:
            let winding = transport == .fastForwarding
            return CassetteKeyAppearance(enabled: !isLive, depressed: winding,
                                         colour: winding ? .blue : .none)
        case .backToStart:
            return CassetteKeyAppearance(enabled: !isLive, depressed: false, colour: .none)
        case .play:
            let down = transport == .playing || transport == .recording
            let colour: CassetteKeyColour
            if !down { colour = .none }
            else if !motorOn { colour = .grey }
            else { colour = transport == .recording ? .red : .green }
            return CassetteKeyAppearance(enabled: true, depressed: down, colour: colour)
        case .stop:
            return CassetteKeyAppearance(enabled: true, depressed: false, colour: .none)
        case .record:
            // Greyed purely on the server's recordable flag, which is false for
            // a CSW, a WAV, a live input and a read-only UEF alike. Down while
            // Recording; red only once the relay closes, grey while it waits.
            let recording = transport == .recording
            let colour: CassetteKeyColour = !recording ? .none : (motorOn ? .red : .grey)
            return CassetteKeyAppearance(enabled: recordable, depressed: recording, colour: colour)
        case .toEnd:
            // Momentary, never down or coloured. Greyed purely on the server's
            // append refusal -- empty means the append seek is allowed; a live
            // tape, a not-ready signal map or an at-end tape each fill it.
            return CassetteKeyAppearance(enabled: appendRefusal.isEmpty,
                                         depressed: false, colour: .none)
        }
    }

    /// The To the end of the recordings key's help: "To the end of the
    /// recordings" when the append seek is allowed, otherwise the server's own
    /// refusal sentence verbatim (a live tape, a not-ready map, an at-end tape).
    /// Pure, so it is tested without a view.
    static func appendHelp(refusal: String) -> String {
        let trimmed = refusal.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? "To the end of the recordings" : trimmed
    }

    /// The Record key's help: "Record" when the tape can be recorded, otherwise
    /// the server's own refusal sentence -- a CSW, a WAV, a live input and a
    /// read-only UEF each give their own -- with a generic fallback if the server
    /// sent none. Pure, so it is tested without a view.
    static func recordHelp(recordable: Bool, refusal: String) -> String {
        if recordable { return "Record" }
        let trimmed = refusal.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? "This tape cannot be recorded onto" : trimmed
    }
}

/// The one cassette deck, shown at the top of the storage sidebar above the
/// floppy drives when the model has a cassette interface. A transport with a
/// counter and a scrubbing timeline, as a real deck is: Play, Stop, Rewind, Fast
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
    /// Non-nil while the user drags the position timeline: the scrub position
    /// the pointer follows. Nil the rest of the time, when the pointer follows
    /// the deck. A seek happens on release, never while the pointer moves.
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

            // An empty deck shows only the ways to fill it; the transport key
            // row, counter and timeline appear only once a tape is in, so no
            // greyed row sits waiting to come alive (section 9.0).
            if CassetteDeckLayout.showsTransport(tapeInserted: tapeInserted) {
                transportControls
            } else {
                emptyControls
            }
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
        // A sustained live-input starve or drop is a deck failure, deduplicated
        // by the client. It takes the same marker and popover an insert failure
        // does; the client reports each raise once.
        .onChange(of: cassetteClient.liveStarve) { starve in
            if let starve {
                deckError.showFailure(starve.brief, detail: starve.detail)
                cassetteClient.clearLiveStarve()
            }
        }
        // A deck message is about the tape that was in; when that tape leaves
        // (an eject, or a live stream ending), clear it, as section 9.0 asks.
        // The transition fires only for a tape that was present, so an insert
        // failure -- reported with no tape in -- is left standing.
        .onChange(of: tapeInserted) { inserted in
            if !inserted { deckError.clear() }
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

            counterAndTimeline
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
    /// timeline beneath it. With an unknown duration (0) there is no
    /// proportional timeline, only the counter, as a real deck with no tape
    /// length to show.
    @ViewBuilder
    private var counterAndTimeline: some View {
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
            TapeTimeline(
                position: min(scrubSeconds ?? cassetteClient.positionSeconds, duration),
                duration: duration,
                isEnabled: !isProcessing,
                signalSeconds: cassetteClient.signalMapSeconds,
                signalLevels: cassetteClient.signalMapLevels,
                onScrub: { scrubSeconds = $0 },
                onCommit: { target in
                    scrubSeconds = nil
                    seek(to: target)
                }
            )
        }

        // A live tape has no timeline (no length to scrub); in its place sits
        // the one indicator a user can act on -- the input level and a clip
        // mark, from the client's own capture tap (section 9.0).
        if isLive {
            LiveLevelMeter(bar: cassetteClient.liveMeterBar,
                           hold: cassetteClient.liveMeterHold,
                           clipping: cassetteClient.liveMeterClipping)
                .help("The level of the live input, in decibels. Keep the held peak near the "
                      + "tick; turn the source down if the red mark appears.")
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

    /// The deck row with a tape in (section 9.0). The transport keys are one
    /// group at the left edge, laid out in the tape's own geography (Back to the
    /// beginning, Rewind, Stop, Play, Record, Fast forward, To the end of the
    /// recordings -- see CassetteDeckLayout.transportRow), then a stretch, then
    /// Eject alone at the right edge. Browse is not here: a tape cannot be
    /// inserted over an occupied deck, so the deck is filled by ejecting first.
    private var transportControls: some View {
        HStack(spacing: 8) {
            ForEach(CassetteDeckLayout.transportRow, id: \.self) { key in
                transportButton(for: key)
            }
            Spacer()
            ejectButton
        }
    }

    /// The button for one transport key, so the row is built from
    /// CassetteDeckLayout.transportRow and its order cannot drift from the test.
    @ViewBuilder
    private func transportButton(for key: CassetteKey) -> some View {
        switch key {
        case .backToStart: backToStartButton
        case .rewind: rewindButton
        case .stop: stopButton
        case .play: playButton
        case .record: recordButton
        case .fastForward: fastForwardButton
        case .toEnd: toEndButton
        }
    }

    /// The empty deck's one control: the browse menu (Browse, Recent, New Blank
    /// Tape and the live-input device picker). No transport keys, counter or
    /// timeline -- an empty deck offers only the ways to fill it, and the whole
    /// view is also a drop target.
    private var emptyControls: some View {
        HStack(spacing: 8) {
            browseButton
            Spacer()
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
    /// the server by its tape length ("C15", "C30", "C60", "C90"). Choosing one
    /// saves a copy to a path the user picks and inserts it, stopped at the
    /// start, ready to Record. The labels come from the server (nothing
    /// hard-coded), so the submenu is disabled when the server offers none.
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
                     key: .rewind) {
            run { await cassetteClient.rewind() }
        }
    }

    private var fastForwardButton: some View {
        transportKey("forward.fill", help: isLive ? "A live input cannot be wound" : "Fast forward",
                     key: .fastForward) {
            run { await cassetteClient.fastForward() }
        }
    }

    private var backToStartButton: some View {
        transportKey("backward.end.fill",
                     help: isLive ? "A live input cannot be sought" : "Back to the beginning",
                     key: .backToStart) {
            seek(to: 0)
        }
    }

    /// To the end of the recordings: the mirror of Back to the beginning, a seek
    /// to the tape's append point so a new recording follows the last. Momentary;
    /// greyed with the server's sentence as help while the append is refused.
    private var toEndButton: some View {
        transportKey("forward.end.fill",
                     help: CassetteKeys.appendHelp(refusal: cassetteClient.appendRefusal),
                     key: .toEnd) {
            run { await cassetteClient.seekToAppendPoint() }
        }
    }

    private var playButton: some View {
        transportKey("play.fill", help: "Play", key: .play) {
            run { await cassetteClient.play() }
        }
    }

    private var stopButton: some View {
        transportKey("stop.fill", help: "Stop", key: .stop) {
            run { await cassetteClient.stop() }
        }
    }

    /// The Record key. It is drawn in the ordinary key colour and turns red only
    /// while Recording (section 9.0), never as a standing invitation. It is
    /// greyed for a tape that cannot be recorded onto -- a CSW, a WAV, a live
    /// input or a read-only UEF -- with the server's own refusal sentence as its
    /// help, so the disabled key says why rather than letting the server refuse
    /// after a press.
    private var recordButton: some View {
        transportKey("record.circle.fill",
                     help: CassetteKeys.recordHelp(recordable: cassetteClient.tape?.recordable ?? false,
                                                   refusal: cassetteClient.tape?.recordRefusal ?? ""),
                     key: .record) {
            run { await cassetteClient.record() }
        }
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

    /// One transport key: a borderless glyph that latches. It is drawn depressed
    /// (a filled rounded background) while it is the lit status key, coloured
    /// only while the tape is moving (green Play, red Record, blue wind) and grey
    /// while a down key merely waits, and dimmed only when the deck cannot do it.
    /// Every key stays enabled while a tape is in; a press on a key already down
    /// does nothing (the server's transport is idempotent, so the short-circuit
    /// is a local nicety).
    private func transportKey(_ symbol: String, help: String, key: CassetteKey,
                              action: @escaping () -> Void) -> some View {
        let appearance = CassetteKeys.appearance(
            of: key, transport: transport, motorOn: cassetteClient.motorOn,
            isLive: isLive, recordable: cassetteClient.tape?.recordable ?? false,
            appendRefusal: cassetteClient.appendRefusal)
        return Button {
            if !appearance.depressed { action() }
        } label: {
            Image(systemName: symbol)
                .font(.system(size: 14))
                .foregroundColor(transportKeyColor(appearance))
                .padding(2)
                .background(
                    RoundedRectangle(cornerRadius: 4)
                        .fill(appearance.depressed ? Color.secondary.opacity(0.25) : Color.clear))
        }
        .buttonStyle(.borderless)
        .disabled(!appearance.enabled)
        .help(help)
    }

    /// A transport key's glyph colour: dim when it cannot be pressed; otherwise
    /// its motion colour -- green, red or blue while the tape moves, grey while a
    /// down key waits -- or the ordinary key colour (muted while up) when it
    /// carries no motion colour.
    private func transportKeyColor(_ appearance: CassetteKeyAppearance) -> Color {
        if !appearance.enabled { return .secondary.opacity(0.3) }
        switch appearance.colour {
        case .green: return .green
        case .red: return .red
        case .blue: return .blue
        case .grey: return .secondary
        case .none: return .primary.opacity(appearance.depressed ? 1.0 : 0.7)
        }
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
            panel.directoryURL = DialogLocations.shared.startDirectory(for: .insertTape)
            if panel.runModal() == .OK, let url = panel.url {
                DialogLocations.shared.remember(url, for: .insertTape)
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
            panel.directoryURL = DialogLocations.shared.startDirectory(for: .newBlankTape)
            guard panel.runModal() == .OK, let url = panel.url else { return }
            DialogLocations.shared.remember(url, for: .newBlankTape)
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
                    // A path the server refuses -- a shipped template, a file
                    // that has gone or gone bad -- should stop being offered, so
                    // a refused insert drops it from Recent Tapes. A plain
                    // not-connected is transient and leaves the list alone.
                    if case .operationFailed = error {
                        recentTapes.remove(filepath: url.path)
                    }
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

/// The position timeline: a pointer on a bar with no fill either side, since a
/// tape has no played and unplayed portion (section 9.0). Dragging anywhere
/// along the bar scrubs the pointer; the seek happens on release, as the slider
/// it replaced did. The pointer follows the deck -- including winds -- while the
/// bar is not being touched. Shown only when the server reports a duration.
///
/// When the server's signal map is ready, the bar is coloured from it: the
/// seconds that carry signal (at or above the accent threshold) are drawn in the
/// accent colour, the rest in the bar's plain grey. Until the map is ready, and
/// for a live tape (which has none), the plain grey bar.
private struct TapeTimeline: View {
    /// The position the pointer shows, in seconds (the scrub position while
    /// dragging, the deck position otherwise), already clamped to the tape.
    let position: Double
    let duration: Double
    let isEnabled: Bool
    /// The signal map's total seconds and one level byte per second, or empty
    /// for a plain bar (no map yet, or a live tape).
    let signalSeconds: Int
    let signalLevels: [UInt8]
    /// Called continuously while dragging, with the scrubbed-to position.
    let onScrub: (Double) -> Void
    /// Called once on release, with the position to seek to.
    let onCommit: (Double) -> Void

    private let barHeight: CGFloat = 3
    private let pointerSize: CGFloat = 9

    var body: some View {
        GeometryReader { geometry in
            let width = geometry.size.width
            let fraction = TapeTimelineGeometry.fraction(position: position, duration: duration)
            ZStack(alignment: .topLeading) {
                bar
                // The pointer, its tip on the bar at the position. It is drawn in
                // the primary colour so it stays legible over both the plain grey
                // and the accent-coloured stretches of the bar.
                Image(systemName: "arrowtriangle.down.fill")
                    .font(.system(size: pointerSize))
                    .foregroundColor(isEnabled ? .primary : Color.primary.opacity(0.4))
                    .offset(x: CGFloat(fraction) * width - pointerSize / 2, y: 0)
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        guard isEnabled else { return }
                        onScrub(TapeTimelineGeometry.position(
                            atX: Double(value.location.x), width: Double(width), duration: duration))
                    }
                    .onEnded { value in
                        guard isEnabled else { return }
                        onCommit(TapeTimelineGeometry.position(
                            atX: Double(value.location.x), width: Double(width), duration: duration))
                    }
            )
        }
        .frame(height: 16)
    }

    /// The bar itself: a plain grey capsule with no map, or, when the map is
    /// ready, a grey capsule whose signal-carrying columns are drawn in the
    /// accent colour. Both keep the rounded ends and carry no fill of a played
    /// or unplayed side.
    @ViewBuilder
    private var bar: some View {
        if signalLevels.isEmpty {
            Capsule()
                .fill(Color.secondary.opacity(0.3))
                .frame(height: barHeight)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .center)
        } else {
            Canvas { context, size in
                let barY = (size.height - barHeight) / 2
                let barRect = CGRect(x: 0, y: barY, width: size.width, height: barHeight)
                context.clip(to: Capsule().path(in: barRect))
                context.fill(Path(barRect), with: .color(Color.secondary.opacity(0.3)))
                let columns = CassetteSignalMap.litColumns(
                    seconds: signalSeconds, levels: signalLevels,
                    width: Int(size.width.rounded()))
                guard !columns.isEmpty else { return }
                let columnWidth = size.width / CGFloat(columns.count)
                for (index, lit) in columns.enumerated() where lit {
                    let rect = CGRect(x: CGFloat(index) * columnWidth, y: barY,
                                      width: columnWidth + 1, height: barHeight)
                    context.fill(Path(rect), with: .color(Color.accentColor))
                }
            }
        }
    }
}

/// The live input's level meter, in decibels of full scale, shown where the
/// timeline sits for a file tape (a live tape has no length, section 9.0;
/// cassette round 12). The bar fills to the decibel level, a tick marks the
/// target band (-12 dB), a thin peak-hold line sits at the highest recent level
/// -- the figure the user sets the source by -- and a mark at the right lights
/// red only while the input is clipping, and is otherwise absent (never dimmed).
/// The bar, hold and clip state are computed and held in `LevelMeterState` via
/// CassetteClient; this only draws them.
private struct LiveLevelMeter: View {
    /// The bar's position (0 at the -60 dB floor, 1 at full scale).
    let bar: Float
    /// The peak-hold line's position (0 at the floor, 1 at full scale).
    let hold: Float
    /// Whether the input is clipping (held briefly so a transient is seen).
    let clipping: Bool

    private let barHeight: CGFloat = 4
    private let markWidth: CGFloat = 4

    var body: some View {
        HStack(spacing: 4) {
            GeometryReader { geometry in
                let width = geometry.size.width
                let fill = width * CGFloat(min(max(bar, 0), 1))
                let tickX = width * CGFloat(LevelMeterState.targetFraction)
                let holdX = width * CGFloat(min(max(hold, 0), 1))
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.secondary.opacity(0.3))
                        .frame(height: barHeight)
                    Capsule().fill(Color.accentColor)
                        .frame(width: fill, height: barHeight)
                    // The target tick, above and below the bar.
                    Rectangle().fill(Color.secondary)
                        .frame(width: 1, height: barHeight + 6)
                        .offset(x: tickX)
                    // The peak-hold line; drawn only once there is a peak to show.
                    if hold > 0 {
                        Rectangle().fill(Color.primary)
                            .frame(width: 2, height: barHeight + 4)
                            .offset(x: min(holdX, width - 2))
                    }
                }
                .frame(maxHeight: .infinity, alignment: .center)
            }
            // The clip mark keeps its column so the bar does not reflow, but is
            // clear (absent) unless lit -- never a grey or dim state (round 12).
            RoundedRectangle(cornerRadius: 1)
                .fill(clipping ? Color.red : Color.clear)
                .frame(width: markWidth, height: barHeight + 6)
        }
        .frame(height: 16)
    }
}
