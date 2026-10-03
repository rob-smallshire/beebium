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

import Darwin
import Foundation
import GRPC
import NIOCore
import NIOPosix
import XCTest
@testable import Beebium

/// Measurement harness for issue #152: how the number of NIO event-loop threads
/// affects CPU and per-window video-frame delivery as open machines scale. NOT a
/// unit test and NOT in the shipping app -- it spawns real servers and runs for
/// about a minute, so it is skipped unless BEEBIUM_ELG_PERF=1. It opens N real
/// gRPC video-subscribe streams (the same GRPCChannelPool construction VideoClient
/// uses) on an event loop group of a chosen thread count and reports, per
/// (machines, threads): process CPU in cores, aggregate delivered frames/s, the
/// worst stream's p95 inter-frame interval, and late frames (>30 ms, nominal 20).
///
/// Run (Release, from clients/macos/Beebium):
///   BEEBIUM_ELG_PERF=1 xcodebuild test -scheme BeebiumTests \
///     -configuration Release -only-testing:BeebiumTests/EventLoopScalingMeasurement
final class EventLoopScalingMeasurement: XCTestCase {

    private let serverPath = "/Users/rjs/Code/beebium/build/src/server/beebium-model-b"
    private let romDir = "/Users/rjs/Code/beebium/roms"
    private let basePort = 49200
    private let measureSeconds = 8.0

    func testEventLoopScaling() throws {
        guard ProcessInfo.processInfo.environment["BEEBIUM_ELG_PERF"] == "1" else {
            throw XCTSkip("set BEEBIUM_ELG_PERF=1 to run the #152 scaling measurement")
        }
        let cores = ProcessInfo.processInfo.activeProcessorCount
        // Before (the single-loop ceiling) vs after (the shipped sizing).
        let threadCounts = [1, VideoClient.eventLoopThreadCount(coreCount: cores)]
        let machineCounts = [3, 6, 10]

        func pad(_ s: String, _ w: Int) -> String {
            s.count >= w ? s : s + String(repeating: " ", count: w - s.count)
        }
        print("\n#152 event-loop scaling -- host has \(cores) cores")
        print(pad("machines", 9) + pad("threads", 8) + pad("cpu(cores)", 11)
              + pad("frames/s", 13) + pad("worst p95(ms)", 15) + pad("late", 10))

        for machines in machineCounts {
            let servers = try spawnServers(machines)
            defer { for s in servers { s.terminate() }; for s in servers { s.waitUntilExit() } }
            try waitUntilReady(ports: servers.map(\.port))

            for threads in threadCounts {
                let result = runStreams(ports: servers.map(\.port), threadCount: threads)
                print(String(format: "%-9d %-8d %-11.2f %-13.0f %-15.1f %-10d",
                             machines, threads, result.cpuCores, result.framesPerSecond,
                             result.worstP95Ms, result.lateFrames))
            }
        }
    }

    // MARK: - Servers

    private struct Server { let process: Process; let port: Int
        func terminate() { if process.isRunning { process.terminate() } }
        func waitUntilExit() { process.waitUntilExit() }
    }

    private func spawnServers(_ count: Int) throws -> [Server] {
        var servers: [Server] = []
        for i in 0..<count {
            let port = basePort + i
            let process = Process()
            process.executableURL = URL(fileURLWithPath: serverPath)
            process.arguments = ["start", "--port", "\(port)", "--rom-dir", romDir]
            process.standardOutput = FileHandle.nullDevice
            process.standardError = FileHandle.nullDevice
            try process.run()
            servers.append(Server(process: process, port: port))
        }
        return servers
    }

    /// Poll GetConfig on each port until it answers, on a throwaway single-thread
    /// group so readiness probing never shares the group under measurement.
    private func waitUntilReady(ports: [Int]) throws {
        let group = MultiThreadedEventLoopGroup(numberOfThreads: 1)
        defer { try? group.syncShutdownGracefully() }
        for port in ports {
            let deadline = Date().addingTimeInterval(20)
            var ready = false
            while Date() < deadline {
                let channel = try? GRPCChannelPool.with(
                    target: .host("127.0.0.1", port: port),
                    transportSecurity: .plaintext, eventLoopGroup: group)
                if let channel {
                    let client = Beebium_VideoServiceNIOClient(channel: channel)
                    if (try? client.getConfig(Beebium_GetConfigRequest()).response.wait()) != nil {
                        ready = true
                    }
                    try? channel.close().wait()
                }
                if ready { break }
                Thread.sleep(forTimeInterval: 0.25)
            }
            XCTAssertTrue(ready, "server on port \(port) never became ready")
        }
    }

