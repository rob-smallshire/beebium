# Controllers and the joystick: round 4 specification (#98)

Round 3 gave the Model B family an analogue port with a device seam (`analogue-port-seam-specification.md`).
This round answers the question "where does joystick input come from?" and ships the first device that
uses the answer: a plain two-stick joystick adapter in the analogue port, driven over gRPC by a test
client today and by the GUI clients' host gamepads in later rounds.

## The architectural decision

Beebium already divides I/O by who is at the other end of it. Everything a person touches or perceives
crosses the gRPC boundary: the keyboard in, video and audio out. The client owns the host hardware and the
mapping from host terms to BBC terms; the server owns the emulated device and speaks only in that
device's terms. Everything that is about where data goes rather than who is at the desk is a
server-side extension: discs, printers, serial, Econet, and the host-device sinks that need the server's
filesystem.

`Decision`: joysticks, handsets, mice and light pens are in the first group. They are **controllers**: a
controller is a device a person holds or points, and a controller publishes **controls**, each a named
axis or button. A client sets controls through one core service, `ControllerService`, in the way it
presses BBC keys through `KeyboardService`. The server never reads a host gamepad.

`Decision`: the vocabulary is the device's, not the port's and not a generic gamepad's. A joystick adapter
publishes two sticks and two fire buttons. A Voltmace Delta 14B/1 (round 5) publishes two handsets, each
a stick and twelve keys by their printed labels. A light pen (#210) publishes `x`, `y` and `switch`. The
client maps host controls onto the published ids, as it maps host keys onto BBC key names, and never
knows what a device is. This keeps the GUI clients device-agnostic and gives the user one mapping layer.

The port seams are untouched and remain the only thing an analogue or user port device must implement.
A device that takes its stimulus from the server host, such as a datalogger replaying a file or reading
a sensor, implements only the port seam and never appears in `ControllerService`. The two seams are
orthogonal; a device implements whichever apply.

| Device | AnaloguePortDevice | UserPortDevice | Controller | Round |
|---|---|---|---|---|
| Joystick adapter (two sticks, two fire buttons) | yes | no | yes | 4 |
| Voltmace Delta 14B/1 adapter | yes | yes | yes | 5, after research |
| Light pen | yes | no | yes | #210 |
| Datalogger, sensor, waveform source | yes | no | no | no plan |

## Principles

Those of rounds 2 and 3 apply: datasheet and hardware first, no MOS dependence in the model (MOS 1.20
only as a test vector), test first, `Decision` marks a choice this document makes. In addition:

- **Hot path answers from plain members.** `button_lines()` is called on every System VIA tick and
  `channel_level()` at every conversion start. A controller stores its controls in atomics written from
  the gRPC thread and loaded on the emulation thread, exactly as `KeyboardMatrix` stores its columns.
  No queue, no pause, no mutex.
- **A control is a physical thing someone is holding.** Its value persists across Break and across a
  power cycle (`Hardware::reset()`). Nothing in a controller resets.
- **The server does no interpretation.** No dead zone, no inversion, no calibration, no auto-fire. Those
  are the client's, as keyboard mapping is.

## The controller seam

`src/core/include/beebium/extension/Controller.hpp`, `BEEBIUM_EXT_API`:

```cpp
enum class ControlKind { Axis, Button };

struct ControlDescription {
    std::string id;            // stable, 7-bit ASCII, fixed by the device: "stick1.x", "fire1"
    std::string display_name;  // "Stick 1 X", "Fire 1"
    std::string group;         // "Stick 1", "Handset 2"; empty for a flat device
    ControlKind kind;
};

struct Controller {
    virtual ~Controller() = default;
    virtual std::string_view controller_id() const = 0;             // the extension instance id
    virtual std::string_view display_name() const = 0;              // "Joystick"
    virtual std::span<const ControlDescription> controls() const = 0;
    virtual uint16_t axis(std::string_view id) const = 0;           // 0..65535
    virtual bool button(std::string_view id) const = 0;             // true = pressed
    virtual void set_axis(std::string_view id, uint16_t value) = 0; // throws std::invalid_argument on an unknown id
    virtual void set_button(std::string_view id, bool pressed) = 0; // likewise
};
```

`Decision`: axes are 16-bit, 0 to 65535, centre 32768, the scale `AnaloguePortDevice::channel_level`
already uses, so a stick's axis is its channel level with no conversion. Buttons are `true` when pressed;
the device turns that into whatever its lines carry (a joystick adapter grounds PB4, so `button_lines()`
clears bit 4). `Decision`: ids are dotted ASCII identifiers chosen by the device and never renamed by a
user. Mapping files refer to ids. `Decision`: `set_*` on an unknown id throws; the service turns that into
InvalidArgument.

A registry collects the attached controllers for the service:

```cpp
class ControllerRegistry {
public:
    void add(Controller& controller);      // called by an extension's attach
    void remove(Controller& controller);   // called by its detach, if the framework detaches
    std::vector<Controller*> controllers() const;
    Controller* find(std::string_view controller_id) const;
};
```

One `ControllerRegistry` per machine, owned by the assembled machine beside the extension registry
(not by the hardware policies, since it is per machine and not per variant), passed into
`ExtensionContext` with `get<ControllerRegistry>()` and `has<>()` like the port handles. It exists on every machine, so the
service always has something to list, and an empty list is the answer with no controller attached.
Lookups happen on the gRPC thread only; registration happens at assembly time before the emulation
thread runs, so the registry needs no lock. If the framework can attach after start, say so in the
brief and the registry gets the same treatment the extension framework already uses for that case.

## The service

`src/service/proto/controller.proto`, `ControllerService`. The first comment line defines the word:
"A controller is a device a person holds or points at the screen: a joystick, a handset, a mouse, a
light pen. It publishes named controls, each an axis or a button, and a client sets them."

```proto
service ControllerService {
  rpc ListControllers(ListControllersRequest) returns (ListControllersResponse);
  rpc GetControls(GetControlsRequest) returns (GetControlsResponse);
  rpc SetControls(SetControlsRequest) returns (SetControlsResponse);
}

// ControlInfo and ControlType, not ControlDescription and ControlKind: every proto is in
// package beebium, so the C++ seam's names would clash with the generated types.
message ControlInfo {
  string id = 1;
  string display_name = 2;
  string group = 3;
  ControlType type = 4;
}
enum ControlType { CONTROL_TYPE_UNSPECIFIED = 0; AXIS = 1; BUTTON = 2; }

message ControllerDescription {
  string controller_id = 1;   // the extension instance id, as PeripheralExtensionService names it
  string display_name = 2;
  repeated ControlInfo controls = 3;
}
message ListControllersRequest {}
message ListControllersResponse { repeated ControllerDescription controllers = 1; }

message ControlValue {
  string id = 1;
  oneof value { uint32 axis = 2; bool pressed = 3; }   // axis 0..65535
}
message GetControlsRequest { string controller_id = 1; }
message GetControlsResponse { repeated ControlValue values = 1; }  // every control, in controls() order
message SetControlsRequest { string controller_id = 1; repeated ControlValue values = 2; }
message SetControlsResponse {}
```

Semantics:

- `SetControls` applies every value in the request before returning, in order; a request that names an
  unknown controller returns NotFound, an unknown control id or a value of the wrong kind or an axis
  above 65535 returns InvalidArgument, and in those cases nothing in the request is applied (validate
  first, then store). Error details are sentences that name the controller by the id the client used, never by display
  name: "The controller joystick has no control named stick3.x.", "There is no controller named
  joystick2."
- `GetControls` returns the current value of every control, so a test can read back what it set.
- `Decision`: unary only in this round. A client-streaming form can be added when a GUI client measures
  that it needs one.
- The service is registered on every machine like `KeyboardService`, from the machine's
  `ControllerRegistry`.
- The protocol fingerprint changes: regenerate C++, Python, TypeScript and Swift stubs and bump the
  fingerprint in all four places (`ProtocolFingerprint.hpp`, `protocol_fingerprint.py`,
  `protocol_fingerprint.ts`, `ProtocolFingerprint.swift`), as b82393aa did.

## The joystick

A built-in extension in `BuiltinExtensions.hpp`, for the same CLI-test ergonomics reason as
`file-printer`, with its class in `src/core` beside the port handles or under `src/extensions/joystick/`
if it needs its own translation unit (agent's call; keep it small).

- Manifest: `name = "joystick"`, `display_name = "Joystick"`, `cli_name = "joystick"`,
  `description = "Two analogue joysticks with a fire button each, plugged into the analogue port."`,
  `attaches_to = {"analogue-port"}`, no parameters.
- Implements `AnaloguePortDevice` and `Controller`. Controls, in this order:
  `stick1.x`, `stick1.y` (group "Stick 1"), `fire1` (group "Stick 1"), `stick2.x`, `stick2.y`, `fire2`
  (group "Stick 2"). Display names "Stick 1 X", "Stick 1 Y", "Fire 1", and so on.
- Wiring: `stick1.x` is channel 0, `stick1.y` channel 1, `stick2.x` channel 2, `stick2.y` channel 3;
  `fire1` grounds PB4 (connector PB0), `fire2` grounds PB5 (connector PB1). `light_pen_strobe()` stays 0.
- Initial values: axes 32768 (stick at rest, centre), buttons not pressed. On a real Beeb a centred
  stick reads about &8000, so `ADVAL(1)` with nothing touched is the same number as an empty port.
- Storage: four `std::atomic<uint16_t>` and one `std::atomic<uint8_t>` of button bits;
  `channel_level` and `button_lines` are plain loads.
- `Decision`: not attached by any stock preset. The user adds it in New Machine or with `--joystick`,
  as with a printer.
- A Voltmace Delta handset plugged straight into the analogue port is electrically this device with two
  logical keys, so this round covers that case.

## Documentation

- `docs/analogue-port.md`: a "Controllers" section (the decision, the seam, the service) and a
  "The joystick" section (controls, wiring, initial values, how a test drives it); the "What is
  modelled" table gains the joystick.
- `docs/grpc-server.md` and the service table in `CLAUDE.md`: `ControllerService` / `controller.proto`.
- `docs/peripheral-extension-framework.md`: `joystick` in the built-in extensions list.
- `docs/client-specification.md`: a new section 7.7 "Controllers" (or a short chapter if 7 is strictly
  keys; agent's call, keep the numbering stable elsewhere): clients discover controllers with
  `ListControllers`, map host controls onto the published control ids, send values with `SetControls`,
  apply dead zone and inversion client-side, and never special-case a controller by name. Platform
  detail (GameController on macOS, SDL or evdev on Linux) is for the client rounds; mark it so.
  Appendix A gains the service.
- `docs/howto_write_a_peripheral_extension.md`: a paragraph on implementing `Controller` and registering
  with `get<ControllerRegistry>()`.

## Tests

### Catch2, `tests/test_joystick.cpp`, tag `[joystick]`, 7-bit ASCII names

1. The manifest is in the built-in entries with the name, display name and `attaches_to` above, and
   `--joystick` on the CLI attaches it to a Model B.
2. The controls list is the six controls above in that order with those kinds and groups.
3. Initial values: every axis 32768, every button not pressed; `channel_level(n)` is 32768 for all four.
4. `set_axis("stick1.x", 0x1234)` then a 12-bit conversion on channel 0 gives high `&12`, low `&30`;
   each stick axis reaches its channel.
5. `set_button("fire1", true)` makes a System VIA port B read (DDR bits 4 and 5 inputs) return bit 4
   clear and bit 5 set; `fire2` the other way; both released returns both set.
6. `set_axis` and `set_button` on an unknown id throw `std::invalid_argument` with a sentence naming
   the id.
7. Values survive `soft_reset()` and `Hardware::reset()`.
8. The `[analogue_port]` round 3 stub tests still pass unchanged.

### Catch2, `tests/test_grpc_controller.cpp`, tag `[grpc][controller]`

9. `ListControllers` on a Model B with no joystick returns an empty list; with `--joystick` returns one
   controller whose id is the instance id `PeripheralExtensionService` reports and whose controls are
   the six above.
10. `SetControls` with two axes and a button, then `GetControls`, returns the set values and the untouched
    defaults for the rest.
11. Unknown controller id: NotFound with a sentence. Unknown control id, a `pressed` value for an axis,
    an axis above 65535: InvalidArgument with a sentence, and `GetControls` afterwards shows nothing
    changed.
12. The fingerprint test that compares the four constants still passes after the bump.

### Catch2, Model B with MOS 1.20 as the test vector

13. With the joystick attached and `stick1.x` set to `&F000`, the MOS workspace `&2BA` reads `&F0`
    within 300 ms of the first conversion. With `fire1` pressed, OSBYTE &80 X=0 returns bit 0 set,
    read through a CALL from a disc-loaded BASIC program or the workspace, whichever the round 3 test 11
    used.

### Python, `clients/beebium-python-client/tests/test_controller.py`

14. `machine.controllers.list()` on a `--joystick` Model B returns one controller named "Joystick" with
    six controls; `machine.controllers.set("joystick", {"stick1.x": 0xF000, "fire1": True})` followed by
    `get` returns those values; `ADVAL(1)` from a disc-loaded BASIC program prints a value in
    `&F000..&F0FF` on the screen (screen text library), and `ADVAL(0) AND 1` prints 1.
15. Unknown control: the wrapper raises the client's gRPC error type with the server's sentence.

The Python wrapper is `clients/beebium-python-client/src/beebium/client/controller.py`, in the shape of
`keyboard.py`, exposed as `machine.controllers`. The TypeScript client gets the regenerated stubs and a
matching thin wrapper with one test; the macOS client gets only the regenerated Swift stubs and the
fingerprint bump in this round.

## Delivery

Commits, each with targeted tests green:

1. `Controller`, `ControllerRegistry`, `ExtensionContext` plumbing, the joystick extension with
   tests 1 to 8 and 13.
2. `controller.proto`, `ControllerService`, the fingerprint bump and stub regeneration in all four
   places, tests 9 to 12.
3. Python and TypeScript wrappers with tests 14 and 15, documentation, client specification section.

Full gate from a separate worktree build before reporting the round done. Same rules as rounds 2 and
3: `git commit --only`, never amend, never push, no people or host names, 7-bit ASCII in test names
and tags, stop and ask if a `Decision` proves wrong. Round 5, the Voltmace Delta 14B/1, starts with a
research brief on the adapter's wiring and is not part of this round.
