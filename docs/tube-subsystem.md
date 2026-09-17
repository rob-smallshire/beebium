# Tube Subsystem

This document describes the Tube subsystem as built. The normative
statement of the coprocessor contract, with the reasoning behind each
decision and the record of how it was introduced, is
`docs/tube-coprocessor-contract.md`; this document summarises it and
describes the surrounding code. The history of earlier architectures is in
`docs/tube-architecture-evolution.md`. To add a coprocessor, read
`docs/coprocessor-extension-guide.md`.

## Overview

The Tube is Acorn's coprocessor interface: a custom ULA providing
bidirectional FIFO communication between two independent processor systems.
The host (the BBC Micro) handles I/O; the coprocessor (Acorn's term is
"parasite") runs application code. The two have independent clocks and
communicate only through the ULA's registers.

Beebium models this as two clock domains meeting only at the Tube ULA,
executed by one thread. The host `Machine` owns the clock. A coprocessor is
supplied by a peripheral extension plugin and is driven in host time through
a small contract: the host tells it how far to run, it converts host cycles
to its own with an exact rational ratio, and it runs. The host runs it in
batches of up to eight host cycles, and exactly to the host's own cycle
before any host access to a Tube register, so every register access on both
sides happens in host-time order. Nothing about a coprocessor's CPU family is
known to the core, the server or the clients: the 6502 second processors are
plugins like any other, and their debugger is served from a description the
coprocessor supplies.

Two coprocessors ship: `tube-65c02`, the Acorn 6502 Second Processor (65C02
at 3 MHz, 64 KB, Tube client v1.10), and `tube-65c102`, the 65C102
Co-processor from the Master Turbo (65C02-family at 4 MHz, 64 KB, client
v1.20). They are one class constructed with different clock ratios and
firmware.

```
beebium-model-b start --tube-65c02  ...      # 6502 Second Processor, 3 MHz
beebium-model-b start --tube-65c102 ...      # 65C102 Co-processor, 4 MHz
beebium-model-b start --tube-65c02 rom=/path/to/client.rom   # alternative client ROM
```

The Tube socket accepts one coprocessor; giving both flags is refused.

A coprocessor is not special to `start`: it is an extension, and every
subcommand that runs the machine builds it through the one shared
`assemble_machine` and drives it through the one shared `step_emulation` (see
`docs/emulation-thread-ownership.md`). So `capture-screenshot` on a Tube preset
runs the coprocessor and captures its banner, exactly as `start` would boot it
-- there is no second, extension-less machine assembly for any subcommand to
diverge into.


## Hardware Reference

### Register map

The Tube ULA exposes 8 bytes of I/O space to each processor. On the host
side these appear at `&FEE0-&FEE7` (Sheila), mirrored across `&FEE0-&FEFF`
because only A0-A2 are decoded; on the coprocessor side at `&FEF8-&FEFF`.

| Offset | Host read          | Host write         | Coprocessor read     | Coprocessor write  |
|--------|--------------------|--------------------|----------------------|--------------------|
| 0      | R1STAT + flags     | Control flags (S)  | R1STAT + flags       | --                 |
| 1      | R1DATA (24B FIFO)  | R1DATA (1B latch)  | R1DATA (1B read)     | R1DATA (24B FIFO)  |
| 2      | R2STAT             | --                 | R2STAT               | --                 |
| 3      | R2DATA (1B read)   | R2DATA (1B write)  | R2DATA (1B read)     | R2DATA (1B write)  |
| 4      | R3STAT             | --                 | R3STAT               | --                 |
| 5      | R3DATA (2B FIFO)   | R3DATA (2B FIFO)   | R3DATA (2B FIFO)     | R3DATA (2B FIFO)   |
| 6      | R4STAT             | --                 | R4STAT               | --                 |
| 7      | R4DATA (1B read)   | R4DATA (1B write)  | R4DATA (1B read)     | R4DATA (1B write)  |

