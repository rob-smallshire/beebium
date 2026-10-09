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

/// The preset document (`*.preset.beebium`) as an editable Swift model.
///
/// The New Machine dialog edits a `MachineDraft`; each configuration section is
/// an editor over part of this document, and launching serialises it to a
/// temporary preset file. The keys match the server's preset schema exactly
/// (snake_case), so a draft decoded from a preset, round-tripped and handed back
/// to the server as `start --preset` means the same machine.
///
/// Forward compatibility is a first-class requirement: the server keeps adding
/// preset keys (speed, startup, links, mos, ...), and a draft that silently
/// dropped keys it did not yet model would corrupt presets it merely passed
/// through. So every object preserves the keys it does not model in an `extra`
/// bag and re-emits them unchanged. Round-tripping any preset is therefore
/// lossless at the structural level (`decode -> encode -> decode` is an
/// identity on `MachineDraft`), the only normalisation being that an explicit
/// `null` for a modelled optional and the absence of that key both mean "unset"
/// and encode as absence.
struct MachineDraft: Equatable {
    /// The target machine model (e.g. "model-b"). The one field a draft needs to
    /// launch; optional here because a malformed/partial file may omit it and we
    /// still want to represent what it does contain.
    var model: String?
    /// Display name shown in the preset picker.
    var name: String?
    /// Window-title template, e.g. "Station {econet-station} (AUN, Model B)".
    var machineName: String?
    var description: String?
    /// Release date for sorting: YYYY, YYYY-MM or YYYY-MM-DD.
    var releaseDate: String?
    /// Whether the machine auto-boots (SHIFT+BREAK) at startup. Deprecated in
    /// favour of `startup.auto_boot`; read but no longer written by the server.
    var autoBoot: Bool?
    /// Emulation speed: a positive number (multiple of real time) or "unlimited".
    var speed: JSONValue?
    /// Startup keyboard links: screen_mode + auto_boot, or the whole links byte.
    var startup: Startup?
    var storage: Storage?
    var sidewaysBank: SidewaysBank?
    var econet: Econet?
    var extensions: [Extension]?
    /// Top-level keys this model does not (yet) understand, preserved verbatim.
    var extra: [String: JSONValue]

    init(model: String? = nil,
         name: String? = nil,
         machineName: String? = nil,
         description: String? = nil,
         releaseDate: String? = nil,
         autoBoot: Bool? = nil,
         speed: JSONValue? = nil,
         startup: Startup? = nil,
         storage: Storage? = nil,
         sidewaysBank: SidewaysBank? = nil,
         econet: Econet? = nil,
         extensions: [Extension]? = nil,
         extra: [String: JSONValue] = [:]) {
        self.model = model
        self.name = name
        self.machineName = machineName
        self.description = description
        self.releaseDate = releaseDate
        self.autoBoot = autoBoot
        self.speed = speed
        self.startup = startup
        self.storage = storage
        self.sidewaysBank = sidewaysBank
        self.econet = econet
        self.extensions = extensions
        self.extra = extra
    }

    // MARK: Nested documents

    /// The `storage` section: the floppy-disc controller fitted and the floppy
    /// drives with their mounted images.
    struct Storage: Equatable {
        var fdcSocket: FdcSocket?
        var floppyDrives: [FloppyDrive]?
        var extra: [String: JSONValue]

        init(fdcSocket: FdcSocket? = nil,
             floppyDrives: [FloppyDrive]? = nil,
             extra: [String: JSONValue] = [:]) {
            self.fdcSocket = fdcSocket
            self.floppyDrives = floppyDrives
            self.extra = extra
        }
    }

    /// The floppy-disc controller fitted to the machine's FDC socket.
    struct FdcSocket: Equatable {
        var id: String?
        var extra: [String: JSONValue]

        init(id: String? = nil, extra: [String: JSONValue] = [:]) {
            self.id = id
            self.extra = extra
        }
    }

