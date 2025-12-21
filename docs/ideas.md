# Beebium architecture refactor

**User:** Anonymous (robert@smallshire.org.uk)  

## Prompt:
Ok, I’m interesting in radically refactoring BeebEm into Beebium. A fork. Entirely about improving the software architecture and system qualities,that than functionality. In particular I’d like a core, headless server for the emulation part, with pluggable emulated peripherals. The front end, which can be platform specific will run in a separate process. So, rather than using cross-platform libraries like SDL or Allegro for the front-end, platform native libraries can be used. It will also be important to be able to use the emulator in an entirely headless mode.

Chromium is something of an inspiration here, so I need to learn more about how that is put together, at least the parts relevant to this task.

## Response:
That’s a very solid direction, and _Beebium_ is a good name for it. What you’re describing is essentially **turning an emulator into a distributed system with strong internal contracts**, which is exactly the kind of architectural leap Chromium made.

I’ll break this into four parts:

1.  **A concrete target architecture for Beebium**
2.  **How Chromium actually structures this (relevant bits only)**
3.  **Process model and IPC choices**
4.  **What to study in Chromium without drowning**

* * *

1\. A concrete target architecture for Beebium
----------------------------------------------

A useful mental model is:

> _“The BBC Micro is a machine. Beebium is an operating environment for BBC Micros.”_

### High-level process split

```
+---------------------------+
|  beebium-core (server)    |
|---------------------------|
|  - CPU (6502)             |
|  - Memory map             |
|  - Scheduler / timing     |
|  - VIA / ULA / CRTC       |
|  - Disc controller        |
|  - Peripheral registry    |
|                           |
|  NO UI                    |
|  NO audio backend         |
|  NO OS APIs               |
+-------------+-------------+
              |
        IPC boundary
              |
+-------------+-------------+
|  Frontend(s)               |
|---------------------------|
|  - Video presentation     |
|  - Audio output           |
|  - Keyboard/mouse input   |
|  - Debug UI               |
|  - Native platform APIs   |
+---------------------------+
```

### Key design properties

*   **Core is deterministic**
*   **Core has no concept of pixels, windows, or sound devices**
*   **Frontends are replaceable**
*   **Peripherals are plugins**
*   **Headless mode is just “no frontend attached”**

This immediately gives you:

*   CI-friendly testing
*   Batch emulation
*   Remote control
*   Scriptability
*   Debug tooling that doesn’t pollute the emulator

* * *

2\. Chromium’s architecture (the parts that matter)
---------------------------------------------------

Chromium is massive, but you only need **three ideas** from it.

### 2.1 The Browser / Renderer split (conceptual)

Chromium separates:

| Chromium | Beebium equivalent |
| --- | --- |
| Browser process | Emulator core |
| Renderer process | UI frontend |
| GPU process | Optional accelerated display/audio |
| IPC contracts | Emulator protocol |

The browser process:

*   Owns _state_
*   Owns _policy_
*   Owns _lifecycle_

The renderer:

*   Is sandboxed
*   Crashes safely
*   Can be restarted

**For Beebium:**  
If a frontend crashes, the emulation keeps running.

* * *

### 2.2 Mojo: interfaces-first IPC

Chromium does **not** think in terms of “send messages”. It thinks in terms of:

> _“What interfaces exist between processes?”_

Example (simplified):

```cpp
interface VideoSink {
  OnFrame(FrameData frame);
}

interface InputSource {
  SendKey(KeyEvent event);
}
```

The transport (pipes, shared memory, etc.) is secondary.

**This is the big lesson:**  
Design your IPC _as if it were a local API_.

* * *

### 2.3 Service-oriented decomposition

Chromium decomposes functionality into **services**:

*   Audio service
*   GPU service
*   Network service

They can be:

*   In-process
*   Out-of-process
*   Restarted
*   Replaced

For Beebium, that maps neatly to:

*   Video service
*   Audio service
*   Storage service
*   Debug service

* * *

3\. Process model and IPC choices for Beebium
---------------------------------------------

You don’t need Mojo or Chromium’s complexity. But you _do_ want the same _shape_.

### 3.1 Recommended process model

Start simple:

```
beebium-core
    |
    +-- frontend (local process)
    |
    +-- debugger (optional)
```

Later:

```
beebium-core
    |
    +-- SDL frontend
    +-- Cocoa frontend
    +-- Wayland frontend
    +-- Web frontend (WebSocket)
```

### 3.2 IPC transport options (pragmatic)

**Good initial choices:**

| Transport | Why |
| --- | --- |
| Unix domain sockets | Fast, debuggable, cross-language |
| Shared memory (for video/audio) | Performance |
| JSON or Cap’n Proto | Human-readable vs efficient |

A common pattern:

*   Control messages → structured IPC
*   Frame/audio buffers → shared memory ring buffers

### 3.3 Define contracts early

Example IPC surfaces:

```text
EmulatorControl
  - load_rom()
  - reset()
  - pause()
  - step()

VideoStream
  - subscribe()
  - frame_ready(shm_handle, metadata)

Input
  - key_down()
  - key_up()

Debug
  - read_memory()
  - disassemble()
```

These **must not leak implementation details**.

* * *

4\. How to study Chromium without drowning
------------------------------------------

Chromium is a trap if approached whole. Instead:

### 4.1 Focus only on these subsystems

1.  **Process model**
    *   `content/`
    *   Browser vs Renderer responsibilities
2.  **Mojo interfaces**
    *   IDL-based API design
    *   Interface versioning
3.  **Service lifecycle**
    *   How services start, stop, reconnect

Ignore:

*   HTML
*   Blink
*   JavaScript engines
*   Networking
*   GPU drivers

### 4.2 Read these concepts, not code first

Search terms:

*   “Chromium multi-process architecture”
*   “Chromium Mojo IPC overview”
*   “Chromium services architecture”

Only then read code _to confirm the idea_.

* * *

5\. A Beebium-specific insight
------------------------------

BBC Micro emulation is actually _ideal_ for this architecture:

*   Frame rates are low
*   Audio bandwidth is tiny
*   Determinism matters
*   Debugging is first-class

You can:

*   Run the emulator at 10× speed headless
*   Attach a debugger frontend later
*   Replay entire executions from event logs
*   Run regression tests against known ROM output

That’s hard in BeebEm today, but _natural_ in Beebium.

* * *

6\. Suggested next concrete steps
---------------------------------

1.  **Define the core/frontend boundary on paper**
2.  Extract BeebEm’s emulation loop into a UI-free library
3.  Write a trivial frontend:
    *   No graphics
    *   Just logs frames and key events
4.  Introduce IPC
5.  Only _then_ write a real UI

If you want, next we can:

*   Sketch a Beebium IPC IDL
*   Map BeebEm subsystems onto services
*   Design a plugin model for emulated peripherals
*   Compare against jsbeeb and MAME architecture

This is a _very_ worthwhile fork.

## Prompt:
So video seems like the most difficult part. How would this work? The emulator core renders into image buffers (frames) and these frames are transferred over IPC to subscribing front-ends?

## Response:
Yes — _conceptually_ that’s exactly right — but the devil (and the architectural leverage) is in **how little video the core actually “knows” about**.

The key Chromium-inspired insight is:

> **The emulator core should not “render frames”.  
> It should publish _state changes_ over time, and optionally expose a _frame producer_ service.**

Let’s walk this carefully, from naïve → correct → elegant.

* * *

1\. The naïve model (what _not_ to stop at)
-------------------------------------------

