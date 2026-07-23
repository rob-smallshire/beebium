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
import Foundation
import SwiftUI

// MARK: - Wire-free value types
//
// The coordinator and its tests speak in these plain types, never in the
// generated protobuf structs, so the selection logic can be exercised without a
// server. VideoClient translates at the boundary (see its ScreenTextService
// conformance).

/// Which of the two searches the server should run. The teletext strategy reads
/// the character grid and ignores it; the glyph-recognising strategy honours it.
enum ScreenTextSearchMode {
    /// Everything, on the grid and placed freely with VDU 5. Slower.
    case anywhere
    /// Only text aligned to the character grid. Exact and cheaper.
    case aligned

    var proto: Beebium_ScreenTextSearch {
        switch self {
        case .anywhere: return .anywhere
        case .aligned: return .aligned
        }
    }
}

/// Which meaning a MODE 7 byte is read with.
///
/// The SAA5050 draws eleven codes as characters ASCII puts elsewhere -- a left
/// arrow where ASCII has `[`, an up arrow where ASCII has `^`, and so on -- so a
/// teletext screen can be read two ways and only the person reading it knows
/// which they meant. The mapping itself is the server's; this is the choice.
enum ScreenTextCharactersMode: String, CaseIterable {
    /// The byte at face value. The default: MODE 7 is the BBC's default screen
    /// mode, and a copied BASIC listing keeps its assembler brackets and its
    /// exponentiation operator.
    case codes
    /// The glyphs the screen showed, as Unicode. For capturing a teletext page
    /// as it looked.
    case displayed

    var proto: Beebium_ScreenTextCharacters {
        switch self {
        case .codes: return .codes
        case .displayed: return .displayed
        }
    }

    /// Where the choice is remembered between sessions. A window takes this as
    /// its starting value and may then be set independently, so two windows
    /// showing different things can be read differently.
    static let defaultsKey = "screenTextMode7Characters"

    /// The remembered choice, or `.codes` when nothing has been remembered or
    /// what was remembered is no longer a mode we have.
    static func remembered(in defaults: UserDefaults = .standard) -> Self {
        guard let raw = defaults.string(forKey: defaultsKey),
              let mode = Self(rawValue: raw) else { return .codes }
        return mode
    }

    func remember(in defaults: UserDefaults = .standard) {
        defaults.set(rawValue, forKey: Self.defaultsKey)
    }
}

/// How the server joins runs into its `text` field.
enum ScreenTextJoinLayout {
    case rows
    case flowed

    var proto: Beebium_ScreenTextLayout {
        switch self {
        case .rows: return .rows
        case .flowed: return .flowed
        }
    }
}

/// One cell of a run: where it sits, and whether a glyph was recognised there.
/// A genuine space is matched; an unmatched cell had ink no glyph fit and copies
/// as a space. Highlighting only matched cells keeps the overlay from dressing a
/// failed read up as a success.
struct ScreenTextRunCell: Equatable {
    var bounds: FramePixelRect
    var matched: Bool
}

/// A contiguous piece of text and where it was found, in frame pixels.
struct ScreenTextRun: Equatable {
    var text: String
    var bounds: FramePixelRect
    /// The cell geometry this run was read with. Zero when the run is not
    /// cell-aligned (text written at the graphics cursor with VDU 5).
    var cellWidth: Int
    var cellHeight: Int
    /// The run's cells in reading order. Empty from the teletext strategy, whose
    /// cells are exact characters; a client then highlights the whole `bounds`.
    var cells: [ScreenTextRunCell] = []
}

/// A still the server captured, in the shape the renderer needs to show it.
///
/// The client displays this rather than whichever frame it last happened to
/// draw, so the picture the user selects on is pixel-for-pixel the one the
/// reads are made against -- not a frame or two either side of it.
struct HeldFrame: Equatable {
    var pixels: Data
    var width: Int
    var height: Int
    var displayWidth: Int
    var displayHeight: Int
    var leftBorder: Int
    var rightBorder: Int
    var topBorder: Int
    var bottomBorder: Int
    var interlaced: Bool
    var regions: [DisplayRegion]
}

