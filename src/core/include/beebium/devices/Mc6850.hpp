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

#include <cstdint>

namespace beebium {

// Motorola MC6850 Asynchronous Communications Interface Adapter (ACIA).
//
// The MC6850 is the serial communications chip on the BBC Micro motherboard.
// It sits in SHEILA at &FE08-&FE0F (two registers, mirrored on the low
// address bit):
//
//   Offset 0 read:  Status Register
//   Offset 0 write: Control Register
//   Offset 1 read:  Receive Data Register (RDR)
//   Offset 1 write: Transmit Data Register (TDR)
//
// The ACIA does not generate its own bit-rate clock. On the BBC the transmit
// and receive clocks are supplied by the Serial ULA (SERPROC, &FE10), which
// also selects RS423 vs cassette. The ACIA's own counter-divide bits select
// /1, /16 or /64 (or a master reset); on the BBC the MOS programs /64. Because
// the SerialUla in this emulator owns all bit-rate timing, this model processes
// exactly one serial bit per update_transmit()/update_receive() call and treats
// the counter-divide field as informational (apart from detecting master reset).
// This mirrors b2's approach ("REMOVED CLOCK DIVISION - SERPROC already handles
// timing").
//
// This is a bit-level model: the transmit and receive paths walk a start /
// data / (parity) / stop-bit state machine one bit at a time, tracking parity
// and framing errors and receiver overrun, exactly as the real device does.
// The SerialUla shifts those bits to and from a byte-oriented host transport.
//
// Conventions for the control/handshake pins follow the datasheet's active-low
// naming (/DCD, /CTS, /RTS): a "not_dcd" value of true means the /DCD pin is
// HIGH (no carrier); false means carrier present.
class Mc6850 {
public:
    // --- Control Register field decode (write to offset 0) ---
    //
    // bits 0-1: Counter Divide Select
    // bits 2-4: Word Select (data bits / parity / stop bits)
    // bits 5-6: Transmitter Control (/RTS level, TX IRQ enable, break)
    // bit  7:   Receive Interrupt Enable
    static constexpr uint8_t CR_COUNTER_DIVIDE_MASK = 0x03;
    static constexpr uint8_t CR_WORD_SELECT_MASK    = 0x1C;  // bits 2-4
    static constexpr uint8_t CR_WORD_SELECT_SHIFT   = 2;
    static constexpr uint8_t CR_TX_CONTROL_MASK     = 0x60;  // bits 5-6
    static constexpr uint8_t CR_TX_CONTROL_SHIFT    = 5;
    static constexpr uint8_t CR_RX_IRQ_ENABLE       = 0x80;

    // Counter Divide Select values (bits 0-1)
    static constexpr uint8_t COUNTER_DIVIDE_1            = 0x00;
    static constexpr uint8_t COUNTER_DIVIDE_16           = 0x01;
    static constexpr uint8_t COUNTER_DIVIDE_64           = 0x02;
    static constexpr uint8_t COUNTER_DIVIDE_MASTER_RESET = 0x03;

    // Transmitter Control values (bits 5-6)
    //   00: /RTS low, no TX interrupt
    //   01: /RTS low, TX interrupt enabled
    //   10: /RTS high, no TX interrupt
    //   11: /RTS low, transmit break, no TX interrupt
    static constexpr uint8_t TX_CTRL_RTS_LOW_NO_IRQ  = 0x00;
    static constexpr uint8_t TX_CTRL_RTS_LOW_IRQ     = 0x01;
    static constexpr uint8_t TX_CTRL_RTS_HIGH_NO_IRQ = 0x02;
    static constexpr uint8_t TX_CTRL_RTS_LOW_BREAK   = 0x03;

    // --- Status Register bits (read from offset 0) ---
    static constexpr uint8_t SR_RDRF    = 0x01;  // Receive Data Register Full
    static constexpr uint8_t SR_TDRE    = 0x02;  // Transmit Data Register Empty
    static constexpr uint8_t SR_NOT_DCD = 0x04;  // /DCD (1 = no carrier)
    static constexpr uint8_t SR_NOT_CTS = 0x08;  // /CTS (1 = not clear to send)
    static constexpr uint8_t SR_FE      = 0x10;  // Framing Error
    static constexpr uint8_t SR_OVRN    = 0x20;  // Receiver Overrun
    static constexpr uint8_t SR_PE      = 0x40;  // Parity Error
    static constexpr uint8_t SR_IRQ     = 0x80;  // Interrupt Request

    // Parity mode derived from the Word Select field.
    enum class Parity : uint8_t { None, Odd, Even };