Register 1 is asymmetric: the coprocessor-to-host direction is a 24-byte
FIFO sized for the longest VDU command (OSWRCH), the host-to-coprocessor
direction a single-byte latch used for events and escape.

**Register 1** carries OSWRCH one way and events/escape the other.
**Register 2** carries the OS call protocol (OSRDCH, OSCLI, OSBYTE, OSWORD,
OSBPUT, OSBGET, OSFIND, OSARGS, OSFILE, OSGBPB): the coprocessor writes a
command byte, then the two sides exchange parameters. **Register 3** is the
bulk NMI-driven transfer channel, a 2-byte FIFO each way, in one- or
two-byte mode according to the V flag. **Register 4** is the transfer control
channel: the host writes a transfer type to interrupt the coprocessor, and
error strings pass through it.

### Status registers

Bit 7 of each status register is "data available" to the reader, bit 6 is
"not full" for the writer. Bits 0-5 of the R1 status register are the
control flags P V M J I Q; bits 0-5 of the R2, R3 and R4 status registers
read as 1 (Application Note 004, note 11). On the coprocessor side the R3
status register's bit 7 is N, "register 3 action required", the same
condition that raises PNMI, rather than plain FIFO occupancy.

In two-byte mode (V=1) the R3 flags are sticky: data available is asserted
only when two bytes are present and stays asserted until both are removed;
not full is asserted only when both are gone and stays asserted until both
are entered.

Reading an empty FIFO (R1 coprocessor-to-host, R3 either way) returns the
last byte the other side drove onto its data bus, whatever the address it
wrote to. R2 and R4 are latches and return their contents.

### Control flags

Written via host offset 0. Bit 7 (S) selects set or clear: with S=1 the
other bits are ORed into the control register, with S=0 they are cleared.

| Bit | Flag | Meaning                                        |
|-----|------|------------------------------------------------|
| 0   | Q    | Enable HIRQ from Register 4                    |
| 1   | I    | Enable PIRQ from Register 1                    |
| 2   | J    | Enable PIRQ from Register 4                    |
| 3   | M    | Enable PNMI from Register 3                    |
| 4   | V    | Two-byte operation of Register 3               |
| 5   | P    | Activate PRST (coprocessor reset)              |
| 6   | T    | Clear all Tube registers                       |
| 7   | S    | Set (1) or clear (0) the indicated flags       |

### Interrupts

Three outputs, active low on the chip and active high in the model:

- **HIRQ** to the host: Q=1 and R4 has coprocessor-to-host data.
- **PIRQ** to the coprocessor: (I=1 and R1 has host-to-coprocessor data)
  or (J=1 and R4 has host-to-coprocessor data).
- **PNMI** to the coprocessor: M=1 and the R3 condition, which in one-byte
  mode is "host-to-coprocessor R3 has 1+ bytes or coprocessor-to-host R3 is
  empty", in two-byte mode "has 2 bytes or is empty", with the sticky
  behaviour above. The ULA's DRQ pin is the same condition ungated by M;
  Beebium does not model DRQ or DACK.

Interrupts clear by removing the cause.

### Reset

HRST clears all control flags, purges every register, and asserts PRST.
Register 3 coprocessor-to-host is left holding one valid but insignificant
byte so that PNMI does not fire immediately. The T flag performs the same
purge but preserves P, V, M, J, I and Q. The host's reset line propagates
through the cable: `TubeSocket::reset()` resets the ULA and the installed
coprocessor, and the coprocessor discards its time base so that the next
`run_until` establishes a fresh origin (a hard host reset zeroes the host
cycle count).

### Writes to a full register

