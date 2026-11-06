# Measuring server performance

How to measure what a headless server costs on a slow host, a Raspberry Pi
above all, and where that cost goes. The method is the one behind the
baseline numbers below; `tools/perf/profile_server.py` runs it.

The rule for this work is to measure first: an optimisation is proposed
against numbers taken this way, and judged by numbers taken the same way
after it.

## What to measure

Three numbers describe a server's cost:

- **The emulation thread's share of one core at 1x.** The emulation loop runs
  on the server's main thread, so this is the main thread's CPU time over a
  window, from `/proc/<pid>/task/<pid>/stat`. Near 100% means the host only
  just keeps up: anything else the host does makes the emulator miss real
  time.
- **The ceiling**: the fastest the emulator runs on this host. Run at a
  speed the host cannot reach (`--speed 10` on a Pi) and read the achieved
  speed; the emulation thread then sits at 100%.
- **Where the time goes**: a sampled profile of the server, by thread and by
  function, taken at `--speed 10` so the emulation thread is busy the whole
  window and the shares are clean, and at 1x for comparison.

The other threads matter as well, since on a four-core Pi they compete with
the client: the render loop (pixel batches into the framebuffer) is the next
largest, and the pacing timer is small.

## The pacing statistics

`SystemService.GetPacingStats` reports the server's own account, sampled over
a window of about five seconds:

- `speed_multiplier`: the speed asked for (0.0 is unlimited).
- `achieved_speed_multiplier`: emulated cycles run in the window, divided by
  the cycles real time at 2 MHz would give. 1.0 is real time.
- `estimated_max_speed_multiplier`: the achieved speed divided by the
  fraction of the window the emulation thread spent computing rather than
  sleeping. It extrapolates the ceiling from a run below it, and is an upper
  bound: per-tick overheads do not scale perfectly with speed. When the host
  cannot reach the speed asked for, the thread never sleeps and the estimate
  equals the achieved speed. 0.0 means no estimate yet (paused, or the first
  window has not finished).

The estimate at 1x and the achieved speed at 10x agree to within a few
percent on the Pi 5 (2.30 and 2.25 for an idle prompt), so the estimate is
a fair guide when a run at 10x is not practical.

## Thread names

The server names its threads (`set_current_thread_name`) so that `top -H`,
`ps -L`, `/proc/<pid>/task/<tid>/comm` and `perf report --sort comm` show
what each one does: `render-loop`, `pacing-timer`, `indicators`,
`sideways-scan`, `desc-refresh`, `signal-dispatch`, `input-watch`,
`shutdown`. gRPC names its own worker threads. The emulation loop runs on
the main thread, which keeps the executable's name (`beebium-model-b`) so
that `pgrep -x` and `killall` still find the process; the tools report it as
`emulation (main)`.

## Building for profiling

Profile a build with symbols and frame pointers, so `perf record -g` walks
the stacks cheaply and accurately:

```bash
cmake -S . -B build-prof -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_FLAGS=-fno-omit-frame-pointer \
    -DCMAKE_CXX_FLAGS=-fno-omit-frame-pointer \
    -DBEEBIUM_BUILD_TESTS=OFF
cmake --build build-prof --parallel 2 --target beebium-model-b \
    beebium_ext_acorn_65c02_plugin
```

RelWithDebInfo compiles at `-O2`; the shipped Release servers use `-O3`, so
absolute numbers from this build differ a little from a release binary's.
Compare like with like. On a Pi with 8 GB, two parallel jobs are safe; the
server's main translation unit alone takes about 2 GB to compile.

`perf` comes from the distribution (`linux-perf` on Debian). Profiling your
own processes without root needs `kernel.perf_event_paranoid` at 2 or
below, which is what the Raspberry Pi kernel sets.

## Running the measurements

From the repository root, with the Python client's environment:

```bash
uv run --project clients/beebium-python-client \
    python tools/perf/profile_server.py \
    --server build-prof/src/server/beebium-model-b \
    --speed 1 --speed 10 --seconds 15 \
    --perf-stat --perf-record --callers-of __aarch64_ldadd8_acq_rel \
    --output-dirpath perf-out
```

Without `--perf-stat` and `--perf-record` it measures only the per-thread CPU
and the pacing statistics: the ceiling measurement. Use `--port` and
`BEEBIUM_USER_DIRPATH` to keep clear of other servers on a shared host; the
script stops only the servers it started.

Each scenario starts its own server, drives the guest into a state with the
Python client and measures each speed in turn: a three-second settle, a
window for the per-thread CPU and pacing statistics (with `perf stat`
alongside), then a `perf record -F 997 -g` window of the same length. The
scenarios:

| Scenario | Machine | Guest |
|----------|---------|-------|
| `idle` | Model B | the MODE 7 BASIC prompt |
| `mode2` | Model B | a BASIC loop drawing random lines in MODE 2 |
| `disc` | Model B, 1770 DFS 2.26 | BASIC writing a 4 KiB file and reading it back, for ever, on a blank disc |
| `tube` | Model B, 1770 DFS, 65C02 second processor (the `model-b-disc-65c02-copro` configuration) | a BASIC loop on the second processor, printing every 100 iterations |

The output directory holds, for each scenario and speed: the server log, the
screen as the measurements ended (to show the program was still running),
`perf stat` output, the `perf.data` file, a flat report by thread and
symbol, and the caller graph of each `--callers-of` symbol. `results.json`
gathers the per-thread CPU and pacing numbers.

### How much a run varies

Two runs of the same binary on the Pi 5, one after the other, reached
ceilings within about 2% of each other in three scenarios and 4% apart in
`disc`, whose file I/O makes it the noisiest. The emulation thread's share at
1x varies by a few points too. So a change of 2% or less in one run is not a
result: repeat the run, or measure the same binary twice, before reading it
as one. A change that matters shows in every scenario it should touch, and
not in those it should not (a change to the empty Tube socket leaves `tube`,
which has a coprocessor fitted, where it was).

## Baseline (November 2026)

### Pi 5 (Cortex-A76, 2.4 GHz)

RelWithDebInfo build, frame pointers, master at 78574b3f plus thread names.
Headless, no client attached.

| Scenario | Emulation thread at 1x | Render loop at 1x | Ceiling (achieved at 10x) | Estimated ceiling at 1x |
|----------|------------------------|-------------------|---------------------------|-------------------------|
| idle | 46.1% | 6.8% | 2.26x | 2.30x |
| mode2 | 43.5% | 5.5% | 2.39x | 2.38x |
| disc | 47.6% | 6.9% | 2.18x | 2.21x |
| tube | 60.3% | 6.8% | 1.78x | 1.73x |

`perf stat` at 10x: about 2.05 to 2.16 instructions per cycle in every
scenario, with few cache misses (30 to 46 million in 15 s against 39 billion
cycles): the emulation thread is compute-bound.

Where the time goes at 10x, idle prompt (the other scenarios are alike, with
`CoprocessorCpu::tick` at 6.5% added for `tube`, and the MODE 2 video path
in place of the SAA5050 for `mode2`):

