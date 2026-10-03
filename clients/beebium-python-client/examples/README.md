# Beebium Python client — examples

Small, focused programs, one per API area, meant to be **read** as well as run.
Each self-launches a server (or attaches to one you started) via the shared
`_demo.run(...)` helper, then exercises a single part of the client so its style
is easy to judge in isolation.

## Running

```bash
cd clients/beebium-python-client

# self-launch a server for the duration (needs a built server + MOS ROM)
uv run python examples/lifecycle.py \
    --server ../../build/src/server/beebium-model-b \
    --mos ../../roms/acorn-mos_1_20.rom

# or attach to a server you started yourself
uv run python examples/lifecycle.py --port 50071
```

With a full checkout, `--mos`/`--basic` default to the ROMs under `<repo>/roms`
and `--server` falls back to `$BEEBIUM_SERVER` / `PATH`.

## Core APIs

| Example | Shows |
|---------|-------|
| `lifecycle.py` | connect/launch, emulated-time run helpers, machine identity and name template |
| `debugging.py` | debugger stop/step, breakpoints, CPU registers |
| `memory_access.py` | bus vs peek, ranges, typed casts, named regions |
| `keyboard_and_screen.py` | typing into BASIC, reading the MODE 7 screen |
| `basic_programs.py` | running a BASIC program and reading its output |
| `video_capture.py` | display config + frame capture (PNG via `imaging` extra) |
| `audio_streaming.py` | audio format + sample-chunk streaming |
| `disc_drives.py` | disc controller/drive status (`--image` to mount) |
| `sideways_roms.py` | sideways ROM/RAM slot topology |
| `device_inspection.py` | VIA/CRTC/Video ULA/sound/latch/indicators |
| `serial_demo.py` | full serial (ACIA) round trip via rpc-serial |

## Extensions

Adapters are split by the server service that discovers them:
**peripheral extensions** (attach to a bus/port) via `bbc.extensions`, and
**Econet transports** (provide the Econet wire) via `bbc.transport`. Both
facades list what's loaded and bridge it to a typed adapter; `Adapter.attach(bbc)`
works for either.

| Example | Facade | Shows |
|---------|--------|-------|
| `extension_discovery.py` | both | both registries, adapter groups, typed vs generic access |
| `ext_rpc_serial.py` | `bbc.extensions` | rpc-serial adapter (send/receive/status) |
| `ext_host_serial.py` | `bbc.extensions` | host-serial bridge config |
| `ext_acorn_rtc.py` | `bbc.extensions` | acorn-rtc clock read/set/registers |
| `ext_acorn_scsi.py` | `bbc.extensions` | acorn-scsi target and bus inspection |
| `ext_aun.py` | `bbc.transport` | AUN peer table and cable state |
| `ext_piconet.py` | `bbc.transport` | Piconet adapter status |

Import paths mirror the split: `beebium.ext.peripheral.<name>` and
`beebium.ext.econet.<name>`.
