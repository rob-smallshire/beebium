# Program Counter Histogram

The program counter histogram tracks instruction execution frequency across the address space. It answers the question: "How many times was code at address X executed?"

This is distinct from `MemoryHistogram`, which tracks memory reads/writes.

## Design

The PC histogram is integrated directly into `Machine::step()` with minimal overhead:

```cpp
void step() {
    // Minimal overhead: single pointer check + IsAboutToExecute when enabled
    if (pc_histogram_ && M6502_IsAboutToExecute(&state_.cpu)) {
        pc_histogram_->record(state_.cpu.pc.w);
    }
    // ... rest of cycle execution ...
}
```

**Overhead characteristics:**
- When disabled: single null pointer check per cycle (essentially free)
- When enabled: null check + `IsAboutToExecute()` check per cycle, plus array increment at instruction boundaries

**Memory footprint:** ~512KB (65536 addresses × 8-byte counters)

## Usage

### Basic Profiling

```cpp
#include <beebium/ProgramCounterHistogram.hpp>
#include <beebium/Machines.hpp>

ModelB machine;
// ... set up machine ...

ProgramCounterHistogram pc_histogram;
pc_histogram.attach(machine);

// Run emulation
for (int i = 0; i < 1000000; ++i) {
    machine.step_instruction();
}

pc_histogram.detach(machine);

// Analyze results
auto top = pc_histogram.top_addresses(10);
for (const auto& [addr, count] : top) {
    printf("$%04X: %llu visits\n", addr, count);
}
```

### Loop Detection

```cpp
ProgramCounterHistogram pc_histogram;
pc_histogram.attach(machine);

uint64_t loop_threshold = 50000;

while (running) {
    uint16_t pc = machine.cpu().pc.w;

    if (pc_histogram.exceeds_threshold(pc, loop_threshold)) {
        printf("Loop detected at $%04X after %llu visits\n",
               pc, pc_histogram.visits(pc));
        break;
    }

    machine.step_instruction();
}
```

### Hot Spot Analysis

```cpp
// Find the single hottest address
auto [addr, count] = pc_histogram.max_visits();

// Get top N addresses
auto hotspots = pc_histogram.top_addresses(20);

// Count unique addresses executed
size_t coverage = pc_histogram.unique_addresses();
printf("Code coverage: %zu unique addresses\n", coverage);
```

## API Reference

### Attachment

| Method | Description |
|--------|-------------|
| `attach(machine)` | Start recording PC visits |
| `detach(machine)` | Stop recording |
| `record(pc)` | Manually record a visit (called by Machine) |

### Queries

| Method | Description |
|--------|-------------|
| `visits(addr)` | Visit count for specific address |
| `total_visits()` | Sum of all visit counts |
| `unique_addresses()` | Count of addresses with any visits |

### Analysis

| Method | Returns |
|--------|---------|
| `max_visits()` | `{address, count}` for most visited |
| `top_addresses(n)` | Vector of top N `{address, count}` pairs |
| `exceeds_threshold(addr, threshold)` | True if address exceeds threshold |
| `find_exceeding_threshold(threshold)` | First address exceeding threshold |

### Management

| Method | Description |
|--------|-------------|
| `clear()` | Reset all counters to zero |
| `visit_counts()` | Direct access to underlying array |

## Use Cases

### Boot Sequence Analysis

Track which MOS routines are executed most during boot:

```cpp
ProgramCounterHistogram pc_histogram;
pc_histogram.attach(machine);

// Run boot sequence
while (!booted) {
    machine.step_instruction();
}

pc_histogram.detach(machine);

// Show MOS hot spots (MOS is at $C000-$FFFF)
auto all = pc_histogram.top_addresses(100);
for (const auto& [addr, count] : all) {
    if (addr >= 0xC000) {
        printf("MOS $%04X: %llu\n", addr, count);
    }
}
```

### Infinite Loop Detection

Detect when the emulator is stuck in an unproductive loop:

```cpp
uint64_t threshold = 100000;  // Adjust based on expected behavior

while (instruction_count < max_instructions) {
    auto [stuck_pc, visits] = pc_histogram.find_exceeding_threshold(threshold);
    if (visits > 0) {
        printf("Stuck at $%04X (%llu visits)\n", stuck_pc, visits);
        break;
    }
    machine.step_instruction();
    instruction_count++;
}
```

### Code Coverage

Measure what percentage of ROM was executed:

```cpp
pc_histogram.attach(machine);
run_test_suite(machine);
pc_histogram.detach(machine);

size_t rom_coverage = 0;
for (uint32_t addr = 0xC000; addr <= 0xFFFF; ++addr) {
    if (pc_histogram.visits(addr) > 0) {
        rom_coverage++;
    }
}

printf("MOS coverage: %zu/%d bytes (%.1f%%)\n",
       rom_coverage, 16384, 100.0 * rom_coverage / 16384);
```

## Comparison with MemoryHistogram

| Feature | ProgramCounterHistogram | MemoryHistogram |
|---------|------------------------|-----------------|
| Tracks | Instruction execution | Memory reads/writes |
| Hook point | `M6502_IsAboutToExecute()` | Watchpoints |
| Question answered | "How often was code at X run?" | "How often was address X accessed?" |
| Typical use | Profiling, loop detection | I/O analysis, hot data |