| Share | Thread | Function |
|-------|--------|----------|
| 19.0% | emulation | `__aarch64_ldadd8_acq_rel` (GCC's out-of-line 64-bit atomic add) |
| 10.6% | emulation | `Machine::step` |
| 8.3% | render-loop | `FrameRenderer::process_unit` |
| 7.7% | emulation | `Saa5050::emit_pixels` |
| 4.6% | emulation | `Via6522::update_port_pins` |
| 4.2% | emulation | `VideoRenderer::deliver` |
| 3.5% | emulation | `VideoBinding::tick_falling` |
| 3.3% | emulation | `CpuBinding::execute_cycle` |
| 3.2% | emulation | `VideoRenderer::render` |
| 3.0% | emulation | `Sn76489::tick` |
| 3.0% | emulation | `fold_irqs` (the IRQ aggregator) |
| 2.9% | emulation | `Via6522::tick_control_phi2_trailing_edge` |
| 2.8% | emulation | `Via6522::update_phi2_trailing_edge` |
| 2.1% | emulation | `Saa5050::byte` |
| 1.9% | emulation | `Machine::run` |

The atomic add has two callers, in every scenario:

- `VideoRenderer::deliver`, 15.0% of all samples (12 to 17% across the
  scenarios): `OutputQueue::push` commits each pixel batch with
  `write_pos_.fetch_add(n, acq_rel)`.
- `Machine::step`, 4.1% (3 to 5%): `++sequence_` on a
  `std::atomic<uint64_t>`, a sequentially consistent read-modify-write on
  every emulated cycle, stretch cycles included. Only the debugger reads the
  sequence number.

### Pi 400 (Cortex-A72, 1.8 GHz)

Release 0.16.1 binary, idle MODE 7 prompt: the emulation thread takes 97.5%
of a core at 1x and the ceiling is 1.09x; the render loop takes about 13%.
The A72 has no LSE atomics, so the atomic add above is a load-exclusive /
store-exclusive loop. Build and profile on the Pi 5, and use the Pi 400 only
to confirm a result there.

## After the two atomics (November 2026)

The two read-modify-writes above were replaced: `OutputQueue` publishes each
index with a release store (c3064268), and `Machine::step` counts cycles in a
plain integer and folds them into `sequence_` only where the debugger can
observe the machine (924fc284). Measured the same way as the baseline, same
host, same build type.

### Pi 5

| Scenario | Emulation thread at 1x: before / after | Ceiling at 10x: before / after |
|----------|----------------------------------------|--------------------------------|
| idle | 46.1% / 42.1% | 2.26x / 2.52x |
| mode2 | 43.5% / 41.3% | 2.39x / 2.52x |
| disc | 47.6% / 44.7% | 2.18x / 2.34x |
| tube | 60.3% / 55.1% | 1.78x / 1.89x |

The atomic helper has no samples in any scenario at either speed. The top of
the idle profile is now `Machine::step_cycle` (the old body of `step`, 15%),
the render loop's `FrameRenderer::process_unit` (9%), `Saa5050::emit_pixels`
(8%), `SystemViaPeripheral::update_control_lines` (8%, a symbol the earlier
inlining had folded into `step`), `Via6522::update_port_pins` (6%),
`CpuBinding::execute_cycle` (5%) and `VideoRenderer::deliver` (4%).

`VideoRenderer::deliver` costs 11 to 14% in `disc` and `tube` against 4 to 5%
in `idle` and `mode2`. `perf annotate` puts three quarters of its samples on
the instruction after the acquire load of `read_pos_` in `available()`: a
coherence miss on the line the render loop has just written on another core.
That is what the cached-index refinement below would remove. There is also a
division per push for `% capacity_`.

### Pi 5, after the cached indices (089fb37e)

Each ring side now reloads the other's index only when its own copy says
full or empty. Ceilings at 10x: idle 2.53x, mode2 2.55x, disc 2.40x, tube
1.93x; against the baseline that is +12%, +7%, +10% and +8%. `deliver` fell
from 14% to 3.4% in `disc` and out of the top ten in `tube`, but the
scenarios gained only 2 to 3%, because part of the stall moved rather than
vanished: `SystemViaPeripheral::update_port_a` appeared at 8 to 10% in those
two scenarios, almost all of it on the acquire load of a keyboard matrix
column (`LDAR`, since the generic Armv8-A build has no `LDAPR`). An `LDAR`
waits for earlier `STLR`s to drain, and `produce()` issues one release store
per pixel batch, up to two per emulated cycle. The next step is to publish
the ring's index once per pacing batch rather than per pixel batch.

### Pi 5, after publishing once per run (1619dded)

The producer now stages pixel batches and publishes `write_pos_` with one
release store at the end of each `run()` (and every 4,096 batches, so a long
late tick cannot fill the ring), at the same points the sequence number is
published. Ceilings at 10x: idle 2.69x, mode2 2.84x, disc 2.57x, tube 2.05x;
against the baseline, +19%, +19%, +18% and +15%. The keyboard matrix reads
fell from 8 to 10% to under 1% in every scenario: with the release stores
gone, the acquire loads no longer wait. The remaining profile is flat
per-cycle work: `Machine::step_cycle` (14%), the SAA5050 (9%), the VIAs
(about 11% between `update_port_pins` and the phi2 edges), the CPU binding
(5%), the sound chip (4 to 5%) and the IRQ aggregator (6.5% at 1x).

### Pi 400

Release build of the same commit, idle MODE 7 prompt, headless: the emulation
thread fell from 97.5% to 90.9% of a core at 1x, and the ceiling rose from
1.09x to 1.21x. With the Qt client attached on the same Pi (Model B with
disc, Processor sidebar showing) the server now holds 1.00x with an estimated
ceiling of 1.08x, where the previous server wobbled between 0.85x and 0.97x.
The emulation thread is still at 100% of a core in that session, with the
frame-stream thread at 20% and the render loop at 16%; the headroom is thin.

## What an SPSC ring needs

`OutputQueue` is a single-producer, single-consumer ring, so each index has
one writer. The minimum ordering is:

- **Producer**: read its own `write_pos_` relaxed; read `read_pos_` with
  acquire (the consumer has finished reading a slot before the producer
  overwrites it); publish with `write_pos_.store(w + n, release)`.
- **Consumer**: read its own `read_pos_` relaxed; read `write_pos_` with
  acquire (the slot contents are visible); release slots with
  `read_pos_.store(r + n, release)`.

No read-modify-write is needed: on AArch64 these are plain `LDAR`/`STLR`
(`LDAPR` with Armv8.3), with no call to an atomic helper. `OutputQueue` is
built this way.

Each side also keeps a private copy of the other's index and reloads it, with
acquire, only when its copy says the ring is full (producer) or empty
(consumer). Indices only grow, so a stale copy only understates: the producer
is offered no more space than is free and the consumer no more items than are
written, and neither reports full or empty without reloading first. Each side
then stays off the other's cache line until it has to look.
`VideoRenderer::deliver` takes its slot through `get_producer_buffer()`, the
producer's own view, rather than asking `available()`, which reads both
indices afresh. `size()`, `available()`, `empty()` and `full()` keep that
fresh view for any other caller.