> _Core renders full frames → sends them over IPC → frontend blits them_

This works, but has problems:

*   High bandwidth (even at BBC Micro resolutions)
*   Forces the core to care about:
    *   Pixel formats
    *   Frame timing
    *   VSync-ish semantics
*   Makes headless/debug uses awkward
*   Couples video rate to emulation rate

So: **acceptable prototype, poor final shape**.

* * *

2\. The correct baseline model (good first milestone)
-----------------------------------------------------

### 2.1 Core owns a framebuffer, but not a display

The core exposes:

*   A **memory-backed framebuffer**
*   A **notification mechanism**
*   No display concepts

```
Core:
  - uint8_t framebuffer[WIDTH * HEIGHT * BPP]
  - frame_counter++
  - notify(VideoSink::FrameReady)
```

### 2.2 IPC mechanics

**Do not copy frames.**

Use:

*   Shared memory for pixel data
*   IPC only for _control + metadata_

```
Shared Memory
+---------------------------+
| Framebuffer (RGBA/Indexed)|
+---------------------------+

IPC message:
  FrameReady {
    shm_id
    width
    height
    stride
    format
    frame_number
    timestamp
  }
```

Frontend:

*   Maps shared memory
*   Chooses how and when to display
*   Can drop frames freely

This already gives you:

*   Headless mode (no subscriber)
*   Multiple frontends (read-only SHM)
*   Native graphics APIs per platform

* * *

3\. Chromium-style refinement: decouple _scanout_ from _emulation_
------------------------------------------------------------------

BBC Micro video is _not_ inherently frame-based. It’s:

*   Character-based
*   Raster-based
*   Timing-sensitive

So the real power move is:

> **The core emulates the video hardware, not a screen.**

### 3.1 Two video output levels

#### Level 1: Video State (authoritative)

The core exposes:

*   CRTC registers
*   ULA palette
*   Screen memory regions
*   Timing information

This is:

*   Compact
*   Precise
*   Ideal for debuggers and testing

#### Level 2: Frame Producer (optional service)

A _service_ (possibly in-process at first) converts video state → pixels.

```
CRTC/ULA emulation
        |
        v
VideoState snapshots
        |
        v
FrameProducer
        |
        v
Framebuffer (SHM)
```

Later, that FrameProducer can:

*   Move out-of-process
*   Be replaced
*   Be optimized (SIMD, GPU, etc.)

This mirrors Chromium’s:

> DOM → Layout → Raster → Compositor

* * *

4\. How to think about “frames” correctly
-----------------------------------------

A BBC Micro does not naturally produce “frames”.

Instead, define:

### 4.1 Emulation ticks

*   CPU cycles
*   Video cycles
*   Scanlines

The core emits events like:

```text
ScanlineCompleted(n)
FrameBoundaryReached
PaletteChanged
```

### 4.2 Frontend chooses presentation policy

Frontend decides:

*   50 Hz vs uncapped
*   Integer scaling
*   CRT effects
*   Frame skipping

The core only guarantees:

*   **Causality**
*   **Ordering**
*   **Determinism**

* * *

5\. Headless mode becomes trivial
---------------------------------

Headless mode is just:

*   No `VideoSink` connected
*   Or a dummy sink that:
    *   Captures frames to disk
    *   Compares against golden output
    *   Streams over network

No special cases.

* * *

6\. A practical staged implementation plan
------------------------------------------

### Stage 1 — Simple but correct

*   Core renders full frames
*   Shared memory framebuffer
*   Single frontend
*   Fixed pixel format

This gets Beebium usable fast.

### Stage 2 — Introduce VideoService

*   FrameProducer abstraction
*   VideoState structs
*   FrameProducer initially lives in core

### Stage 3 — Split FrameProducer

*   Move rasterisation out-of-process
*   Core publishes video state deltas
*   Frontend (or video service) rasterises

### Stage 4 — Advanced

*   Multiple rasterisers (debug, CRT, fast)
*   Remote frontends
*   Deterministic replays

* * *

7\. What Chromium teaches here
------------------------------

Chromium’s killer ideas applied to Beebium:

| Chromium | Beebium |
| --- | --- |
| Renderer doesn’t draw to screen | Core doesn’t display |
| GPU process does raster | FrameProducer rasterises |
| Compositor decides timing | Frontend decides timing |
| Surfaces shared via SHM | Framebuffers via SHM |

* * *

8\. A key Beebium-specific insight
----------------------------------

**BBC Micro video bandwidth is tiny.**  
What matters is _architecture_, not raw performance.

So optimise for:

*   Clean contracts
*   Replaceable components
*   Debuggability
*   Testability

You can always inline or collapse processes later — Chromium does this all the time.

* * *

If you like, next we can:

*   Design a concrete `VideoService` IDL
*   Decide whether the core ever rasterises at all
*   Compare with how jsbeeb handles video
*   Sketch shared-memory layouts for indexed colour modes

This is the hardest part — and the most rewarding to get right.

## Prompt:
But there are Vsync signals in the hardware, so frames is a legitimate concept, is it not? Everything is designed around display on a raster CRT.

## Response:
You’re absolutely right — and this is an important correction to make early, because it sharpens the model rather than weakening it.

**Yes: on the BBC Micro, a “frame” is a real, hardware-level concept.**  
VSYNC exists, is observable, and software _does_ rely on it. So we should not pretend frames are an artificial UI invention.

The subtlety is **what VSYNC means, and what should own it**.

* * *

1\. What VSYNC really is on the BBC Micro
-----------------------------------------

On the real machine:

*   The **CRTC generates VSYNC**
*   VSYNC:
    *   Is a signal with a defined duration
    *   Occurs at a predictable interval (~50 Hz PAL)
    *   Can trigger interrupts (via the VIA)
*   Software:
    *   Polls or waits for VSYNC
    *   Synchronises animation to it
    *   Uses it as a timing reference

So from the emulator core’s point of view:

> **VSYNC is an emulated hardware event, not a presentation decision.**

That distinction matters.

* * *

2\. Frames are legitimate — but not _presentation frames_
---------------------------------------------------------

So we should separate two things that are often conflated:

| Concept | Owned by | Meaning |
| --- | --- | --- |
| **VSYNC / frame boundary** | Emulator core | Hardware timing event |
| **Displayed frame** | Frontend | When pixels hit a screen |

On real hardware:

*   VSYNC can occur even if no monitor is connected
*   The monitor may or may not keep up perfectly
*   Software cares about _VSYNC_, not phosphors

That maps beautifully onto your split-process model.

* * *

3\. Correct Beebium model: VSYNC as a first-class event
-------------------------------------------------------

The core should absolutely expose:

```text
Event: VSyncBegin
Event: VSyncEnd
Event: FrameNumber++
```

These are:

*   Deterministic
*   Testable
*   Replayable
*   Required for correctness

This allows:

*   Games synchronised to VSYNC
*   Accurate VIA interrupts
*   Cycle-exact timing

* * *

4\. How video output hangs off this cleanly
-------------------------------------------

### 4.1 Core responsibilities

The core:

*   Emulates:
    *   CRTC timing
    *   ULA palette
    *   Scanline generation
*   Maintains:
    *   Frame counter
    *   Scanline counter
*   Signals:
    *   VSYNC events
    *   Optional “frame ready” hints

It may also:

*   Maintain a canonical framebuffer **at VSYNC boundary**

That’s historically accurate:

*   CRT redraw happens continuously
*   But software reasons in frames

* * *

