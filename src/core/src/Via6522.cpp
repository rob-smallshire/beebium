#include <beebium/Via6522.hpp>

namespace beebium {

Via6522::Via6522() : peripheral_(nullptr) {
    reset();
}

Via6522::Via6522(ViaPeripheral& peripheral) : peripheral_(&peripheral) {
    reset();
}

Via6522::Via6522(ViaPeripheral* peripheral) : peripheral_(peripheral) {
    reset();
}

void Via6522::reset() {
    state_ = Via6522State{};
}

//////////////////////////////////////////////////////////////////////////////
// Register reads
//////////////////////////////////////////////////////////////////////////////

uint8_t Via6522::read(uint16_t offset) {
    switch (offset & 0x0F) {
    case REG_ORB: {  // IRB - Input Register B
        // Update peripheral to get current input values before reading
        update_port_pins();

        uint8_t value = state_.port_b.or_ & state_.port_b.ddr;

        if (state_.acr.bits.pb_latching) {
            value |= state_.port_b.p_latch & ~state_.port_b.ddr;
        } else {
            value |= state_.port_b.p & ~state_.port_b.ddr;
        }

        // IRB reads always reflect the PB7 output value when T1 output is active
        if (state_.acr.bits.t1_output_pb7) {
            value &= 0x7F;
            value |= state_.t1_pb7;
        }

        // Clear port B interrupt flags
        state_.ifr.bits.cb1 = 0;
        // Don't clear CB2 if in independent interrupt mode
        if ((state_.pcr.bits.cb2_mode & 5) != 1) {
            state_.ifr.bits.cb2 = 0;
        }

        return value;
    }

    case REG_ORA: {  // IRA with handshake
        // Clear port A interrupt flags
        state_.ifr.bits.ca1 = 0;
        // Don't clear CA2 if in independent interrupt mode
        if ((state_.pcr.bits.ca2_mode & 5) != 1) {
            state_.ifr.bits.ca2 = 0;
        }

        // Read handshaking
        switch (state_.pcr.bits.ca2_mode) {
        case static_cast<uint8_t>(ViaCx2Control::Output_Handshake):
            state_.port_a.c2 = 0;
            break;
        case static_cast<uint8_t>(ViaCx2Control::Output_Pulse):
            state_.port_a.c2 = 0;
            state_.port_a.pulse = 2;
            break;
        }

        // Fall through to no-handshake read
    }
    [[fallthrough]];

    case REG_ORA_NH:  // IRA without handshake
        // Update peripheral to get current input values before reading
        update_port_pins();
        if (state_.acr.bits.pa_latching) {
            return state_.port_a.p_latch;
        } else {
            return state_.port_a.p;
        }

    case REG_DDRB:
        return state_.port_b.ddr;

    case REG_DDRA:
        return state_.port_a.ddr;

    case REG_T1CL:  // Timer 1 Counter Low
        // Don't acknowledge IRQ if T1 just timed out this cycle
        if (!state_.t1_timeout) {
            state_.ifr.bits.t1 = 0;
        }
        return static_cast<uint8_t>(state_.t1);

    case REG_T1CH:  // Timer 1 Counter High
        return static_cast<uint8_t>(state_.t1 >> 8);

    case REG_T1LL:  // Timer 1 Latch Low
        return state_.t1ll;

    case REG_T1LH:  // Timer 1 Latch High
        return state_.t1lh;

    case REG_T2CL:  // Timer 2 Counter Low
        if (!state_.t2_timeout) {
            state_.ifr.bits.t2 = 0;
        }
        return static_cast<uint8_t>(state_.t2);

    case REG_T2CH:  // Timer 2 Counter High
        return static_cast<uint8_t>(state_.t2 >> 8);

    case REG_SR:  // Shift Register
        return state_.sr;

    case REG_ACR:  // Auxiliary Control Register
        return state_.acr.value;

    case REG_PCR:  // Peripheral Control Register
        return state_.pcr.value;

    case REG_IFR: {  // Interrupt Flag Register
        uint8_t value = state_.ifr.value & 0x7F;
        // Set bit 7 if any enabled interrupt is pending
        if (state_.ier.value & state_.ifr.value & 0x7F) {
            value |= 0x80;
        }
        return value;
    }

    case REG_IER:  // Interrupt Enable Register
        // Bit 7 always reads as 1
        return state_.ier.value | 0x80;

    default:
        return 0xFF;
    }
}

//////////////////////////////////////////////////////////////////////////////
// Register writes
//////////////////////////////////////////////////////////////////////////////

void Via6522::write(uint16_t offset, uint8_t value) {
    switch (offset & 0x0F) {
    case REG_ORB:  // ORB - Output Register B
        state_.port_b.or_ = value;

        // Clear port B interrupt flags
        state_.ifr.bits.cb1 = 0;
        if ((state_.pcr.bits.cb2_mode & 5) != 1) {
            state_.ifr.bits.cb2 = 0;
        }

        // Write handshaking
        switch (state_.pcr.bits.cb2_mode) {
        case static_cast<uint8_t>(ViaCx2Control::Output_Handshake):
            state_.port_b.c2 = 0;
            break;
        case static_cast<uint8_t>(ViaCx2Control::Output_Pulse):
            state_.port_b.c2 = 0;
            state_.port_b.pulse = 2;
            break;
        }

        // Update peripheral with new output
        update_port_pins();
        break;

    case REG_ORA:  // ORA with handshake
        // Clear port A interrupt flags
        state_.ifr.bits.ca1 = 0;
        if ((state_.pcr.bits.ca2_mode & 5) != 1) {
            state_.ifr.bits.ca2 = 0;
        }

        // Write handshaking
        switch (state_.pcr.bits.ca2_mode) {
        case static_cast<uint8_t>(ViaCx2Control::Output_Handshake):
            state_.port_a.c2 = 0;
            break;
        case static_cast<uint8_t>(ViaCx2Control::Output_Pulse):
            state_.port_a.c2 = 0;
            state_.port_a.pulse = 2;
            break;
        }
        [[fallthrough]];

    case REG_ORA_NH:  // ORA without handshake
        state_.port_a.or_ = value;
        update_port_pins();
        break;

    case REG_DDRB:
        state_.port_b.ddr = value;
        update_port_pins();
        break;

    case REG_DDRA:
        state_.port_a.ddr = value;
        update_port_pins();
        break;

    case REG_T1CL:  // T1L-L (writing T1C-L writes to latch)
    case REG_T1LL:
        state_.t1ll = value;
        break;

    case REG_T1CH:  // T1C-H (starts timer)
        if (!state_.t1_timeout) {
            state_.ifr.bits.t1 = 0;
        }
        state_.t1lh = value;
        state_.t1_pending = true;
        state_.t1_reload = true;
        state_.t1_pb7 = 0;
        break;

    case REG_T1LH:  // T1L-H
        if (!state_.t1_timeout) {
            state_.ifr.bits.t1 = 0;
        }
        state_.t1lh = value;
        break;

    case REG_T2CL:  // T2L-L
        state_.t2ll = value;
        break;

    case REG_T2CH:  // T2C-H (starts timer)
        if (!state_.t2_timeout) {
            state_.ifr.bits.t2 = 0;
        }
        state_.t2lh = value;
        state_.t2_pending = true;
        state_.t2_reload = true;
        break;

    case REG_SR:  // Shift Register
        state_.sr = value;
        break;

    case REG_ACR:  // Auxiliary Control Register
        state_.acr.value = value;
        // If T1 just timed out and continuous mode is disabled, stop T1
        if (state_.t1_timeout && !state_.acr.bits.t1_continuous) {
            state_.t1_pending = false;
        }
        break;

    case REG_PCR:  // Peripheral Control Register
        state_.pcr.value = value;
        break;

    case REG_IFR:  // Interrupt Flag Register - writing 1s clears flags
        state_.ifr.value &= ~value;
        break;

    case REG_IER:  // Interrupt Enable Register
        if (value & 0x80) {
            // Bit 7 set: enable specified bits
            state_.ier.value |= value;
        } else {
            // Bit 7 clear: disable specified bits
            state_.ier.value &= ~value;
        }
        break;
    }
}

//////////////////////////////////////////////////////////////////////////////
// Two-phase clock updates
//////////////////////////////////////////////////////////////////////////////

uint8_t Via6522::update_phi2_leading_edge() {
    // Handle T1 timeout
    if (state_.t1_timeout) {
        state_.t1_pending = state_.acr.bits.t1_continuous;
        state_.ifr.bits.t1 = 1;
        state_.t1_pb7 ^= 0x80;  // Toggle PB7 output
    }

    // Handle T2 timeout
    if (state_.t2_timeout) {
        state_.t2_pending = false;
        state_.ifr.bits.t2 = 1;
    }

    // PB6 pulse counting mode for T2
    if (state_.acr.bits.t2_count_pb6) {
        // Count on falling edge of PB6
        state_.t2_count = ((state_.old_pb & 0x40) != 0) && ((state_.port_b.p & 0x40) == 0);
        state_.old_pb = state_.port_b.p;
    } else {
        state_.t2_count = true;
    }

    return state_.ier.value & state_.ifr.value & 0x7F;
}

uint8_t Via6522::update_phi2_trailing_edge() {
    // CA1/CA2 control line handling
    tick_control_phi2_trailing_edge(
        state_.port_a,
        state_.acr.bits.pa_latching,
        state_.pcr.value,
        static_cast<uint8_t>(ViaIrqMask::CA2));

    // CB1/CB2 control line handling
    tick_control_phi2_trailing_edge(
        state_.port_b,
        state_.acr.bits.pb_latching,
        state_.pcr.value >> 4,
        static_cast<uint8_t>(ViaIrqMask::CB2));

    // Timer 1
    state_.t1_timeout = false;

    if (state_.t1_reload) {
        state_.t1 = static_cast<uint16_t>(state_.t1ll) |
                    (static_cast<uint16_t>(state_.t1lh) << 8);
        state_.t1_reload = false;
    } else {
        --state_.t1;
        state_.t1_reload = (state_.t1 == 0xFFFF);
        state_.t1_timeout = state_.t1_pending && state_.t1_reload;
    }

    // Timer 2
    state_.t2_timeout = false;

    if (state_.t2_reload) {
        state_.t2 = static_cast<uint16_t>(state_.t2ll) |
                    (static_cast<uint16_t>(state_.t2lh) << 8);
        state_.t2_reload = false;
    } else {
        if (state_.t2_count) {
            --state_.t2;
            state_.t2_timeout = state_.t2_pending && (state_.t2 == 0xFFFF);
        }
    }

    return state_.ier.value & state_.ifr.value & 0x7F;
}

//////////////////////////////////////////////////////////////////////////////
// Control line handling
//////////////////////////////////////////////////////////////////////////////

void Via6522::tick_control_phi2_trailing_edge(ViaPort& port,
                                               uint8_t latching,
                                               uint8_t pcr_bits,
                                               uint8_t cx2_mask) {
    // Check for Cx1 edge
    {
        uint8_t old_c1 = port.old_c1;
        port.old_c1 = port.c1;

        // Build code: c1 | old_c1<<1 | pcr_bit0<<2
        uint8_t code = (port.c1 | (old_c1 << 1) | (pcr_bits << 2)) & 7;

        // Edge detection logic:
        // code 2: pcr_bit0=0, was high, now low (negative edge)
        // code 5: pcr_bit0=1, was low, now high (positive edge)
        if (code == 2 || code == 5) {
            // cx2_mask << 1 is the mask for Cx1
            state_.ifr.value |= cx2_mask << 1;

            if (latching) {
                port.p_latch = port.p;
            }
        }
    }

    // Handle Cx2
    {
        uint8_t old_c2 = port.old_c2;
        port.old_c2 = port.c2;

        auto cx2_mode = static_cast<ViaCx2Control>((pcr_bits >> 1) & 7);
        switch (cx2_mode) {
        case ViaCx2Control::Input_NegEdge:
        case ViaCx2Control::Input_IndIRQNegEdge:
            if (old_c2 && !port.c2) {
                state_.ifr.value |= cx2_mask;
            }
            port.c2 = 1;
            break;

        case ViaCx2Control::Input_PosEdge:
        case ViaCx2Control::Input_IndIRQPosEdge:
            if (!old_c2 && port.c2) {
                state_.ifr.value |= cx2_mask;
            }
            port.c2 = 1;
            break;

        case ViaCx2Control::Output_Pulse:
            if (port.pulse > 0) {
                --port.pulse;
                if (port.pulse == 0) {
                    port.c2 = 1;
                }
            }
            break;

        case ViaCx2Control::Output_High:
            port.c2 = 1;
            break;

        case ViaCx2Control::Output_Low:
            port.c2 = 0;
            break;

        case ViaCx2Control::Output_Handshake:
            if (port.c1 == 0) {
                // Data taken -> not data ready
                port.c2 = 1;
            }
            break;
        }
    }

    // Cx1 is always an input - reset to 1 for peripheral to drive
    port.c1 = 1;

    // Update port value: output bits from OR, input bits pulled high
    port.p = ~port.ddr | (port.or_ & port.ddr);
}

uint8_t Via6522::compute_port_value(const ViaPort& port) {
    return ~port.ddr | (port.or_ & port.ddr);
}

void Via6522::update_port_pins() {
    if (peripheral_) {
        // Get output values from OR (only for output pins)
        uint8_t out_a = state_.port_a.or_;
        uint8_t out_b = state_.port_b.or_;

        // Call peripheral to get input pin states
        uint8_t in_a = peripheral_->update_port_a(out_a, state_.port_a.ddr);
        uint8_t in_b = peripheral_->update_port_b(out_b, state_.port_b.ddr);

        // Combine: output pins from OR, input pins from peripheral
        state_.port_a.p = (out_a & state_.port_a.ddr) | (in_a & ~state_.port_a.ddr);
        state_.port_b.p = (out_b & state_.port_b.ddr) | (in_b & ~state_.port_b.ddr);

        // Update control lines
        peripheral_->update_control_lines(
            state_.port_a.c1, state_.port_a.c2,
            state_.port_b.c1, state_.port_b.c2);
    } else {
        // No peripheral: inputs pulled high
        state_.port_a.p = state_.port_a.or_ | ~state_.port_a.ddr;
        state_.port_b.p = state_.port_b.or_ | ~state_.port_b.ddr;
    }
}

} // namespace beebium
