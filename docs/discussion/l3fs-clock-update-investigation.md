# L3FS Clock Display Update Timing Investigation

## Problem Statement

The Level 3 File Server v1.26 running in Beebium with the `--acorn-rtc`
extension displays a clock (bottom-right of the MODE 7 screen) that should
update roughly every 25-30 seconds, matching the behaviour reported by a
user with real hardware and Ken Lowe's reproduction User Port RTC module.
Instead, the display updates only every 3-5 minutes, causing the shown time
to lag significantly behind wall-clock time.

### Reproduction

```
./beebium-model-b-romram \
  --sideways 9:rom:acorn-anfs_4_18.rom \
  --sideways 10:rom:acorn-adfs_1_30.rom \
  --sideways 11:rom:acorn-dfs_2_26.rom \
  --fdc acorn-1770 \
  --floppy 0:/path/to/FS3v126.ssd \
  --acorn-scsi --scsi-hdd 0:/path/to/scsi0.dat \
  --station 254 --aun-port 10254 \
  --aun-map 0.221:127.0.0.1:10221 \
  --machine-name "L3FS" \
  --acorn-rtc layout=7bit-year-in-r7 \
  --tube 65C02-3MHz --advertise
```

(Historical: this is the command line as it was during the investigation,
when coprocessors ran as a separate executable. Today the coprocessor is a
plugin attached with `--tube-65c02`, and several of the other options have
changed form too.)

After boot, `*RUN FS3v126`, respond `1` (drives), `S` (start), `2`
(stations). The time display at the bottom-right should update roughly
every 30 seconds. In Beebium it updates every 3-5 minutes.


## Investigation Method

All measurements were empirical, using gRPC diagnostic APIs to observe the
running system from the outside. No reasoning about what *should* happen --
only what *does* happen.

### Tools used

| Tool | Purpose |
|------|---------|
| `DebuggerControl.GetState()` | Read host and parasite cycle counts |
| `DeviceInspection.GetTubeState()` | Read Tube register transfer counters |
| `CPU.GetRegisters()` | Sample program counter for profiling |
| `Memory.ReadBytes()` | Dump memory at hotspot addresses |
| `AcornRtcService.WatchActivity()` | Stream register read/write events (new) |
| Python `time.monotonic()` | Wall-clock timestamps for rate calculations |
| BASIC `TIME` | Emulated centisecond timestamps per OSBYTE call |

### Key principle

