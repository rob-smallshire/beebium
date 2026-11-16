// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
// Copyright 2026 Mark J. Fisher
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

#include "SerialDevice.hpp"
#include "../devices/Mc6850.hpp"

#include <cstdint>
#include <optional>

namespace beebium {

// Serial ULA (SERPROC) emulation: the serial processor at &FE10.
//
// On the BBC Micro the Serial ULA generates the transmit and receive clocks
// for the MC6850 ACIA, selects between the RS423 connector and the cassette
// interface, and drives the cassette motor relay. It is a write-only control
// latch:
//
//   bits 0-2: Transmit rate select
//   bits 3-5: Receive rate select
//   bit  6:   RS423 (1) / cassette (0) select
//   bit  7:   Cassette motor relay
//
// Clock model. The ULA runs from 16 MHz / 13 = 1.2308 MHz. tick() is called
// once per 2 MHz cycle and a fractional accumulator yields 8 ULA clocks for
// every 13 ticks, so every interval is a whole number of ULA clocks and every
// rate comes out 0.16% above nominal, as on the machine. Each rate select is a
// divider of the ULA clock (DIVIDERS): 19200 is 1 ULA clock per receive or
// transmit clock, 9600 is 2, ... 75 is 256. The ACIA divides those clocks by 1,
// 16 or 64 itself; MOS 1.20 runs RS423 at /64.
//
// RS423 selected. The receive and transmit clocks are the two divider outputs.
// This class is the byte<->bit shifter between the ACIA and the byte-oriented
// host transport (SerialDataSource / SerialDataSink):
//
//   Receive (device -> Beeb): a byte pulled from the source is framed as the
//     ACIA's Word Select field specifies (start, 7 or 8 data bits LSB first, an
//     optional parity bit, 1 or 2 stop bits) and each level is held on RxD for
//     64 receive clocks, whatever the ACIA's divider: the far end runs at the
//     selected baud rate and knows nothing of it. At /64 the ACIA reads the
//     byte; at /16 or /1 it reads garbage or framing errors, as on the machine.
//     When idle RxD is high.
//   Transmit (Beeb -> device): the ACIA reports each bit it starts; the data
//     bits (LSB first) are assembled into a byte delivered to the sink on the
//     stop bit. /CTS follows the sink's flow control.
//
// Cassette selected. /DCD and /CTS are held low (AUG 20.7.4); the high-tone
// detector that drives /DCD in cassette mode comes with the tape path. The
// ACIA gets no receive clock and RxD idles high. The RS423 line still runs at
// its own rate, so bytes a far end sends meanwhile are framed and lost, as on
// the machine (and the device's has_data() poll keeps its tick). The transmit clock is the transmit divider output; transmitted
// bits go nowhere, so the RS423 sink receives nothing.
//
// Reading &FE10. The ULA has no read/write line and latches the data bus on
// any access; on a read the bus holds the address high byte, so a read acts as
// a write of &FE (%11111110: motor on, RS423 selected, receive 75, transmit
// 2400). The read
// itself returns open bus, supplied by the owning socket. This is an unverified
// decision of the cassette specification: Acorn does not document it, and the
// reference emulators and the CPLD replica of the ULA all do it.
class SerialUla {
public:
    // The silicon fitted: the Ferranti part or VLSI's second source. Recorded
    // for the cassette side, where the two differ.
    enum class Part : uint8_t { Ferranti, VLSI };

    // Control-register field masks.
    static constexpr uint8_t TX_BAUD_MASK   = 0x07;  // bits 0-2
    static constexpr uint8_t RX_BAUD_MASK   = 0x38;  // bits 3-5
    static constexpr uint8_t RX_BAUD_SHIFT  = 3;
    static constexpr uint8_t RS423_SELECT   = 0x40;  // bit 6
    static constexpr uint8_t MOTOR_ENABLE   = 0x80;  // bit 7

    // The value a read of &FE10 latches.
    static constexpr uint8_t READ_LATCH_VALUE = 0xFE;

    // Nominal baud rate indexed by the 3-bit select field (the BBC SERPROC
    // encoding used by OSBYTE 7/8), assuming the ACIA divides by 64.
    static constexpr unsigned BAUD_RATES[8] = {
        19200, 1200, 4800, 150, 9600, 300, 2400, 75,
    };

    // ULA clocks per receive or transmit clock, indexed by the select field.
    static constexpr uint32_t DIVIDERS[8] = {
        1, 16, 4, 128, 2, 64, 8, 256,
    };

    // RxD levels of an RS423 frame are held for this many receive clocks.
    static constexpr uint32_t RS423_CLOCKS_PER_BIT = 64;

    explicit SerialUla(Mc6850& acia, Part part = Part::Ferranti)
        : acia_(acia), part_(part) {
        reset();
    }

