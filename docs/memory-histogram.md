# Memory Histogram Debugging Tool

The memory histogram is an optional diagnostic facility for profiling memory access patterns during emulation. It tracks read and write counts for each address in the 16-bit address space, helping identify hot spots, detect unexpected accesses, and understand program behavior.

## Design

The histogram integrates with Beebium's existing watchpoint infrastructure rather than adding overhead to the core memory path:

```
┌─────────────────────────────────────────────────────────┐
│                      Machine::step()                     │
│                            │                             │
│                     CPU memory access                    │
│                            │                             │
│                    ┌───────▼───────┐                     │
│                    │  Watchpoints  │──────► Histogram    │
│                    └───────────────┘        (if attached)│
└─────────────────────────────────────────────────────────┘
```

Key design decisions:

- **Runtime allocation**: The histogram is only allocated when needed (~1MB for 65536 × 2 × 8-byte counters)
- **Zero overhead when disabled**: No histogram code runs unless explicitly attached
- **Reuses watchpoint system**: No changes to the hot memory access path
- **Observes CPU accesses only**: Direct programmatic reads/writes via `machine.read()`/`machine.write()` are not tracked (these are for setup, not emulation)

## Usage

### Basic Profiling

```cpp
#include <beebium/MemoryHistogram.hpp>
#include <beebium/Machines.hpp>

ModelB machine;
// ... load ROMs, set up machine ...

// Allocate and attach histogram
auto histogram = std::make_unique<MemoryHistogram>();
histogram->attach(machine);

// Run emulation
for (int i = 0; i < 1000000; ++i) {
    machine.step();
}

// Analyze results
printf("Total reads:  %llu\n", histogram->total_reads());
printf("Total writes: %llu\n", histogram->total_writes());
printf("Active addresses: %zu\n", histogram->active_addresses());

// Find hottest addresses
auto [read_addr, read_count] = histogram->max_reads();
auto [write_addr, write_count] = histogram->max_writes();
printf("Most read:    $%04X (%llu times)\n", read_addr, read_count);
printf("Most written: $%04X (%llu times)\n", write_addr, write_count);
```

### Detaching

To stop recording, clear the watchpoint:

```cpp
machine.clear_watchpoints();
```

Note: This clears all watchpoints, not just the histogram. If you have other watchpoints, you'll need to re-add them.

### Querying Specific Addresses

```cpp
// Check activity at specific locations
uint64_t zp_reads = histogram->reads(0x00);      // Zero page location 0
uint64_t screen_writes = histogram->writes(0x7C00);  // Mode 7 screen start

// Check a range (manual iteration)
uint64_t sheila_total = 0;
for (uint16_t addr = 0xFE00; addr <= 0xFEFF; ++addr) {
    sheila_total += histogram->total(addr);
}
```

### Direct Array Access

For bulk analysis, access the underlying arrays directly:

```cpp
const auto& reads = histogram->read_counts();
const auto& writes = histogram->write_counts();

// Example: dump all non-zero entries
for (size_t addr = 0; addr < 65536; ++addr) {
    if (reads[addr] > 0 || writes[addr] > 0) {
        printf("$%04zX: R=%llu W=%llu\n", addr, reads[addr], writes[addr]);
    }
}
```

## API Reference

### Construction and Attachment

| Method | Description |
|--------|-------------|
| `MemoryHistogram()` | Default constructor, all counters zero |
| `attach(machine)` | Register watchpoint to record all CPU memory accesses |
| `clear()` | Reset all counters to zero |

### Per-Address Queries

| Method | Description |
|--------|-------------|
| `reads(addr)` | Read count for address |
| `writes(addr)` | Write count for address |
| `total(addr)` | Sum of reads and writes for address |

### Aggregate Queries

| Method | Description |
|--------|-------------|
| `total_reads()` | Sum of all read counts |
| `total_writes()` | Sum of all write counts |
| `active_addresses()` | Count of addresses with any access |

### Analysis Helpers

| Method | Returns |
|--------|---------|
| `max_reads()` | `{address, count}` pair for most-read address |
| `max_writes()` | `{address, count}` pair for most-written address |
| `max_total()` | `{address, count}` pair for most-accessed address |

### Direct Access

| Method | Description |
|--------|-------------|
| `read_counts()` | Reference to underlying `std::array<uint64_t, 65536>` |
| `write_counts()` | Reference to underlying `std::array<uint64_t, 65536>` |

## Example Use Cases

### Finding Zero Page Hot Spots

Zero page is heavily used on 6502 for fast access. Profile which locations are most active:

```cpp
printf("Zero page usage:\n");
for (uint16_t addr = 0; addr < 256; ++addr) {
    uint64_t t = histogram->total(addr);
    if (t > 1000) {  // Threshold for "hot"
        printf("  $%02X: %llu accesses\n", addr, t);
    }
}
```

### Detecting Unexpected ROM Writes

Writes to ROM addresses indicate bugs (either in emulated code or the emulator):

```cpp
for (uint16_t addr = 0x8000; addr <= 0xFFFF; ++addr) {
    if (histogram->writes(addr) > 0) {
        printf("WARNING: Write to ROM at $%04X (%llu times)\n",
               addr, histogram->writes(addr));
    }
}
```

### Profiling I/O Activity

See which hardware registers are accessed most:

```cpp
printf("SHEILA I/O activity:\n");
for (uint16_t addr = 0xFE00; addr <= 0xFEFF; ++addr) {
    uint64_t r = histogram->reads(addr);
    uint64_t w = histogram->writes(addr);
    if (r > 0 || w > 0) {
        printf("  $%04X: R=%llu W=%llu\n", addr, r, w);
    }
}
```

### Comparing Execution Phases

Clear and re-record to compare different phases:

```cpp
// Phase 1: Boot
histogram->attach(machine);
boot_machine(machine);
uint64_t boot_accesses = histogram->total_reads() + histogram->total_writes();

// Phase 2: Running
histogram->clear();
run_program(machine);
uint64_t run_accesses = histogram->total_reads() + histogram->total_writes();

printf("Boot: %llu accesses, Running: %llu accesses\n",
       boot_accesses, run_accesses);
```

## Performance Considerations

- **Memory**: ~1MB allocation (65536 addresses × 2 counters × 8 bytes)
- **CPU overhead**: One watchpoint check per memory access, plus counter increment
- **Recommended**: Enable only during debugging/profiling sessions, not production emulation

The histogram uses the same watchpoint mechanism as breakpoints, so the overhead is similar to having a single address-range watchpoint active.