    // MARK: - One measurement run

    private struct RunResult {
        let cpuCores: Double
        let framesPerSecond: Double
        let worstP95Ms: Double
        let lateFrames: Int
    }

    /// A per-stream frame-arrival collector. The subscribe handler runs on an ELG
    /// thread, so guard the arrivals with a lock.
    private final class Collector: @unchecked Sendable {
        private let lock = NSLock()
        private var times: [Double] = []
        func record(_ t: Double) { lock.lock(); times.append(t); lock.unlock() }
        var snapshot: [Double] { lock.lock(); defer { lock.unlock() }; return times }
    }

    private func runStreams(ports: [Int], threadCount: Int) -> RunResult {
        let group = MultiThreadedEventLoopGroup(numberOfThreads: threadCount)
        var channels: [GRPCChannel] = []
        var calls: [ServerStreamingCall<Beebium_SubscribeFramesRequest, Beebium_Frame>] = []
        let collectors = ports.map { _ in Collector() }

        for (index, port) in ports.enumerated() {
            guard let channel = try? GRPCChannelPool.with(
                target: .host("127.0.0.1", port: port),
                transportSecurity: .plaintext, eventLoopGroup: group) else {
                NSLog("[#152] channel to port %d failed; skipping stream", port)
                continue
            }
            channels.append(channel)
            let client = Beebium_VideoServiceNIOClient(channel: channel)
            let collector = collectors[index]
            let call = client.subscribeFrames(Beebium_SubscribeFramesRequest()) { _ in
                collector.record(Date().timeIntervalSinceReferenceDate)
            }
            calls.append(call)
        }

        let cpuStart = taskCpuSeconds()
        let wallStart = Date()
        Thread.sleep(forTimeInterval: measureSeconds)
        let wall = Date().timeIntervalSince(wallStart)
        let cpuCores = (taskCpuSeconds() - cpuStart) / wall

        for call in calls { call.cancel(promise: nil) }
        for channel in channels { try? channel.close().wait() }
        try? group.syncShutdownGracefully()

        // Aggregate: total frames over wall, and the worst stream's p95 interval
        // and late-frame count (an interval over 30 ms when fields arrive at 20).
        var totalFrames = 0
        var worstP95 = 0.0
        var lateFrames = 0
        for collector in collectors {
            let times = collector.snapshot.sorted()
            totalFrames += times.count
            guard times.count > 2 else { continue }
            var intervalsMs: [Double] = []
            for k in 1..<times.count { intervalsMs.append((times[k] - times[k - 1]) * 1000.0) }
            let sorted = intervalsMs.sorted()
            let median = sorted[sorted.count / 2]
            // "Late" is congestion relative to this stream's own cadence, so the
            // metric does not depend on the server's frame rate: an interval more
            // than 1.5x the median is a stall, not the normal beat.
            lateFrames += intervalsMs.filter { $0 > 1.5 * median }.count
            let p95 = sorted[min(sorted.count - 1, Int(Double(sorted.count) * 0.95))]
            worstP95 = max(worstP95, p95)
        }
        return RunResult(cpuCores: cpuCores,
                         framesPerSecond: Double(totalFrames) / wall,
                         worstP95Ms: worstP95,
                         lateFrames: lateFrames)
    }

    // MARK: - CPU

    /// Live-thread CPU seconds for this process (user+system), dominated by the
    /// NIO event loop threads during a run.
    private func taskCpuSeconds() -> Double {
        var info = task_thread_times_info()
        var count = mach_msg_type_number_t(
            MemoryLayout<task_thread_times_info>.size / MemoryLayout<natural_t>.size)
        let kr = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                task_info(mach_task_self_, task_flavor_t(TASK_THREAD_TIMES_INFO), $0, &count)
            }
        }
        guard kr == KERN_SUCCESS else { return 0 }
        let user = Double(info.user_time.seconds) + Double(info.user_time.microseconds) / 1e6
        let system = Double(info.system_time.seconds) + Double(info.system_time.microseconds) / 1e6
        return user + system
    }
}
