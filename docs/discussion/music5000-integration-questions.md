# Music 5000 integration: facts and open questions

Groundwork for the Music 500/5000 design. It states how the 1 MHz bus
and the audio pipeline work today, with `file:line` references at
commit 363ae96f, and lists the questions they raise for a Music 5000.
It makes no proposals. The hardware facts about the Music 5000 come
from `docs/music5000/` (mainly `emulator-survey.md`).

## (a) Two devices on `1mhz-bus`

### Facts: the bus today

- **Multiple extensions are allowed.** The `1mhz-bus` attachment point
  has occupancy 0..N
  (`src/core/include/beebium/extension/AttachmentPointCatalogue.hpp:58-59`,
  `max_occupancy` = `std::nullopt`). The attachment plan only enforces a
  cap when one is catalogued
  (`src/core/include/beebium/extension/AttachmentPlan.hpp:163-168`), so
  any number of extensions may attach.
- **One port per machine.** Each hardware policy owns a single
  `OneMHzBusPort` (`src/core/include/beebium/ModelBHardware.hpp:263`) and
  maps it over `0xFC00-0xFDFF` (`ModelBHardware.hpp:894`). The other
  variants are the same; for example
  `ModelBIntegraBHardware.hpp:312,1127`. Extensions reach the port
  through `ExtensionContext::get<OneMHzBusPort>()`
  (`src/core/include/beebium/extension/ExtensionContext.hpp:45`,
  `get()` below it).
- **Offsets.** They run from `0xFC00`: FRED is `0x000-0x0FF` and JIM is
  `0x100-0x1FF` (`src/core/include/beebium/extension/OneMHzBusDevice.hpp:24-26`).
- **Claims are exclusive per offset.** The port keeps one owner pointer
  per offset (`src/core/include/beebium/extension/OneMHzBusPort.hpp:82`).
  `claim_addresses()` throws if any offset in the range is already
  claimed (`src/core/extension-api/OneMHzBusPort.cpp:37-43`). A read or
  write goes to that one owner, or reads `0xFF` and drops the write if
  the offset is unclaimed (`OneMHzBusPort.hpp:45-56`).
- **Tests.** Exclusivity is tested (`tests/test_one_mhz_bus_port.cpp:97`,
  "rejects overlapping claims"), and so is coexistence on disjoint
  ranges (`tests/test_one_mhz_bus_port.cpp:150`).
- **Current claimants.** Only two extensions claim anything:
  - `acorn-scsi` claims FRED `0x40-0x43` (`&FC40-&FC43`):
    `src/extensions/acorn-scsi/AcornScsiHostAdapter.hpp:53-54`,
    `AcornScsiHostAdapter.cpp:35`.
  - `test-scratch-ram` claims FRED `0x80-0x87` (`&FC80-&FC87`):
    `src/extensions/test-scratch-ram/TestScratchRam.hpp:45-46`,
    `TestScratchRam.cpp:41`.

  Nothing claims `&FCFF` or any JIM offset today.
- **Tick.** The port ticks every claiming device at 1 MHz
  (`OneMHzBusPort.hpp:65-67`, `OneMHzBusPort.cpp:55-60`). The call comes
  from the hardware's `poll_nmi()` (`ModelBHardware.hpp:467`), on even
  2 MHz cycles (`src/core/include/beebium/Machine.hpp:373-375`) and also
  during bus-stretch cycles (`Machine.hpp:1108-1111`). Device time
  therefore keeps pace with emulated time while the CPU is stalled on
  the 1 MHz bus.
- **Stretching.** Every FRED and JIM access is stretched to 1 MHz
  (`src/core/include/beebium/BusStretching.hpp:66`; applied in
  `src/core/include/beebium/CpuBinding.hpp:168-169`).
- **IRQ.** IRQs from all devices are ORed into IRQ bit 3
  (`ModelBHardware.hpp:885`, `OneMHzBusPort.cpp:62-74`). The Music 5000
  raises no interrupts.

### Facts: what the Music 5000 needs

These come from `docs/music5000/emulator-survey.md`, "The common
register model".

- **The paging register.** Writes to `&FCFF` (FRED offset `0x0FF`), the
  JIM paging register, select the active 256-byte JIM page. A Music 5000
  responds when the page's upper nibble is 3; a Music 3000 when it is 5.
