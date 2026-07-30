# Econet Beebium-to-Beebium Communication Investigation

This document records the investigation into getting two Beebium instances
to communicate via AUN (Acorn Universal Networking over UDP) -- specifically,
a Level 3 File Server running on one Beebium instance with a client station
running on another.

Status: **root cause identified**. Three bugs found:

1. **Stale watchdog timer (FIXED):** FourWayHandshake watchdog from
   incoming RX handshake not cancelled on normal completion. Fix
   applied in `handle_tx_final_ack_from_beeb()`.

2. **Inline R3 read stretch loop (ROOT CAUSE, since FIXED):** Fixed by the
   single-process Tube migration. `TubeSocket::read()` no longer stretches at
   all -- reads complete immediately, an empty R3 P-to-H returning stale latch
   data, which is what B2, BeebEm, jsbeeb and B-Em all do. The scenario below
   has not been re-run since, so it is unverified rather than known-good.
   Original diagnosis follows. The
   `TubeSocket::read()` inline stretch loop (TubeSocket.hpp lines
   124-136) ticks the parasite in a tight busy-wait without
   incrementing host `cycle_count` or ticking peripherals. During
   L3FS disc I/O, this loop consumes 753M parasite ticks inside a
   single host cycle, causing the server's effective emulation rate
   to drop to 0.17 MHz. The server cannot complete its reply TX
   before the client's 22-second timeout. Fix requires restructuring
   the read-side stretch to match the write-side stretch (which
   correctly counts cycles and ticks peripherals).

3. **NMI delivery mechanism (NOT THE BUG):** Hypotheses H1-H5
   thoroughly tested and rejected. The INTOFF/INTON mechanism, ADLC
   IRQ, and Tube interactions all work correctly.

See "April 2026 Investigation" section for the complete hypothesis
chain (H1-H12) with test methods and results.


## Test Scenarios

### Scenario 1: Beebium client to BeebEm-hosted L3FS -- WORKS

A Beebium client station connects to a Level 3 File Server running in
BeebEm. `*I AM SYST` and `*CAT` both succeed.

**Client (Beebium):**

```bash
./beebium-model-b-romram \
  --sideways 9:rom:roms/acorn-anfs_4_18.rom \
  --station 221 --aun-port 10221 \
  --aun-map 0.254:127.0.0.1:32768 \
  --machine-name "Station 221" \
  --advertise --port 48876
```

After boot:

```
*I AM SYST
*CAT
```

Both commands succeed, producing expected output.

**Server (BeebEm):**

Model B with 6502 second processor, ANFS 4.18 in slot 9, ADFS 1.30 in
slot 10, using disc image `L3FS-KL.adl`. AUN configuration in
`econet.cfg`:

```
AUNMODE 1
LEARN 0
AUNSTRICT 0
SINGLESOCKET 1
MASSAGENETS 1
0 254 127.0.0.1 32768
0 221 127.0.0.1 10221
```

The L3FS autoboots via `!BOOT` in the `.adl` image.

**Conclusion:** Beebium's client-side Econet stack (AunBackend,
FourWayHandshake, Mc6854, ANFS ROM interaction) is fully functional.


### Scenario 2: Beebium client to Beebium-hosted L3FS (floppy) -- FAILS

Both server and client are Beebium instances. The server uses the same
`L3FS-KL.adl` floppy image that works in BeebEm.

**Server (Beebium):**

```bash
./beebium-model-b-romram \
  --sideways 9:rom:roms/acorn-anfs_4_18.rom \
  --sideways 10:rom:roms/acorn-adfs_1_30.rom \
  --sideways 11:rom:roms/acorn-dfs_2_26.rom \
  --fdc acorn-1770 \
  --floppy 0:tests/assets/scsi/L3FS-KL.adl \
  --station 254 --aun-port 10254 \
  --aun-map 0.221:127.0.0.1:10221 \
  --machine-name "L3FS" \
  --acorn-rtc layout=7bit-year-in-r7 \
  --tube-65c02
```

After boot: `*ADFS` then `CHAIN "GoFS3-125"` to start the L3FS v1.25.

**Client (Beebium):**

```bash
./beebium-model-b \
  --sideways 9:rom:roms/acorn-anfs_4_18.rom \
  --station 221 --aun-port 10221 \
  --aun-map 0.254:127.0.0.1:10254 \
  --machine-name "Station 221"
```

After boot: `*I AM SYST` -- hangs, then "No reply from station 254".

**Test file:** `integration_tests/l3fs/tests/test_l3fs_floppy_econet.py`


### Scenario 3: Beebium client to Beebium-hosted L3FS (SCSI) -- FAILS

Uses a pre-built 10 MB SCSI hard disc image (`tests/assets/scsi/l3fs.dat`)
with FS3v126, ADFS partition, and AFS0 data partition. The server manually
navigates the L3FS startup questionnaire (Number of drives: 1, Command: S,
Stations: 2).

**Test file:** `integration_tests/l3fs/tests/test_l3fs_econet.py`

Same failure mode as Scenario 2.


## Disc Images

| Image | Location | Description |
|-------|----------|-------------|
| `L3FS-KL.adl` | `tests/assets/scsi/L3FS-KL.adl` | 640 KB ADFS floppy from BeebEm. Contains L3FS v0.92, v1.07y, and v1.25py with BASIC launchers (`GoFS3-092`, `GoFS3-107`, `GoFS3-125`). Machine code `!BOOT` file. Works in BeebEm with autoboot. |
| `l3fs.dat` + `.dsc` | `tests/assets/scsi/l3fs.dat` | 10 MB SCSI hard disc image. ADFS partition with FS3v126 binary and `!BOOT` (`*ADFS` / `*RUN $.FS3v126`). AFS0 data partition (disc name "L3DATA") created by oaknut. 296 cylinders, 4 heads, 33 sectors/track. |
| `l3fs-blank-20mb.dat` + `.dsc` | `tests/assets/scsi/l3fs-blank-20mb.dat` | Blank 20 MB ADFS hard disc for WFSINIT testing. |
| `wfsinit.ssd` | `tests/assets/scsi/wfsinit.ssd` | DFS floppy containing tokenised WFSINIT BASIC program. |
| `FS3v126.ssd` | `discs/l3fs/1_26/FS3v126.ssd` (not committed) | DFS floppy with L3FS v1.26 binary. Also at `/Users/rjs/Code/L3V126/FS3v126.ssd`. |


## ROM Configuration

### File server (Model B with ROM/RAM board)

| Slot | ROM | Purpose |
|------|-----|---------|
| 9 | ANFS 4.18 | Tube Host Code (required for 65C02 parasite) + NFS |
| 10 | ADFS 1.30 | SCSI/floppy disc access |
| 11 | DFS 2.26 | Required by 1770 FDC hardware (floppy-based scenarios only) |
| 14 | DFS 2.26 | Auto-loaded by `beebium-model-b-romram` for 1770 FDC. Use `--sideways 14:empty` to suppress if ADFS-only boot is desired |
| 15 | BASIC 2 | Auto-loaded |

### Client (Model B)

| Slot | ROM | Purpose |
|------|-----|---------|
| 9 | ANFS 4.18 | NFS filing system (only filing system needed) |
| 15 | BASIC 2 | Auto-loaded |


## AUN Map Configuration

The AunBackend hardcodes `local_net = 0` (see `ServerMain.hpp` line 1167).
All `--aun-map` entries must use network 0:

```
--aun-map 0.254:127.0.0.1:10254   # correct
--aun-map 1.254:127.0.0.1:10254   # WRONG -- peer lookup fails
```

Port assignment convention: `10000 + station_number`.


## What Works

1. **File server boots and reaches "Starting - Ready"** on both floppy
   and SCSI configurations.

2. **Client boots and shows "Econet Station 221"** with ANFS in slot 9.

3. **AUN packets flow bidirectionally.** Confirmed via `BEEBIUM_AUN_TRACE=1`
   stderr logging in `AunBackend.cpp`. The client sends an Immediate
   (machine peek, type 5) and receives an ImmReply (type 6). The client
   sends a Unicast (type 2, port 0x99 = `*I AM` data) and the server
   sends an Ack (type 3).

4. **The server's FourWayHandshake processes the client's request
   correctly.** The handshake transitions through `ScoutReceived` ->
   `ScoutAckSent` -> `DataReceived` -> Ack sent -> `WaitForIdle` -> `Idle`.