/// A screen held on the server for the life of a selection.
struct ScreenHold: Equatable {
    var holdID: UInt64
    /// The grid the held screen implies, returned with the hold so the geometry
    /// cannot drift from the pixels it describes -- and so holding costs no
    /// more round trips than fetching the geometry used to.
    var bands: [ScreenBandGeometry]
    /// The held still, when it was asked for.
    var frame: HeldFrame?
}

/// What GetScreenText returned, wire-free.
struct ScreenTextReading: Equatable {
    var supported: Bool
    var text: String
    var unreadableCells: Int
    var ambiguousCells: Int
    var runs: [ScreenTextRun]
}

// MARK: - Interpretation

/// The three readings of one drag, chosen by the modifier held while the
/// selection is live. The modifier does one job in two places: it decides what
/// is highlighted and what a copy captures, so the two never disagree.
enum ScreenSelectionInterpretation: Equatable {
    /// (no modifier) Aligned rows, reading order from anchor to focus, like
    /// selecting text in an editor. The common case.
    case rows
    /// (Option) The aligned block of cells the selection covers -- column
    /// selection, for a table of figures.
    case rectangle
    /// (Shift) All the text whose glyphs fall in the selection, on the grid and
    /// placed freely with VDU 5. Inherently a rectangle: off-grid text has no
    /// rows.
    case anywhere

    var search: ScreenTextSearchMode {
        switch self {
        case .rows, .rectangle: return .aligned
        case .anywhere: return .anywhere
        }
    }
}

// MARK: - Seams

/// The screen-text calls the selection needs, narrowed so the coordinator can be
/// tested without a channel.
@MainActor
protocol ScreenTextService: AnyObject {
    /// Hold the screen as it stands, so every read a selection makes describes
    /// one still. The emulator keeps running: a hold is a copy, not a pause.
    func holdScreen(includeFrame: Bool) async -> ScreenHold?

    /// Let a held screen go. Holds expire on their own, but a client that has
    /// finished should say so.
    func releaseScreen(_ holdID: UInt64) async

    func screenGeometry(holdID: UInt64?) async -> [ScreenBandGeometry]

    func screenText(region: FramePixelRect?,
                    search: ScreenTextSearchMode,
                    layout: ScreenTextJoinLayout,
                    characters: ScreenTextCharactersMode,
                    holdID: UInt64?) async -> ScreenTextReading?
}

/// The rendering geometry the mapping needs, captured once when the frame
/// freezes. Not main-actor isolated: MetalRenderer is a plain NSObject driven on
/// the main thread, and the coordinator calls this from the main actor anyway.
protocol RenderGeometrySource: AnyObject {
    func captureRenderGeometry() -> ScreenRenderGeometry?
}

/// Freezing and resuming the displayed frame. Freezing is a property of the
/// view, not the machine: the emulator keeps running underneath.
@MainActor
protocol DisplayFreezer: AnyObject {
    func freezeDisplay()
    func resumeDisplay()

    /// Show a still the server captured, in place of the frame the view last
    /// drew, so the picture the selection is drawn on is the one being read.
    func showHeldFrame(_ frame: HeldFrame)
}

/// The system pasteboard, as the copy needs it.
@MainActor
protocol SelectionPasteboard: AnyObject {
    func writeText(_ text: String)
}

// MARK: - Coordinator

/// Owns the freeze-select-highlight-copy sequence for one machine window.
///
/// Extracted from the Metal view for the same reason PasteCoordinator was: the
/// interesting logic -- mapping a mouse point to a frame pixel, snapping to
/// cells, flowing a rows selection, choosing the search, and trimming the copy
/// -- is arithmetic that wants testing without a window, a server, or a
/// pasteboard. The view is left a thin trigger that forwards mouse and key
/// events here.
@MainActor
final class SelectionCoordinator: ObservableObject {

    weak var textService: ScreenTextService?
    weak var geometrySource: RenderGeometrySource?
    weak var freezer: DisplayFreezer?
    weak var pasteboard: SelectionPasteboard?

    /// True while a selection is live and the frame is frozen. Drives whether
    /// the view swallows Escape and draws the overlay.
    @Published private(set) var isSelecting = false

    /// The runs currently highlighted -- what a copy will actually take -- in
    /// frame pixels, for the overlay to draw as the stronger fill. Reflects the
    /// current interpretation.
    @Published private(set) var highlightRects: [FramePixelRect] = []

