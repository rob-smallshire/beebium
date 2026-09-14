# The emulation-thread ownership model

This document is the reference for anyone writing a gRPC service or a peripheral
extension that touches the emulator's state. It states who owns that state, the
one primitive for reaching it safely from another thread, and the rules and
tests that enforce it. Read it before adding a handler that reads or writes
guest memory, CPU registers, or any device the machine emulates.

The short version: **the emulation thread owns all emulated state. Any other
thread that mutates it, or reads it and needs a consistent value, must halt the
emulation thread across that access.** There is exactly one primitive for doing
so, and a per-cycle lock is never the answer.

## What runs on the emulation thread

One thread runs the machine. The per-iteration work lives in one place --
`step_emulation` in `src/server/include/beebium/server/ServerMain.hpp` -- which
every subcommand that runs the machine calls. Each iteration it:

1. parks in `machine.wait_if_paused(on_wake)` while the machine is paused;
2. takes the caller's cycle budget (the serving loop waits for the next pacing
   tick here, a sleep kept outside the busy scope below);
3. under a `Machine::EmulationBusyScope`, does the per-iteration work: ticks the
   disc drives, updates the Econet socket's speed, and calls `machine.run(N)`.

**One assembly, one stepper.** The machine is built in exactly one place,
`assemble_machine` (ROMs, disc, Econet, extensions + `resolve_and_init`, the
quiescer wiring, reset), and advanced in exactly one place, `step_emulation`.
The server's `start` loop and the headless `capture-screenshot` loop both
consume the assembly and drive the stepper -- they differ only in serving
concerns (gRPC, pacing, stats) and assembly options (screenshot enables video
but not audio), never in how the machine is built or run. This is deliberate:
`capture-screenshot` once had its own extension-less assembly and its own
`run()` loop, so a Tube preset silently dropped the coprocessor and rendered the
host banner. A subcommand that needs to run the machine must go through the
assembly and the stepper; a `test_server_main` guard pins that the header
constructs a `MachineType` in exactly one place.

`machine.run()` steps the CPU, which drives every peripheral and, through
`Machine::step` -> `TubeSocket::run_coprocessor_until`, the coprocessor too. So
"the emulation thread" touches: the 6502 and its memory map; the VIAs, CRTC,
video ULA, sound chip; the disc controller and drives; the Econet ADLC and its
backend chain; the Tube ULA and the coprocessor's CPU, memory and ROM. All of
that is emulated state, and all of it is read and written on this thread every
cycle.

gRPC service handlers and extension RPC dispatchers run on **other** threads
(gRPC worker threads). When one of them touches emulated state, it races the
emulation thread unless it first halts it.

## The pause/quiesce primitive

`Machine::with_emulation_paused(f)` runs `f` with the emulation thread
guaranteed not to be touching machine state. Use it from any non-emulation
thread; never from the emulation thread itself (it would wait for itself
forever).

It separates two independent notions of "paused", so neither can lose the
other's intent:

- **`user_paused_`** -- the logical pause a debugger client sets with `pause()`
  and clears with `resume()`. `is_paused()` reports this and only this.
- **`quiesce_depth_`** -- raised for the duration of a `with_emulation_paused`
  body (and nested bodies).

The emulation loop reads a derived atomic, `paused_ == user_paused_ ||
quiesce_depth_ > 0`, recomputed under `debug_mutex_` whenever either source
changes. Keeping them apart means a `Stop` that lands while another caller is
quiescing is not discarded, and a transient quiesce never makes a running
machine look stopped to a client, nor does a `resume()` release a machine
another caller is still quiescing.

`with_emulation_paused` holds a **recursive** quiesce mutex across the body, so
distinct callers serialise and only one body runs against machine state at a
time. It is recursive by necessity, not convenience: a quiescing body may call a
Machine mutator that itself quiesces on the same thread (a `DebuggerService`
`with_execution_change` handler calling `Machine::set_watchpoint_entries`, which
is itself a `with_emulation_paused`). Do not "fix" it to a plain mutex.

### Waiting for idle covers everything, not just run()

`wait_until_idle()` (which `with_emulation_paused` calls after raising the
pause) waits until the emulation thread is neither inside `run()` (`in_run_`)
nor doing per-iteration work outside it. The latter is covered by
`Machine::EmulationBusyScope`, an RAII guard the server loop holds around the
work between `wait_if_paused` and the end of `run()`. Its constructor re-tests
`paused_` under `debug_mutex_`: if a quiesce landed in the gap since
`wait_if_paused` returned, the scope is inactive and the loop re-parks instead
of racing. `on_wake` housekeeping (which ticks drives while the machine is
parked) stands down whenever `quiesce_depth_ > 0`, so a quiescing body mutating
a device never races the tick of that same device.

## The rule for services

A gRPC service (`DiscService`, `SidewaysService`, `EconetService`,
`DebuggerService`, ...) holds a `MachineType& machine_`. The emulation thread
never takes a service's mutex, so a handler may hold its own `mutex_` and call
`machine_.with_emulation_paused(...)` with no risk of lock inversion.

- **A mutation** of emulated state -- installing or removing a device, writing
  guest memory or a CPU register, retyping or rewriting a sideways bank,
  fitting or removing Econet hardware -- **must** run inside
  `with_emulation_paused`. `DiscService::InsertDisc` and `InstallDiscController`,
  `SidewaysService::ConfigureSlot`, `EconetService::EnableEconet`/`DisableEconet`,
  and `DebuggerService`'s `WriteMemory`/`WriteRegion`/`SetCpuState` all do.
