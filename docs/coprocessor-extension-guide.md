# Adding a Coprocessor Extension

This guide is for adding a new second processor to Beebium: a Z80, a 6809,
an NS32016, an 80186, or another 6502-family variant. It assumes the
general plugin mechanics in `docs/peripheral-extension-framework.md` and the
contract in `docs/tube-coprocessor-contract.md`; this document says what a
coprocessor in particular must provide, in the order you would build it.

A coprocessor is added by adding a directory under `src/extensions/`.
Nothing in the core, the server, the protos or the clients changes.

## What you are building

Everything on the far side of the Tube cable:

- a **bridge** presenting the host-facing `TubeHostBackend` and the
  coprocessor-facing `TubeCoprocessorBackend`. For an Acorn design that is
  `TubeUla`, which you reuse; a third-party design with different bridging
  hardware would implement the two interfaces itself;
- a **coprocessor**: a CPU, its memory map and boot firmware, implementing
  `Coprocessor` for execution and `CpuDebugTarget` for debugging;
- an **extension** deriving from `CoprocessorExtension` that owns both and
  installs them into the host's `TubeSocket`;
- a **manifest**, the **firmware** images, and a **plugin entry point**.

The two shipped coprocessors, `src/extensions/acorn-65c02-coprocessor`
(3 MHz 6502 Second Processor) and `src/extensions/acorn-65c102-coprocessor`
(4 MHz 65C102), are the worked example. The second is the first constructed
with a different clock ratio and firmware, and is the pattern for any
variant that shares a CPU family.

## 1. Decide what differs

Answer these before writing code; each maps onto one part of the contract.

| Question | Where the answer goes |
|---|---|
| What is the clock, as a ratio to the host's 2 MHz? | `ClockRatio{numerator, denominator}`, e.g. 3/2, 2/1, 3/1 for 6 MHz, 5/1 for 10 MHz. Exact rationals only. |
| What bridging hardware? | Reuse `TubeUla`, or implement `TubeHostBackend` and `TubeCoprocessorBackend`. |
| What does the CPU's memory map look like, and where do the Tube registers appear in it? | Your memory map class. On the 6502 family the registers are at `&FEF8-&FEFF`; on a Z80 they are I/O ports; on the ARM and 32016 they are memory-mapped elsewhere. |
| What firmware, how big, and how is it mapped at boot? | The manifest's `roms` entry and your memory map's boot overlay. |
| What CPU core? | Any core that can be driven to an exact cycle count, cycle-stepped or instruction-level (see "The CPU core" below). |
| What registers and interrupt lines should the debugger show? | Your `CpuDescriptor`. |

## 2. The extension class

Derive from `CoprocessorExtension` and implement its three accessors plus
`init` and `shutdown`. Parameterise the class on what varies within a
family rather than subclassing, as the 65C02 does with its clock ratio and
label.

```cpp
class MyCoprocessorExtension : public CoprocessorExtension {
public:
    explicit MyCoprocessorExtension(ClockRatio ratio) : ratio_(ratio) {}

    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"tube"};
        return deps;
    }
    std::span<const std::string_view> provides() const override { return {}; }

    void init(ExtensionContext& ctx) override {
        tube_socket_ = &ctx.get<TubeSocket>();

        std::array<uint8_t, ROM_SIZE> rom{};
        if (auto path = config_value("rom")) {
            read_rom_image(std::filesystem::path(*path), rom);   // user override
        } else {
            load_rom("client", rom);                             // packaged firmware
        }

        bridge_ = std::make_unique<TubeUla>();
        runner_ = std::make_unique<MyRunner>(*bridge_, rom, ratio_);
        runner_->reset();

        tube_socket_->install_backend(bridge_.get());
        tube_socket_->install_coprocessor(runner_.get());
    }

    void shutdown() override {
        if (tube_socket_) {
            tube_socket_->remove_coprocessor();
            tube_socket_->install_backend(nullptr);
        }
        runner_.reset();
        bridge_.reset();
    }

    Coprocessor*     coprocessor()  override { return runner_.get(); }
    TubeHostBackend* tube_backend() override { return bridge_.get(); }
    CpuDebugTarget*  debug_target() override { return runner_.get(); }
    // ...
};
```

Rules the server relies on:

- `init()` leaves the socket populated with both the backend and the
  coprocessor; `shutdown()` leaves it empty.
- The extension owns the bridge and the coprocessor and keeps them alive
  while installed; the socket holds non-owning pointers.
- The extension hosts no gRPC service. The server reads `debug_target()`
  and serves the debugger itself.
- Only one coprocessor may attach to `tube`; the server refuses to start
  with two.

## 3. The coprocessor: `Coprocessor`

Implement `run_until`, `pause`, `resume`, `is_paused`, `reset` and
`clock_ratio`. Use `CoprocessorClock` for the arithmetic; do not write your
own.