    /// The selection range in frame pixels: the reading-order flow for rows (to
    /// the screen edges), the snapped block for a rectangle, and nothing for
    /// anywhere (which has no grid). Drawn as the lighter fill beneath the runs,
    /// so the user sees what is selected as well as what will copy. Pure
    /// geometry, so it appears as soon as the grid is known, before the text
    /// comes back.
    @Published private(set) var rangeRects: [FramePixelRect] = []

    /// The raw dragged rectangle, in frame pixels, for the overlay to outline.
    @Published private(set) var marqueeRect: FramePixelRect?

    private(set) var interpretation: ScreenSelectionInterpretation = .rows

    /// Where the `Mode 7 Copies As` choice is read from. Injectable so the
    /// coordinator can be tested without touching the user's own defaults.
    var defaults: UserDefaults = .standard

    /// Which meaning a MODE 7 byte is copied with, applied to every copy this
    /// window makes: the choice is a property of what the user is looking at,
    /// not of which copy command they reach for, so it does not multiply with
    /// them.
    ///
    /// Read at each copy rather than held. The menu writes the defaults and
    /// nothing pushes the value here, which is what keeps that submenu free of
    /// any focused value -- see Mode7CopiesAsMenu for why that matters.
    var mode7Characters: ScreenTextCharactersMode {
        .remembered(in: defaults)
    }

    /// The screen held on the server for this selection's life. Every read names
    /// it, so what is recognised describes the still on screen.
    private var holdID: UInt64?

    /// The gesture in normalised view coordinates, kept so the anchor and focus
    /// can be re-derived if the held still turns out to be a slightly later
    /// frame than the one the view had when the drag began.
    private var anchorNormalized: CGPoint?
    private var focusNormalized: CGPoint?

    /// The work a begin or a cancel scheduled, so tests can await it.
    private var pendingHold: Task<Void, Never>?
    private var pendingRelease: Task<Void, Never>?

    /// Frame-pixel anchor (drag start) and focus (drag now). Both frozen-frame
    /// coordinates; the view-to-frame mapping happened at event time.
    private var anchorFrame: CGPoint?
    private var focusFrame: CGPoint?

    /// Geometry captured at freeze, fixed for the selection's life. Exposed
    /// read-only so the overlay can map highlight rectangles from frame pixels
    /// to view coordinates.
    private(set) var frozenGeometry: ScreenRenderGeometry?
    private var bands: [ScreenBandGeometry] = []

    /// Bumped on every selection change so a slow highlight response for a
    /// stale selection is discarded rather than drawn.
    private var highlightGeneration = 0

    init() {}

    /// Whether a plain (non-Cmd) drag should be treated as the machine's, not
    /// ours. Once a selection is live, the qualifier modifiers belong to the
    /// selection and must not reach the emulated keyboard.
    var ownsQualifierModifiers: Bool { isSelecting }

    // MARK: - Gesture lifecycle

    /// Begin a selection at a normalised, top-left-origin view point. Freezes the
    /// frame, captures its geometry, and fetches the character grid. A point
    /// outside the picture rectangle (in the letterbox) does not start a
    /// selection.
    func begin(atNormalizedPoint point: CGPoint,
               interpretation: ScreenSelectionInterpretation) {
        guard let geometry = geometrySource?.captureRenderGeometry() else { return }
        guard let hit = ScreenCoordinateMapper.framePixel(
            normalizedPoint: point, geometry: geometry) else { return }

        // A fresh drag replaces whatever was selected, so let the screen the
        // last one held go rather than leaving it for the expiry to reap.
        cancel()

        frozenGeometry = geometry
        anchorNormalized = point
        focusNormalized = point
        anchorFrame = hit.pixel
        focusFrame = hit.pixel
        self.interpretation = interpretation
        isSelecting = true

        // Stop the view at once, so the picture is still while the hold is
        // taken; the hold then replaces it with the exact frame it captured.
        freezer?.freezeDisplay()

        pendingHold = Task { [weak self] in await self?.holdScreenAndRefresh() }
    }