### 4.2 Frontend responsibilities

The frontend:

*   Subscribes to VSYNC events
*   Decides whether to:
    *   Present every frame
    *   Drop frames
    *   Interpolate
*   Uses:
    *   Native graphics APIs
    *   Native vsync / swapchain semantics

Critically:

> **Frontend timing does not feed back into emulation timing.**

Just like real hardware.

* * *

5\. IPC design that respects hardware reality
---------------------------------------------

A good IPC contract might look like:

```text
VideoTiming
  - frame_number
  - vsync_start_time
  - vsync_duration
  - scanlines_per_frame

VideoFrame
  - shm_handle
  - width
  - height
  - pixel_format
  - frame_number
```

Rules:

*   `VideoFrame` is produced _at_ VSYNC
*   Frontends may ignore frames
*   Core never waits for acknowledgement

This mirrors:

*   Real VSYNC pulses
*   Real monitor behaviour

* * *

6\. Why this still isn’t “core renders frames for UI”
-----------------------------------------------------

Even though frames are real:

*   The **meaning of VSYNC is hardware timing**
*   The **meaning of pixels is hardware state**
*   The **meaning of display is UI**

The core does _not_:

*   Care about refresh rate matching host
*   Care about swap intervals
*   Care about DPI, scaling, tearing

So you preserve:

*   Architectural cleanliness
*   Faithful emulation
*   UI flexibility

* * *

7\. A helpful analogy from Chromium (again)
-------------------------------------------

Chromium has:

*   **BeginFrame** signals (vsync-derived)
*   **Compositor frames**
*   **SwapBuffers**

They are _explicitly separate_ concepts.

Likewise in Beebium:

| Chromium | Beebium |
| --- | --- |
| BeginFrame | VSYNC |
| CompositorFrame | VideoFrame |
| SwapBuffers | Frontend presentation |

Chromium _absolutely_ believes in frames — it just doesn’t confuse them.

* * *

8\. Practical recommendation for Beebium
----------------------------------------

For Beebium v1:

1.  **Treat VSYNC as sacred**
2.  Produce one canonical framebuffer per VSYNC
3.  Publish:
    *   VSYNC event
    *   Frame metadata
4.  Use SHM for pixel data
5.  Let frontends do whatever they want

Later you can:

*   Make scanline-accurate debugging frontends
*   Run without rasterisation entirely
*   Attach multiple observers

* * *

9\. The key takeaway
--------------------

You were right to push back.

**Frames are real in the BBC Micro.  
But they are real as _hardware timing events_, not as _UI artefacts_.**

Once you make that distinction explicit, the Chromium-style split actually becomes _more_ faithful to the original machine, not less.

If you want, next we can:

*   Specify a VSYNC-centric video IDL
*   Decide whether the core should rasterise per scanline or per frame
*   Look at how BeebEm currently entangles UI timing with VSYNC
*   Discuss how to test VSYNC correctness headlessly

This is exactly the right level of scrutiny for this refactor.

## Prompt:
Yes, I’m very happy to distinguish emulation frames and presentation frames as distinct concepts. So the core emulator could render into two images buffers and swap every VSync, publish an event at the end of a frame, and the front end or other clients can fetch the most recently updated frame. Something like that?

## Response:
Yes — that’s a _very_ good and very _workable_ model. You’ve basically described a **VSYNC-latched framebuffer with asynchronous consumers**, which is exactly the right shape.

Let me tighten it, point out the invariants, and show where the traps are.

* * *

1\. Canonical model (clean, minimal, correct)
---------------------------------------------

### Core emulator

The core maintains:

*   Two image buffers (A / B)
*   A current _render_ buffer
*   A current _published_ buffer
*   A monotonically increasing `frame_number`

Pseudo-logic:

```text
render_buffer = A
published_buffer = B

for each emulated cycle:
    update_video_state()
    draw_pixels_into(render_buffer)

on VSync:
    swap(render_buffer, published_buffer)
    frame_number += 1
    publish FrameCompleted(frame_number, published_buffer)
```

That is:

*   **Rendering is continuous**
*   **Publishing is discrete**
*   **VSYNC is the only authority for “frame complete”**

This is faithful to the hardware _and_ friendly to IPC.

* * *

2\. What “publish” really means
-------------------------------

Importantly:

> Publishing does **not** mean copying or sending the frame.

It means:

*   The published buffer becomes **immutable**
*   Its identity (SHM handle + generation) becomes visible
*   A notification is sent

So your mental model of:

> “clients can fetch the most recently updated frame”

is exactly right — with one crucial refinement:

> **Clients never “fetch” pixels; they _observe_ a buffer identity.**

* * *

3\. IPC contract (practical and robust)
---------------------------------------

### FrameCompleted event

```text
FrameCompleted {
    frame_number
    buffer_id        // A or B (or generation counter)
    shm_handle
    width
    height
    stride
    pixel_format
}
```

Rules:

*   Sent _after_ VSYNC
*   Never blocks emulation
*   Can be dropped without harm

### Frontend behaviour

Frontend:

*   Subscribes to `FrameCompleted`
*   Maps shared memory once
*   On event:
    *   Updates pointer to “current frame”
    *   Schedules presentation when convenient
*   If it misses events:
    *   It just displays the newest frame

Exactly like a real monitor.

* * *

4\. Why double buffering is enough (and when it isn’t)
------------------------------------------------------

For BBC Micro-class video:

*   VSYNC ≈ 50 Hz
*   Resolution ≈ tiny
*   Rasterisation cost ≈ trivial

So:

*   **Double buffering is sufficient**
*   Triple buffering only adds complexity

Later, if you:

*   Move rasterisation out-of-process
*   Add expensive CRT shaders

…then you can introduce a buffer pool.

But don’t start there.

* * *

5\. Important invariants (this is where correctness lives)
----------------------------------------------------------

### 5.1 Immutability after publish

Once a buffer is published:

*   Core must _never_ write to it
*   Until it becomes the render buffer again

This is what allows:

*   Lock-free access
*   Multiple readers
*   Zero-copy IPC

### 5.2 Frontend is never authoritative

The core:

*   Never waits for frontend
*   Never asks whether a frame was displayed
*   Never adapts speed to presentation

Frontend:

*   May drop frames
*   May display late
*   May disconnect

This preserves:

*   Determinism
*   Headless operation
*   Testability

* * *

6\. VSYNC vs “end of frame” subtlety
------------------------------------

You said:

> “publish an event at the end of a frame”

Be precise here:

*   **VSYNC start** is usually the correct “frame boundary”
*   Some software is sensitive to:
    *   VSYNC pulse width
    *   Scanline counts

So the core should really emit:

```text
VSyncBegin
VSyncEnd
FrameCompleted   // usually at VSyncBegin
```

Even if the frontend only cares about `FrameCompleted`, debuggers and tests may care about the others.

* * *

7\. Headless and non-visual consumers
-------------------------------------

This design shines here.

Examples of other subscribers:

*   Regression test harness (compare frames)
*   Video capture (write PNGs)
*   Remote streaming client
*   Debugger with scanline view

None of these require:

*   A UI
*   Timing coordination
*   Special cases

They just watch the published buffer.

* * *

8\. Where this still fits future refinements
--------------------------------------------

Your model doesn’t paint you into a corner:

Later you can:

*   Replace pixel buffers with:
    *   Indexed colour + palette
    *   Tile maps
*   Publish:
    *   Video state deltas instead of pixels
*   Move rasterisation into a service

