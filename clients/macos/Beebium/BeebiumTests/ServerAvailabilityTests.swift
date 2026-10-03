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

import XCTest
@testable import Beebium

final class ServerAvailabilityTests: XCTestCase {

    // MARK: - Resolution from liveness and reconnect phase

    func testIdleReconnectPhaseFollowsLiveness() {
        XCTAssertEqual(ServerAvailability.resolve(liveness: .active, reconnectPhase: .idle), .live)
        XCTAssertEqual(ServerAvailability.resolve(liveness: .died, reconnectPhase: .idle), .died)
        XCTAssertEqual(ServerAvailability.resolve(liveness: .stopped, reconnectPhase: .idle), .stopped)
        XCTAssertEqual(ServerAvailability.resolve(liveness: .unreachable, reconnectPhase: .idle),
                       .unreachable)
    }

    func testActiveReconnectOutranksEveryLiveness() {
        let all: [SystemClient.Liveness] = [.active, .died, .stopped, .unreachable]
        for liveness in all {
            XCTAssertEqual(
                ServerAvailability.resolve(liveness: liveness,
                                           reconnectPhase: .reconnecting(attempt: 3)),
                .reconnecting)
            XCTAssertEqual(
                ServerAvailability.resolve(liveness: liveness, reconnectPhase: .givenUp),
                .reconnectFailed)
        }
    }

    func testOnlyLiveIsLive() {
        XCTAssertTrue(ServerAvailability.live.isLive)
        for availability in Self.notLive {
            XCTAssertFalse(availability.isLive, "\(availability)")
        }
    }

    func testLiveHasNothingToAnnounce() {
        XCTAssertNil(ServerAvailability.live.title)
        XCTAssertNil(ServerAvailability.live.detail)
        XCTAssertNil(ServerAvailability.live.systemImage)
    }

    func testEveryNonLiveStateAnnouncesItself() {
        for availability in Self.notLive {
            XCTAssertNotNil(availability.title, "\(availability)")
            XCTAssertNotNil(availability.detail, "\(availability)")
            XCTAssertNotNil(availability.systemImage, "\(availability)")
        }
    }

    func testTerminalStatesAreToldApart() {
        XCTAssertEqual(ServerAvailability.died.title, "Emulator stopped unexpectedly")
        XCTAssertEqual(ServerAvailability.stopped.title, "Emulator stopped")
        XCTAssertEqual(ServerAvailability.unreachable.title, "Connection lost")
    }

    // MARK: - Per-mode stale presentation

    func testLiveServerLeavesEveryModeUntouched() {
        for mode in SidebarMode.allCases {
            let presentation = SidebarStalePresentation(mode: mode, availability: .live)
            XCTAssertNil(presentation.banner, mode.label)
            XCTAssertTrue(presentation.isContentEnabled, mode.label)
            XCTAssertFalse(presentation.isContentDimmed, mode.label)
        }
    }

    func testServerBackedModesGoStaleWhenTheServerIsNotLive() {
        let serverBacked: [SidebarMode] = [.storage, .memory, .peripherals, .sound,
                                           .processor, .network]
        for mode in serverBacked {
            for availability in Self.notLive {
                let presentation = SidebarStalePresentation(mode: mode,
                                                            availability: availability)
                XCTAssertEqual(presentation.banner, availability, mode.label)
                XCTAssertFalse(presentation.isContentEnabled, mode.label)
                XCTAssertTrue(presentation.isContentDimmed, mode.label)
            }
        }
    }

    func testAppOnlyModesStayUsableWhenTheServerIsNotLive() {
        let appOnly: [SidebarMode] = [.video, .keyboard]
        for mode in appOnly {
            for availability in Self.notLive {
                let presentation = SidebarStalePresentation(mode: mode,
                                                            availability: availability)
                XCTAssertNil(presentation.banner, mode.label)
                XCTAssertTrue(presentation.isContentEnabled, mode.label)
                XCTAssertFalse(presentation.isContentDimmed, mode.label)
            }
        }
    }

    static let notLive: [ServerAvailability] = [.died, .stopped, .unreachable,
                                                .reconnecting, .reconnectFailed]
}

final class NetworkSidebarPresentationTests: XCTestCase {

    /// A healthy Econet machine with one transport panel.
    private func healthy() -> NetworkSidebarPresentation.Inputs {
        .init(availability: .live,
              econetIsLoaded: true,
              econetIsStale: false,
              econetEnabled: true,
              econetConnected: true,
              transportsHaveListed: true,
              transportsAttempted: true,
              transportPanelIDs: ["aun-0"])
    }