    /// Hold the screen for this selection and show the still it captured.
    ///
    /// This is what makes the preview honest. Without it the client freezes its
    /// own picture while reads run against the live screen, so on anything that
    /// moves the highlights describe a frame the user is not looking at. Split
    /// out from `begin` so it can be driven deterministically in tests.
    func holdScreenAndRefresh() async {
        guard isSelecting else { return }

        guard let hold = await textService?.holdScreen(includeFrame: true) else {
            // No still to read against. Carrying on would read the live screen
            // behind a frozen picture -- the mismatch this exists to remove --
            // so let the display go rather than mislead.
            cancel()
            return
        }
        guard isSelecting else {
            // Dismissed while the hold was in flight; do not leak it.
            await textService?.releaseScreen(hold.holdID)
            return
        }

        holdID = hold.holdID
        bands = hold.bands

        if let frame = hold.frame {
            freezer?.showHeldFrame(frame)

            // The capture may be a frame or two on from the one the view had,
            // so take the geometry the still actually implies and re-map the
            // gesture onto it. Ordinarily identical; this costs nothing when it
            // is, and is right when a mode changed in between.
            if let geometry = geometrySource?.captureRenderGeometry() {
                frozenGeometry = geometry
                remapGesture(to: geometry)
            }
        }

        refreshHighlights()
    }

    /// Re-derive the frame-pixel anchor and focus from the normalised gesture.
    private func remapGesture(to geometry: ScreenRenderGeometry) {
        if let normalized = anchorNormalized,
           let hit = ScreenCoordinateMapper.framePixel(
               normalizedPoint: normalized, geometry: geometry) {
            anchorFrame = hit.pixel
        }
        if let normalized = focusNormalized,
           let hit = ScreenCoordinateMapper.framePixel(
               normalizedPoint: normalized, geometry: geometry) {
            focusFrame = hit.pixel
        }
    }

    /// Awaits the work a begin or a cancel scheduled. For tests.
    func drainPendingWork() async {
        await pendingHold?.value
        await pendingRelease?.value
    }

    /// Extend the selection to a new normalised view point.
    func update(toNormalizedPoint point: CGPoint) {
        guard isSelecting, let geometry = frozenGeometry else { return }
        guard let hit = ScreenCoordinateMapper.framePixel(
            normalizedPoint: point, geometry: geometry) else { return }
        focusNormalized = point
        focusFrame = hit.pixel
        refreshHighlights()
    }

    /// Change the held interpretation while the selection stays put.
    func setInterpretation(_ interpretation: ScreenSelectionInterpretation) {
        guard isSelecting, interpretation != self.interpretation else { return }
        self.interpretation = interpretation
        refreshHighlights()
    }

    /// Dismiss the selection, drop the freeze, and let the display animate on.
    /// Idempotent: a plain click and an Escape can both arrive.
    func cancel() {
        guard isSelecting else { return }
        isSelecting = false

        // Let the server drop the still. Holds expire on their own, so a lost
        // release is survivable, but saying so returns the memory at once.
        if let released = holdID, let service = textService {
            pendingRelease = Task { await service.releaseScreen(released) }
        }
        holdID = nil

        anchorFrame = nil
        focusFrame = nil
        anchorNormalized = nil
        focusNormalized = nil
        frozenGeometry = nil
        bands = []
        highlightRects = []
        rangeRects = []
        marqueeRect = nil
        highlightGeneration += 1
        freezer?.resumeDisplay()
    }

    // MARK: - Highlights

    private func refreshHighlights() {
        highlightGeneration += 1
        let generation = highlightGeneration

        guard let region = selectionBounds(),
              let anchor = anchorFrame, let focus = focusFrame else {
            marqueeRect = nil
            highlightRects = []
            rangeRects = []
            return
        }
        let interpretation = self.interpretation

        // Rows and columns show their snapped range, which traces the selection
        // more clearly than the raw drag, so the marquee outline would only
        // clutter them. Only the grid-less anywhere mode, which has no range,
        // needs the outline to show how far the selection reaches.
        marqueeRect = interpretation == .anywhere ? region : nil

        // The range is pure geometry from the frozen grid, so it can be drawn at
        // once, without waiting for the text. The band under the selection start
        // decides the cell geometry.
        let band = griddedBand(containingFrameY: Int(min(anchor.y, focus.y)))
        rangeRects = Self.rangeRects(
            interpretation: interpretation, anchor: anchor, focus: focus,
            band: band, frameWidth: Int(frozenGeometry?.textureSize.width ?? 0))

        Task { [weak self] in
            guard let self else { return }
            let resolved = await self.resolveRuns(interpretation: interpretation)
            // Discard a response for a selection that has since moved.
            guard self.highlightGeneration == generation else { return }
            self.highlightRects = Self.matchedCellRects(resolved?.runs ?? [])
        }
    }

