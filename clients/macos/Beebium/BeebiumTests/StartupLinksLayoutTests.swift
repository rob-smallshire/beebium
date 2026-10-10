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

/// The startup links byte, projected through the schema's layout. These pin the
/// bit maths and the draft-writing rule to the layout alone (no BBC bit
/// constants in the test), using the real Model B links shape.
final class StartupLinksLayoutTests: XCTestCase {

    /// The Model B links layout as describe-preset-schema reports it.
    private func layout() throws -> StartupSchemaSection.LinksLayout {
        let json = """
        { "min": 0, "max": 255, "default": 255,
          "excludes": ["screen_mode", "auto_boot"],
          "link_made_when": "clear",
          "screen_mode_bits": [0, 1, 2],
          "auto_boot_bit": 3, "auto_boot_true_when": "clear",
          "bits": [
            {"bit":7,"link":"Link 1","label":"Filing system"},
            {"bit":6,"link":"Link 2","label":"Not used"},
            {"bit":5,"link":"Link 3","label":"Disc timing (link 3)"},
            {"bit":4,"link":"Link 4","label":"Disc timing (link 4)"},
            {"bit":3,"link":"Link 5","label":"SHIFT+BREAK action"},
            {"bit":2,"link":"Link 6","label":"Screen mode bit 2"},
            {"bit":1,"link":"Link 7","label":"Screen mode bit 1"},
            {"bit":0,"link":"Link 8","label":"Screen mode bit 0"} ] }
        """
        return try JSONDecoder().decode(StartupSchemaSection.LinksLayout.self, from: Data(json.utf8))
    }

    /// The default (255) reads as mode 7, auto-boot off -- the normal machine.
    func testDefaultByte() throws {
        let l = try layout()
        XCTAssertEqual(l.screenMode(fromByte: 255), 7)
        XCTAssertFalse(l.autoBoot(fromByte: 255))
    }

    /// The worked example from the core: mode 3 with auto-boot is &F3 (243).
    func testWorkedExample() throws {
        let l = try layout()
        XCTAssertEqual(l.byte(screenMode: 3, autoBoot: true), 0xF3)
        XCTAssertEqual(l.screenMode(fromByte: 0xF3), 3)
        XCTAssertTrue(l.autoBoot(fromByte: 0xF3))
    }

    /// Every friendly pair round-trips through the byte.
    func testFriendlyRoundTrip() throws {
        let l = try layout()
        for mode in 0...7 {
            for boot in [false, true] {
                let byte = l.byte(screenMode: mode, autoBoot: boot)
                XCTAssertEqual(l.screenMode(fromByte: byte), mode)
                XCTAssertEqual(l.autoBoot(fromByte: byte), boot)
                XCTAssertTrue(l.isFriendlyRepresentable(byte),
                              "a friendly byte must be friendly-representable")
            }
        }
    }

    /// A byte with a non-default link outside mode/boot is not friendly; one with
    /// those bits at default is.
    func testFriendlyRepresentable() throws {
        let l = try layout()
        XCTAssertTrue(l.isFriendlyRepresentable(0xF3))       // only mode/boot differ
        XCTAssertFalse(l.isFriendlyRepresentable(0x73))      // bit 7 cleared (filing system)
        XCTAssertFalse(l.isFriendlyRepresentable(0xE3))      // bit 4 cleared (disc timing)
    }

    /// A link reads as made when its bit is clear (active-low), and toggling it
    /// flips exactly that bit.
    func testLinkMadeAndSet() throws {
        let l = try layout()
        XCTAssertFalse(l.isLinkMade(bit: 7, in: 0xFF))       // open at default
        XCTAssertTrue(l.isLinkMade(bit: 7, in: 0x7F))        // bit 7 clear = made
        XCTAssertEqual(l.byte(0xFF, settingLink: 7, made: true), 0x7F)
        XCTAssertEqual(l.byte(0x7F, settingLink: 7, made: false), 0xFF)
    }

    // MARK: - Draft-writing rule

    /// A friendly byte is written as screen_mode/auto_boot, no links.
    func testWriteRuleFriendly() throws {
        let l = try layout()
        let startup = KeyboardSectionView.startup(forByte: l.byte(screenMode: 3, autoBoot: true), layout: l)
        XCTAssertNil(startup.links)
        XCTAssertEqual(startup.screenMode, 3)
        XCTAssertEqual(startup.autoBoot, true)
    }

    /// Setting a bit outside mode/boot switches to the whole links byte; clearing
    /// it switches back to the friendly form.
    func testWriteRuleSwitchesToAndFromLinks() throws {
        let l = try layout()
        // Start friendly (mode 3, boot), then break the filing-system link (bit 7).
        let friendly = l.byte(screenMode: 3, autoBoot: true)   // 0xF3
        let withOther = l.byte(friendly, settingLink: 7, made: true)  // clear bit 7 -> 0x73
        let linksForm = KeyboardSectionView.startup(forByte: withOther, layout: l)
        XCTAssertEqual(linksForm.links, 0x73)
        XCTAssertNil(linksForm.screenMode)
        XCTAssertNil(linksForm.autoBoot)

        // Restore the link (set bit 7) -> back to the friendly form.
        let restored = l.byte(withOther, settingLink: 7, made: false) // set bit 7 -> 0xF3
        let backToFriendly = KeyboardSectionView.startup(forByte: restored, layout: l)
        XCTAssertNil(backToFriendly.links)
        XCTAssertEqual(backToFriendly.screenMode, 3)
        XCTAssertEqual(backToFriendly.autoBoot, true)
    }

    // MARK: - Byte field parsing

    func testParseHexByte() {
        XCTAssertEqual(KeyboardSectionView.parseHexByte("F3"), 0xF3)
        XCTAssertEqual(KeyboardSectionView.parseHexByte("&f3"), 0xF3)
        XCTAssertEqual(KeyboardSectionView.parseHexByte("0xFF"), 255)
        XCTAssertEqual(KeyboardSectionView.parseHexByte(" 00 "), 0)
        XCTAssertNil(KeyboardSectionView.parseHexByte("100"))   // 256, out of range
        XCTAssertNil(KeyboardSectionView.parseHexByte("zz"))
        XCTAssertNil(KeyboardSectionView.parseHexByte(""))
        XCTAssertNil(KeyboardSectionView.parseHexByte("&"))
    }

    func testParseDecByte() {
        XCTAssertEqual(KeyboardSectionView.parseDecByte("243"), 243)
        XCTAssertEqual(KeyboardSectionView.parseDecByte("0"), 0)
        XCTAssertEqual(KeyboardSectionView.parseDecByte("255"), 255)
        XCTAssertNil(KeyboardSectionView.parseDecByte("256"))
        XCTAssertNil(KeyboardSectionView.parseDecByte("-1"))
        XCTAssertNil(KeyboardSectionView.parseDecByte("ff"))
        XCTAssertNil(KeyboardSectionView.parseDecByte(""))
    }
}
