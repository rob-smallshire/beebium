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
#include "../cassette/CassetteDeck.hpp"
#include "../cassette/TapeSink.hpp"
#include "../devices/Mc6850.hpp"

#include <cstdint>
#include <functional>
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
// Cassette selected. The receive clock and RxD come from the data separator,
// /DCD from the high-tone detector, and /CTS is held low (AUG 20.7.4); the
// receive rate field is ignored. The transmit clock is the transmit divider
// output, and TxD drives the tone synthesiser; the RS423 sink receives nothing.
// The RS423 line still runs at its own rate, so bytes a far end sends meanwhile
// are framed and lost, as on the machine.
//
// The cassette side runs on the ULA clock in every mode, from the parameter
// table of the cassette specification (findings 7.1), all intervals in ULA
// clocks:
//
//   Data separator. Each edge of the squared tape signal starts a burst of 4
//     receive-clock pulses of 4 clocks each (1.625 us low, 1.625 us high); an
//     edge during a burst ends it and starts a fresh one. 350 clocks (284 us)
//     with no edge emit one more burst and mark the gap long. At each edge RxD
//     is decided from the gap just ended: a long gap gives 0, two consecutive
//     short gaps give 1, otherwise it holds. So a 1200-baud bit of either tone
//     is 16 receive clocks and a 300-baud bit 64, and the ACIA's divider alone
//     sets the rate. The edge-to-burst delay of the real part is not modelled.
//   High-tone detector. A counter advances once per 256-clock cell while RxD is
//     1, edges are arriving and the motor bit is set; it clears on RxD 0 or
//     motor off, and holds through silence with the motor on. At 962 cells
//     (Ferranti, 200 ms) or 445 (VLSI, 92.7 ms) /DCD is driven high for one
//     cell and the counter holds until cleared. With RS423 selected /DCD is
//     forced low but the counter runs; switching to cassette does not clear it.
//   Tone synthesiser. With cassette selected and RTS low, TxD is sampled at
//     each 1024-clock cell boundary and the cell is filled with two cycles of
//     2403.8 Hz (mark) or one of 1201.9 Hz (space), phase-continuous, as edges
//     to an optional TapeSink. RTS high or RS423 selected gives silence.
//   VLSI part. With latch bit 3 set the tone sense is inverted on receive and
//     transmit; the Ferranti ignores bits 3 to 5 in cassette mode.
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

    // The cassette side, in ULA clocks.
    static constexpr uint32_t SEPARATOR_TIMEOUT_CLOCKS = 350;   // 284 us
    static constexpr uint32_t BURST_PULSES = 4;
    static constexpr uint32_t PULSE_CLOCKS = 4;                 // 1.625 us low, 1.625 us high
    static constexpr uint32_t BURST_CLOCKS = BURST_PULSES * PULSE_CLOCKS;
    static constexpr uint32_t DETECTOR_CELL_CLOCKS = 256;       // 208 us
    static constexpr uint32_t FERRANTI_HIGH_TONE_CELLS = 962;   // 200 ms
    static constexpr uint32_t VLSI_HIGH_TONE_CELLS = 445;       // 92.7 ms
    static constexpr uint32_t TONE_CELL_CLOCKS = 1024;          // one 1200-baud bit
    static constexpr uint8_t VLSI_TONE_INVERT = 0x08;           // latch bit 3

    explicit SerialUla(Mc6850& acia, Part part = Part::Ferranti)
        : acia_(acia), part_(part) {
        reset();
    }

    // Non-copyable (holds a reference and transport pointers).
    SerialUla(const SerialUla&) = delete;
    SerialUla& operator=(const SerialUla&) = delete;

    // Memory-mapped write (the latch ignores the address within its range).
    void write(uint16_t /*offset*/, uint8_t value) {
        const bool motor_was_on = motor_on();
        control_ = value;
        if (motor_on() != motor_was_on) {
            update_motor_led();
        }
        tx_divider_ = DIVIDERS[value & TX_BAUD_MASK];
        rx_divider_ = DIVIDERS[(value & RX_BAUD_MASK) >> RX_BAUD_SHIFT];

        // Motor off re-arms the high-tone detector; a write that keeps the
        // motor on, whether or not the value changes, does not.
        if (!motor_on()) {
            clear_high_tone();
        }
        // RS423 forces carrier present; in cassette mode /DCD is the detector's.
        acia_.set_not_dcd(!rs423_selected() && dcd_output_);
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
        const bool motor_was_on = motor_on();
        control_ = 0;
        if (motor_was_on) {
            update_motor_led();
        }
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
        // The cassette side: silence, RxD high, the detector cleared.
        clocks_since_edge_ = SEPARATOR_TIMEOUT_CLOCKS;
        separator_timed_out_ = true;
        previous_gap_short_ = false;
        recovered_ = true;
        burst_clocks_left_ = 0;
        high_tone_cells_ = 0;
        dcd_output_ = false;
        dcd_pulse_clocks_left_ = 0;
        tx_level_ = true;
        tone_enabled_ = false;
        tone_mark_ = true;
        tone_level_ = false;
        // The power-on latch selects cassette with the motor off: /DCD and
        // /CTS low.
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

    // Told the relay's state (latch bit 7) now and whenever it changes, on the
    // thread that writes the latch. The keyboard's cassette motor LED hangs
    // here (cassette/CassetteMotorLed.hpp).
    void set_motor_listener(std::function<void(bool)> listener) {
        motor_listener_ = std::move(listener);
        update_motor_led();
    }

    // --- The cassette side's attachments (non-owning) ---
    // The deck whose edges the data separator receives, clocked with the relay.
    void set_deck(CassetteDeck* deck) { deck_ = deck; }
    // Where the tone synthesiser's edges go; none drops them.
    void set_tape_sink(TapeSink* sink) { tape_sink_ = sink; }

    // --- Accessors for tests / debugging ---
    uint8_t control() const { return control_; }
    // The cassette RxD the separator presents to the ACIA (after any VLSI
    // inversion), the detector's count, and its /DCD output.
    bool cassette_rxd() const { return rxd(); }
    uint32_t high_tone_cells() const { return high_tone_cells_; }
    bool dcd_output() const { return dcd_output_; }
    uint32_t high_tone_threshold() const {
        return part_ == Part::VLSI ? VLSI_HIGH_TONE_CELLS : FERRANTI_HIGH_TONE_CELLS;
    }
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
        cassette_clock();
    }

    void update_motor_led() {
        if (motor_listener_) {
            motor_listener_(motor_on());
        }
    }

    // --- The cassette side, one ULA clock ---

    void cassette_clock() {
        const bool edge = deck_ != nullptr && deck_->clock(motor_on());
        // Idle: silence, no burst or pulse in progress, nothing listening to
        // the tone. Only the detector's cell boundary can matter.
        if (!edge && separator_timed_out_ && (burst_clocks_left_ | dcd_pulse_clocks_left_) == 0 &&
            tape_sink_ == nullptr) {
            if ((ula_clocks_ & (DETECTOR_CELL_CLOCKS - 1)) == 0) {
                detector_cell();
            }
            return;
        }
        if (edge) {
            separator_edge();
        } else if (!separator_timed_out_ && ++clocks_since_edge_ >= SEPARATOR_TIMEOUT_CLOCKS) {
            // No edge for 284 us: one more burst, and the gap is long.
            separator_timed_out_ = true;
            burst_clocks_left_ = BURST_CLOCKS;
        }
        if (burst_clocks_left_ != 0) {
            burst_clock();
        }
        // A /DCD pulse ends one cell after it began.
        if (dcd_pulse_clocks_left_ != 0 && --dcd_pulse_clocks_left_ == 0) {
            set_dcd_output(false);
        }
        if ((ula_clocks_ & (DETECTOR_CELL_CLOCKS - 1)) == 0) {
            detector_cell();
        }
        if (tape_sink_ != nullptr) {
            tone_clock();
        }
    }

    // An edge of the tape signal: decide RxD from the gap just ended and start
    // a fresh burst (ending any burst in progress).
    void separator_edge() {
        const bool long_gap = separator_timed_out_;
        if (long_gap) {
            set_recovered(false);
        } else if (previous_gap_short_) {
            set_recovered(true);
        }
        previous_gap_short_ = !long_gap;
        clocks_since_edge_ = 0;
        separator_timed_out_ = false;
        burst_clocks_left_ = BURST_CLOCKS;
    }

    // One clock of a burst: a receive-clock pulse rises every 4 clocks, and in
    // cassette mode the ACIA takes it with the current RxD.
    void burst_clock() {
        const uint32_t offset = BURST_CLOCKS - burst_clocks_left_;
        if (offset % PULSE_CLOCKS == 0 && !rs423_selected()) {
            ++rx_clocks_;
            acia_.receive_clock(rxd());
        }
        --burst_clocks_left_;
    }

    bool tone_inverted() const {
        return part_ == Part::VLSI && (control_ & VLSI_TONE_INVERT) != 0 && !rs423_selected();
    }

    bool rxd() const { return recovered_ != tone_inverted(); }

    void set_recovered(bool value) {
        recovered_ = value;
        if (!rxd()) {
            clear_high_tone();
        }
    }

    // One 256-clock cell of the high-tone detector.
    void detector_cell() {
        if (!motor_on() || !rxd()) {
            clear_high_tone();
            return;
        }
        if (separator_timed_out_) {
            return;  // silence with the motor on: hold
        }
        const uint32_t threshold = high_tone_threshold();
        if (high_tone_cells_ < threshold && ++high_tone_cells_ == threshold) {
            set_dcd_output(true);
            dcd_pulse_clocks_left_ = DETECTOR_CELL_CLOCKS;
        }
    }

    // Re-arm the detector: the count cleared and /DCD low.
    void clear_high_tone() {
        high_tone_cells_ = 0;
        dcd_pulse_clocks_left_ = 0;
        if (dcd_output_) {
            set_dcd_output(false);
        }
    }

    void set_dcd_output(bool high) {
        dcd_output_ = high;
        if (!rs423_selected()) {
            acia_.set_not_dcd(high);
        }
    }

    // One clock of the tone synthesiser: at each 1024-clock cell boundary take
    // TxD and whether the tone is on; within the cell an edge every half-cycle.
    void tone_clock() {
        const uint32_t offset = static_cast<uint32_t>(ula_clocks_ % TONE_CELL_CLOCKS);
        if (offset == 0) {
            tone_enabled_ = !rs423_selected() && !acia_.get_not_rts();
            tone_mark_ = tx_level_ != tone_inverted();
            if (!tone_enabled_) {
                tape_sink_->silence_until(ula_clocks_ + TONE_CELL_CLOCKS);
                return;
            }
        }
        if (!tone_enabled_) {
            return;
        }
        const uint32_t half_cycle = tone_mark_ ? TONE_CELL_CLOCKS / 4 : TONE_CELL_CLOCKS / 2;
        if (offset % half_cycle == 0) {
            tone_level_ = !tone_level_;
            tape_sink_->edge(ula_clocks_, tone_level_);
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
        if (!result) {
            return;  // between bit boundaries
        }
        tx_level_ = result->bit != 0;  // TxD, for the tone synthesiser
        if (!rs423_selected()) {
            return;  // cassette: the RS423 sink receives nothing
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
                    // This poll is also the device's tick: HostSerialEndpoint
                    // applies a pending re-point inside has_data(), and only this
                    // framer calls it, which is why the framer runs in cassette
                    // mode too.
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

    // Told the relay's state as it changes.
    std::function<void(bool)> motor_listener_;

    // The cassette side.
    CassetteDeck* deck_ = nullptr;
    TapeSink* tape_sink_ = nullptr;
    uint32_t clocks_since_edge_ = SEPARATOR_TIMEOUT_CLOCKS;
    bool separator_timed_out_ = true;   // no edge for the timeout: the gap is long
    bool previous_gap_short_ = false;
    bool recovered_ = true;             // the separator's data, before any VLSI inversion
    uint32_t burst_clocks_left_ = 0;
    uint32_t high_tone_cells_ = 0;
    bool dcd_output_ = false;
    uint32_t dcd_pulse_clocks_left_ = 0;
    bool tx_level_ = true;              // TxD as the ACIA last set it
    bool tone_enabled_ = false;
    bool tone_mark_ = true;
    bool tone_level_ = false;

    // Last RTS / BREAK levels delivered to the attached device, for edge
    // detection.
    bool last_rts_asserted_ = false;
    bool last_break_asserted_ = false;

    SerialDataSource* source_ = nullptr;
    SerialDataSink* sink_ = nullptr;
};

}  // namespace beebium