```cpp
void MyRunner::run_until(uint64_t host_cycle) {
    const uint64_t due = clock_.cycles_due(host_cycle);  // exact, carries remainder
    if (paused_) return;                                  // paused cycles are lost, not deferred
    for (uint64_t i = 0; i < due; ++i) step();            // one coprocessor cycle
}

void MyRunner::reset() {
    cpu_.reset(); memory_.reset(); bridge_.reset();
    clock_.rebase();                                       // next run_until sets the origin
}
```

What the contract requires of you:

- **Exact cycles.** After `run_until(t)` you have executed exactly the
  cycles `CoprocessorClock` said were due, no more and no fewer. Over any
  run from install your cycle count is exactly the ratio times the host
  cycles; `test_coprocessor_skew` checks this for a stub, and your own boot
  test should check it for the real thing, as the 65C102's does.
- **Tube accesses in order.** Every access your CPU makes to the bridge
  happens inside `run_until`, so it is automatically ordered after all host
  accesses at earlier host times and before all later ones. If your core
  executes whole instructions per call (see below), an instruction that
  touches the Tube window must not run past the batch's end.
- **Interrupts.** Sample the bridge's `pirq()` and `pnmi_level()` as your
  CPU recognises interrupts, at least once per instruction. NMI is
  edge-triggered; the 6502 runner tracks handler entry and exit so that a
  second PNMI edge cannot nest an NMI, and a family with the same hazard
  should do the same.
- **Pause.** While paused, `run_until` advances the clock but runs nothing.
- **Reset.** Restart the CPU at its reset vector, re-enter boot mode, reset
  the bridge, and `rebase()` the clock.

Batching: the host calls `run_until` at most every `MAX_COPROCESSOR_SKEW`
(8) host cycles and immediately before any host Tube access, so a call may
be for anything from zero to `8 * ratio` coprocessor cycles.

## 4. The memory map and firmware

Your memory map must route the CPU's accesses to the bridge for the Tube
register window and to RAM or ROM otherwise, implement the boot-mode
overlay your hardware has, and provide side-effect-free `peek` for the
debugger. On the 6502 second processor the first access to the Tube window
unmaps the boot ROM for good; model whatever your hardware does.

Declare the firmware in `manifest.json` and put it in `roms/` beside it:

```json
"roms": [
    {
        "key": "client",
        "filename": "acorn-tube-z80_1_20.rom",
        "size": 4096,
        "description": "Acorn Tube Z80 client ROM v1.20"
    }
]
```

The loader refuses to load the plugin if a declared image is missing or the
wrong size, naming the plugin and the path. `Extension::load_rom(key, span)`
loads it. `size` is the device's size and the image must match it exactly:
a ROM image is the chip's contents, with no content rule for any part of it
and no half-size or padded-dump acceptance -- synthesising a missing part would
be a guess. (The Acorn 6502/65C102 clients are the full 4 KB 2732: the lower
2 KB is `&FF` in Acorn's firmware but is genuine ROM space a client may use,
so the shipped image and any `rom=` override are the whole 4096 bytes; a 2 KB
upper-half-only file is refused.) Keep a `rom` parameter so users can supply
another image, and if you reject a wrong size say what the device is and what
you expected in one sentence.

Hash any firmware you are given against another emulator's copy (B2 and
B-Em ship most of Acorn's client ROMs) or Toby Lobster's ROM library before
debugging a boot failure. A "ROM" saved from a running system's RAM has
been self-modified and will not boot; that cost an afternoon on the 65C102.

## 5. The debugger: `CpuDebugTarget` and `CpuDescriptor`

The server serves your debugger from a description you supply; no server
or client code needs to know your register names.

```cpp
const cpu::CpuDescriptor& MyRunner::cpu_descriptor() const {
    static const cpu::CpuDescriptor d{
        .family = "z80",
        .address_bits = 16,
        .little_endian = true,
        .registers = {
            {"AF", 16, cpu::RegisterRole::Flags, {"C","N","P/V","","H","","Z","S"}},
            {"BC", 16}, {"DE", 16}, {"HL", 16},
            {"SP", 16, cpu::RegisterRole::StackPointer},
            {"PC", 16, cpu::RegisterRole::ProgramCounter},
            // ...
        },
        .signals = {"INT", "NMI"},
    };
    return d;
}
```

Then implement `register_value(i)`, `set_register_value(i, v)` and
`signal_state(i)` by index into that list, and the rest of the interface:
execution control (`cycle_count`, `sequence`, `step`, `step_instruction`,
`prepare_for_step`, `wait_until_idle`, `finish_step`), flat memory access
(`read`, `peek`, `write` with 32-bit addresses; narrow inside if your
address space is smaller), the region model (`get_memory_regions`,
`peek_region`, `read_region`, `write_region`, `machine_type`), and the
breakpoint and watchpoint entries and hit callbacks. `CoprocessorRunner` is
the reference implementation; the 6502 helper in
`beebium/Cpu6502Descriptor.hpp` shows how a family shares one descriptor
between the host and coprocessor implementations.

