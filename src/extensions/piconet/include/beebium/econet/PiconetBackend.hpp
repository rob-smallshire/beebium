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

#pragma once

#include "beebium/econet/NetworkBackend.hpp"
#include "beebium/econet/piconet/Mode.hpp"
#include "beebium/econet/piconet/PiconetConfig.hpp"
#include "beebium/econet/piconet/SerialPort.hpp"

#include <moodycamel/readerwriterqueue.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace beebium {

// Network backend that bridges Beebium's emulated Econet to a real Econet
// network via a Piconet USB-CDC serial device. Runs in aun_mode -- the
// FourWayHandshake decorator must be active because Piconet's wire-level
// four-way handshake is opaque to the host (frames in/out are atomic).
//
// Threading model: a dedicated reader thread drains the SerialPort,
// accumulates bytes into a line buffer, parses complete event lines, and
// pushes the resulting NetworkFrames onto a lock-free ReaderWriterQueue.
// The emulation thread drains the queue via receive_frame(). All writes
// to the SerialPort happen on the emulation thread (send_frame and
// on_station_id_changed) per the threading contract documented in
// SerialPort.hpp -- no internal mutex.
class PiconetBackend : public NetworkBackend {
public:
    // Factory that opens a fresh SerialPort for a given device path. Used
    // by process_pending_reopen() to rebuild the serial after a path
    // change. Production code passes a factory that constructs the
    // platform-specific SerialPort (PosixSerialPort / Win32SerialPort);
    // tests pass a factory that returns a scripted mock. When null, the
    // reopen request is silently dropped (the pre-opened serial handed
    // in at construction time is the only one the backend will ever own).
    using SerialFactory =
        std::function<std::unique_ptr<piconet::SerialPort>(const std::string&)>;

    // Constructor opens the SerialPort, sends SET_STATION + SET_MODE LISTEN,
    // and starts the reader thread. Failures during startup mark the
    // backend disconnected (is_connected() returns false) but do not throw,
    // matching AunBackend's behaviour. The destructor signals shutdown,
    // closes the serial port (which unblocks the reader's timed read), and
    // joins the reader thread.
    //
    // serial_factory is used only by process_pending_reopen() to
    // construct a replacement SerialPort when the user re-points the
    // adapter via the Extension UI. When null, request_reopen() is a
    // no-op.
    //
    // on_async_state_change is invoked by the reader thread whenever
    // PiconetBackend's externally-observable state changes for reasons
    // other than a synchronous call from the emulation thread (today:
    // hot-unplug detection, serial read errors that close the port,
    // completion of a pending reopen). PiconetEconetTransportExtension
    // wires this to PiconetUi::mark_dirty() so the Extension UI framework
    // knows to push a fresh View. If null, the callback is simply not
    // invoked. Must be thread-safe -- it runs on the reader thread (or
    // the emulation thread in the reopen-completion case).
    PiconetBackend(piconet::PiconetConfig config,
                   std::unique_ptr<piconet::SerialPort> serial,
                   SerialFactory serial_factory = nullptr,
                   std::function<void()> on_async_state_change = nullptr);

    ~PiconetBackend() override;

    void send_frame(const NetworkFrame& frame) override;

    // Non-blocking. Returns the next NetworkFrame from the reader thread's
    // queue, or nullopt if empty.
    std::optional<NetworkFrame> receive_frame() override;

    // NetworkBackend interface: "is the BBC actually in two-way comms with
    // the wire?" -- requires both the USB-CDC link to the adapter to be
    // open AND the firmware to be in LISTEN mode. STOP-mode means the
    // Pico is silently dropping all traffic, so functionally we are not
    // connected. Symmetric with AunBackend's "did the user simulate
    // unplugging the cable" semantics; both transports' is_connected()
    // now answers the same user-meaningful question, which lets the
    // Network sidebar's Connection state row be transport-agnostic.
    bool is_connected() const override {
        return is_serial_open() &&
               current_mode_.load(std::memory_order_acquire) == piconet::Mode::Listen;
    }

