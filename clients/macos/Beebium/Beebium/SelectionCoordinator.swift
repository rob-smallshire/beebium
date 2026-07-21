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
    func screenGeometry() async -> [ScreenBandGeometry]
    func screenText(region: FramePixelRect?,
                    search: ScreenTextSearchMode,
                    layout: ScreenTextJoinLayout) async -> ScreenTextReading?
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

        frozenGeometry = geometry
        anchorFrame = hit.pixel
        focusFrame = hit.pixel
        self.interpretation = interpretation
        isSelecting = true
        freezer?.freezeDisplay()

        // The grid is fixed for the frozen frame; fetch it once. Highlights wait
        // for it so a rows/rectangle selection can snap.
        Task { [weak self] in await self?.loadBandsAndRefresh() }
    }

    /// Fetch the character grid for the frozen frame and refresh the highlights.
    /// Split out from `begin` so it can be driven deterministically in tests.
    func loadBandsAndRefresh() async {
        let bands = await textService?.screenGeometry() ?? []
        guard isSelecting else { return }
        self.bands = bands
        refreshHighlights()
    }

    /// Extend the selection to a new normalised view point.
    func update(toNormalizedPoint point: CGPoint) {
        guard isSelecting, let geometry = frozenGeometry else { return }
        guard let hit = ScreenCoordinateMapper.framePixel(
            normalizedPoint: point, geometry: geometry) else { return }
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
        anchorFrame = nil
        focusFrame = nil
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

    /// The rectangles to highlight for a set of runs: each matched cell, or the
    /// whole run's bounds when it reports no cells (teletext, whose cells are all
    /// exact matches). Unmatched cells -- ink the font could not identify -- are
    /// left dark, so the overlay shows only what was actually read, not a line
    /// of spaces dressed up as a successful copy.
    static func matchedCellRects(_ runs: [ScreenTextRun]) -> [FramePixelRect] {
        runs.flatMap { run in
            run.cells.isEmpty
                ? [run.bounds]
                : run.cells.filter { $0.matched }.map { $0.bounds }
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
            region: nil, search: .aligned, layout: .rows) else { return nil }
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
                region: region, search: .anywhere, layout: .rows) else { return nil }
            return (reading.runs, reading.text)

        case .rectangle:
            let region = Self.snappedRectangle(anchor: anchor, focus: focus, band: band)
            guard let reading = await textService?.screenText(
                region: region, search: .aligned, layout: .rows) else { return nil }
            return (reading.runs, reading.text)

        case .rows:
            guard let band, band.isGridded else {
                // No grid to flow along: fall back to the plain rectangle.
                let region = Self.boundingRect(anchor, focus)
                guard let reading = await textService?.screenText(
                    region: region, search: .aligned, layout: .rows) else { return nil }
                return (reading.runs, reading.text)
            }
            let flow = Self.rowsFlow(anchor: anchor, focus: focus, band: band,
                                     frameWidth: Int(frozenGeometry?.textureSize.width ?? 0))
            guard let reading = await textService?.screenText(
                region: flow.requestRegion, search: .aligned, layout: .rows) else {
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
