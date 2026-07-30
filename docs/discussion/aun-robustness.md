# AUN robustness: defects in the transport and handshake layers

An audit of the Econet transport stack against the question "what does the
guest see when the network misbehaves?" Eight defects, in descending order of
severity. Five are correctness bugs with user-visible consequences; three are
documentation or hygiene.

**All eight are fixed.** A ninth, found by the perturbation proxy once it
existed, is open — see "9. A transaction stalls when acks are reordered" below.

 1 (packet destruction), 2 (transmit failures reported
as success), 3 (cable simulation), 4 (blocking socket), 5 (frame-level event
streaming), 6 (documentation drift), 7 (teardown order), 8 (inbound net
translation).

Fixing 2 also turned up and fixed two ADLC defects it was resting on: Rx Idle
never latching or reaching S2RQ, and the handshake reporting the line inactive
mid-transaction where a real wire holds flag fill throughout.

Defects 1-7 were found by reading. Defect 8 was found by the first test of the
PiEconetBridge interop harness, within an hour of that harness existing, and is
the only one so far confirmed against a real peer.

**Numbering.** The number in each heading is a stable identifier, assigned in
the order defects were found and never reused or reassigned. The *order* of the
sections is severity. The two therefore do not agree, and are not meant to:
renumbering on each new finding would silently invalidate every reference from
a test, a commit message, or an `xfail` reason.

The unifying theme of the first two is that `FourWayHandshake` is optimistic:
it assumes the transaction it is bridging will succeed, and it has no memory of
anything that does not fit the transaction currently in flight. Both
assumptions hold on a quiet loopback link between two emulators. Neither holds
on a loaded LAN with a bridge, a fileserver, and a third station on it. Both
halves are now fixed.

---

## 1. Out-of-order and interleaved packets are destroyed

**Status:** FIXED. **Severity:** high — silent data loss.

`FourWayHandshake::receive_frame()` takes exactly one datagram from the backend
and offers it to `handle_incoming()`. If the packet does not match what the
current stage expects, `handle_incoming` returns `false` and the frame is
destroyed: it has already been read off the socket, and there is no holding
queue anywhere in the chain (`FourWayHandshake.hpp:410-460`).

Every stage other than `Idle` accepts exactly one packet type:

| Stage | Accepts | Everything else |
|---|---|---|
| `Idle` | Unicast, Broadcast, Immediate | destroyed |
| `WaitForIdle` (queue drained) | as `Idle` | destroyed |
| `DataSent` | `Ack` | destroyed |
| `ImmediateSent` | `ImmReply` | destroyed |
| `ScoutSent`, `ScoutAckReceived`, `ScoutReceived`, `ScoutAckSent`, `DataReceived`, `ImmediateReceived` | nothing | destroyed |

UDP does not guarantee ordering, and nothing constrains a peer to speak only
when we are listening. Three consequences follow directly.

**A fileserver reply that overtakes its own ack is lost.** A bridge or
fileserver typically acks our data frame and then sends its reply as a fresh
transaction. Those are two datagrams; under load or across a routed path they
can arrive reversed. We are in `DataSent` when the reply Unicast arrives, so
the reply is destroyed. The ack then arrives, the handshake completes
successfully, and NFS waits for a response that no longer exists until its
OSWORD timeout expires. To the user this is a login or a `*CAT` that hangs and
then fails, intermittently, under load only.

**An inbound immediate operation during a transaction is lost.** A peer
probing us with MachinePeek while we happen to be mid-transaction gets no
reply, because the request never reaches the guest. `*STATIONS`-style
enumeration from a peer is therefore unreliable in a way that correlates with
how busy we are.

**Traffic from a third station is lost.** Any station on the segment that
addresses us during our transaction has its frame destroyed. On a segment with
more than two participants this scales badly: the busier we are, the more of
everyone else's traffic we discard.

The behaviour was asserted as correct: a test named *"unexpected AUN packet
type in DataSent is ignored"* injected a Unicast while in `DataSent` and
asserted that `receive_frame()` returned nothing and no state changed — which
is precisely the overtaking-reply case above. It has been renamed to
*"... is held, not dropped"* and now additionally asserts the frame was
retained.

