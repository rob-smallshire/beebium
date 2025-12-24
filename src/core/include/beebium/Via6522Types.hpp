#ifndef BEEBIUM_VIA6522_TYPES_HPP
#define BEEBIUM_VIA6522_TYPES_HPP

#include <cstdint>

namespace beebium {

// IRQ mask bits for IFR/IER registers
enum class ViaIrqMask : uint8_t {
    CA2 = 0x01,
    CA1 = 0x02,
    SR  = 0x04,
    CB2 = 0x08,
    CB1 = 0x10,
    T2  = 0x20,
    T1  = 0x40,
    IRQ = 0x80   // Master IRQ flag (IFR bit 7)
};

// CA2/CB2 control modes (PCR bits 1-3 and 5-7)
enum class ViaCx2Control : uint8_t {
    Input_NegEdge        = 0,  // Input, interrupt on negative edge
    Input_IndIRQNegEdge  = 1,  // Independent interrupt, negative edge
    Input_PosEdge        = 2,  // Input, interrupt on positive edge
    Input_IndIRQPosEdge  = 3,  // Independent interrupt, positive edge
    Output_Handshake     = 4,  // Output, handshake mode
    Output_Pulse         = 5,  // Output, pulse mode
    Output_Low           = 6,  // Output, always low
    Output_High          = 7   // Output, always high
};

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

// Peripheral Control Register (PCR) bitfield
struct ViaPcrBits {
    uint8_t ca1_pos_irq : 1;   // CA1 interrupt edge: 0=neg, 1=pos
    uint8_t ca2_mode    : 3;   // CA2 control mode (ViaCx2Control)
    uint8_t cb1_pos_irq : 1;   // CB1 interrupt edge: 0=neg, 1=pos
    uint8_t cb2_mode    : 3;   // CB2 control mode (ViaCx2Control)
};

union ViaPcr {
    uint8_t value = 0;
    ViaPcrBits bits;
};

// Auxiliary Control Register (ACR) bitfield
struct ViaAcrBits {
    uint8_t pa_latching   : 1;  // Port A latching enabled on CA1 edge
    uint8_t pb_latching   : 1;  // Port B latching enabled on CB1 edge
    uint8_t sr            : 3;  // Shift register mode (0-7)
    uint8_t t2_count_pb6  : 1;  // T2 counts PB6 pulses (else counts phi2)
    uint8_t t1_continuous : 1;  // T1 free-running mode (else one-shot)
    uint8_t t1_output_pb7 : 1;  // T1 toggles PB7 output on timeout
};

union ViaAcr {
    uint8_t value = 0;
    ViaAcrBits bits;
};

// Interrupt Flag/Enable Register bitfield
struct ViaIrqBits {
    uint8_t ca2 : 1;  // Bit 0
    uint8_t ca1 : 1;  // Bit 1
    uint8_t sr  : 1;  // Bit 2
    uint8_t cb2 : 1;  // Bit 3
    uint8_t cb1 : 1;  // Bit 4
    uint8_t t2  : 1;  // Bit 5
    uint8_t t1  : 1;  // Bit 6
    uint8_t irq : 1;  // Bit 7 (master IRQ flag in IFR)
};

union ViaIrq {
    uint8_t value = 0;
    ViaIrqBits bits;
};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

// Port state (PA or PB)
struct ViaPort {
    uint8_t or_     = 0x00;   // Output register (ORx)
    uint8_t ddr     = 0x00;   // Data direction register (DDRx)
    uint8_t p       = 0xFF;   // Port pins state (Px)
    uint8_t p_latch = 0xFF;   // Latched input value
    uint8_t c1      = 0;      // Control line 1 (Cx1)
    uint8_t old_c1  = 0;      // Previous C1 for edge detection
    uint8_t c2      = 0;      // Control line 2 (Cx2)
    uint8_t old_c2  = 0;      // Previous C2 for edge detection
    uint8_t pulse   = 0;      // Pulse countdown for output pulse mode
};

// Complete VIA state (serializable)
struct Via6522State {
    ViaPort port_a;
    ViaPort port_b;

    ViaIrq ifr{};        // Interrupt flag register
    ViaIrq ier{};        // Interrupt enable register
    ViaAcr acr{};        // Auxiliary control register
    ViaPcr pcr{};        // Peripheral control register

    uint8_t sr = 0;      // Shift register

    // Timer 1
    // Default latch values: 51962 cycles = ~52ms at 1MHz (~19.2Hz)
    // These are arbitrary power-on defaults - the MOS will immediately
    // reprogram Timer 1 during initialization. The values are chosen
    // to be non-zero to avoid immediate timeout before MOS sets them.
    uint8_t t1ll = 250;       // T1 latch low  (0xFA)
    uint8_t t1lh = 202;       // T1 latch high (0xCA) -> 0xCAFA = 51962
    uint16_t t1  = 0;         // T1 counter
    bool t1_reload  = false;  // T1 needs reload
    bool t1_pending = false;  // T1 is active and can generate IRQ
    bool t1_timeout = false;  // T1 timed out this cycle
    uint8_t t1_pb7  = 0;      // PB7 output state from T1

    // Timer 2
    uint8_t t2ll = 0;         // T2 latch low
    uint8_t t2lh = 0;         // T2 latch high
    uint16_t t2  = 0;         // T2 counter
    bool t2_reload  = false;  // T2 needs reload
    bool t2_pending = false;  // T2 is active and can generate IRQ
    bool t2_timeout = false;  // T2 timed out this cycle
    bool t2_count   = true;   // T2 counting enabled

    uint8_t old_pb = 0;       // Previous PB for PB6 counting
};

// Static assertions for union sizes
static_assert(sizeof(ViaPcr) == 1, "ViaPcr must be 1 byte");
static_assert(sizeof(ViaAcr) == 1, "ViaAcr must be 1 byte");
static_assert(sizeof(ViaIrq) == 1, "ViaIrq must be 1 byte");

} // namespace beebium

#endif // BEEBIUM_VIA6522_TYPES_HPP