The Tube ULA has no way to stall the host: the host connector carries no
ready, wait or clock line from the ULA, and a cycle-exact re-implementation
(hoglet's ReTuLaReMake), a real Ferranti part, and b2/B-Em/MAME all complete
every write in its own cycle. A write to a register that is already full is
either ignored or overwrites, per hoglet's measurements
(see `docs/discussion/tube-ula-full-register-writes.md`):

| Register | Depth | Write when full |
|----------|-------|-----------------|
| HP1 (host->coprocessor, R1) | 1 | overwrites |
| PH1 (coprocessor->host, R1) | 24 | ignored |
| HP2 / PH2 | 1 / 1 | overwrites / overwrites |
| HP3 (R3) | 2 | ignored |
| PH3 (R3) | 2 | ignored |
| HP4 / PH4 | 1 / 1 | overwrites / overwrites |

R3's FIFO depth is always two; the V flag changes only the status flags, not
the depth. Reads never block either: a read of an empty register returns the
opposite side's data bus latch.

Two known simplifications and differences: the empty-read value is the bus
latch, where a real Ferranti part returns fixed-looking values (0xE4 from the
coprocessor side, 0x96/0x94 from the host side, and more complex cases in the
later measurements) -- a follow-up may model these. And Beebium models the
Ferranti ULA; an AMI part (Master Turbo) shows its own deviations (one PH3 line
in the R3 test, an unexpected HP1 value, PH1's hdav staying set after the last
byte is read), recorded here as differences, not selected by any switch.

### Software protocol and transfer types

OSWRCH goes through R1 and the other OS calls through R2, both polled.
Block transfers are set up through R4 and carried through R3:

| Type | Direction | Method   | Notes                                          |
|------|-----------|----------|------------------------------------------------|
| 0    | P to H    | NMI      | Single-byte, 24 us/byte                        |
| 1    | H to P    | NMI      | Single-byte, 24 us/byte                        |
| 2    | P to H    | NMI      | Double-byte, 26 us/pair                        |
| 3    | H to P    | NMI      | Double-byte, 26 us/pair                        |
| 4    | --        | Execute  | Start execution at given address               |
| 5    | --        | Release  | Filing system releases Tube                    |
| 6    | P to H    | Polling  | 256-byte block, 10 us/byte, no NMI             |
| 7    | H to P    | Polling  | 256-byte block, 10 us/byte, no NMI             |

Types 0 to 3 are open-loop: the host touches R3 at the stated rate and
expects the coprocessor's NMI handler to have run between touches. That is
the timing the skew bound below is sized against.

The host side of the protocol, the Tube host code, lives in the filing
system ROM (DFS, DNFS, ANFS): the host needs one of those to use a
coprocessor.

### The 6502 second processor's boot mode

The 6502 second processor carries a 4 KB EPROM (a 2732, IC3) mapped at
`&F000-&FFFF` during boot mode. A flip-flop set at reset enables the ROM for
reads while writes pass through to the underlying RAM, so the boot code copies
itself to RAM by reading each byte and writing it back. The first access to any
Tube register address (`&FEF8-&FEFF`) clears the flip-flop, unmapping the ROM
for good until the next reset. The Tube registers punch through the RAM in both
modes. `CoprocessorMemoryMap` models exactly this: 64 KB RAM, a 4 KB ROM overlay
read-only during boot mode, cleared by the first Tube-window access.

The whole 4 KB device is modelled and shipped, not just the code half. Acorn's
own client firmware occupies only the upper 2 KB (`&F800-&FFFF`) and leaves the
lower half unprogrammed (`&FF`), but the lower half is genuine ROM address
space -- John Kortink's ReCo6502 client, for one, executes from it -- so a client
ROM image is the full 4096-byte device contents. There is no half-size or
padded-dump acceptance: a 2 KB file (the upper-half-only image other emulators
ship) is a fragment and is rejected.

Boot-mode decode: the model answers ROM only at `&F000-&FFFF`, as MAME's
`tube_6502` does. The service manual describes the boot latch as disabling CAS
on every read while set, which would mirror the 2732 across the whole address
space in boot mode; whether the hardware decodes that broadly is unverified
pending the schematic's chip-select logic, so the model does not assume it.

### Pinout

The ULA's 40 pins, from the period test vectors replayed in
`tests/test_tube_ula_vectors.cpp`: GND1, VCC1-3, GND2; host side HD0-7,
HA0-2, HRW, HCS, HPHI, HRST; coprocessor side PD0-7, PA0-2, PCS, PNRD,
PNWD, DACK; outputs PIRQ, PRST, DRQ, HIRQ, PNMI.


## Architecture

### Components

```
Machine<Hardware>                          (host, owns the clock)
  |
  +-- TubeSocket                            (&FEE0-&FEFF, HIRQ into the IRQ aggregator)
        |-- TubeHostBackend*   ---------->  TubeUla   (owned by the extension)
        |-- Coprocessor*       ---------->  CoprocessorRunner
        |                                     |-- CoprocessorClock   (ratio, exact)
        |                                     |-- CoprocessorCpu     (65C02, M6502 library)
        |                                     |-- CoprocessorMemoryMap (64 KB + 4 KB boot ROM @ F000-FFFF)
        |                                     `-- TubeCoprocessorBackend& -> the same TubeUla
        `-- host_time, coprocessor_time, MAX_COPROCESSOR_SKEW

ServerMain
  |-- finds the one CoprocessorExtension among the loaded extensions
  |-- DebuggerControlServiceImpl(HostDebugTarget(machine))        -> DebuggerControl
  `-- DebuggerControlServiceImpl(*ext->debug_target())             -> CoprocessorDebuggerControl
```

| Component | Header | Role |
|---|---|---|
| `TubeSocket` | `beebium/tube/TubeSocket.hpp` | The connector on the host board. Memory-mapped device at `&FEE0`, IRQ source, holder of the installed backend and coprocessor, keeper of host time and the skew bound. |
| `TubeHostBackend` | `beebium/tube/TubeHostBackend.hpp` | The host-facing bridge interface: `host_read/peek/write`, `hirq`, `inspection`, `reset`. An extension installs its bridge as this. |
| `TubeCoprocessorBackend` | `beebium/tube/TubeCoprocessorBackend.hpp` | The coprocessor-facing bridge interface: `coprocessor_read/peek/write`, `pirq`, `pnmi_level`, `reset`. |
| `TubeInspection` | `beebium/tube/TubeInspection.hpp` | Read-only diagnostics a bridge may offer: control flags, both sides' peeks, interrupt lines, transfer counters, the protocol trace. `GetTubeState` reads it. Also holds the flag constants and the counter and trace types. |
| `TubeUla` | `beebium/tube/TubeUla.hpp` | The Ferranti ULA model: implements all three interfaces above. Verified against the period test vectors. |
| `Coprocessor` | `beebium/tube/Coprocessor.hpp` | The execution contract: `run_until(host_cycle)`, `pause`, `resume`, `is_paused`, `reset`, `clock_ratio`. |
| `CoprocessorClock` | `beebium/tube/CoprocessorClock.hpp` | Exact conversion of host cycles to coprocessor cycles for a `ClockRatio`, carrying the remainder, with no origin until the first call. |
| `CoprocessorRunner` | `beebium/tube/CoprocessorRunner.hpp` | The 6502-family coprocessor: implements `Coprocessor` and `CpuDebugTarget`; owns the CPU, memory map and clock; breakpoints and watchpoints. |
| `CoprocessorCpu`, `CoprocessorMemoryMap` | `beebium/tube/` | The 65C02 core wrapper (cycle-stepped `M6502`, with page-cross dummy reads routed through `peek` so they cannot consume Tube data) and the memory map. |
| `CoprocessorExtension` | `beebium/extension/CoprocessorExtension.hpp` | The extension contract: `coprocessor()`, `tube_backend()`, `debug_target()`. |
| `CpuDebugTarget`, `CpuDescriptor` | `beebium/extension/` | The family-agnostic debugger contract and the description of a CPU it serves. |
| `SecondProcessor65C02Extension` | `src/extensions/acorn-65c02-coprocessor/` | The shipped coprocessor; both plugins construct it. |

### Time

The unit of time in the contract is the host cycle, the host's cumulative
2 MHz count. A coprocessor owns its clock ratio, coprocessor cycles per host
cycle as an exact rational (3/2 for the 65C02, 2/1 for the 65C102), and
converts with `CoprocessorClock`: the cycles due at host time `t` since the
origin are exactly `floor((t - t0) * num / den)`, with the remainder carried
so nothing drifts. The origin is set by the first `run_until`, which
`TubeSocket::install_coprocessor` calls at once, so a coprocessor starts at
the host time it is installed and never runs a catch-up burst. `reset()`
discards the origin.

### The skew contract

Host time `H` is the current cycle; coprocessor time `C` is the host time
the coprocessor has been run to. `C <= H` always.

1. Immediately before any host read or write of a Tube register, `C == H`.
   The ULA then presents exactly the state of that bus cycle.
2. At every other host cycle, `H - C <= TubeSocket::MAX_COPROCESSOR_SKEW`,
   which is 8 host cycles, 4 us. The only observable consequence is
   interrupt latency of at most that, in either direction.
3. Reset and pause keep the bound: reset re-establishes the origin, and a
   paused coprocessor's time still advances (cycles in a paused interval are
   lost, not deferred, as a stopped processor's would be).

The value is sized against the type 0 to 3 transfers, which give the
coprocessor's NMI handler about 24 us between host touches of R3; 4 us of
latency leaves it well over half of that.

### Execution

`Machine::step()` begins every cycle, on every path, with
`tube_socket.host_cycle(cycle_count)`. That stores `H` and runs the
coprocessor to `H` only when it has fallen `MAX_COPROCESSOR_SKEW` behind, so
the coprocessor executes in batches. `TubeSocket::read()` and `write()` run
it to `H` before touching the ULA, which is what makes accesses exact.
The coprocessor's clock runs continuously whatever the host bus is doing:
during 1MHz bus stretches and on normal cycles alike. (The ULA never stalls
the host, so there is no Tube stretch path to keep in step.)

Whenever the host stops, at the end of a `run()` chunk, on a breakpoint or
watchpoint hit, or after a debugger single step (`CpuDebugTarget::finish_step`),
the coprocessor is run to `H`, so a stopped machine shows both processors
at the same time. `Machine::pause()` is called from an RPC thread, so it
syncs only when the emulation loop is not running; a running loop owns the
coprocessor and syncs on the exit that the pause causes.

Within `run_until` the 65C02 runner ticks its cycle-stepped core once per
due cycle. An instruction-level core that executes a whole batch per call
is designed but not built (`tube-coprocessor-contract.md`, Step 3b); it is
the basis for the heavier CPU families.

The host's IRQ aggregator polls the socket's `hirq()` every cycle and sees
ULA state as of `C`. The coprocessor samples PIRQ and PNMI as it runs. The
runner tracks NMI-handler entry and exit so that a second PNMI edge cannot
nest an NMI inside the handler.

### The debugger

One `DebuggerControlServiceImpl` serves any `CpuDebugTarget`. The host's
is `HostDebugTarget`, an adapter over `Machine`; the coprocessor's is the
runner itself. The server registers the first as `DebuggerControl` and the
second as `CoprocessorDebuggerControl`; the two gRPC services have identical
RPCs. A `CpuDescriptor` names the family, address width, endianness, the
registers with widths and roles (program counter, stack pointer, flags with
their bit names) and the interrupt signals; `GetCpuState` and `SetCpuState`
carry registers by name. All addresses in the debug path are 32-bit and are
bounded by the descriptor's `address_bits`. Cross-processor stops are wired
server-side: a host breakpoint with `stop_counterpart` pauses the
coprocessor through `Coprocessor::pause()`, a coprocessor breakpoint pauses
the host.

Clients render from the descriptor. In Python, `bbc.connect_coprocessor()`
returns a client whose debugger calls go to the coprocessor;
`cpu.registers` is an ordered mapping with attribute access, `cpu.signals`
the interrupt lines, `cpu.descriptor` the description. TypeScript mirrors
this. The macOS app uses the host debugger only.

### Plugins and firmware

Each coprocessor is a plugin in `<exe-dir>/extensions/<name>/`, holding the
shared library, `manifest.json` and a `roms/` directory. The manifest's
`roms` array declares each firmware image with a key, filename, size and
description; the loader checks every declared image is present and valid
before loading the library, and `describe-extension` lists them. The
extension loads its firmware with `Extension::load_rom(key, span)`, which
resolves beside the manifest; the `rom` parameter overrides it with a
user-supplied path. The shared `roms/` directory holds host ROMs only.

The plugin links `beebium_core` and the 6502 library into its shared object
with hidden visibility, so its private copies of the ULA and runner never
interpose on the server's own. The server reaches it only through the
exported interfaces; it has no compiled-in knowledge of any coprocessor.

### Fidelity notes

- Page-cross dummy reads on the 65C02 are routed through `peek`, so a
  fixup-cycle read landing on `&FEF8-&FEFF` cannot consume a byte. This
  was the Chuckie Egg 2023 hang; `tests/test_tube_ce2023_trace.cpp` guards
  it.
- The ULA's status semantics, bus latch behaviour, sticky R3 flags and
  reset dummy byte are pinned by the period test vectors.
- The coprocessor never freezes while the host is stretched by a 1MHz
  device; that was a defect of the earlier per-cycle model.


## Tests

| Test | What it proves |
|---|---|
| `test_tube_ula_vectors` | The ULA against the period pin-level vectors, 116 assertions. Fixed oracle; do not edit. |
| `test_tube_ula`, `test_tube_socket`, `test_tube_inspection` | ULA register semantics; socket dispatch; identical `GetTubeState` through an installed backend and the socket's own ULA. |
| `test_coprocessor_clock`, `test_coprocessor_runner`, `test_coprocessor_cpu`, `test_coprocessor_memory_map`, `test_coprocessor_extension`, `test_coprocessor_debug_target` | Exact ratio conversion; the runner's `run_until`, pause and reset semantics; the CPU wrapper and memory map; the extension install path and cross-processor stop; the debugger driven through the interface. |
| `test_coprocessor_skew` | The skew contract across a full boot and the CE2023 load; that batching happens; interrupt latency within the bound; pause and single-step sync; exact ratio from an arbitrary install time. |
| `test_boot_tube`, `test_coprocessor_boot` | Both coprocessors boot to their banners and the BASIC prompt; the 65C102 runs exactly twice the host's cycles. |
| `test_tube_ce2023_trace` | Chuckie Egg 2023 loads. |
| `test_stub_cpu_family` | A made-up CPU family, including a 24-bit one, is fully served by the debugger with no server code naming it. |
| `test_extension_rom`, `test_plugin_loader` | Manifest ROM declarations, resolution beside the manifest, exact-size (4 KB device) acceptance and wrong-size rejection, load-time presence/size checks. |
| `integration_tests/wfsinit`, `tube-save`, `l3fs` | Scenario suites through the Python client: ADFS and SCSI over the Tube on both coprocessors, DFS save without byte doubling, a Level 3 file server over Econet. Run in CI. |


## References

- `docs/tube-coprocessor-contract.md`: the contract, its rationale, and the
  step-by-step record of how it was built.
- `docs/coprocessor-extension-guide.md`: adding a coprocessor.
- `docs/tube-architecture-evolution.md`: the dual-process, multi-threaded
  and lockstep designs that preceded this one, and what was learned.
- `docs/discussion/cross-emulator-tube-analysis.md`: how PiTubeDirect,
  jsbeeb, B-Em and B2 do it.
- `docs/discussion/debugger-requirements.md`: debugger requirements for
  coupled host and coprocessor systems.
- Acorn Tube Application Note 004 (`docs/datasheets/Tube_Application_Note_004.pdf`).
- Acorn 6502 Tube Client v1.10 annotated disassembly:
  https://acornaeology.uk/acorn-6502-tube-client/1.10.html