    // The type of bit produced by update_transmit(). The SerialUla uses this to
    // frame outgoing bytes (it only needs Start/Data/Stop, but the full set is
    // reported for completeness and tracing).
    enum class BitType : uint8_t { None, Start, Data, Parity, Stop, Break };

    // One serialised transmit bit plus its role in the frame.
    struct TransmitResult {
        uint8_t bit = 1;            // line level (idle = 1)
        BitType type = BitType::None;
    };

    Mc6850() { reset(); }

    // --- Memory-mapped register interface (offset masked to the low bit) ---

    uint8_t read(uint16_t offset) {
        return (offset & 0x01) ? read_data() : read_status();
    }

    void write(uint16_t offset, uint8_t value) {
        if (offset & 0x01) {
            write_data(value);
        } else {
            write_control(value);
        }
    }

    // --- CPU-facing register operations ---

    void write_control(uint8_t value) {
        control_ = value;
        switch (value & CR_COUNTER_DIVIDE_MASK) {
            case COUNTER_DIVIDE_MASTER_RESET:
                reset();
                break;
            default:
                break;
        }
        update_irqs();
    }

    void write_data(uint8_t value) {
        // Loading TDR clears TDRE; the byte will be serialised by the TX state
        // machine, which is pumped one bit at a time from update_transmit().
        tdr_ = value;
        tx_tdre_ = false;
        irq_tx_ = false;
        update_irqs();
    }

    uint8_t read_status() {
        return status_value();
    }

    uint8_t read_data() {
        // Reading RDR clears RDRF and the receive IRQ cause, and reveals any
        // overrun that was latched while RDRF was still set.
        rdrf_ = false;
        irq_rx_ = false;
        update_irqs();

        if (rx_ovrn_pending_) {
            ovrn_ = true;
            rx_ovrn_pending_ = false;
        } else {
            ovrn_ = false;
        }
        return rdr_;
    }

    // Side-effect-free read of RDR/status for the debugger.
    uint8_t debug_read_data() const { return rdr_; }
    uint8_t debug_read_status() const { return status_value(); }

    // --- Handshake input pins (driven by the Serial ULA) ---

    // /DCD: true = pin HIGH = no carrier (receiver held idle).
    void set_not_dcd(bool not_dcd) { not_dcd_ = not_dcd; }

    // /CTS: true = pin HIGH = not clear to send (TDRE reported as 0).
    void set_not_cts(bool not_cts) { not_cts_ = not_cts; }

    // /RTS output level requested by the Transmitter Control field.
    bool get_not_rts() const {
        return tx_control() == TX_CTRL_RTS_HIGH_NO_IRQ;
    }

    // --- Bit-level serial engine (driven by the Serial ULA bit clock) ---

    // Advance the receive state machine by one received bit. The line idles
    // high (bit = 1); a start bit is a 0. Sets RDRF (and FE/PE/OVRN) when a
    // whole character has been clocked in.
    void update_receive(uint8_t bit) {
        // A 0->1 transition on /DCD (carrier lost) latches a receive interrupt
        // condition, matching the datasheet note that the Rx clock must be
        // running for /DCD to be processed.
        if (!old_not_dcd_ && not_dcd_) {
            irq_rx_ = true;
            update_irqs();
        }
        old_not_dcd_ = not_dcd_;

        switch (rx_state_) {
            case RxState::Idle:
                // The receiver only leaves idle once carrier is present.
                if (!not_dcd_) {
                    rx_state_ = RxState::StartBit;
                }
                break;

            case RxState::StartBit:
                if (!bit) {
                    rx_state_ = RxState::DataBits;
                    rx_mask_ = 1;
                    rx_data_ = 0;
                    rx_parity_ = 0;
                    rx_parity_error_ = false;
                    rx_framing_error_ = false;
                }
                break;

            case RxState::DataBits:
                if (bit) {
                    rx_data_ |= rx_mask_;
                    rx_parity_ ^= 1;
                }
                advance_data_mask(rx_mask_, rx_state_,
                                  RxState::ParityBit, RxState::StopBits);
                break;

            case RxState::ParityBit: {
                if (parity() == Parity::Even) {
                    rx_parity_ ^= 1;
                }
                // rx_parity_ == 1 means the expected parity is satisfied, in
                // which case the parity bit on the wire should be 0.
                if (rx_parity_ != (bit == 0)) {
                    rx_parity_error_ = true;
                }
                rx_state_ = RxState::StopBits;
                rx_mask_ = 1;
                break;
            }

            case RxState::StopBits: {
                if (!bit) {
                    rx_framing_error_ = true;
                }
                if (advance_stop_mask(rx_mask_, rx_state_, RxState::StartBit)) {
                    // A newly received character clears a stale overrun.
                    // "Character synchronization is maintained during the
                    // Overrun condition": the receiver keeps delivering, so the
                    // flag describes the gap in the data stream just past, not
                    // a condition the next character inherits. Leaving it set
                    // across an idle line makes the guest discard the next
                    // character to arrive as an error (issue #59), and holding
                    // RDRF set instead -- the datasheet's literal wording --
                    // wedges the receiver against a MOS that only reads on
                    // interrupt.
                    ovrn_ = false;
                    if (rdrf_) {
                        // Previous byte not yet read: overrun. The byte that was
                        // already in RDR is preserved; OVRN surfaces on the next
                        // read of RDR.
                        rx_ovrn_pending_ = true;
                    } else {
                        rdrf_ = true;
                        rdr_ = rx_data_;
                        fe_ = rx_framing_error_;
                        pe_ = rx_parity_error_;
                    }
                    irq_rx_ = true;
                    update_irqs();
                }
                break;
            }
        }
    }