We were partly shielded from the worst form of this by defect 2: because the
final ack is synthesised unconditionally, a destroyed `Ack` could not deadlock
the state machine, and the 250ms watchdog reset anything that did get stuck.
That was luck rather than design, and it converted a hang into silent loss.
Note the dependency runs the wrong way round: fixing defect 2 without this one
would have turned some of that silent loss back into hangs.

**Fix applied.** `FourWayHandshake` now holds packets the current stage
cannot use, in a bounded queue, and re-offers them oldest-first before taking
anything new off the backend. `HELD_FRAME_TTL` (~1s) discards anything nobody
becomes ready for — generous next to a handshake, short of the multi-second
timeouts NFS applies to an OSWORD, so a held frame is either redelivered while
it still means something or dropped before it can surface wildly out of
context. `MAX_HELD_FRAMES` (32) caps the queue, dropping oldest-first.

One refinement from the original sketch was **not** taken. The intention was to
deliver broadcasts and inbound immediates from any stage, on the grounds that
they need no handshake context. In the code they do: `handle_rx_in_idle`
transitions to `ImmediateReceived` for an immediate and clobbers the saved
addressing, and even a broadcast — which changes no stage — would interleave a
frame into `rx_queue_` in the middle of a scout/data sequence the guest is
part-way through reading. Both are held and re-offered like everything else.
That is a little less prompt and considerably safer.

Replay stops at the first accepted packet, because accepting one can change the
stage and the rest must be re-evaluated against the new stage rather than the
one just left.

Four counters — held, redelivered, expired, dropped — are exposed for tests to
assert on the mechanism rather than only the outcome. They are not yet reachable
from a client; that arrives with defect 5.

**Tests.** Six cases tagged `[holding]` in `tests/test_four_way_handshake.cpp`
covering the overtaking reply, a third station's traffic, a broadcast, TTL
expiry, the bound, and reset. Against a real bridge,
`integration_tests/pieb-aun/tests/test_repeated_login.py` loops login /
`*CAT` / `*BYE`; it is a smoke test rather than a proof until the counters are
reachable.

## 8. `--aun net=N` is unusable for any non-zero N

**Status:** FIXED. **Severity:** high — the feature did not work at all.

`AunBackend::receive_frame` presents every inbound frame to the guest with
`dest_net` set to our own `local_net_` (`AunBackend.cpp:297`):

```cpp
result.frame.src_net  = (peer_net == local_net_) ? 0 : peer_net;   // correct
result.frame.dest_net = local_net_;                                // wrong
result.frame.dest_stn = local_stn_;
```

The `src_net` line is right: a peer on our own segment is presented as net 0,
because that is what "this segment" means from the guest's point of view. The
`dest_net` line then contradicts it. The guest believes it is station N on net
0 — that is the only thing a BBC can believe, since the wire protocol has no
way to tell it otherwise — so a frame addressed to `local_net_.N` is not
addressed to it, and NFS discards it.

The in-code comment rationalises the current behaviour as matching "the form an
outgoing frame would have used", but an outgoing frame from the guest uses
`dest_net=0` for a local destination, which is exactly what this line fails to
produce.

**Observed.** Against a PiEconetBridge fileserver at 1.254, with Beebium
declaring `--aun net=2`, `*I AM 1.254 SYST` produces:

- the immediate MachinePeek exchange completes normally;
- our command datagram is sent and the bridge acks it;
- the bridge's fileserver **processes the login successfully** — its log shows
  user handles allocated;
- the reply arrives at Beebium five times, retransmitted, and is never
  acknowledged;
- the guest reports `No reply from station 1.254`.

The identical scenario with `--aun net=0` completes cleanly, every reply
acked, through login and `*CAT`. The two runs differ in nothing else.

**Why it survived this long.** Beebium-to-Beebium testing has only ever used
the flat net-0 topology, where `local_net_` is 0 and the wrong line is
accidentally right. `net=` was added with the mDNS work specifically so Beebium
could join multi-net deployments, and its inbound path has never carried
traffic from a peer that numbers its nets.

**Fix applied.** `dest_net` is now always 0, mirroring the `src_net`
treatment. An inbound frame is by definition addressed to this station, and
this station is always on the guest's own segment.

Two existing unit tests asserted the old behaviour (`AunBackend: BBC dest_net=0
routes via local_net for peer lookup` and `AunBackend: cross-net unicast
preserves source net`, both checking `dest_net == local_net_`) and were
inverted. A new `AunBackend: inbound dest_net is 0 for every local_net` covers
nets 0, 1, 3 and 127 at the backend seam, where no bridge is needed; the
interop test covers it against a real peer.

