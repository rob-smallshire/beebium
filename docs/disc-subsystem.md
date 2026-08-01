# Disc Subsystem

This document describes Beebium's disc subsystem architecture for emulating BBC Micro floppy disc storage.

## Architecture Overview

The disc subsystem uses a layered architecture separating storage, physical drive emulation, and controller logic:

```
┌─────────────────────────────────────────────────────────────┐
│                    ModelBPlusHardware                       │
│  ┌─────────────────┐  ┌─────────────────────────────────┐   │
│  │ DiscControlReg  │  │           WD1770                │   │
│  │    (0xFE80)     │──│      (0xFE84-0xFE87)            │   │
│  └─────────────────┘  └──────────┬──────────────────────┘   │
│                                  │                          │
│           ┌──────────────────────┼──────────────────────┐   │
│           │                      │                      │   │
│     ┌─────▼─────┐          ┌─────▼─────┐                │   │
│     │ DiscDrive │          │ DiscDrive │                │   │
│     │  (drive0) │          │  (drive1) │                │   │
│     └─────┬─────┘          └─────┬─────┘                │   │
│           │                      │                      │   │
│     ┌─────▼─────┐          ┌─────▼─────┐                │   │
│     │ DiscImage │          │ DiscImage │                │   │
│     │ (SSD/DSD) │          │ (SSD/DSD) │                │   │
│     └───────────┘          └───────────┘                │   │
└─────────────────────────────────────────────────────────────┘
```

### Layer Responsibilities

| Layer | Class | Responsibility |
|-------|-------|----------------|
| Storage | `DiscImage` | Sector-level read/write, format geometry |
| Drive | `DiscDrive` | Head positioning, motor control, disc insertion |
| Controller | `WD1770` | Command execution, timing, status/interrupt signals |
| Hardware | `ModelBPlusHardware` | Memory-mapped registers, NMI gating |

## Components

### DiscImage (Abstract Interface)

**Header:** `src/core/include/beebium/disc/DiscImage.hpp`

Abstract interface for disc storage backends. Provides sector-level access independent of physical format.

```cpp
class DiscImage {
public:
    // Metadata
    virtual std::string name() const = 0;
    virtual bool is_write_protected() const = 0;
    virtual void set_write_protected(bool) = 0;

    // Geometry
    virtual uint8_t sides() const = 0;              // 1 or 2
    virtual uint8_t tracks_per_side() const = 0;    // 40 or 80
    virtual uint8_t sectors_per_track() const = 0;  // 10 for DFS
    virtual uint16_t sector_size() const = 0;       // 256 for DFS

    // Sector I/O
    virtual bool read_sector(uint8_t side, uint8_t track, uint8_t sector,
                            std::span<uint8_t> buffer) = 0;
    virtual bool write_sector(uint8_t side, uint8_t track, uint8_t sector,
                             std::span<const uint8_t> buffer) = 0;
    virtual void flush() = 0;
};
```

**Implementations:**

| Class | Header | Purpose |
|-------|--------|---------|
| `FileDiscImage` | `disc/FileDiscImage.hpp` | File-backed SSD/DSD images |
| `MemoryDiscImage` | `disc/MemoryDiscImage.hpp` | In-memory images for testing |

### FileDiscImage

**Header:** `src/core/include/beebium/disc/FileDiscImage.hpp`
**Source:** `src/core/src/disc/FileDiscImage.cpp`

Loads disc images from filesystem. Entire image is read into memory; writes go through immediately.

```cpp
// Load existing image (throws std::runtime_error on failure)
auto disc = FileDiscImage::load("/path/to/disc.ssd");

// Create new empty image
auto disc = FileDiscImage::create("/path/to/new.ssd", geometry);
```

**Error conditions:**
- File not found: `"Disc image not found: <path>"`
- Permission denied: `"Cannot open disc image (permission denied?): <path>"`
- Unknown format: `"Unrecognized disc image format (size=N, ext=X): <path>"`

**Write protection:** Automatically detected from filesystem permissions. A read-only file appears as a write-protected disc.

### DiscGeometry

**Header:** `src/core/include/beebium/disc/DiscGeometry.hpp`
**Source:** `src/core/src/disc/DiscGeometry.cpp`

Describes disc format and provides sector offset calculations.

```cpp
struct DiscGeometry {
    DiscFormat format;           // SSD or DSD
    uint8_t sides;               // 1 or 2
    uint8_t tracks_per_side;     // 40 or 80
    uint8_t sectors_per_track;   // 10
    uint16_t sector_size;        // 256

    size_t total_size() const;
    std::optional<size_t> sector_offset(uint8_t side, uint8_t track, uint8_t sector) const;

    static std::optional<DiscGeometry> detect_from_size(size_t file_size, std::string_view extension);
};
```

### DiscDrive

**Header:** `src/core/include/beebium/disc/DiscDrive.hpp`

Emulates physical floppy drive mechanics: head positioning, motor control, disc insertion/ejection.