    // USB-level connection: is the serial port open and the adapter
    // physically reachable? Independent of firmware mode. Used by
    // PiconetService.GetStatus.serial_open (reports physical-layer
    // health) and PiconetUi's Indicator (distinguishes "USB unplugged"
    // from "muted via STOP mode" -- both produce zero traffic but for
    // different reasons the user can act on differently).
    bool is_serial_open() const noexcept {
        return serial_ && serial_->is_open();
    }

    // EconetSocket calls this when the BBC's station latch changes; we
    // re-issue SET_STATION to keep the Piconet device in sync. Per the
    // threading contract this is invoked on the emulation thread, so it
    // shares the SerialPort write path with send_frame().
    void on_station_id_changed(std::uint8_t new_station_id) override;

    // Switch the firmware between SET_MODE STOP / LISTEN / MONITOR.
    // Caches the new mode so mode() can report it without round-tripping
    // the device. Per the SerialPort threading contract this must be
    // called from the emulation thread (or the test driver thread that
    // owns the write path); ExtensionUi::handle_event invocations are
    // serialised by the gRPC server, so PiconetUi calls this from the
    // gRPC handler thread -- safe because the gRPC handler is the only
    // other writer for tests, and in production the emulator never
    // touches set_mode().
    void set_mode(piconet::Mode mode);

    // Last mode set via set_mode() (or the constructor's initial LISTEN).
    // Atomic so PiconetUi::build_view, called on the gRPC server thread,
    // can read it safely while another thread might be calling set_mode.
    piconet::Mode mode() const noexcept {
        return current_mode_.load(std::memory_order_acquire);
    }

    // Accessor for diagnostics / gRPC introspection.
    const piconet::PiconetConfig& config() const { return config_; }

    // Last OS-level error encountered opening the serial port, populated
    // when a reopen (see request_reopen) fails. Empty otherwise. Read by
    // PiconetUi to surface a human-readable diagnosis on the Indicator
    // after a user-initiated path change; distinct from the startup-open
    // error held by PiconetEconetTransportExtension (which describes the
    // pre-backend failure case).
    const std::string& open_error_message() const { return open_error_message_; }

    // Atomic snapshot of the state read by PiconetUi::build_view. Built
    // by the emulation thread under ui_mutex_; returned by value so the
    // caller (the gRPC SubscribeView thread) sees a consistent view
    // without holding the lock for the rest of build_view. This is the
    // sole supported cross-thread read of the racy fields (config_,
    // serial_, open_error_message_); direct accessors above are for the
    // emulation thread or single-threaded tests.
    //
    // Without this synchronisation, build_view (gRPC thread) racing
    // against process_pending_reopen (emulation thread) corrupts heap
    // memory near serial_factory_ on Windows -- the std::string
    // assignment to config_.device_path interleaved with the gRPC
    // thread's read of the same string produces UB that manifests as a
    // 32-bit zero write to PiconetBackend+0x48 (the std::function's
    // stored function pointer). See
    // docs/discussion/grpc-windows-streaming-race.md.
    struct UiSnapshot {
        std::string device_path;
        bool serial_open;
        bool listening;
        std::string open_error_message;
    };
    UiSnapshot ui_snapshot() const;

    // Request that the backend close its current SerialPort and reopen
    // the named device path. Safe to call from the gRPC handler thread;
    // the actual close / open / reader-restart runs on the emulation
    // thread at the start of the next receive_frame() tick, keeping the
    // emulation thread as the sole writer of serial_. Successive calls
    // before the emulation thread has picked up the pending request
    // coalesce to the most recent request. No-op when the backend was
    // constructed without a SerialFactory.
    //
    // target_mode is the firmware mode to leave the freshly-opened device
    // in. The manual device-path editor re-points a disabled adapter, so it
    // uses the default (Stop) and the user then Enables; discovery Retry
    // wants the station live immediately, so it passes Listen.
    void request_reopen(std::string new_path,
                        piconet::Mode target_mode = piconet::Mode::Stop);

private:
    void reader_loop();