The contract doesn’t change:

> **“At VSYNC, there is a stable, complete representation of the frame.”**

* * *

9\. Chromium analogy (one last time, because it really matches)
---------------------------------------------------------------

| Chromium | Beebium |
| --- | --- |
| Back buffer | Render buffer |
| Front buffer | Published buffer |
| BeginFrame | VSYNC |
| CompositorFrame | FrameCompleted |
| SwapBuffers | Frontend presentation |

Chromium does _exactly_ this — it just has more buffers and more GPUs.

* * *

10\. Verdict
------------

What you’ve proposed is:

*   Architecturally sound
*   Faithful to the BBC Micro
*   Simple enough to build
*   Powerful enough to evolve

You’ve found the _right abstraction_.

If you want, next we can:

*   Write a concrete `VideoService` interface
*   Decide pixel format vs indexed format
*   Look at how BeebEm currently mixes rendering and UI
*   Sketch a headless regression test based on frame hashes

This is now very much in “design it once, live with it for 20 years” territory — and you’re making exactly the right calls.

## Prompt:
If we did CRT shaders (planning ahead here) where would they live? I imagine the emulator would render simple pixels into the shared buffer and then a separate process would consume pixel data from the shared buffer as input to a shader.

## Response:
You’re imagining it _exactly_ right — and planning ahead here pays off, because CRT shaders are the perfect stress-test of whether your architecture is actually clean.

Short answer first:

> **CRT shaders should _never_ live in the emulator core.  
> They belong in a presentation or video-processing process that consumes canonical frames.**

Now let’s place them precisely, and talk about the options.

* * *

1\. Canonical layering (the one you want)
-----------------------------------------

```
+----------------------------+
|        Emulator Core       |
|----------------------------|
|  - CRTC / ULA emulation    |
|  - VSYNC timing            |
|  - Pixel generation       |
|  - Double-buffered frames |
+-------------+--------------+
              |
        SHM framebuffer
              |
+-------------+--------------+
|   Video Pipeline Service   |   (optional, pluggable)
|----------------------------|
|  - Scaling                 |
|  - CRT shaders             |
|  - Colour space conversion |
|  - Frame pacing            |
+-------------+--------------+
              |
       Native graphics API
              |
+-------------+--------------+
|        Frontend UI         |
+----------------------------+
```

Key point:

> **The core produces _truth_.  
> Everything after that is _interpretation_.**

CRT effects are interpretation.

* * *

2\. Why CRT shaders must be outside the core
--------------------------------------------

### 2.1 Hardware fidelity

The BBC Micro did _not_ contain:

*   Scanline bloom
*   Phosphor persistence
*   Mask patterns

Those are properties of:

*   The display
*   The viewer’s eye

So:

*   They are presentation concerns
*   Not emulation concerns

Keeping them out of the core keeps the emulator _honest_.

* * *

### 2.2 Architectural cleanliness

If CRT shaders live in the core:

*   The core suddenly needs:
    *   GPU APIs
    *   Shader languages
    *   Platform-specific code
*   Headless mode breaks or becomes awkward
*   Testing becomes harder

If they live outside:

*   The core remains pure C/C++/Rust
*   Headless mode stays trivial
*   Multiple renderers are possible

* * *

3\. How the shader pipeline should consume frames
-------------------------------------------------

Your intuition is spot-on:

> “a separate process would consume pixel data from the shared buffer as input to a shader”

Yes — with a couple of refinements.

### 3.1 The input is the _published frame_

*   The shader pipeline reads only:
    *   The published buffer
    *   Never the render buffer
*   That buffer is:
    *   Immutable
    *   Stable for the whole frame

This maps perfectly to GPU textures.

* * *

### 3.2 Zero-copy where possible

Best case:

*   Shared memory is:
    *   CPU-visible
    *   Mappable into a GPU texture
*   On some platforms:
    *   This can be truly zero-copy
*   On others:
    *   It’s one upload per frame (still fine)

You don’t need to solve this now — just don’t prevent it.

* * *

4\. Where does this “video pipeline service” live?
--------------------------------------------------

You have **three viable placements**, and your architecture can support all of them.

### Option A — Inside the frontend process (initially)

```
[Core] -> SHM -> [Frontend + shaders]
```

Pros:

*   Simple
*   Fewer processes
*   Easy debugging

Cons:

*   UI + rendering tightly coupled

This is _perfectly fine_ for v1.

* * *

### Option B — Dedicated video service process (Chromium-like)

```
[Core] -> SHM -> [VideoService] -> GPU -> [Frontend]
```

Pros:

*   Clear separation
*   Can restart shaders independently
*   Multiple frontends can share processed output

Cons:

*   More IPC
*   More moving parts

This is where Chromium ends up.

* * *

### Option C — Remote / network video pipeline

```
[Core] -> SHM -> Encoder -> Network -> Shader -> Display
```

Yes, really.  
This becomes trivial with your design.

* * *

5\. Shader pipeline responsibilities (explicit)
-----------------------------------------------

A CRT shader service should own:

*   Upscaling (integer or fractional)
*   Aspect correction
*   Scanline simulation
*   Shadow mask
*   Bloom
*   Phosphor decay (needs frame history!)
*   Colour correction

Notice:

*   **Phosphor decay explicitly needs multiple frames**
*   This _cannot_ be done in the core
*   But it _can_ be done in the shader pipeline

This is another strong reason for the split.

* * *

6\. The core’s pixel format choice matters (slightly)
-----------------------------------------------------

For future CRT shaders:

*   **Indexed colour + palette**
    *   Pros: authentic, compact
    *   Cons: shaders must apply palette
*   **RGBA**
    *   Pros: simpler shader code
    *   Cons: loses some semantic info

A good compromise:

*   Core publishes:
    *   Indexed pixels
    *   Palette data per frame
*   Video pipeline converts to RGBA

This gives shaders maximum fidelity.

But again:

> You can start with RGBA and evolve.

* * *

7\. Timing and CRT effects
--------------------------

CRT effects often want:

*   Real-time pacing
*   Fractional persistence
*   Sub-frame blending

That means:

*   The shader pipeline should use **host vsync**
*   Not emulated vsync
*   Emulated vsync only gates _new input_

This separation is _exactly_ why presentation must be outside the core.

* * *

8\. A concrete future-proof contract
------------------------------------

Here’s a great long-term `VideoFrame` contract:

```text
VideoFrame {
    frame_number
    width
    height
    pixel_format        // Indexed8 or RGBA8888
    palette[16]
    shm_handle
    timestamp_emulated
}
```

CRT pipeline:

*   Keeps N previous frames
*   Applies temporal effects
*   Presents when host allows

Core:

*   Doesn’t know
*   Doesn’t care

* * *

9\. Final verdict
-----------------

Your instinct is dead on.

*   Core renders _simple, canonical pixels_
*   Publishes them at VSYNC
*   Everything “CRT-like” lives downstream
*   Ideally in a separate process
*   Using native GPU APIs and shaders

That’s:

*   Faithful
*   Testable
*   Performant
*   Architecturally elegant

If you like, next we can:

*   Design the shader pipeline API
*   Decide how frame history should be managed
*   Look at how MAME and RetroArch structure this
*   Talk about fallback CPU-based CRT effects for headless capture

You’re laying a _very_ solid foundation here.

## Prompt:
So I’m interested in who provisions the render buffers used by the core. Is it desirable that they could be externally provisioned and handed to the core? Or should the core provision its own buffers and hand out references to them to clients?