5. **The file server on the Tube parasite processes the request.**
   After the handshake completes, the file server code on the 65C02
   generates a reply. The FourWayHandshake trace shows `FWH TX:
   stage=idle data=6B` followed by `FWH TX->ScoutSent: dest=221.0
   src=254.0 port=144 ctrl=128 timer=5000` -- the file server initiated
   its reply as a new outbound scout on port 0x90 (144 decimal).


## What Fails

The server's reply scout enters `ScoutSent` and arms the handshake timer
at 5000 ticks (~2.5 ms). The timer should fire `on_handshake_timeout()`
which generates a fake scout ack and transitions to `ScoutAckReceived`.
The NFS ROM would then send the data frame, which would be transmitted
as an AUN Unicast to the client.

**The timer never fires.** The trace file shows only 260 more FWH ticks
after the reply scout is sent. The 5000-tick timer needs at least 5000
ticks to fire.


## Timing Analysis

### Corrected interpretation (April 2026)

**The server runs at full 2 MHz emulation speed.** Standalone pacing
reports exactly 2.000 MHz at 100.0% with ~26% CPU utilisation. There is
no slowdown when two instances run simultaneously -- the machine has
ample capacity.

The low Econet tick counts reported below were measured **before** the
stretch fix (see "Bug Fix Applied" section). They do NOT indicate slow
emulation. They indicate that `EconetSocket::tick_rising()` was not
being called during Tube stretch cycles. On a Tube machine, the host
CPU spends a large fraction of its time in Tube stretch (host halted
while waiting for the parasite to drain a Tube register). Before the
fix, the Econet ADLC was not ticked during these stretch cycles, so the
`tick_count_` counter dramatically under-counted actual emulated time.

After the stretch fix, the server accumulates 172M ticks in 86 seconds
of emulated time -- consistent with full 2 MHz operation.

### Pre-stretch-fix tick counts (historical, superseded)

These measurements were taken **before** the stretch fix was applied.
They show the Econet socket's tick starvation, not machine slowdown.

| Metric | Server | Client |
|--------|--------|--------|
| EconetSocket tick delta (60s wall-clock) | ~2,000,000 | ~24,000,000 |
| Effective Econet tick rate | ~33,000/sec | ~400,000/sec |
| Percentage of 2 MHz nominal Econet ticking | 1.7% | 20% |

The server's low Econet tick rate was caused by Tube stretch cycles not
ticking the Econet socket. The client (no Tube) had no stretch cycles,
so its ticks were closer to nominal (still below 100% due to 1 MHz bus
stretch for SHEILA I/O accesses, which also did not tick Econet before
the fix).

### Pre-stretch-fix server tick budget (historical, superseded)

Of the server's ~2,000,000 pre-fix Econet ticks:
- ~2,000,000 were consumed by file server processing on the parasite
- ~260 remained after the reply scout was sent
- The 5000-tick FourWayHandshake timer could not fire in 260 ticks

This was the direct cause of the "scout_sent stuck" symptom observed
before the stretch fix. After the fix, the timer fires correctly.

### Post-stretch-fix tick counts

After the stretch fix, the server accumulates 172M Econet ticks over
86 seconds of emulated time at 2 MHz. This is consistent with full-
speed operation (2M ticks/sec). The 5000-tick scout ack timer fires
correctly.


## ANFS Client-Side Behaviour

The following was established by analysis of the ANFS 4.18 ROM
disassembly.

### `*I AM` command flow

The `*I AM SYST` command executes in two phases:

**Phase 1: Send the command (client-initiated four-way handshake)**

`send_net_packet` at `&983F` handles getting the Econet frame onto the
wire via the ADLC. This phase has retries for ADLC-level failures.

- Retry count: default 255 (from `net_context` at `&0D6D`)
- Per-retry delay: ~61 ms at 2 MHz (24,448 inner-loop iterations at
  5 cycles each)
- Total worst-case Phase 1 time: ~15.6 seconds
- Error on failure: "Line jammed", "Net error", "Not listening" --
  NOT "No reply"

Each retry is a complete from-scratch attempt. The ADLC is fully reset
to RX listen mode after every transmission. `tx_begin` rebuilds the
entire scout+data sequence from the TXCB each time.

Before each TX attempt, the NFS ROM:
1. Polls SR2 bit 2 (INACTIVE) in a tight loop at `&85F4` to confirm
   the wire is idle
2. Writes CR2 = `&67` (clear status), checks CTS (SR1 bit 4)
3. Writes CR2 = `&E7` (asserts RTS -- makes CTS go low)
4. Writes CR1 = `&44` (RX_RESET | TIE -- disables RX, enables TX IRQ)
5. Installs `nmi_tx_data` handler and enables NMI via INTON

The four-way handshake runs entirely via NMI: scout bytes written to
TX FIFO, NMI handler manages the data phase.

On completion (success or failure), `tx_store_result` stores the result
in the TXCB, then `discard_reset_rx` resets the ADLC to idle RX listen
mode: CR1 = `&82` (TX_RESET | RIE), CR2 = `&67`, and reinstalls
`nmi_rx_scout` as the NMI handler.

**Phase 2: Wait for server's reply (polling loop)**

`recv_and_process_reply` calls `init_txcb_bye` to set up an open receive
control block on port `&90` (RXCB ctrl = `&7F`, bit 7 clear = awaiting
reply), then calls `wait_net_tx_ack` at `&95DD`.

`wait_net_tx_ack` is a three-level nested polling loop:

```asm
.loop_poll_tx
    LDA (net_tx_ptr),Y     ; 5 cycles
    BMI done_poll_tx        ; 2 cycles (not taken)
    DEC error_text,X        ; 7 cycles
    BNE loop_poll_tx        ; 3 cycles (taken)
```

- Inner counter: 256 iterations
- Middle counter: 256 iterations
- Outer counter: 40 (from `rx_wait_timeout` at `&0D6E`, configurable
  via OSWORD &13 sub-function 16)
- Total: 256 x 256 x 40 = 2,621,440 iterations
- Total cycles: 44,656,999 cycles = **22.3 seconds at 2 MHz**

The BBC Micro's RAM runs at 4 MHz with CPU and video ULA on alternate
cycles -- there is no contention, so the timeout is the same in all
screen modes.

There is **no retry** of Phase 2. It polls once, then gives up with
"No reply from station".

### Reply mechanism

The reply is a **separate server-initiated transaction**. The complete
sequence:

1. **Client sends command** (client-initiated four-way handshake on
   port `&99`). After success, the ADLC is reset to idle RX listen.
2. **Client sets up open receive** control block on port `&90` with
   ctrl = `&7F` (bit 7 clear).
3. **Client polls** RXCB byte 0 bit 7 in `wait_net_tx_ack` (22.3s
   timeout).
4. **Server processes command** on the parasite (takes ~1 second of
   emulated time for `*I AM`).
5. **Server sends reply** (server-initiated four-way handshake on
   port `&90`).
6. **Client's NMI handler** (`nmi_rx_scout`) receives the incoming
   scout, matches port `&90` to the open RXCB, sends scout ack,
   receives data, sends final ack, and sets RXCB bit 7.
7. **Foreground polling loop** sees bit 7 set, exits successfully.

In AUN terms, this is two completely independent Unicast+Ack exchanges.
The client returns to idle between sending the command and receiving
the reply. The FourWayHandshake's treatment of the reply as a separate
server-initiated transaction is correct.

### Error message summary

| Error | Mechanism | Default timeout |
|-------|-----------|-----------------|
| "No reply from station" | Single polling loop in `wait_net_tx_ack` | ~22.3 s at 2 MHz |
| "Not listening" | 255 TX retries in `send_net_packet` | ~15.6 s at 2 MHz |
| "Line jammed" | Single INACTIVE poll timeout in `tx_begin` | ~1.3 s at 2 MHz |

All three timeouts are pure software busy-wait loops with no hardware
timer reference. They are deterministic at 2 MHz, modulo interrupt load
(100 Hz system timer, keyboard scanning, and sound processing steal
occasional cycles).

The timeout values are configurable via OSWORD &13:
- Sub-function 15 (read): PB[1] = transmit retry count (default &FF),
  PB[2] = receive poll timeout (default &28 = 40), PB[3] = peek retry
  count (default &0A)
- Sub-function 16 (write): sets the same three values


## L3FS Server-Side Behaviour

The following was established by analysis of the L3FS v1.26 disassembly
(Uade*.asm source files).

