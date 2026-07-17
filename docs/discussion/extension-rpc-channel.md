# Extension RPC Channel: tunnelling extension-defined gRPC services

Status: Draft / proposal. Created on the `feature/extension-rpc-channel`
branch as the design to iterate on. Nothing here is implemented yet.

## TL;DR

Today, an extension that wants to expose a gRPC service compiles the gRPC
service stub and implementation into its plugin module, and the core
registers that `grpc::Service*` with its `ServerBuilder`. That makes the
plugin link gRPC, which puts two copies of the gRPC runtime in one process
and crashes when a call crosses the executable <-> plugin boundary. See
`grpc-windows-streaming-race.md` and `project_macos_static_grpc_*` for the
full history.

This document proposes removing gRPC from plugins entirely. The core hosts
a single, generic `ExtensionRpc` service. Extensions register plain C++
handlers through the extension ABI; the only thing that crosses the module
boundary is **opaque serialized bytes**. On top of that channel we layer a
tiny code generator so that an extension's API can still be **defined as a
normal protobuf `service`** and **consumed from Python / TypeScript / Swift
as a normal typed stub** - "gRPC over gRPC". The linkage problem disappears
on every platform, present and future.

## 1. The need

Beebium is built around the idea that almost all functionality is exposed
over a set of rich gRPC services. That is what lets the emulator run
headless, be driven for sophisticated automated testing of BBC Micro
software, and support multiple front-ends from one core:

- the macOS (Swift / Metal) GUI,
- the Python client library (testing, automation),
- the TypeScript client library,
- and, in future, a standalone graphical debugger.

We also want true extensibility: peripheral extensions, defined in and
loaded from plugins, that add capability to the emulator - emulated
hardware peripherals (SCSI, RTC, serial peers), alternative Econet
transport backends (AUN, Piconet, IP232, RFC 2217), and, in future, other
extension points entirely (e.g. co-processors).

The tension is the intersection of the two: as the system grows new
capabilities via plugins, **those capabilities must be remote-controllable
over gRPC** so that every front-end can use them, exactly like the built-in
services. We need extension-provided services to be first-class.

## 2. The constraint: plugin-hosted gRPC services don't link cleanly

gRPC keeps per-process runtime state - the `ExecCtx` thread-local, closure
lists, the iomgr, the protobuf descriptor pool. That state must be a single
instance in the process. vcpkg builds gRPC as a **static** library on every
platform (`vcpkg_check_linkage(ONLY_STATIC_LIBRARY)`), so when both the
executable and a plugin link it, each gets its own copy.

What we have learned, the hard way:

- **macOS**: a gRPC call dispatched from the server core (exe copy) into a
  plugin's service handler (plugin copy) crashes with `SIGSEGV` in
  `grpc_core::ExecCtx::Run`. We worked around it (Option 2) by making the
  plugin leave the gRPC symbols undefined and resolve the exe's single copy
  at load (`-rdynamic` + `-undefined dynamic_lookup` + headers-only proto
  libs). It works but is delicate and macOS-specific.
