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

#include "beebium/econet/PiconetBackend.hpp"

#include "beebium/econet/FourWayHandshake.hpp"
#include "beebium/econet/piconet/Commands.hpp"
#include "beebium/econet/piconet/Constants.hpp"
#include "beebium/econet/piconet/Events.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace beebium {

namespace {

// Mask used to strip the scout's high control bit before placing the
// control byte into a NetworkFrame. Matches FourWayHandshake's
// CTRL_FUNCTION_MASK (0x7F).
constexpr std::uint8_t CTRL_FUNCTION_MASK = 0x7F;

// The Econet scout/data marker bit. FourWayHandshake stores ctrl bytes
// with this stripped (control_byte holds the function code, 0-127).
// On the wire, scouts and broadcasts/immediates need it set so the
// receiving station's ADLC sees the right frame type. PiconetBackend
// restores it before passing the byte to the firmware's TX/BCAST commands.
constexpr std::uint8_t WIRE_CTRL_HIGH_BIT = 0x80;

bool trace_enabled() {
    static const bool on = std::getenv("BEEBIUM_PICONET_TRACE") != nullptr;
    return on;
}

void write_to_serial(piconet::SerialPort& serial, std::string_view line) {
    auto bytes = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(line.data()), line.size()};
    auto result = serial.write(bytes);
    if (result.error || result.bytes != line.size()) {
        if (trace_enabled()) {
            std::cerr << "PiconetBackend: serial write incomplete ("
                      << result.bytes << "/" << line.size()
                      << ", error=" << result.error << ")\n";
        }
    }
}

// Construct a typed Unicast/Immediate NetworkFrame from a Piconet
// RX_TRANSMIT or RX_IMMEDIATE event. Returns nullopt on malformed input
// (scout < 6 bytes or data < 4 bytes).
//
// Wire-format extraction (matches piconet/board/src/econet.c frame layout):
//   scout = [dest_stn, dest_net, src_stn, src_net, ctrl, port, scout_extra...]
//   data  = [dest_stn, dest_net, src_stn, src_net, payload...]
//
// FourWayHandshake's NetworkFrame layout for incoming Unicast/Immediate
// has nf.data = [scout_extra_bytes, data_payload_bytes]; the addressing
// fields capture sender/receiver as separate uint8s.
std::optional<NetworkFrame> make_typed_frame(FrameType type,
                                             const std::vector<std::uint8_t>& scout,
                                             const std::vector<std::uint8_t>& data_frame) {
    if (scout.size() < 6 || data_frame.size() < 4) {
        return std::nullopt;
    }
    NetworkFrame nf;
    nf.type = type;
    nf.dest_stn     = scout[0];
    nf.dest_net     = scout[1];
    nf.src_stn      = scout[2];
    nf.src_net      = scout[3];
    nf.control_byte = static_cast<std::uint8_t>(scout[4] & CTRL_FUNCTION_MASK);
    nf.port         = scout[5];
    // Concatenate scout-extra (after the 6-byte scout header) and data
    // payload (after the 4-byte data header) -- matches FourWayHandshake's
    // outbound packing in handle_tx_data_after_scout_ack().
    nf.data.reserve((scout.size() - 6) + (data_frame.size() - 4));
    nf.data.insert(nf.data.end(), scout.begin() + 6, scout.end());
    nf.data.insert(nf.data.end(), data_frame.begin() + 4, data_frame.end());
    return nf;
}

// Construct a Broadcast NetworkFrame from a Piconet RX_BROADCAST event.
// Wire format: [dest=0xFF, dest_net=0xFF, src_stn, src_net, ctrl, port, payload...]
std::optional<NetworkFrame> make_broadcast_frame(const std::vector<std::uint8_t>& wire) {
    if (wire.size() < 6) return std::nullopt;
    NetworkFrame nf;
    nf.type         = FrameType::Broadcast;
    nf.dest_stn     = wire[0];
    nf.dest_net     = wire[1];
    nf.src_stn      = wire[2];
    nf.src_net      = wire[3];
    nf.control_byte = static_cast<std::uint8_t>(wire[4] & CTRL_FUNCTION_MASK);
    nf.port         = wire[5];
    if (wire.size() > 6) {
        nf.data.assign(wire.begin() + 6, wire.end());
    }
    return nf;
}

