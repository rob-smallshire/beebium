# AUN robustness: defects in the transport and handshake layers

An audit of the Econet transport stack against the question "what does the
guest see when the network misbehaves?" Eight defects, in descending order of
severity. Five are correctness bugs with user-visible consequences; three are
documentation or hygiene.

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
on a loaded LAN with a bridge, a fileserver, and a third station on it.

---

## 1. Out-of-order and interleaved packets are destroyed

**Status:** open. **Severity:** high — silent data loss.

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

The behaviour is currently asserted as correct.
`tests/test_four_way_handshake.cpp:855`, *"unexpected AUN packet type in
DataSent is ignored"*, injects a Unicast while in `DataSent` and asserts that
`receive_frame()` returns nothing and no state changes — which is precisely the
overtaking-reply case above.

We are partly shielded from the worst form of this by defect 2: because the
final ack is synthesised unconditionally, a destroyed `Ack` cannot deadlock the
state machine, and the 250ms watchdog resets anything that does get stuck. That
is luck rather than design, and it converts a hang into silent loss.

**Fix.** Give `FourWayHandshake` a bounded inbound holding queue. A packet
that does not match the current stage is parked with an arrival tick and a TTL
rather than dropped, and re-offered when the stage next changes. Two
refinements:

- Broadcasts and inbound Immediates addressed to us need no handshake context
  and should be deliverable from any stage, subject to the ADLC being able to
  accept a frame — park them only when the ADLC is mid-frame, not when the
  handshake is mid-transaction.
- Cap the queue and drop oldest-first when full, so a hostile or broken peer
  cannot grow it without bound. Count the drops and expose the counter.

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

**Status:** open. **Severity:** high — the guest is actively misinformed.

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

**Fix.** Add a failure channel to `NetworkBackend` so a transport can report
that a specific transmission failed — either by injecting a `Nack` frame or via
an explicit `send_result` hook. In `DataSent`, a reported failure should
suppress the synthetic final ack and instead drive the ADLC into the state the
NFS ROM reads as the corresponding OSWORD status. Retain the timeout-driven
synthetic ack purely as the fallback for transports that cannot report
(silence is still better than a hang). `PiconetBackend` already has the result
codes in hand and needs only to forward them; `AunBackend` should report the
unmapped-peer case the same way.

## 3. Disconnecting the AUN cable does not disconnect it

**Status:** open. **Severity:** medium.

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

**Fix.** Either early-return on `!connected_` in both methods, or extract
`SpeedGate`'s shape into a `CableGate` decorator and drive it from
`set_connected`, so there is one implementation of "the wire is severed".

## 4. The AUN socket is blocking

**Status:** open. **Severity:** medium — violates the no-stall rule.

The AUN socket is created with no `O_NONBLOCK` and no `FIONBIO`; `select()`
guards the receive path only. `sendto()` on a blocking datagram socket can
stall when the send buffer is full or the interface is congested, and it does
so on the emulation thread.

This breaches the standing rule that no external peer may stall the emulator
(bounded, never-blocking I/O on the emulation thread; back-pressure only
through the real flow-control signal). It is also the failure mode most likely
to appear precisely when the network is under the load that triggers defect 1,
which makes the two hard to tell apart in the field.

**Fix.** Set the socket non-blocking at construction and treat `EWOULDBLOCK`
from `sendto` as a transmit failure — which, once defect 2 is fixed, is
something we can honestly report to the guest.

## 5. `SubscribeEconetEvents` is unimplemented

**Status:** open. **Severity:** low, but misleading.

`EconetService.hpp:283-293` returns `UNIMPLEMENTED`, and no `ObservableBackend`
exists in the tree. `docs/econet-integration.md` presents both the streaming
RPC and the observable decorator as settled design decisions, and
`docs/networking.md` refers to the stream when describing a possible
Piconet MONITOR-mode traffic analyser. A reader of either document would
reasonably conclude the feature ships.

Frame-level observability would also be the natural instrument for diagnosing
defects 1 and 2 in the field, which argues for building it rather than
retracting it.

**Fix.** Build it, or mark it plainly as not-built in both documents.

## 6. Documentation drift

**Status:** open. **Severity:** low.

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

**Status:** latent. **Severity:** low.

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

**Fix.** A comment at both declaration sites stating the dependency. Consider
having the extension hold a weak handle rather than a raw reference if the
ordering ever needs to change.

---

## Related

- `docs/networking.md` — the as-built design of the stack these defects sit in.
- `docs/discussion/pieconetbridge-aun-interop-testing.md` — the integration
  test programme intended to catch defects 1-4 automatically.
- `docs/discussion/pieconetbridge-investigation.md` — a separate, unresolved
  problem on the Piconet/real-wire path.