    /// One floppy drive and the disc image mounted in it (if any).
    struct FloppyDrive: Equatable {
        var drive: Int
        var imageUri: String?
        var extra: [String: JSONValue]

        init(drive: Int, imageUri: String? = nil, extra: [String: JSONValue] = [:]) {
            self.drive = drive
            self.imageUri = imageUri
            self.extra = extra
        }
    }

    /// The `sideways_bank` section: per-slot ROM/RAM assignments.
    struct SidewaysBank: Equatable {
        var slots: [SidewaysSlot]
        var extra: [String: JSONValue]

        init(slots: [SidewaysSlot] = [], extra: [String: JSONValue] = [:]) {
            self.slots = slots
            self.extra = extra
        }
    }

    /// One sideways slot assignment. `type` is "rom", "ram" or "empty".
    struct SidewaysSlot: Equatable {
        var slot: Int
        var type: String
        var imageUri: String?
        /// Power-on write-protect position for a sideways-RAM slot (RAM only).
        var writeProtected: Bool?
        var extra: [String: JSONValue]

        init(slot: Int, type: String, imageUri: String? = nil,
             writeProtected: Bool? = nil, extra: [String: JSONValue] = [:]) {
            self.slot = slot
            self.type = type
            self.imageUri = imageUri
            self.writeProtected = writeProtected
            self.extra = extra
        }
    }

    /// The `econet` section: station number and the transport it is wired to.
    struct Econet: Equatable {
        /// The station: an integer station number, or the string "auto" (for
        /// `--station auto`). Carried as a JSONValue so either form, and any the
        /// server adds later, round-trips without loss.
        var station: JSONValue?
        var transport: Transport?
        var extra: [String: JSONValue]

        init(station: JSONValue? = nil, transport: Transport? = nil, extra: [String: JSONValue] = [:]) {
            self.station = station
            self.transport = transport
            self.extra = extra
        }
    }

    /// An Econet transport binding (e.g. "aun") and its string parameters.
    struct Transport: Equatable {
        var name: String?
        /// Transport parameters (e.g. discovery/net/port); values are strings in
        /// the schema but carried as JSONValue so an unexpected value type is not
        /// lost.
        var parameters: [String: JSONValue]?
        var extra: [String: JSONValue]

        init(name: String? = nil, parameters: [String: JSONValue]? = nil, extra: [String: JSONValue] = [:]) {
            self.name = name
            self.parameters = parameters
            self.extra = extra
        }
    }

    /// The `startup` section: either a screen mode + auto-boot pair, or the raw
    /// keyboard-links byte (the two forms are mutually exclusive; the server
    /// enforces it and the editor presents it that way).
    struct Startup: Equatable {
        var screenMode: Int?
        var autoBoot: Bool?
        var links: Int?
        var extra: [String: JSONValue]

        init(screenMode: Int? = nil, autoBoot: Bool? = nil, links: Int? = nil,
             extra: [String: JSONValue] = [:]) {
            self.screenMode = screenMode
            self.autoBoot = autoBoot
            self.links = links
            self.extra = extra
        }
    }

    /// One entry in the `extensions` array: a named plugin and its config.
    struct Extension: Equatable {
        var name: String
        /// Arbitrary per-extension configuration (keys and value types vary by
        /// extension), carried verbatim.
        var config: [String: JSONValue]?
        var extra: [String: JSONValue]

        init(name: String, config: [String: JSONValue]? = nil, extra: [String: JSONValue] = [:]) {
            self.name = name
            self.config = config
            self.extra = extra
        }
    }
}

// MARK: - Codable

