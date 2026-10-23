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

/// A typed wrapper over the server executable's command-line interface. One
/// method per run-to-completion subcommand the client invokes: each builds the
/// arguments, runs the executable with `run(...)`, and decodes its output,
/// mapping a non-zero exit (or unparseable output) to the method's own failure
/// shape. PresetManager owns one and routes every such call through it, so
/// process invocation and output decoding live in one place rather than being
/// hand-rolled at each call site.
///
/// The long-lived `start` launch (which spawns a server and awaits its
/// listening port) is not here: it is a process-lifecycle concern, not a
/// run-and-decode one, and stays in PresetManager. The two tiny JSONL line
/// parsers (`PresetManager.parseDiscFormats`, `parseSerialPortPaths`) stay there
/// too, with their pinned-sample tests; this wrapper calls them.
///
/// `@MainActor` because PresetManager drives it from there; `run(...)` itself
/// is nonisolated and never blocks, so a slow subcommand costs the main actor
/// nothing but an await.
@MainActor
struct CoreCLI {
    /// Run `executablePath` with `arguments` to completion, returning its stdout
    /// and, on a non-zero exit (or a spawn failure), an error message (the
    /// trimmed stderr, or a status/thrown description).
    ///
    /// Both pipes are drained to end concurrently from the moment of spawn and
    /// the exit is awaited only afterwards. The order matters: a pipe holds
    /// 64 KiB, so a child that writes more blocks in write until someone reads,
    /// and waiting for its exit first would deadlock (#214).
    nonisolated func run(executablePath: String, arguments: [String]) async -> (stdout: String, error: String?) {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executablePath)
        process.arguments = arguments

        let stdoutPipe = Pipe()
        let stderrPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = stderrPipe

        async let stdoutData = Self.readToEnd(stdoutPipe.fileHandleForReading)
        async let stderrData = Self.readToEnd(stderrPipe.fileHandleForReading)

        let status: Int32
        do {
            status = try await Self.runToExit(process)
        } catch {
            // Nothing was spawned, so nothing will ever close the pipes' write
            // ends: close them here so the readers reach end of file.
            try? stdoutPipe.fileHandleForWriting.close()
            try? stderrPipe.fileHandleForWriting.close()
            _ = await (stdoutData, stderrData)
            return ("", error.localizedDescription)
        }

        let stdout = String(data: await stdoutData, encoding: .utf8) ?? ""

        guard status == 0 else {
            let stderrText = String(data: await stderrData, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            let errorMsg = stderrText.isEmpty
                ? "exit status \(status)"
                : stderrText
            return (stdout, errorMsg)
        }

        return (stdout, nil)
    }

    /// Read `handle` to end of file on a global queue, so the read never
    /// occupies a cooperative thread or the main actor while it waits.
    nonisolated private static func readToEnd(_ handle: FileHandle) async -> Data {
        await withCheckedContinuation { continuation in
            DispatchQueue.global().async {
                continuation.resume(returning: handle.readDataToEndOfFile())
            }
        }
    }

    /// Spawn `process` and await its exit status without blocking any thread.
    /// The termination handler is installed before the spawn: one set after a
    /// process has already exited is never called.
    nonisolated private static func runToExit(_ process: Process) async throws -> Int32 {
        try await withCheckedThrowingContinuation { continuation in
            process.terminationHandler = { continuation.resume(returning: $0.terminationStatus) }
            do {
                try process.run()
            } catch {
                continuation.resume(throwing: error)
            }
        }
    }

    // MARK: - Schema

    /// `describe-preset-schema`, decoded whole as a PresetSchema. Returns
    /// (schema, errorMessage) with the error nil on success.
    func presetSchema(executablePath: String) async -> (PresetSchema?, String?) {
        let (output, error) = await run(executablePath: executablePath, arguments: ["describe-preset-schema"])
        if let error = error { return (nil, error) }
        guard let data = output.data(using: .utf8) else {
            return (nil, "no output")
        }
        do {
            return (try JSONDecoder().decode(PresetSchema.self, from: data), nil)
        } catch {
            return (nil, error.localizedDescription)
        }
    }