### Network I/O model

The L3FS uses MOS APIs exclusively for all network I/O. It never touches
the ADLC hardware registers directly:

- **OSWORD &11** (Econet Receive): opens a receive control block on
  port `&99` (command port)
- **OSBYTE 51**: polls receive CB completion status
- **OSWORD &10** (Econet Transmit): sends replies
- **OSBYTE 50**: polls transmit completion
- **OSBYTE 52**: deletes a receive CB

On a Tube system, all of these calls are forwarded via the Tube to the
host I/O processor. The host-side ANFS Tube Host Code services them --
the ANFS code on the host drives the ADLC, handles NMIs, and manages
the four-way handshake. The file server on the parasite is a pure
"userspace" program from the Econet hardware's perspective.

### Main loop

The file server's main loop (`CMND` in `Uade15.asm`) has two nested
tiers:

**Outer loop** (`CPOLXX`, every ~30 seconds):
1. Call `PRTIM` to update the clock display
2. Set `TCOUNT = 85` (iteration counter)
3. Fall into the inner loop

**Inner loop** (`CPOLL0` / `CLPD`):
1. Check `EVCHAR` -- keyboard event flag (M=monitor toggle, Q=quit,
   ESC=change disc)
2. Call `WAIT3` with the current receive CB number and Y=1 (~0.36
   second timeout)
3. If `WAIT3` returns with N flag set (data received), jump to
   `DOCMND` to process the command
4. Otherwise, decrement `TCOUNT`; if zero, loop back to `CPOLXX` for
   the clock update

### WAIT3 -- the polling loop

`WAIT3` (`Uade20.asm`) is a busy-wait loop that repeatedly calls
OSBYTE 51:

```asm
.WAITLB
    LDX RXCBN        ; CB number
    LDA #51
    JSR OSBYTE        ; poll: has this CB received data?
    TXA
    BMI WAITEX        ; X < 0 = data received, exit
    DEC TIMER         ; inner counter (ONEMS = 5 iterations ~ 1 ms)
    BNE WAITLB
    DEC TIMER1        ; middle counter (WAITCL = &50 = 80 ms)
    BNE WAITLA
    DEC TIMER2        ; outer counter (Y on entry)
    BNE WAITLA        ; timeout when all counters exhausted
```

With Y=1 in the main loop, the total timeout is ~0.36 seconds. During
this time, the file server calls OSBYTE 51 continuously (~every 14 us
in the inner loop). Between commands, the file server spends essentially
all its time in this polling loop.

### Reply mechanism

When a `*I AM` request arrives on port `&99`:

1. `DOCMND` (`Uade15.asm:156`) extracts the reply port from byte 0
   of the received data buffer and stores it in `RPLYPT`. The ANFS
   client puts `&90` here.
2. The function code (byte 1) dispatches through the function table
   to `LOGON` (`Uade17.asm:333`).
3. `LOGON` parses the username/password, calls `USRMAN` to register
   the machine. On success, copies CSD/UFD/LIB handles and boot option
   into the transmit buffer.
4. `REPLYC` -> `REPLY` -> `SEND` -> `XMIT` (`Uade20.asm`) builds the
   transmit control block using the reply port from `RPLYPT` and calls
   OSWORD &10 (Econet Transmit).

The file server is port-agnostic for replies -- it echoes back whatever
reply port the client specified in byte 0 of the command packet. The
only hardcoded ports are `&99` for receiving commands and `&9A` for data
during saves.

### Processing time for `*I AM SYST`

The `*I AM SYST` processing path performs 5-8 disc reads (root
directory, password file, user directory, library search) via OSWORD
&72 (disc controller command). Each floppy disc read takes ~20-40 ms
(seek + rotational latency + transfer). With a cold STRMAN cache on
first login after boot:

- CPU work: ~500 us total
- Disc I/O: 100-320 ms (5-8 reads at 20-40 ms each)
- Library search: may scan multiple drives, adding more reads
- **Total: ~300-700 ms is normal for cold-cache first login**

The observed ~2M host ticks (~667 ms at 3 MHz parasite / 2 MHz host)
is plausible and does not indicate the file server is stuck. The
processing is purely sequential with no busy-waits between receive
and transmit (other than disc operations).

SCSI-based scenarios would be faster (no rotational latency in
emulation), but the same code path is followed.

### Implications for the investigation

The complete reply chain for a Beebium-hosted file server is:

1. Client ANFS sends command via ADLC (host-side four-way handshake)
2. Host ANFS Tube Host Code receives the frame, delivers to parasite
   via Tube
3. Parasite file server processes command (~300-700 ms of disc I/O)
4. Parasite file server calls OSWORD &10 (Econet Transmit)
5. Parasite MOS forwards OSWORD &10 via Tube to host
6. **Host ANFS Tube Host Code executes OSWORD &10 on the host side**
7. Host ANFS code builds scout frame, drives ADLC, runs four-way
   handshake via NMI
8. Beebium's FourWayHandshake converts scout+data to AUN Unicast
9. UDP packet sent to client

Step 6 is critical: the host CPU must be running and responsive to
service the Tube OSWORD &10 request. The ANFS Tube Host Code on the
host side performs the same `send_net_packet` -> `tx_begin` -> ADLC
setup -> NMI-driven handshake sequence that a non-Tube client uses
for its own transmissions.

The FourWayHandshake trace confirms step 8 begins (server enters
`ScoutSent`). Before the stretch fix, only ~260 Econet ticks remained
after the scout was sent -- insufficient for the 5000-tick timer. This
was because the Econet socket was not ticked during Tube stretch cycles.
After the stretch fix, the Econet socket receives ticks during stretch
and the timer fires correctly.


## Root Cause

### FourWayHandshake fake scout ack blocked by ADLC RX_RESET

The server's FourWayHandshake correctly generates a fake scout ack
after the `ScoutSent` timeout (5000 ticks). The ack is enqueued in
`rx_queue_`. However, the ADLC's `rx_process_byte()` method never
calls `receive_frame()` because CR1 bit 6 (RX_RESET) is asserted:

```cpp
void rx_process_byte() {
    if (cr1_ & CR1_RX_RESET) return;  // ← blocks all RX processing
    // ...
    auto frame = backend_.receive_frame();  // never reached
```

The NFS ROM writes CR1=&44 (RX_RESET | TIE) when setting up for
transmission (step 4 of `tx_begin` in the ANFS analysis). This
disables the RX section to prevent spurious receive interrupts during
transmission. On real Econet hardware, the scout ack arrives on the
wire AFTER the scout is fully transmitted, at which point the NFS
ROM's NMI handler clears RX_RESET to switch to receive mode. But in
the FourWayHandshake emulation, the fake scout ack is generated by a
timer during the scout TX phase and queued immediately -- before the
NFS ROM has cleared RX_RESET.

**The sequence on the server:**

1. File server on parasite calls OSWORD &10 to send reply
2. Host ANFS Tube Host Code receives the call
3. ANFS `tx_begin` writes CR1=&44 (RX_RESET | TIE)
4. ANFS NMI handler writes scout bytes to ADLC TX FIFO
5. Mc6854 `write_tx_fifo()` → `flush_tx_frame()` → `on_tx_frame_complete()`
   → FourWayHandshake `send_frame()` → `handle_tx_from_idle()`
6. FourWayHandshake saves scout, arms 5000-tick timer, enters `ScoutSent`
7. After 5000 ticks, `on_handshake_timeout()` fires:
   - Enqueues fake scout ack in `rx_queue_`
   - Transitions to `ScoutAckReceived`
8. **ADLC `rx_process_byte()` is called but returns immediately**
   because CR1 & CR1_RX_RESET is still set
9. The fake scout ack sits in `rx_queue_` undelivered
10. NFS ROM never sees the scout ack, never sends the data frame
11. Watchdog fires (500000 ticks), resets handshake to Idle
12. NFS ROM's OSWORD &10 retry loop repeats from step 3

**Evidence:**

Querying the server's Econet status via gRPC long after the client's
"No reply" timeout shows `handshake.stage = "scout_sent"`. The server
has 172M ticks (86 seconds of emulated time at 2MHz) -- far more than
enough for the 5000-tick timer. The handshake is continuously cycling
through ScoutSent → timeout → reset → NFS ROM retry → ScoutSent.

