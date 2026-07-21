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

import XCTest
@testable import Beebium

@MainActor
final class SelectionCoordinatorTests: XCTestCase {

    // MARK: - Pure geometry (no window, no server)

    /// A plain MODE-0-like grid: 8-pixel cells on an 8-pixel pitch from the
    /// origin, so column c spans x in [8c, 8c+8) and row r spans y in [8r, 8r+8).
    private let band = ScreenBandGeometry(
        top: 0, bottom: 256, cellWidth: 8, cellHeight: 8,
        columnPitch: 8, rowPitch: 8, originX: 0, originY: 0)

    func testBoundingRectIsOrientationIndependent() {
        let rect = SelectionCoordinator.boundingRect(
            CGPoint(x: 30, y: 18), CGPoint(x: 20, y: 2))
        XCTAssertEqual(rect, FramePixelRect(x: 20, y: 2, width: 10, height: 16))
    }

    func testSnappedRectangleExpandsToWholeCells() {
        let rect = SelectionCoordinator.snappedRectangle(
            anchor: CGPoint(x: 20, y: 2), focus: CGPoint(x: 30, y: 18), band: band)
        // cols 2..3, rows 0..2 -> x 16, y 0, width 2 cells, height 3 cells.
        XCTAssertEqual(rect, FramePixelRect(x: 16, y: 0, width: 16, height: 24))
    }

    func testSnappedRectangleFallsBackWithoutAGrid() {
        let rect = SelectionCoordinator.snappedRectangle(
            anchor: CGPoint(x: 20, y: 2), focus: CGPoint(x: 30, y: 18), band: nil)
        XCTAssertEqual(rect, FramePixelRect(x: 20, y: 2, width: 10, height: 16))
    }

    func testReadingOrderPutsTheEarlierCellFirst() {
        typealias Cell = SelectionCoordinator.CellAddress
        // Different rows: the higher row wins regardless of column.
        XCTAssertEqual(
            SelectionCoordinator.orderedInReadingOrder(
                Cell(column: 5, row: 2), Cell(column: 1, row: 0)).0,
            Cell(column: 1, row: 0))
        // Same row: the smaller column wins.
        XCTAssertEqual(
            SelectionCoordinator.orderedInReadingOrder(
                Cell(column: 5, row: 1), Cell(column: 2, row: 1)).0,
            Cell(column: 2, row: 1))
    }

    func testRowsFlowRequestsFullWidthAcrossTheRows() {
        let flow = SelectionCoordinator.rowsFlow(
            anchor: CGPoint(x: 20, y: 2), focus: CGPoint(x: 30, y: 18),
            band: band, frameWidth: 640)
        XCTAssertEqual(flow.start, SelectionCoordinator.CellAddress(column: 2, row: 0))
        XCTAssertEqual(flow.end, SelectionCoordinator.CellAddress(column: 3, row: 2))
        // Full band width, rows 0..2 (bottom = row 2 top 16 + cell 8 = 24).
        XCTAssertEqual(flow.requestRegion, FramePixelRect(x: 0, y: 0, width: 640, height: 24))
    }

    func testTrimToFlowKeepsTheRaggedFirstAndLastRows() {
        // Flow from (col 2, row 0) to (col 3, row 2): the first row keeps cells
        // from column 2 on, the last row up to column 3, the middle row whole.
        let flow = SelectionCoordinator.RowsFlow(
            start: .init(column: 2, row: 0),
            end: .init(column: 3, row: 2),
            requestRegion: FramePixelRect(x: 0, y: 0, width: 640, height: 24))
        let runs = [
            ScreenTextRun(text: "ABCDEF",
                          bounds: FramePixelRect(x: 0, y: 0, width: 48, height: 8),
                          cellWidth: 8, cellHeight: 8),
            ScreenTextRun(text: "GHIJ",
                          bounds: FramePixelRect(x: 0, y: 8, width: 32, height: 8),
                          cellWidth: 8, cellHeight: 8),
            ScreenTextRun(text: "KLMNOP",
                          bounds: FramePixelRect(x: 0, y: 16, width: 48, height: 8),
                          cellWidth: 8, cellHeight: 8),
        ]
        let trimmed = SelectionCoordinator.trimToFlow(runs: runs, band: band, flow: flow)
        XCTAssertEqual(trimmed.text, "CDEF\nGHIJ\nKLMN")
        XCTAssertEqual(trimmed.runs.count, 3)
        // The first row's kept run starts at column 2 (x = 16).
        XCTAssertEqual(trimmed.runs.first?.bounds.x, 16)
        XCTAssertEqual(trimmed.runs.first?.text, "CDEF")
    }