extension MachineDraft: Codable {
    private static let knownKeys: Set<String> = [
        "model", "name", "machine_name", "description", "release_date",
        "auto_boot", "speed", "startup", "storage", "sideways_bank", "econet", "extensions",
    ]

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: JSONCodingKey.self)
        model = try c.decodeIfPresent(String.self, forKey: "model")
        name = try c.decodeIfPresent(String.self, forKey: "name")
        machineName = try c.decodeIfPresent(String.self, forKey: "machine_name")
        description = try c.decodeIfPresent(String.self, forKey: "description")
        releaseDate = try c.decodeIfPresent(String.self, forKey: "release_date")
        autoBoot = try c.decodeIfPresent(Bool.self, forKey: "auto_boot")
        speed = try c.decodeIfPresent(JSONValue.self, forKey: "speed")
        startup = try c.decodeIfPresent(Startup.self, forKey: "startup")
        storage = try c.decodeIfPresent(Storage.self, forKey: "storage")
        sidewaysBank = try c.decodeIfPresent(SidewaysBank.self, forKey: "sideways_bank")
        econet = try c.decodeIfPresent(Econet.self, forKey: "econet")
        extensions = try c.decodeIfPresent([Extension].self, forKey: "extensions")
        extra = try c.decodeExtra(excluding: Self.knownKeys)
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: JSONCodingKey.self)
        try c.encodeIfPresent(model, forKey: "model")
        try c.encodeIfPresent(name, forKey: "name")
        try c.encodeIfPresent(machineName, forKey: "machine_name")
        try c.encodeIfPresent(description, forKey: "description")
        try c.encodeIfPresent(releaseDate, forKey: "release_date")
        try c.encodeIfPresent(autoBoot, forKey: "auto_boot")
        try c.encodeIfPresent(speed, forKey: "speed")
        try c.encodeIfPresent(startup, forKey: "startup")
        try c.encodeIfPresent(storage, forKey: "storage")
        try c.encodeIfPresent(sidewaysBank, forKey: "sideways_bank")
        try c.encodeIfPresent(econet, forKey: "econet")
        try c.encodeIfPresent(extensions, forKey: "extensions")
        try c.encodeExtra(extra)
    }
}

extension MachineDraft.Startup: Codable {
    private static let knownKeys: Set<String> = ["screen_mode", "auto_boot", "links"]

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: JSONCodingKey.self)
        screenMode = try c.decodeIfPresent(Int.self, forKey: "screen_mode")
        autoBoot = try c.decodeIfPresent(Bool.self, forKey: "auto_boot")
        links = try c.decodeIfPresent(Int.self, forKey: "links")
        extra = try c.decodeExtra(excluding: Self.knownKeys)
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: JSONCodingKey.self)
        try c.encodeIfPresent(screenMode, forKey: "screen_mode")
        try c.encodeIfPresent(autoBoot, forKey: "auto_boot")
        try c.encodeIfPresent(links, forKey: "links")
        try c.encodeExtra(extra)
    }
}

extension MachineDraft.Storage: Codable {
    private static let knownKeys: Set<String> = ["fdc_socket", "floppy_drives"]

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: JSONCodingKey.self)
        fdcSocket = try c.decodeIfPresent(MachineDraft.FdcSocket.self, forKey: "fdc_socket")
        floppyDrives = try c.decodeIfPresent([MachineDraft.FloppyDrive].self, forKey: "floppy_drives")
        extra = try c.decodeExtra(excluding: Self.knownKeys)
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: JSONCodingKey.self)
        try c.encodeIfPresent(fdcSocket, forKey: "fdc_socket")
        try c.encodeIfPresent(floppyDrives, forKey: "floppy_drives")
        try c.encodeExtra(extra)
    }
}

extension MachineDraft.FdcSocket: Codable {
    private static let knownKeys: Set<String> = ["id"]

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: JSONCodingKey.self)
        id = try c.decodeIfPresent(String.self, forKey: "id")
        extra = try c.decodeExtra(excluding: Self.knownKeys)
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: JSONCodingKey.self)
        try c.encodeIfPresent(id, forKey: "id")
        try c.encodeExtra(extra)
    }
}

