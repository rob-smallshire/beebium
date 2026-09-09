# Clock Architecture and Pacing

This document describes Beebium's clock system, which handles both the internal timing of emulated hardware and the real-time pacing of emulation.

## Overview

The clock system operates at two distinct levels:

1. **Internal Clocking**: Cycle-accurate timing of emulated hardware (CPU, VIAs, CRTC, etc.)
2. **Real-time Pacing**: Synchronizing emulation speed to wall-clock time

These concerns are deliberately separated to keep the emulator core deterministic and testable, while allowing flexible real-time playback.

## BBC Micro Clock Architecture

The BBC Micro uses a 16 MHz master crystal divided down for various subsystems:

| Clock        | Frequency | Purpose                          |
|--------------|-----------|----------------------------------|
| Master       | 16 MHz    | Master crystal oscillator        |
| RAM          | 4 MHz     | RAM access timing                |
| CPU (PHI2)   | 2 MHz     | 6502 CPU clock                   |
| Peripheral   | 1 MHz     | VIAs, CRTC, other peripherals    |

These constants are defined in `ClockTypes.hpp`:

```cpp
namespace timing {
    constexpr uint64_t MASTER_HZ     = 16'000'000;  // 16MHz master crystal
    constexpr uint64_t RAM_HZ        = 4'000'000;   // 4MHz RAM access
    constexpr uint64_t CPU_HZ        = 2'000'000;   // 2MHz CPU clock
    constexpr uint64_t PERIPHERAL_HZ = 1'000'000;   // 1MHz peripherals
}
```

## Internal Clock Distribution

### Clock Edges

The 6502 CPU clock (PHI2) has two phases. Devices can subscribe to either or both edges:

```cpp
enum class ClockEdge : uint8_t {
    None    = 0x00,
    Rising  = 0x01,   // PHI2 rising (odd cycles)
    Falling = 0x02,   // PHI2 falling (even cycles)
    Both    = 0x03,
};
```

### Clock Rates

Devices operate at either 1 MHz or 2 MHz:

```cpp
enum class ClockRate : uint8_t {
    Rate_1MHz = 1,   // VIAs, CRTC in Mode 7
    Rate_2MHz = 2,   // CPU, CRTC in graphics modes
};
```

### Device Clocking

Each device declares its clock requirements via static members:

```cpp
class Via6522 {
public:
    // Both edges of the 2MHz clock (leading and trailing of 1MHz PHI2)
    static constexpr ClockEdge clock_edges = ClockEdge::Both;
    static constexpr ClockRate clock_rate = ClockRate::Rate_2MHz;

    void tick_rising();   // PHI2 leading edge (update_phi2_leading_edge)
    void tick_falling();  // PHI2 trailing edge (update_phi2_trailing_edge)
};
```

The CRTC has a **dynamic** clock rate: 1 MHz in Mode 7 (teletext) and 2 MHz in bitmap modes 0-6. The Video ULA control register determines the rate. `Machine::step()` ticks the video binding on both rising and falling edges when the character clock is 2 MHz, and on falling edges only when it is 1 MHz.

`Machine::step()` dispatches clock ticks to devices based on these declarations.

## 1MHz Bus Stretching

The BBC Micro's 2MHz CPU can access 1MHz peripherals, but these accesses require synchronization. The bus controller inserts wait cycles ("bus stretching") to align the CPU with the slower peripheral clock.

### Affected Address Ranges

| Address Range | Device | 1MHz? |
|---------------|--------|-------|
| $FC00-$FCFF | FRED (external I/O) | Yes |
| $FD00-$FDFF | JIM (external I/O) | Yes |
| $FE00-$FE1F | CRTC, ACIA, Serial ULA | Yes |
| $FE20-$FE3F | Video ULA | No (clocked by video) |
| $FE40-$FE5F | System VIA | Yes |
| $FE60-$FE7F | User VIA | Yes |
| $FE80-$FE9F | Disc controller (WD1770) | No |
| $FEA0-$FEBF | Econet | No |
| $FEC0-$FEDF | A/D converter | Yes |
| $FEE0-$FEFF | Tube | No |

### Timing Model

When the CPU accesses a 1MHz peripheral:

1. **Instruction executes normally** until the memory access cycle
2. **The memory access cycle** is part of the 1MHz access (not separate)
3. **Extra cycles** are added to complete the 1MHz timing:
   - 1 cycle if already aligned with the 1MHz clock (even 2MHz cycle)
   - 2 cycles if misaligned (odd 2MHz cycle)

```
CPU cycle N: Instruction reaches memory access phase
             ├─ If N is even (aligned): add 1 extra cycle
             └─ If N is odd (misaligned): add 2 extra cycles

During extra cycles:
  - CPU is halted (no new cycles execute)
  - Both VIAs continue to tick (except the VIA being accessed, which was pre-ticked)
  - Video continues to tick (at 1MHz or 2MHz depending on the current display mode)
```

### Implementation

Bus stretching is implemented in `CpuBinding`:

1. After the CPU executes a cycle, the accessed address is checked
2. If it's a 1MHz VIA address, that specific VIA is pre-ticked for the stretch amount (including the current cycle)
3. The memory access then completes (seeing the VIA state at the 1MHz clock edge)
4. Machine accounts for the stretch cycles (halting CPU, ticking video and non-pre-ticked VIAs)

This "Option B" approach (pre-tick before access) ensures the CPU reads see the correct peripheral state after synchronization, matching real hardware behavior.

```cpp
// BusStretching.hpp
constexpr uint8_t stretch_cycles(uint64_t cycle_count) {
    return 1 + static_cast<uint8_t>(cycle_count & 1);
}
```