// Construct a bare Ack NetworkFrame for FourWayHandshake to consume on
// TX_RESULT OK. FourWayHandshake's handler at handle_incoming() Stage::DataSent
// branch ignores Ack addressing and uses its own saved_* values, so an
// empty Ack suffices to short-circuit the synthetic final-ack timer and
// complete the handshake immediately. Without this, every TX would wait
// for FINAL_ACK_TIMEOUT (5ms) or the watchdog (250ms).
NetworkFrame make_ack() {
    NetworkFrame nf;
    nf.type = FrameType::Ack;
    return nf;
}

// Construct a bare Nack for FourWayHandshake to consume when the firmware
// reports a transmission failed on the wire. Without it the handshake's timer
// synthesises a successful final ack and the guest is told a frame arrived
// that never did. Piconet cannot know a station is absent in advance -- its
// firmware runs the wire handshake and reports afterwards -- so reporting
// after the fact is the only honest option available to it.
NetworkFrame make_nack() {
    NetworkFrame nf;
    nf.type = FrameType::Nack;
    return nf;
}

}  // namespace

PiconetBackend::PiconetBackend(piconet::PiconetConfig config,
                               std::unique_ptr<piconet::SerialPort> serial,
                               SerialFactory serial_factory,
                               std::function<void()> on_async_state_change)
    : config_(std::move(config)),
      serial_(std::move(serial)),
      serial_factory_(std::move(serial_factory)),
      on_async_state_change_(std::move(on_async_state_change)) {
    if (trace_enabled()) {
        std::cerr << "PiconetBackend: constructed for device " << config_.device_path
                  << " station=" << static_cast<unsigned>(config_.initial_station) << "\n";
    }

    // If the serial port failed to open, leave the backend disconnected:
    // is_connected() will return false, send_frame()/receive_frame() are
    // no-ops, and the reader thread is never started. Capture the
    // OS-level error from the SerialPort so the UI Indicator can surface
    // it ("Cannot open device: ...") even though the backend itself is
    // alive -- this lets the ModalEditor's reopen path recover from a
    // wrong-path-at-startup without the extension needing a separate
    // "build a fresh backend" code path.
    if (!is_serial_writable()) {
        if (serial_) {
            auto why = serial_->open_error();
            if (!why.empty()) {
                std::lock_guard<std::mutex> lock(ui_mutex_);
                open_error_message_ = std::string(why);
            }
        }
        return;
    }

    // Initial handshake: tell the device our station, then put it in LISTEN
    // mode. Failures are logged via trace but do not abort -- the next
    // operation will observe the failure.
    write_to_serial(*serial_, piconet::format_set_station(config_.initial_station));
    write_to_serial(*serial_, piconet::format_set_mode(piconet::Mode::Listen));
    current_mode_.store(piconet::Mode::Listen, std::memory_order_release);

    reader_thread_ = std::thread([this] { reader_loop(); });
}

PiconetBackend::~PiconetBackend() {
    shutdown_.store(true, std::memory_order_relaxed);
    if (serial_) {
        // Closing the serial port unblocks the reader's timed read by
        // returning ReadResult{error=true}, which lets the loop exit
        // promptly even if no bytes were arriving.
        serial_->close();
    }
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
    // Drop any reopen request the emulation thread never got to.
    delete pending_reopen_path_.exchange(nullptr, std::memory_order_acq_rel);
}