    func testHealthyMachineShowsLiveStatusAndEnabledPanel() {
        let presentation = NetworkSidebarPresentation(healthy())
        XCTAssertEqual(presentation.content, .status)
        XCTAssertEqual(presentation.linkState, .connected)
        XCTAssertFalse(presentation.isStatusStale)
        XCTAssertTrue(presentation.isStationEditorEnabled)
        XCTAssertEqual(presentation.transportArea, .panels(ids: ["aun-0"], isEnabled: true))
    }

    func testUnpluggedLinkReadsDisconnected() {
        var inputs = healthy()
        inputs.econetConnected = false
        XCTAssertEqual(NetworkSidebarPresentation(inputs).linkState, .disconnected)
    }

    /// The reported defect: cached "connected" must never be shown, the editor
    /// must not open, and the last panel stays, disabled, for a server that is
    /// not live -- whatever the cached state claims.
    func testServerNotLiveNeverReadsConnected() {
        for availability in ServerAvailabilityTests.notLive {
            var inputs = healthy()
            inputs.availability = availability
            let presentation = NetworkSidebarPresentation(inputs)
            XCTAssertEqual(presentation.content, .status, "\(availability)")
            XCTAssertEqual(presentation.linkState, .unknown, "\(availability)")
            XCTAssertNotEqual(presentation.linkState.label, "Connected", "\(availability)")
            XCTAssertTrue(presentation.isStatusStale, "\(availability)")
            XCTAssertFalse(presentation.isStationEditorEnabled, "\(availability)")
            XCTAssertEqual(presentation.transportArea,
                           .panels(ids: ["aun-0"], isEnabled: false), "\(availability)")
        }
    }

    func testTransportsNeverListedOnDeadServerSaysUnavailable() {
        var inputs = healthy()
        inputs.availability = .died
        inputs.transportsHaveListed = false
        inputs.transportPanelIDs = []
        XCTAssertEqual(NetworkSidebarPresentation(inputs).transportArea, .unavailable)

        // Even before the failed attempt has been recorded.
        inputs.transportsAttempted = false
        XCTAssertEqual(NetworkSidebarPresentation(inputs).transportArea, .unavailable)
    }

    func testFailedListOnLiveServerSaysUnavailable() {
        var inputs = healthy()
        inputs.transportsHaveListed = false
        inputs.transportsAttempted = true
        inputs.transportPanelIDs = []
        XCTAssertEqual(NetworkSidebarPresentation(inputs).transportArea, .unavailable)
    }

    func testListStillLoadingOnLiveServerShowsNothing() {
        var inputs = healthy()
        inputs.transportsHaveListed = false
        inputs.transportsAttempted = false
        inputs.transportPanelIDs = []
        XCTAssertEqual(NetworkSidebarPresentation(inputs).transportArea, TransportArea.none)
    }

    func testServerThatListedNoPanelsShowsNothingEvenWhenDead() {
        var inputs = healthy()
        inputs.transportPanelIDs = []
        XCTAssertEqual(NetworkSidebarPresentation(inputs).transportArea, TransportArea.none)
        inputs.availability = .died
        XCTAssertEqual(NetworkSidebarPresentation(inputs).transportArea, TransportArea.none)
    }

    func testStatusStreamLostOnLiveServerIsStale() {
        var inputs = healthy()
        inputs.econetIsStale = true
        inputs.econetConnected = false
        let presentation = NetworkSidebarPresentation(inputs)
        XCTAssertEqual(presentation.content, .status)
        XCTAssertEqual(presentation.linkState, .unknown)
        XCTAssertTrue(presentation.isStatusStale)
        XCTAssertFalse(presentation.isStationEditorEnabled)
        XCTAssertEqual(presentation.transportArea, .panels(ids: ["aun-0"], isEnabled: false))
    }

    func testNoSnapshotOnLiveServerIsLoading() {
        var inputs = healthy()
        inputs.econetIsLoaded = false
        let presentation = NetworkSidebarPresentation(inputs)
        XCTAssertEqual(presentation.content, .loading)
        XCTAssertFalse(presentation.isStationEditorEnabled)
    }

    func testNoSnapshotOnDeadServerIsUnavailableNotLoading() {
        var inputs = healthy()
        inputs.econetIsLoaded = false
        inputs.availability = .died
        let presentation = NetworkSidebarPresentation(inputs)
        XCTAssertEqual(presentation.content, .unavailable)
        XCTAssertFalse(presentation.isStationEditorEnabled)
    }

    func testNotFittedMachineHasNoStationEditor() {
        var inputs = healthy()
        inputs.econetEnabled = false
        let presentation = NetworkSidebarPresentation(inputs)
        XCTAssertEqual(presentation.content, .notFitted)
        XCTAssertFalse(presentation.isStationEditorEnabled)
    }

    private typealias TransportArea = NetworkSidebarPresentation.TransportArea
}