## Response:
This is a subtle and important design decision. It depends on your priorities: **simplicity, safety, flexibility, or zero-copy performance**. Let’s walk through the options carefully.

* * *

1\. Option 1 — Core provisions its own buffers (canonical design)
-----------------------------------------------------------------

**How it works:**

*   Core internally allocates double buffers (or N buffers)
*   Core owns lifetime and memory management
*   Core publishes _immutable references_ (or SHM handles) to clients at VSYNC
*   Clients read only; they never write or free buffers
*   Core swaps buffers internally each frame

**Pros:**

*   Simplicity: core owns memory, no race conditions or lifecycle complexity
*   Safety: clients can’t overwrite emulated pixels
*   Determinism: core has full control over the frame data
*   Works well for headless testing

**Cons:**

*   Less flexible if you want zero-copy across multiple frontends or GPUs
*   Slightly less performance flexibility if clients need special buffer layouts

**This is generally the _default_ choice**, especially for early versions.

* * *

2\. Option 2 — External buffer provisioning (inverted)
------------------------------------------------------

**How it works:**

*   Core doesn’t allocate buffers itself
*   Clients or a memory manager allocate buffers (maybe in shared memory or GPU memory)
*   Core is handed a buffer pointer each frame to render into
*   After VSYNC, core hands buffer identity back to clients

**Pros:**

*   Maximal flexibility:
    *   Frontends or video services can control format/layout
    *   Zero-copy to GPU textures or external pipelines is easier
*   Multi-consumer scenarios can pre-allocate a buffer pool
*   Can implement GPU-resident buffers for high-speed shaders

**Cons:**

*   Lifecycle is trickier:
    *   Core must never use a buffer after a client frees it
    *   Must implement pool management or reference counting
*   API is more complex
*   Core becomes dependent on external allocator, which complicates headless mode

**This is the model MAME and RetroArch sometimes use** for high-performance GPU pipelines.

* * *

3\. Option 3 — Hybrid / pluggable buffer provisioning
-----------------------------------------------------

A modern, flexible architecture often combines both approaches:

1.  Core has _default internal buffers_ for simplicity and headless mode.
2.  Optional external buffer provider can be plugged in:
    *   Shared memory from another process
    *   GPU-mapped buffers
    *   Pre-allocated circular buffer pool
3.  Core API abstracts “frame render target”:
    ```text
    class FrameBuffer {
        uint8_t* pixels;
        width, height;
        stride;
        generation;  // frame number
    }
    interface FrameAllocator {
        FrameBuffer* requestBuffer();
        void releaseBuffer(FrameBuffer*);
    }
    ```
4.  Default implementation uses internal buffers
5.  Advanced frontends or GPU pipelines can inject custom allocator

**Pros:**

*   Default simplicity + safety
*   High-performance flexibility later
*   Core code stays largely unchanged
*   Supports multiple frontends with different requirements

* * *

4\. Recommended approach for Beebium
------------------------------------

*   **Start simple:** core owns its own buffers internally
*   Publish references at VSYNC
*   Clients consume but never mutate
*   Use immutable semantics
*   Keep the API abstract so buffer allocation can be replaced in the future

This gives you:

*   Determinism
*   Headless testability
*   A clean baseline architecture

Later, you can:

*   Introduce an optional `FrameAllocator` interface
*   Support GPU zero-copy paths
*   Allow video pipeline services to provision buffers with special layouts
*   Keep core unaware of presentation or GPU mechanics

* * *

### ✅ Summary

| Design | Pros | Cons | Use case |
| --- | --- | --- | --- |
| Core allocates buffers | Simple, safe, deterministic | Less flexible for GPU/zero-copy | Default, headless, early version |
| External buffers | Max flexibility, zero-copy | Complex lifecycle, risk of errors | Advanced GPU pipelines, high-performance shaders |
| Hybrid | Flexible and safe | Slightly more design work | Best long-term architecture for Beebium |

**Verdict for Beebium v1:**  
Core-provisioned buffers with immutable references. Later, optionally support external buffer provisioning for GPU/CRT pipelines.

* * *

If you like, I can sketch a **concrete C++/Rust-ish API for frame buffers and allocators** that supports both default internal buffers and optional external provisioning — something you could actually use in Beebium. It would illustrate exactly how the core hands out references safely.

## Prompt:
Can we do the FrameAllocator at the outset, but only provide one built-in implementation for locally allocated buffers?

## Response:
Absolutely — that’s exactly the right compromise. You get the **flexible architecture upfront** without committing to complexity prematurely. This is a common pattern in modern, modular designs: define the _abstraction first_, implement a simple default, and leave hooks for advanced cases later.

Here’s how it would look conceptually:

* * *

1\. Core idea
-------------

*   Define a `FrameAllocator` interface or trait from the start
*   Core uses this interface for every frame render
*   Provide a single **built-in implementation**:
    *   Allocates double buffers internally
    *   Handles swapping at VSYNC
    *   Returns immutable references to clients
*   Frontend sees only references/pointers and frame metadata

This gives you:

*   Clean separation of concerns
*   Immediate support for headless mode
*   Future-proof path for GPU/shared memory/etc.
*   Determinism and safety

* * *

2\. Conceptual interface
------------------------

```text
interface FrameAllocator {
    // Request a buffer for the core to render into
    FrameBuffer* requestRenderBuffer();

    // Notify the allocator the buffer has been published (optional)
    void releasePublishedBuffer(FrameBuffer* buffer);
}

struct FrameBuffer {
    uint8_t* pixels;
    int width;
    int height;
    int stride;
    int generation;  // frame number
}
```

*   `requestRenderBuffer()` gives the core a buffer to draw pixels into
*   On VSYNC:
    *   Core swaps buffers internally
    *   Publishes the “current” buffer to clients
    *   Calls `releasePublishedBuffer()` so the allocator can manage lifetimes
*   Built-in allocator simply double-buffers internally and ignores `releasePublishedBuffer()` because buffers never leave the process

* * *

3\. Built-in allocator
----------------------

*   Simple double buffer
*   Allocates on startup
*   Swaps buffers at VSYNC
*   Tracks generation/frame number
*   Immutable after publish
*   Provides SHM handle (or pointer) to frontends

Optional extensions in the future:

*   GPU texture pool
*   Cross-process shared memory
*   Zero-copy buffers for multiple frontends
*   Variable buffer formats (RGBA vs indexed)

* * *

4\. Advantages of this approach
-------------------------------

1.  **Architecture-first design**
    *   All consumers and the core talk to the allocator interface
    *   Changing buffer management later does not touch the core
2.  **Safety**
    *   Immutable buffers prevent race conditions
    *   Frontends can read without blocking core
3.  **Extensibility**
    *   Later you can inject external GPU-backed buffers
    *   Or network-provided buffers for remote frontends
4.  **Early testing**
    *   Default allocator is enough to implement headless regression tests
    *   Emulation is fully deterministic

* * *

5\. Practical Beebium workflow
------------------------------

1.  Core requests a render buffer via `FrameAllocator`
2.  Core draws pixels into the buffer
3.  On VSYNC:
    *   Swap render/published buffers
    *   Increment frame number
    *   Publish buffer to frontends
    *   Call `releasePublishedBuffer()` on old buffer (optional)
4.  Frontends:
    *   Map buffer (SHM or pointer)
    *   Read/convert/present frame asynchronously
    *   May drop frames if slow

* * *

### ✅ Summary