```cpp
class DiscDrive {
public:
    // Disc management
    void insert(std::unique_ptr<DiscImage> disc);
    std::unique_ptr<DiscImage> eject();
    bool has_disc() const;
    DiscImage* disc() const;

    // Head positioning (0-79 tracks)
    void step_in();              // Toward higher tracks
    void step_out();             // Toward track 0
    void seek(uint8_t track);
    uint8_t current_track() const;
    bool at_track_0() const;

    // Motor control
    void set_motor(bool on);
    bool motor_on() const;

    // Sector access (at current track)
    bool read_sector(uint8_t side, uint8_t sector, std::span<uint8_t> buffer);
    bool write_sector(uint8_t side, uint8_t sector, std::span<const uint8_t> buffer);
    bool is_write_protected() const;
};
```

### WD1770

**Header:** `src/core/include/beebium/disc/WD1770.hpp`

Emulates the Western Digital WD1770 Floppy Disc Controller. This is a large header-only implementation (~770 lines) containing the full state machine.

#### Register Interface

| Offset | Read | Write |
|--------|------|-------|
| 0 | Status | Command |
| 1 | Track | Track |
| 2 | Sector | Sector |
| 3 | Data | Data |

#### Command Types

| Type | Commands | Function |
|------|----------|----------|
| I | Restore, Seek, Step, Step-In, Step-Out | Head positioning |
| II | Read Sector, Write Sector | Sector I/O |
| III | Read Address, Read Track, Write Track | Track-level I/O |
| IV | Force Interrupt | Command termination/interrupt control |

#### Status Register Bits

The meaning of status bits depends on command type:

| Bit | Type I | Type II/III |
|-----|--------|-------------|
| 0 | BUSY | BUSY |
| 1 | INDEX | DRQ |
| 2 | TRACK0 | LOST_DATA |
| 3 | CRC_ERROR | CRC_ERROR |
| 4 | SEEK_ERROR | RNF |
| 5 | SPIN_UP | RECORD_TYPE |
| 6 | WRITE_PROT | WRITE_PROT |
| 7 | MOTOR_ON | MOTOR_ON |

#### Key Methods

```cpp
class WD1770 {
public:
    // Register access
    uint8_t read(uint16_t offset);
    void write(uint16_t offset, uint8_t value);

    // Clock (1MHz)
    void tick();

    // Interrupt signals
    bool drq() const;        // Data request
    bool intrq() const;      // Interrupt request
    bool nmi_pending() const;  // For NmiAggregator

    // Drive attachment
    void attach_drive(int drive_num, DiscDrive* drive);

    // External control signals
    void set_side(uint8_t side);
    void set_drive(uint8_t drive);
    void set_density(bool double_density);

    void reset();
};
```

#### Force Interrupt (Type IV) Command

The Force Interrupt command `0xDx` uses bits I0-I3 to control interrupt behavior:

| Command | I3 | I2 | I1 | I0 | Behavior |
|---------|----|----|----|----|----------|
| `0xD0` | 0 | 0 | 0 | 0 | Terminate command, no interrupt |
| `0xD8` | 1 | 0 | 0 | 0 | Immediate interrupt |
| `0xD4` | 0 | 1 | 0 | 0 | Interrupt on index pulse |
| `0xD2` | 0 | 0 | 1 | 0 | Interrupt on ready→not-ready |
| `0xD1` | 0 | 0 | 0 | 1 | Interrupt on not-ready→ready |

## Hardware Integration (Model B+)

**Header:** `src/core/include/beebium/ModelBPlusHardware.hpp`

The Model B+ has a built-in WD1770 disc controller. The hardware class owns the controller and drives:

```cpp
class ModelBPlusHardware {
    WD1770 disc_controller;
    DiscDrive disc_drive_0;
    DiscDrive disc_drive_1;
    // ...
};
```

### Memory Map

| Address | Function |
|---------|----------|
| 0xFE80 | Disc control register |
| 0xFE84 | WD1770 Status/Command |
| 0xFE85 | WD1770 Track |
| 0xFE86 | WD1770 Sector |
| 0xFE87 | WD1770 Data |

### Disc Control Register (0xFE80)