    // Advance the transmit state machine by one bit time and return the line
    // level to drive plus its role in the frame.
    TransmitResult update_transmit() {
        switch (tx_state_) {
            case TxState::Idle:
                if (tx_control() == TX_CTRL_RTS_LOW_BREAK) {
                    return {0, BitType::Break};
                }
                if (tx_tdre_) {
                    // Nothing to send; hold the line idle high.
                    return {1, BitType::None};
                }
                // Begin a new character: latch TDR, mark TDRE, raise TX IRQ.
                tx_data_ = tdr_;
                tx_tdre_ = true;
                irq_tx_ = true;
                update_irqs();
                tx_state_ = TxState::DataBits;
                tx_mask_ = 1;
                tx_parity_ = 0;
                return {0, BitType::Start};

            case TxState::DataBits: {
                TransmitResult result{static_cast<uint8_t>((tx_data_ & tx_mask_) ? 1 : 0),
                                      BitType::Data};
                tx_parity_ ^= result.bit;
                advance_data_mask(tx_mask_, tx_state_,
                                  TxState::ParityBit, TxState::StopBits);
                return result;
            }

            case TxState::ParityBit:
                if (parity() == Parity::Even) {
                    tx_parity_ ^= 1;
                }
                tx_mask_ = 1;
                tx_state_ = TxState::StopBits;
                return {tx_parity_, BitType::Parity};

            case TxState::StopBits:
                advance_stop_mask(tx_mask_, tx_state_, TxState::Idle);
                return {1, BitType::Stop};
        }
        return {1, BitType::None};
    }

    // --- IRQ output (wired to the shared CPU IRQ line) ---

    // True when an enabled interrupt condition is active. Master reset
    // suppresses the IRQ output, as on the real device.
    bool irq_pending() const {
        if ((control_ & CR_COUNTER_DIVIDE_MASK) == COUNTER_DIVIDE_MASTER_RESET) {
            return false;
        }
        return irq_out_rx_ || irq_out_tx_;
    }

    // Master reset: clears the transmitter and receiver state machines and the
    // status register, leaving the /DCD and /CTS pin levels intact.
    void reset() {
        tx_state_ = TxState::Idle;
        tx_tdre_ = true;
        rx_state_ = RxState::Idle;
        rdrf_ = false;
        fe_ = false;
        pe_ = false;
        ovrn_ = false;
        rx_ovrn_pending_ = false;
        irq_rx_ = false;
        irq_tx_ = false;
        irq_out_rx_ = false;
        irq_out_tx_ = false;
    }

    // --- Accessors for tests / debugging ---

    uint8_t control() const { return control_; }
    uint8_t status() const { return status_value(); }
    uint8_t tx_control() const {
        return (control_ & CR_TX_CONTROL_MASK) >> CR_TX_CONTROL_SHIFT;
    }
    uint8_t word_select() const {
        return (control_ & CR_WORD_SELECT_MASK) >> CR_WORD_SELECT_SHIFT;
    }
    bool tdre() const { return tx_tdre_ && !not_cts_; }
    bool rdrf() const { return rdrf_; }
    bool not_dcd() const { return not_dcd_; }
    bool not_cts() const { return not_cts_; }

    // Word-format helpers (derived from the Word Select field).
    Parity parity() const { return PARITY_BY_WORD_SELECT[word_select()]; }
    bool eight_bit() const { return (word_select() & 0x04) != 0; }
    bool one_stop_bit() const { return ONE_STOP_BIT_BY_WORD_SELECT[word_select()]; }

private:
    enum class TxState : uint8_t { Idle, DataBits, ParityBit, StopBits };
    enum class RxState : uint8_t { Idle, StartBit, DataBits, ParityBit, StopBits };