- **The whole JIM window.** It needs all of `&FD00-&FDFF` (offsets
  `0x100-0x1FF`), and acts on a write only when its page is selected.
- **Write-only.** The ETI article says the interface is write-only.
  BeebEm, jsbeeb and the FPGA reconstruction nonetheless return data on
  reads; the survey's "Where the models disagree" table records this.
- **Acorn's FRED allocation** lists `&FCFF` as the "JIM paging register",
  shared by all JIM users, and `&FC40-&FC47` as "Winchester Disc"
  (`docs/manuals_text/BBC_Master-New-Advanced-User-Guide/23_23._1Mhz_bus__carts/01_1._Introduction/01_1._General.md:3-24`).
  The JIM paging scheme follows at line 37 onward of the same file: one
  paging register selects one of 256 extended pages through page `&FD`.

### Consequences of the facts as they stand

- **Music 5000 with the SCSI host adapter: no overlap.** The Music 5000
  (`0x0FF`, `0x100-0x1FF`) and the SCSI host adapter (`0x040-0x043`)
  claim disjoint offsets. With today's port they can coexist as two
  extensions.
- **Two JIM-paged devices cannot coexist.** A Music 5000 plus a Music
  3000, or a Music 5000 plus any other JIM-paged device (expansion RAM,
  Pi1MHz-style devices), cannot both claim `0x0FF` or the JIM window.
  The second claim throws. On real hardware each such device latches
  every `&FCFF` write and decodes only its own extended pages, so all
  of them see the paging register and the window together.
- **Reads of `&FCFF` and JIM need an owner.** If one device owns them,
  what it returns is what the guest sees. On real hardware a read with
  no device driving the bus gives the transceiver's float value
  (`OneMHzBusPort.hpp:30-31`; see `docs/empirical-databus-behaviour.md`).

### Open questions (a)

1. **Who owns `&FCFF`?** Should the JIM paging register belong to the
   port (machine-side state that every JIM device can read), or to an
   extension? On a real BBC Micro the register lives on each add-on, not
   on the motherboard.
2. **How should several devices share JIM?** Should a device claim a
   range of extended page numbers, rather than offsets `0x100-0x1FF`, so
   the port can route each access by the current page? Or should several
   devices observe the same offsets, with read conflicts resolved
   somehow?
3. **How does a pair of boards appear?** Is the Music 3000 a second
   instance of the same extension with a different page nibble, or one
   extension modelling both boards?
4. **What do JIM reads return?** On a page the Music 5000 owns, should a
   read give the float value (write-only, as ETI says) or RAM contents
   (as BeebEm, jsbeeb and the FPGA do)? To be settled from the
   original schematic.
5. **Is the Music 5000 entry in the hard-disc doc right?**
   `docs/hard-disc-comparison.md:231` lists "Music 5000 / BeebOPL" at
   `0xFC20-0xFC27`, and `:340` lists "Music 5000/3000" at `0xFC4x`. Both
   contradict every implementation surveyed, which use `&FCFF` plus JIM.
   Is the entry simply wrong, or does it describe some other board?

## (b) A second audio source at 46875 Hz

### Facts: the pipeline today

- **One buffer, one rate.** The machine has one `AudioBuffer`
  (`ModelBHardware.hpp:159`), a single-producer single-consumer (SPSC)
  queue of `AudioSample` frames. Its default capacity is one second at
  48 kHz (`src/core/include/beebium/AudioBuffer.hpp:81`).
- **The frame.** Each frame holds four 32-bit source fields
  (`AudioBuffer.hpp:30-32`). The comment on it names the Music 5000 as
  an intended 2x16-bit stereo source (`AudioBuffer.hpp:26`), and the
  sound subsystem doc reserves index 2 for "1 MHz bus audio (Music
  5000)" (`docs/sound-subsystem.md:225`).
- **The SN76489 sets the frame rate.** The chip is built for 48 kHz
  output (`ModelBHardware.hpp:156`). It decimates its 250 kHz internal
  rate to 48 kHz (`src/core/src/Sn76489.cpp:53`), pushes one complete
  frame per output sample, and zeroes sources 2 and 3
  (`Sn76489.cpp:217-223`). So the SN76489 alone produces every frame,
  and the frame rate is its output rate.