extension MachineDraft.FloppyDrive: Codable {
    private static let knownKeys: Set<String> = ["drive", "image_uri"]

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: JSONCodingKey.self)
        drive = try c.decode(Int.self, forKey: "drive")
        imageUri = try c.decodeIfPresent(String.self, forKey: "image_uri")
        extra = try c.decodeExtra(excluding: Self.knownKeys)
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: JSONCodingKey.self)
        try c.encode(drive, forKey: "drive")
        try c.encodeIfPresent(imageUri, forKey: "image_uri")
        try c.encodeExtra(extra)
    }
}

extension MachineDraft.SidewaysBank: Codable {
    private static let knownKeys: Set<String> = ["slots"]

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: JSONCodingKey.self)
        slots = try c.decodeIfPresent([MachineDraft.SidewaysSlot].self, forKey: "slots") ?? []
        extra = try c.decodeExtra(excluding: Self.knownKeys)
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: JSONCodingKey.self)
        try c.encode(slots, forKey: "slots")
        try c.encodeExtra(extra)
    }
}

extension MachineDraft.SidewaysSlot: Codable {
    private static let knownKeys: Set<String> = ["slot", "type", "image_uri", "write_protected"]

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: JSONCodingKey.self)
        slot = try c.decode(Int.self, forKey: "slot")
        type = try c.decode(String.self, forKey: "type")
        imageUri = try c.decodeIfPresent(String.self, forKey: "image_uri")
        writeProtected = try c.decodeIfPresent(Bool.self, forKey: "write_protected")
        extra = try c.decodeExtra(excluding: Self.knownKeys)
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: JSONCodingKey.self)
        try c.encode(slot, forKey: "slot")
        try c.encode(type, forKey: "type")
        try c.encodeIfPresent(imageUri, forKey: "image_uri")
        try c.encodeIfPresent(writeProtected, forKey: "write_protected")
        try c.encodeExtra(extra)
    }
}

extension MachineDraft.Econet: Codable {
    private static let knownKeys: Set<String> = ["station", "transport"]

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: JSONCodingKey.self)
        station = try c.decodeIfPresent(JSONValue.self, forKey: "station")
        transport = try c.decodeIfPresent(MachineDraft.Transport.self, forKey: "transport")
        extra = try c.decodeExtra(excluding: Self.knownKeys)
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: JSONCodingKey.self)
        try c.encodeIfPresent(station, forKey: "station")
        try c.encodeIfPresent(transport, forKey: "transport")
        try c.encodeExtra(extra)
    }
}

extension MachineDraft.Transport: Codable {
    private static let knownKeys: Set<String> = ["name", "parameters"]

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: JSONCodingKey.self)
        name = try c.decodeIfPresent(String.self, forKey: "name")
        parameters = try c.decodeIfPresent([String: JSONValue].self, forKey: "parameters")
        extra = try c.decodeExtra(excluding: Self.knownKeys)
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: JSONCodingKey.self)
        try c.encodeIfPresent(name, forKey: "name")
        try c.encodeIfPresent(parameters, forKey: "parameters")
        try c.encodeExtra(extra)
    }
}

extension MachineDraft.Extension: Codable {
    private static let knownKeys: Set<String> = ["name", "config"]

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: JSONCodingKey.self)
        name = try c.decode(String.self, forKey: "name")
        config = try c.decodeIfPresent([String: JSONValue].self, forKey: "config")
        extra = try c.decodeExtra(excluding: Self.knownKeys)
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: JSONCodingKey.self)
        try c.encode(name, forKey: "name")
        try c.encodeIfPresent(config, forKey: "config")
        try c.encodeExtra(extra)
    }
}

// MARK: - Serialisation helpers

extension MachineDraft {
    /// Decode a draft from preset-file bytes.
    static func decode(from data: Data) throws -> MachineDraft {
        try JSONDecoder().decode(MachineDraft.self, from: data)
    }

    /// Encode the draft as pretty-printed preset-file bytes, ready to write to a
    /// `*.preset.beebium` file. Keys are sorted so the output is stable.
    func encoded() throws -> Data {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try encoder.encode(self)
    }
}