### Validation

This implementation passes hardware-validated timing tests from jsbeeb (VIA.AC1, VIA.T12) which were measured against real BBC Master hardware by @scarybeasts.

## Real-time Pacing

### Design Goals

1. **Separation of concerns**: Emulation core has no knowledge of wall-clock time
2. **Determinism**: Same inputs produce same outputs regardless of pacing
3. **Flexibility**: Support variable speed, pause, and unlimited modes
4. **Low latency**: Responsive to user input and audio requirements

### Architecture

```
┌─────────────────────┐     signals      ┌─────────────────────┐
│   PacingClock       │ ──────────────── │   Emulation Thread  │
│   (Timer Thread)    │                  │                     │
│                     │                  │   machine.run(N)    │
│   wait_for_tick()◄──┼──────────────────┤   wait_for_tick()   │
└─────────────────────┘                  └─────────────────────┘
```

### PacingConfig

Configuration for the pacing system:

```cpp
struct PacingConfig {
    uint64_t base_clock_hz;    // Machine clock (2 MHz for BBC)
    uint32_t pacing_hz;        // Sync rate (50-500 Hz typical)
    double speed_multiplier;   // 1.0 = real-time, 0.0 = unlimited
};
```

**Key calculations:**

- `cycles_per_tick()` = `base_clock_hz * speed_multiplier / pacing_hz`
- `tick_interval()` = `1 second / pacing_hz`

**Example configurations:**

| Use Case           | pacing_hz | speed_multiplier | cycles_per_tick |
|--------------------|-----------|------------------|-----------------|
| Normal playback    | 200       | 1.0              | 10,000          |
| Frame-locked       | 50        | 1.0              | 40,000          |
| Slow-motion debug  | 200       | 0.5              | 5,000           |
| Fast-forward       | 200       | 2.0              | 20,000          |
| Unlimited (tests)  | 200       | 0.0              | 10,000          |

### PacingClock

The `PacingClock` class manages real-time synchronization:

```cpp
PacingClock clock(config);
clock.start();

while (running) {
    machine.run(clock.cycles_per_tick());
    clock.wait_for_tick();
}

clock.stop();
```

**Features:**

1. **Dedicated timer thread**: Monitors wall-clock time independently
2. **Adaptive sleep**: Learns typical sleep overshoot and adjusts margins
3. **Graceful degradation**: Skips ticks rather than trying to catch up
4. **Pause support**: For debugger integration

### Adaptive Timing

The pacing clock optimizes CPU usage by:

1. Sleeping until close to the target time (with safety margin)
2. Spin-waiting for precise final timing
3. Dynamically adjusting the safety margin based on observed sleep behavior

```cpp
// Exponential moving average of sleep overshoot
avg_overshoot_ns_ = avg_overshoot_ns_ * 0.9 + overshoot_ns * 0.1;

// New margin includes buffer for variance
safety_margin_ = avg_overshoot_ns_ + max_recent_overshoot_ns_ * 0.5;
```

This minimizes CPU-burning spin-wait time while maintaining accurate timing.

### Falling Behind

If the emulator falls behind (e.g., during heavy load), the pacing clock:

1. Detects when more than one interval has elapsed
2. Skips ahead rather than trying to catch up
3. Tracks skipped ticks for monitoring

This prevents "death spirals" where the emulator tries to run faster and faster.

## Integration with Server

The server main loop integrates pacing:

```cpp
// Machine-specific configuration
PacingClock pacing_clock(Memory::default_pacing_config());
pacing_clock.start();

while (running) {
    machine.wait_if_paused();          // park while a debugger/quiescer holds the machine
    pacing_clock.wait_for_tick();      // sleep OUTSIDE the busy scope below

    // All work that touches state the emulation thread owns runs under the
    // busy scope, so a quiescing caller (a service or extension mutating that
    // state) waits for it. If a pause landed since wait_if_paused returned, the
    // scope is inactive and the loop re-parks instead of racing.
    MachineType::EmulationBusyScope busy(machine);
    if (!busy.active()) continue;
    machine.run(pacing_clock.cycles_per_tick());
}

pacing_clock.stop();
```

The emulation thread owns all guest state; any other thread that mutates it
must halt the emulation thread across the mutation via
`Machine::with_emulation_paused`. See the pause/quiesce primitive in
`docs/tube-coprocessor-contract.md` ("Debugger entry mutation").

### Environment Variable Override

For debugging, pacing can be disabled:

```bash
BEEBIUM_NO_PACING=1 ./beebium-model-b
```

This runs the emulator as fast as possible, useful for:
- Automated testing
- Profiling emulator performance
- Debugging timing issues

## Timing Statistics

The pacing clock provides monitoring statistics:

```cpp
struct TimingStats {
    double avg_overshoot_us;       // Average sleep overshoot
    double max_recent_overshoot_us; // Recent maximum
    double safety_margin_us;       // Current safety margin
    uint64_t ticks_executed;       // Total ticks
    uint64_t ticks_skipped;        // Ticks skipped (fell behind)
};
```

## Files

| File | Purpose |
|------|---------|
| `ClockTypes.hpp` | Clock edge/rate enums, timing constants |
| `BusStretching.hpp` | 1MHz bus stretch address detection and cycle calculation |
| `PacingConfig.hpp` | Pacing configuration struct |
| `PacingClock.hpp` | Real-time pacing clock implementation |
| `ModelBHardware.hpp` | Machine-specific default pacing config |

## See Also

- [Video Subsystem](video-subsystem.md) - VSYNC timing and frame rendering
- [gRPC Server](grpc-server.md) - Server integration with pacing
