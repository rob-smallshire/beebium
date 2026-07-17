# How to write a peripheral extension

This guide walks through building a **peripheral extension**: a self-contained
unit that attaches an emulated or host-bridging device to one of the BBC's
hardware attachment points (the serial port, user port, 1MHz bus, Tube, or
SCSI bus), optionally exposing a typed control API and a GUI panel.

It uses the existing extensions as worked examples:

- **rpc-serial** (`src/extensions/rpc-serial/`) — a client-driven serial peer,
  shipped as a dynamically-loaded plugin. The smallest end-to-end example.
- **host-serial** (`src/extensions/host-serial/`) — bridges the serial port to a
  host PTY/device, with a typed config API *and* a GUI panel. Built **in** to
  the server rather than shipped as a plugin.

Read `docs/peripheral-extension-framework.md` for the architecture and
`docs/discussion/extension-rpc-channel.md` for why extension APIs are served the
way they are (the short version is in [Step 3](#step-3-expose-a-typed-api-with-a-dispatcher)).

## The anatomy of an extension

A peripheral extension is usually four small pieces plus build glue:

| Piece | Purpose | Example |
|-------|---------|---------|
| `*Extension` class | The lifecycle object: attaches a device at `init`, tears it down at `shutdown`. | `RpcSerialExtension` |
| A device | The thing that actually talks to the hardware seam (a `SerialPortDevice`, `UserPortDevice`, …). | `RpcSerialEndpoint` |
| `*Dispatcher` (optional) | A typed request/response API, served over the core's ExtensionRpc channel. | `RpcSerialDispatcher` |
| `*Ui` (optional) | A declarative GUI panel (the Peripherals sidebar). | `HostSerialUi` |

You do not need all four. A purely-mechanical device (no scripting API, no UI)
is just the `*Extension` + the device.

## Step 1: subclass `PeripheralExtension`

`PeripheralExtension` (`src/core/include/beebium/extension/PeripheralExtension.hpp`)
has four pure-virtual methods:

```cpp
class MyExtension : public beebium::PeripheralExtension {
public:
    // Which attachment-point handles you need. The framework resolves these
    // and rejects a config that double-occupies a single-occupancy point.
    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"serial-port"};
        return deps;
    }

    // Handles you publish for *other* extensions to attach to. Usually empty.
    std::span<const std::string_view> provides() const override { return {}; }

    // Build your device and attach it to the seam. Called once, before the
    // machine starts ticking.
    void init(beebium::ExtensionContext& ctx) override {
        device_ = std::make_unique<MyDevice>();
        ctx.get<beebium::SerialPort>().attach(*device_);
    }

    // Reverse-order teardown. Drop the device (joins its threads, releases the
    // seam) before anything it referenced.
    void shutdown() override { device_.reset(); }

private:
    std::unique_ptr<MyDevice> device_;
};
```

The attachment-point names are the ubiquitous-language strings from
`docs/sideways-slots.md` / the extension framework: `serial-port`,
`user-port`, `1mhz-bus`, `tube`, `scsi`. `ctx.get<T>()` hands you the typed
handle for a point you declared in `attaches_to()`.

Configuration arrives before `init()`. The framework parses
`--my-extension key=value:key2=value2` (or the equivalent preset entry) into a
map; read it with `config_value("key")`:

```cpp
void init(ExtensionContext& ctx) override {
    std::size_t buffer = kDefault;
    if (auto v = config_value("tx_buffer"); v && !v->empty()) {
        buffer = std::stoul(std::string(*v));
    }
    device_ = std::make_unique<MyDevice>(buffer);
    ...
}
```

See `reference_extension_attachment_points` and `docs/peripheral-extension-framework.md` for
the attachment-point taxonomy and the config/manifest schema.

## Step 2: write the device

The device implements the seam interface (e.g. `SerialPortDevice`). This is
ordinary emulation code and out of scope here — see `RpcSerialEndpoint` for a
queue-backed peer or `HostSerialEndpoint` for a host-port bridge. Two rules that
bite extensions specifically:

- **Never block the emulation thread on an external peer.** Use bounded,
  never-blocking I/O; apply back-pressure through the real chip mechanism (e.g.
  `/CTS`) so an impolite peer stalls the *guest*, never the host. See
  `feedback_no_external_peer_stalls_emulator`.
- **Own your threads.** If the device runs a reader thread, join it in the
  device destructor, and make sure `shutdown()` drops the device before any
  object the thread touches.

## Step 3: expose a typed API with a dispatcher

If clients (Python/TypeScript/Swift, or tests) need to drive your device, give
it a typed API. **Do not host your own gRPC service** — an extension that links
gRPC puts a second gRPC runtime in the process and corrupts it across the
module boundary (gRPC #39198; see the design doc). Instead, the core hosts a
single generic `ExtensionRpc` service and routes calls to plain C++ handlers you
register. Only opaque serialized bytes cross the boundary.

### 3a. Define your messages

Write a `.proto` next to your extension. Keep a `service` block for
documentation, but the gRPC stub is never compiled into your module — only the
messages are.

```proto
// my_extension.proto
syntax = "proto3";
package beebium;

service MyService {           // documents the contract; not compiled to a stub
    rpc DoThing(DoThingRequest) returns (DoThingResponse);
}
message DoThingRequest  { uint32 amount = 1; }
message DoThingResponse { uint32 accepted = 1; }
```

### 3b. Write the dispatcher

A dispatcher is an `ExtensionRpcDispatcher`
(`src/core/include/beebium/extension/ExtensionRpc.hpp`). It names a logical
service and switches on the method, (de)serializing your own messages:

```cpp
#include <beebium/extension/ExtensionRpc.hpp>
#include "my_extension.pb.h"   // messages only

class MyDispatcher final : public beebium::ExtensionRpcDispatcher {
public:
    explicit MyDispatcher(MyDevice& device) : device_(device) {}

    std::string_view service_name() const override { return "MyService"; }

    beebium::RpcStatus invoke(std::string_view method, std::string_view request,
                              std::string& response,
                              beebium::RpcContext&) override {
        if (method == "DoThing") {
            DoThingRequest req;
            if (!req.ParseFromArray(request.data(), (int)request.size())) {
                return beebium::RpcStatus::error(
                    beebium::kRpcInvalidArgument, "malformed DoThing request");
            }
            DoThingResponse resp;
            resp.set_accepted(device_.do_thing(req.amount()));
            resp.SerializeToString(&response);
            return beebium::RpcStatus::ok();
        }
        return beebium::RpcStatus::error(
            beebium::kRpcUnimplemented,
            "MyService has no method '" + std::string(method) + "'");
    }

private:
    MyDevice& device_;
};
```

`RpcStatus` maps to a gRPC status at the boundary. Two conventions worth
copying from the existing dispatchers:

- A **malformed request** is the one case that returns a non-OK `RpcStatus`
  (`kRpcInvalidArgument`). Everything maps cleanly to a gRPC status code.
- **Domain "failures"** (validation, "device not active") can be reported
  *in-band* — a `success`/`error` field in your response with an OK status —
  if your client checks that field. `AunDispatcher` does this; `RpcSerial`
  does not. Pick one per response type and be consistent.

`server_stream()` is available for server-streaming methods (status feeds,
event taps). Unary `invoke()` is enough for most extensions.

### 3c. Hand the dispatcher to the framework

Override `rpc_dispatchers()` (declared on the `Extension` base) to return your
handler. Construct it lazily after `init()` so it can reference the device:

```cpp
std::vector<beebium::ExtensionRpcDispatcher*> rpc_dispatchers() override {
    if (!device_) return {};                 // init() not yet called
    if (!dispatcher_) dispatcher_ = std::make_unique<MyDispatcher>(*device_);
    return {dispatcher_.get()};
}
```

> `rpc_dispatchers()` is the modern hook. The base also still has a legacy
> `grpc_services()` returning `{}`; it is being retired as extensions move to
> the channel. New extensions should not override it.

## Step 4 (optional): a GUI panel

For a sidebar panel in graphical frontends, return an `ExtensionUi*` from
`ui()`. The UI is a declarative control tree (`View` of `Label`, `Indicator`,
`Button`, `ModalEditor`, `EditableChoice`, …) that the framework streams to
frontends and dispatches validated events back into. `HostSerialUi` is the
reference; see `docs/discussion/extension-ui-architecture.md`. Prefer an Indicator (state) + Button
(action) over a Toggle when the state can change for reasons beyond the user's
click (`feedback_state_vs_action_controls`).

The typed dispatcher (Step 3) and the UI (Step 4) are **not** redundant — they
serve different audiences (scripting vs. humans). See
`feedback_extension_multi_api`.

## Step 5: build it

### Plugin or built-in?

- **Plugin** (default; rpc-serial): a `SHARED` library discovered from
  `<exe-dir>/extensions/<name>/` at runtime. Add a `plugin_entry.cpp` exporting
  the C ABI factory:

  ```cpp
  extern "C" {
  BEEBIUM_PLUGIN_EXPORT
  beebium::Extension* beebium_create_extension(
          const beebium::ExtensionManifest& manifest) {
      auto* ext = new beebium::MyExtension();
      ext->set_manifest(manifest);
      return ext;
  }
  }
  ```

- **Built-in** (host-serial, aun): a `STATIC` library linked into the server,
  listed in `BuiltinExtensions`. Choose this only when CLI-parsing tests need
  the flag recognised without a plugin directory (the reason host-serial and
  AUN are built in).

### CMake: messages, not gRPC

Compile the proto **messages only** — never the `.grpc.pb.cc` stub, and never
link `gRPC::grpc++`. The plugin links protobuf for its own messages and
`beebium_extension_api` for the dispatcher ABI:

```cmake
if(BEEBIUM_BUILD_SERVICE)
    beebium_compile_proto(TARGET beebium_ext_my
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/my_extension.proto
        PROTO_PATH  ${CMAKE_CURRENT_SOURCE_DIR})
    add_library(beebium_ext_my_proto OBJECT ${beebium_ext_my_PROTO_SRCS})  # _PROTO_SRCS, NOT _GRPC_SRCS
    target_include_directories(beebium_ext_my_proto PUBLIC ${beebium_ext_my_OUT_DIR})
    target_link_libraries(beebium_ext_my_proto PUBLIC protobuf::libprotobuf)
    set_target_properties(beebium_ext_my_proto PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

add_library(beebium_ext_my_plugin SHARED MyExtension.cpp plugin_entry.cpp)
target_link_libraries(beebium_ext_my_plugin PRIVATE beebium_extension_api)
if(BEEBIUM_BUILD_SERVICE)
    target_compile_definitions(beebium_ext_my_plugin PUBLIC BEEBIUM_BUILD_SERVICE)  # see ODR caveat
    target_link_libraries(beebium_ext_my_plugin PRIVATE beebium_ext_my_proto)
endif()
set_target_properties(beebium_ext_my_plugin PROPERTIES PREFIX "" OUTPUT_NAME "my-extension")
beebium_finalize_plugin(TARGET beebium_ext_my_plugin NAME my-extension)
```

> **ODR caveat (this one bites).** If your extension's *public header* gates a
> member on `#ifdef BEEBIUM_BUILD_SERVICE` (e.g. `unique_ptr<MyDispatcher>
> dispatcher_;`), the flag **must** be `PUBLIC`, so every consumer that
> compiles the header (tests that link the plugin) sees the same class layout.
> A `PRIVATE` flag means the plugin compiles a bigger object than the test —
> different `sizeof`, and constructing in one module while destroying in
> another corrupts the heap (`pointer being freed was not allocated`). The
> simplest alternative is to keep the member unconditional. See the piconet
> port for the bug and the fix.

Add the proto to the client codegen scripts so the messages reach the clients:
`clients/beebium-python-client/scripts/generate_proto.sh` and
`clients/beebium-typescript-client/scripts/generate-protos.sh`.

## Step 6: consume it from a client

Clients reach your API through the one ExtensionRpc channel, never a
per-extension stub. The wrapper encodes its request, calls
`channel.invoke(service, method, payload)`, and decodes the reply.

Python (`clients/beebium-python-client/src/beebium/rpc_serial.py` is the model):

```python
from beebium._proto import my_extension_pb2
from beebium.extension_rpc import ExtensionChannel

class MyExtensionClient:
    def __init__(self, channel: ExtensionChannel):
        self._channel = channel

    def do_thing(self, amount: int) -> int:
        req = my_extension_pb2.DoThingRequest(amount=amount)
        reply = self._channel.invoke("MyService", "DoThing", req.SerializeToString())
        resp = my_extension_pb2.DoThingResponse()
        resp.ParseFromString(reply)
        return resp.accepted
```

Wire it in `Client` via `MyExtensionClient(ExtensionChannel(self._connection.extension_rpc_stub))`.
A non-OK `RpcStatus` surfaces as the `grpc.RpcError` it maps to.

TypeScript is the same shape with `ExtensionChannel` from
`clients/beebium-typescript-client/src/extension_rpc.ts` and ts-proto `encode`/`decode`
(remember `Msg.encode(Msg.fromPartial({...})).finish()` — `encode` on a bare
partial throws on unset string fields). Swift consumes the same generated
messages over the same channel.

## Step 7: test it

Two complementary levels, both cheap:

- **Dispatcher unit test** — drive the dispatcher directly with a tiny
  `RpcContext`, asserting the (serialize → invoke → parse) round-trip and the
  error cases (unknown method → `kRpcUnimplemented`, bad bytes →
  `kRpcInvalidArgument`). See `tests/test_rpc_serial_extension.cpp`.
- **End-to-end channel test** — register your extension in a registry, stand up
  the real `ExtensionRpcServiceImpl` over an in-process gRPC server, and call
  `ExtensionRpc.Invoke` through a stub. See `tests/test_grpc_aun_service.cpp`
  for the pattern and `tests/test_grpc_extension_rpc.cpp` for the harness.

Client wrappers get a unit test that mocks the channel and asserts the tunnelled
request decodes to the right fields (`tests/test_aun.py`,
`clients/beebium-typescript-client/tests/aun.test.ts`).

## Gotchas checklist

- Plugins **never** link gRPC. Messages only; `beebium_extension_api` for the ABI.
- Plugins are never `dlclose`d — they stay mapped for process lifetime
  (`feedback_plugin_no_dlclose`). Don't rely on unload-time cleanup.
- Adding/removing a virtual on the `Extension` base is an **ABI break** — every
  plugin must be rebuilt against the host. They always are (built together).
- Use the domain term (the extension's name) in user-facing CLI/APIs, not
  "plugin" (`feedback_ubiquitous_language`).
- See also: `howto_write_an_econet_transport.md` for the transport variant.