    /// The rectangles to highlight for a set of runs, showing only the text that
    /// was actually read.
    ///
    /// A run with no cells is teletext, whose cells are all exact matches: the
    /// whole run's bounds are lit. Otherwise a cell is lit only when it is a
    /// *real glyph* -- matched, and not a space -- or a space that bridges real
    /// glyphs, the way an editor highlights the space in "two words". A matched
    /// space is a blank cell that happens to fit the space glyph; adrift in a
    /// run of unrecognised glyphs (a game's custom font, where every letter is
    /// unmatched and only the gaps between words match) it is not text we read,
    /// so it stays dark and the overlay honestly shows nothing rather than a row
    /// of lit gaps.
    ///
    /// The run's `text` is one character per cell, so a cell's character says
    /// whether it is a space without the server having to send the codepoint.
    static func matchedCellRects(_ runs: [ScreenTextRun]) -> [FramePixelRect] {
        runs.flatMap { run -> [FramePixelRect] in
            guard !run.cells.isEmpty else { return [run.bounds] }

            let characters = Array(run.text)
            func isRealGlyph(_ i: Int) -> Bool {
                run.cells[i].matched && i < characters.count && characters[i] != " "
            }
            let realGlyphs = run.cells.indices.filter(isRealGlyph)
            guard let first = realGlyphs.first, let last = realGlyphs.last else {
                // Nothing recognised in this run -- do not light its gaps.
                return []
            }
            // From the first real glyph to the last, light every matched cell:
            // the glyphs, and the spaces between them that they bridge.
            return (first...last)
                .filter { run.cells[$0].matched }
                .map { run.cells[$0].bounds }
        }
    }

    // MARK: - Copy

    /// Copy the selection in the given interpretation, or -- with no selection
    /// live -- the whole screen aligned from the live frame.
    ///
    /// Returns the copied text (for tests and callers that want it), or nil when
    /// there was nothing to copy.
    @discardableResult
    func copy(_ interpretation: ScreenSelectionInterpretation) async -> String? {
        guard isSelecting else { return await copyWholeScreen() }
        guard let resolved = await resolveRuns(interpretation: interpretation) else {
            return nil
        }
        let text = resolved.text
        guard !text.isEmpty else { return nil }
        pasteboard?.writeText(text)
        return text
    }

    /// Copy the whole screen, aligned, from the current live frame. No freeze
    /// and nothing to preview: the everyday "copy what is on the screen".
    @discardableResult
    func copyWholeScreen() async -> String? {
        guard let reading = await textService?.screenText(
            region: nil, search: .aligned, layout: .rows,
            characters: mode7Characters, holdID: nil) else { return nil }
        let text = reading.text
        guard !text.isEmpty else { return nil }
        pasteboard?.writeText(text)
        return text
    }

    // MARK: - Region and run resolution

    /// The dragged rectangle in frame pixels, or nil when there is no selection.
    private func selectionBounds() -> FramePixelRect? {
        guard let anchor = anchorFrame, let focus = focusFrame else { return nil }
        return Self.boundingRect(anchor, focus)
    }