**Tests.** `tests/test_aun_backend.cpp` and
`integration_tests/pieb-aun/tests/test_login.py::
test_login_with_matching_non_zero_net_declaration`, both passing.

## 2. Transmit failures are reported to the guest as success

**Status:** FIXED. **Severity:** high — the guest was actively misinformed.

`handle_tx_data_after_scout_ack()` arms `FINAL_ACK_TIMEOUT`
(`FourWayHandshake.hpp:358`). When that timer fires in `DataSent`, the code
synthesises a final ack unconditionally (`:569-586`). Nothing upstream can
suppress it, so every failure mode below terminates in the guest being told the
transmission succeeded:

- **AUN, unmapped peer.** `AunBackend::send_frame` cannot resolve the
  destination and returns without sending (`AunBackend.cpp:192-197`).
- **AUN, negative acknowledgement.** A `Nack` from the peer arrives in
  `DataSent`, does not match `Ack`, and is destroyed by defect 1.
- **Piconet, wire-level failure.** `TX_RESULT NO_SCOUT_ACK`, `NOT_LISTENING`,
  `LINE_JAMMED`, `NO_CLOCK` and `TIMEOUT` are all discarded with at most a
  trace line (`PiconetBackend.cpp:343-353`). The comment there says
  "FourWayHandshake's watchdog times out and resets"; it does not — the 5ms
  final-ack timer fires long before the 250ms watchdog.

In every case OSWORD &10 returns `&00 Transmitted OK`. The result codes
`&40 Line jammed`, `&42 Not listening` and `&43 No clock` are unreachable
through both production transports for a transient, per-transaction failure.
(`&43` is still reachable for the static case of no carrier at all, via DCD.)

The consequence compounds with defect 1: the guest believes it has delivered a
frame, so it does not retry at the protocol level, and the higher layer waits
for a reply that was never going to come.

`tests/test_four_way_handshake.cpp:702` locks the unconditional synthesis in.

**Two attempted fixes, both reverted.** Recorded because each looked
obviously right and each made things worse; the next attempt should not
rediscover them.

*Attempt 1 — reachability pre-flight.* Added `NetworkBackend::is_reachable(net,
stn)`, answered from `AunBackend`'s peer table, and had `FourWayHandshake`
decline to start a transaction to a destination that could not be resolved,
suppressing the synthetic scout ack. The reasoning was that the guest's own
timeout would then conclude "not listening", as on a real wire.

It does not. Against the interop harness, `*I AM 1.99 SYST` to a station
nobody knows produced **no output at all** — no error, no returned prompt —
and stayed that way for the full 60-second window. Nothing was transmitted, so
the pre-flight worked exactly as intended; the guest simply hung. This is worse
than the defect, which at least leaves the machine usable.