**On real Econet hardware**, the NFS ROM's NMI handler clears RX_RESET
after the last scout byte is transmitted, switching the ADLC to receive
mode to capture the scout ack from the wire. The timing works because
the scout ack arrives on the wire after a propagation delay. In the
FourWayHandshake emulation, the fake ack is generated instantaneously
(by timer) and queued, but the ADLC RX path is still blocked.

**The fix** needs to ensure the fake scout ack can be delivered to the
NFS ROM despite RX_RESET being asserted during the TX phase. Possible
approaches:

1. **Deliver the fake scout ack when RX_RESET is cleared.** The NFS
   ROM's NMI handler will eventually clear RX_RESET to switch to RX
   mode. At that point, the queued frame in `rx_queue_` should be
   delivered. This requires `rx_process_byte()` to check `rx_queue_`
   whenever RX_RESET transitions from set to clear. This matches real
   hardware behaviour: the scout ack is received after the NFS ROM
   switches to RX mode.

2. **Bypass RX_RESET for FourWayHandshake-generated frames.** Treat
   locally-generated fake acks differently from wire-received frames.
   This is less clean architecturally.

3. **Delay the fake scout ack generation until after RX_RESET is
   cleared.** The FourWayHandshake could monitor the ADLC's CR1 state
   and only generate the ack after the NFS ROM switches to RX mode.
   This couples the handshake to ADLC internals.

Approach 1 is the most natural: on real hardware, the scout ack IS
received after the NFS ROM clears RX_RESET. The current implementation
just needs to preserve the queued frame until then.

**Note:** The client-side TX works because the client's NFS ROM sends
the *I AM request from the BASIC foreground (not from Tube Host Code).
The ANFS client code manages the ADLC CR1 state and clears RX_RESET
to receive the scout ack. The timing works because the client's
`send_net_packet` code has explicit ADLC state management. The issue
is specific to the server-side Tube Host Code path where the OSWORD
&10 forwarding creates a timing difference.

### Unit test findings: IRQ fires, but from CTS not TDRA

A unit test (`tests/test_econet_tx_complete_nmi.cpp`) models the ANFS
ROM's TX sequence at the Mc6854 register level:

1. CR2 = &E7 (RTS asserted)
2. CR1 = &44 (RX_RESET | TIE)
3. Write scout bytes to TX FIFO
4. CR2 = &3F (TX_LAST_DATA)

After step 4, CR2 is stored as &0F (auto-clearing bits TX_LAST_DATA
and CLR_RX_ST removed). RTS (bit 7) is NOT in the &3F value, so RTS
is cleared. CTS goes high.

**Result: the IRQ DOES fire**, but from CTS (SR1 bit 4) being set
with TIE enabled -- not from TDRA. The MC6854's IRQ formula is:

    IRQ = (TIE AND (TDRA OR CTS OR TXU))

CTS transitioning high (when RTS is cleared by the CR2=&3F write)
triggers the IRQ via the TIE-gated CTS path. The NFS ROM's
`nmi_tx_complete` handler doesn't distinguish why the NMI fired -- it
simply writes CR1=&82 (clearing RX_RESET). So on real hardware AND in
unit tests, the handler should run.

**This means the ADLC-level IRQ path is NOT the bug.** The IRQ fires
correctly. The NFS ROM's `nmi_tx_complete` should be called, should
write CR1=&82, and the fake scout ack should then be deliverable.

### Remaining investigation

Since the ADLC IRQ fires correctly at the register level, the bug
must be in how the NMI reaches the 6502 in the full emulator context.
Candidates:

1. **NMI gating via INTON/INTOFF flip-flop.** The NFS ROM's `tx_begin`
   disables NMI via INTOFF (reading &FE18) during ADLC setup, then
   re-enables via INTON (reading &FE20) after installing the NMI
   handler. If the INTON/INTOFF state is wrong when `nmi_tx_complete`'s
   NMI should fire, the 6502 won't see it.

2. **NMI edge detection.** The 6502 detects NMI on a falling edge,
   not a level. If the ADLC IRQ was already asserted when INTON
   re-enables NMI, the 6502 may not see a new edge. The transition
   from "NMI masked" to "NMI unmasked with IRQ already high" might
   not produce the falling edge the 6502 needs.

3. **Tube Host Code NMI context.** On the server side, the OSWORD &10
   is forwarded from the parasite via the Tube. The ANFS Tube Host
   Code calls `tx_begin` from service call context. The NMI handler
   chain runs asynchronously after `tx_begin` returns. If the Tube
   Host Code's main loop interferes with NMI delivery (e.g. by
   polling Tube registers that trigger INTOFF), the `nmi_tx_complete`
   NMI might be masked.

4. **Difference between client and server paths.** The client's
   `*I AM` TX works. Both paths use the same `send_net_packet` /
   `tx_begin` / NMI handler chain. The difference is what happens
   AFTER `tx_begin` returns: on the client, `poll_adlc_tx_status`
   polls the TXCB in a tight loop. On the server (Tube OSWORD &10),
   control returns to the Tube host main loop which polls R1/R2 for
   co-processor requests. Both paths should allow NMI to fire, but
   the Tube host polling might interact with NMI delivery differently.

The next debugging step: add a breakpoint or watchpoint on CR1 writes
(&FEA0) in the server emulator to see whether `nmi_tx_complete` ever
writes CR1=&82 during an `*I AM` attempt. If it does, the bug is
downstream (fake scout ack delivery). If it doesn't, the bug is in
NMI delivery to the 6502.


## Bug Fix Applied

### Econet ADLC not ticked during stretch cycles

`Machine::tick_stretch_cycle()` ticks VIAs, video, and the disc
controller during Tube and 1 MHz bus stretch cycles, but did not tick
the Econet socket. The ADLC's byte trickle timer would stall during
stretch, preventing `receive_frame()` from being called.

**Fix:** Added `econet_socket.tick_rising()` / `tick_falling()` to
`tick_stretch_cycle()` in `Machine.hpp`, positioned after VIA ticking
and before the disc controller NMI poll. Uses the same
`HasEconetSocket<MemoryPolicy>` constexpr guard as the normal path.

This fix is correct and necessary, but does not resolve the primary
Beebium-to-Beebium communication failure (which is caused by the
server process not receiving enough ticks overall).


## Diagnostic Instrumentation

### AUN packet trace

Set `BEEBIUM_AUN_TRACE=1` to enable packet-level logging to stderr in
`AunBackend.cpp`:

```
AUN TX: type=5 port=0 ctrl=8 0.221 -> 0.254 data=4B sent=12
AUN RX: type=6 port=0 ctrl=8 0.254 -> 0.221 data=4B
```

Types: 2=Unicast, 3=Ack, 5=Immediate, 6=ImmReply.

### FourWayHandshake trace

Also enabled by `BEEBIUM_AUN_TRACE=1`. Writes to
`/tmp/beebium-fwh-<PID>.log` (one file per process):

```
FWH TX: stage=idle data=6B
FWH ARM: scout_timer=5000 stage=idle fwh_ticks=61983301
FWH TX->ScoutSent: dest=221.0 src=254.0 port=144 ctrl=128 timer=5000 fwh_ticks=61983301
FWH TIMEOUT: stage=scout_sent fwh_ticks=61988301
FWH WATCHDOG: stage=idle fwh_ticks=62483301
```

### EconetSocket tick counter

`EconetSocket::tick_count()` returns the total number of `tick_rising()`
calls since boot. Exposed via `GetEconetStatusResponse.tick_count` in
the gRPC proto and available in Python as `bbc.econet.status.tick_count`.

### FourWayHandshake tick counter

`FourWayHandshake::tick_count_` incremented on every `tick()` call.
Logged in the FWH trace file at key state transitions.


## Open Questions

1. ~~**Why does the server process run at 1.7% of nominal speed when a
   second Beebium process is running?**~~ **RESOLVED**: The server runs
   at full 2 MHz. The 1.7% figure was the Econet socket's tick rate
   before the stretch fix, not the machine's emulation speed. The
   Econet socket was not being ticked during Tube stretch cycles. The
   stretch fix (ticking Econet in `tick_stretch_cycle()`) resolved this.

2. **Why does "No reply" appear after only ~12 seconds of client
   emulated time?** The ANFS `wait_net_tx_ack` timeout is 22.3 seconds
   at 2 MHz (44.6M cycles). The client tick counter shows only ~24M
   ticks (12 seconds at 2 MHz). The tick counter counts only
   `tick_rising()` calls, not CPU cycles; the actual emulated time
   may be closer to the expected 22.3 seconds once 1 MHz bus stretch
   ticks (which were also not counted before the stretch fix) are
   included.