    /// `describe-preset-schema`, decoding the one section whose `type` matches as
    /// `T`. nil when the executable could not be run, the section is absent, or it
    /// does not decode.
    func schemaSection<T: Decodable>(_ type: String, executablePath: String) async -> T? {
        let (output, error) = await run(executablePath: executablePath, arguments: ["describe-preset-schema"])
        if error != nil { return nil }
        guard let data = output.data(using: .utf8) else { return nil }
        do {
            guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let sections = json["sections"] as? [[String: Any]],
                  let section = sections.first(where: { ($0["type"] as? String) == type }) else {
                return nil
            }
            let sectionData = try JSONSerialization.data(withJSONObject: section)
            return try JSONDecoder().decode(T.self, from: sectionData)
        } catch {
            NSLog("[CoreCLI] Failed to decode '\(type)' schema section: \(error)")
            return nil
        }
    }

    // MARK: - ROMs and disc images

    /// `describe-rom <image>`, resolving bare names against `romDirpath` when
    /// given. nil on any failure to run or decode.
    func romHeader(image: String, executablePath: String, romDirpath: String?) async -> RomHeaderInfo? {
        var arguments = ["describe-rom", image]
        if let romDirpath = romDirpath {
            arguments.append(contentsOf: ["--rom-dir", romDirpath])
        }
        let (output, error) = await run(executablePath: executablePath, arguments: arguments)
        if error != nil { return nil }
        guard let data = output.data(using: .utf8),
              let info = try? JSONDecoder().decode(RomHeaderInfo.self, from: data) else {
            return nil
        }
        return info
    }

    /// `describe-disc-image <path>`. nil only when the executable could not be
    /// run at all; a "not a disc image" verdict comes back as a decoded
    /// DiscImageInfo with recognised == false.
    func describeDiscImage(path: String, executablePath: String) async -> DiscImageInfo? {
        let (output, error) = await run(executablePath: executablePath, arguments: ["describe-disc-image", path])
        if error != nil { return nil }
        guard let data = output.data(using: .utf8),
              let info = try? JSONDecoder().decode(DiscImageInfo.self, from: data) else {
            return nil
        }
        return info
    }

    /// `list-disc-formats` (JSONL), parsed to the disc formats the server loads.
    /// nil when the executable could not be run (so the caller can retry rather
    /// than cache a failure); an empty array is a real answer.
    func discFormats(executablePath: String) async -> [PresetManager.DiscFormat]? {
        let (output, error) = await run(
            executablePath: executablePath, arguments: ["--format", "jsonl", "list-disc-formats"])
        if error != nil { return nil }
        return PresetManager.parseDiscFormats(fromFormatList: output)
    }

    /// `list-serial-ports` (JSONL), parsed to the host's device paths. nil when
    /// the executable could not be run (so the caller can retry); an empty array
    /// is a real answer.
    func serialPorts(executablePath: String) async -> [String]? {
        let (output, error) = await run(
            executablePath: executablePath, arguments: ["--format", "jsonl", "list-serial-ports"])
        if error != nil { return nil }
        return PresetManager.parseSerialPortPaths(fromJSONL: output)
    }

    // MARK: - Preset resolution

    /// `resolve-preset <path> --json`, decoded to the resolved draft and its
    /// diagnostics. resolve-preset prints its JSON even on a non-zero exit, so
    /// the error is ignored and the output parsed regardless; (nil, []) when
    /// nothing could be parsed.
    func resolvePreset(presetPath: String, executablePath: String) async -> (MachineDraft?, [PresetManager.PresetDiagnostic]) {
        let (output, _) = await run(
            executablePath: executablePath, arguments: ["resolve-preset", presetPath, "--json"])
        return Self.decodeResolveOutput(output) ?? {
            NSLog("[CoreCLI] Could not parse resolve-preset output for \(presetPath)")
            return (nil, [])
        }()
    }

    /// Decode `resolve-preset --json` output: the resolved preset (optional) and
    /// its diagnostics. nil when the text is not the expected JSON. A pure seam,
    /// so the decode is testable against pinned samples without running a server.
    nonisolated static func decodeResolveOutput(_ text: String) -> (MachineDraft?, [PresetManager.PresetDiagnostic])? {
        struct Output: Decodable {
            let preset: MachineDraft?
            let diagnostics: [PresetManager.PresetDiagnostic]
        }
        guard let data = text.data(using: .utf8),
              let parsed = try? JSONDecoder().decode(Output.self, from: data) else {
            return nil
        }
        return (parsed.preset, parsed.diagnostics)
    }

    // MARK: - Preset listing