    /// Run the request for the current interpretation and return the runs plus
    /// the text a copy would take -- trimmed to the flow for a rows selection.
    private func resolveRuns(interpretation: ScreenSelectionInterpretation)
        async -> (runs: [ScreenTextRun], text: String)? {
        guard let anchor = anchorFrame, let focus = focusFrame else { return nil }

        let band = griddedBand(containingFrameY: Int(min(anchor.y, focus.y)))

        switch interpretation {
        case .anywhere:
            let region = Self.boundingRect(anchor, focus)
            guard let reading = await textService?.screenText(
                region: region, search: .anywhere, layout: .rows,
                characters: mode7Characters, holdID: holdID) else { return nil }
            return (reading.runs, reading.text)

        case .rectangle:
            let region = Self.snappedRectangle(anchor: anchor, focus: focus, band: band)
            guard let reading = await textService?.screenText(
                region: region, search: .aligned, layout: .rows,
                characters: mode7Characters, holdID: holdID) else { return nil }
            return (reading.runs, reading.text)

        case .rows:
            guard let band, band.isGridded else {
                // No grid to flow along: fall back to the plain rectangle.
                let region = Self.boundingRect(anchor, focus)
                guard let reading = await textService?.screenText(
                    region: region, search: .aligned, layout: .rows,
                    characters: mode7Characters, holdID: holdID) else { return nil }
                return (reading.runs, reading.text)
            }
            let flow = Self.rowsFlow(anchor: anchor, focus: focus, band: band,
                                     frameWidth: Int(frozenGeometry?.textureSize.width ?? 0))
            guard let reading = await textService?.screenText(
                region: flow.requestRegion, search: .aligned, layout: .rows,
                characters: mode7Characters, holdID: holdID) else {
                return nil
            }
            return Self.trimToFlow(runs: reading.runs, band: band, flow: flow)
        }
    }

    /// The gridded band whose scanlines contain `y`, or the first gridded band,
    /// or nil when none advertises a grid.
    private func griddedBand(containingFrameY y: Int) -> ScreenBandGeometry? {
        if let hit = bands.first(where: { y >= $0.top && y < $0.bottom && $0.isGridded }) {
            return hit
        }
        return bands.first(where: { $0.isGridded })
    }
}

// MARK: - Pure geometry (static, testable without a window)

extension SelectionCoordinator {

    /// The axis-aligned bounding rectangle of two frame-pixel points.
    static func boundingRect(_ a: CGPoint, _ b: CGPoint) -> FramePixelRect {
        let x0 = Int(min(a.x, b.x).rounded(.down))
        let y0 = Int(min(a.y, b.y).rounded(.down))
        let x1 = Int(max(a.x, b.x).rounded(.up))
        let y1 = Int(max(a.y, b.y).rounded(.up))
        return FramePixelRect(x: x0, y: y0, width: max(0, x1 - x0), height: max(0, y1 - y0))
    }

    /// The block of whole cells the selection covers, snapped to the band's
    /// grid. Falls back to the plain bounding rectangle when the band has no
    /// grid.
    static func snappedRectangle(anchor: CGPoint, focus: CGPoint,
                                 band: ScreenBandGeometry?) -> FramePixelRect {
        guard let band, band.isGridded else { return boundingRect(anchor, focus) }
        let minX = min(anchor.x, focus.x), maxX = max(anchor.x, focus.x)
        let minY = min(anchor.y, focus.y), maxY = max(anchor.y, focus.y)
        let firstCol = band.column(atFrameX: Int(minX))
        let lastCol = band.column(atFrameX: Int(maxX))
        let firstRow = band.row(atFrameY: Int(minY))
        let lastRow = band.row(atFrameY: Int(maxY))
        let x = band.frameX(ofColumn: firstCol)
        let y = band.frameY(ofRow: firstRow)
        let width = band.frameX(ofColumn: lastCol) + band.cellWidth - x
        let height = band.frameY(ofRow: lastRow) + band.cellHeight - y
        return FramePixelRect(x: x, y: y, width: max(0, width), height: max(0, height))
    }

    /// The selection range in frame pixels -- the overlay's lighter fill, drawn
    /// beneath the recognised runs. Rows flows in reading order to the screen's
    /// left and right edges; a rectangle is the snapped block; anywhere has no
    /// grid, so its only feedback is the marquee and the runs. `frameWidth` is
    /// the active area's width, the right edge the rows flow reaches.
    static func rangeRects(interpretation: ScreenSelectionInterpretation,
                           anchor: CGPoint, focus: CGPoint,
                           band: ScreenBandGeometry?, frameWidth: Int)
        -> [FramePixelRect] {
        switch interpretation {
        case .anywhere:
            return []
        case .rectangle:
            guard let band, band.isGridded else { return [] }
            return [snappedRectangle(anchor: anchor, focus: focus, band: band)]
        case .rows:
            guard let band, band.isGridded else { return [] }
            let a = CellAddress(column: band.column(atFrameX: Int(anchor.x)),
                                row: band.row(atFrameY: Int(anchor.y)))
            let b = CellAddress(column: band.column(atFrameX: Int(focus.x)),
                                row: band.row(atFrameY: Int(focus.y)))
            let (start, end) = orderedInReadingOrder(a, b)
            return rowsFlowRects(start: start, end: end, band: band,
                                 frameWidth: frameWidth)
        }
    }

