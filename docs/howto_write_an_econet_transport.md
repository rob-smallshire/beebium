# How to write an Econet transport

An **Econet transport** carries the BBC's Econet traffic to the outside world.
Where a [peripheral extension](howto_write_a_peripheral_extension.md) attaches a
device to a hardware seam, a transport supplies a `NetworkBackend` that the
emulated ADLC/Econet socket reads from and writes to. At most one transport is
active at a time, chosen by the server's configuration.

The two existing transports are the worked examples:

- **AUN** (`src/extensions/aun/`) — Econet-over-UDP (Acorn Universal Networking),
  with peer-table management, a GUI panel, and mDNS peer discovery. Built **in**
  to the server.
- **Piconet** (`src/extensions/piconet/`) — a USB-CDC bridge to a real Econet
  wire. Shipped as a **plugin**. POSIX-only.

Most of the mechanics — the typed dispatcher API, the GUI panel, the CMake
"messages not gRPC" rule, the client wrappers, and the tests — are identical to
a peripheral extension. **Read that guide first**; this one covers only what is
different for a transport.

## The shape of a transport

A transport subclasses `EconetTransportExtension`
(`src/core/include/beebium/extension/EconetTransportExtension.hpp`) rather than
`PeripheralExtension`. Its defining method is `create_backend`:

```cpp
class MyTransportExtension : public beebium::EconetTransportExtension {
public:
    // Build the backend from the current config. Return nullptr if the
    // transport is configured but unavailable (missing parameter, open
    // failed) -- the server then installs a disconnected stub, and the UI
    // surfaces the reason. The owning unique_ptr is handed off to
    // EconetSocket; keep a *non-owning* raw pointer for your UI / dispatcher.
    std::unique_ptr<beebium::NetworkBackend> create_backend(uint8_t station) override {
        auto path = config_value("device_path");
        if (!path || path->empty()) {
            open_error_ = "missing 'device_path'";
            return nullptr;
        }
        auto backend = std::make_unique<MyBackend>(std::string(*path), station);
        backend_ = backend.get();          // non-owning; lives in EconetSocket
        return backend;
    }

    MyBackend* backend() { return backend_; }            // for UI + dispatcher
    beebium::ExtensionUi* ui() override { return &ui_; } // optional panel

    // Transport-specific RPCs, served over the ExtensionRpc channel.
    std::vector<beebium::ExtensionRpcDispatcher*> rpc_dispatchers() override;

private:
    MyBackend* backend_ = nullptr;   // non-owning; EconetSocket owns it
    std::string open_error_;
    MyUi ui_{*this};
};
```

Key differences from a peripheral extension:

- **No `attaches_to()` / `init()` / `shutdown()`.** A transport doesn't occupy a
  hardware attachment point; the framework calls `create_backend()` and installs
  the result into the Econet socket.
- **The backend is owned elsewhere.** `create_backend` *transfers ownership* of
  the `NetworkBackend` to `EconetSocket`. Keep only a raw `backend_` pointer for
  your UI and dispatcher to read. It becomes dangling at machine shutdown (which
  only happens at process exit), so never dereference it from a destructor.
- **`create_backend` returning `nullptr` is a normal state**, not an error —
  "configured but the cable's unplugged". Record a human-readable reason
  (`open_error_`) for the UI to show; the dispatcher's status RPC should report
  it too.
- **Override `requires_real_time_pacing()` if your transport bridges to a
  real-time peer.** It defaults to `false` — any emulation speed is fine, as for
  AUN over UDP. Return `true` (as Piconet does) when the transport talks to real
  hardware sharing a wall-clock timebase; the server then gates the transport off
  whenever the emulation speed is not 1x, because its protocol timing cannot
  interleave with real peers at any other rate. See `docs/networking.md`
  ("Emulation speed and real-time peers").

## The backend

`NetworkBackend` is the transport seam: the emulated Econet socket pushes frames
to it and polls it for inbound frames, on the emulation thread. Implement it
with the same discipline as any device — bounded, never-blocking I/O on the
emulation thread, threads owned and joined cleanly. `AunBackend` and
`PiconetBackend` are the references.

If host and parasite-style asynchrony is involved, remember the clock-domain
rule: peers run on independent clocks and the bridge is asynchronous
(`feedback_tube_clock_domains` is the analogous case for the Tube).

## Transport-specific RPCs

Give the transport a typed API exactly as for a peripheral extension: a `.proto`
(messages only), an `ExtensionRpcDispatcher`, and `rpc_dispatchers()`. The
dispatcher reads through the non-owning `backend()` pointer and reports the
unavailable state when it is null:

```cpp
beebium::RpcStatus invoke(std::string_view method, std::string_view request,
                          std::string& response, beebium::RpcContext&) override {
    if (method == "GetStatus") {
        MyGetStatusResponse resp;
        if (auto* b = extension_.backend()) {
            resp.set_device_path(b->config().device_path);
            resp.set_open(b->is_open());
        }                                  // else: all-zero "unavailable" defaults
        resp.SerializeToString(&response);
        return beebium::RpcStatus::ok();
    }
    ...
}
```

The core's `ExtensionRpcServiceImpl` routes a call to your dispatcher by its
`service_name()` across **both** the peripheral and transport registries, so the
service name must be unique. AUN registers `"AunService"`, Piconet
`"PiconetService"`. Worked examples: `AunDispatcher` (peer table, cable plug,
status — five unary methods, with in-band success/error) and `PiconetDispatcher`
(a single status method).

> Both AUN and Piconet previously hosted their own gRPC services. That put a
> second gRPC runtime in the plugin and corrupted the heap when a streaming
> call crossed into the DLL on Windows (see
> `docs/discussion/grpc-windows-streaming-race.md`). Serving the RPCs over the
> shared channel removed that whole class of failure — which is the reason the
> Piconet status RPC, stripped during that incident, is back.

## Discovery (optional)

A transport can advertise and browse for peers (AUN does this over mDNS:
`AunDiscoveryAnnouncer` / `AunDiscoverySubscriber`, see
`project_aun_mdns_discovery`). Discovered entries should be marked as such so a
client can distinguish them from operator-configured ones — `AunPeer.source`
carries `DISCOVERED` vs `OPERATOR_CONFIGURED`, and operator entries take routing
precedence. mDNS support is platform-dependent (macOS bidirectional, Windows
advertise-only, Linux none today); guard accordingly.

Do **not** assume a single transport in shared infrastructure — a future Acorn
Econet Bridge machine type would run two ADLCs at once
(`project_econet_bridge_aspiration`).

## Built-in vs plugin

- **Plugin** (Piconet): a `SHARED` library with a `plugin_entry.cpp`, discovered
  from `<exe-dir>/extensions/`. The same C-ABI factory as a peripheral plugin.
- **Built-in** (AUN): a `STATIC` library linked into the server, listed in
  `BuiltinExtensions`. AUN is built in for the same CLI-test ergonomics reason
  as host-serial: `parse_start_arguments` tests use a synthetic `argv` that
  can't auto-discover a plugin directory, so `--aun` must be recognised without
  one.

The CMake is the peripheral recipe verbatim: compile the `.proto` **messages
only** (`_PROTO_SRCS`, never `_GRPC_SRCS`), link `protobuf::libprotobuf` +
`beebium_extension_api`, never `gRPC::grpc++`. The same **PUBLIC
`BEEBIUM_BUILD_SERVICE`** ODR caveat applies to any plugin whose header gates a
member on the flag — see the peripheral guide. (A built-in static lib like AUN
already exports the flag `PUBLIC` and keeps the member unconditional, so it's
safe either way.)

## Clients and tests

Identical to a peripheral extension:

- **Client wrappers** tunnel through `ExtensionChannel`
  (`clients/beebium-python-client/src/beebium/aun.py`, `clients/beebium-typescript-client/src/aun.ts`). They
  expose the transport API and are reached via
  `bbc.aun` / `bbc.piconet` once the transport is active. Check
  `bbc.transport.active` first — the RPC returns an error / empty state when the
  transport isn't the configured one.
- **End-to-end test**: register the transport in an `EconetTransportRegistry`,
  stand up the real `ExtensionRpcServiceImpl`, and drive
  `ExtensionRpc.Invoke` (`tests/test_grpc_aun_service.cpp`).
- **Dispatcher unit test**: drive the dispatcher with no backend to exercise the
  unavailable-state path and the error cases
  (`tests/test_piconet_dispatcher.cpp`).
- **Client unit tests** mock the channel with real serialized protobuf
  (`tests/test_aun.py`, `clients/beebium-typescript-client/tests/aun.test.ts`).

## Checklist

- `create_backend` returns the backend by value (ownership → `EconetSocket`);
  keep only a non-owning pointer.
- `nullptr` from `create_backend` is "unavailable", not a crash — record a
  reason for the UI and status RPC.
- Unique `service_name()` across all extensions; routed by the core, not hosted
  by you.
- Messages only, no gRPC, in the transport module.
- Mark discovered peers distinctly; don't assume a single transport everywhere.