    /// `list-presets --format jsonl`, with `--all-models` so one executable
    /// reports every model's presets (and the user's), not just its own.
    /// Returns (rows, errorMessage) with the error nil on success; an empty
    /// array is a real answer.
    func listPresets(allModels: Bool, executablePath: String) async -> ([PresetListing], String?) {
        var arguments = ["--format", "jsonl", "list-presets"]
        if allModels {
            arguments.append("--all-models")
        }
        let (output, error) = await run(executablePath: executablePath, arguments: arguments)
        if let error = error { return ([], error) }
        return (Self.decodeListPresets(output), nil)
    }

    /// Decode `list-presets --format jsonl` output: one PresetListing per
    /// non-blank line, skipping any line that does not decode. A pure,
    /// side-effect-free seam, so the decode is testable against pinned samples
    /// without running a server.
    nonisolated static func decodeListPresets(_ text: String) -> [PresetListing] {
        let decoder = JSONDecoder()
        var out: [PresetListing] = []
        for raw in text.split(whereSeparator: \.isNewline) {
            guard let data = raw.data(using: .utf8),
                  let row = try? decoder.decode(PresetListing.self, from: data) else {
                continue
            }
            out.append(row)
        }
        return out
    }

    // MARK: - Preset thumbnail

    /// `show-preset <idOrPath> --thumbnail --output <png>`, which writes the
    /// preset's thumbnail PNG. Returns nil when it was written, or the error
    /// message otherwise -- including exit 66 when the preset has no thumbnail,
    /// which callers normally avoid by checking the listing's `thumbnail` flag
    /// first.
    func showPresetThumbnail(idOrPath: String, outputPath: String, executablePath: String) async -> String? {
        let (_, error) = await run(
            executablePath: executablePath,
            arguments: ["show-preset", idOrPath, "--thumbnail", "--output", outputPath])
        return error
    }

    // MARK: - Preset management

    /// `create-preset --name <name> --from <presetId>`. Returns (newPresetId,
    /// errorMessage) with exactly one non-nil; the id is trimmed.
    func createPreset(name: String, from presetId: String, executablePath: String) async -> (String?, String?) {
        let (output, error) = await run(
            executablePath: executablePath, arguments: ["create-preset", "--name", name, "--from", presetId])
        if let error = error { return (nil, error) }
        return (output.trimmingCharacters(in: .whitespacesAndNewlines), nil)
    }

    /// `delete-preset <presetId>`. Returns an error message, or nil on success.
    func deletePreset(presetId: String, executablePath: String) async -> String? {
        let (_, error) = await run(executablePath: executablePath, arguments: ["delete-preset", presetId])
        return error
    }

    /// `export-preset <presetId> --output <filepath>`. Returns an error message,
    /// or nil on success.
    func exportPreset(presetId: String, to filepath: String, executablePath: String) async -> String? {
        let (_, error) = await run(
            executablePath: executablePath, arguments: ["export-preset", presetId, "--output", filepath])
        return error
    }

    /// `import-preset <filepath>`. Returns (newPresetId, errorMessage) with
    /// exactly one non-nil; the id is trimmed.
    func importPreset(filepath: String, executablePath: String) async -> (String?, String?) {
        let (output, error) = await run(executablePath: executablePath, arguments: ["import-preset", filepath])
        if let error = error { return (nil, error) }
        return (output.trimmingCharacters(in: .whitespacesAndNewlines), nil)
    }

    // MARK: - Saved machines

    /// `describe-machine <dir|document> --format jsonl`, decoded to one
    /// MachineDescription. nil when the executable could not be run or the
    /// output did not decode. Needs no running server; the model is irrelevant,
    /// so any core executable serves.
    func describeMachine(path: String, executablePath: String) async -> MachineDescription? {
        let (output, error) = await run(
            executablePath: executablePath, arguments: ["describe-machine", path, "--format", "jsonl"])
        if error != nil { return nil }
        return Self.decodeDescribeMachine(output)
    }

    /// Decode `describe-machine --format jsonl` output: the one machine object
    /// on the first line that decodes. A pure, side-effect-free seam, so the
    /// decode is testable against pinned samples without running a server.
    nonisolated static func decodeDescribeMachine(_ text: String) -> MachineDescription? {
        let decoder = JSONDecoder()
        for raw in text.split(whereSeparator: \.isNewline) {
            guard let data = raw.data(using: .utf8),
                  let row = try? decoder.decode(MachineDescription.self, from: data) else {
                continue
            }
            return row
        }
        return nil
    }
}