    func testTrimToFlowDropsRunsOutsideTheRowRange() {
        let flow = SelectionCoordinator.RowsFlow(
            start: .init(column: 0, row: 1),
            end: .init(column: 3, row: 1),
            requestRegion: FramePixelRect(x: 0, y: 8, width: 640, height: 8))
        let runs = [
            ScreenTextRun(text: "ABOVE",
                          bounds: FramePixelRect(x: 0, y: 0, width: 40, height: 8),
                          cellWidth: 8, cellHeight: 8),
            ScreenTextRun(text: "WXYZ",
                          bounds: FramePixelRect(x: 0, y: 8, width: 32, height: 8),
                          cellWidth: 8, cellHeight: 8),
        ]
        let trimmed = SelectionCoordinator.trimToFlow(runs: runs, band: band, flow: flow)
        XCTAssertEqual(trimmed.text, "WXYZ")
    }

    // MARK: - Selection range (the lighter fill)

    func testRowsRangeFlowsToTheScreenEdges() {
        // Anchor col 2 row 0, focus col 3 row 2: first row from col 2 to the
        // right edge, the whole middle row, the last row from the left edge to
        // col 3. Full width is 640 (80 columns of 8).
        let rects = SelectionCoordinator.rangeRects(
            interpretation: .rows,
            anchor: CGPoint(x: 20, y: 2), focus: CGPoint(x: 30, y: 18),
            band: band, frameWidth: 640)
        XCTAssertEqual(rects, [
            FramePixelRect(x: 16, y: 0, width: 624, height: 8),   // col 2 -> right
            FramePixelRect(x: 0, y: 8, width: 640, height: 8),    // whole middle row
            FramePixelRect(x: 0, y: 16, width: 32, height: 8),    // left -> col 3
        ])
    }

    func testRowsRangeOnASingleRowIsJustTheSegment() {
        let rects = SelectionCoordinator.rangeRects(
            interpretation: .rows,
            anchor: CGPoint(x: 20, y: 2), focus: CGPoint(x: 52, y: 2),
            band: band, frameWidth: 640)
        // Columns 2..6 on row 0: x 16, through the end of column 6 (56).
        XCTAssertEqual(rects, [FramePixelRect(x: 16, y: 0, width: 40, height: 8)])
    }

    func testRowsRangeSkipsTheMiddleBlockWhenRowsAreAdjacent() {
        let rects = SelectionCoordinator.rangeRects(
            interpretation: .rows,
            anchor: CGPoint(x: 20, y: 2), focus: CGPoint(x: 30, y: 10),
            band: band, frameWidth: 640)
        // Rows 0 and 1 only: first row to the right edge, last row from the left.
        XCTAssertEqual(rects, [
            FramePixelRect(x: 16, y: 0, width: 624, height: 8),
            FramePixelRect(x: 0, y: 8, width: 32, height: 8),
        ])
    }

    func testRectangleRangeIsTheSnappedBlock() {
        let rects = SelectionCoordinator.rangeRects(
            interpretation: .rectangle,
            anchor: CGPoint(x: 20, y: 2), focus: CGPoint(x: 30, y: 18),
            band: band, frameWidth: 640)
        XCTAssertEqual(rects, [FramePixelRect(x: 16, y: 0, width: 16, height: 24)])
    }

    func testAnywhereHasNoRange() {
        let rects = SelectionCoordinator.rangeRects(
            interpretation: .anywhere,
            anchor: CGPoint(x: 20, y: 2), focus: CGPoint(x: 30, y: 18),
            band: band, frameWidth: 640)
        XCTAssertTrue(rects.isEmpty)
    }

    func testRangeIsEmptyWithoutAGrid() {
        let rects = SelectionCoordinator.rangeRects(
            interpretation: .rows,
            anchor: CGPoint(x: 20, y: 2), focus: CGPoint(x: 30, y: 18),
            band: nil, frameWidth: 640)
        XCTAssertTrue(rects.isEmpty)
    }

    // MARK: - Interpretation to search

    func testInterpretationChoosesTheSearch() {
        XCTAssertEqual(ScreenSelectionInterpretation.rows.search, .aligned)
        XCTAssertEqual(ScreenSelectionInterpretation.rectangle.search, .aligned)
        XCTAssertEqual(ScreenSelectionInterpretation.anywhere.search, .anywhere)
    }

    // MARK: - Doubles