- **Windows**: the same root cause. The crash dump shows `0xC0000005`
  reading `0x8` in the exe's closure code - gRPC issue
  [#39198](https://github.com/grpc/grpc/issues/39198) ("closure_list was
  0x8"). `dumpbin` confirms gRPC is statically linked into both the exe and
  `rpc-serial.dll`, while protobuf/abseil are shared DLLs. The macOS trick
  cannot be ported (Windows DLLs cannot carry undefined symbols), and
  building gRPC as a shared DLL fails because gRPC's bundled `upb` does not
  export its data symbols (`upb_alloc_global`, `kUpb_MiniTable_Empty`) -
  which is exactly why vcpkg forces it static.
- **Linux**: works, because everything links one shared system
  `libgrpc++.so`.

So plugin-hosted gRPC is a permanent, per-platform maintenance burden, and
each new extension point would re-open the wound. We want it gone.

## 3. The key insight

Every failure above has the same shape: **gRPC/protobuf runtime objects
(closures, `ExecCtx`, descriptors) cross the exe <-> plugin boundary.** That
is the thing that cannot be made robust when the runtime is duplicated.

If the only thing that crosses the boundary is **opaque serialized bytes**,
the problem evaporates:

- gRPC lives **only in the core**. Plugins never link it. There is no
  duplicate-runtime surface, on any platform, for any future extension
  point.
- protobuf may still be used inside a plugin for *its own* message types,
  but because the core treats the payload as raw bytes and never
  deserializes the inner message, **no protobuf descriptor or message
  object is shared across the boundary**. Even a duplicated, statically
  linked protobuf is harmless here - nothing of its runtime state crosses.

This is not a workaround. It removes the fragile crossing rather than trying
to make it survivable.

## 4. Prior art (this is a known-good pattern)

- **Twirp**, **Connect (connectrpc)**, and **Cap'n Proto RPC** all define
  services in an IDL (often protobuf) and carry them over a transport that
  is *not* gRPC's. They prove that "protobuf-defined service + custom
  transport" is sound and ergonomic.
- In-repo, `ExtensionUiService` already follows exactly the structure
  proposed here: the core hosts one generic service; plugins contribute
  capability (View trees, event handlers) through a C++ ABI with no gRPC in
  the plugin. The RPC channel proposed here is its programmatic sibling. The
  UI channel is intentionally dynamic (untyped View/Dispatch), which is what
  makes it awkward for automation; this channel restores static typing.

## 5. Architecture overview

```
   Python / TypeScript / Swift client
        |  (typed extension stub, generated)
        v
   ExtensionRpc.Invoke / ServerStream   <-- ONE real gRPC service, in the core
        |  envelope: {extension_id, service, method, payload-bytes}
        v
   Core: ExtensionRpcServiceImpl
        |  routes by extension_id -> registered dispatcher
        |  bridges streaming to abstract Reader/Writer
        v
   Extension ABI (beebium_extension_api, NO gRPC types)
        |  ExtensionRpcDispatcher::invoke(method, request-bytes, &response-bytes, ctx)
        v
   Plugin: generated dispatcher  ->  typed C++ handler (parses its own protos)
```

Three layers:

1. **The channel** - one core-hosted gRPC service (`ExtensionRpc`) that
   carries opaque request/response bytes plus routing metadata.
2. **The ABI seam** - plain C++ interfaces in `beebium_extension_api`
   (handlers, an abstract response writer/reader, a context). No gRPC, no
   protobuf-runtime types cross here; only `std::string`/`string_view`
   byte buffers.
3. **The typing layer** - a small code generator that turns an extension's
   `.proto` `service` into (a) a plugin-side dispatcher with typed virtual
   methods, and (b) per-language client stubs that look and feel like native
   gRPC stubs but tunnel over `ExtensionRpc`.

The channel and ABI are enough to be *correct* and unblock every platform;
the typing layer is what keeps the developer experience first-class. They
can land in that order.

## 6. The channel service

`src/service/proto/extension_rpc.proto` (core-owned, lives with the other
core services, so its stub + impl are in the same module - no boundary):

```proto
syntax = "proto3";
package beebium.extension;

service ExtensionRpc {
  // Unary: one request, one response.
  rpc Invoke(InvokeRequest) returns (InvokeResponse);

  // Server-streaming: one request, a stream of responses (status feeds,
  // event subscriptions, etc.).
  rpc ServerStream(InvokeRequest) returns (stream InvokeResponse);

  // DEFERRED (not in the initial scope; no extension needs them yet).
  // Client-streaming and bidi would be added the same way: the FIRST request
  // frame carries the route (extension_id/service/method), subsequent frames
  // carry payload only.
  //   rpc ClientStream(stream InvokeRequest) returns (InvokeResponse);
  //   rpc BidiStream(stream InvokeRequest) returns (stream InvokeResponse);

  // Discovery + reflection (see section 10).
  rpc ListExtensions(ListExtensionsRequest) returns (ListExtensionsResponse);
  rpc DescribeExtension(DescribeExtensionRequest) returns (DescribeExtensionResponse);
}

message InvokeRequest {
  string extension_id = 1;            // instance UUID (an extension can have
                                      // several instances on different
                                      // attachment points)
  string service = 2;                 // logical service name, e.g. "RpcSerial"
  string method = 3;                  // method name, e.g. "Send"
  bytes  payload = 4;                 // serialized request message
  map<string, string> metadata = 5;  // headers (trace ids, auth, ...)
}

message InvokeResponse {
  bytes  payload = 1;                 // serialized response message
  uint32 status_code = 2;            // mirrors grpc::StatusCode (0 == OK)
  string status_message = 3;
  map<string, string> metadata = 4;  // trailers
}
```

Notes:

- **One connection, one more service.** `ExtensionRpc` is just another
  service on the core's existing gRPC server and port. Clients add one stub
  on the connection they already have.
- **Status** mirrors `grpc::StatusCode` so the client stub can raise/throw
  the same error types a native call would.
- **Deadlines / cancellation** ride the outer `Invoke` call itself: the core
  surfaces `ServerContext::IsCancelled()` and the remaining deadline to the
  extension via the context (section 7). For server-streams, a write that
  the peer has abandoned returns "stop".
- **Metadata** carries cross-cutting headers without baking them into every
  extension schema.

## 7. The extension C++ ABI

In `beebium_extension_api` (which already is the formal, gRPC-free ABI
surface). Everything here is plain C++; **no gRPC or protobuf-runtime type
appears**.

```cpp
namespace beebium::ext {

// Mirrors grpc::StatusCode numerically; 0 == OK.
struct RpcStatus {
  int code = 0;
  std::string message;
  static RpcStatus ok() { return {}; }
  static RpcStatus error(int c, std::string m) { return {c, std::move(m)}; }
};

// Per-call context. The core implements it over grpc::ServerContext.
class RpcContext {
 public:
  virtual ~RpcContext() = default;
  virtual bool is_cancelled() const = 0;                 // peer gone / deadline
  virtual const std::map<std::string, std::string>& metadata() const = 0;
};

// Server-streaming sink. write() returns false once the peer is gone, so the
// extension can stop producing and never blocks the emulation thread (it runs
// on a gRPC worker thread, like today's services).
class RpcResponseWriter {
 public:
  virtual ~RpcResponseWriter() = default;
  virtual bool write(std::string_view serialized_response) = 0;
};

// Client-streaming / bidi source.
class RpcRequestReader {
 public:
  virtual ~RpcRequestReader() = default;
  virtual bool read(std::string& serialized_request) = 0;  // false == end
};

// What an extension registers. A generated subclass (section 8) implements
// the typed methods; hand-rolling is also fine for one-offs.
class ExtensionRpcDispatcher {
 public:
  virtual ~ExtensionRpcDispatcher() = default;

  // The logical service name this dispatcher serves, e.g. "RpcSerial".
  virtual std::string_view service_name() const = 0;

  virtual RpcStatus invoke(std::string_view method,
                           std::string_view request,
                           std::string& response,
                           RpcContext& ctx) = 0;

  virtual RpcStatus server_stream(std::string_view method,
                                  std::string_view request,
                                  RpcResponseWriter& writer,
                                  RpcContext& ctx) = 0;

  // client_stream / bidi: default to UNIMPLEMENTED; override when needed.
};

}  // namespace beebium::ext
```

The existing `grpc_services() -> std::vector<grpc::Service*>` seam on
`PeripheralExtension` is **replaced** by:

```cpp
virtual std::vector<beebium::ext::ExtensionRpcDispatcher*> rpc_dispatchers();
```

The core registers each dispatcher in a per-instance routing table keyed by
`(extension_id, service_name)`. `ExtensionRpcServiceImpl` (the one real gRPC
service) parses the envelope, looks up the dispatcher, builds an
`RpcContext`/`RpcResponseWriter` over the live `grpc::ServerContext`/
`grpc::ServerWriter`, and forwards.

Threading and lifetime contracts are unchanged from today: dispatch happens
on gRPC worker threads, the extension owns its own synchronisation, and the
"no external peer stalls the emulator" rule holds because streaming writers
report backpressure/cancellation rather than blocking the emulation thread.

## 8. Defining a custom extension service

A developer defines the extension's API as an ordinary protobuf service.
Example, re-casting today's `rpc-serial`:

`src/extensions/rpc-serial/rpc_serial.proto`

```proto
syntax = "proto3";
package beebium.ext.rpc_serial;

service RpcSerial {
  rpc Send(SendRequest) returns (SendResponse);
  rpc Receive(ReceiveRequest) returns (ReceiveResponse);
  rpc GetStatus(StatusRequest) returns (StatusResponse);
  rpc WatchStatus(StatusRequest) returns (stream StatusResponse);
}
message SendRequest    { bytes data = 1; }
message SendResponse   { uint32 accepted = 1; }
// ... etc.
```

Two artefacts are generated from it by a custom protoc plugin
`protoc-gen-beebium-ext` (a thin wrapper around the descriptor; see
`cmake/BeebiumProto.cmake` for where this hooks in):

1. **The message types** - generated by *stock* `protoc` (`--cpp_out`),
   exactly as now. These are normal protobuf messages, used only inside the
   plugin. (On the client side, stock `protoc` likewise generates the
   message types for each language.)

2. **The plugin-side dispatcher** - a generated base class:

```cpp
// generated rpc_serial.beebium-ext.h
class RpcSerialDispatcher : public beebium::ext::ExtensionRpcDispatcher {
 public:
  std::string_view service_name() const final { return "RpcSerial"; }

  // The extension author implements these typed methods:
  virtual beebium::ext::RpcStatus Send(const SendRequest&, SendResponse*, RpcContext&) = 0;
  virtual beebium::ext::RpcStatus GetStatus(const StatusRequest&, StatusResponse*, RpcContext&) = 0;
  virtual beebium::ext::RpcStatus WatchStatus(const StatusRequest&, /*stream*/ ServerWriter<StatusResponse>&, RpcContext&) = 0;

  // Generated glue: parse bytes -> typed message -> dispatch by method name
  // -> serialize typed reply -> bytes. Implements invoke()/server_stream().
  beebium::ext::RpcStatus invoke(std::string_view method, std::string_view req,
                                 std::string& resp, RpcContext& ctx) final { /* generated switch */ }
  beebium::ext::RpcStatus server_stream(std::string_view method, std::string_view req,
                                        beebium::ext::RpcResponseWriter& w, RpcContext& ctx) final { /* generated */ }
};
```

The extension author writes a normal-looking handler:

```cpp
class RpcSerialExtension : public PeripheralExtension {
  class Service : public RpcSerialDispatcher {
    RpcStatus Send(const SendRequest& req, SendResponse* resp, RpcContext&) override {
      resp->set_accepted(endpoint_.inject(req.data()));
      return RpcStatus::ok();
    }
    // ...
  };
  std::vector<ExtensionRpcDispatcher*> rpc_dispatchers() override { return {&service_}; }
};
```

Crucially, `RpcSerialExtension` and its generated dispatcher link **only**
protobuf (for the message types) and `beebium_extension_api`. **No gRPC.**
On Windows that means `rpc-serial.dll` no longer depends on a static gRPC
copy, and the `0x8` crash cannot happen. On macOS the whole Option 2
apparatus (`-rdynamic`, `dynamic_lookup`, headers-only proto libs,
force-load in tests) becomes unnecessary and can be deleted.

`ServerWriter<T>` above is a thin generated typed wrapper over
`RpcResponseWriter` (serialize `T` -> `write(bytes)`), so the author never
touches the byte layer.

## 9. Consuming it from the clients

The whole point is that a remote consumes an extension service as if it were
a normal typed service. For each language the recipe is the same:

- **Messages**: generated by stock protobuf codegen from the extension
  `.proto` (`protoc --python_out` / `protoc-gen-ts` / `protoc-gen-swift`).
  Reused verbatim, no custom work.
- **Service stub**: a thin generated class that, per method, serializes the
  typed request, calls `ExtensionRpc.Invoke`/`ServerStream` with
  `{extension_id, service, method, payload}`, deserializes the typed reply,
  and maps `status_code` to the language's normal error type. Generated by
  the same `protoc-gen-beebium-ext` (one small target per language) - or
  hand-written until the generator exists.
- **Channel**: reuses the client's existing connection; it is just one more
  stub (`ExtensionRpc`) on the same target.
- **Discovery**: `ExtensionRpc.ListExtensions` yields the `extension_id`s and
  the services each instance offers (section 10).

### Python

```python
# Stock protoc output: rpc_serial_pb2.py (messages)
# Generated stub:      rpc_serial_ext.py
from beebium.extension import ExtensionChannel        # wraps the ExtensionRpc stub
from rpc_serial_pb2 import SendRequest, StatusRequest
from rpc_serial_ext import RpcSerialStub

channel = ExtensionChannel(bbc.connection)             # one ExtensionRpc stub
serial  = RpcSerialStub(channel, extension_id=ext_id)  # ext_id from discovery/launch

serial.Send(SendRequest(data=b"Z"))                    # unary, typed
for status in serial.WatchStatus(StatusRequest()):     # server-stream, typed
    print(status.tx_pending)
```

Under the hood `RpcSerialStub.Send` is:

```python
def Send(self, req):
    env = self._channel.invoke(
        InvokeRequest(extension_id=self._id, service="RpcSerial",
                      method="Send", payload=req.SerializeToString()))
    if env.status_code != 0:
        raise ExtensionRpcError(env.status_code, env.status_message)
    return SendResponse.FromString(env.payload)
```

This slots straight into the existing client (`clients/beebium-python-client/src/beebium/`):
the package already wraps services as typed Python objects; an extension stub
is one more of those, parameterised by `extension_id`.

### TypeScript

Same structure. Stock `protoc` + `ts-proto` (already used,
`clients/beebium-typescript-client`, `npm run generate-protos`) generates the messages; the
generated stub wraps the `ExtensionRpc` client:

```ts
const channel = new ExtensionChannel(conn);
const serial  = new RpcSerialStub(channel, extId);
await serial.send({ data });                                  // unary
for await (const s of serial.watchStatus({})) { ... }         // server-stream
```

where `send` does `ExtensionRpc.invoke({extensionId, service:"RpcSerial",
method:"Send", payload: SendRequest.encode(req).finish()})` and decodes
`SendResponse` from the reply payload.

### Swift (macOS client)

The macOS client uses grpc-swift + SwiftProtobuf. Messages come from
`protoc --swift_out`; the generated stub wraps the `ExtensionRpc`
async client:

```swift
let channel = ExtensionChannel(connection)
let serial  = RpcSerialStub(channel, extensionID: extID)
let resp = try await serial.send(.with { $0.data = data })          // unary
for try await s in serial.watchStatus(.init()) { /* ... */ }        // server-stream
```

`send` is `try await extensionRpc.invoke(.with {
  $0.extensionID = id; $0.service = "RpcSerial"; $0.method = "Send"
  $0.payload = try req.serializedData() })`, then
`try SendResponse(serializedBytes: env.payload)`, throwing on non-OK status.

Swift's strong typing and async sequences map cleanly onto the
unary/server-stream pair.

### What "necessary to consume" actually amounts to

1. The extension's `.proto` is available to the client build (we already
   ship/copy protos to clients).