    /// The reading-order flow as up to three rectangles: the first row from the
    /// start column to the right edge, whole rows in between as one block, and
    /// the last row from the left edge to the end column. A single-row selection
    /// is just the segment between the two columns. Rows use the pitch, not the
    /// cell height, so consecutive rows touch (MODE 3 and MODE 6 leave blank
    /// scanlines between cells; a selection should not be dashed).
    static func rowsFlowRects(start: CellAddress, end: CellAddress,
                              band: ScreenBandGeometry, frameWidth: Int)
        -> [FramePixelRect] {
        let rowHeight = band.rowPitch
        let leftX = band.frameX(ofColumn: 0)
        let rightX = max(leftX, frameWidth)
        let startX = band.frameX(ofColumn: start.column)
        let endX = band.frameX(ofColumn: end.column) + band.columnPitch

        func rowRect(_ row: Int, _ x0: Int, _ x1: Int) -> FramePixelRect {
            FramePixelRect(x: x0, y: band.frameY(ofRow: row),
                           width: max(0, x1 - x0), height: rowHeight)
        }

        if start.row == end.row {
            return [rowRect(start.row, startX, endX)]
        }
        var rects = [rowRect(start.row, startX, rightX)]
        let middleRows = end.row - start.row - 1
        if middleRows > 0 {
            rects.append(FramePixelRect(
                x: leftX, y: band.frameY(ofRow: start.row + 1),
                width: max(0, rightX - leftX), height: rowHeight * middleRows))
        }
        rects.append(rowRect(end.row, leftX, endX))
        return rects
    }

    /// A cell address within a band.
    struct CellAddress: Equatable { var column: Int; var row: Int }

    /// A rows text-flow: the cells from `start` to `end` in reading order, plus
    /// the rectangle to request from the server (the flow's bounding box, full
    /// width so whole intermediate rows are read).
    struct RowsFlow: Equatable {
        var start: CellAddress
        var end: CellAddress
        var requestRegion: FramePixelRect
    }

    /// Resolve the anchor and focus to a rows flow. `start` is the earlier of
    /// the two cells in reading order (top-then-left), `end` the later, so a
    /// drag in either direction reads the same.
    static func rowsFlow(anchor: CGPoint, focus: CGPoint,
                         band: ScreenBandGeometry, frameWidth: Int) -> RowsFlow {
        let a = CellAddress(column: band.column(atFrameX: Int(anchor.x)),
                            row: band.row(atFrameY: Int(anchor.y)))
        let b = CellAddress(column: band.column(atFrameX: Int(focus.x)),
                            row: band.row(atFrameY: Int(focus.y)))
        let (start, end) = orderedInReadingOrder(a, b)

        let top = band.frameY(ofRow: start.row)
        let bottom = band.frameY(ofRow: end.row) + band.cellHeight
        let width = max(frameWidth, band.frameX(ofColumn: end.column) + band.cellWidth)
        let region = FramePixelRect(x: 0, y: top, width: width, height: max(0, bottom - top))
        return RowsFlow(start: start, end: end, requestRegion: region)
    }

    /// The two cells ordered so the first precedes the second in reading order.
    static func orderedInReadingOrder(_ a: CellAddress, _ b: CellAddress)
        -> (CellAddress, CellAddress) {
        if a.row != b.row { return a.row < b.row ? (a, b) : (b, a) }
        return a.column <= b.column ? (a, b) : (b, a)
    }