- **Ticking.** `Machine` ticks the SN76489 on every 2 MHz cycle,
  including stretch cycles (`Machine.hpp:320-321`, `1100-1101`). No
  extension takes part in producing audio. `ExtensionContext` offers
  ports and indicators only, with no audio handle
  (`ExtensionContext.hpp:45-51`).
- **The service.** `AudioService` hard-codes `SAMPLE_RATE = 48000`
  (`src/service/include/beebium/service/AudioService.hpp:51`). It
  streams every source field of every frame
  (`AudioService.hpp:92-105`). `GetAudioFormat` describes sources 0 and
  1 as the SN76489, and 2 and 3 as "Reserved", `ENCODING_SILENCE`
  (`AudioService.hpp:128-166`).
- **The protocol.** `audio.proto` has one `sample_rate` per stream
  (`src/service/proto/audio.proto:73`), with per-source encodings and
  names ("Music5000" is given as an example at `audio.proto:54`). There
  is no per-source sample rate, and no second stream.
- **The macOS client** reads `sample_rate` and `source_count` from
  `GetAudioFormat` (`clients/macos/Beebium/Beebium/AudioClient.swift:208-231`).
  However, it copies only the first 8 bytes of each frame, sources 0
  and 1 (`AudioClient.swift:265-283`). Its renderer has 4 channels
  (`clients/macos/Beebium/Beebium/AudioRenderer.swift:44`), applies a
  10 Hz DC-removal high-pass (`AudioRenderer.swift:50`), and defaults to
  48 kHz (`AudioRenderer.swift:139`, `AudioEngine.swift:67`).
- **The Python client** decodes `AudioFormat` generically: rate,
  sources and encodings
  (`clients/beebium-python-client/src/beebium/client/audio.py:110-114`).
  The TypeScript client has no audio module.
- **The Music 5000 side** (from the survey): its native rate is 46875 Hz
  (6 MHz / 128). It produces signed stereo, while the SN76489 is
  unipolar per channel. The designer holds that resampling quality and
  the analogue output filter are central to how it sounds
  (`docs/music5000/stardot-key-points.md`, points 2, 3 and 5).
- **The rate ratio.** 46875/48000 = 125/128. At a 1 MHz device tick,
  one Music 5000 sample is 21 1/3 ticks: 3 samples per 64 ticks.

### Consequences of the facts as they stand

- **Mixing into the existing frame needs a resampler.** A Music 5000
  sample in source 2 would have to be resampled to 48 kHz on the
  server: frames are pushed at the SN76489's rate, and the stream
  declares one rate.
- **A native-rate stream needs protocol changes.** Delivering the Music
  5000 at 46875 Hz would need either a per-source rate in the
  protocol, or a second buffer and stream (or service).
- **Every client would need work.** The macOS client would play neither
  form today, since it drops source 2. It also assumes the SN76489's
  unipolar channel layout in its renderer and DC filter.

### Open questions (b)

6. **Where is the 46875 to 48000 conversion done, if at all?** In the
   server, before the samples enter `AudioBuffer` source 2? Or in the
   client, with the server carrying the native rate? The designer's
   remark on resampling (`stardot-key-points.md`, point 2) bears on
   this.
7. **Where does the analogue output filter live?** In the device
   model, so every client hears the same thing (the server is the
   single source of truth for emulated behaviour), or in the client?
   Before or after any resampling?
8. **How does the device's audio reach the pipeline?** Should a plugin
   get an audio handle through `ExtensionContext`? None exists, and
   `AudioBuffer` is SPSC with the SN76489 as its one producer.
9. **Who decides when a frame is complete?** If the Music 5000 shares
   the frame, should a frame still be pushed by the SN76489 alone, with
   the Music 5000 filling its field from its own history? Or should the
   machine assemble a frame from all sources at a common rate?
10. **Is `AudioFormat` dynamic?** It is currently constant. Should it
    describe the Music 5000 (and the 3000) only when attached, and how
    does a client learn that the format changed mid-session, for
    example after a preset change?
11. **Drift.** Does a second source change how drift between the
    emulated audio clock and the host device clock is absorbed? Client
    adaptive resampling for that is issue #128 (open, deferred).
