# PiEconetBridge: kernel module silently drops Piconet scouts

Investigation log for an unresolved interaction between the Piconet
USB-CDC device and PiEconetBridge (https://github.com/cr12925/PiEconetBridge),
discovered while building Beebium's Piconet hardware contract test
suite. This is **not a Piconet defect** -- the Piconet side has been
verified to do its job correctly. The drop happens after the kernel
module receives our frames but before they reach the bridge's
user-space process.

**Status (October 2026): partly resolved, cause still open.** The
Piconet-to-bridge four-way handshake works against a freshly restarted
bridge (see "Update 2026-04-17" below); what remains unexplained is the
bridge-side wedge that later stops it scout-acking our station, cleared
only by restarting `econet-hpbridge`. The hardware test that depends on it is `Piconet network: TX to the
fileserver completes the wire handshake` in
`tests/test_piconet_hardware_network.cpp` (tag
`[piconet-hardware-network]`, skipped unless `BEEBIUM_PICONET_DEVICE`
and `BEEBIUM_PICONET_NETWORK_AVAILABLE=1` are set), whose comment points here; the `[needs-station-registered]`
tag this document originally named no longer exists. The AUN side of
PiEconetBridge interop, which needs no Pi or kernel module, is covered
separately by the containerised harness
([`pieconetbridge-aun-interop-testing.md`](pieconetbridge-aun-interop-testing.md), #16, #143).

## Setup

- Pi 3 running PiEconetBridge v2.1 (commit 57243a1) on
  `/dev/econet-gpio` (Pi Econet HAT v2 r2a or later).
- Bridge config:
    ```
    WIRE NET 1 ON DEVICE /dev/econet-gpio
    FILESERVER ON 1.254 PATH /home/rjs/econetfs
    EXPOSE HOST 1.254 ON PORT *:32768
    ```
- The same setup is known to work correctly with a real BBC Microcomputer
  on the wire as a client of the fileserver at 1.254.
- Piconet at `/dev/tty.usbmodem101` on the developer's macOS host,
  attached to the same physical Econet wire as the Pi via the standard
  Econet socket on the Piconet board.

## What works

1. **Piconet's CLK LED follows the wire clock**: lights when the
   cable is plugged in, goes out when unplugged. So the Piconet
   correctly synchronises to the wire's clock generator.
2. **Piconet's TX/RX activity LEDs flicker** during Beebium tests --
   physical signaling is happening in both directions.
3. **`econet-monitor` (PiEconetBridge utility) running on the Pi
   sees our wire frames**. With the bridge stopped and
   `econet-monitor` reading `/dev/econet-gpio` directly, sending five
   broadcasts and one scout from the Piconet produced exactly six
   captured packets:

       --- PACKET ---  DST 0xff/0xff  SRC 0x00/0x20   ff ff 20 00
       (six broadcasts)
       --- PACKET ---  DST 0x00/0xfe  SRC 0x00/0x20   fe 00 20 00 80 99
       (one scout to the fileserver)

   The scout has the right addressing (dest 0.254, src 0.32), the
   right ctrl byte (0x80 with the wire's scout high bit set), and the
   right port (0x99). Piconet's wire transmission is correct.

   (Side note: the captured broadcasts show only the 4-byte
   address header, with no ctrl/port/payload bytes. We sent
   broadcasts whose payload was 4-5 bytes. Either `econet-monitor`
   defaults to brief output, or Piconet's BCAST is genuinely
   truncating to 4 bytes on the wire. Worth following up but
   orthogonal to the main issue here.)

## What does not work

When the bridge runs (any flag combination tried, including
`-p iIoO -z -z -z -z -z -e -d /tmp/bridge.log`), Beebium's
`TX to fileserver` test reliably returns `TX_RESULT NO_SCOUT_ACK`.
The bridge's debug log shows only its startup messages and a
periodic pool-collector tick every 60 seconds:

    [+   0.431s] BRIDGE  : Internal  Bridge reset from internal
    [+   0.431s] BRIDGE  : Internal  Networks list reset
    [+   0.431s] BRIDGE  : Internal  Station set reset on wire network 1
    [+   0.431s] BRIDGE  : Internal  Sender net is 0 for Wire device net 1
    [+   0.431s] BRIDGE  : Wire   1  Unable to find sender net. Not sending bridge reset.
    [+   0.431s] DESPATCH: Wire   1  Despatcher thread infinite condwait
    [+  60.029s] POOL    : Pool garbage collector running

Crucially, the kernel module's `irq_read` path -- which would log
`"econet-gpio: econet_irq_read(): AUN: Scout received from %d.%d
with port %02x, ctrl %02x. Acknowledging."` (kernel module
econet-gpio-module.c line 1616, an unconditional `printk`) -- never
fires for our scouts. With kernel printk verbosity maxed out
(`sysctl -w kernel.printk="8 8 8 8"`) and the kernel module's
`extralogs` flag enabled (`-e` passed to the bridge), `dmesg` shows
zero entries during a test run.

## The interpretation (revised after reading PiEconetBridge source)

The original "kernel doesn't see our frames" diagnosis was based on
dmesg silence. **That signal is meaningless for this question.** Source
review of `module/econet-gpio-module.c` shows that EVERY diagnostic
`printk` in the AUN-mode RX classification path (lines 1514-1618 of
econet-gpio-module.c, including the "AUN: Scout received from %d.%d
... Acknowledging." line) is wrapped in `#ifdef ECONET_GPIO_DEBUG_AUN`.
The production kernel module is not compiled with that flag. Setting
`extralogs` (the runtime `-e` flag) does NOT enable these prints --
they're compile-time guarded. dmesg silence during our test means the
debug build wasn't used; nothing more.

A second misleading observation: `econet-monitor` shows our frames,
which we read as "the kernel sees them." But `econet-monitor` (line
195) explicitly turns OFF AUN mode (`ioctl(fd, ECONETGPIO_IOC_AUNMODE,
0)`) before reading. So econet-monitor proves only that **raw mode**
delivers frames -- it tells us nothing about whether the AUN-mode
receive path is processing them.

The bridge's startup message `Sender net is 0 for Wire device net 1.
Unable to find sender net. Not sending bridge reset.` (econet-hpbridge.c
line 1836) is a red herring. It fires when the bridge wants to
broadcast a bridge-reset announcement at startup but has no other
networks to announce; it's benign and unrelated to per-frame routing.

### What the source actually tells us

1. **The kernel itself sends scout-acks** (lines 1652-1666). When
   `aun_mode == 1` and a 6-byte frame arrives whose destination is in
   the station bitmap, the kernel transitions to `EA_R_WRITEFIRSTACK`
   and writes a 4-byte ack with inverted addressing
   (`dst = our_src`, `src = our_dst`) immediately. The user-space
   bridge sees nothing until the scout-ack has been sent and the data
   packet has arrived. So the bridge's user-space log being silent
   during our tests proves nothing about whether scout-acks were
   attempted.

2. **The station bitmap should include `(0, 254)`**.
   `eb_setclr_single_wire_host` (econet-hpbridge.c lines 7357-7398)
   translates `net` to `0` if it matches the wire's configured net
   before setting the bitmap entry. So `FILESERVER ON 1.254` with
   `WIRE NET 1` results in bitmap entry `(0, 254)` being set, and our
   scout to `0.254` should pass the `ECONET_DEV_STATION(0, 254)`
   check at kernel module line 1415.

3. **`stations_initial` is reset on every bridge reset**
   (econet-hpbridge.c line 2088), and the kernel bitmap is
   re-pushed via `ECONETGPIO_IOC_SET_STATIONS`. So the post-restart
   "Station set updated" message twice in dmesg is the bridge
   re-establishing its baseline -- both times push the same baseline
   bitmap including `(0, 254)`.

4. **The frame-length check is satisfied** (kernel module line 1601):
   our scout is exactly 6 bytes (4 address + ctrl + port), so the
   "expecting Scout and this wasn't" path is NOT taken.

5. **The expected ack format matches Piconet's `_wait_ack` precisely**:
   Piconet expects `_wait_ack(254, 0, 32, 0)` -- src=254, src_net=0,
   dst=32, dst_net=0. The kernel constructs exactly that (lines
   1454-1457).

### So why is it failing?

If our scout reaches the kernel's irq_read in AUN mode, AND passes
the station bitmap check, the kernel SHOULD send an ack that
Piconet's `_wait_ack` accepts. Yet we get NO_SCOUT_ACK reliably. The
remaining hypotheses, in order of likelihood:

a. **The kernel never enters its AUN-mode irq_read path for our
   scouts.** Even though `aun_mode == 1` and the bitmap is correct,
   maybe an earlier guard (line 1969 -- the early-out abort during
   reception) is firing. Worth instrumenting.

b. **The kernel sends the ack but the wire transaction times out.**
   The kernel needs to: receive scout IRQ, process state, set up ack,
   switch ADLC to write, send 4 bytes, receive frame-complete IRQ.
   Piconet's `TIMEOUT_WAIT_ACK_MS = 200`. Generous, but if the bridge
   is using all 4 cores on something else, it could miss the deadline.

c. **The kernel's ack goes out but is corrupted at the wire layer**
   for some reason specific to the Piconet's ADLC behaviour. Hard to
   verify without an oscilloscope.

d. **There's a state-machine constraint we haven't found** -- e.g.
   the kernel rejects scouts when it's mid-transaction with another
   peer, or after a previous failed handshake left the state machine
   in an unexpected state. Bridge-restart didn't clear it, so it
   would have to be a per-Piconet quirk.

(a) is the cheapest to investigate -- a one-line patch to the kernel
module to log unconditionally at line 1415 and 1969 would
definitively answer "did the AUN-mode path see this frame at all?".

## Why a real BBC works in the same setup

The user reports that a real BBC Microcomputer at station 221, on
the same wire and bridge config, talks to the fileserver fine.
Differences between the BBC and the Piconet that may matter:

1. The BBC's NFS ROM may issue a Bridge Query at boot and learn the
   wire's actual net number (1), then send subsequent traffic with
   `srcnet=1`. The Piconet firmware always stamps `srcnet=0`
   (econet.c lines 164, 174 -- documented as upstream issue #4 in
   piconet-upstream-issues.md). PiEconetBridge's docs note that BBC
   Bs do NOT do a Bridge Query on reset, but RISC OS or other clients
   may.

2. The BBC may emit additional handshake traffic the bridge looks for
   before adding the station to its set. The Piconet sends only what
   the host asks it to.

3. There may be a kernel-module station-set learning step that's
   triggered by traffic patterns the BBC produces and the Piconet
   doesn't.

## Update 2026-04-17: integration works after bridge restart

When we returned to the rig with a real BBC at station 221 logged into the
fileserver, the picture became clearer:

- **The full four-way handshake against the real fileserver works
  reliably** when the bridge is in a clean state. After
  `sudo systemctl restart econet-hpbridge`, six consecutive isolated
  runs of the `TX to fileserver` test passed (17 assertions each).
- This is the integration-level validation we'd been missing. We now
  know Piconet's `_wait_ack` strict-validation accepts real-bridge
  scout-acks, the data-ack phase completes within timing, and the
  TX_RESULT OK propagates back through PiconetBackend's reader.
- The previous flakiness was bridge-side wedging, not a Beebium or
  Piconet defect. The bridge enters a state (after time or some
  triggering condition we haven't pinned down) where it stops
  scout-acking our station's frames; a restart clears it.
- **TX from Piconet to the BBC at 221 still fails** with NO_SCOUT_ACK
  -- but this is **expected** behaviour, not a bug. A BBC's NFS ROM
  only scout-acks frames addressed to ports it's actively listening
  on. Port 0x99 (the fileserver-request port) is one a BBC SENDS on,
  not a port it listens for. To test BBC-as-peer we would need a
  port the BBC listens on (notify, immediate operations).

The bridge-wedging investigation can be reopened later if it becomes
operationally important, but it no longer blocks Beebium's Piconet
integration story.

## Where to dig next time

When PiEconetBridge investigation resumes, in priority order:

1. **The decisive test: TX from Piconet to the BBC at 221**
   when both are on the wire. The BBC's NFS ROM should scout-ack any
   TX addressed to it -- this bypasses the bridge entirely on the
   response path. If this succeeds, we've definitively proven Piconet
   can complete a four-way handshake against an Acorn-compatible
   peer; the bridge issue becomes pure investigation rather than a
   blocker. If it ALSO fails, we know the issue is electrical or
   wire-protocol-specific to Piconet vs Acorn ADLC, not a bridge
   thing.

2. **Compare BBC vs Piconet traffic with `econet-monitor`** running
   side-by-side. With the bridge stopped and `econet-monitor`
   capturing in raw mode:
   - BBC issues `*I AM 0.254 SYST` (or similar fileserver
     interaction). Capture the scout-ack the bridge sends back to
     the BBC. Note exact byte layout.
   - Piconet sends an equivalent scout. Compare what comes back (or
     doesn't).

3. **Patch the kernel module to log unconditionally** at the AUN-
   mode RX entry points. Two lines (1415 in `econet_irq_read` and
   1969 in `econet_process_rx`) need to be moved out of their
   `#ifdef ECONET_GPIO_DEBUG_AUN` blocks (or have unconditional
   `printk(KERN_INFO ...)` lines added). This will definitively
   tell us whether our scouts reach the AUN-mode path. Alternative:
   rebuild the kernel module with `-DECONET_GPIO_DEBUG_AUN` and
   replace the running module.

4. **Try forcing srcnet=1 on the wire** by patching the Piconet
   firmware temporarily (one-line change to `board/src/econet.c`
   lines 164 and 174 to use `1` instead of the hardcoded `0`).
   If that changes the bridge's behaviour, the bridge has a
   sender-net dependency we missed.

5. **Test against the BBC's recently-used station number (221)**
   for our station instead of 32. Unlikely to matter, but rules
   out per-station bridge state.

6. **Read `econet_irq_write`** (kernel module around 1140-1220) to
   understand whether the kernel's scout-ack TX has any conditional
   guards that could silently drop it (e.g. CTS handling, line-busy
   detection).

## Related upstream-Piconet observation

The captured-broadcast truncation noted above (ff ff 20 00 with no
ctrl/port/payload) is independent of this kernel-module issue and
worth investigating separately. If real, it would mean Piconet's
BCAST drops user-supplied payload on the wire -- a clear firmware
bug. If it's just `econet-monitor` truncating its display by default,
the issue is presentational. To resolve: send a single distinctive
broadcast and trace via the kernel module's RX path (line 1788 area)
to see the on-wire byte count.