| Bit | Function |
|-----|----------|
| 0 | Drive 0 select (active high) |
| 1 | Drive 1 select (active high) |
| 2 | Side select (0=side 0, 1=side 1) |
| 3 | Density (0=double/MFM, 1=single/FM) |
| 4 | Motor on (active high) |
| 5 | WD1770 reset (active low) |
| 6 | NMI enable (nominally gates INTRQ/DRQ to NMI, see [NMI Gating](#nmi-gating-bit-6)) |

Note: Bits 0 and 1 are active-high drive selects, not a binary drive number. DFS sets bit 0 for drive 0, bit 1 for drive 1.

**Important:** This register is **write-only**. Reading returns 0xFF (open bus). See [8271 vs 1770 Detection](#8271-vs-1770-detection) below.

### 8271 vs 1770 Detection

The Model B+ motherboard was designed to accept either an Intel 8271 or WD1770 disc controller. In practice, only the WD1770 was ever fitted (soldered in). However, the address decoding deliberately swaps the register addresses between the two controllers to allow DFS to detect which is present:

| Controller | Command/Status Registers | Data/Control Registers |
|------------|-------------------------|------------------------|
| Intel 8271 | 0xFE80-0xFE83 (readable) | 0xFE84-0xFE87 (DACK) |
| WD1770 | 0xFE80-0xFE83 (write-only latch) | 0xFE84-0xFE87 (registers) |

From the B+ Service Manual (section 5.5.2):

> "It can be seen that the 1770 controller and the 8271 controller address space has been swapped. This is to allow the disc system software to distinguish between the two devices."

**Detection mechanism:** DFS reads from 0xFE80. If it receives a valid response (indicating 8271 command/status registers), it uses 8271 protocol. If it receives 0xFF (open bus from the write-only IC17 latch), it knows a WD1770 is fitted and uses the registers at 0xFE84-0xFE87.

The disc control register (IC17) is a write-only latch that provides:
- Drive/side/density selection
- Motor control
- WD1770 reset
- NMI enable gating

Since Beebium only emulates Model B+ with WD1770 (the only configuration ever manufactured), reading 0xFE80-0xFE83 returns 0xFF.

## Hardware Integration (Model B)

**Headers:**
- `src/core/include/beebium/disc/DiscControllerSocket.hpp`
- `src/core/include/beebium/disc/DiscControllerInterface.hpp`
- `src/core/include/beebium/disc/Acorn1770DiscController.hpp`

Unlike the Model B+ which has a soldered WD1770, the BBC Model B had a physical socket for optional disc controller upgrades. Beebium models this with a pluggable socket architecture.

### Architecture

```
ModelBHardware
    │
    ├── disc_drive_0, disc_drive_1  (owned by hardware, persist across controller changes)
    │
    └── disc_socket (DiscControllerSocket at 0xFE80-0xFE9F)
            │
            └── DiscControllerInterface* (optional, runtime-pluggable)
                    │
                    ├── Acorn1770DiscController (WD1770 + Acorn control register)
                    ├── OpusDiscController (future: WD2793)
                    ├── WatfordDiscController (future)
                    └── Intel8271DiscController (future)
```

### Key Design Decisions

1. **Drives owned by hardware** - Drives persist across controller changes. Disc images don't need re-insertion when swapping controllers.

2. **Socket at 0xFE80-0xFE9F (32 bytes)** - Full disc controller region, allowing different controllers to use different address layouts.

3. **Controller includes control register** - The control register is board-specific (Acorn vs Opus vs Watford have different bit layouts), so it's encapsulated within each controller implementation.

4. **Empty socket returns 0xFF** - Open bus behaviour when no controller installed.

### DiscControllerSocket

The socket wrapper provides a uniform interface regardless of whether a controller is installed:

```cpp
class DiscControllerSocket {
public:
    uint8_t read(uint16_t offset);   // Returns 0xFF if empty
    void write(uint16_t offset, uint8_t value);  // Ignored if empty
    void tick();
    bool nmi_pending() const;

    void install(std::unique_ptr<DiscControllerInterface> controller);
    std::unique_ptr<DiscControllerInterface> remove();
    bool has_controller() const;
    DiscControllerInterface* controller();

    // Drive management (delegates to controller)
    void attach_drive(int drive_num, DiscDrive* drive);
    DiscDrive* attached_drive(int drive_num) const;

    void reset();
};
```

### DiscControllerInterface

Abstract interface for pluggable disc controllers:

```cpp
class DiscControllerInterface {
public:
    virtual uint8_t read(uint16_t offset) = 0;
    virtual void write(uint16_t offset, uint8_t value) = 0;
    virtual void tick() = 0;
    virtual bool nmi_pending() const = 0;
    virtual void reset() = 0;

    virtual void attach_drive(int drive_num, DiscDrive* drive) = 0;
    virtual void detach_drives() = 0;
    virtual DiscDrive* attached_drive(int drive_num) const = 0;

    virtual std::string_view name() const = 0;
    virtual void set_spin_up_delay_enabled(bool enabled) = 0;
    virtual bool spin_up_delay_enabled() const = 0;
};
```

### Acorn1770DiscController

The Acorn 1770 upgrade board wraps a WD1770 FDC with Acorn's control register layout:

| Offset | Read | Write |
|--------|------|-------|
| 0x00-0x03 | 0xFF (open bus) | Control register (mirrored) |
| 0x04 | WD1770 Status | WD1770 Command |
| 0x05 | WD1770 Track | WD1770 Track |
| 0x06 | WD1770 Sector | WD1770 Sector |
| 0x07 | WD1770 Data | WD1770 Data |

Control register bits match the Model B+ layout (see above).

### Command-Line Configuration

Model B accepts the `--fdc` option to install a disc controller at startup:

```bash
# List available controllers
beebium-model-b --list-fdc

# Install Acorn 1770 controller
beebium-model-b --fdc acorn-1770 --floppy 0:game.ssd

# No disc controller (default)
beebium-model-b --fdc none
```

### gRPC Configuration

The DiscService provides runtime controller management:

```protobuf
service DiscService {
    // List available controller types
    rpc ListAvailableControllers(ListAvailableControllersRequest)
        returns (ListAvailableControllersResponse);

    // Install a controller (socketed machines only)
    rpc InstallDiscController(InstallDiscControllerRequest)
        returns (InstallDiscControllerResponse);

    // GetDriveStatus reports is_socketed and installed_controller_id
}
```

**Example workflow:**
```python
# Check if machine supports controller installation
status = disc_service.GetDriveStatus()
if status.is_socketed:
    # List available controllers
    available = disc_service.ListAvailableControllers()
    for ctrl in available.controllers:
        print(f"{ctrl.id}: {ctrl.display_name} ({ctrl.fdc_chip})")

    # Install a controller
    result = disc_service.InstallDiscController(controller_id="acorn-1770")
    if result.success:
        print(f"Installed: {result.controller_type}")
```

### NMI Handling

The WD1770 generates NMI via two signals that are OR'd together:
- **DRQ** (Data Request): Asserted when a byte is ready to be read or the controller is ready to accept a byte for writing
- **INTRQ** (Interrupt Request): Asserted when a command completes

From the B+ Service Manual:
> Two interrupt signals come from a 1770, pins 27 and 28. The two interrupts are inverted and wire NORed on to the notNMI line by two parts of IC7 (quad NAND gate).

#### 1MHz Clock Timing

**Critical:** The WD1770 operates at 1MHz, not the CPU's 2MHz clock. NMI state must only be updated on 1MHz clock edges (every other 2MHz cycle). This is implemented in `Machine::step()`:

```cpp
void step() {
    // ... tick CPU and VIAs ...

    // NMI handling - only update on 1MHz clock edges
    if ((state_.cycle_count & 1) == 0) {
        uint8_t nmi_mask = state_.memory.poll_nmi();
        M6502_SetDeviceNMI(&state_.cpu, kDiscNmiDeviceMask, nmi_mask ? 1 : 0);
    }

    ++state_.cycle_count;
}
```

If NMI is updated every 2MHz cycle, DRQ can toggle too rapidly: after the NMI handler reads the data register (clearing DRQ), the very next cycle would tick the WD1770 and set DRQ for the next byte, creating a new falling edge on /NMI before the handler completes RTI. This causes NMIs to stack up infinitely.

#### Inter-Byte Timing

At 250kbps MFM (double density), bytes arrive approximately every 64µs. The WD1770 implementation enforces this timing with a `byte_delay_` counter:

```cpp
// In tick_read_sector():
data_ = sector_buffer_[byte_counter_];
drq_ = true;
byte_delay_ = US_PER_BYTE;  // 64 ticks at 1MHz
```

The `tick()` method decrements `byte_delay_` and only advances the state machine when it reaches zero. This ensures the NMI handler has sufficient time (~64µs = ~128 CPU cycles) to complete before the next DRQ assertion.

#### NMI Gating (Bit 6)

The disc control register bit 6 is nominally an "NMI enable" bit. However, following the B2 emulator's approach, Beebium does **not** gate NMI via this bit because:

1. B2's `DiscInterfaceControl` struct has no NMI enable field
2. DFS does not appear to set bit 6 before disc operations
3. Real software works correctly without gating

The `poll_nmi()` implementation simply returns the WD1770's NMI state:

```cpp
uint8_t poll_nmi() {
    uint8_t nmi = disc_controller.nmi_pending() ? 0x01 : 0x00;
    disc_controller.tick();
    return nmi;
}
```

Note the ordering: NMI state is sampled **before** ticking the controller. This is critical for edge detection—after the CPU reads the DATA register (clearing DRQ), `poll_nmi()` returns 0. Then `tick()` may set DRQ for the next byte. On the next `poll_nmi()` call, we return 1, creating a clean 0→1 edge for the 6502's edge-triggered NMI detection.

### Motor Control

The BBC Micro's disc interface supported two different floppy disc controllers with fundamentally different motor control mechanisms. Understanding this distinction is crucial for emulator implementation.

#### 8271 vs WD1770 Motor Control

| Controller | Motor Control | Used In |
|------------|--------------|---------|
| Intel 8271 | **Explicit** via disc control register bit 4 | Model B (original) |
| WD1770 | **Internal** to controller during command execution | Model B+ (built-in), Model B (upgrade) |

The disc control register at 0xFE80 has bit 4 labelled "Motor on" in documentation, but this is only relevant for 8271-style explicit motor control. The WD1770 manages motor state internally:

```
8271 Motor Control:
    DFS software → writes 0x1x to 0xFE80 → motor on
    DFS software → writes 0x0x to 0xFE80 → motor off

WD1770 Motor Control:
    DFS software → writes command to 0xFE84 → WD1770 spins up internally
    WD1770 → command completes → idle timeout → WD1770 spins down internally
```

**Evidence:** When DFS 2.26 issues a `*CAT` command, it writes `0x29` to the disc control register:
- Bit 0 = 1: Drive 0 select
- Bit 3 = 1: Single density
- Bit 4 = 0: **Motor bit is NOT set**
- Bit 5 = 1: Reset inactive

DFS does not set the motor bit because it relies on the WD1770's internal motor control.

#### WD1770 Internal Motor Control

The WD1770 maintains motor state via the STATUS_MOTOR_ON bit (bit 7 of the status register). Motor control is triggered by command execution:

```cpp
// In WD1770::execute_command()
void execute_command(uint8_t cmd) {
    // ... command setup ...

    // Spin up motor for all disc access commands (Type I/II/III)
    spin_up();

    // ... rest of command ...
}
```

The `spin_up()` method:
1. Sets the internal `motor_on_` flag
2. Sets STATUS_MOTOR_ON in the status register
3. Calls `DiscDrive::spin_up()` which activates the drive's motor and LED indicator
4. Resets the idle timeout counter

#### Motor Spin-Up Delay

Real WD1770 hardware waits for the motor to reach stable speed before executing commands. This takes approximately 6 disc revolutions at 300 RPM (~1.2 seconds). Beebium implements this delay:

```cpp
// In WD1770::spin_up()
void spin_up() {
    if (!motor_on_) {
        motor_on_ = true;
        status_ |= STATUS_MOTOR_ON;
        if (spin_up_delay_enabled_) {
            spin_up_delay_ = SPIN_UP_DELAY_TICKS;  // 1.2M ticks at 1MHz
        }
        if (auto* drive = get_current_drive()) {
            drive->spin_up();  // LED activates immediately
        }
    }
}

// In WD1770::tick()
void tick() {
    if (spin_up_delay_ > 0) {
        --spin_up_delay_;
        return;  // Wait for motor to reach speed
    }
    // ... command execution proceeds ...
}
```

**Behavior:**
- LED activates immediately when `spin_up()` is called
- Command execution is delayed until spin-up completes
- If motor is already running, no delay (commands execute immediately)

**Configuration via gRPC:**

The spin-up delay can be disabled for automation scenarios via the DiscService gRPC API:

```protobuf
service DiscService {
    rpc SetSpinUpDelay(SetSpinUpDelayRequest) returns (SetSpinUpDelayResponse);
    rpc GetSpinUpDelay(GetSpinUpDelayRequest) returns (GetSpinUpDelayResponse);
}

message SetSpinUpDelayRequest { bool enabled = 1; }
message GetSpinUpDelayResponse { bool enabled = 1; }
```

**Usage:**
```
// Disable for fast automated testing
DiscService.SetSpinUpDelay(enabled=false)

// Query current setting
DiscService.GetSpinUpDelay() → enabled: true/false
```

**Direct accessor (for tests):**
```cpp
machine.state().memory.disc_controller.set_spin_up_delay_enabled(false);
```

#### Motor Spin-Down Timeout

After a command completes, the motor remains on for an idle period before spinning down. Real WD1770 hardware spins down after approximately 10 index pulses (~2 seconds at 300 RPM). Beebium implements this as a tick counter:

```cpp
// In WD1770::tick()
void tick() {
    if (!(status_ & STATUS_BUSY)) {
        if (motor_on_) {
            if (++idle_ticks_ >= MOTOR_OFF_DELAY_TICKS) {  // 2,000,000 ticks = 2s at 1MHz
                spin_down();
                idle_ticks_ = 0;
            }
        }
        return;
    }
    // ... command execution ...
}
```

The `spin_down()` method:
1. Clears the internal `motor_on_` flag
2. Clears STATUS_MOTOR_ON in the status register
3. Calls `DiscDrive::spin_down()` which deactivates the drive's motor and LED indicator

#### Object Model

The motor control hierarchy follows proper encapsulation:

```
WD1770 (disc controller)
    │
    ├── spin_up() / spin_down()     ← called during command execution
    │
    └── DiscDrive (physical drive)
            │
            ├── spin_up() / spin_down()  ← delegated from WD1770
            │
            ├── set_motor(bool)          ← updates motor state & indicator
            │
            └── Indicators               ← activity LED signaling
```

The WD1770 talks to drives (via `DiscDrive` pointers), and drives encapsulate their motor. The disc controller doesn't directly manipulate indicators; that's handled by `DiscDrive::set_motor()`.

#### Implications for 8271 Implementation

When implementing Intel 8271 support for Model B compatibility:

1. **8271 does NOT have internal motor control** — the motor bit in the disc control register (0xFE80 bit 4) must directly control `DiscDrive::set_motor()`

2. **DFS for 8271 explicitly sets motor bit** — the software writes to enable/disable motor

3. **Same DiscDrive class can be used** — only the controller logic differs:
   ```cpp
   // 8271 disc control register write handler
   void write(uint16_t, uint8_t value) {
       // ...
       // Bit 4: Motor on (8271 explicit control)
       drive0.set_motor((value & 0x10) != 0);
       drive1.set_motor((value & 0x10) != 0);
       // ...
   }
   ```

4. **Detection mechanism** — DFS reads from 0xFE80 to distinguish controllers:
   - 8271: Returns valid status (command/status registers at 0xFE80-0xFE83)
   - WD1770: Returns 0xFF (write-only latch, open bus on read)

#### Drive Activity LED Indicators

Motor state directly controls the drive activity LED indicator:

```cpp
// In DiscDrive::set_motor()
void set_motor(bool on) {
    if (motor_on_ != on) {
        motor_on_ = on;
        if (indicators_) {
            indicators_->set(activity_led_id_, on ? 255 : 0);
        }
    }
}
```

With WD1770 internal motor control:
- LED activates when WD1770 executes any Type I/II/III command
- LED remains on during command execution and idle timeout
- LED deactivates ~2 seconds after last command completes

## File Formats

### SSD (Single-Sided Disc)

Linear sector layout: Track 0 sectors, Track 1 sectors, etc.

| Variant | Tracks | Size |
|---------|--------|------|
| 40-track | 40 | 102,400 bytes |
| 80-track | 80 | 204,800 bytes |

**Sector offset:** `track * 10 * 256 + sector * 256`

### DSD (Double-Sided Disc)

Interleaved layout: Track 0 Side 0, Track 0 Side 1, Track 1 Side 0, etc.

| Variant | Tracks | Size |
|---------|--------|------|
| 40-track | 40 | 204,800 bytes |
| 80-track | 80 | 409,600 bytes |

**Sector offset:** `track * 2 * 10 * 256 + side * 10 * 256 + sector * 256`

### DFS Constants

- Sector size: 256 bytes
- Sectors per track: 10
- Tracks per side: 40 or 80

## Configuration

### Command-Line Options

```
beebium-model-b-plus --drive0 <filepath> --drive1 <filepath>
```

| Option | Description |
|--------|-------------|
| `--drive0 <filepath>` | Insert disc image into drive 0 |
| `--drive1 <filepath>` | Insert disc image into drive 1 |

**Write protection:** Determined by filesystem permissions. Read-only files appear as write-protected discs.

**Error handling:** Exits with error if file not found or format unrecognized.

### Programmatic Configuration

```cpp
ModelBPlus machine;

// Load and insert disc image
auto disc = FileDiscImage::load("game.ssd");
machine.memory().disc_drive_0.insert(std::move(disc));

// Or create in-memory disc for testing
auto disc = MemoryDiscImage::create_ssd();
machine.memory().disc_drive_0.insert(std::move(disc));
```

## Write-Back and Durability

How guest writes reach the host image file differs deliberately between the
floppy (FDD) and hard-disc (HDD) subsystems. The two models are not the same,
and the difference is intentional.

### Floppy (FDD) write-back

Floppy images are decoded at the pulse level: a write lands in an in-memory
pulse track and sets a per-track dirty flag (`DiscTrack`). Nothing touches the
host file at that point. Dirty tracks are written back to the host `.ssd`/`.dsd`
through a format `write_track_callback` (`SsdFormatHandler`/`AdfsFormatHandler`/
`HfeFormatHandler`) on these triggers:

- **Head steps off a track** -- `DiscDrive::flush_before_leaving_track` flushes
  the track being left, so sustained multi-track activity persists continuously.
- **Write inactivity** -- the WD1770 flushes all dirty tracks after a short
  idle period with no writes (`FLUSH_IDLE_TICKS`, ~250 ms at 1 MHz). This is the
  prompt path for the final track, and it is the one place that also calls
  `platform::sync_file_to_disk` (`fsync`), so a settled save is durable against
  a crash. It is deliberately decoupled from motor spin-down (~2 s), because
  motor-off also gates eject quiescence and flushing there would race the eject
  path.
- **Server shutdown** -- `flush_disc_drives` flushes both drives after the
  emulation loop has stopped, as a backstop for quitting just after a write.
- **Eject** -- `complete_eject` flushes all dirty tracks before the disc leaves.

### Ejecting

A safe eject (`EjectDisc`) puts the drive into `Ejecting` and returns at once.
The **emulation loop** completes it, via `tick_disc_drives` in
`run_emulation_loop`, once the motor has been off for the quiescence period
(500 ms by default). That placement matters twice over: the emulation thread
owns the drives, so completing an eject there does not free the disc out from
under the disc controller; and it is the one thread that always exists.
Progress used to be a side effect of the `SubscribeDiscEvents` handler, which
meant a server nobody was streaming from left a disc in `Ejecting` for ever.
The loop keeps ticking across a debugger pause too -- a standing-still drive is
exactly when it is safe for a disc to leave.

**The server never forces an eject.** A pending safe eject waits as long as the
drive stays busy. Giving up is the caller's decision: eject again with
`immediate` to take the disc now, or `CancelEject` to leave it where it is. The
macOS sidebar offers both once an eject has been pending for about two seconds,
so forcing is always something a person chose.

### Reporting drive changes

`DiscEvent`s are raised where the state changes -- `DiscDrive` notifies a
`DiscDriveObserver` from `insert`, `request_eject`, `cancel_eject`,
`complete_eject` and `set_motor` -- and the service fans them out to the open
`SubscribeDiscEvents` streams. Nothing samples drive state on a timer.

This is not merely tidier. A drive sampled periodically cannot show an
excursion that begins and ends between two samples: an eject followed straight
away by an insert reads `Loaded` both times, so a subscriber went on displaying
a disc that had already been swapped. Raising the event where the change
happens also keeps a forced eject distinguishable from a graceful one, which no
reconstruction from sampled state could recover.

Because the drives are mutated from more than one thread -- the emulation
thread for motor transitions and for a safe eject completing, an RPC thread for
an insert or an immediate eject -- the fan-out uses a short lock per subscriber
rather than the single-producer queue used for debugger events. Publishing
never waits on a subscriber; a client that stops reading loses its oldest
events rather than holding up the emulation thread.

### Changing a disc

`InsertDisc` fails if the drive already holds one. Loading over an occupied
drive would have to eject the disc that is there, and the only eject available
synchronously is `eject_immediate`, which skips the quiescence wait and so can
remove a disc from under a spinning motor mid-command. Removing a disc is the
user's decision, so it is a separate `EjectDisc`, whose default path waits for
the motor to be off for 500 ms first.

`eject_immediate` does still flush dirty tracks (via `complete_eject`), so what
it risks is an operation in flight rather than data already written to a track.
It remains available through `EjectDisc` with `immediate` set, for callers that
have decided they want it.

Both `InsertDisc` and `EjectDisc` run their mutation inside
`Machine::with_emulation_paused`. They arrive on RPC threads, and swapping or
freeing the disc while the disc controller is reading pulses from it is a race
on a pointer that is about to be freed.

Front-ends follow the same rule: the macOS Storage sidebar refuses a drop onto
an occupied drive, saying so while the drag is still in the air, rather than
swapping the disc silently.

The in-memory dirty layer is why this machinery is needed: without it, a track
written and then stepped away from would sit unflushed in process memory and be
invisible to external tools. (This was a real bug; see the
`integration_tests/dfs-writeback` suite, which verifies the host image with
`oaknut-dfs` out of process.)

### Hard disc (HDD) write-through

The SCSI hard disc has **no in-memory image buffer and no dirty-tracking**.
`HardDiskImage` holds an open `FILE*`; each SCSI WRITE block is written straight
through with `fseek` + `fwrite` + `fflush` per sector (`HardDiskImage::write_sector`).
By the time a WRITE command returns, the data is already in the OS page cache,
so external programs on the same host see it immediately. The FDD's
"buffered-in-memory, may not be flushed" failure mode therefore **cannot occur**
for the HDD, and the HDD needs none of the flush triggers above.

### Deliberate variance: HDD is OS-cache durable, not `fsync` durable

The HDD calls `fflush` (data to the OS) but **not** `fsync` (data to the storage
device); the FDD's inactivity flush does call `fsync`. This asymmetry is a
deliberate decision, not an oversight:

- The HDD has no pending-write-in-process-memory risk to begin with, so the
  reason the FDD needs prompt flushing simply does not apply.
- `fflush` after every sector already gives the property that matters in
  practice -- writes are immediately visible to other processes and survive a
  process crash or clean exit.
- ADFS/SCSI generates far more, larger and more frequent sector writes than a
  DFS floppy, so per-sector `fsync` would be costly for no benefit to the
  reported failure mode (stale data seen by external tools).

The accepted trade-off is that HDD writes are **not** durable against host
power-loss / kernel panic, only against process and emulator failure. If that
ever needs to change, the right shape is a debounced `fsync` on the SCSI
device's tick (mirroring the FDD's inactivity flush) rather than a per-sector
`fsync`; note the HDD currently has no teardown hook reaching the image, as
`ScsiHardDiscExtension::shutdown()` transfers ownership to the SCSI target
registry.

## Implementation Status

### Complete

- All WD1770 command types (I-IV)
- SSD/DSD format detection and I/O
- Step rate timing (6ms, 12ms, 20ms, 30ms)
- DRQ/INTRQ signal generation with proper timing
- Inter-byte timing (64µs between bytes at 250kbps MFM)
- 1MHz clock synchronization for NMI updates
- Motor control (spin-up at command start, spin-down after ~2s idle)
- Motor spin-up delay (~1.2s, configurable via gRPC for automation)
- Drive activity LED indicators via motor state
- Model B+ hardware integration
- Command-line disc configuration

### Capability Gaps

See [Implementation Gaps](#implementation-gaps) section below for detailed list of unimplemented features that may affect compatibility with copy-protected software.

## Test Coverage

| Category | Tests | Assertions |
|----------|-------|------------|
| DiscGeometry | 20+ | Format detection, offset calculation |
| DiscImage | 9 | Sector I/O, write protection |
| DiscDrive | 15 | Head positioning, insertion/ejection |
| WD1770 | 73 | All command types, timing, status |
| NMI Aggregator | 7 | NMI signal aggregation |
| Integration | 12 | Model B+ register access, DFS *CAT command |

---

## Implementation Gaps

The following features are not implemented. They may be needed for compatibility with specific software:

### High Priority (if compatibility issues arise)

- **Index pulse generation**: I2 flag in Force Interrupt sets INTRQ immediately rather than waiting for index pulse (~200ms per revolution at 300 RPM).
- **Lost data detection**: Currently waits indefinitely for DRQ service. Real hardware sets LOST_DATA after ~64µs.

### Medium Priority

- **Head load/settle timing**: 15ms or 30ms delay not implemented.
- **CRC error detection**: Sector CRC not verified on read.

### Low Priority (copy protection)

- **Track-level synthesis from sector images**: Read Track currently returns concatenated raw sector data. Real hardware (and correctly-emulating emulators) returns the full track structure: gap bytes, sync bytes, ID address marks, sector ID fields (track/side/sector/size + CRC), data address marks, sector data, data CRC, and inter-sector gaps. Software that uses Read Track to inspect track structure (e.g. QuicDisc, copy protection checks) will see incorrect data. Implementing this requires synthesizing a standard DFS track layout from the sector data in SSD/DSD images. See [Stardot discussion](https://stardot.org.uk/forums/viewtopic.php?p=479299#p479299) for context on emulator differences.
- **Write Track format parsing**: Format data stream not parsed for sync bytes, address marks.
- **Deleted data address mark**: RECORD_TYPE status bit not supported.
- **I0/I1 ready transition detection**: Rarely used by BBC software.

### Future Hardware

#### Acorn Controllers

- **Intel 8271**: Required for Model B disc compatibility (uses 0xFE80-0xFE83 for command/status registers, completely different command protocol). Note: The Model B+ was designed to support 8271 but only WD1770 was ever fitted.
- **WD1772**: Faster step rates (2ms, 3ms, 6ms, 12ms).

#### Opus Controllers

Opus Supplies produced four different FDC boards for the BBC Micro, each using a different controller chip and requiring specific ROM versions:

| Controller | Chip | Compatible ROMs |
|------------|------|-----------------|
| Opus DDOS (8272) | Intel 8272A | DDOS 3.00, 3.05 |
| Opus DDOS (2791) | WD2791 | DDOS 3.12, 3.15, 3.16, EDOS 0.4 |
| Opus DDOS (2793) | WD2793 | DDOS 3.35, 3.36 |
| Opus DDOS (1770) | WD1770 | DDOS 3.45, 3.46 |

The WD2791 and WD2793 are predecessors to the WD1770, with slightly different register layouts and timing characteristics. EDOS was a separate filing system from Opus that also used the WD2791.

#### Solidisk Controllers

- **Solidisk 1770 FDC**: WD1770-based controller, typically used with Solidisk DDFS ROM.
- **Solidisk DFDC**: Unique dual-controller board containing both an Intel 8271 and WD1770 with a physical switch to select between them. This allowed users to run both single-density (8271-compatible) and double-density (1770) software on the same machine. The DFDC presents an interesting emulation challenge — effectively two mutually-exclusive controllers sharing the same drives.

#### Other Third-Party Controllers

- **Watford DDFS**: WD1770-based with support for 4 drives.

The variety of third-party disc controllers confirms the need for the pluggable `DiscControllerSocket` architecture to avoid combinatorial explosion of model variants (model-b-8271, model-b-acorn-1770, model-b-opus-2793, model-b-watford, model-b-solidisk-dfdc, etc.).

---

## References

### Primary Sources

| Source | URL | Content |
|--------|-----|---------|
| WD1770 Datasheet | [PDF](https://cdn.hackaday.io/files/256641098008576/WD177x-00.pdf) | Official timing specifications |
| WD1772 Annotated | [PDF](http://info-coach.fr/atari/documents/_mydoc/WD1772-JLG.pdf) | Corrected diagrams/tables |
| Cloud9 WD1770 Docs | [HTML](https://www.cloud9.co.uk/james/BBCMicro/Documentation/wd1770.html) | Signal descriptions, timing |
| Stardot NMI Timing | [Forum](https://stardot.org.uk/forums/viewtopic.php?t=16114) | Real hardware measurements |
| Stardot Read Track | [Forum](https://stardot.org.uk/forums/viewtopic.php?p=479299#p479299) | Track-level synthesis, QuicDisc compatibility across emulators |
| Atari-Forum WD1772 | [Forum](https://www.atari-forum.com/viewtopic.php?t=27448) | Undocumented behaviors |

### Reference Implementations

| Emulator | Location | Notes |
|----------|----------|-------|
| B2 | `/Users/rjs/Code/b2/` | Primary architectural reference |
| MAME | [GitHub](https://github.com/mamedev/mame/blob/master/src/devices/machine/wd_fdc.h) | Comprehensive WD FDC family |
| BeebEm | `/Users/rjs/Code/beebem-mac/` | Both 8271 and WD1770 |
| B-Em | `/Users/rjs/Code/b-em/` | Timing constants |

### Timing Data

#### Step Rates

| Controller | Rate 0 | Rate 1 | Rate 2 | Rate 3 |
|------------|--------|--------|--------|--------|
| WD1770 | 6ms | 12ms | 20ms | 30ms |
| WD1772 | 2ms | 3ms | 6ms | 12ms |

#### Transfer Timing

- Byte transfer: ~64µs (MFM, 300 RPM)
- Command start delay: 16µs
- Settle time: 30ms (WD1770), 15ms (WD1772)
- Motor spin-up: 6 revolutions (~1.2s at 300 RPM)

#### NMI Timing (from real hardware tests)

| Source | X Register | Notes |
|--------|------------|-------|
| Real BBC B | 91-147 | Varies with disc state |
| jsbeeb/b-em/beebem | 216 | |
| MAME/B2 | 217 | |

# Interesting links

- [BBC Micro Disk Controllers](http://www.adsb.co.uk/bbc/disk_controllers/)
- 