3. ~~**Is there a deadlock, blocked thread, or resource contention that
   specifically affects the server process?**~~ **RESOLVED**: No. The
   server runs at full speed. The apparent slowdown was caused by the
   Econet tick counter not incrementing during Tube stretch, giving the
   false impression of slow execution.

4. ~~**Does the same two-process slowdown occur with simpler workloads?**~~
   **RESOLVED**: There is no two-process slowdown. Both instances run at
   full speed. The apparent slowdown was a measurement artefact from the
   Econet tick counter not being incremented during stretch cycles.


## April 2026 Investigation

### What has been proven by unit tests

1. **ADLC IRQ fires correctly** after TX_LAST_DATA (CR2=&3F). The CTS
   transition triggers the IRQ via the TIE-gated path.
   (`test_econet_tx_complete_nmi.cpp`)

2. **NMI delivery works with INTOFF/INTON**. When the NMI handler reads
   &FE18 (INTOFF) at entry and &FE20 (INTON) before RTI, the
   M6502_SetDeviceNMI edge detection correctly generates new NMI edges.
   The full TX sequence completes: TDRA data NMIs + CTS completion NMI.
   (`test_econet_nmi_delivery.cpp`)

3. **Without INTOFF/INTON, the CTS completion NMI is lost**. The ADLC
   IRQ transitions atomically from TDRA-asserted to CTS-asserted when
   CR2=&3F is written. Without INTOFF/INTON to toggle the NMI enable
   flip-flop, `device_nmi_flags` is never cleared and no new edge is
   detected. (`test_econet_nmi_delivery.cpp`)

4. **ANFS 4.18 uses INTOFF/INTON** on every NMI entry/exit. The shim
   at &0D00 starts with `BIT &FE18` (INTOFF) and nmi_rti at &0D1C has
   `BIT &FE20` (INTON) before RTI. Confirmed by ANFS disassembly.

5. **Econet TX completes with Tube present**. A ModelB with ANFS 4.18
   + 65C02 parasite, typing `*.` after boot, successfully completes the
   NMI-driven TX sequence: `cr1_0x82_write_count = 3`, `frames_sent =
   1`, handshake returns to idle. (`test_econet_tx_with_tube.cpp`)

6. **FourWayHandshake handles RX-then-TX correctly**. A complete RX
   handshake followed by a TX handshake completes successfully at the
   FourWayHandshake level. (`test_four_way_handshake.cpp`)

### Hypotheses tested

#### H1: ADLC IRQ doesn't fire after TX_LAST_DATA

**Status: REJECTED**

The hypothesis was that writing CR2=&3F (TX_LAST_DATA) doesn't trigger
an ADLC IRQ, so `nmi_tx_complete` never runs.

**Test:** `test_econet_tx_complete_nmi.cpp` writes the ANFS ROM's
tx_begin register sequence (CR2=&E7, CR1=&44, scout bytes, CR2=&3F)
to a standalone Mc6854 and checks `irq_output()`.

**Result:** IRQ fires correctly. The CTS transition (RTS cleared by
CR2=&3F) triggers the IRQ via the TIE-gated CTS path. The MC6854
IRQ formula `IRQ = TIE AND (TDRA OR CTS OR TXU)` produces an IRQ
from CTS being set with TIE enabled.

#### H2: NMI edge detection fails without INTOFF/INTON

**Status: CONFIRMED (but ANFS uses INTOFF/INTON, so not the cause)**

The hypothesis was that M6502_SetDeviceNMI's edge detection fails
when the ADLC IRQ transitions atomically from TDRA-asserted to
CTS-asserted (no gap where the IRQ de-asserts), preventing a new
NMI from being generated.

**Test:** `test_econet_nmi_delivery.cpp` runs two variants of an
NMI handler on a real ModelB Machine:
- With INTOFF/INTON: handler reads &FE18 at entry (INTOFF), reads
  &FE20 before RTI (INTON). Full TX sequence completes: 4 NMIs fire
  (2x TDRA data + 1x last-byte + 1x CTS completion). CR1=&82 written.
- Without INTOFF/INTON: TDRA NMIs fire (FIFO fill creates natural
  edges), but the CTS completion NMI is lost. `tx_done` never set.

**Result:** The edge detection issue is real -- without INTOFF/INTON,
the CTS NMI is lost. However, ANFS 4.18 ROM disassembly confirms
that the NMI shim at &0D00 uses `BIT &FE18` (INTOFF) as its first
instruction and `BIT &FE20` (INTON) at &0D1C before RTI. Every NMI
handler invocation toggles the flip-flop, creating a guaranteed
falling edge. So this mechanism works correctly in Beebium.

Assembly source for both variants: `tests/assets/econet/nmi_tx_test.6502`
and `nmi_tx_test_no_intoff.6502` (assembled with beebasm).

#### H3: Tube presence prevents NMI-driven TX from completing

**Status: REJECTED**

The hypothesis was that Tube bus stretch cycles, ROM banking context,
or other Tube-related timing interferes with the NMI sequence.

**Test:** `test_econet_tx_with_tube.cpp` boots a ModelB with ANFS 4.18
+ 65C02 parasite, types `*NET` then `*.` to trigger an Econet TX, and
checks `cr1_0x82_write_count` and `frames_sent`.

**Result:** Both with and without Tube, the TX sequence completes:
`cr1_0x82_write_count = 3`, `frames_sent = 1`, handshake returns to
idle. The Tube does not interfere with NMI delivery.

#### H4: FourWayHandshake fails on RX-then-TX transition

**Status: REJECTED (at unit level)**

The hypothesis was that the FourWayHandshake's state after completing
an RX handshake (WaitForIdle, idle cooldown) prevents the subsequent
TX handshake from completing.

**Test:** `test_four_way_handshake.cpp` "TX scout succeeds immediately
after RX handshake completes" performs a full RX handshake (incoming
Unicast → scout ack → data → final ack → Idle), waits past the idle
cooldown, then performs a TX handshake (scout → fake scout ack → data
→ AUN Unicast → remote ack → final ack → Idle).

**Result:** Both phases complete correctly. The TX handshake succeeds
immediately after the RX handshake.

#### H5: nmi_tx_complete never runs in the live two-process scenario

**Status: REJECTED**

The hypothesis was that some live-scenario interaction prevents
`nmi_tx_complete` from writing CR1=&82.

**Test:** Live integration test (`test_l3fs_floppy_econet.py`) with
`cr1_0x82_write_count` diagnostic counter. Two Beebium processes
(server with Tube + L3FS, client with ANFS) connected via AUN UDP
on loopback.

**Result:** `cr1_0x82_write_count` delta = 6 on the server during
`*I AM SYST`. This indicates ~3 complete TX cycles (each producing
2 writes: one from `nmi_tx_complete`, one from `discard_reset_rx`).
The NMI-driven TX mechanism works correctly in the live scenario.

However, the server's handshake is still stuck at `scout_sent` (a
4th attempt caught mid-flight), and the client receives "No reply".
The TX completion works, but the reply does not reach the client.

Live diagnostic results (April 2026):
```
Server tick rate: 1,000,263 ticks/sec (50.0% of 2 MHz rising edges)
Before *I AM SYST:
  Server: ticks=59,172,316  cr1_82=150  handshake=idle
  Client: ticks=6,160,012   cr1_82=4    handshake=idle
After *I AM SYST:
  Server: ticks=61,241,392  cr1_82=156  handshake=scout_sent  CR1=0x82
  Client: ticks=30,296,851  cr1_82=10   handshake=idle        CR1=0x82
Delta:
  Server Econet ticks: 2,069,076  (~1 second emulated time)
  Client Econet ticks: 24,136,839 (~12 seconds emulated time)
  Server cr1_82 writes: 6
  Client cr1_82 writes: 6
```

Note: the Econet tick delta disparity (server 2M vs client 24M)
does NOT indicate slow emulation. The server runs at full 2 MHz.
The low server tick delta is because the L3FS processing on the
parasite consumes most of the wall-clock time, and the server's
reply TX phase is brief (~1 second of emulated time). The client's
24M ticks represent its ~12-second wait in `wait_net_tx_ack`.

### Current status