2. Run stock per-language `protoc` for the messages (existing tooling).
3. Run `protoc-gen-beebium-ext` for the thin per-language stub (new, small),
   or hand-write the wrapper until it exists.
4. Construct one `ExtensionChannel` from the existing connection and pass the
   `extension_id`. Done.

No client links anything new; no client speaks a bespoke protocol - it is
protobuf messages over one well-known gRPC method.

## 10. Discovery and reflection

A client needs to learn which extension instances exist and what they
expose. `ExtensionRpc` provides:

- `ListExtensions` -> `[{extension_id, type, attachment_point, services:[name]}]`.
  Parallels the existing `list-extensions` CLI and the
  `ExtensionUiService` instance discovery.
- `DescribeExtension(extension_id)` -> the `FileDescriptorSet` of that
  extension's `.proto`(s), plus a version string.

`DescribeExtension` returning descriptors is the "self-describing" option:
generic tooling (a grpcurl-equivalent, a future debugger's plugin panel) can
build dynamic stubs and call extension methods without compiled stubs at
all - protobuf reflection, scoped to extensions, carried over the channel.
This is optional and can come after the typed-stub path.

## 11. Relationship to ExtensionUiService

`ExtensionUiService` (View trees + `Dispatch` events) and `ExtensionRpc`
(typed method calls) are the dynamic and typed faces of the same idea:
core-hosted generic service, plugin capability via the ABI, routed by
instance id. They should stay parallel initially. Longer term the UI could
be expressed as one well-known service over `ExtensionRpc`, unifying the
plumbing - a nice symmetry, not a priority. The two share the per-instance
routing table and the discovery surface.

## 12. Tradeoffs and costs (honest)

This is not free. We re-host, at the envelope/ABI level, the slice of gRPC
semantics we still want:

- **Streaming** is the real work. Unary is trivial. Server-streaming needs
  the abstract writer + cancellation/backpressure forwarding. Client-stream
  and bidi only if an extension needs them (none today do).
- **Deadlines, cancellation, status, metadata** must be forwarded through
  the envelope and context rather than coming for free.
- **Code generation** is a maintenance surface (`protoc-gen-beebium-ext` for
  C++ dispatchers and per-language stubs). Until it exists, stubs are
  hand-written thin wrappers - functional, just not generated.
- **Double serialization** (inner message bytes wrapped in the outer
  envelope, which gRPC serializes again) adds a per-call copy. Negligible
  for control-plane traffic; worth noting only if some future extension
  becomes a high-rate data-plane (today's high-rate paths - video, audio -
  are core, not plugins).
- **Tooling transparency**: a tunnelled method is invisible to generic gRPC
  tools unless the reflection layer (section 10) is present.
- **Discipline**: "one service for everything" must not rot into a
  god-object; the routing/registry needs to stay clean and per-instance.

These are bounded, one-time costs that **replace** an unbounded,
per-platform, recurring one.

## 13. Alternatives considered

- **Per-platform linkage fixes** (macOS Option 2 + a Windows equivalent):
  fragile, divergent, and re-opened by every new extension point. Windows
  has no clean equivalent (no `dynamic_lookup`; gRPC's shared DLL build is
  broken at upb's exports; exe-symbol-export hits the TLS-across-module and
  64K-export-limit walls). This is the status quo we are trying to leave.
- **Co-location** (compile each extension's gRPC service into the exe, plugin
  provides only the endpoint via the ABI): reliable, and the documented
  piconet TODO - but it puts the service in the exe, so it is *not* a plugin
  service, defeating the extensibility goal, and it needs bespoke exe-side
  plumbing per service. The channel generalises co-location: the "service in
  the exe" is the single `ExtensionRpc`, written once.
- **Build our own shared gRPC DLL / wrap gRPC in a beebium DLL**: blocked on
  the same upb export breakage on Windows; even where it links, sharing the
  `ExecCtx` TLS across modules is the exact thing #39198 says is unreliable.

## 14. Versioning and compatibility

The `ExtensionRpc` envelope is a core service and is covered by the existing
protocol-fingerprint handshake (`versioning-and-compatibility.md`). Extension
`.proto`s evolve independently of the core; `DescribeExtension` reports an
extension version (and, optionally, its descriptors) so a client can detect a
mismatch at discovery time rather than mid-call. Because payloads are opaque
to the core, an extension can change its schema without touching the core or
its fingerprint - extension and client just need to agree, which the
version/descriptor surface makes checkable.

## 15. Phased plan (de-risking)

- **Phase 0 - one vertical slice, no codegen.** Add the `ExtensionRpc`
  service (unary + server-stream) and the ABI seam. Reimplement *only*
  rpc-serial over it, with a hand-written client wrapper in the integration
  test. Prove on **Windows** that the crash is simply gone (the plugin links
  no gRPC) and on **macOS** that Option 2 is unnecessary. This validates the
  entire thesis end-to-end, cheaply, on the box already set up.
- **Phase 1 - firm up the ABI.** Stabilise `ExtensionRpcDispatcher` /
  writer / reader / context; add discovery; migrate piconet, acorn-rtc,
  acorn-scsi.
- **Phase 2 - the typing layer.** `protoc-gen-beebium-ext` for C++
  dispatchers and Python/TS/Swift stubs; optional reflection.
- **Phase 3 - retire the old path.** Remove `grpc_services()`, the macOS
  Option 2 build machinery, and the Windows special-casing.

## 16. Decisions (settled)

1. **Payload typing**: raw **bytes** for now. `Any`/descriptor-reflection are
   a later option (section 10), not in the initial scope.
2. **Streaming scope**: **unary + server-stream only**. Client-streaming and
   bidi are deferred until an extension actually needs them (none do today).
3. **Stubs**: **hand-written** thin wrappers initially. The
   `protoc-gen-beebium-ext` generator is a later phase (Phase 2), once the
   shape has settled against a couple of real migrations.
4. **Routing key**: **instance UUID only** (`extension_id`). This matches the
   existing `ExtensionUiService` instance keying; the logical service +
   method live in the envelope. Discovery lists instances and their services.
5. **UI unification**: **not yet**. `ExtensionUiService` and `ExtensionRpc`
   stay parallel; folding the UI onto the channel is a possible later
   simplification, not a goal.
6. **Naming**: **`ExtensionRpc`** (service), `beebium.extension` package.

Still genuinely open (can settle during Phase 0/1, not blocking):

- Exact `RpcContext` surface (does Phase 0 need deadline propagation, or is
  cancellation enough?).
- Whether `rpc_dispatchers()` returns one dispatcher per logical service or a
  single dispatcher that names its services - a small ABI ergonomics call.
- Error-code vocabulary: reuse `grpc::StatusCode` values verbatim, or a
  smaller extension-specific set mapped at the edge.

## 17. References

- `docs/discussion/grpc-windows-streaming-race.md` - the Windows #39198
  analysis (ExtensionUiService case) and the co-location proposal.
- `docs/discussion/test-grpc-piconet-ui-windows-av.md` - the same family,
  from the test-fixture side.
- gRPC [#39198](https://github.com/grpc/grpc/issues/39198) - cross-module
  service access violation.
- `src/core/extension-api/` - the existing gRPC-free extension ABI surface
  this builds on.
- `src/service/include/beebium/service/ExtensionUiService.hpp` - the
  in-repo precedent for "core-hosted generic service + plugin ABI".
- Twirp, Connect (connectrpc), Cap'n Proto RPC - protobuf services over a
  non-gRPC transport.