    // Called by receive_frame() on the emulation thread. If a reopen
    // has been requested, orchestrates teardown + replacement open +
    // reader restart; on open failure leaves the backend in a closed
    // state with open_error_message_ populated. Fires
    // on_async_state_change_ so the UI re-renders.
    void process_pending_reopen();

    // A queued reopen request: the device path plus the firmware mode to
    // leave it in once opened.
    struct PendingReopen {
        std::string path;
        piconet::Mode target_mode = piconet::Mode::Stop;
    };

    // Returns the pending reopen request (and clears the slot) if one is
    // queued, otherwise nullopt.
    std::optional<PendingReopen> take_pending_reopen();

    // Closes the current SerialPort and joins the reader thread. The
    // close unblocks the reader's timed read; the reader's own
    // close + async-state-change side-effect fires once (benign:
    // bump_backend_status_sequence and mark_dirty are idempotent and
    // process_pending_reopen will issue its own at the end).
    void tear_down_active_serial();

    // Stores `fresh` (a SerialPort whose open failed) as the new
    // serial_, captures the OS-level error into open_error_message_,
    // and pushes mode to Stop. Backend is now in a closed state.
    void install_failed_serial(std::unique_ptr<piconet::SerialPort> fresh);

    // Stores `fresh` as the new serial_, clears open_error_message_,
    // sends SET_STATION + SET_MODE <target_mode> to the device, resets
    // shutdown_, and starts a fresh reader thread.
    void install_open_serial(std::unique_ptr<piconet::SerialPort> fresh,
                             piconet::Mode target_mode);

    // Bump the backend-status sequence and fire the async-state-change
    // callback, both gated on !shutdown_ so the destructor's
    // teardown doesn't race observers. Used by every place that
    // observably mutates is_connected() / is_serial_open() outside a
    // synchronous emulation-thread call.
    void notify_state_changed();

    // True iff a write to serial_ would be sensible right now. The
    // ctor-time, send-time and station-change-time guards all share
    // the same condition; the helper keeps them visually uniform and
    // gives a single named anchor for "is the wire writeable?".
    bool is_serial_writable() const noexcept {
        return serial_ && serial_->is_open();
    }

    piconet::PiconetConfig config_;
    std::unique_ptr<piconet::SerialPort> serial_;
    SerialFactory serial_factory_;

    moodycamel::ReaderWriterQueue<NetworkFrame> rx_queue_{64};
    std::atomic<bool> shutdown_{false};
    std::atomic<piconet::Mode> current_mode_{piconet::Mode::Stop};
    std::function<void()> on_async_state_change_;
    std::thread reader_thread_;

    // Cross-thread slot used by request_reopen() to post a new device
    // path (and its target mode) to the emulation thread. Heap-allocated
    // because there is no std::atomic for the struct. Ownership: whichever
    // thread exchanges the pointer out of the slot (or the destructor) is
    // responsible for deleting it.
    std::atomic<PendingReopen*> pending_reopen_{nullptr};

    // Populated by process_pending_reopen() when the new SerialPort
    // fails to open. Cleared on successful reopen.
    std::string open_error_message_;

    // Guards the cross-thread read path (PiconetUi::build_view on the
    // gRPC SubscribeView thread) against the emulation thread's writes
    // to config_.device_path / serial_ / open_error_message_. Held only
    // for the duration of an atomic field swap or the construction of a
    // UiSnapshot; the heavy lifting in process_pending_reopen
    // (tear_down_active_serial, the SerialPort open, the reader-thread
    // spawn) all run outside the lock.
    mutable std::mutex ui_mutex_;
};

}  // namespace beebium