*   Define `FrameAllocator` from the start
*   Provide a **single, simple built-in allocator** for local buffers
*   All future buffer strategies (GPU, shared memory, remote) plug in later without changing core code
*   Core remains fully deterministic, headless-friendly, and UI-agnostic

* * *

If you like, I can draw a **diagram showing how the core, the allocator, and frontends interact**, including double buffering and VSYNC events. This usually makes the lifecycle very clear.

## Prompt:
So what IPC technology should we use? The core will be in C and C++ in order to leverage existing code, but front-ends and debuggers and whatnot which consume the services provided by the emulator should be able to be written in other languages such as Swift, NodeJS (e.g. an electron app), or indeed C++.

## Response:
Perfect — now we’re getting into the “pluggable, language-agnostic IPC” part, which is _exactly_ where your architecture benefits from being designed like Chromium. We need something that is:

*   **Cross-language** (C/C++, Swift, Node.js, Rust, etc.)
*   **Low-latency** for video / input / audio events
*   **Capable of shared memory handles** for zero-copy frame delivery
*   **Stable and versionable** to allow evolving the API

Let’s break down the viable approaches.

* * *

1\. IPC paradigms
-----------------

### 1.1 Shared memory + signaling (lightweight, simple)

**How it works:**

*   Core allocates frame buffers in shared memory (SHM)
*   IPC channel is used **only for notifications / metadata**
*   Consumers map SHM and read buffers asynchronously
*   Signals: `FrameCompleted`, `VSync`, input events, etc.

**Transport choices:**

*   Unix Domain Sockets (POSIX) / Named Pipes (Windows)
*   ZeroMQ (optional, can handle cross-platform)
*   TCP / localhost sockets (simple fallback for remote clients)

**Pros:**

*   Very fast for pixel-heavy data
*   Language-agnostic (sockets + shared memory can be mapped in almost any language)
*   Minimal dependencies

**Cons:**

*   You need to manage synchronization / notifications yourself
*   Metadata and frame formats must be standardized

**This is the simplest and robust starting point.**

* * *

### 1.2 Higher-level RPC frameworks

If you want **richer, typed APIs**, you can consider:

| Framework | Notes |
| --- | --- |
| **gRPC** | Supports C/C++, Swift, Node.js, Python; codegen handles types. Can carry handles indirectly (e.g., SHM IDs). |
| **Cap’n Proto RPC** | Extremely low latency, supports C++ / Node / Swift; built-in versioning; can handle shared memory descriptors. |
| **FlatBuffers + IPC transport** | Like Cap’n Proto, but more manual; cross-language. |
| **MessagePack / JSON-RPC** | Lightweight, language-agnostic; easier to debug, but slower and text-based. |

**Pros:**

*   Strongly typed
*   Automatic code generation
*   Easy to evolve services

**Cons:**

*   Slightly more complex initial setup
*   May require more boilerplate for zero-copy buffer sharing

* * *

### 1.3 Existing “Chromium-like” approach

Chromium uses **Mojo**, which is:

*   Typed IPC
*   Cross-platform
*   Supports transferring handles (like shared memory)
*   Supports process isolation / sandboxes
*   Supports async calls and callbacks

**Pros:**

*   Exactly what Chromium uses
*   Future-proofs multiple frontends, debuggers, GPU services
*   Multi-language support is possible (C++, Node bindings exist, Swift bindings feasible)

**Cons:**

*   Mojo is not trivial to integrate
*   Brings a large dependency chain

> For Beebium, you could take the _Mojo philosophy_ but implement a simpler, lightweight alternative first.

* * *

2\. Recommended approach for Beebium v1
---------------------------------------

**Layered approach:**

1.  **Transport layer:**
    *   Use **Unix domain sockets / named pipes** for cross-language IPC
    *   JSON or MessagePack for early prototyping
    *   Simple pub/sub notification channel for frame events, input events, etc.
2.  **Shared memory for frames:**
    *   Core provides SHM handles for frame buffers
    *   Frontends map the memory for direct GPU upload or CPU access
    *   Avoid copying large pixel buffers through IPC
3.  **Service abstraction:**
    *   Define a simple RPC-like API per service (`Video`, `Input`, `Debug`)
    *   Start with asynchronous events only
    *   Optional: add synchronous request/response for queries (memory dump, state inspection)
4.  **Future-proof path:**
    *   Later replace socket + JSON with **Cap’n Proto or gRPC**, keeping the same service contract
    *   This enables multiple language frontends, advanced performance, and better versioning

* * *

3\. Example “event + shared memory” flow
----------------------------------------

```
[Core]
  allocates SHM -> FrameBuffer
  draws pixels -> FrameBuffer
  sends "FrameCompleted" event via socket:
      {
          frame_number: 123,
          shm_handle: "/tmp/beebium_fb_123",
          width: 320,
          height: 256,
          pixel_format: INDEXED8
      }

[Frontend / Debugger]
  receives event
  maps SHM
  reads pixels
  applies shaders or displays
```

Notes:

*   No pixels are copied via IPC
*   Frontends in Node.js, Swift, C++ all work
*   Optional: multiple frontends subscribe to the same frames

* * *

4\. Frontend flexibility
------------------------

With this setup:

*   Electron / Node.js → map SHM via `ffi` or `node-addon-api`
*   Swift → map SHM via `mmap` / `FileHandle`
*   C++ → native mapping
*   All languages communicate with the core using **the same service API**

**Benefits:**

*   Core remains language-agnostic and headless
*   You can evolve services independently
*   CRT shaders, audio processing, network streaming all become pluggable processes

* * *

5\. Suggested v1 path
---------------------

1.  Define the **service API** (Video, Input, Debug, etc.) in JSON or MessagePack schema
2.  Use **Unix sockets / named pipes** for events and RPC
3.  Use **shared memory** for frame buffers, optionally double-buffered
4.  Implement a **minimal built-in allocator** for local buffers
5.  Frontends consume events asynchronously, map SHM, display frames

Later:

*   Swap JSON → Cap’n Proto or gRPC
*   Add multi-process or GPU-backed video pipelines
*   Support multiple frontends simultaneously

* * *

✅ **Summary Recommendation**

*   **IPC transport:** Unix domain sockets / named pipes for v1
*   **Data channel:** shared memory for pixel buffers
*   **Protocol:** async events, JSON or MessagePack for metadata
*   **Future-proofing:** design around typed service interfaces so you can swap transport / serialization later without changing core
*   **Multi-language support:** inherent, because all modern languages support sockets and SHM

* * *

If you want, I can draft a **service-level diagram** showing the core, shared memory, IPC, frontends, and debuggers — with the flow of frames, VSYNC events, and inputs. That usually makes this architecture crystal-clear before coding.

## Prompt:
I’m happy to use gRPC from the outset so long as it meets our other requirements. Is it always layered on top of HTTP?

## Response:
Not necessarily — and this is an important distinction for your use case. gRPC can work in different “modes,” and for Beebium’s core requirements (low-latency video, multi-language frontends, optional headless) you need to understand the trade-offs.

* * *

1\. Standard gRPC
-----------------

*   **Transport:** HTTP/2 over TCP
*   **Protocol:** Protobuf messages, multiplexed streams
*   **Pros:**
    *   Language-agnostic (C/C++, Swift, Node.js, Python, Rust)
    *   Strongly typed RPC with auto-generated stubs
    *   Supports bidirectional streaming (good for video events, inputs, logs)
    *   Mature ecosystem, plenty of tutorials and tooling