    final class FakeTextService: ScreenTextService {
        var bands: [ScreenBandGeometry] = []
        var reading = ScreenTextReading(
            supported: true, text: "HELLO", unreadableCells: 0, ambiguousCells: 0, runs: [])

        struct Call: Equatable {
            var region: FramePixelRect?
            var search: ScreenTextSearchMode
            var layout: ScreenTextJoinLayout
        }
        var calls: [Call] = []
        var searchesUsed: [ScreenTextSearchMode] { calls.map { $0.search } }

        func screenGeometry() async -> [ScreenBandGeometry] { bands }
        func screenText(region: FramePixelRect?, search: ScreenTextSearchMode,
                        layout: ScreenTextJoinLayout) async -> ScreenTextReading? {
            calls.append(Call(region: region, search: search, layout: layout))
            return reading
        }
    }

    final class FakeGeometrySource: RenderGeometrySource {
        var geometry: ScreenRenderGeometry?
        func captureRenderGeometry() -> ScreenRenderGeometry? { geometry }
    }

    final class FakeFreezer: DisplayFreezer {
        var frozen = 0
        var resumed = 0
        func freezeDisplay() { frozen += 1 }
        func resumeDisplay() { resumed += 1 }
    }

    final class FakePasteboard: SelectionPasteboard {
        var written: [String] = []
        func writeText(_ text: String) { written.append(text) }
    }

    private var retained: [AnyObject] = []

    /// A geometry with no letterbox, so a normalised point maps cleanly to a
    /// frame pixel (matches ScreenSelectionGeometryTests' filling geometry).
    private func fillingGeometry() -> ScreenRenderGeometry {
        ScreenRenderGeometry(
            drawableSize: CGSize(width: 1250, height: 1000),
            textureSize: CGSize(width: 640, height: 256),
            displaySize: CGSize(width: 640, height: 256),
            totalSize: CGSize(width: 640, height: 256),
            borderOffset: .zero, parScale: 1.0, interlaced: false,
            edgeMargin: 0,
            regions: [DisplayRegion(startLine: 0, endLine: 256, pixelWidth: 640)])
    }

    private func makeCoordinator(bands: [ScreenBandGeometry] = [])
        -> (SelectionCoordinator, FakeTextService, FakeGeometrySource,
            FakeFreezer, FakePasteboard) {
        let text = FakeTextService()
        text.bands = bands
        let geometry = FakeGeometrySource()
        geometry.geometry = fillingGeometry()
        let freezer = FakeFreezer()
        let pasteboard = FakePasteboard()
        retained.append(text)
        retained.append(geometry)
        retained.append(freezer)
        retained.append(pasteboard)

        let coordinator = SelectionCoordinator()
        coordinator.textService = text
        coordinator.geometrySource = geometry
        coordinator.freezer = freezer
        coordinator.pasteboard = pasteboard
        return (coordinator, text, geometry, freezer, pasteboard)
    }

    // MARK: - Gesture lifecycle

    func testBeginFreezesAndStartsSelecting() {
        let (coordinator, _, _, freezer, _) = makeCoordinator()
        XCTAssertFalse(coordinator.isSelecting)
        XCTAssertFalse(coordinator.ownsQualifierModifiers)

        coordinator.begin(atNormalizedPoint: CGPoint(x: 0.5, y: 0.5),
                           interpretation: .rows)

        XCTAssertTrue(coordinator.isSelecting)
        XCTAssertTrue(coordinator.ownsQualifierModifiers)
        XCTAssertEqual(freezer.frozen, 1)
        XCTAssertNotNil(coordinator.frozenGeometry)
    }

    func testBeginWithoutGeometryDoesNothing() {
        let (coordinator, _, geometry, freezer, _) = makeCoordinator()
        geometry.geometry = nil
        coordinator.begin(atNormalizedPoint: CGPoint(x: 0.5, y: 0.5),
                           interpretation: .rows)
        XCTAssertFalse(coordinator.isSelecting)
        XCTAssertEqual(freezer.frozen, 0)
    }

    func testCancelResumesAndClears() {
        let (coordinator, _, _, freezer, _) = makeCoordinator()
        coordinator.begin(atNormalizedPoint: CGPoint(x: 0.5, y: 0.5),
                           interpretation: .rows)
        coordinator.cancel()
        XCTAssertFalse(coordinator.isSelecting)
        XCTAssertEqual(freezer.resumed, 1)
        XCTAssertNil(coordinator.frozenGeometry)
        // Idempotent: a plain click and an Escape can both arrive, but only the
        // first does anything.
        coordinator.cancel()
        XCTAssertEqual(freezer.resumed, 1)
    }