void PiconetBackend::reader_loop() {
    std::string line_buffer;
    std::array<std::uint8_t, 512> buf{};

    // Periodically check the device path still exists; USB hot-unplug
    // (yanking the cable mid-session) makes /dev/tty.usbmodem* disappear
    // but does not necessarily produce a read error if the firmware is
    // silent (e.g. SET_MODE STOP) and the read sits in select() with
    // nothing to do. The stat() poll catches that case; the read-error
    // branch below catches the case where a read was in flight when the
    // device went away. Both paths close the serial port so is_open()
    // and is_connected() flip to false promptly.
    auto last_stat = std::chrono::steady_clock::now();
    constexpr auto STAT_INTERVAL = std::chrono::seconds(1);

    while (!shutdown_.load(std::memory_order_relaxed)) {
        auto result = serial_->read({buf.data(), buf.size()});
        if (result.error) {
            // Serial closed or hangup -- could be intentional (close()
            // from emulation thread / destructor) or device hot-unplug.
            // Either way close the fd so is_open() reflects reality
            // promptly. close() is atomic and idempotent.
            if (trace_enabled()) {
                std::cerr << "PiconetBackend: read error on "
                          << config_.device_path
                          << " -- closing serial port\n";
            }
            serial_->close();
            // is_connected() just flipped to false; kick
            // WatchEconetStatus subscribers (sequence bump) and the
            // per-extension View stream (UI mark_dirty via the
            // callback). notify_state_changed() gates on shutdown_,
            // so the destructor's racing close() doesn't trigger a
            // pointless push.
            notify_state_changed();
            return;
        }
        if (result.would_block) {
            // Idle tick: if the device path has disappeared, treat as
            // hot-unplug. Two implementations share the same outer
            // shape (STAT_INTERVAL cadence, serial_->close() + async
            // callback on detection) but use platform-specific device-
            // existence checks.
            auto now = std::chrono::steady_clock::now();
            if (now - last_stat >= STAT_INTERVAL) {
                last_stat = now;
#ifdef _WIN32
                // Extract the "COMn" portion from the configured device
                // path. QueryDosDeviceW on the bare name returns the
                // backing device-object path (e.g. \Device\USBSER000)
                // while the USB-CDC adapter is plugged, and fails with
                // ERROR_FILE_NOT_FOUND once it is unplugged. Skip the
                // poll entirely for non-COM paths (named-pipe tests)
                // so they don't get falsely flagged as unplugged.
                std::string com_name;
                {
                    const std::string& path = config_.device_path;
                    std::string_view sv(path);
                    if (sv.size() >= 4 && sv[0] == '\\' && sv[1] == '\\' &&
                        (sv[2] == '.' || sv[2] == '?') && sv[3] == '\\') {
                        sv.remove_prefix(4);
                    }
                    bool looks_like_com = sv.size() >= 4 &&
                        (sv[0] == 'C' || sv[0] == 'c') &&
                        (sv[1] == 'O' || sv[1] == 'o') &&
                        (sv[2] == 'M' || sv[2] == 'm');
                    if (looks_like_com) {
                        std::size_t digits_begin = 3;
                        std::size_t digits_end = digits_begin;
                        while (digits_end < sv.size() &&
                               sv[digits_end] >= '0' && sv[digits_end] <= '9') {
                            ++digits_end;
                        }
                        if (digits_end > digits_begin &&
                            digits_end == sv.size()) {
                            com_name.assign(sv.data(), sv.size());
                        }
                    }
                }
                if (!com_name.empty()) {
                    std::wstring wide(com_name.size(), L'\0');
                    for (std::size_t i = 0; i < com_name.size(); ++i) {
                        wide[i] = static_cast<wchar_t>(com_name[i]);
                    }
                    std::array<wchar_t, MAX_PATH> target{};
                    DWORD got = ::QueryDosDeviceW(
                        wide.c_str(), target.data(),
                        static_cast<DWORD>(target.size()));
                    if (got == 0 && ::GetLastError() == ERROR_FILE_NOT_FOUND) {
                        if (trace_enabled()) {
                            std::cerr << "PiconetBackend: device path "
                                      << config_.device_path
                                      << " no longer resolves -- treating as hot-unplug\n";
                        }
                        serial_->close();
                        notify_state_changed();
                        return;
                    }
                }
#else
                struct stat st;
                if (::stat(config_.device_path.c_str(), &st) != 0 &&
                    (errno == ENOENT || errno == ENODEV)) {
                    if (trace_enabled()) {
                        std::cerr << "PiconetBackend: device path "
                                  << config_.device_path
                                  << " no longer exists -- treating as hot-unplug\n";
                    }
                    serial_->close();
                    notify_state_changed();
                    return;
                }
#endif
            }
            continue;
        }

        line_buffer.append(reinterpret_cast<const char*>(buf.data()), result.bytes);

        // Drain every complete line currently buffered. The firmware emits
        // events terminated by '\n'; we tolerate a trailing '\r' before it.
        while (true) {
            auto newline = line_buffer.find(piconet::EVENT_TERMINATOR);
            if (newline == std::string::npos) break;

            std::string_view line(line_buffer.data(), newline);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }

            auto event = piconet::parse_event_line(line);
            std::visit([this](auto&& ev) {
                using T = std::decay_t<decltype(ev)>;
                if constexpr (std::is_same_v<T, piconet::RxTransmitEvent>) {
                    if (auto nf = make_typed_frame(FrameType::Unicast, ev.scout, ev.data)) {
                        rx_queue_.try_enqueue(std::move(*nf));
                    }
                } else if constexpr (std::is_same_v<T, piconet::RxImmediateEvent>) {
                    if (auto nf = make_typed_frame(FrameType::Immediate, ev.scout, ev.data)) {
                        rx_queue_.try_enqueue(std::move(*nf));
                    }
                } else if constexpr (std::is_same_v<T, piconet::RxBroadcastEvent>) {
                    if (auto nf = make_broadcast_frame(ev.data)) {
                        rx_queue_.try_enqueue(std::move(*nf));
                    }
                } else if constexpr (std::is_same_v<T, piconet::TxResultEvent>) {
                    // OK short-circuits the synthetic final-ack timer in
                    // FourWayHandshake (handle_incoming() Stage::DataSent);
                    // anything else abandons the transaction there, so the
                    // line falls idle and the guest learns the transmission
                    // failed rather than being told it succeeded.
                    if (ev.result == piconet::TxResult::Ok) {
                        rx_queue_.try_enqueue(make_ack());
                    } else {
                        if (trace_enabled()) {
                            std::cerr << "PiconetBackend: TX failed: "
                                      << piconet::to_string(ev.result) << "\n";
                        }
                        rx_queue_.try_enqueue(make_nack());
                    }
                } else if constexpr (std::is_same_v<T, piconet::ErrorEvent>) {
                    if (trace_enabled()) {
                        std::cerr << "PiconetBackend: device ERROR: " << ev.message << "\n";
                    }
                } else if constexpr (std::is_same_v<T, piconet::UnknownEvent>) {
                    if (trace_enabled()) {
                        std::cerr << "PiconetBackend: unknown event line: "
                                  << ev.raw_line << "\n";
                    }
                }
                // StatusEvent, ReplyResultEvent, MonitorEvent: nothing to
                // forward in phase 4. Status will inform diagnostics in
                // phase 5; the others have no consumer.
            }, event);

            line_buffer.erase(0, newline + 1);
        }
    }
}