The reason is narrower than "NFS has no timeout", which is how this was first
written and is wrong. The ROM has software watchdogs on *either side* of the
handshake: the pre-transmit INACTIVE poll with its 24-bit counter (→ "Line
Jammed"), and `wait_net_tx_ack` at roughly 22 seconds (→ "No reply"). What it
has no escape from is the handshake window itself. `poll_adlc_tx_status` opens
with `ASL tx_complete_flag / BCC` (&98C9-&98CC) — an unbounded spin on a flag
written only by NMI paths — so every retry attempt blocks there waiting for an
interrupt. The 255 retries in `tx_retry_count` bound nothing, because each
attempt's spin is itself unbounded.

That makes ADLC interrupt fidelity load-bearing in an unusual way: an emulator
that gets the TX-completion or line-idle interrupt wrong does not produce a
wrong error, it produces a hung machine with no ROM-level recovery. We
discovered this twice before understanding it.

*Attempt 2 — quiet line.* On the theory that NFS concludes "not listening"
from the *absence of flag fill* after an unanswered scout — our
`is_receiving_flags()` reports continuous flag fill whenever idle and
connected, simulating the clock box, so the line always looks alive — a
~100ms window was added during which flag fill is suppressed after a
transmission to an absent station. The guest hung identically.

**Root cause, from the ROM.** The annotated ANFS 4.18 disassembly
(`/Users/rjs/Code/acornaeology/acorn-nfs/versions/anfs-4.18/output/`) settles
it. The relevant path is:

- `tx_line_idle_check` (&85E3) checks SR2 DCD, then `inactive_poll` (&85F4)
  spins on **SR2 INACTIVE** with a three-byte counter on the stack, reporting
  *Line jammed* on timeout. So INACTIVE gates transmission.
- `nmi_error_dispatch` (&8236), reached from **twelve** call sites, reads the
  TX flag and jumps to `tx_result_fail` (&88D4), which loads `#&41` — *Not
  listening*.

The decisive point is that `nmi_error_dispatch` is an **NMI** handler. NFS does
not poll for the absence of a reply; it transmits its scout, enables the
receiver, and waits for an interrupt. The interrupt that means "nobody
answered" is the line going idle: on a real wire a replying station holds the
line in flag fill, and if none does the line falls inactive, which the MC6854
reports as an interrupt-causing condition.

**Why we cannot produce it.** `Mc6854` excludes Rx Idle from S2RQ — the comment
reads "INACTIVE and RDA do NOT participate in S2RQ" — and `idle_stored_` is
cleared in three places but never latched. The condition NFS is waiting on can
therefore never raise an NMI in Beebium, whatever the handshake does. That is
why both attempted fixes hung: they were arranging for a signal the ADLC is
structurally unable to deliver. The datasheet is explicit that Inactive Idle is
stored and causes an interrupt, so this is a deviation, not a design choice.

**Why the obvious fix is not enough.** Latching `idle_stored_` on the 0->1 edge
of the idle condition, and admitting that latch (not the level) into S2RQ —
exactly mirroring what DCD already does — was tried. It compiles, and the
handshake, socket, boot and NMI-delivery suites stay green, but **16 of the 135
`Mc6854` tests fail**. The reason is the PSE cascade: once latched, INACTIVE
sits at priority 2 and masks AP (P3) and RDA (P4), so the ROM's frame-reading
loops stop seeing the bits they read frames with. The latch is cleared only by
CLR_RX_ST, so it persists across the arrival of a frame.

That is not a reason to abandon the approach — real hardware has the same
priority scheme and works, and NFS does issue CLR_RX_ST as part of both TX
preparation (CR2=&67 at &860D) and its RX listen setup. It means the change has
to account for when the latch is cleared relative to frame reception, and the
PSE tests need to be reasoned through one at a time rather than bulk-updated.
That is a careful piece of ADLC work in its own right, not a rider on a
transport fix.

**ADLC half: DONE.** Rx Idle now latches on the 0->1 edge and reaches S2RQ
through that latch, as the datasheet requires and as DCD already did. The
datasheet pages were read directly rather than trusting our transcription: SR1
b1 is "All the status bits (stored conditions) of status register #2 (except
RDA bit) ... logically ORed", so only RDA is excluded, and Figure 10's priority
tree places Rx Idle above AP and RDA.

The PSE interaction needed no compromise in the end. Two things resolved it:

- Tests that received a frame without first clearing status were relying on the
  absent latch. Real software always clears -- NFS's listen setup writes
  CR2=&67, which carries CLR Rx ST -- so they now take up the listening
  position the same way, through a `begin_listening()` fixture helper that says
  why.
- More interestingly, it exposed a second bug. `is_expecting_frame()` reported
  the line inactive during a transaction, whereas a real wire is held in flag
  fill from end to end: at every step whichever station owes the next frame
  holds the line. Once Rx Idle latched, that error became visible as a latched
  INACTIVE masking AP and RDA mid-exchange -- hiding the very frames the
  transaction consists of. It now reports the line held for any stage with a
  transaction in flight *or* frames still queued for the guest, and inactive
  only when genuinely quiet.

The full 2775-test suite is green, as is the interop suite.

**Transport half: still open, and now precisely specified.** The ADLC can
deliver the interrupt, but that alone is not sufficient -- established by
experiment rather than argument: re-applying the reachability pre-flight on top
of the fixed ADLC still leaves the guest silent.

The reason is the edge. The latch fires on a 0->1 transition of the idle
condition, and with the pre-flight the handshake never leaves `Idle`, so the
line is idle before the transmission and idle after. No transition, no
interrupt. On a real wire the act of transmitting makes the line busy and it
then *falls* idle, and it is that fall the receiver latches.

**Transport half: DONE.** Two routes, because transports differ in when they
can know:

- *Known in advance.* `NetworkBackend::is_reachable(net, stn)` is asked before
  the handshake commits to a transaction. `AunBackend` answers from its peer
  table, applying the same `dest_net=0 -> local_net` translation `send_frame`
  uses so the answer matches what a send would actually do. Backends that
  cannot know leave it `true`.
- *Known afterwards.* A transport that has already tried reports failure by
  delivering a `Nack`, which the handshake accepts in `DataSent` and treats as
  the end of the transaction. `PiconetBackend` now does this for every non-OK
  `TX_RESULT` instead of dropping it, which is the only honest option open to
  it: its firmware runs the wire handshake and reports after the fact.

Both routes converge on the same thing: abandon the transaction, and leave the
line busy for `LINE_BUSY_AFTER_TX` (~250us, roughly a scout's wire time) so
that it then *falls* idle. That fall is the edge the ADLC latches, and the
interrupt it raises is what NFS turns into an error.

**Confirmed against a real bridge.** `*I AM 1.99 SYST` to a station neither the
bridge nor our peer table knows now produces `Station 1.99 not present` and
returns to a prompt. The message came as a mild surprise: the acceptance test
had been written against a guessed list of failure strings, none of which
matched, so it kept reporting failure after the defect was already fixed. The
list is now taken from the ANFS 4.18 string table (the compound `Station n.n
<suffix>` forms are built from suffixes at &980F and &982A) rather than
guessed.

**Why "not present" and not "not listening".** The two suffixes distinguish
*which code path reported the failure*, not which failure occurred. `*I AM`
probes with a MachinePeek (ctrl &88, port 0) via `init_tx_ptr_for_pass`, and
that path returns through `fixup_reply_status_a`, which rewrites &41 (the
not-listening TX code, class 1 "Net error") to &42 (class 2 "Station") and
then `CLV`s to select the "not present" suffix. The general `send_net_packet`
path instead enters at `classify_reply_error`, where a `BIT` sets V and selects
"not listening". So a MachinePeek probe always says "not present" whatever went
wrong — which is worth knowing before reading either string as diagnostic.

(That explanation, and the two confirmations below, come from the maintainers
of the annotated NFS/ANFS disassemblies at
`/Users/rjs/Code/acornaeology/acorn-nfs`, in response to findings from this
work. Their disassembly is the authority here; ours were black-box
observations.)

**Tests.** Four cases tagged `[reachability]` in `tests/test_mc6854.cpp`,
covering no-synthetic-ack, the busy-then-idle edge raising IRQ, an unaffected
reachable station, and the after-the-fact `Nack` route; the `TX_RESULT` cases
in `tests/test_piconet_backend_rx.cpp`; and
`integration_tests/pieb-aun/tests/test_unreachable_station.py` against a real
bridge, no longer `xfail`.

Note also the dependency on defect 1, which ran the opposite way: the
unconditional synthetic ack was what stopped destroyed packets from
deadlocking the state machine. That prop is no longer needed now the holding
queue exists, which makes this a better time to revisit it than before — but
not without the ROM work first.

**Test.** `integration_tests/pieb-aun/tests/test_unreachable_station.py`,
`xfail(strict=True)`. It addresses a station neither the bridge nor our peer
table knows, and requires that the guest be told *something*. It will flip to
an unexpected pass the moment this is genuinely fixed.

## 3. Disconnecting the AUN cable does not disconnect it

**Status:** FIXED. **Severity:** medium.

`AunBackend::set_connected` flips the `connected_` flag and bumps the status
sequence (`AunBackend.cpp:399-404`), and nothing else. Both `send_frame`
(`:137`) and `receive_frame` (`:223`) gate solely on `socket_fd_`. With the
transport panel showing "Disconnected", frames continue to leave and — more
importantly — continue to be delivered inbound.

DCD and CTS do follow `connected_`, so the guest's NFS ROM usually declines to
transmit of its own accord, which masks the outbound half. The inbound half is
unmasked: a peer can still drive our handshake while we present as unplugged.

`SpeedGate` models the intended semantics correctly — it severs traffic in both
directions and reports the link down — which makes the inconsistency plain.

**Fix applied.** Both methods now return early while disconnected. The
inbound path additionally *drains* whatever has queued on the socket, bounded
per call, rather than leaving it buffered: a severed wire loses traffic, it
does not store it, and reconnecting must not deliver a burst of frames
belonging to handshakes that finished long ago.

Extracting `SpeedGate`'s shape into a shared `CableGate` decorator was
considered and rejected for now: the two severances differ in exactly this
respect, since speed gating is transient and expected to resume, and folding
them together would have obscured that.

**Tests.** Four cases under `[cable]` in `tests/test_aun_backend.cpp`, covering
each direction, restoration, and the no-replay property.

## 4. The AUN socket is blocking

**Status:** FIXED. **Severity:** medium — violated the no-stall rule.

The AUN socket is created with no `O_NONBLOCK` and no `FIONBIO`; `select()`
guards the receive path only. `sendto()` on a blocking datagram socket can
stall when the send buffer is full or the interface is congested, and it does
so on the emulation thread.

This breaches the standing rule that no external peer may stall the emulator
(bounded, never-blocking I/O on the emulation thread; back-pressure only
through the real flow-control signal). It is also the failure mode most likely
to appear precisely when the network is under the load that triggers defect 1,
which makes the two hard to tell apart in the field.

**Fix applied.** The socket is set non-blocking at construction, on both
POSIX (`O_NONBLOCK`) and Windows (`FIONBIO`). A `sendto` that would block is
distinguished from a real error and counted rather than logged as a fault:
on a datagram socket it means the send buffer is full, so the frame is dropped
rather than delayed, which is the correct trade for the emulation thread —
Econet is a lossy medium and the guest's protocol copes with a lost frame.

`AunBackend::send_would_block_count()` exposes the count. It is currently
diagnostic only; once defect 2 gives us a failure channel, a would-block send
becomes something we can report to the guest honestly instead of counting
quietly.

## 5. `SubscribeEconetEvents` is unimplemented

**Status:** FIXED. **Severity:** low, but misleading.

`EconetService.hpp:283-293` returns `UNIMPLEMENTED`, and no `ObservableBackend`
exists in the tree. `docs/econet-integration.md` presents both the streaming
RPC and the observable decorator as settled design decisions, and
`docs/networking.md` refers to the stream when describing a possible
Piconet MONITOR-mode traffic analyser. A reader of either document would
reasonably conclude the feature ships.

Frame-level observability would also be the natural instrument for diagnosing
defects 1 and 2 in the field, which argues for building it rather than
retracting it.

**Fix applied.** Built, as an `ObservableBackend` decorator sitting directly
above the wire-side backend and below the speed gate — so it records what
actually crossed the transport rather than what the handshake intended, and a
gated transport shows as the silence it is.

The emulation thread never allocates on this path: payloads are truncated into
an inline array rather than copied into a vector, and the ring buffer is fixed
at 256 events. Subscribers poll by sequence number rather than being pushed to,
so a slow or vanished one cannot hold up emulation — it misses events instead,
which the sequence gap makes visible rather than hiding. `data_length` always
reports a payload's true size even when the bytes are truncated, so a reader
can tell a short frame from a clipped one.

The stream starts from the moment of subscription rather than replaying the
ring: a subscriber asks what happens next, and history it never asked for would
be indistinguishable from live traffic. Subscribing with no Econet hardware
fitted is refused with `FAILED_PRECONDITION` rather than opening a stream that
could only ever be silent, which a caller cannot tell from a quiet network.

**Tests.** Three cases in `tests/test_grpc_econet.cpp` (refusal without
hardware, frames in both directions with monotonic sequencing, and payload
truncation reporting its true size); mock-stub tests in the Python and
TypeScript clients.

## 6. Documentation drift

**Status:** FIXED. **Severity:** low.

- `docs/howto_write_an_econet_transport.md` describes mDNS support as "macOS
  bidirectional, Windows advertise-only, Linux none today". All three platforms
  are bidirectional.
- `docs/networking.md` contradicts itself: the "AUN peer discovery via mDNS"
  section describes shipped discovery, while the "Implementation Status"
  section still states that local discovery is not implemented and all peer
  mappings must be explicit.
- `docs/networking.md` open question 5, "Broadcast announcement format —
  Deferred", was answered by the mDNS work.

## 7. Extension/backend teardown order is load-bearing and unmarked

**Status:** FIXED (documented; the order was already correct). **Severity:** low.

`AunDiscoverySubscriber` holds a reference to `AunBackend` and invokes
callbacks from the mDNS browser thread. The subscriber is owned by the
transport extension (in the `EconetTransportRegistry`); the backend is owned by
the `EconetSocket` inside the machine. Correctness depends on the registry
being destroyed before the machine.

It is: in `ServerMain::start()` the machine is declared before the transport
registry, so reverse destruction order tears the registry down first, and
`~AunDiscoverySubscriber` stops the browser before the backend dies. But
nothing at either declaration says so, the two declarations are ~40 lines apart
in a very long function, and the header comment on
`AunEconetTransportExtension` asserts the opposite — that the backend outlives
the extension.

**Fix applied.** Both sites now state the dependency: the declaration in
`ServerMain::start()` explains why the machine must be declared first, and
`AunEconetTransportExtension`'s member comment — which previously asserted the
opposite — now agrees with it. No behaviour changed; the ordering was already
correct, just undocumented and contradicted.

If the ordering ever needs to change, the answer is for the extension to hold a
weak handle rather than a raw reference.

---

## Related

- `docs/networking.md` — the as-built design of the stack these defects sit in.
- `docs/discussion/pieconetbridge-aun-interop-testing.md` — the integration
  test programme intended to catch defects 1-4 automatically.
- `docs/discussion/pieconetbridge-investigation.md` — a separate, unresolved
  problem on the Piconet/real-wire path.

---

## 9. A transaction stalls when acknowledgements are reordered

**Status:** FIXED. **Severity:** high — a stalled session, on any network
where acknowledgements can be delayed.

Found by the perturbation proxy, which is what that instrument was built for.
With every acknowledgement from the bridge held until one further datagram has
overtaken it, a session logs in and `*CAT` prints its full catalogue, but the
transaction never completes and the prompt does not return.

### What the event stream shows

Subscribing to `SubscribeEconetEvents` and sampling the handshake's internal
state alongside it gives the whole picture. From the point of failure:

```
12.877  WIRE  RX unicast 1.254->0.1 port=0x90 len=5     the reply
12.877  WIRE  RX ack     1.254->0.1 port=0x99 len=0     its ack, arriving after
12.891  STATE stage=scout_received held=1
13.400  STATE stage=idle          wd=2                  watchdog abandoned it
13.875  WIRE  RX unicast 1.254->0.1 port=0x90 len=5     bridge retransmits
13.876  STATE stage=scout_received
14.395  STATE stage=idle          wd=3                  abandoned again
...     the same, once a second, indefinitely
```

So the handshake **is** delivering the fileserver's reply to the guest, as a
scout. The guest never acknowledges it. The watchdog abandons the transaction
250ms later, the bridge retransmits because it was never acked, and the loop
repeats for ever. Each cycle costs a watchdog reset — six in a twelve-second
run.

That relocates the question. It is not that the reply is lost, nor that the
holding queue failed: `redelivered=2` shows reordered frames being caught and
re-offered correctly. It is that the guest declines to answer.

### Contributing defect, fixed: abandoning a transaction discarded held frames

`reset_handshake()` cleared the holding queue. Held frames are by definition
*not* part of the transaction in flight -- that is the whole reason they are
held -- so a watchdog timeout, a reported transmit failure, or an unexpected
transmission would throw away network traffic that had arrived and was simply
waiting for a stage that could accept it. With six watchdog resets in this
scenario, that was destroying frames steadily.

Fixed: `reset_handshake()` now leaves held frames alone, and only the public
`reset()` -- a machine reset, where forgetting everything is the point --
discards them. This was self-inflicted, introduced with the defect 1 fix and
invisible until reordering made watchdog resets common.

It improved matters (`redelivered` went from 1 to 2) but did not resolve the
stall, so it was a contributing defect rather than the cause.

### Secondary finding: stale acks occupy the holding queue

An `Ack` for a transaction that has already completed is accepted by no stage,
so it sits in the holding queue until its TTL expires a second later
(`expired=1` in every run). Harmless today, but it means the queue carries
frames that can never be delivered, and under heavier reordering that is
capacity spent on nothing. An `Ack` arriving with no transaction outstanding
could be recognised as stale and discarded immediately rather than held.

### Root cause, confirmed by the AUN handle

The handle settled it. A peer retransmitting an unacknowledged frame reuses
it, so it distinguishes a retransmission from a peer legitimately sending the
same thing twice — something no amount of staring at payloads can do. Adding it
to the event stream produced this immediately:

```
14.959  RX unicast port=0x90 len=5  handle=16404
15.958  RX unicast port=0x90 len=5  handle=16404
16.960  RX unicast port=0x90 len=5  handle=16404
17.966  RX unicast port=0x90 len=5  handle=16404
18.966  RX unicast port=0x90 len=5  handle=16404
```

One frame, five times, handle unchanged. Every "new" reply after the first was
a retransmission, and we were handing each copy to the guest as a fresh scout
for a fresh four-way handshake.

The full mechanism:

1. Reordering delays an acknowledgement, so a reply lands while the handshake
   is in `DataSent` and is held.
2. It is redelivered correctly — but by then the guest has often been given an
   earlier copy already.
3. NFS has consumed that reply and closed its receive block, so the duplicate
   is traffic it has no reason to answer. This was inferred from black-box
   behaviour and has since been confirmed in the ROM:
   `rx_complete_update_rxcb` ORs &80 into byte 0 of the receive control block
   (&83D8), and the slot scanner at `scout_ctrl_check` matches only a control
   byte of exactly &7F — so &FF matches nothing and an RXCB is **one-shot**. A
   retransmitted reply walks the port list, matches no slot, and is discarded
   silently at `discard_no_match`.

   That makes acknowledging the duplicate ourselves the correct fix rather than
   a workaround: there is no arrangement of the ROM's receive blocks under
   which a retransmission could be answered by the guest.
4. Nothing acknowledges it. The peer retransmits indefinitely, and every copy
   costs a watchdog reset.

### Fix

`FourWayHandshake` remembers the handles of inbound data frames the guest has
acknowledged, and answers a retransmission itself instead of delivering it
again. Three cases, and they differ:

- **Already acknowledged** — acknowledge it directly. The guest has consumed
  it and will not answer a copy.
- **Still in flight** — drop it. Acknowledging would claim a delivery the
  guest has not made; redelivering would start a second handshake for one
  frame.
- **Handle unseen, or zero** — ordinary traffic. Zero means the transport has
  no handles (Piconet, whose firmware owns the wire handshake), so nothing can
  be concluded and nothing is.

Matching is on the handle alone. Matching on content would swallow a peer
legitimately sending the same bytes twice. The memory is a fixed window of the
32 most recent handles: peers retry for as long as they choose — PiEconetBridge
retries once a second, indefinitely — so what is needed is to outlast a burst
of traffic rather than a span of time, and handles advance monotonically, so no
timer is involved.

This is the mirror of upstream's `AUTOACK`, which exists because emulators
re-send what is really a retry as a fresh packet and the bridge acknowledges on
their behalf. The same asymmetry, pointing the other way.

### Contributing defect, also fixed: abandoning a transaction discarded held frames

`reset_handshake()` cleared the holding queue. Held frames are by definition
*not* part of the transaction in flight — that is the whole reason they are
held — so a watchdog timeout, a reported transmit failure, or an unexpected
transmission threw away traffic that had arrived and was waiting for a stage
that could accept it. With six watchdog resets in a twelve-second run, that was
destroying frames steadily.

Fixed: only `reset()`, a machine reset where forgetting everything is the
point, discards them. Self-inflicted, introduced with the defect 1 fix and
invisible until reordering made watchdog resets common. It was necessary but
not sufficient — `redelivered` went from 1 to 2 and the stall remained.

### Secondary finding: stale acks occupy the holding queue

An `Ack` for a transaction that has already completed is accepted by no stage,
so it sits in the holding queue until its TTL expires a second later. Harmless
— it no longer appears now that transactions complete — but it means the queue
can carry frames that will never be delivered, and under heavier reordering
that is capacity spent on nothing. An `Ack` with no transaction outstanding
could be recognised as stale and discarded on arrival. Left alone for now,
since inventing more special cases in the receive path is exactly how this area
became hard to reason about.

**Tests.** Four cases tagged `[duplicate]` in `tests/test_four_way_handshake.cpp`
(acknowledge a retransmission, do not confuse a fresh transaction carrying the
same bytes, drop one arriving mid-transaction, bound the memory), two under
`[holding]` for the reset behaviour, and
`integration_tests/pieb-aun/tests/test_reordered_reply.py` against a real
bridge with acks deliberately reordered.