    // Non-copyable (holds a reference and transport pointers).
    SerialUla(const SerialUla&) = delete;
    SerialUla& operator=(const SerialUla&) = delete;

    // Memory-mapped write (the latch ignores the address within its range).
    void write(uint16_t /*offset*/, uint8_t value) {
        control_ = value;
        tx_divider_ = DIVIDERS[value & TX_BAUD_MASK];
        rx_divider_ = DIVIDERS[(value & RX_BAUD_MASK) >> RX_BAUD_SHIFT];

        // RS423 forces carrier present; in cassette mode /DCD comes from the
        // high-tone detector, which is not yet modelled, so it is low too.
        acia_.set_not_dcd(false);
        refresh_cts();
    }

    // A read of the latch's address acts as a write of &FE. Returns the latch's
    // own contribution to the bus, which is nothing: the socket supplies the
    // open-bus value.
    uint8_t read(uint16_t offset) {
        write(offset, READ_LATCH_VALUE);
        return 0xFF;
    }

    // Advance by one 2MHz tick (called from SerialSocket).
    void tick() {
        // Forward RTS and BREAK transitions to the device. Both are line
        // conditions rather than data, so they are polled every tick (a cheap
        // bool compare) and delivered out of band: a control-register write that
        // changes RTS must reach the device even while /CTS is holding the
        // guest's transmit loop.
        update_rts();
        update_break();

        // 16/13 MHz from 2 MHz: 8 ULA clocks every 13 ticks.
        ula_phase_ += 8;
        if (ula_phase_ < 13) {
            return;
        }
        ula_phase_ -= 13;
        ula_clock();
    }

    void reset() {
        control_ = 0;
        tx_divider_ = DIVIDERS[0];
        rx_divider_ = DIVIDERS[0];
        tx_div_count_ = 0;
        rx_div_count_ = 0;
        cts_refresh_count_ = 0;
        tx_byte_ = 0;
        tx_bit_index_ = 0;
        rx_state_ = RxByteState::Idle;
        rx_level_ = true;
        rx_level_clocks_ = RS423_CLOCKS_PER_BIT - 1;
        rx_byte_ = 0;
        rx_bit_index_ = 0;
        rx_data_bits_ = 8;
        rx_stop_bits_ = 1;
        rx_stop_index_ = 0;
        rx_parity_ = Mc6850::Parity::None;
        rx_has_byte_ = false;
        rx_break_ = false;
        // The power-on latch selects cassette: /DCD and /CTS low.
        acia_.set_not_dcd(false);
        acia_.set_not_cts(false);
        // Re-baseline RTS and BREAK to the (reset) ACIA level and re-deliver to
        // any attached device so its view tracks the machine reset.
        last_rts_asserted_ = !acia_.get_not_rts();
        last_break_asserted_ = acia_.tx_control() == Mc6850::TX_CTRL_RTS_LOW_BREAK;
        if (sink_) {
            sink_->set_rts(last_rts_asserted_);
            sink_->set_break(last_break_asserted_);
        }
    }

    // --- Transport wiring (non-owning; the SerialSocket owns the backend) ---
    void set_source(SerialDataSource* source) { source_ = source; }
    void set_sink(SerialDataSink* sink) {
        sink_ = sink;
        refresh_cts();
        if (sink_ == nullptr) {
            return;
        }
        // Deliver the current RTS and BREAK levels to the freshly attached
        // device so it starts from the right baseline rather than waiting for a
        // transition.
        last_rts_asserted_ = !acia_.get_not_rts();
        sink_->set_rts(last_rts_asserted_);
        last_break_asserted_ = acia_.tx_control() == Mc6850::TX_CTRL_RTS_LOW_BREAK;
        sink_->set_break(last_break_asserted_);
    }

    // --- Accessors for tests / debugging ---
    uint8_t control() const { return control_; }
    Part part() const { return part_; }
    bool motor_on() const { return (control_ & MOTOR_ENABLE) != 0; }
    bool rs423_selected() const { return (control_ & RS423_SELECT) != 0; }
    unsigned tx_baud() const { return BAUD_RATES[control_ & TX_BAUD_MASK]; }
    unsigned rx_baud() const { return BAUD_RATES[(control_ & RX_BAUD_MASK) >> RX_BAUD_SHIFT]; }
    // ULA clocks per transmit or receive clock for the selected rates.
    uint32_t tx_divider() const { return tx_divider_; }
    uint32_t rx_divider() const { return rx_divider_; }
    // Running counts of ULA clocks and of the clocks delivered to the ACIA.
    uint64_t ula_clocks() const { return ula_clocks_; }
    uint64_t tx_clocks() const { return tx_clocks_; }
    uint64_t rx_clocks() const { return rx_clocks_; }

private:
    // Bytes-to-bits receive framing state.
    enum class RxByteState : uint8_t { Idle, Start, Data, Parity, Stop };