void PiconetBackend::send_frame(const NetworkFrame& frame) {
    if (!is_serial_writable()) {
        return;  // Disconnected: drop silently. FourWayHandshake's watchdog cleans up.
    }

    switch (frame.type) {
        case FrameType::Unicast: {
            // FourWayHandshake packs nf.data as:
            //   [scout_extra_bytes (count = scout_payload_size(ctrl)), data_payload].
            // Piconet's TX command takes scout_extra and data as separate
            // base64 fields, so we split them here.
            const int extra_size =
                FourWayHandshake::scout_payload_size(frame.control_byte);
            std::span<const std::uint8_t> all{frame.data.data(), frame.data.size()};
            std::span<const std::uint8_t> scout_extra;
            std::span<const std::uint8_t> data = all;
            if (extra_size > 0 &&
                static_cast<std::size_t>(extra_size) <= all.size()) {
                scout_extra = all.subspan(0, extra_size);
                data = all.subspan(extra_size);
            } else if (extra_size > 0 && trace_enabled()) {
                std::cerr << "PiconetBackend: Unicast ctrl 0x" << std::hex
                          << static_cast<int>(frame.control_byte)
                          << " expects " << std::dec << extra_size
                          << " scout-extra bytes but data has only "
                          << all.size() << "; sending all as data field\n";
            }
            write_to_serial(*serial_, piconet::format_tx(
                frame.dest_stn, frame.dest_net,
                static_cast<std::uint8_t>(frame.control_byte | WIRE_CTRL_HIGH_BIT),
                frame.port,
                data, scout_extra));
            break;
        }

        case FrameType::Immediate: {
            // Immediates do not carry scout-extra (FourWayHandshake handles
            // ctrl 0x02-0x05 as Unicast scouts, not Immediates). Send via
            // TX with port=0 and an empty scout_extra field.
            write_to_serial(*serial_, piconet::format_tx(
                frame.dest_stn, frame.dest_net,
                static_cast<std::uint8_t>(frame.control_byte | WIRE_CTRL_HIGH_BIT),
                /*port=*/0,
                std::span<const std::uint8_t>{frame.data.data(), frame.data.size()},
                {}));
            break;
        }

        case FrameType::Broadcast: {
            // Piconet stamps the source station from its own SET_STATION value
            // and the dest is hardcoded 0xFF. The BCAST data field is the
            // bytes that come after the 4-byte address header on the wire,
            // i.e. [ctrl, port, payload...]. FourWayHandshake's nf.data
            // contains only the payload (ctrl/port are in the named fields),
            // so we prepend them here.
            // Size to the final length and fill by index (no realloc path for
            // GCC's optimiser to mis-analyse as -Wfree-nonheap-object).
            std::vector<std::uint8_t> wire_data(2 + frame.data.size());
            wire_data[0] = static_cast<std::uint8_t>(frame.control_byte | WIRE_CTRL_HIGH_BIT);
            wire_data[1] = frame.port;
            if (!frame.data.empty()) {
                std::memcpy(wire_data.data() + 2, frame.data.data(), frame.data.size());
            }
            write_to_serial(*serial_, piconet::format_bcast(wire_data));
            break;
        }

        case FrameType::Ack:
            // The wire-level four-way handshake has already completed inside
            // Piconet's firmware before the host sees TX_RESULT. Synthesised
            // Acks from FourWayHandshake have nowhere to go.
            break;

        case FrameType::ImmReply:
            // Replies to inbound immediate operations cannot be delivered:
            // Piconet's firmware does not support host-driven in-handshake
            // replies (the REPLY path was abandoned upstream -- see
            // docs/discussion/piconet-feasibility.md, "Immediate Operations
            // Limitation"). Drop silently.
            if (trace_enabled()) {
                std::cerr << "PiconetBackend: dropping ImmReply (Piconet's "
                             "REPLY path is unsupported by firmware)\n";
            }
            break;

        case FrameType::Nack:
            // AUN-only; not used in practice.
            break;

        case FrameType::RawFrame:
            // FourWayHandshake should never hand a RawFrame to a backend
            // operating in aun_mode. Defensive drop.
            if (trace_enabled()) {
                std::cerr << "PiconetBackend: dropping unexpected RawFrame "
                             "(should not occur in aun_mode)\n";
            }
            break;
    }
}