    /// Trim the runs the server returned for the flow's bounding rectangle down
    /// to the flow itself: the first row keeps cells from `start.column` on, the
    /// last row keeps cells up to `end.column`, whole rows between are kept
    /// entirely. The runs' cells are evenly spaced at `cellWidth`, so which
    /// column a character sits in is arithmetic.
    static func trimToFlow(runs: [ScreenTextRun], band: ScreenBandGeometry,
                           flow: RowsFlow) -> (runs: [ScreenTextRun], text: String) {
        var kept: [(row: Int, x: Int, run: ScreenTextRun)] = []

        for run in runs {
            let row = band.row(atFrameY: run.bounds.y)
            guard row >= flow.start.row, row <= flow.end.row else { continue }
            let step = run.cellWidth > 0 ? run.cellWidth : band.columnPitch
            guard step > 0 else {
                // No cell step to reason about; keep the whole run on kept rows.
                kept.append((row, run.bounds.x, run))
                continue
            }
            let characters = Array(run.text)
            var trimmedText = ""
            var trimmedCells: [ScreenTextRunCell] = []
            var firstKeptIndex: Int?
            for (index, character) in characters.enumerated() {
                let centreX = run.bounds.x + index * step + step / 2
                let column = band.column(atFrameX: centreX)
                let afterStart = row > flow.start.row || column >= flow.start.column
                let beforeEnd = row < flow.end.row || column <= flow.end.column
                if afterStart && beforeEnd {
                    if firstKeptIndex == nil { firstKeptIndex = index }
                    trimmedText.append(character)
                    // Cells correspond one-to-one with characters, so a kept
                    // character keeps its cell and thus its matched status.
                    if index < run.cells.count { trimmedCells.append(run.cells[index]) }
                }
            }
            guard let firstIndex = firstKeptIndex, !trimmedText.isEmpty else { continue }
            var bounds = run.bounds
            bounds.x = run.bounds.x + firstIndex * step
            bounds.width = trimmedText.count * step
            kept.append((row, bounds.x, ScreenTextRun(
                text: trimmedText, bounds: bounds,
                cellWidth: run.cellWidth, cellHeight: run.cellHeight,
                cells: trimmedCells)))
        }

        // Reading order, then join runs on a row into a line and rows with LF.
        kept.sort { $0.row != $1.row ? $0.row < $1.row : $0.x < $1.x }
        let orderedRuns = kept.map { $0.run }

        var lines: [String] = []
        var currentRow: Int?
        for entry in kept {
            if entry.row != currentRow {
                lines.append(entry.run.text)
                currentRow = entry.row
            } else {
                lines[lines.count - 1] += entry.run.text
            }
        }
        return (orderedRuns, lines.joined(separator: "\n"))
    }
}

// MARK: - Live adapters

extension VideoClient: ScreenTextService {}

extension VideoClient: DisplayFreezer {
    func freezeDisplay() { setDisplayFrozen(true) }
    func resumeDisplay() { setDisplayFrozen(false) }

    /// Push the server's captured still through the same path a streamed frame
    /// takes, so the view shows exactly the frame the reads are made against.
    /// The display is already frozen, so nothing overwrites it.
    func showHeldFrame(_ frame: HeldFrame) {
        renderer?.updateFrame(
            data: frame.pixels,
            width: frame.width,
            height: frame.height,
            displayWidth: frame.displayWidth,
            displayHeight: frame.displayHeight,
            leftBorder: frame.leftBorder,
            rightBorder: frame.rightBorder,
            topBorder: frame.topBorder,
            bottomBorder: frame.bottomBorder,
            interlaced: frame.interlaced,
            regions: frame.regions)
    }
}

extension MetalRenderer: RenderGeometrySource {}

/// Writes selected text to the general pasteboard. Lines arrive joined with LF,
/// which is what the macOS pasteboard wants.
@MainActor
final class SystemSelectionPasteboard: SelectionPasteboard {
    func writeText(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }
}

// MARK: - Menu plumbing

/// Reaches the frontmost machine window's selection coordinator from the Edit
/// menu, the same pattern PasteCoordinator uses for "Paste at Full Speed".
struct SelectionCoordinatorFocusedValueKey: FocusedValueKey {
    typealias Value = SelectionCoordinator
}

extension FocusedValues {
    var selectionCoordinator: SelectionCoordinator? {
        get { self[SelectionCoordinatorFocusedValueKey.self] }
        set { self[SelectionCoordinatorFocusedValueKey.self] = newValue }
    }
}
