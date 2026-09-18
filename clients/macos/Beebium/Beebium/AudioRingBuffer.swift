// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version. Beebium is distributed in the hope that
// it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details. You should have received a copy of the GNU General Public License along with
// Beebium. If not, see <https://www.gnu.org/licenses/>.

import Foundation
import Atomics

/// Thread-safe lock-free single-producer single-consumer (SPSC) ring buffer for audio samples.
///
/// The buffer stores one 64-bit element per audio frame: the SN76489's two
/// 32-bit source fields (source 0 = tone0|tone1, source 1 = tone2|noise), each
/// two signed 16-bit channels, packed with source 0 in the low half.
///
/// Thread safety:
/// - Producer thread (gRPC callback): calls `write()` to add samples
/// - Consumer thread (audio render): calls `read()` to retrieve samples
///
/// The implementation uses atomic indices with acquire/release memory ordering to ensure
/// visibility of writes across threads without locks.
final class AudioRingBuffer: @unchecked Sendable {

    /// Default capacity in samples (~85ms at 48kHz)
    static let defaultCapacity = 4096

    // Each element holds one audio frame: the SN76489's two 32-bit source
    // fields (source 0 = tone0|tone1, source 1 = tone2|noise) packed as a
    // UInt64, low half = source 0.
    private let buffer: UnsafeMutableBufferPointer<UInt64>
    private let capacity: Int

    /// Read index - only modified by consumer, read by producer
    private let readIndex = ManagedAtomic<UInt64>(0)

    /// Write index - only modified by producer, read by consumer
    private let writeIndex = ManagedAtomic<UInt64>(0)

    /// Statistics: total samples written (for drop detection)
    private let totalWritten = ManagedAtomic<UInt64>(0)

    /// Statistics: samples dropped due to buffer full
    private let droppedSamples = ManagedAtomic<UInt64>(0)

    /// Initialize the ring buffer with specified capacity.
    /// - Parameter capacity: Number of samples the buffer can hold
    init(capacity: Int = AudioRingBuffer.defaultCapacity) {
        self.capacity = capacity
        self.buffer = .allocate(capacity: capacity)
        buffer.initialize(repeating: 0)
    }

    deinit {
        buffer.deallocate()
    }

    // MARK: - Producer Interface (gRPC thread)

    /// Write packed samples to the buffer.
    ///
    /// This method is safe to call from the producer thread while the consumer thread
    /// is reading. If the buffer is full, samples are dropped and counted.
    ///
    /// - Parameters:
    ///   - samples: Pointer to packed 32-bit samples
    ///   - count: Number of samples to write
    /// - Returns: Number of samples actually written (may be less if buffer full)
    func write(_ samples: UnsafePointer<UInt64>, count: Int) -> Int {
        let currentWrite = writeIndex.load(ordering: .relaxed)
        let currentRead = readIndex.load(ordering: .acquiring)

        // Calculate available space
        let used = Int(currentWrite &- currentRead)
        let available = capacity - used
        let toWrite = min(count, available)

        if toWrite < count {
            // Track dropped samples
            droppedSamples.wrappingIncrement(by: UInt64(count - toWrite), ordering: .relaxed)
        }

        if toWrite > 0 {
            // Write samples to buffer
            let writePos = Int(currentWrite % UInt64(capacity))

            // Handle wrap-around
            let firstChunk = min(toWrite, capacity - writePos)
            let secondChunk = toWrite - firstChunk

            // Copy first chunk
            for i in 0..<firstChunk {
                buffer[writePos + i] = samples[i]
            }

            // Copy second chunk (wrapped)
            for i in 0..<secondChunk {
                buffer[i] = samples[firstChunk + i]
            }

            // Update write index with release ordering to ensure writes are visible
            writeIndex.store(currentWrite &+ UInt64(toWrite), ordering: .releasing)
            totalWritten.wrappingIncrement(by: UInt64(toWrite), ordering: .relaxed)
        }

        return toWrite
    }

    /// Write samples from a Data object containing packed UInt64 frames.
    ///
    /// - Parameter data: Data containing packed frames (8 bytes per frame)
    /// - Returns: Number of samples written
    func write(data: Data) -> Int {
        let sampleCount = data.count / MemoryLayout<UInt64>.size
        if sampleCount == 0 { return 0 }
        // Data storage is not guaranteed 8-byte aligned, so load each frame
        // unaligned rather than reinterpreting the pointer.
        var frames = [UInt64](repeating: 0, count: sampleCount)
        data.withUnsafeBytes { rawBuffer in
            for i in 0..<sampleCount {
                frames[i] = rawBuffer.loadUnaligned(fromByteOffset: i * 8, as: UInt64.self)
            }
        }
        return frames.withUnsafeBufferPointer { buffer in
            write(buffer.baseAddress!, count: sampleCount)
        }
    }

    // MARK: - Consumer Interface (audio render thread)

    /// Read samples from the buffer.
    ///
    /// This method is safe to call from the consumer thread while the producer thread
    /// is writing.
    ///
    /// - Parameters:
    ///   - destination: Buffer to write samples into
    ///   - count: Maximum number of samples to read
    /// - Returns: Number of samples actually read
    func read(_ destination: UnsafeMutablePointer<UInt64>, count: Int) -> Int {
        let currentRead = readIndex.load(ordering: .relaxed)
        let currentWrite = writeIndex.load(ordering: .acquiring)

        // Calculate available samples
        let available = Int(currentWrite &- currentRead)
        let toRead = min(count, available)

        if toRead > 0 {
            let readPos = Int(currentRead % UInt64(capacity))

            // Handle wrap-around
            let firstChunk = min(toRead, capacity - readPos)
            let secondChunk = toRead - firstChunk

            // Copy first chunk
            for i in 0..<firstChunk {
                destination[i] = buffer[readPos + i]
            }

            // Copy second chunk (wrapped)
            for i in 0..<secondChunk {
                destination[firstChunk + i] = buffer[i]
            }

            // Update read index with release ordering
            readIndex.store(currentRead &+ UInt64(toRead), ordering: .releasing)
        }

        return toRead
    }

    // MARK: - Status

    /// Number of samples currently available for reading
    var available: Int {
        let currentWrite = writeIndex.load(ordering: .acquiring)
        let currentRead = readIndex.load(ordering: .relaxed)
        return Int(currentWrite &- currentRead)
    }

    /// Number of samples that can be written without dropping
    var freeSpace: Int {
        return capacity - available
    }

    /// Total number of samples ever written
    var totalSamplesWritten: UInt64 {
        return totalWritten.load(ordering: .relaxed)
    }

    /// Number of samples dropped due to buffer overflow
    var totalSamplesDropped: UInt64 {
        return droppedSamples.load(ordering: .relaxed)
    }

    /// Buffer capacity in samples
    var bufferCapacity: Int {
        return capacity
    }

    /// Reset the buffer to empty state.
    ///
    /// Only safe to call when no concurrent reads/writes are occurring.
    func reset() {
        readIndex.store(0, ordering: .relaxed)
        writeIndex.store(0, ordering: .relaxed)
    }
}