std::optional<NetworkFrame> PiconetBackend::receive_frame() {
    // Serialise any user-initiated reopen with the emulation thread's
    // own reads of serial_. receive_frame() is the natural tick entry
    // point: the emulation thread is here every frame, never simultaneously
    // with send_frame(), and the reopen is a no-op on the common path.
    process_pending_reopen();

    NetworkFrame frame;
    if (rx_queue_.try_dequeue(frame)) {
        return frame;
    }
    return std::nullopt;
}

void PiconetBackend::on_station_id_changed(std::uint8_t new_station_id) {
    if (!is_serial_writable()) {
        return;
    }
    config_.initial_station = new_station_id;  // Keep config in sync for diagnostics.
    write_to_serial(*serial_, piconet::format_set_station(new_station_id));
}

void PiconetBackend::set_mode(piconet::Mode mode) {
    if (!is_serial_writable()) {
        return;
    }
    write_to_serial(*serial_, piconet::format_set_mode(mode));
    const piconet::Mode prev = current_mode_.exchange(mode, std::memory_order_acq_rel);
    // is_connected() flips when we cross the Listen boundary in either
    // direction, so WatchEconetStatus subscribers need to be kicked.
    // Transitions between two non-Listen modes (Stop <-> Monitor) leave
    // is_connected() unchanged.
    if ((prev == piconet::Mode::Listen) != (mode == piconet::Mode::Listen)) {
        bump_backend_status_sequence();
    }
}

