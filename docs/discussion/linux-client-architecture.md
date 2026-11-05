# Linux client architecture

Status: discussion, with a record of the client as built. Sections 1 to 10
are the proposals and open questions written before the UI existed, with the
spike's measurements; section 11 describes the client as it stands. It is not
a specification: behaviour comes from `docs/client-specification.md`
(extracted from the macOS client), and this note only covers how a Qt 6
client on Linux realises it. Decisions already taken are in
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
(As built: OpenGL ES is the default on Wayland for now, because of a Qt 6.8
crash closing Vulkan windows there; see section 10.)

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

Keys and focus (client specification 7.1 and 13.2, as Rob ruled). The
emulated keyboard wins: while the display has keyboard focus, every key the
active mapping uses goes to the machine, Ctrl shortcuts included, since on a
BBC Micro CTRL with a letter is typing. The display shows that it has focus
with a frame in the desktop's highlight colour, and the status bar says
where keys go ("Keys: machine" or "Keys: Beebium"). Clicking the display
takes focus; Ctrl+Tab and Ctrl+Shift+Tab, which the built-in mappings leave
alone, move focus out of it; F11, Alt with a menu letter and the mouse always
reach the application.

Application shortcuts (Rob, 2026-11-03). The BBC Micro has CTRL and SHIFT but
no Alt and no Meta, and the client cannot hide a key from the machine, so
every machine-window shortcut carries Alt or Meta; none is on Ctrl or Shift
alone (Ctrl+V is VDU 22, Ctrl+S, W and Q are VDU 19, 23 and 17). Meta, the
Super key that the Raspberry Pi 400 prints with a raspberry and other
keyboards with a Windows logo, plays the role Command plays on macOS. Each
action carries two bindings, a Meta form and a Ctrl+Alt form (Meta+K and
Ctrl+Alt+K for Switch to Previous Keyboard Mapping, Meta+digit and
Ctrl+Alt+digit for the sidebar modes); Qt shows only the first in a menu, so
the client orders them by session: Meta first on the Raspberry Pi Desktop
(DESKTOP_SESSION or XDG_SESSION_DESKTOP begins "rpd-"), where Meta is free
apart from labwc's own handful, and Ctrl+Alt first elsewhere, since KDE and
GNOME take Meta+digit, Meta+V and Meta+W for themselves. Menus never show
both; the modifier is named as Qt prints it ("Meta"), not "Raspberry" or
"Win", because a Pi is as likely to have an ordinary keyboard. The display
keeps for the window any non-modifier key pressed with Alt or Meta that the
active mapping does not itself bind with Alt or Meta (Alt+F12 Break and
Alt+F11 Shift Lock stay the machine's), releasing the CTRL or SHIFT the
machine received on the way down. The home window has no machine, so it
keeps the desktop's Ctrl+N, Ctrl+O, Ctrl+Q with the Meta forms beside them.

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

As built, the output is Qt's `QAudioSink` in push mode: the client writes
mixed samples to the sink's device on a worker thread. In pull mode the sink
consumed about two per cent less than 48 kHz on PipeWire, so it is not
used.


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
reader 1.1%, the typing-status watcher 1.2 to 1.4% of one core. Read those
shares with care: gRPC's synchronous API does the connection's transport
work (here mostly receiving 33 MB/s of frames) on whichever threads are
blocked in it, so the work moves between them. Measured directly, with the
watcher not started the client totals 6.5 to 6.6% against 6.8 to 7.2% with
it: the watcher's own cost is about 0.3 to 0.6% of a core.

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
- **Decided: present on arrival at 50 Hz too.** Two ways to avoid the
  periodic drop were considered: presenting the newest frame at each vblank
  when the display rate matches the frame rate, or nudging the server's
  pacing towards the display's. Rob ruled (phase 1 brief): keep
  present-on-arrival everywhere, as the specification requires (6.1) and
  the macOS client does, and accept the occasional dropped frame; no
  vblank-latest or server-pacing work.

### Raspberry Pi 4 class (Pi 400), 2026-11-01

A Pi 400 (BCM2711, VideoCore VI: V3D 4.2.14.0 through `v3dv`, Vulkan 1.3,
OpenGL ES 3.1; Mesa 25.0.7; 3.7 GiB, no swap) running Raspberry Pi OS trixie
with its labwc desktop logged in. Same server, same client build, same
method. QRhi drew on the V3D in every run. 40.9 C before the longest runs and
45.7 C after; never throttled (`get_throttled` 0x0 throughout).

Offscreen, Vulkan (upload, draw, GPU finished):

| Case | Mean | p99 | Client CPU |
|------|------|-----|------------|
| MODE 1, 1920x1080 | 4.57 ms | 9.92 ms | 11.0% |
| MODE 1, 3840x2160 | 14.89 ms | 16.44 ms | 10.5% |
| MODE 7, 1920x1080 | 6.24 ms | 10.96 ms | 14.4% |

Presented, 30 s each:

| Where | API | Arrival | Render interval | Receive to render | Dropped | Client CPU |
|-------|-----|---------|-----------------|-------------------|---------|------------|
| cage 50 Hz | Vulkan | 20.08 ms | 20.09, sd 0.68 | mean 8.4, max 14.4 ms | 8 | 21.7% |
| cage 50 Hz | GLES | 20.84 ms | 20.84, sd 1.00 | mean 0.91, p99 5.1 ms | 6 | 26.5% |
| cage 60 Hz | Vulkan | 19.91 ms | 19.90, sd 0.86 | mean 0.40, p99 0.66 ms | 1 | 20.4% |
| cage 60 Hz | GLES | 20.92 ms | 20.95, sd 1.08 | mean 1.45, p99 14.2 ms | 8 | 27.3% |
| labwc, 59.95 Hz monitor, windowed | Vulkan | 20.32 ms | 20.34, sd 2.06 | mean 0.53, p99 1.7 ms | 1 | 26.3% |
| labwc, 59.95 Hz monitor, windowed | GLES | 20.11 ms | 20.11, sd 1.35 | mean 0.31, p99 0.9 ms | 0 | 21.3% |

What it says:

- **The Pi 4 is sized well for the job.** A 1080p frame costs 4.6 ms of GPU
  time (three times the Pi 5's), 4K still fits in a 20 ms frame at 14.9 ms,
  and the client uses about a fifth of one core (three times the Pi 5's).
  The shader budget for effects at 1080p is therefore roughly 15 ms a frame
  on the Pi 4, before the compositor's own share.
- **Pacing matches the Pi 5's**: locked to the vblank at 50 Hz, drawn on
  arrival at 60 Hz, with somewhat wider spread from the busier CPU.
- **Anomaly: under cage, the GLES runs slowed the server.** Frames arrived
  every 20.8 to 20.9 ms instead of 20.0: the emulator ran at about 96% speed
  while the client drew with GLES in cage. It did not happen with Vulkan in
  cage, nor with GLES under labwc. Unexplained.
- **On the real desktop** (labwc, 2560x1440 at 59.95 Hz, the monitor's mode
  left as it was), both APIs drew every frame on arrival within a
  millisecond and dropped at most one in 30 s.

### Not covered, or found along the way

- Not measured: a server on another machine; a native Xorg session (the
  Pi 400's desktop is labwc, and replacing it was not an option).
- **Qt 6.8.2 crashes closing a Vulkan QRhiWidget window on Wayland.** On
  both Pis, under labwc and cage, on Mesa 25 and 26: segfault, abort or hang
  in `QBackingStoreRhiSupport::SwapchainData::reset` reaching the V3D
  driver's Wayland code (`wl_display_flush` on a dead lock), after Mesa
  warns that its display queue was destroyed with proxies attached. A
  thirty-line QRhiWidget program reproduces it
  (`clients/linux/tools/qrhiwidget-wayland-close`); the same program on
  OpenGL, and this client on Vulkan under X11 (xcb through Xwayland), close
  cleanly. The Pi 5 runs above crashed on exit too, after their
  measurements, unnoticed; the spike's scripts (`clients/linux/tools/spike`)
  now fail any run whose client does not exit cleanly. **Decided:** the
  client defaults to OpenGL ES on Wayland and Vulkan elsewhere, logging the
  choice and why, with `--graphics-api` to choose either; the default flips
  back when a Qt that fixes the crash ships. Qt 6.9 and 6.10: no release
  note found that names this crash or `QBackingStoreRhiSupport`; VLC treats
  Qt before 6.9.0 as destroying a window's Wayland surface on `hide()` and
  works around it, which may be related but was not confirmed (destroying
  the window before it is hidden did not avoid this crash). To be reported
  to Qt with the reproducer, and re-tested on the first newer Qt the
  distribution ships.
- **Shortcut inhibition (6.4).** labwc 0.9.7 does not offer
  `keyboard-shortcuts-inhibit` (it never creates the wlroots manager its
  wlroots provides), and Qt 6.8's Wayland client never requests it. On
  Raspberry Pi OS, then, immersive mode cannot take the compositor's own
  bindings; the specification's rule applies: the exit path is mandatory,
  and the user is never trapped.
- Caps Lock readers on the Pi 400's desktop: the X11 reader (xcb through
  Xwayland) followed `xdotool`'s Caps Lock off, on, off. The Wayland reader,
  against labwc's real seat and the Pi 400's own keyboard pressed by hand
  (Caps Lock on, off, on, off, a few seconds apart), read off, on, off, on,
  off at two-second samples: every transition, none missed or invented.
  (`wtype` cannot test this: its virtual keyboard sends keys and held
  modifiers, never a locked Caps state.) Under cage's headless seat, which
  has no keyboard, it reports the state as unknown. The probe did not count
  the taps sent to the guest; those are covered by the live
  `--caps-lock-test` on the Pi 5.
- Fixed on the way: the Wayland reader asked a seat with no keyboard for
  one, a protocol error that ended the connection (it now waits for the
  seat's keyboard capability); and OpenGL meant desktop GL 3.1 under
  Debian's Qt, which the shaders did not cover (it now requests OpenGL ES,
  and the shaders are baked for GLSL 1.40 too).
- Followed up: the typing-status watcher now asks for status at most every
  250 ms (it had the stream's 20 ms default). That cuts the stream's
  traffic before the guest is ready; CPU was not measurably different.


## 11. As built

The client as it stands, built test-first: about 757 Catch2 test cases, run
headless under the offscreen platform. Sections 1 to 9 describe how it is
built; this section records what it presents, and where it follows the macOS
client or departs from it.

### The machine window and its eight modes

A machine window is the display, a sidebar and a status bar. The sidebar has
the eight modes of client specification 13.1, in its fixed order, each always
offered:

| Mode | What it shows |
|------|---------------|
| Storage | The disc controller's drives with their discs, eject (safe, cancel, force), recent and blank discs, drop targets; storage that peripherals provide (hard and RAM discs) beneath |
| Memory | The sideways sockets, highest priority first, each ROM by the title the server reads from its header, write-protect and hide by the server's protection groups |
| Peripherals | The live peripheral tree from `PeripheralExtensionService`, nested under the server's providers, an Unattached section for orphans, and each extension's own panel |
| Video | Display style (Standard, Debug), pixels (Authentic, Crisp), boxing colour and edge margin, per window |
| Sound | The group mixer: master, a sub-master per group, its channels, and buffer health |
| Keyboard | The mappings, the active one's labelled keys, and Caps Lock synchronisation |
| Processor | Emulation speed on a log2 scale, a caret marking the speed achieved, and Unlimited |
| Network | Econet status, the station settled or pending, collisions, the transport's panel, and the pause a real-time transport imposes |

A mode watches its streams only while it is shown (`showEvent` starts them,
`hideEvent` ends them), so switching modes releases the extension
subscriptions the specification requires. When the server is lost, every
mode shows the same unavailable banner and dims its content.

### Shortcuts as shipped

The rule of section 3 holds: every machine-window shortcut carries Meta or
Alt, never Ctrl or Shift alone, and each has a Meta form and a Ctrl+Alt
form, the Meta form shown first on the Raspberry Pi Desktop. The home window
has no machine, so it keeps the desktop's Ctrl forms. The three copies of the
screen's text are the exception: they carry Alt, the selection modifier, with
the selection's own qualifiers, and have one binding each on every desktop.

| Command | Meta form | Other form |
|---------|-----------|------------|
| New Machine... (home) | Meta+N | Ctrl+N |
| Open Machine... (home) | Meta+O | Ctrl+O |
| Connect to Machine... (home) | Meta+Shift+O | Ctrl+Shift+O |
| Configure Beebium... (home) | Meta+, | Ctrl+Shift+, |
| Save | Meta+S | Ctrl+Alt+S |
| Save As... | Meta+Shift+S | Ctrl+Alt+Shift+S |
| Close Window | Meta+W | Ctrl+Alt+W (Ctrl+W at home) |
| Quit | Meta+Q | Ctrl+Alt+Q (Ctrl+Q at home) |
| Paste | Meta+V | Ctrl+Alt+V |
| Copy | Alt+C (one binding) | |
| Copy as Columns | Alt+Meta+C (one binding) | |
| Copy Text from Graphics | Alt+Shift+C (one binding) | |
| Switch to Previous Keyboard Mapping | Meta+K | Ctrl+Alt+K |
| A sidebar mode | Meta+1 to Meta+8 | Ctrl+Alt+1 to Ctrl+Alt+8 |
| Show Sidebar | Meta+Alt+S | Ctrl+Alt+0 |
| Show Status Bar | Meta+/ | Ctrl+Alt+/ |
| Enter Immersive Mode | Meta+Shift+F | Ctrl+Alt+F |
| Minimize | Meta+M | Ctrl+Alt+M |
| Show Beebium (the home window) | Meta+Shift+H | Ctrl+Alt+H |

F11 is the desktop's full screen. Rename Machine... and Move To... have no
shortcut. Alt+Meta+C is the macOS client's Option+Command+C.

Alt and the menu bar. Qt Widgets' menu bar arms its keyboard mode on a lone
Alt press and takes focus when Alt comes up, so the key-up goes to the menu
bar, not the display. With the display focused, a lone Alt or Meta is
overridden there, so the menu bar never arms from it; Alt with a menu's
mnemonic letter (Alt+E for Edit) still opens that menu.

### Extension views, and the configurator that reuses them

`ExtensionViewWidget` renders an extension's view (`extension_ui.proto`) by
control type, recursively: the eleven primitives, each a widget named by its
control id and kept across views, so a new view patches the panel in place
rather than rebuilding it. Every action carries the revision of the view on
show and is committed before it is sent; nothing changes until the server's
next view says so. A stream that ends with NOT_FOUND, OK or CANCELLED drops
the view; any other loss keeps it, marked stale.

The configurator reuses the renderer rather than having forms of its own. A
`ParameterFormModel` builds a `beebium::View` from an extension's manifest
(the schema's catalogue entry) over a part of the machine draft, and applies
the renderer's actions to the draft instead of sending them to a server.
`ParameterForm` puts the two together. So the same widget renders an Econet
transport's live panel in the Network mode and its parameters in the New
Machine dialog.

### The machine draft and the preset library

The configurator's model is Qt-free, in `src/preset` (`beebium_linux_preset`,
which links only nlohmann_json and the generated protos):

- `PresetDocument` and `MachineDraft`: the document `resolve-preset --json`
  returns, edited in place, each part judged changed against the resolved
  baseline, serialised whole for a launch or a save.
- `PresetResolution`: the resolved document and its diagnostics (severity,
  JSON-pointer path, location, message); an error blocks Create.
- `PresetSchema`: `describe-preset-schema` read as the editors need it:
  controllers, sideways sockets and their aliasing, Econet station bounds,
  attachment points, the extension catalogue, bundled images.
- Per-section models, each tested against the server's own output:
  `SidewaysDraft` (sockets, move up and down, revert), `ExtensionsDraft` (the
  draft's extensions as a tree of attachment points, attached as the server
  attaches them), `HardDiscsDraft`, `StartupLinks` (the startup byte seen
  four ways), `EconetStation`, `StructuredItem` (a list parameter's items),
  and `ParameterFormModel`.

`PresetResolver` (in the application layer) asks the server again 300 ms
after the last edit and drops any answer a later edit has overtaken. The New
Machine dialog lays the sections out in the sidebar's order, launches the
draft as a temporary preset through the same launcher a preset card uses,
and can save it as a new preset with `import-preset`. Settings > Machines is
the user's preset library: the built-in and the user's presets by release
date then name, with Duplicate (`create-preset --from`), Export
(`export-preset`), Delete (`delete-preset`) and Open Containing Folder.

### Revealing a file

Copy Path and Open Containing Folder appear wherever the client names a
file: a drive's disc, a socket's ROM, a preset. `FileReveal` asks the file
manager to show the file selected, with the freedesktop
`org.freedesktop.FileManager1.ShowItems` call that Nautilus, Dolphin, Nemo,
Thunar and PCManFM-Qt answer, and opens the containing folder when nothing
answers. The Raspberry Pi Desktop's file manager, PCManFM 1.4 (GTK), does
not offer the interface, so on a Pi the folder opens without the file
selected. A path is revealed only when the server shares this filesystem and
the path exists here (`frontend-local-server-gating.md`); otherwise the
command is disabled with the reason as its tooltip.

### Selecting and copying screen text

Client specification 6.5, as the macOS client's selection
(`screen-text-selection-ui.md`) with the platform's changes. All recognition
is the server's; the client never reads pixels.

The gesture. A plain drag stays the machine's. The macOS client selects on a
Command-drag, but on Linux both reference desktops take a Super-drag to move
the window (labwc binds it in its default `rc.xml`, mutter by its
`mouse-button-modifier`; labwc matches a binding's modifiers exactly, so
Super with others held is not taken). The selection modifier is Alt, which
the BBC Micro lacks: an Alt drag selects rows, Alt+Meta columns and Alt+Shift
anywhere, Shift winning; Alt itself and Ctrl qualify nothing. The qualifiers
are read from every mouse and key event as it arrives, a modifier key's own
flag set by its press and cleared by its release, never kept from presses and
releases, so a release that went elsewhere leaves nothing latched. After the
button comes up the selection stays live and the same rule previews the
reading: Meta held for columns, Shift for anywhere, nothing for rows. While a
selection is live its modifier keys are tracked but never sent, so Shift and
Ctrl, which are BBC keys, do not reach the machine; beginning one also
releases what reached the machine before Alt went down. Whether or not a
selection is live, a modifier key pressed with Alt held is the host's, and
Alt with any key the mapping does not bind with Alt is the window's; keys a
mapping binds with Alt (Logical's Alt+F12 for Break) stay the machine's.
Escape and a plain click end a selection and reach nothing; other keys still
type.

The hold. A drag freezes the display at once (the stream's frames wait in the
mailbox, the latest replacing the rest), asks `HoldScreen` for the still and
shows it in place of the stream, then puts the drag onto that still, which
may be a frame or two later than the picture under the pointer. Every read
names the hold. A refused hold, or a read answered `NOT_FOUND` because the
hold has expired, ends the selection rather than reading the live screen
behind a frozen picture. A new drag, Escape, a plain click, losing the
connection, connecting afresh and closing the window each release the hold
and let the display run on. The calls run on a worker of their own, so a slow
read never delays the window's other calls.

Geometry. `PictureMapping` maps view points to frame pixels and back as the
exact inverse of the display's own fit: it is built from the same
`make_display_uniforms` the renderer draws with, so the quad, the edge margin
or the CRTC borders, and each split-screen band's pixel width are the
renderer's numbers, not a second copy of them. Its two inputs are kept apart:
the content is the held still's shape, the presentation is the widget's size
and video settings as they are now, so a resize or a style change mid-
selection lays the same still out afresh and the highlight stays registered.
A point in the boxing maps to nothing; a point in a margin or border clamps to
the active edge. The selection's own geometry (`ScreenSelection`: the band a
drag snaps to, the rows flow and the full-width region read for it, the trim
to the flow, which cells to light) is Qt-free and carries the macOS client's
test cases.

Copying. Edit has Copy, Copy as Columns and Copy Text from Graphics directly
beneath Paste, and Mode 7 Copies As (Character Codes, Displayed Characters)
beneath them: a client setting each copy reads afresh, kept as
`screenText/mode7Characters` (`codes` or `displayed`; anything else reads as
codes), so every window follows a change at once. Rows are rebuilt from the
runs, one line per row of the selection, blank rows kept; columns and
anywhere take the server's text as it comes. With no selection, Copy and Copy
as Columns read the whole live screen aligned, and Copy Text from Graphics
reads it anywhere, the only way to copy a whole screen of text drawn at the
graphics cursor. The clipboard gets plain text with LF line endings. A copy
ends one of three ways besides succeeding: nothing to copy (no text, or only
blank rows) is a beep; a read the server refuses is a "Could Not Copy"
dialog with its words beneath; a hold that has expired ends the selection
with a beep, the display running on.

The overlay is a transparent child of the display that takes no input,
painted in the palette's highlight: the range at a quarter, the cells that
will copy at a half on top, and for anywhere the marquee as a one-pixel
outline. A small label in the same tint names the reading, Rows, Columns or
Anywhere, and follows the qualifiers as they change. It sits behind the corner
the drag began from, clear of the selected cells, flipping to the selection's
other side or beside it where the widget's edge leaves no room.

### Delivering a build to the Pi 400

Builds for the Pi 400 are Release builds made on the Pi 5 build host from a
copy of the working tree, and copied to the Pi 400, never built there. Each
check stops the copy if it fails:

1. the Release build's own tests pass;
2. the protocol fingerprint in `ProtocolFingerprint.hpp` appears in the
   binary. A copy of the tree that missed that header once shipped a client
   with a stale fingerprint, which every current server refused;
3. `beebium-display-bench`, under `QT_QPA_PLATFORM=vkkhrdisplay`, connects
   to the server release the Pi 400 runs, draws real frames for two seconds
   and saves one as a PNG.

The bench's server is started for the check and stopped after it, whatever
the outcome. Nothing is run on the Pi 400 as part of a delivery.