The NMI delivery chain, ADLC, INTOFF/INTON mechanism, and Tube
interactions all work correctly. The bug is **downstream** of the
TX completion: the server successfully sends scout frames to the
FourWayHandshake, `nmi_tx_complete` runs and clears RX_RESET, but
the reply never reaches the client.

### Remaining candidates

The bug is in the path from "scout sent to FourWayHandshake" to
"AUN Unicast received by client". Candidates:

1. **FourWayHandshake fake scout ack delivery timing**: The 5000-tick
   scout ack timer may not fire, or the fake scout ack may not be
   delivered to the ADLC despite RX_RESET being cleared. This could
   be a timing issue where the ADLC RX path is re-enabled too early
   (before the timer fires) or too late (after the NFS ROM's internal
   timeout expires for the scout ack wait).

2. **AUN Unicast not sent**: The FourWayHandshake may complete the
   scout/data handshake but the AUN Unicast may not be sent to the
   AunBackend. The `frames_sent` counter (not yet measured in the
   live test) would reveal this.

3. **AUN UDP packet not received by client**: The server's AunBackend
   sends the UDP packet but the client's AunBackend doesn't receive
   it (socket not polled, packet dropped, address mismatch).

4. **Client FourWayHandshake can't process the reply**: The client's
   FourWayHandshake may be in a state that rejects the incoming
   Unicast (not Idle, or cooldown active).

#### H6: Server sends AUN Unicast replies but they don't reach the client

**Status: PARTIALLY CONFIRMED -- server never sends the reply Unicast**

The hypothesis was that the server's AUN Unicast reply is sent but
lost in UDP or rejected by the client.

**Test:** Live integration test with `BEEBIUM_AUN_TRACE=1` to capture
all AUN packets at the UDP level.

**Result:** The server **never sends** an AUN Unicast (type=2) reply.
The complete AUN packet trace shows only 4 packets:
1. Client → Server: Immediate (machine peek, type=5)
2. Server → Client: ImmReply (type=6)
3. Client → Server: Unicast (port &99, *I AM data, type=2)
4. Server → Client: Ack (type=3)

No server→client Unicast (the reply) appears. The server receives
and ACKs the request, but never generates a reply packet.

This means the FourWayHandshake never reaches `DataSent` (which is
where it sends the AUN Unicast to the AunBackend). Despite
`nmi_tx_complete` running 6 times and CR1=&82 being written (clearing
RX_RESET), the fake scout ack is never delivered to the NFS ROM. The
NFS ROM never sends the data frame. The FourWayHandshake stays at
`ScoutSent` indefinitely.

#### H7: FourWayHandshake scout ack timer never fires on the server

**Status: CONFIRMED**

The hypothesis was that the FourWayHandshake's 5000-tick scout ack
timer is armed but never fires for the server's reply TX.

**Test:** Live integration test with additional diagnostic counters
in FourWayHandshake (`scout_ack_generated_count`,
`tx_frames_from_beeb_count`, `unexpected_tx_reset_count`) and in
Mc6854 (`rx_frames_received_count`, `rx_blocked_by_reset_count`).

**Result:**

```
Delta (server, during *I AM SYST):
  cr1_82 writes: 6           -- nmi_tx_complete runs correctly
  rx_frames_received: 3      -- ADLC receives frames (all from client request)
  rx_blocked_by_reset: 27    -- some RX attempts blocked (expected during TX)
  scout_ack_generated: 0     -- TIMER NEVER FIRES
  tx_frames_from_beeb: 4     -- NFS ROM sends 4 frames to FWH
  unexpected_tx_reset: 0     -- no handshake resets from unexpected TX

Delta (client, during *I AM SYST):
  cr1_82 writes: 6
  rx_frames_received: 3
  scout_ack_generated: 1     -- client's scout ack works correctly
  tx_frames_from_beeb: 3
  unexpected_tx_reset: 0
```

The server's FourWayHandshake never generates a fake scout ack
(`scout_ack_generated: 0`). The timer is armed at 5000 ticks by
`arm_scout_timer()` in `handle_tx_from_idle()`, but it never
counts down to zero. Meanwhile, the client's FourWayHandshake
correctly generates 1 scout ack for its `*I AM` request.

The server sends 4 TX frames to the FWH: 2 from the incoming
request handling (scout ack + final ack) and 2 scout frames from
reply TX attempts. No unexpected resets occur. The ADLC receives
3 frames from the backend -- all from the incoming client request
(Immediate + scout + data). Zero fake scout acks are delivered.

No AUN Unicast reply is ever sent by the server. The complete
packet trace shows only 4 AUN packets:
1. Client → Server: Immediate (machine peek)
2. Server → Client: ImmReply
3. Client → Server: Unicast (port &99, *I AM data)
4. Server → Client: Ack

### Narrowed root cause

The bug is in the FourWayHandshake timer mechanism. The
`handshake_timer_` is armed at 5000 by `handle_tx_from_idle()`
but never reaches zero. The `tick()` function that decrements
the timer IS being called (EconetSocket `tick_count` increments
correctly). Two possible causes remain:

1. **`tick()` is called but `handshake_timer_` is zero.** The
   timer might be cleared by an intervening event between
   `arm_scout_timer()` and the 5000th tick. Candidates:
   - `reset_handshake()` sets `handshake_timer_ = 0`, but
     `unexpected_tx_reset_count` is 0, ruling this out.
   - `on_watchdog_timeout()` calls `reset_handshake()`, but the
     watchdog is 500,000 ticks (100x longer than the scout timer).

2. **`handle_tx_from_idle()` is never called.** The server's
   reply scout might not reach `handle_tx_from_idle()` because
   the FWH is not in `Idle` when the scout arrives. Instead, it
   might be in `WaitForIdle` (from the incoming request handshake)
   or another state. The scout would be handled by a different
   case in `send_frame()` that doesn't arm the timer.

   Evidence: `tx_frames_from_beeb: 4` with `unexpected_tx_reset: 0`
   means all 4 frames were handled by explicit cases. The 2 reply
   scouts could have been handled by `ScoutReceived` (mistaken for
   a scout ack) or `DataReceived` (mistaken for a final ack)
   if the FWH was processing a SECOND incoming request from a
   client retry that arrived during `WaitForIdle`.

   However, the AUN trace shows only 1 incoming Unicast from the
   client, so a second incoming request is not the cause. The
   FWH state at the time the reply scout arrives needs further
   investigation.

#### H8: Stale watchdog timer from incoming RX handshake resets reply TX

**Status: CONFIRMED -- this is the root cause**

The hypothesis was that the watchdog timer from the incoming request
handshake survives the handshake completion and fires during the
server's reply TX, resetting the scout ack timer.

**Test:** Live integration test with `watchdog_timeout_count`,
`tx_from_idle_count`, and `max_handshake_timer_seen` diagnostics.

**Result:**
```
Server tx_from_idle: 1           -- reply scout reaches handle_tx_from_idle
Server max_handshake_timer: 5000 -- timer IS armed correctly
Server scout_ack_generated: 0   -- but never fires
Server watchdog_timeouts: 1     -- WATCHDOG FIRES during *I AM
```

The incoming RX handshake (client's *I AM request) arms the watchdog
at 500,000 ticks in `handle_rx_in_idle()`. The handshake completes
normally via `handle_tx_final_ack_from_beeb()`, which sets
`stage_ = WaitForIdle` but does NOT cancel `watchdog_timer_`.

The L3FS server processes the request on the parasite (~300-700ms
of disc I/O). During this time, the watchdog counts down. After
250ms (500,000 ticks), `on_watchdog_timeout()` fires, calling
`reset_handshake()` which clears ALL timers including any scout
ack timer from a reply TX that may have started.

If the server's reply TX starts BEFORE the watchdog fires (i.e.,
L3FS processing takes less than 250ms), the reply's scout ack
timer is killed by the stale watchdog. If it starts AFTER, the
reply works (but this race is lost in practice because L3FS
cold-cache processing on floppy takes ~300-700ms, and the
watchdog fires at 250ms -- before the reply starts but within the
same emulated-time window).

**The fix:** Cancel `watchdog_timer_` when the handshake completes
normally in `handle_tx_final_ack_from_beeb()` (RX completion path)
and in `on_handshake_timeout()` for the `DataSent` case (TX
completion path). The watchdog is only needed as a safety net for
hung transactions, not for normally-completing ones.