    // One ULA clock: run both dividers.
    void ula_clock() {
        ++ula_clocks_;
        if (++tx_div_count_ >= tx_divider_) {
            tx_div_count_ = 0;
            ++tx_clocks_;
            transmit_clock();
        }
        // The RS423 line runs whichever input is selected: a far end sending
        // while cassette is selected sends to nobody, and its bytes are lost.
        if (++rx_div_count_ >= rx_divider_) {
            rx_div_count_ = 0;
            receive_clock();
        }
    }

    // /CTS: in RS423 mode it follows the device's flow control; in cassette
    // mode it is held low.
    void refresh_cts() {
        acia_.set_not_cts(rs423_selected() && sink_ != nullptr && !sink_->accepts_more());
    }

    // Forward an RTS transition to the attached device (edge-triggered, so the
    // device sees one call per change rather than one per tick).
    void update_rts() {
        if (sink_ == nullptr) {
            return;
        }
        const bool rts_asserted = !acia_.get_not_rts();
        if (rts_asserted != last_rts_asserted_) {
            last_rts_asserted_ = rts_asserted;
            sink_->set_rts(rts_asserted);
        }
    }

    // Forward a BREAK transition to the attached device (edge-triggered). The
    // guest asserts a break by writing the Transmitter-Control field = 11
    // (TX_CTRL_RTS_LOW_BREAK, e.g. via OSBYTE &9C) and clears it to end the
    // break; this surfaces those start/stop edges to the device out of band.
    void update_break() {
        if (sink_ == nullptr) {
            return;
        }
        const bool break_asserted =
            acia_.tx_control() == Mc6850::TX_CTRL_RTS_LOW_BREAK;
        if (break_asserted != last_break_asserted_) {
            last_break_asserted_ = break_asserted;
            sink_->set_break(break_asserted);
        }
    }

    // One transmit clock: clock the ACIA and, in RS423 mode, assemble the
    // outgoing byte from the bits it reports.
    void transmit_clock() {
        // Drive /CTS from the device's flow control: when the device cannot
        // accept more, the ACIA holds TDRE low so the guest's transmit loop
        // busy-waits until the device drains. Refreshed every 64 transmit
        // clocks (one bit at /64), so /CTS releases promptly once the device
        // reports ready again without polling the device on every clock.
        if (++cts_refresh_count_ >= RS423_CLOCKS_PER_BIT) {
            cts_refresh_count_ = 0;
            refresh_cts();
        }

        const std::optional<Mc6850::TransmitResult> result = acia_.transmit_clock();
        if (!result || !rs423_selected()) {
            return;  // between bit boundaries, or cassette: the bits go nowhere
        }
        switch (result->type) {
            case Mc6850::BitType::Start:
                tx_byte_ = 0;
                tx_bit_index_ = 0;
                break;
            case Mc6850::BitType::Data:
                // LSB first.
                if (result->bit && tx_bit_index_ < 8) {
                    tx_byte_ |= static_cast<uint8_t>(1u << tx_bit_index_);
                }
                ++tx_bit_index_;
                break;
            case Mc6850::BitType::Stop:
                if (sink_) {
                    sink_->add_byte(tx_byte_);
                }
                break;
            default:
                break;
        }
    }

    // One RS423 receive divider clock: every 64 clocks move the line to the
    // next level of the frame. With RS423 selected the clock and the level go
    // to the ACIA; in cassette mode the receive clock will come from the data
    // separator, and until then the ACIA has none.
    void receive_clock() {
        if (++rx_level_clocks_ >= RS423_CLOCKS_PER_BIT) {
            rx_level_clocks_ = 0;
            rx_level_ = next_receive_level() != 0;
        }
        if (rs423_selected()) {
            ++rx_clocks_;
            acia_.receive_clock(rx_level_);
        }
    }

