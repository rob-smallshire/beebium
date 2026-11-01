# Linux client architecture

Status: discussion. Proposals and open questions for the Linux client
(`clients/linux`), written before any UI exists. It is not a specification:
behaviour comes from `docs/client-specification.md` (being extracted from the
macOS client), and this note only covers how a Qt 6 client on Linux would
realise it. Decisions already taken are in
`raspberry-pi-linux-client-discussion.md`: Qt 6 Widgets, KDE-HIG-style
interaction, the window manager owns decorations, Raspberry Pi OS (64-bit,
Pi 4 and Pi 5) is the reference environment, a separate display renderer on
Vulkan with OpenGL ES as fallback, and remote servers are first class.

The binding front-end rules are not repeated here; every section below names
the one it must satisfy: `lifecycle-management.md`, `frontend-modifier-keys.md`,
`frontend-local-server-gating.md`, `frontend-sleep-wake.md`,
`sound-subsystem.md`, `video-subsystem.md`, `peripheral-extension-framework.md`,
`machine-directory.md`.


## 1. Process and threading model

One client process, one or more machine windows, each window owning one
connection (one `grpc::Channel`) to one server, local or remote. This mirrors
the macOS client and keeps "close the window" equal to "drop the connection".

Proposed threads per connection:

| Thread | Owns | Hands off to |
|--------|------|--------------|
| GUI (Qt main) | widgets, QRhi rendering, all UI state | -- |
| video reader | blocking `ClientReader<Frame>::Read` loop | latest-frame mailbox |
| audio reader | `ClientReader<AudioChunk>` loop | SPSC sample ring |
| status reader | `WatchServerStatus`, heartbeat watchdog input | GUI (queued signal) |
| other streams | indicators, drive status, Econet, extension views | GUI (queued signal) |
| send worker | one serial queue for unary calls (keys first) | server |
| audio callback | real-time pull from the ring (PipeWire's thread) | sound server |

Reasoning:

- **Video is latest-wins, audio is a queue.** A frame that has been
  superseded before the GUI thread got to it is worth nothing; a single-slot
  mailbox (swap a pointer under a tiny lock, or an atomic exchange) plus one
  coalesced "frame ready" event to the GUI thread means a slow GUI drops frames
  instead of queueing latency. Audio must not drop, so it goes through a ring
  with the macOS client's adaptive pre-roll (section 4).
- **The GUI thread never blocks on the network.** Unary calls go through the
  send worker; results return as queued signals. This also gives the
  serialised, generation-ordered key sends that `frontend-modifier-keys.md`
  section 3 requires, for free.
- **Synchronous gRPC with one thread per stream** rather than the callback
  (reactor) API: a client holds perhaps eight streams, the code is far easier
  to reason about, and cancellation is `ClientContext::TryCancel()`. The
  completion status then distinguishes our own cancellation (`CANCELLED`,
  ignore) from loss (anything else), which is lifecycle rule 3.
- **No shared event-loop-group trap.** The macOS "one long-lived NIO group,
  never shut down" rule is a gRPC-Swift/SwiftNIO workaround. gRPC C++ owns its
  own threads; the equivalent rule here is only "destroy channels and join
  reader threads off the GUI thread, after `TryCancel`".
- **Liveness only from `WatchServerStatus`** with the 2 s watchdog over the
  0.5 s heartbeat, never from the frame stream (lifecycle rule 1), and the
  same four states: active, stopped, died, unreachable.

Open question: should the reader threads be plain `std::thread` or `QThread`?
Proposal: `std::thread` in a Qt-free `connection` library (testable without a
QApplication), with the Qt adaptor owning signal emission.


## 2. Display renderer

### Surface

Proposal: a `QRhiWidget` subclass (Qt 6.7+; Debian trixie and Pi OS trixie
ship 6.8.2) as the emulator display inside a `QMainWindow`, with
`setApi(Vulkan)` preferred and OpenGL ES as fallback, chosen at start-up by
what the platform offers and overridable by a setting or environment variable.

Things to know about `QRhiWidget` that the spike must measure rather than
assume:

- A top-level window that contains a `QRhiWidget` composites its whole widget
  tree through the 3D API: menus, sidebars and status bar become textures
  blended with the display. That is cheap on the Pi 5; on the Pi 4 it is
  bandwidth the shader budget also wants.
- The alternative is a `QWindow` driving `QRhi` directly, embedded with
  `QWidget::createWindowContainer`. It owns its own swapchain and present
  timing, and avoids the compositing pass, but window containers have focus and
  stacking quirks under Wayland. Keep it as the fallback if the spike shows
  compositing cost or pacing problems.

### Frames

`Frame.pixels` is BGRA32, active area only, `width * height * 4` bytes, with
borders, `display_width`/`display_height`, `field_order` and per-scanline
`regions` alongside. Roughly 0.65 MB a frame, 33 MB/s at 50 Hz.

- Upload into an `RGBA8` texture and swizzle `.bgra` in the fragment shader.
  This avoids depending on BGRA texture support, which Vulkan has but GLES
  only has by extension; one code path for both backends.
- Recreate the texture only when `width`/`height` change (mode change).
- Geometry (aspect from `display_width`/`display_height`, the 0.96 or 1.0
  pixel aspect, borders, split-screen regions) lives in shader uniforms as in
  the macOS Standard style.
- Shaders written once in Vulkan-flavoured GLSL 440 and baked at build time
  to `.qsb` (SPIR-V plus GLSL ES variants) with `qt_add_shaders`; nothing is
  compiled at run time.

### Pacing

Present on frame arrival: each new frame calls `update()`. Never drive
presentation from a free-running timer or a vsync callback that a sleeping
display could halt (`frontend-sleep-wake.md`, display sleep). Swapchain FIFO
then throttles to the display.

50 Hz frames on a 60 Hz panel judder by construction; the macOS client lives
with the same. The fixes (a 50 Hz display mode in full screen, or frame
blending) are later work and are display-mode policy, not renderer design.

### Raspberry Pi 4 versus Pi 5

Both GPUs are VideoCore (V3D 4.2 on the Pi 4, V3D 7.1 on the Pi 5), both run
OpenGL ES 3.1, and current Mesa `v3dv` exposes Vulkan on both; the 1.0 versus
1.2 split in the discussion note looks out of date and is to be confirmed with
`vulkaninfo` on each board rather than taken from anywhere else. What the
Pi 4 can and cannot do, to be checked against measurements:

- Can: the trivial scaler at any desktop resolution; a single-pass CRT-style
  shader with a handful of texture taps at 1080p.
- Probably cannot: multi-pass effects (bloom, phosphor persistence via
  render-to-texture) at 1080p and 50 or 60 Hz; anything at 4K. The V3D is a
  tile-based renderer whose cost is memory bandwidth, so every extra pass and
  every load of a previous attachment is expensive; use `DontCare` load ops and
  no readbacks.
- So: shader effects come in tiers, and the Pi 4 is the sizing target for the
  default tier.

### Later: an appliance mode

Qt can run a Widgets application with no desktop at all (the `eglfs` platform
on DRM/KMS, GLES only). That is the "dedicated BBC Micro terminal" from the
discussion note, and it is where a 50 Hz video mode becomes natural. Not phase
0, but the renderer must not assume a compositor.


## 3. Keyboard

The rules are `frontend-modifier-keys.md` R1 to R10; this section is only about
what Qt on Linux gives us to satisfy them.

- **Physical identity.** `QKeyEvent::nativeScanCode()` is the XKB keycode on
  both the xcb (X11) and Wayland platform plugins: the Linux evdev code plus 8.
  That is layout-independent and the right key for the held-key set (R1) and
  for positional ("game") mappings.
- **Left and right modifiers (R3)** are separate keycodes with their own press
  and release events. Linux is simpler than AppKit here: there is no coalesced
  `flagsChanged` to decode.
- **Auto-repeat (R7):** `QKeyEvent::isAutoRepeat()`.
- **Characters.** `QKeyEvent::text()` is the layout-and-modifier-applied text,
  which is what character-mapped resolution against the server's
  `GetAllKeyMappings` wants. With CTRL held, R9 requires the unmodified
  character; `QKeyEvent::key()` gives the base symbol in that case. To be
  verified on dead-key layouts, where `text()` is empty on the dead press; the
  display widget must disable input methods (`Qt::WA_InputMethodEnabled`
  off) so a compose sequence cannot swallow keys.
- **Focus loss (R4):** release everything synchronously in `focusOutEvent` and
  on window deactivation; Qt delivers both on the GUI thread, so there is no
  hop to avoid.
- **Caps Lock** is reported in `QKeyEvent::nativeModifiers()` (the XKB Lock
  bit) on both platforms, but only while we have focus. Host-to-guest Caps
  sync is therefore possible on focus and key events; reading it while
  unfocused is not.

What Wayland forbids, by design, and so what we must not build on:

- no global key capture or grabs, and no keyboard state while unfocused;
- no synthesising input into the host, and no setting the host's lock state;
- compositor key bindings (Super, Alt+Tab, labwc's own bindings) are taken
  before the client sees them. The `keyboard-shortcuts-inhibit` protocol lets a
  focused full-screen client ask for them; whether Qt 6.8 and labwc support it
  is an open question for the immersive mode.

Raspberry Pi OS on the Pi 4 and 5 now defaults to labwc on Wayland, with X11
selectable. Both must be tested; the code path is the same.

Mapping files. The macOS mapping JSON already keeps per-platform sections and
preserves the ones it does not understand (`"macOS": {"keyCode": 6}`). The
minimal Linux addition is a `"linux": {"evdev": 44}` section. A better
question for the specification: should mappings key on a platform-neutral
physical code, such as the W3C UI Events `code` names (`"KeyZ"`), which both
macOS key codes and evdev codes translate to by fixed tables? Then one mapping
file serves every client. Proposal: raise it with the specification work; ship
the evdev table either way, because the translation table is needed in both
designs and is unit-testable.


## 4. Audio

Rules from `sound-subsystem.md`: samples arrive as `source_count` 32-bit fields
per 48 kHz frame, described by `GetAudioFormat` (encodings, channels, groups,
bipolar or unipolar). The client removes DC (the macOS renderer high-passes at
10 Hz), scales, mixes per group with sub-masters, and never hides a group.

Proposal:

- Port the macOS pieces nearly one for one, as Qt-free C++ with unit tests: the
  SPSC ring, the adaptive pre-roll policy from #126 (120 ms target, +60 ms per
  underrun up to 480 ms, held for the session), sample unpacking per encoding,
  the DC high-pass, the group mixer.
- Output through PipeWire directly (`pw_stream`, F32 stereo, 48 kHz, pull
  callback on PipeWire's real-time thread). Raspberry Pi OS uses PipeWire.
  Qt 6.8's `QAudioSink` on Linux goes through PulseAudio (pipewire-pulse) and
  gives less control over buffer sizes and the callback thread.
- Behind a small `AudioOutput` interface, so a `QAudioSink` implementation can
  serve distributions without PipeWire.

Not phase 0; the skeleton has no audio.


## 5. Server discovery and connection

- Discovery: link `src/discovery` (`beebium_discovery`) directly. It is plain
  C++20 with no dependency on the emulator core, its Linux browser is Avahi
  loaded with `dlopen`, and it already browses `_beebium._tcp`. This is a real
  saving over the macOS client, which has its own `NWBrowser` code. Callbacks
  arrive on a background thread and are marshalled to the GUI thread.
- Remote servers: a connect dialog that takes host and port, plus the
  discovered list, as on macOS.
- Protocol fingerprint check at connect against `GetSystemInfo`, refusing a
  mismatch. The client compiles in the same `ProtocolFingerprint.hpp` the
  server uses, so there is no fifth constant for
  `scripts/sync_protocol_fingerprint.py` to keep in step.
- Local-server gating: `SystemInfo.host_fingerprint` compared with our own,
  derived from `/etc/machine-id` by the same `HostFingerprint.hpp` the server
  uses, exposed to the rest of the client as one `isServerLocal` boolean,
  failing closed and reset on disconnect. The audit of what to gate must be
  redone over the Linux client's own file dialogs and drops, as that document
  says.
- Local launch: the macOS client spawns servers it finds in the installed
  layout (`deployment.md`). The Linux client does the same with
  `QProcess`, with server stderr captured off the GUI thread (cf. the macOS
  64 KiB pipe deadlock).

Sleep and wake: the Linux full-system-sleep signal is logind's
`org.freedesktop.login1.Manager.PrepareForSleep(bool)` on the system D-Bus:
`true` before suspend, `false` after resume. Display blanking does not emit it,
which is exactly the distinction `frontend-sleep-wake.md` requires. It becomes
the Linux conformer of the `SystemPowerMonitoring` abstraction, via QtDBus;
the reconnect policy (0.5 s doubling to 8 s, wake as accelerant, never fight a
graceful stop) is ported as a pure, unit-tested class.


## 6. Build

- `clients/linux` is a CMake project in its own right, like `clients/macos` is
  an Xcode project in its own right, so the client can be built and packaged
  without building the emulator core. It compiles the protos from
  `src/service/proto` (and `extension_ui.proto` from
  `src/core/extension-api/proto`) at build time with the same
  `cmake/BeebiumProto.cmake` the servers use; generated code never enters the
  tree.
- It reuses, by path, `src/discovery`, `ProtocolFingerprint.hpp` and
  `src/host-identity` (`beebium_host_identity`, the header-only host
  fingerprint recipe, free of the emulator core).
- Dependencies on Raspberry Pi OS come from the distribution: Qt 6.8, gRPC
  1.51 and protobuf 3.21 (the Linux server already builds against these via the
  pkg-config fallback), OpenSSL, Avahi headers, Vulkan and GLES headers.
- Tests: Catch2 v3, matching the repository, for everything that is not a
  widget (frame geometry, key tables and the modifier derivation, audio
  unpacking and pre-roll, reconnect policy, mapping-file parsing), runnable
  headless with `QT_QPA_PLATFORM=offscreen` where Qt types are involved.

### Continuous integration (proposal for phase 1)

No workflow builds `clients/linux` yet. Proposed: one job in `ci.yml`, two
lanes, each configuring `clients/linux` on its own (not the root project),
building it and running its ctest. The client needs Qt 6.7 or later
(`QRhiWidget`), and that decides where each lane runs.

| Lane | Runner | Dependencies |
|------|--------|--------------|
| arm64 | `[self-hosted, Linux, ARM64]` (the Pi 5) | Pre-provisioned on the host, as for `server-linux-arm64`: Qt 6.8.2 base, private and shader tools, gRPC 1.51, protobuf 3.21, OpenSSL, Vulkan headers |
| x86-64 | `ubuntu-latest` running a `debian:trixie` container | `apt-get install` per run: `qt6-base-dev qt6-base-private-dev qt6-shadertools-dev libgrpc++-dev protobuf-compiler-grpc libprotobuf-dev libssl-dev cmake ninja-build g++ pkg-config libvulkan-dev libgl-dev` |

Points behind the proposal:

- **Not plain `ubuntu-latest` apt.** Ubuntu 24.04 ships Qt 6.4, too old for
  `QRhiWidget`. A `debian:trixie` container gives the same Qt, gRPC and
  protobuf as Raspberry Pi OS, so x86-64 also builds against the reference
  distribution's versions. An Ubuntu runner image with Qt 6.7 or later
  (26.04) would do as well, once GitHub offers one; installing Qt with
  aqtinstall instead would test a Qt no distribution package ships.
- **Light on the Pi.** The client has no heavy translation unit (the
  servers' memory ceiling does not apply), and a clean build plus tests takes
  a few minutes. `CMAKE_BUILD_PARALLEL_LEVEL: "2"`, as the server job uses,
  keeps headroom anyway. The single runner serialises it with the server job.
- **Headless only.** Every test runs under the offscreen platform and
  in-process fake services; nothing in CI needs a GPU or a display. A later
  stage could run `beebium-display-bench --key-test` (and a Vulkan frame
  bench under `QT_QPA_PLATFORM=vkkhrdisplay`) against the server the arm64
  server job builds; that is an end-to-end smoke, not part of this proposal.
- **Triggered by what it builds from.** Path filters: `clients/linux/**`,
  `src/service/proto/**`, `src/core/extension-api/proto/**`,
  `src/service/include/beebium/service/ProtocolFingerprint.hpp`,
  `src/host-identity/**`, `cmake/BeebiumGrpc.cmake`,
  `cmake/BeebiumProto.cmake`, and the workflow itself.
- **Warnings.** The client builds with `-Wall -Wextra` and has none; the job
  should keep it that way (`-Werror` on the client's own targets only, not on
  generated code).
- **Retry configure** as the server jobs do: Catch2 comes from FetchContent.


## 7. Packaging

- Raspberry Pi OS: a `.deb` for trixie arm64, dynamically linked against the
  distribution's Qt and gRPC (Qt is not something to bundle into a distro
  package), dependencies from `dpkg-shlibdeps`. It installs a desktop entry
  (`com.beebium.Beebium.desktop`, the macOS bundle identifier's form, matching
  the application's desktop file name; the executable is `beebium`),
  hicolor icons generated from the Shape of Beebium generator, an AppStream
  metainfo file, and shared-mime-info definitions for `.ssd`, `.dsd` and `.hfe`
  (no freedesktop types exist for them; glob plus, for `.hfe`, the
  `HXCPICFE`/`HXCHFEV3` magic) and for the `.beebium-machine` document file
  inside a machine directory (`machine-directory.md`; the core already writes
  KDE's `.directory` folder metadata there). It Recommends the server package
  rather than containing it.
- Generic Linux: later, as an AppImage or Flatpak carrying its own Qt. Flatpak
  needs care: its sandbox gives the client a different filesystem view from a
  local server's, so `isServerLocal` would be true while paths do not mean the
  same thing. That breaks an assumption `frontend-local-server-gating.md`
  makes and must be resolved before a Flatpak ships.


## 8. What to take from the macOS client, and what not

Worth copying, as behaviour and often as structure:

- Server-driven UI: extension views, configurator, sidebars rendered from
  `ExtensionUiService` and peripheral descriptions, dispatching by control type.
- The keyboard model: held-key set, frozen per-press facts, derived synthetic
  SHIFT/CTRL (`BBCModifierState`), one send queue, release-all on focus loss.
- The liveness model and its overlays, the reconnect coordinator as a pure
  policy, sidebar availability and staleness derived from one window-level
  verdict.
- Present-on-frame-arrival.
- Display styles as data (pixel shape, borders) feeding one renderer.
- Adaptive audio pre-roll, the per-group mixer with sub-masters.
- The machine window as a document over a machine directory, the server owning
  its bytes.
- `isServerLocal` as one boolean, failing closed.
- Keyboard mapping files with per-platform sections and game mappings.

Not worth copying, because they solve macOS or Swift problems:

- `WindowCloseCoordinator` redirecting the close button: a SwiftUI workaround.
  `QWidget::closeEvent` is the real hook on Linux.
- The never-shut-down NIO event loop group: a gRPC-Swift workaround.
- Reading AppKit device-dependent modifier bits: Linux reports each modifier
  key separately.
- Cmd-based shortcuts, the macOS Settings scene, Touch Bar support.
- Any custom-drawn control where a stock Qt widget exists: native widgets,
  no Beebium widget theme.
- An immersive mode built from window-style tricks. The behaviour carries over
  (display fills the client area, aspect kept, reliable exit, distinct from F11
  full screen); the implementation is Qt window states plus, perhaps, shortcut
  inhibition.


## 9. Open questions

1. Platform-neutral physical key codes in mapping files (section 3), or a
   per-platform section per client?
2. `QRhiWidget` or a window container (section 2): decided by the spike's
   measurements on the Pi 5, then confirmed on a Pi 4.
3. Does labwc honour `keyboard-shortcuts-inhibit`, and does Qt 6.8 request it?
   Immersive mode depends on the answer.
4. PipeWire directly, or `QAudioSink` first and PipeWire when needed?
5. Flatpak and local-server gating (section 7).
6. Appliance mode on `eglfs`: in scope for the Pi build, or a separate later
   product?


## 10. Phase 0 spike: what it must measure, and what it found

The brief: on the Pi 5 (and later the Pi 4), connected to a local
`beebium-model-b` and to one on another machine, measure client CPU per
thread at 50 Hz frames on Vulkan and GLES; frame pacing (inter-arrival, and
arrival to present) as histograms; whether presentation is smooth under labwc
and X11; and that keys reach the machine through the hardware path with SHIFT
derived.

### Setup (2026-11-01)

- Raspberry Pi 5, 8 GiB, Debian 13, Qt 6.8.2, Mesa 26.2.2 (rpt). Every run
  logged QRhi's device: V3D 7.1.7.0, never llvmpipe. Vulkan 1.3 through
  `v3dv`; OpenGL ES 3.1.
- Server: the released v0.15.0 `beebium-model-b`, local, in MODE 1 (50
  progressive frames a second; MODE 7 sends 25, each a composited pair of
  fields, by design).
- Presentation: `cage` 0.3.1 on the wlroots headless backend
  (`WLR_BACKENDS=headless`, GLES2 renderer on the V3D), its virtual
  1920x1080 output set to 50 or 60 Hz with `wlr-randr`; the client full
  screen in it on the Wayland platform. Runs of 30 s, on an otherwise idle
  machine.
- Offscreen: `beebium-display-bench`, the same renderer drawing into a
  texture under `QT_QPA_PLATFORM=vkkhrdisplay` (Vulkan with no display),
  timing upload, draw and the wait for the GPU.

### Results

Frames arrived every 20.0 ms (sd 0.5 to 0.6 ms) in every run.

Cost of a frame, offscreen, Vulkan (upload, draw, GPU finished):

| Target | Mean | p99 | Client CPU (one core) |
|--------|------|-----|-----------------------|
| 1920x1080 | 1.56 ms | 1.66 ms | 3.8% |
| 3840x2160 | 5.04 ms | 5.10 ms | 3.8% |

Presented in cage, 1920x1080, 30 s each:

| Output | API | Render interval | Receive to render | Frames dropped | Client CPU |
|--------|-----|-----------------|-------------------|----------------|------------|
| 50 Hz | Vulkan | 20.03 ms, sd 0.17 | mean 9.4, p95 18.0, max 19.6 ms | 4 | 7.0% |
| 50 Hz | GLES | 20.03 ms, sd 0.06 | mean 9.6, p95 17.3, max 19.8 ms | 5 | 7.8% |
| 60 Hz | Vulkan | 20.00 ms, sd 0.56 | mean 0.15, p99 0.18, max 7.8 ms | 2 | 7.2% |
| 60 Hz | GLES | 19.99 ms, sd 0.55 | mean 0.12, p99 0.13, max 9.3 ms | 2 | 7.8% |

Histograms, 1 ms buckets (the 50 Hz Vulkan run; GLES alike):

    render interval     [14,15):1 [19,20):64 [20,21):1450 [21,22):1
    receive to render   [0,1):96 [1,2):94 [2,3):90 [3,4):95 [4,5):94 [5,6):81
                        [6,7):66 [7,8):60 [8,9):62 [9,10):59 [10,11):58
                        [11,12):57 [12,13):58 [13,14):96 [14,15):125
                        [15,16):95 [16,17):44 [17,18):114 [18,19):59 [19,20):14

and at 60 Hz (Vulkan):

    render interval     [16,17):2 [19,20):941 [20,21):540 [21,22):1 [26,27):1
    receive to render   [0,1):1484 [3,4):1 [7,8):1

CPU per thread, in cage: the render (GUI) thread 4.2 to 5.2%, the video
reader 1.1%, the typing-status watcher 1.2 to 1.4% of one core.

Keyboard: typing `PRINT 6*7` through the client's own keyboard path (synthetic
Qt key events with real scan codes, `*` as a physical Shift+8) read back
`42` from the screen; Caps Lock synchronisation tapped the guest only when it
disagreed and read back the wanted latch each time.

### What the numbers say

- **The Pi 5 has headroom to spare.** A frame costs 1.6 ms of GPU time at
  1080p and 5 ms at 4K; the whole client uses under a tenth of one core.
  Vulkan and GLES are equivalent for this load; Vulkan stays the default.
- **Pacing is the display's, not the client's.** On a 50 Hz output the
  render locks to the vblank (sd under 0.2 ms) and a frame waits for the next
  one: latency spreads evenly over 0 to 20 ms, because the server's 50 Hz
  and the display's 50 Hz are separate clocks whose phase drifts. Where two
  frames fall into one vblank, one is dropped: 4 or 5 in 30 s, about one
  every 7 s. On a 60 Hz output a frame is drawn as it arrives (0.1 ms), and
  the 50-in-60 judder is the display's, as on the Mac.
- **Next for smoothness at 50 Hz** (not phase 0): present the newest frame
  at each vblank rather than on arrival when the display rate matches the
  frame rate, or nudge the server's pacing towards the display's (the
  server already re-anchors its pacing clock). Either removes the periodic
  drop.

### Not covered, or found along the way

- Not measured: a server on another machine; labwc (Raspberry Pi OS's
  compositor) and X11, since trejon has no desktop session (cage is a
  wlroots compositor like labwc, so the Wayland path is the same one); the
  Pi 4.
- The Wayland Caps Lock reader runs under cage without harm, but a headless
  seat has no keyboard, so it reports the state as unknown; nothing could
  type into it (no `wtype`). The X11 reader is untested: there is no
  Xwayland on trejon. Both need a desktop session.
- Fixed on the way: the Wayland reader asked a seat with no keyboard for
  one, a protocol error that ended the connection (it now waits for the
  seat's keyboard capability); and OpenGL meant desktop GL 3.1 under
  Debian's Qt, which the shaders did not cover (it now requests OpenGL ES,
  and the shaders are baked for GLSL 1.40 too).
- To look at: the typing-status watcher's 1.2% of a core. It asks for the
  stream's default 20 ms interval; the Caps Lock sync it serves needs far
  less.