`with_execution_stopped` and `set_execution_quiescer` are also on the
interface, but you do not implement or call them yourself: inherit
`CoprocessorRunner`'s implementations (or copy them). Your coprocessor runs
on the host emulation thread, so the debugger must halt that thread before it
touches your breakpoint/watchpoint vectors. The server supplies the quiescer
that halts the host and wires it in for you; an extension author never sets
it. Do make your `set_*_entries` mutate through `with_execution_stopped`, as
`CoprocessorRunner` does, so a debugger mutation while the machine runs is
safe.

Three roles exist today, program counter, stack pointer and flags. If your
family needs more, segment registers say, add the role to
`RegisterRole` and the proto with a concrete need in hand; that is the
agreed way roles grow.

Client-side disassembly is per family and lives in the clients. The Python
client ships a 6502 disassembler; a new family brings its own when someone
wants one, and nothing else depends on it.

## 6. The CPU core

Two ways to meet the contract:

- **Cycle-stepped**, as the 65C02 runner: one core call per cycle, memory
  access per bus cycle. Simplest to make exact; if the core generates
  dummy bus cycles (page-cross fixups, read-modify-write), route the ones
  that could land on the Tube window through `peek` so they have no side
  effect. This is what the 6502 runner does and why Chuckie Egg 2023 works.
- **Instruction-level**, which is the practical choice for the 32016,
  80186 and 80286: execute whole instructions, count their cycles from a
  table, and stop the batch before any instruction that touches the Tube
  window and would not complete within the budget; other instructions may
  overshoot by one, with the overshoot deducted from the next batch. Never
  issue dummy accesses at all. The full design, with what it keeps exact
  and what it drops, is Step 3b of `tube-coprocessor-contract.md`. Beebium
  runs coprocessors at their historical clocks only, so the core's job is
  fidelity at real speed, not throughput.

Either way, differential testing against a known-good core or against the
Tube register trace of a working image is how you prove exactness.

## 7. Build and packaging

Follow `src/extensions/acorn-65c102-coprocessor/CMakeLists.txt`:

- a `SHARED` plugin target with `plugin_entry.cpp` exporting
  `beebium_create_extension`, linking `beebium_core` (position-independent)
  and `beebium_extension_api`, with `CXX_VISIBILITY_PRESET hidden` so your
  private copies of core classes never interpose on the server's;
- `beebium_finalize_plugin(TARGET ... NAME <name>)`, which deploys the
  library, `manifest.json` and `roms/` to `<exe-dir>/extensions/<name>/`
  and installs them under `bin/extensions/<name>/`;
- if tests link your extension directly, a `STATIC` library of the same
  sources, as the 65C02 has;
- `add_subdirectory` in `src/extensions/CMakeLists.txt`, and the plugin
  target in the `beebium-servers` aggregate in `src/server/CMakeLists.txt`
  so every artifact, package and the macOS app bundle ships it.

Do not link `beebium_service` or host a gRPC service; do not include
anything under `src/server`.

## 8. Tests to write

The minimum, mirroring what exists for the 65C02 and 65C102:

- A boot test: the machine reaches your coprocessor's banner and a prompt,
  and its cycle counter is exactly the ratio times the host's over the run.
- `list-extensions` shows your CLI flag from the extensions directory;
  `describe-extension` shows your ROM entry and parameters.
- A packaged-firmware proof: with `BEEBIUM_ROM_DIR` pointing at a directory
  of host ROMs only, the coprocessor still boots.
- The debugger through the descriptor: read and set a register, read a
  signal, a breakpoint hit, all via `connect_coprocessor()` in Python.
- The skew observer from `test_coprocessor_skew` across your boot, if your
  core is instruction-level: it proves exact accesses and the bound.
- One scenario-level protocol exercise. For anything that runs Acorn OS
  calls, the wfsinit ADFS-select test parametrised over coprocessors is the
  heaviest R2/R3/R4 workout available; add your flag to its parameter list.

## 9. Things that are the host's business, not yours

- Pacing to real time, and the sleep and burst structure of the emulation
  loop.
- The skew bound and how often `run_until` is called.
- Bus stretching of the host CPU.
- Cross-processor debugger stops: the server wires both directions through
  `Coprocessor::pause()` and `Machine::pause()`.
- Which filing system ROM provides the Tube host code.

If you find your coprocessor needs to know any of these, that is a gap in
the contract; raise it rather than reaching around it.