*   **Cons:**
    *   HTTP/2 framing adds some overhead (~1–2% latency, usually negligible for control messages)
    *   Not zero-copy: you have to serialize messages (pixels still go via SHM, not over gRPC)

For your use case, **control messages, VSYNC notifications, input events, debug requests** are ideal for standard gRPC streams.

* * *

2\. gRPC “in-process” / alternate transport
-------------------------------------------

*   gRPC’s core library actually separates **API semantics** from transport.
*   In theory, you could plug in:
    *   Unix domain sockets directly (without HTTP/2)
    *   In-process memory transport (useful for testing / headless)

This is less common in production, but some gRPC implementations support it (e.g., C++ gRPC in-process channels, or custom “channel” implementation).

**Benefit:** you can avoid HTTP/2 overhead entirely for local IPC while keeping the typed RPC interface.

* * *

3\. Practical architecture for Beebium with gRPC
------------------------------------------------

### Core:

*   C/C++ gRPC server
*   Exposes services:
    *   `VideoService` → frame metadata, VSYNC events
    *   `InputService` → send keypresses, joystick events
    *   `DebugService` → memory inspection, disassembly
*   Publishes **frame buffers via shared memory**; gRPC messages carry only handles/metadata

### Frontend:

*   Any language with gRPC client (Swift, Node, C++)
*   Subscribes to streams for events
*   Maps SHM to GPU textures / CRT shaders
*   Sends inputs back via RPC

### Notes:

*   Video frames themselves **never travel over gRPC** — only metadata and notifications.
*   gRPC handles bidirectional streaming naturally, so you can implement async input and debug commands efficiently.
*   Using standard HTTP/2 is fine if you’re mostly local; overhead is negligible.
*   If you need ultra-low-latency local IPC, you could investigate an in-process or custom transport later.

* * *

4\. Comparison to alternative IPC
---------------------------------

| Feature | gRPC | Unix Domain Socket + JSON | Cap’n Proto RPC |
| --- | --- | --- | --- |
| Cross-language | ✅ | ✅ | ✅ |
| Typed | ✅ | ❌ (manual) | ✅ |
| Async streaming | ✅ | ✅ (manual) | ✅ |
| Shared memory integration | ✅ (via handles, not automatic) | ✅ | ✅ |
| HTTP overhead | ~1–2% | minimal | minimal |
| Versioning / evolution | ✅ | manual | ✅ |

**Verdict:** gRPC gives you the typed, versionable, multi-language architecture upfront. SHM for pixels handles the heavy data path. HTTP/2 overhead is irrelevant unless you’re doing microsecond-level latency measurements, which is unlikely for BBC Micro video.

* * *

✅ **Recommendation for Beebium v1**

*   Use gRPC over HTTP/2 for all services: Video, Input, Debug
*   Keep pixel buffers in shared memory, never send them via RPC
*   Use bidirectional streaming for events and commands
*   Consider optional in-process channels only for testing or headless mode if needed

* * *

If you want, I can **sketch a concrete gRPC service API** for Beebium showing how VSYNC events, frame metadata, inputs, and debug requests flow — including where shared memory buffers fit in. This would give a full blueprint for multi-language frontends.

## Prompt:
I’ve been reading about Cap’N proto. Investigation is cheap at this stage. What is the language and platform support like? Swift?

## Response:
Yes — Cap’n Proto is actually _very well designed for multi-language support_, though there are some caveats when it comes to Swift. Let me break it down carefully.

* * *

1\. Official language support
-----------------------------

The core Cap’n Proto project provides:

| Language | Notes |
| --- | --- |
| **C++** | Native, first-class support. Most mature, full feature set. |
| **Python** | Mature, supports RPC and serialization. |
| **Go** | Supported, fairly mature. |
| **Java** | Supported, less commonly used for RPC. |
| **Node.js / JavaScript** | Supported via `capnp-ts` or community bindings. |
| **Rust** | Well-supported via `capnp-rust`. |
| **C# / .NET** | Partial support, serialization works; RPC may need extra setup. |

> C++ is the canonical implementation; other languages usually wrap or reimplement the schema compiler output.

* * *

2\. Swift support
-----------------

*   There is **no official Swift Cap’n Proto implementation** yet.
*   Community efforts exist (e.g., SwiftCapnp), but most are experimental.
*   Using Swift generally means one of these approaches:
    1.  **Use C++ Cap’n Proto from Swift via bridging headers**
        *   You can call C++ code from Swift on Apple platforms using mixed targets.
        *   The Cap’n Proto runtime stays in C++, Swift just calls the API.
        *   Works well for desktop and iOS/macOS apps.
    2.  **Use a text or JSON bridge**
        *   Swift talks to a separate C++ process via JSON over sockets (less efficient).
        *   Not ideal for video or low-latency IPC.

> In practice, the bridging approach is what Chromium itself does on macOS when interfacing C++ components with Swift/Objective-C UI.

* * *

3\. Platform support
--------------------

Cap’n Proto is extremely portable:

*   **Linux**: Full support
*   **Windows**: Full support
*   **macOS / iOS**: Full support, C++ works fine
*   **Embedded / ARM / Raspberry Pi / BSD**: Supported (C++ runtime is tiny)

So you can run Beebium core anywhere C++ runs, and frontends on desktop platforms.

* * *

4\. Pros for Beebium
--------------------

*   **Zero-copy serialization**: perfect for passing handles/metadata about frames or shared memory buffers.
*   **RPC support**: allows typed async streams for video events, inputs, debug commands.
*   **Schema evolution**: safe API changes over time.
*   **Multi-language**: Node.js, Python, Rust frontends are supported. Swift will need bridging, but works.

* * *

5\. Cons / caveats
------------------

*   Swift support is **not native**, so adding Swift frontends involves some C++ interop.
*   Cap’n Proto is lower-level than gRPC; you’ll need to define your RPC loop and threading model carefully.
*   Tooling is less mature than gRPC (fewer tutorials, fewer ecosystem libraries).

* * *

### ✅ Bottom line

*   **C++ core + Cap’n Proto** is perfect.
*   Node.js, Rust, Python frontends are easy.
*   Swift frontends are feasible via C++ bridging.
*   HTTP is **not required** — Cap’n Proto works directly over sockets or any file descriptor transport.

* * *

If you like, I can sketch a **Beebium Cap’n Proto service design**, including:

*   `VideoService` events + frame metadata
*   Shared memory buffer handoff
*   InputService for keyboard/joystick
*   DebugService for memory inspection

…showing exactly how multi-language frontends (Swift, Node, C++) could subscribe. This gives a real blueprint for moving forward.

## Prompt:
Is there good support for gRPC in Swift?

## Response:
Yes — Swift has **good, production-ready support for gRPC**, especially on Apple platforms, though there are some nuances worth knowing for your Beebium architecture.

* * *

1\. gRPC support in Swift
-------------------------