**Fix applied:** `watchdog_timer_ = 0` added to
`handle_tx_final_ack_from_beeb()` and the `DataSent` AUN Ack
handler. This eliminates the stale watchdog (`watchdog_timeouts`
drops from 1 to 0). However, `scout_ack_generated` remains 0
because of a second issue (see H9).

#### H9: Server reply TX starts too late for scout ack timer to fire

**Status: UNDER INVESTIGATION**

After fixing the stale watchdog (H8), the scout ack timer still
does not fire. New diagnostic data:

```
Server ticks_with_timer_active: 5278  (lifetime total)
Server scout_ack_generated: 0
```

The 5278 lifetime ticks with the timer active break down as:
- 5000 from the incoming request's data delivery timer
  (ScoutAckSent → on_handshake_timeout at 5000 ticks)
- 278 from the reply TX's scout ack timer

Only 278 of the required 5000 ticks elapsed on the reply TX's
timer before the gRPC diagnostic snapshot was taken. The reply
TX starts very late in the observation window.

The server's Econet tick delta is ~2M vs the client's ~24M over
the same wall-clock period. Despite both machines running at 2 MHz,
the server's `tick_rising()` count advances ~12x slower than the
client's. This is the same disparity noted in the pre-stretch-fix
timing analysis, but it persists AFTER the stretch fix.

#### H10: Server emulation rate drops during heavy Tube+disc workload

**Status: CONFIRMED -- this is the primary remaining issue**

The hypothesis was that the server's emulation thread genuinely
slows down during L3FS processing, not because of a measurement
artefact but because the combined host+parasite+disc workload
exceeds the pacing system's capacity.

**Test:** Wall-clock timestamps added to the integration test
diagnostic, measuring actual tick rates against `time.monotonic()`.

**Result:**
```
Wall-clock elapsed: 24.12 seconds

Idle measurement (5-second window before *I AM SYST):
  Server tick rate: 1,000,123 ticks/sec (100% of nominal)

During *I AM SYST (24.12 seconds wall-clock):
  Server tick rate: 85,333 ticks/sec (8.5% of nominal)
  Client tick rate: 999,974 ticks/sec (100.0% of nominal)
  Server emulated time: 2.058 sec
  Client emulated time: 24.120 sec
```

The server runs at full 2 MHz when idle. But during L3FS
processing on the parasite (OSWORD &72 disc operations via Tube),
the effective emulation rate drops to 8.5% of nominal -- a 12x
slowdown. Over 24 seconds of wall-clock time, the server advances
only 2 seconds of emulated time.

The client runs at exactly 100% throughout (no Tube, no disc I/O).

This is consistent with all prior observations:
- The Beebium client works against a BeebEm-hosted L3FS server
  (BeebEm runs independently, no shared-thread slowdown)
- The server's reply TX starts very late (only 278 of 5000
  scout ack timer ticks elapsed before the client times out)
- The `*.` command in unit tests works (no disc I/O, no heavy
  parasite workload)

**Root cause:** The host and parasite share a single emulation
thread (Machine::step() ticks both via TubeSocket). The pacing
system maintains 2 MHz average during idle periods, but during
heavy Tube+disc workload the combined per-quantum cost exceeds
the real-time budget. The pacing quantum is ~127 us / ~254
cycles. When the parasite executes disc controller commands
(OSWORD &72 involves multiple SCSI/floppy operations, each
requiring many parasite cycles forwarded through the Tube), the
per-cycle emulation cost increases, consuming the quantum budget
faster and producing fewer host cycles per wall-clock second.

The 12x slowdown means the server needs ~12x longer wall-clock
time to process the L3FS request. The client's 22-second reply
timeout (in emulated time) corresponds to ~22 seconds of wall-
clock time (client runs at 100%). But the server needs ~12 *
(L3FS processing time) wall-clock seconds to complete. With
300-700ms of L3FS emulated processing time, the server needs
3.6-8.4 seconds of wall-clock time. Adding the reply TX time
(scout ack timer = 5000 ticks = 2.5ms emulated = ~30ms wall-
clock at 8.5% rate), the total is well within the client's
22-second wall-clock budget. But the reply TX starts at emulated
second ~2.0 of the server's time, which corresponds to wall-
clock second ~23.5 (2.0 / 0.085). This is AFTER the client's
22-second timeout.

**Implications for fix:** There are several possible approaches:

1. **Optimise parasite emulation throughput.** Reduce the per-
   cycle cost of Tube+disc operations so the pacing system can
   maintain 2 MHz. This is the ideal fix but may require
   significant profiling and optimisation.

2. **Decouple host and parasite threads.** Run the parasite on
   a separate thread so heavy parasite workload doesn't slow
   the host's 2 MHz progress. This is architecturally complex
   (the current single-threaded Tube model was chosen for
   simplicity and determinism).

3. **Increase the client's reply timeout.** The ANFS ROM's
   `wait_net_tx_ack` timeout is configurable via OSWORD &13.
   Increasing it would give the server more wall-clock time.
   This is a workaround, not a fix.

4. **Rate-match the emulation.** Slow the client to match the
   server's effective rate during Econet transactions. This
   would require cross-process coordination.

5. **Pre-empt the pacing quantum for Econet operations.** When
   the server's FourWayHandshake is in ScoutSent (waiting for
   the timer to fire), prioritise host-side ticking over
   parasite processing to ensure the timer fires promptly.
   This is a targeted optimisation for the Econet use case.

#### H11: Cycle count confirms genuine emulation slowdown

**Status: CONFIRMED**

Wall-clock measurement with host CPU `cycle_count` delta:
```
Wall-clock elapsed: 24.12 seconds
Server cycle delta: 4,131,386 (0.17 MHz)
Client cycle delta: 48,240,871 (2.00 MHz)
Server ticks/cycle ratio: 0.4999 (expect ~0.5)
Client ticks/cycle ratio: 0.5000 (expect ~0.5)
```

The server genuinely executes only 4.1M host cycles in 24 seconds
(0.17 MHz). The Econet tick count is exactly half the cycle count
(0.4999 ratio), confirming `tick_rising()` is called correctly on
every rising-edge cycle. The bottleneck is NOT in the Econet
ticking -- it's in the emulation loop itself.

The server's `Machine::step()` is called only 4.1M times in 24
seconds. Each `step()` call takes ~5.8 μs on average, vs the
500 ns budget at 2 MHz. The per-step cost is ~12x over budget.

**Likely cause: pulse-level WD1770 disc controller.** The WD1770
emulation is pulse-driven (`WD1770.hpp`, 1258 lines), processing
individual FM/MFM-encoded bytes from the drive's pulse stream. It
scans for address marks and matches sector IDs at the pulse level.
Each 1 MHz WD1770 tick during active disc operations involves
FM/MFM decoding, address mark scanning, and CRC calculation. This
is ticked from `poll_nmi()` which runs at 1 MHz, including during
Tube stretch cycles via `tick_stretch_cycle()`.

Combined with the parasite's per-cycle ticking (1.5 parasite ticks
per host cycle at 3:2 ratio), the total per-cycle cost during
disc operations exceeds the 2 MHz budget.

Note: BeebEm uses a sector-level disc emulation (reads whole
sectors at once) rather than pulse-level. This was initially
suspected as the cause, but H12 identified the real bottleneck.

#### H12: Inline R3 read stretch loop is the performance bottleneck

**Status: CONFIRMED -- this is the root cause of the slowdown**

The hypothesis was that the `TubeSocket::read()` inline stretch
loop (lines 124-136 of `TubeSocket.hpp`) consumes wall-clock time
without advancing the host cycle counter or ticking peripherals.

**Test:** Added `read_stretch_parasite_ticks` counter to
`TubeSocket::read()` inline loop. Measured via gRPC in the live
integration test.

**Result:**
```
Server read_stretch_parasite_ticks: 753,718,510
Client read_stretch_parasite_ticks: 0
Server cycle delta: 4,119,273 (0.17 MHz)
Client cycle delta: 48,271,527 (2.00 MHz)
```

The server's inline R3 read stretch loop consumes **753 million
parasite ticks** -- 183x more work than the 4.1 million host
cycles that are visible to the pacing system. These parasite
ticks happen INSIDE a single host CPU memory read callback,
without:
- Incrementing `cycle_count`
- Ticking VIAs, video, sound, or Econet
- Being visible to the pacing deficit controller

From the pacing system's perspective, a single `step()` call
takes enormous wall-clock time (the inline loop blocks) while
producing only 1 host cycle. The deficit controller sees the
emulation falling behind and can never catch up, producing the
observed 0.17 MHz effective rate.