    // The next RxD level of the inbound RS423 frame, one per bit time.
    //
    // The frame follows the ACIA's Word Select field (7 or 8 data bits, an
    // optional parity bit, 1 or 2 stop bits), captured when the frame starts so
    // that a mid-character control-register write cannot corrupt it. Clocking a
    // fixed 8N1 frame into a receiver configured for, say, 7E1 (the viewdata
    // format) puts the two out of step and loses nearly every byte.
    uint8_t next_receive_level() {
        uint8_t bit = 1;  // line idles high
        if (source_) {
            switch (rx_state_) {
                case RxByteState::Idle:
                    // Poll for an inbound BREAK before ordinary data: a break is
                    // a line condition the device signals out of band. We clock
                    // it as an all-zeros frame with the stop bit forced low, so
                    // the ACIA latches a Framing Error with data 0x00 -- exactly
                    // how the MC6850 represents a received break.
                    if (!rx_has_byte_ && source_->take_break()) {
                        start_rx_frame(0, /*is_break=*/true);
                    } else if (!rx_has_byte_ && source_->has_data()) {
                        start_rx_frame(source_->next_byte(), /*is_break=*/false);
                    }
                    bit = 1;  // still idle this tick; start bit on the next
                    break;
                case RxByteState::Start:
                    bit = 0;  // start bit
                    rx_bit_index_ = 0;
                    rx_state_ = RxByteState::Data;
                    break;
                case RxByteState::Data:
                    // A break holds the line in the space state for the whole
                    // frame, so every bit is clocked low.
                    bit = rx_break_ ? 0 : ((rx_byte_ >> rx_bit_index_) & 1);
                    if (++rx_bit_index_ >= rx_data_bits_) {
                        rx_state_ = (rx_parity_ == Mc6850::Parity::None)
                            ? RxByteState::Stop : RxByteState::Parity;
                        rx_stop_index_ = 0;
                    }
                    break;
                case RxByteState::Parity:
                    bit = rx_break_ ? 0 : parity_bit();
                    rx_state_ = RxByteState::Stop;
                    rx_stop_index_ = 0;
                    break;
                case RxByteState::Stop:
                    // A genuine stop bit is high; a break frame forces it low to
                    // raise the receiver's Framing Error.
                    bit = rx_break_ ? 0 : 1;
                    if (++rx_stop_index_ >= rx_stop_bits_) {
                        rx_state_ = RxByteState::Idle;
                        rx_has_byte_ = false;
                        rx_break_ = false;
                    }
                    break;
            }
        }
        return bit;
    }

    // Latch a byte and the ACIA's current frame shape, then start clocking it.
    void start_rx_frame(uint8_t byte, bool is_break) {
        rx_data_bits_ = acia_.eight_bit() ? 8 : 7;
        rx_stop_bits_ = acia_.one_stop_bit() ? 1 : 2;
        rx_parity_ = acia_.parity();
        // A 7-bit line carries only seven bits; drop the eighth so the ACIA
        // receives what the wire would really have delivered and the parity bit
        // is computed over the same data.
        rx_byte_ = (rx_data_bits_ == 8) ? byte : static_cast<uint8_t>(byte & 0x7F);
        rx_break_ = is_break;
        rx_has_byte_ = true;
        rx_state_ = RxByteState::Start;
    }

    // The parity bit for the frame being clocked: even parity makes the total
    // number of one bits (data plus parity) even, odd parity makes it odd.
    uint8_t parity_bit() const {
        uint8_t ones = 0;
        for (uint8_t i = 0; i < rx_data_bits_; ++i) {
            ones ^= static_cast<uint8_t>((rx_byte_ >> i) & 1);
        }
        return (rx_parity_ == Mc6850::Parity::Even) ? ones
                                                    : static_cast<uint8_t>(ones ^ 1);
    }

    Mc6850& acia_;
    Part part_;

    uint8_t control_ = 0;

    // The ULA clock and its dividers.
    uint32_t ula_phase_ = 0;          // eighths of a 2MHz tick, modulo 13
    uint32_t tx_divider_ = 1;
    uint32_t rx_divider_ = 1;
    uint32_t tx_div_count_ = 0;
    uint32_t rx_div_count_ = 0;
    uint32_t cts_refresh_count_ = 0;
    uint64_t ula_clocks_ = 0;
    uint64_t tx_clocks_ = 0;
    uint64_t rx_clocks_ = 0;

    // Transmit byte assembly
    uint8_t tx_byte_ = 0;
    uint8_t tx_bit_index_ = 0;

    // Receive byte framing. The shape fields are captured from the ACIA when a
    // frame starts and hold for its duration.
    RxByteState rx_state_ = RxByteState::Idle;
    bool rx_level_ = true;                // current RxD level
    uint32_t rx_level_clocks_ = 0;        // receive clocks the level has been held
    uint8_t rx_byte_ = 0;
    uint8_t rx_bit_index_ = 0;
    uint8_t rx_data_bits_ = 8;
    uint8_t rx_stop_bits_ = 1;
    uint8_t rx_stop_index_ = 0;
    Mc6850::Parity rx_parity_ = Mc6850::Parity::None;
    bool rx_has_byte_ = false;
    bool rx_break_ = false;  // current inbound frame is a break (stop bit low)

    // Last RTS / BREAK levels delivered to the attached device, for edge
    // detection.
    bool last_rts_asserted_ = false;
    bool last_break_asserted_ = false;

    SerialDataSource* source_ = nullptr;
    SerialDataSink* sink_ = nullptr;
};

}  // namespace beebium