*   **Official gRPC Swift project**: [grpc/grpc-swift](https://github.com/grpc/grpc-swift)
*   Supports:
    *   **Swift on macOS and iOS** (via Swift Package Manager or CocoaPods)
    *   **Linux** (for server or headless frontends in Swift)
*   Implements:
    *   Client and server APIs
    *   Unary, server streaming, client streaming, and bidirectional streaming
    *   Swift-native concurrency (`async/await`) and event-driven APIs
*   Uses **SwiftNIO** under the hood (high-performance, non-blocking I/O)

* * *

2\. Transport
-------------

*   gRPC Swift **supports two modes**:

| Mode | Description | Notes |
| --- | --- | --- |
| **gRPC over HTTP/2** | Standard mode, compatible with all gRPC servers | Ideal for multi-platform communication |
| **gRPC over SwiftNIO channels** | Can be embedded in custom transport for advanced use | Usually unnecessary unless doing in-process IPC or custom transport |

> In practice, you’ll mostly use standard HTTP/2 transport, which works well locally with low latency.

* * *

3\. Language interoperability
-----------------------------

*   gRPC Swift generates Swift-native stubs from Protobuf definitions, so your Swift frontend can call C++ or Rust gRPC services without bridging to C++
*   Works well with:
    *   Node.js / Electron (standard gRPC Node)
    *   Python (gRPC Python)
    *   C++ core (gRPC C++)
*   Supports **bidirectional streaming**, perfect for:
    *   Frame metadata / VSYNC events
    *   Input events
    *   Debugging commands

* * *

4\. Pros for Beebium
--------------------

*   Strong typed RPC with code generation
*   Async streaming for real-time events
*   Cross-platform and language-agnostic
*   Well-supported and production-ready
*   Native Swift concurrency support (async/await)

* * *

5\. Caveats / things to watch
-----------------------------

*   **Pixel data should never travel over gRPC** — still use shared memory for frames
*   gRPC uses HTTP/2, which adds minimal overhead (~microseconds for local transport)
*   Bidirectional streaming may require careful handling of flow control to avoid event backlog if frontends are slow

* * *

### ✅ Summary

*   gRPC Swift is mature and production-ready
*   Perfect for Beebium frontends on macOS / iOS
*   Works seamlessly with C++ core and Node.js/Electron frontends
*   Supports streaming and async events for VSYNC, input, and debug

* * *

If you want, I can draft a **concrete gRPC service layout** for Beebium showing VideoService, InputService, DebugService, frame metadata, and how frontends in Swift, Node, and C++ would interact — essentially a blueprint for multi-language frontends.

## Prompt:
Ok, we’ll plan for gRPC.

## Response:
Perfect — that gives us a solid foundation. With gRPC in place:

*   The **core** (C/C++) exposes typed services for Video, Input, Debug, etc.
*   **Frontends** in Swift, Node.js, C++, or any supported language subscribe via gRPC streams.
*   **Pixel-heavy data** (frames) stay in shared memory; gRPC carries only metadata, handles, and events.
*   Bidirectional streaming handles VSYNC notifications, inputs, and debugger commands asynchronously.

This setup gives you:

*   Language-agnostic frontends
*   Headless operation for testing or server-side emulation
*   Clear separation of **emulation core** vs **presentation/UI**
*   Future-proof upgrade path (GPU-backed pipelines, CRT shaders, multiple frontends, or remote streaming)

The next step is to define the **service interfaces and frame buffer contract**, so all consumers can agree on events, frame metadata, and SHM handles. We can start designing that if you want — it will give a complete blueprint for Beebium’s multi-process architecture.

## Prompt:
I’m quite keen that the external interfaces to the emulator mirror the physical interfaces on a real BBC Micro, so User Port, Printer Port, 1 MHz Bus, the Tube, RS423 serial, and so on. I think some of these need to be clock synchronized with the core-emulator - like the 1 MHz Bus, but others could probably happily live in separate processes - like Tube second processors which have their own clocks. Does that sound reasonable?

## Response:
Absolutely — that sounds not just reasonable, but very _clean architecturally_. You’re essentially modeling your external interfaces as **pluggable, clock-aware devices**, some tightly coupled to the core, others loosely coupled or fully asynchronous. Let’s break it down carefully.

* * *

1\. Classifying BBC Micro interfaces
------------------------------------

| Interface | Characteristics | Timing / Clock coupling |
| --- | --- | --- |
| **1 MHz Bus** | Connects peripherals like disk drives, Econet adapters | **Strictly clocked** — must be cycle-accurate or tightly synchronized with core |
| **User Port** | General-purpose expansion, mostly I/O | Can be asynchronous for most peripherals; depends on peripheral |
| **Printer Port** | Mostly asynchronous serial output | Loosely coupled; buffering works |
| **Tube / Second Processors** | Full CPUs with own memory | Can be in separate process; synchronize only on data exchange |
| **RS423 Serial** | Asynchronous serial | Loosely coupled; emulate baud rate, but doesn’t need core-cycle granularity |

* * *

2\. Architecture principles
---------------------------

1.  **Tightly coupled devices**
    *   Must be emulated **inside the core or via a clock-synchronized plugin**.
    *   Examples: 1 MHz Bus peripherals, some timing-critical User Port devices.
    *   The core provides precise timing, cycles, and VSYNC events.
    *   Plugin may run in the same process, sharing memory and clock ticks.
2.  **Loosely coupled devices**
    *   Can live in separate processes.
    *   Example: Tube second processors, RS423 serial adapters.
    *   Communication via IPC (gRPC, shared memory, or message queues).
    *   Each peripheral can maintain its **own clock or thread**, syncing only at meaningful boundaries (data transfer, interrupt, or handshake).
3.  **Abstraction via interface objects**
    *   Core exposes a **“Peripheral interface”**:
        ```text
        interface Peripheral {
            void tick(int cycles);
            void read(address);
            void write(address, value);
            void irq();
            void reset();
        }
        ```
    *   Each device implements this interface.
    *   Tightly coupled devices: `tick()` called every core cycle (or every N cycles).
    *   Loosely coupled devices: `tick()` called asynchronously; data synchronized through IPC.
4.  **Optional asynchronous queues**
    *   Even tightly coupled devices can buffer input/output to avoid blocking the core.
    *   Loosely coupled devices can exchange messages with event timestamps.

* * *

3\. Tube second processor example
---------------------------------

*   A Tube CPU is essentially a separate processor.
*   Can live in **its own process**:
    *   Maintains its own clock
    *   Executes instructions independently
    *   Communicates with the host core via message-passing: memory reads/writes, interrupts, bus requests
*   Core only needs to:
    *   Provide memory-mapped interface for Tube host interactions
    *   Process responses in order
    *   Optionally, synchronize “ticks” for deterministic debugging

**Benefit:** allows full parallelism, easier multi-language frontends, and potentially GPU offload or remote Tube processors in the future.

* * *

4\. 1 MHz Bus example
---------------------

*   Example: a disk controller must respond cycle-accurately.
*   Strategies:
    1.  Keep the device **inside the core process**
    2.  Expose a **clocked plugin interface** — core calls `tick()` or `step()` every 1 MHz cycle
*   Any attempt to move this into another process would require **extremely low-latency IPC** and careful cycle synchronization, usually not worth the complexity.

* * *

5\. Summary guidelines
----------------------

*   **Core-emulator**: cycle-accurate logic, VSYNC, CPU, memory, timing-critical peripherals
*   **Clock-synchronized peripherals**: optional plugins, likely in-process
*   **Loose peripherals**: Tube processors, RS423, printers — can live in separate processes, communicate asynchronously
*   **Interface design**: abstract each port/device via a consistent API, whether local or remote
*   **Determinism**: maintain it in the core; loose devices may be non-deterministic if process scheduling varies

> This matches the philosophy of “emulate the hardware faithfully, but allow modern process separation for non-critical devices.”

* * *

If you like, I can sketch a **diagram showing the core and peripherals split into tightly and loosely coupled processes**, including how VSYNC and 1 MHz clock devices interact versus Tube second processors and asynchronous ports. This would make the architecture very tangible.