The client has zero read stretch ticks (no Tube).

**The code (TubeSocket.hpp lines 124-136):**
```cpp
while (backend->stretched()) {
    if (parasite_ && !parasite_->is_paused()) {
        parasite_->tick();     // <-- 753M ticks here
    } else {
        break;
    }
    auto* ula = tube_ula();
    if (ula && ula->try_complete_stretch()) {
        result = backend->host_read(static_cast<uint8_t>(offset));
        if (!backend->stretched()) break;
    }
}
```

This loop ticks the parasite in a tight busy-wait, hoping it will
write to R3 P-to-H to resolve the stretch. But if the parasite
is executing a long code sequence (e.g., disc I/O processing via
the host's WD1770, which itself takes many host+parasite cycles),
the loop spins for millions of iterations.

**Why this is fundamentally wrong:** On real hardware, the Tube
ULA stretches the host CPU clock by holding PHI2. During stretch,
the host clock doesn't advance, but time still passes at the
host's normal rate. All peripherals that derive their timing from
the host clock (VIAs, video, sound, Econet) are stretched equally.
The emulation should model this by counting stretch cycles as host
cycles and ticking all peripherals -- exactly as the write-side
stretch (`tube_stretch_active_` in `Machine::step()`) already does.

The write-side stretch is correctly implemented: each stretch
cycle increments `cycle_count`, calls `tick_stretch_cycle()` (which
ticks all peripherals), and is visible to the pacing system. The
read-side stretch needs the same treatment.

**Fix approach:** Replace the inline tight loop in
`TubeSocket::read()` with a deferred-read mechanism similar to the
write-side stretch. When R3 P-to-H is empty and stretch is
triggered, set a flag and return a placeholder. Machine::step()
checks the flag and enters a stretch loop (identical to the
write-side `tube_stretch_active_` path) until the parasite fills
R3. The deferred read is then completed. This ensures host cycles
are counted and peripherals are ticked during R3 read stretch.

The comment in the existing code acknowledges this challenge:
"Deferred reads don't work because the CPU would consume the
placeholder return value before the stretch resolves." This needs
to be solved -- possibly by having the stretch handler retry the
read after the stretch clears, before the CPU processes the result.

### Diagnostic tools

- `cr1_0x82_write_count` counter in Mc6854, exposed via EconetSocket,
  gRPC (`GetEconetStatus`), and Python client. Counts writes of
  CR1=&82 (nmi_tx_complete and discard_reset_rx).
- `BEEBIUM_AUN_TRACE=1` environment variable enables AUN packet
  logging to stderr and FourWayHandshake trace to
  `/tmp/beebium-fwh-<PID>.log`.
- `tick_count` in EconetSocket, exposed via gRPC. Counts
  `tick_rising()` calls (half of CPU cycles, including stretch).


## Proposed fix: deferred R3 read stretch

The `TubeSocket::read()` inline stretch loop must be replaced with
a deferred mechanism that works like the write-side stretch.

**Current write-side stretch (works correctly):**
1. Host writes to full Tube register
2. `TubeUla::host_write()` sets `host_stretched_ = true`, stores
   pending offset and value
3. `Machine::step()` detects `tube_stretched()`, enters stretch loop
4. Each stretch cycle: `tick_parasite_stretch()` + `tick_stretch_cycle()`
   (ticks all peripherals) + `++cycle_count`
5. `try_complete_tube_stretch()` retries the deferred write
6. When register drains, write completes, stretch ends

**Proposed read-side stretch:**
The challenge is that the 6502 library's read callback must return a
byte synchronously. Options:

1. **Modify the 6502 library to support stalled reads.** Add a "not
   ready" return status. When the CPU's read callback returns "stalled",
   the CPU does not advance its internal state and retries the same
   read on the next `tick()`. `Machine::step()` enters the stretch
   path, ticking peripherals normally. When the parasite fills R3,
   the retry succeeds and the CPU continues. This is the cleanest
   approach and matches real hardware behaviour exactly.

2. **Pre-check and pre-stretch.** Before the CPU tick, check if the
   next memory access will hit an empty R3. If so, enter the stretch
   loop before the CPU reads. This is fragile because it requires
   predicting the CPU's next address, and the 6502 library doesn't
   expose this reliably before the read callback fires.

3. **Post-read correction.** Let the read return a placeholder,
   enter the stretch path, and when the stretch resolves, patch the
   CPU's register state with the correct value. This is error-prone
   because the CPU may have already used the placeholder in its
   internal pipeline.

Option 1 is recommended. The 6502 library is local code at `src/6502/`
and has been modified before.


## Files modified in this investigation

### Bug fixes (keep)

| File | Change |
|------|--------|
| `src/core/include/beebium/Machine.hpp` | Econet ticking in `tick_stretch_cycle()` |
| `src/core/include/beebium/econet/FourWayHandshake.hpp` | `watchdog_timer_ = 0` in completion paths |

### Tests (keep)

| File | Change |
|------|--------|
| `tests/test_econet_tx_complete_nmi.cpp` | ADLC TX_LAST_DATA -> IRQ sequence |
| `tests/test_econet_nmi_delivery.cpp` | NMI delivery chain (INTOFF/INTON, full Machine) |
| `tests/test_econet_tx_with_tube.cpp` | TX with/without Tube (real ANFS ROM boot) |
| `tests/test_four_way_handshake.cpp` | Added watchdog race tests (RX-then-TX) |
| `tests/assets/econet/nmi_tx_test.6502` | beebasm source for NMI test (with INTOFF/INTON) |
| `tests/assets/econet/nmi_tx_test_no_intoff.6502` | beebasm source (without INTOFF/INTON) |
| `tests/CMakeLists.txt` | Added test targets |
| `integration_tests/l3fs/tests/test_l3fs_floppy_econet.py` | Live diagnostic instrumentation |

### Diagnostic instrumentation (temporary -- remove after fix)

| File | Change |
|------|--------|
| `src/core/include/beebium/econet/Mc6854.hpp` | `cr1_0x82_write_count`, `rx_frames_received_count`, `rx_blocked_by_reset_count` |
| `src/core/include/beebium/econet/FourWayHandshake.hpp` | `scout_ack_generated_count`, `tx_frames_from_beeb_count`, `unexpected_tx_reset_count`, `tx_from_idle_count`, `max_handshake_timer_seen`, `watchdog_timeout_count`, `send_stage_log`, `ticks_with_timer_active` |
| `src/core/include/beebium/econet/EconetSocket.hpp` | Forwarding accessors for above counters, `send_stage_log_string()` |
| `src/core/include/beebium/tube/TubeSocket.hpp` | `read_stretch_parasite_ticks` counter |
| `src/service/proto/econet.proto` | Diagnostic fields 13-24 in `GetEconetStatusResponse` |
| `src/service/include/beebium/service/EconetService.hpp` | Populate diagnostic fields |
| `clients/beebium-python-client/src/beebium/econet.py` | Diagnostic fields in `EconetStatus` |
| `clients/beebium-python-client/src/beebium/_proto/econet_pb2.py` | Regenerated proto stubs |
| `clients/beebium-python-client/src/beebium/_proto/econet_pb2_grpc.py` | Regenerated + relative import fix |

### Pre-existing (unchanged by this investigation)

| File | Change |
|------|--------|
| `src/core/src/econet/AunBackend.cpp` | `BEEBIUM_AUN_TRACE` stderr logging |
| `src/core/include/beebium/econet/EconetSocket.hpp` | `tick_count_` counter |
| `tests/assets/scsi/L3FS-KL.adl` | BeebEm L3FS floppy image |
| `tests/assets/scsi/l3fs.dat` + `.dsc` | SCSI L3FS image from oaknut |


## References

- `docs/level-3-file-server-setup.md` -- L3FS hardware configuration
  and disc provisioning
- `docs/local-beebem-econet-lessons.md` -- BeebEm Econet architecture
  comparison (section 3b: emulated time vs wall-clock time)
- `docs/networking.md` -- Econet/AUN protocol and hardware documentation
- `docs/econet-integration.md` -- gRPC integration work programme
- `tests/test_econet_fileserver.cpp` -- C++ file server tests (uses
  external BeebEm server)
- `tests/test_boot_econet.cpp` -- Econet boot tests including "NFS *.
  with no server triggers network activity"