    func testRowsShowsItsRangeAndNotTheMarquee() async {
        // The snapped range traces a rows selection, so the raw marquee outline
        // is suppressed to avoid cluttering it.
        let (coordinator, _, _, _, _) = makeCoordinator(bands: [band])
        coordinator.begin(atNormalizedPoint: CGPoint(x: 0.3, y: 0.3),
                          interpretation: .rows)
        await coordinator.loadBandsAndRefresh()
        coordinator.update(toNormalizedPoint: CGPoint(x: 0.7, y: 0.7))

        XCTAssertNil(coordinator.marqueeRect)
        XCTAssertFalse(coordinator.rangeRects.isEmpty)
    }

    func testAnywhereShowsTheMarqueeAndHasNoRange() async {
        // Anywhere has no grid, so nothing snaps; the marquee outline is the
        // only thing that shows how far the selection reaches.
        let (coordinator, _, _, _, _) = makeCoordinator(bands: [band])
        coordinator.begin(atNormalizedPoint: CGPoint(x: 0.3, y: 0.3),
                          interpretation: .anywhere)
        await coordinator.loadBandsAndRefresh()
        coordinator.update(toNormalizedPoint: CGPoint(x: 0.7, y: 0.7))

        XCTAssertNotNil(coordinator.marqueeRect)
        XCTAssertTrue(coordinator.rangeRects.isEmpty)
    }

    // MARK: - Copy

    func testCopyWithNoSelectionCopiesWholeScreenAligned() async {
        let (coordinator, text, _, _, pasteboard) = makeCoordinator()
        let copied = await coordinator.copy(.rows)
        XCTAssertEqual(copied, "HELLO")
        XCTAssertEqual(pasteboard.written, ["HELLO"])
        XCTAssertEqual(text.calls.count, 1)
        XCTAssertNil(text.calls[0].region, "whole screen: no region")
        XCTAssertEqual(text.calls[0].search, .aligned)
    }

    func testCopyRectangleUsesTheAlignedSearch() async {
        let (coordinator, text, _, _, pasteboard) = makeCoordinator()
        coordinator.begin(atNormalizedPoint: CGPoint(x: 0.2, y: 0.2),
                          interpretation: .rectangle)
        coordinator.update(toNormalizedPoint: CGPoint(x: 0.8, y: 0.8))
        text.calls.removeAll()

        let copied = await coordinator.copy(.rectangle)
        XCTAssertEqual(copied, "HELLO")
        XCTAssertEqual(pasteboard.written.last, "HELLO")
        XCTAssertTrue(text.calls.contains { $0.search == .aligned })
        // A region was sent (not the whole screen).
        XCTAssertNotNil(text.calls.last?.region ?? nil)
    }

    func testCopyAnywhereUsesTheAnywhereSearch() async {
        let (coordinator, text, _, _, pasteboard) = makeCoordinator()
        coordinator.begin(atNormalizedPoint: CGPoint(x: 0.2, y: 0.2),
                          interpretation: .anywhere)
        coordinator.update(toNormalizedPoint: CGPoint(x: 0.8, y: 0.8))
        text.calls.removeAll()

        let copied = await coordinator.copy(.anywhere)
        XCTAssertEqual(copied, "HELLO")
        XCTAssertEqual(pasteboard.written.last, "HELLO")
        XCTAssertTrue(text.calls.contains { $0.search == .anywhere })
    }

    func testCopyRowsRequestsFullWidthWhenAGridIsKnown() async {
        let (coordinator, text, _, _, _) = makeCoordinator(bands: [band])
        coordinator.begin(atNormalizedPoint: CGPoint(x: 0.2, y: 0.2),
                          interpretation: .rows)
        await coordinator.loadBandsAndRefresh()
        coordinator.update(toNormalizedPoint: CGPoint(x: 0.8, y: 0.8))
        text.calls.removeAll()

        _ = await coordinator.copy(.rows)
        // A rows flow reads whole intermediate rows, so its region starts at x 0.
        XCTAssertEqual(text.calls.last?.region?.x, 0)
        XCTAssertEqual(text.calls.last?.search, .aligned)
    }

    func testCopyReturnsNilWhenNothingWasFound() async {
        let (coordinator, text, _, _, pasteboard) = makeCoordinator()
        text.reading = ScreenTextReading(
            supported: true, text: "", unreadableCells: 0, ambiguousCells: 0, runs: [])
        let copied = await coordinator.copy(.rows)
        XCTAssertNil(copied)
        XCTAssertTrue(pasteboard.written.isEmpty, "nothing put on the clipboard")
    }
}
