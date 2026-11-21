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
import XCTest
@testable import Beebium

/// Real-Master integration test for the cartridge sockets (spec 5.9, #300/#301).
/// The unit tests use inline fixtures; this runs the real Master 128 server from
/// the build tree through the client's own CLI seam, so it catches any drift
/// between the landed server schema/resolver and what the configurator expects.
///
/// It spawns the server, so it is skipped unless BEEBIUM_CARTRIDGE_INTEGRATION=1
/// (pass it as TEST_RUNNER_BEEBIUM_CARTRIDGE_INTEGRATION=1 to xcodebuild, which
/// forwards it into the test host). Run (from clients/macos/Beebium):
///   TEST_RUNNER_BEEBIUM_CARTRIDGE_INTEGRATION=1 xcodebuild test -scheme Beebium \
///     -configuration Debug -destination 'platform=macOS' \
///     -only-testing:BeebiumTests/CartridgeIntegrationTests
@MainActor
final class CartridgeIntegrationTests: XCTestCase {

    func testCartridgeSchemaStatesAndResolution() async throws {
        guard ProcessInfo.processInfo.environment["BEEBIUM_CARTRIDGE_INTEGRATION"] == "1" else {
            throw XCTSkip("set BEEBIUM_CARTRIDGE_INTEGRATION=1 to run the cartridge integration test")
        }
        guard let server = Self.findServer() else {
            throw XCTSkip("beebium-master-128 executable not found")
        }
        let cli = CoreCLI()

        // 1. Schema: the two cartridge slots' banks carry provided_by_point and
        //    only "empty"; a motherboard bank does not.
        let sidewaysOpt: SidewaysSchemaSection? =
            await cli.schemaSection("sideways_bank", executablePath: server)
        let sideways = try XCTUnwrap(sidewaysOpt)
        func socket(bank: Int) throws -> SidewaysSocketSchema {
            try XCTUnwrap(sideways.sockets.first { $0.slots.contains(bank) },
                          "a socket for bank \(bank)")
        }
        XCTAssertEqual(try socket(bank: 0).providedByPoint, "cartridge-rear")
        XCTAssertEqual(try socket(bank: 1).providedByPoint, "cartridge-rear")
        XCTAssertEqual(try socket(bank: 2).providedByPoint, "cartridge-front")
        XCTAssertEqual(try socket(bank: 3).providedByPoint, "cartridge-front")
        XCTAssertEqual(try socket(bank: 0).capabilities, ["empty"])
        XCTAssertNil(try socket(bank: 9).providedByPoint, "a MOS bank is not cartridge-provided")

        // The cartridge manifest declares its provision and carries no image
        // parameters -- so its Peripherals view shows no image fields.
        let extensionsOpt: ExtensionsSchemaSection? =
            await cli.schemaSection("extensions", executablePath: server)
        let extensions = try XCTUnwrap(extensionsOpt)
        XCTAssertEqual(extensions.providesSideways(forExtension: "acorn-rom-cartridge"),
                       ProvidesSideways(banks: 2, bankCapabilities: ["rom", "empty"]))
        XCTAssertTrue(extensions.manifest(forExtension: "acorn-rom-cartridge").parameters.isEmpty,
                      "the cartridge manifest must carry no image parameters")
        XCTAssertEqual(extensions.displayName(forExtension: "acorn-rom-cartridge"),
                       "Acorn ROM Cartridge")

        // 2. The projection over the real schema: bank 0 is "No cartridge" until a
        //    cartridge is fitted at the rear slot, then an editable ROM/empty bank.
        let carts = CartridgeSockets(extensionsSchema: extensions)
        let bank0 = try socket(bank: 0)
        XCTAssertEqual(carts.state(of: bank0, in: MachineDraft(model: "master-128")),
                       .cartridgeAbsent(point: "cartridge-rear", pointName: "Rear Cartridge Slot"))
        let fittedDraft = MachineDraft(model: "master-128",
                                       extensions: [.init(name: "acorn-rom-cartridge")])
        guard case .cartridgePresent(_, _, let name, _, let caps) =
                carts.state(of: bank0, in: fittedDraft) else {
            return XCTFail("bank 0 should be present once a cartridge is fitted at the rear slot")
        }
        XCTAssertEqual(name, "acorn-rom-cartridge")
        XCTAssertEqual(caps, ["rom", "empty"])

        // 3. resolve-preset: a bank-0 ROM with the cartridge fitted resolves clean
        //    and keeps the image; without the cartridge it is an orphaned entry the
        //    resolver refuses in the preset's terms (never quoting a command line).
        let romName = "bbc-basic_2.rom"
        let fitted = MachineDraft(
            model: "master-128",
            sidewaysBank: .init(slots: [.init(slot: 0, type: "rom", imageUri: romName)]),
            extensions: [.init(name: "acorn-rom-cartridge")])
        let (fittedResolved, fittedDiags) = try await resolve(fitted, cli: cli, server: server)
        XCTAssertFalse(fittedDiags.contains { $0.isError },
                       "a fitted cartridge's bank 0 ROM should resolve without error: \(fittedDiags)")
        XCTAssertEqual(fittedResolved?.sidewaysBank?.slots.first { $0.slot == 0 }?.type, "rom")

        let orphan = MachineDraft(
            model: "master-128",
            sidewaysBank: .init(slots: [.init(slot: 0, type: "rom", imageUri: romName)]))
        let (_, orphanDiags) = try await resolve(orphan, cli: cli, server: server)
        let slot0 = try XCTUnwrap(orphanDiags.first { $0.path == "/sideways_bank/slots/0" },
                                  "an orphaned bank-0 entry should be diagnosed at its slot path")
        XCTAssertTrue(slot0.isError)
        XCTAssertTrue(slot0.message.lowercased().contains("cartridge"),
                      "the sentence names the cartridge: \(slot0.message)")
        XCTAssertTrue(slot0.message.contains("Rear Cartridge Slot"),
                      "the sentence names where to fit one: \(slot0.message)")
        XCTAssertFalse(slot0.message.contains("--sideways"),
                       "a user-facing sentence must not quote a command line: \(slot0.message)")
    }

    // MARK: - Helpers

    /// Write a draft to a temporary preset file and resolve it through the client's
    /// CLI, exactly as the configurator does.
    private func resolve(_ draft: MachineDraft, cli: CoreCLI, server: String) async throws
    -> (MachineDraft?, [PresetManager.PresetDiagnostic]) {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("cartridge-\(UUID().uuidString).json")
        try draft.encoded().write(to: url)
        defer { try? FileManager.default.removeItem(at: url) }
        return await cli.resolvePreset(presetPath: url.path, executablePath: server)
    }

    private static func findServer() -> String? {
        if let frameworks = Bundle.main.privateFrameworksPath {
            let path = "\(frameworks)/beebium-master-128"
            if FileManager.default.isExecutableFile(atPath: path) { return path }
        }
        let home = FileManager.default.homeDirectoryForCurrentUser
        let build = home.appendingPathComponent("Code/beebium/build/src/server/beebium-master-128").path
        if FileManager.default.isExecutableFile(atPath: build) { return build }
        return nil
    }
}
