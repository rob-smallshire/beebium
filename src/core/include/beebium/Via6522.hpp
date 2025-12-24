#ifndef BEEBIUM_VIA6522_HPP
#define BEEBIUM_VIA6522_HPP

#include "Via6522Types.hpp"
#include "ClockTypes.hpp"
#include <cstdint>

namespace beebium {

// Abstract interface for peripherals connected to VIA ports.
// Implement this to connect hardware (keyboard, printer, etc.) to the VIA.
class ViaPeripheral {
public:
    virtual ~ViaPeripheral() = default;

    // Called when port output changes. Return the state of input pins.
    // output: current output register value
    // ddr: data direction register (1 = output, 0 = input)
    virtual uint8_t update_port_a(uint8_t output, uint8_t ddr) {
        (void)output; (void)ddr;
        return 0xFF;  // Default: all inputs pulled high
    }

    virtual uint8_t update_port_b(uint8_t output, uint8_t ddr) {
        (void)output; (void)ddr;
        return 0xFF;
    }

    // Called to update control line states.
    // Peripheral can modify ca1/ca2/cb1/cb2 to signal events.
    virtual void update_control_lines(uint8_t& ca1, uint8_t& ca2,
                                      uint8_t& cb1, uint8_t& cb2) {
        (void)ca1; (void)ca2; (void)cb1; (void)cb2;
    }
};

// MOS 6522 Versatile Interface Adapter emulation.
//
// The 6522 VIA provides:
// - Two 8-bit bidirectional I/O ports (PA, PB)
// - Two 16-bit interval timers (T1, T2)
// - Shift register for serial I/O
// - Interrupt logic with IFR/IER registers
// - Handshaking control lines (CA1, CA2, CB1, CB2)
//
// Timing model follows B2's two-phase 1MHz clock approach:
// - update_phi2_trailing_edge(): Timer updates, reloads, SR shift
// - update_phi2_leading_edge(): IRQ output, PB7 toggle
//
class Via6522 {
public:
    // Clock subscription: 2MHz, both edges (for timers on trailing, IRQ on leading)
    // Note: VIA internal timing is 1MHz but we tick on every 2MHz edge
    static constexpr ClockEdge clock_edges = ClockEdge::Both;
    static constexpr ClockRate clock_rate = ClockRate::Rate_2MHz;

    // Default constructor (no peripheral attached)
    Via6522();

    // Constructor with peripheral injection
    explicit Via6522(ViaPeripheral& peripheral);
    explicit Via6522(ViaPeripheral* peripheral);

    void reset();

    // Register access (addresses 0x00-0x0F, higher bits ignored)
    // Satisfies MemoryMappedDevice concept
    // Note: read() has side effects (clears interrupt flags) so not const
    uint8_t read(uint16_t offset);
    void write(uint16_t offset, uint8_t value);

    // Two-phase 1MHz clock updates.
    // Call these on alternate 2MHz cycles:
    //   Even cycles: update_phi2_trailing_edge()
    //   Odd cycles: update_phi2_leading_edge()
    // Returns non-zero if IRQ is pending.
    uint8_t update_phi2_leading_edge();
    uint8_t update_phi2_trailing_edge();

    // Clock subscriber interface
    void tick_rising() { update_phi2_leading_edge(); }
    void tick_falling() { update_phi2_trailing_edge(); }

    // State access for serialization
    const Via6522State& state() const { return state_; }
    Via6522State& state() { return state_; }

    // Peripheral connection (for runtime reconfiguration)
    void set_peripheral(ViaPeripheral* peripheral) { peripheral_ = peripheral; }
    ViaPeripheral* peripheral() const { return peripheral_; }

    // Direct port access for peripheral interaction
    ViaPort& port_a() { return state_.port_a; }
    ViaPort& port_b() { return state_.port_b; }
    const ViaPort& port_a() const { return state_.port_a; }
    const ViaPort& port_b() const { return state_.port_b; }

    // IRQ state query
    bool irq_pending() const {
        return (state_.ifr.value & state_.ier.value & 0x7F) != 0;
    }

    // Register addresses
    static constexpr uint8_t REG_ORB    = 0x00;  // Output Register B (+ input)
    static constexpr uint8_t REG_ORA    = 0x01;  // Output Register A (+ input, handshake)
    static constexpr uint8_t REG_DDRB   = 0x02;  // Data Direction Register B
    static constexpr uint8_t REG_DDRA   = 0x03;  // Data Direction Register A
    static constexpr uint8_t REG_T1CL   = 0x04;  // Timer 1 Counter Low
    static constexpr uint8_t REG_T1CH   = 0x05;  // Timer 1 Counter High
    static constexpr uint8_t REG_T1LL   = 0x06;  // Timer 1 Latch Low
    static constexpr uint8_t REG_T1LH   = 0x07;  // Timer 1 Latch High
    static constexpr uint8_t REG_T2CL   = 0x08;  // Timer 2 Counter Low
    static constexpr uint8_t REG_T2CH   = 0x09;  // Timer 2 Counter High
    static constexpr uint8_t REG_SR     = 0x0A;  // Shift Register
    static constexpr uint8_t REG_ACR    = 0x0B;  // Auxiliary Control Register
    static constexpr uint8_t REG_PCR    = 0x0C;  // Peripheral Control Register
    static constexpr uint8_t REG_IFR    = 0x0D;  // Interrupt Flag Register
    static constexpr uint8_t REG_IER    = 0x0E;  // Interrupt Enable Register
    static constexpr uint8_t REG_ORA_NH = 0x0F;  // ORA without handshake

private:
    Via6522State state_;
    ViaPeripheral* peripheral_ = nullptr;

    // Control line edge detection and handshaking
    void tick_control_phi2_trailing_edge(ViaPort& port,
                                         uint8_t latching,
                                         uint8_t pcr_bits,
                                         uint8_t cx2_mask);

    // Helper to compute port value from OR/DDR
    static uint8_t compute_port_value(const ViaPort& port);

    // Update port pin values via peripheral
    void update_port_pins();
};

} // namespace beebium

#endif // BEEBIUM_VIA6522_HPP
