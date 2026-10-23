// Copyright (c) 2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// `CoreCLI.run` against real subprocesses. The subcommands themselves are
/// covered by the pinned-sample decode tests; these pin the process plumbing.
final class CoreCLITests: XCTestCase {

    /// A macOS pipe holds 64 KiB. A child that writes more than that blocks in
    /// write until the parent reads, so `run` must drain its pipes before it
    /// waits for the child to exit, or the two deadlock (#214: the app
    /// beachballed once describe-preset-schema grew past 64 KiB). Raced against
    /// a watchdog so a regression fails instead of hanging the suite.
    func testRunDrainsMoreThanAPipeBufferOfStdout() async throws {
        executionTimeAllowance = 60
        let cli = await CoreCLI()
        let result = await withTaskGroup(of: (stdout: String, error: String?)?.self) { group in
            group.addTask {
                await cli.run(
                    executablePath: "/bin/sh",
                    arguments: ["-c", "head -c 200000 /dev/zero | tr \"\\0\" x"])
            }
            group.addTask {
                try? await Task.sleep(nanoseconds: 30_000_000_000)
                return nil
            }
            let first = await group.next()!
            group.cancelAll()
            return first
        }
        let (stdout, error) = try XCTUnwrap(result, "run did not return within 30 s: pipe deadlock")
        XCTAssertNil(error)
        XCTAssertEqual(stdout.count, 200_000)
    }

    /// stderr is drained too, and is the error on a non-zero exit even when it
    /// exceeds the pipe buffer.
    func testRunReturnsOversizedStderrAsErrorOnNonZeroExit() async throws {
        executionTimeAllowance = 60
        let cli = await CoreCLI()
        let (stdout, error) = await cli.run(
            executablePath: "/bin/sh",
            arguments: ["-c", "head -c 100000 /dev/zero | tr \"\\0\" e >&2; echo out; exit 3"])
        XCTAssertEqual(stdout, "out\n")
        XCTAssertEqual(try XCTUnwrap(error).count, 100_000)
    }

    func testRunReportsSpawnFailure() async {
        let cli = await CoreCLI()
        let (stdout, error) = await cli.run(executablePath: "/nonexistent/beebium", arguments: [])
        XCTAssertEqual(stdout, "")
        XCTAssertNotNil(error)
    }
}