The Tube requires host-side code to function. This code lives in the ANFS
ROM (specifically the "Tube host code" component, which is the complement
of the parasite's Tube client at $F800-$FFFF). Without the ANFS ROM loaded,
the Tube is not operational -- the parasite connects but no R2 commands are
dispatched. Any test of Tube behaviour MUST have the ANFS ROM present and
verify "TUBE" appears in the boot message ("Acorn TUBE 6502 64K").


## Findings

### 1. CBUS protocol is correct

The SAF3019P CBUS protocol implementation was verified against the
datasheet (Fig. 4, TIME ADDRESS / TIME READ cycle) and the exact byte
sequences produced by the L3FS v1.26 code (Uade04.asm).

Key detail: the DNG10 "load pulse" routine produces **three** ORB writes
(`0x00`, `0x02`, `0x00`), not two. This is because `JSR DNG45` returns to
the instruction after the JSR, which is `DNG15` (due to code layout), which
falls through into `DNG45` again. The third write creates the CLB falling
edge with DLEN LOW that transitions the chip into read phase. This is
verified by 18 passing unit tests in `test_saf3019p.cpp` and
`test_saf3019p_v126.cpp`.

### 2. RTC counter advancement is correct

`Saf3019p::advance_counters()` uses `std::chrono::steady_clock` to track
wall-clock time. The registers always reflect the actual current time. This
was never the problem -- the RTC has the right time, but the L3FS doesn't
read it often enough.

### 3. VIA integration is correct

Reading IRB (OSBYTE 150) correctly calls `update_port_pins()` at
`Via6522.cpp:41`, which triggers `update_port_b()` on the
`AcornRtcExtension`, giving the chip a chance to return fresh data bits.
The data bit (`read_data_bit()`) correctly reflects the current position in
the shift register.

### 4. Clock rates are correct

Measured empirically over 6 x 10-second intervals:

| Processor | Expected | Measured | 
|-----------|----------|----------|
| Host      | 2.000 MHz | 2.000 MHz |
| Parasite  | 3.000 MHz | 3.000 MHz |
| Ratio     | 1.500     | 1.500     |

Both processors run at exactly the right speed. The pacing console output
confirms: `Pacing: 2.000 MHz (target 2.000 MHz, 100.0%)`.

### 5. The L3FS scheduling mechanism

The L3FS main loop (`Uade15.asm:65-98`) controls when the RTC is read:

```
CPOLXX:
    JSR PRTIM           ; Read RTC dongle, display time
    LDA #85
    STA TCOUNT          ; Reset counter

CPOLL0:
    ...                 ; Check keyboard events

COMRTS:
    LDY #1              ; ~0.36 second timeout
    JSR WAIT3            ; Poll Econet Rx (OSBYTE 51 x ~400)
    BMI DOCMND           ; Received packet -> process command

    DEC TCOUNT           ; No packet -> decrement
    BPL CPOLL0           ; Counter > 0 -> keep polling
    BMI CPOLXX           ; Counter < 0 -> read RTC again
```

With TCOUNT=85 and WAIT3 taking ~0.36 seconds on real hardware, the RTC
is polled every 85 * 0.36 = **~30 seconds**.

### 6. WAIT3 inner structure

`WAIT3` (`Uade20.asm:159`) is a triple-nested polling loop:

- Outer: `TIMER2 = Y = 1`
- Middle: `TIMER1 = WAITCL = 80` (`Uade02.asm:276`)
- Inner: `TIMER = ONEMS = 5` (`Uade02.asm:277`)

Each inner iteration calls OSBYTE 51 (poll Econet Rx) through the Tube.
Total: **~400 OSBYTE calls** per WAIT3 invocation.

On real hardware, each Tube OSBYTE takes ~0.9ms, giving
400 * 0.9ms = 360ms per WAIT3.

### 7. Parasite MOS OSBYTE dispatch (from Tube client disassembly)

Source: `acorn-6502-tube-client-1.10` disassembly.

The parasite MOS at `osbyte_impl` ($FA73) dispatches all OSBYTE calls:

- **A < &80** (e.g. OSBYTE 51 = &33): `osbyte_low` at $FA77. Sends
  command byte 4, then X, then A via Tube R2. Waits for one response byte.
- **A >= &80** (e.g. OSBYTE 150 = &96): Checks for &82/&83/&84 (handled
  locally). All others via `osbyte_high` at $FAA8: sends command byte 6,
  then X, Y, A via R2. Waits for carry+Y and X response bytes.

Both paths go through R2 to the host. The parasite spends most of its time
spinning at the R2 status poll loops (`BIT $FEFA` / `BVC` or `BPL`),
waiting for the host to consume or produce R2 bytes.

### 8. Tube host code (from ANFS ROM disassembly)

Source: `anfs-4.08.53` disassembly. The Tube host code is relocated from
the ANFS ROM to zero page ($0000-$06FF) at boot time.

The main loop at `:0036` (matching our PC sampling hotspot exactly):

```
.tube_main_loop
    BIT $FEE0           ; :0037 - check R1 (OSWRCH from parasite)
    BPL tube_poll_r2    ; :0039 - no R1 data: check R2
    LDA $FEE1           ; :003B - read R1 data byte
    JSR $FFEE           ; :003E - OSWRCH (display character)
.tube_poll_r2
    BIT $FEE2           ; :0041 - check R2 (command from parasite)
    BPL tube_main_loop  ; :0044 - no R2 data: loop back to R1
    ...                 ;        - dispatch R2 command
```

The R2 dispatch table uses even command numbers as byte offsets:

| Cmd | Handler | Purpose |
|-----|---------|---------|
| 4   | `tube_osbyte_2param` | OSBYTE < &80 (2 params: X, A) |
| 6   | `tube_osbyte_long`   | OSBYTE >= &80 (3 params: X, Y, A) |

The `tube_osbyte_2param` handler reads X and A from R2, calls host OSBYTE,
sends X result back via R2, then returns to `tube_main_loop`. Each R2
read/write involves polling the R2 status register.

### 9. Per-call OSBYTE latency measurement

A targeted test (`test_tube_osbyte_throughput.py`) measures individual
OSBYTE 51 call latency under normal paced execution. The test:

1. Boots Model B ROM/RAM board with Tube + ANFS (verified by "TUBE" in
   boot message)
2. Types a BASIC program that pokes a single-OSBYTE machine code routine
   (`LDA #51 / LDX #0 / JSR &FFF4 / RTS`)
3. Calls it 32 times, timing each with BASIC `TIME` (10ms resolution)
4. Measures wall-clock time between `=GO` and `=OK` markers

Results (consistent across runs):

```
  Wall time: 3.03s
  Wall avg:  94.5 ms/call
  Wall rate: 11 OSBYTE/s

  BASIC TIME per call (10ms resolution):
    min:  30 ms (3 cs)
    mean: 53 ms (5.3 cs)
    max:  60 ms (6 cs)
    raw:  6 5 6 5 6 5 3 6 5 6 5 6 5 6 5 6 5 3 6 5 6 5 6 5 6 5 6 5 6 5
```

Key observations:

- **Alternating 5/6 pattern**: Values alternate between 5 and 6
  centiseconds (50-60ms), with occasional 3s (30ms). This is consistent
  with a fixed number of tick-boundary crossings per call, with occasional
  lucky alignment.
- **~10-12 ticks per OSBYTE**: At 200 Hz pacing (5ms/tick), 50-60ms
  corresponds to 10-12 tick boundaries crossed per OSBYTE round-trip.
  Each R2 byte transfer potentially stalls at a tick boundary (up to 5ms
  per stall). A low OSBYTE round-trip involves ~6 R2 operations (host
  main loop dispatch + 2 reads + OSBYTE execution + 1 write + return),
  each potentially crossing a tick boundary.
- **Wall-clock overhead**: The wall-clock average (94.5ms) exceeds the
  BASIC TIME mean (53ms) because the FOR/NEXT loop and BASIC variable
  storage between calls also involve Tube operations.
- **Expected on real hardware**: ~0.9ms per OSBYTE, ~1100 OSBYTE/s.
  The measured latency is **55-67x slower** than real hardware.


## Root Cause

### Independent pacing creates tick-boundary latency

Both host and parasite are paced independently at **200 Hz** (5ms per
tick):

- Host: 2 MHz / 200 Hz = 10,000 cycles per tick
- Parasite: 3 MHz / 200 Hz = 15,000 cycles per tick

Each processor runs a batch of cycles, then sleeps until its next tick.
They are **not synchronised** with each other at the tick level.

When the parasite writes a byte to Tube R2 and the host is sleeping (which
it is most of the time -- the `run` percentage is only 17%), the parasite
must wait up to **5ms** for the host to wake up, see the data, and process
it. Conversely, when the host writes the response back to R2, the parasite
may need to wait for its next tick to see it.

A single OSBYTE call requires multiple R2 handshakes. The Tube host code
polls R2 in a tight loop, but this loop only runs during host ticks. Each
R2 byte transfer that crosses a tick boundary adds up to 5ms of latency.

The measured per-call latency of 50-60ms (10-12 ticks) is consistent with
~6 R2 operations per OSBYTE, each crossing 1-2 tick boundaries on average.

### Confirming evidence

1. **Clock rates are correct** (2.000/3.000 MHz) -- the slowdown is not
   due to incorrect cycle rates.
2. **Parasite PC sampling** shows 91% of time at the R2 status poll loop
   ($FA93-$FA96), confirming the parasite is waiting for the host.
3. **Host PC sampling** shows 18% of time at the Tube main loop
   ($0036-$0044), confirming the host spends most of its time elsewhere
   (sleeping in the pacing clock).
4. **Alternating 5/6 pattern** in per-call measurements is characteristic
   of tick-boundary alignment effects -- not random jitter.
5. **Same code under coupled stepping** (no independent pacing) completes
   in <1 emulated second, confirming pacing is the bottleneck.

### Impact on L3FS

| Metric | Real hardware | Beebium | Ratio |
|--------|---------------|---------|-------|
| OSBYTE latency | ~0.9 ms | 50-60 ms | 55-67x |
| OSBYTE throughput | ~1100/s | 11-32/s | 35-100x |
| WAIT3 duration | 0.36s | ~20-36s | 55-100x |
| PRTIM interval | 30s | 28-51 min | 55-100x |

The L3FS calls OSBYTE 51 approximately 34,000 times between each RTC read
(85 WAIT3 calls * 400 OSBYTE calls each). At the measured rate of
11-32 OSBYTE/s, this takes **17-51 minutes** instead of the expected
30 seconds.

### False leads eliminated

1. **OSBYTE 150 without ANFS**: Early tests using OSBYTE 150 without the
   ANFS ROM showed fast throughput (~3200+ OSBYTE/s). This was because
   **the Tube was not operational** -- without the ANFS ROM, the host never
   loads the Tube host code. The boot message showed "BBC Computer 32K"
   (no Tube) instead of "Acorn TUBE 6502 64K". These measurements were
   invalid.

2. **Coupled stepping artefacts**: Tests using `CoupledSystem` (the Python
   client's coupled stepping mode) showed different behaviour from normal
   pacing. Coupled stepping bypasses the independent pacing entirely,
   running both processors in lock-step via cycle-budget breakpoints. This
   is useful for fast program entry but does not reproduce the production
   pacing behaviour. The final test uses only normal paced execution
   throughout.


## Artefacts Created

### New gRPC API: `AcornRtcService.WatchActivity`

Streaming RPC that emits events when the BBC reads or writes SAF3019P
registers via the CBUS protocol. Analogous to watching the DATA LED on
Ken Lowe's reproduction hardware.

Files modified:
- `src/extensions/acorn-rtc/acorn_rtc.proto` -- added `WatchActivity` RPC
  and `RtcActivityEvent` message
- `src/extensions/acorn-rtc/Saf3019p.hpp` -- added `ActivityCallback` and
  `set_activity_callback()`
- `src/extensions/acorn-rtc/Saf3019p.cpp` -- fire callback on CBUS read/write
  completion (outside mutex lock)
- `src/extensions/acorn-rtc/AcornRtcService.hpp` -- implemented
  `WatchActivity()` with bounded thread-safe event queue
- `clients/beebium-python-client/src/beebium/_proto/acorn_rtc_pb2.py` -- generated Python
  stub
- `clients/beebium-python-client/src/beebium/_proto/acorn_rtc_pb2_grpc.py` -- generated
  Python stub (with fixed import path)

### Tube OSBYTE throughput test

`clients/beebium-python-client/tests/test_tube_osbyte_throughput.py`

Fast (20s), self-contained test that measures per-call OSBYTE 51 latency
under normal paced execution with the Tube active. No coupled stepping, no
disc images, no external dependencies beyond MOS + BASIC + ANFS ROMs.

- Boots Model B ROM/RAM board with Tube + ANFS + Econet
- Verifies "TUBE" in boot message (Tube host code loaded)
- Types a BASIC program that pokes and calls a single-OSBYTE machine code
  loop 32 times, timing each call with BASIC TIME
- Reports min/mean/max latency and wall-clock throughput
- Asserts throughput >500 OSBYTE/s (currently fails at ~11/s)

### L3FS clock update integration test (slow, opt-in)

`integration_tests/l3fs-clock/tests/test_l3fs_clock_update.py`

Boots the full L3FS stack, monitors the `WatchActivity` stream for 3
minutes, groups events into RDDONG calls (6-register read sequences), and
asserts the average interval is ~30 seconds. Currently fails, detecting
only 1 RDDONG call in 180 seconds.

Marked `@pytest.mark.slow` -- excluded from default test runs.

### Diagnostic scripts

All in `integration_tests/l3fs-clock/tests/`:

- `measure_clock_rates.py` -- confirmed host/parasite clock rates and Tube
  transfer throughput
- `measure_r2_per_tick.py` -- detailed R2 throughput vs pacing rate
  analysis
- `measure_parasite_activity.py` -- PC sampling showing 91% time at R2
  spin loop
- `disassemble_hotspots.py` -- memory dumps at hot addresses


## What Needs to Change

The fix must allow Tube R2 handshakes to complete without stalling at
pacing tick boundaries. The host and parasite must be able to exchange R2
bytes within the same wall-clock instant, rather than each waiting for the
other's next tick.

Possible approaches (not yet evaluated):

1. **Wake the host when the parasite writes to a Tube register** -- break
   the host out of its pacing sleep when R2 data arrives, so it can
   process the OSBYTE immediately rather than waiting up to 5ms for its
   next scheduled tick.

2. **Synchronise host and parasite ticks** -- ensure both processors run
   their batches in lock-step rather than independently, so R2 handshakes
   complete within a single combined tick.

3. **Increase pacing frequency** -- higher Hz = smaller ticks = less
   latency per boundary crossing. However, this increases CPU usage and
   may affect audio/video quality.

4. **Run the Tube host polling loop during pacing sleep** -- allow the
   host's Tube polling code to execute even while the pacing clock is
   waiting, so R2 data is consumed promptly.


## Unrelated Observations

### IDLE_COOLDOWN in FourWayHandshake

`FourWayHandshake.hpp:57` defines `IDLE_COOLDOWN = 5000` ticks (~2.5ms)
that blocks all `receive_frame()` polling after a completed Econet
transaction. There is a TODO at line 130 acknowledging this should be
replaced with a buffered approach. This is **not** the cause of the clock
update issue (confirmed empirically: the slowdown affects all OSBYTE calls,
not just Econet polling), but it may affect Econet throughput in other
scenarios.

### Pacing stats now available via gRPC

`SystemService.GetPacingStats` and `WatchPacingStats` expose tick counts,
I/O wakeup counts, and deficit from both host and parasite servers.


## Resolution

The root cause -- independent 200 Hz pacing of host and parasite creating
50-60ms latency on Tube R2 handshakes -- was resolved by replacing the
fixed-frequency pacing with an adaptive sleep quantum approach.

### The fix

Every pacing iteration: run N crystal ticks, then do one minimum-length
OS sleep. N is computed by a deficit controller (`target - actual`,
clamped). The sleep quantum is measured empirically at startup (~100us
on macOS Apple Silicon).

This is a fundamental redesign: instead of fixed-frequency fixed-batch
pacing (200 Hz × 10,000 cycles), the emulation makes smooth, continuous
progress with tiny, frequent batches (~200 cycles every ~100us). The
Tube I/O latency is naturally bounded by the quantum -- no special
wakeup mechanism is needed.

See `docs/pacing.md` for the full design, and
`docs/discussion/pacing-approaches-evaluation.md` for the approaches
tried during development.

### Results

| Metric | Before | After | Real hardware |
|--------|--------|-------|---------------|
| OSBYTE throughput | 11/s | 1280-1347/s | ~1100/s |
| OSBYTE latency | 50-60ms | 0.74-0.78ms | ~0.9ms |
| L3FS clock update | 3-5 min | ~36s | ~30s |
| Clock accuracy | Minutes behind | Current minute | Current minute |

The clock updates every ~36 seconds (vs ~30 on real hardware, 3-5
minutes before the fix). It always shows the correct current time.
The initial display after the L3FS startup questionnaire may show the
previous minute because the RTC was read before the questionnaire, not
at display time. This matches real hardware behaviour.

### Importance of the L3FS as a test case

The Level 3 File Server was the ideal test case because it exercises
every aspect of the multi-process pacing problem simultaneously:

1. **Sustained Tube I/O**: ~34,000 OSBYTE calls per clock update cycle
   (85 WAIT3 iterations × 400 polls each)
2. **Cross-process synchronisation**: all I/O traverses the Tube R2
   protocol between host and parasite processes
3. **Timing-sensitive polling**: the WAIT3 loop is calibrated with
   `ONEMS=5` and `WAITCL=80` for the real Tube's latency
4. **Real-time clock accuracy**: the SAF3019P RTC tracks wall-clock
   time, so any pacing drift shows up as incorrect displayed time
5. **Disc loading**: extended FDC activity stresses clock stretching
   and execution time accounting
6. **Econet**: active network hardware adds interrupt load

The fix that works for the L3FS -- smooth, continuous progress via
adaptive quantum -- works for all Tube software because it addresses
the fundamental pacing architecture, not the specific symptoms.