- **A read that must be consistent** -- a bulk copy that would tear if the
  emulation thread wrote mid-copy, or a snapshot of several fields that must
  agree -- takes a **quiesced snapshot**: do the copy inside
  `with_emulation_paused`, then process the private copy outside. The sideways
  header scanner copies a 16 KiB bank this way.
- Keep the body tight. `with_emulation_paused` stalls the emulation thread for
  the body's duration, so do file I/O, parsing, allocation and response-building
  outside it, and put only the actual state access inside. The stalls that
  matter here are sub-microsecond (a 16 KiB bank copy measures ~0.6 us; see the
  `[bench]` case in `tests/test_tier2_quiesce.cpp`), far below one host cycle's
  pacing budget per call.

## The rule for extensions

A peripheral extension's device is emulated state too, but a plugin has no
`Machine` handle. Instead, `ExtensionRpcDispatcher` carries a **bus quiescer**:
the server injects it (via `set_bus_quiescer`, forwarding to
`with_emulation_paused`) into every dispatcher, and a dispatcher reaches it
through the protected helper `with_bus_stopped(fn)`. A dispatcher author never
sets the quiescer; when none is set (a standalone unit test with no running
machine) `with_bus_stopped` runs `fn` directly, which is safe single-threaded.

Wrap every device-state mutation a handler makes in `with_bus_stopped` -- a
register poke, a sub-device install/remove, arming or (crucially) detaching an
event buffer whose storage the handler owns. This is the peripheral analogue of
the coprocessor debug target's `with_execution_stopped`. See
`docs/howto_write_a_peripheral_extension.md` section 3d.

## Lifetimes across the boundary

A mutation that destroys an object the emulation thread dereferences (a disc
controller, the Econet ADLC/handshake/backend) is safe under
`with_emulation_paused` because the emulation thread is parked while the object
dies. But a **gRPC reader** may hold a reference to such an object for longer
than a single call -- a `SubscribeEconetEvents` stream holds the event recorder
for its whole life; `SystemService` reads the Econet backend during
advertisement. Halting the emulation thread does not protect those readers from
a concurrent teardown on another gRPC thread.

For that, hand the reader a **co-owning** handle, not a raw pointer.
`EconetSocket` keeps its `backend_` and `observable_` as `std::shared_ptr`
guarded by a `lifetime_mutex_`, and `observable()` / `backend_shared()` return
copies; a concurrent `DisableEconet` drops the socket's own reference, but the
object lives until the reader's copy does. No raw pointer outlives the object it
names.

## Hot-path accessors stay lock-free

The emulation thread reads device members on its per-cycle path (the Econet
`tick_rising`/`tick_falling`, `nmi_pending`, the disc controller poll). Those
members are only ever *replaced* with the emulation thread parked (the mutators
above run inside `with_emulation_paused`), so the hot path reads them **without
a lock**, and the raw accessors (`EconetSocket::adlc()`/`handshake()`/
`backend()`) are lock-free by contract -- valid on the emulation thread or with
the machine quiesced. A lifetime mutex guards only the `shared_ptr` members'
copy and assignment for the gRPC readers above.

**Never put a mutex on the per-cycle path.** A lock taken every host cycle is a
measurable tax on emulation (the `TypeAheadQueue` mutex was ~7% before it was
removed; a stray `adlc()` lock reintroduced the same shape and was caught in
review). If a getter is only ever called on the emulation thread or under
quiesce, it needs no lock; if it is called from a gRPC thread, it takes a
co-owning copy or a quiesced snapshot instead.

## Enforcement: the TSan witnesses

The model is enforced by ThreadSanitizer tests. Build with
`-DBEEBIUM_ENABLE_TSAN=ON` (a fresh build dir; `BEEBIUM_BUILD_SERVICE` must stay
on) and run with `TSAN_OPTIONS="halt_on_error=0"`.

- `tests/test_pause_quiesce.cpp` -- the primitive itself: a pause during a
  quiesce survives; concurrent quiescers serialise; a quiesced device mutation
  excludes `on_wake`; the busy scope excludes a loop-body mutation; `WriteMemory`
  on a running machine is serialised.
- `tests/test_debugger_entry_race.cpp` -- breakpoint/watchpoint entry-vector
  swaps on host and coprocessor.
- `tests/test_tier2_quiesce.cpp` -- the extension bus quiescer and the Econet
  enable/disable lifetime, each with a hidden `[.][race]` variant that removes
  the fix so the race can be witnessed before it, and a
  "hot path takes no lifetime lock" test that holds the lifetime mutex while
  `step()` runs and would hang if the per-cycle path locked it.
- `tests/test_tier3_snapshots.cpp` -- a gRPC inspection getter snapshotting
  live device state against the emulation loop: the real (paused) getter is
  clean, the hidden `[.][race]` variant reads the same state unpaused and
  races. A `[bench]` case measures the getter's quiesce cost (~0.1 us).

When you add a handler that touches emulated state, add a witness in the same
shape: it should be clean under TSan with your `with_emulation_paused` /
`with_bus_stopped` wrap and race without it.

## Checklist

- Mutating emulated state from a gRPC thread? Wrap it in
  `with_emulation_paused` (service) or `with_bus_stopped` (dispatcher).
- Bulk or multi-field read that must be consistent? Take a quiesced snapshot;
  process the copy outside the pause.
- Handing a gRPC reader an object the emulation thread can destroy? Give it a
  co-owning `shared_ptr` copy, not a raw pointer.
- Adding a getter the emulation thread calls every cycle? Keep it lock-free;
  never lock the per-cycle path.
- Added a state-touching handler? Add a TSan witness for it.