void PiconetBackend::request_reopen(std::string new_path) {
    if (!serial_factory_) {
        // Backend was constructed without a factory (typically a test
        // that doesn't exercise reopen). Silently drop -- the UI layer
        // gates the edit affordance, so production never lands here.
        return;
    }
    auto* next = new std::string(std::move(new_path));
    auto* old = pending_reopen_path_.exchange(next, std::memory_order_acq_rel);
    delete old;  // Coalesce: a stale request the emulation thread hasn't
                 // consumed is superseded by the newer one.
}

void PiconetBackend::process_pending_reopen() {
    auto pending = take_pending_reopen();
    if (!pending || !serial_factory_) {
        return;
    }

    tear_down_active_serial();

    {
        std::lock_guard<std::mutex> lock(ui_mutex_);
        config_.device_path = *pending;
    }
    auto fresh = serial_factory_(*pending);
    if (!fresh || !fresh->is_open()) {
        install_failed_serial(std::move(fresh));
    } else {
        install_open_serial(std::move(fresh));
    }
    notify_state_changed();
}

std::optional<std::string> PiconetBackend::take_pending_reopen() {
    std::string* pending =
        pending_reopen_path_.exchange(nullptr, std::memory_order_acq_rel);
    if (!pending) {
        return std::nullopt;
    }
    std::string out = std::move(*pending);
    delete pending;
    return out;
}

void PiconetBackend::tear_down_active_serial() {
    if (serial_) {
        serial_->close();
    }
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
}

void PiconetBackend::install_failed_serial(
    std::unique_ptr<piconet::SerialPort> fresh) {
    std::string why;
    if (fresh) {
        auto why_view = fresh->open_error();
        why = why_view.empty() ? std::string("unknown error")
                               : std::string(why_view);
    } else {
        why = "serial factory returned null";
    }
    {
        std::lock_guard<std::mutex> lock(ui_mutex_);
        open_error_message_ = std::move(why);
        serial_ = std::move(fresh);
    }
    current_mode_.store(piconet::Mode::Stop, std::memory_order_release);
}

void PiconetBackend::install_open_serial(
    std::unique_ptr<piconet::SerialPort> fresh) {
    {
        std::lock_guard<std::mutex> lock(ui_mutex_);
        open_error_message_.clear();
        serial_ = std::move(fresh);
    }

    // Reopen always lands in Stop. The user is re-pointing the
    // adapter while it is disabled; Enable is a deliberate next step
    // taken via the action button once the Indicator has flipped
    // green.
    write_to_serial(*serial_, piconet::format_set_station(config_.initial_station));
    write_to_serial(*serial_, piconet::format_set_mode(piconet::Mode::Stop));
    current_mode_.store(piconet::Mode::Stop, std::memory_order_release);

    // shutdown_ is not touched by tear_down_active_serial (that
    // sequence drives the reader out via read-error, not via the
    // shutdown flag) but reset defensively before spawning a fresh
    // reader.
    shutdown_.store(false, std::memory_order_relaxed);
    reader_thread_ = std::thread([this] { reader_loop(); });
}

PiconetBackend::UiSnapshot PiconetBackend::ui_snapshot() const {
    UiSnapshot snap;
    {
        std::lock_guard<std::mutex> lock(ui_mutex_);
        snap.device_path = config_.device_path;
        snap.serial_open = serial_ && serial_->is_open();
        snap.open_error_message = open_error_message_;
    }
    snap.listening =
        current_mode_.load(std::memory_order_acquire) == piconet::Mode::Listen;
    return snap;
}

void PiconetBackend::notify_state_changed() {
    if (shutdown_.load(std::memory_order_relaxed)) {
        return;
    }
    bump_backend_status_sequence();
    if (on_async_state_change_) {
        on_async_state_change_();
    }
}

}  // namespace beebium