    // Word Select tables (indexed by the 3-bit field). Mirrors the MC6850
    // datasheet encoding (and b2's PARITY_BY_WORD_SELECT/ONE_STOP_BIT tables):
    //   0: 7E2  1: 7O2  2: 7E1  3: 7O1  4: 8N2  5: 8N1  6: 8E1  7: 8O1
    static constexpr Parity PARITY_BY_WORD_SELECT[8] = {
        Parity::Even, Parity::Odd, Parity::Even, Parity::Odd,
        Parity::None, Parity::None, Parity::Even, Parity::Odd,
    };
    static constexpr bool ONE_STOP_BIT_BY_WORD_SELECT[8] = {
        false, false, true, true, false, true, true, true,
    };

    // Compose the status byte from latched flags plus the live pin levels.
    uint8_t status_value() const {
        uint8_t value = 0;
        if (rdrf_) value |= SR_RDRF;
        if (fe_)   value |= SR_FE;
        if (ovrn_) value |= SR_OVRN;
        if (pe_)   value |= SR_PE;
        if (not_dcd_) value |= SR_NOT_DCD;
        if (not_cts_) value |= SR_NOT_CTS;
        // Master reset forces TDRE and IRQ low until the divide bits are
        // reprogrammed to a non-reset value.
        if ((control_ & CR_COUNTER_DIVIDE_MASK) != COUNTER_DIVIDE_MASTER_RESET) {
            // /CTS high masks TDRE (the transmitter cannot signal "empty").
            if (tx_tdre_ && !not_cts_) value |= SR_TDRE;
            if (irq_out_rx_ || irq_out_tx_) value |= SR_IRQ;
        }
        return value;
    }

    // Gate the raw interrupt causes by their enables to form the output.
    void update_irqs() {
        irq_out_rx_ = irq_rx_ && (control_ & CR_RX_IRQ_ENABLE) != 0;
        irq_out_tx_ = irq_tx_ && tx_control() == TX_CTRL_RTS_LOW_IRQ;
    }

    // Shift the data-bit mask; on wrap select the parity-or-stop next state.
    template<typename StateType>
    void advance_data_mask(uint8_t& mask, StateType& state,
                           StateType parity_state, StateType no_parity_state) const {
        // 7-bit words wrap when the mask reaches 0x80; 8-bit words wrap when the
        // shift overflows the byte to 0x00.
        const uint8_t end = eight_bit() ? 0x00 : 0x80;
        mask = static_cast<uint8_t>(mask << 1);
        if (mask == end) {
            mask = 1;
            state = (parity() == Parity::None) ? no_parity_state : parity_state;
        }
    }

    // Shift the stop-bit mask; return true once the configured number of stop
    // bits (1 or 2) has been processed, advancing to next_state.
    template<typename StateType>
    bool advance_stop_mask(uint8_t& mask, StateType& state, StateType next_state) const {
        mask = static_cast<uint8_t>(mask << 1);
        if (mask == 4 || (mask == 2 && one_stop_bit())) {
            state = next_state;
            return true;
        }
        return false;
    }

    // Control / status state
    uint8_t control_ = 0;
    bool not_dcd_ = false;
    bool not_cts_ = false;
    bool old_not_dcd_ = false;

    uint8_t tdr_ = 0;   // transmit data register (CPU -> ACIA)
    uint8_t rdr_ = 0;   // receive data register (ACIA -> CPU)

    bool rdrf_ = false;
    bool fe_ = false;
    bool pe_ = false;
    bool ovrn_ = false;
    bool rx_ovrn_pending_ = false;

    // Receive bit-engine
    RxState rx_state_ = RxState::Idle;
    uint8_t rx_mask_ = 0;
    uint8_t rx_data_ = 0;
    uint8_t rx_parity_ = 0;
    bool rx_parity_error_ = false;
    bool rx_framing_error_ = false;

    // Transmit bit-engine
    TxState tx_state_ = TxState::Idle;
    uint8_t tx_data_ = 0;
    uint8_t tx_mask_ = 0;
    uint8_t tx_parity_ = 0;
    bool tx_tdre_ = true;

    // Raw interrupt causes and their gated outputs
    bool irq_rx_ = false;
    bool irq_tx_ = false;
    bool irq_out_rx_ = false;
    bool irq_out_tx_ = false;
};

}  // namespace beebium
