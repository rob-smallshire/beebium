# Indicators Subsystem

This document describes Beebium's hardware indicators subsystem for exposing observable machine state (LEDs, motor activity) to external frontends.

## Architecture Overview

The indicators subsystem uses a producer-consumer pattern with a lock-free event queue, enabling the emulation thread to push updates without blocking while a background thread applies filtering and publishes state for gRPC clients.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Emulation Thread                                   │
│                                                                             │
│  ┌──────────────────────┐        ┌──────────────────────┐                  │
│  │ SystemViaPeripheral  │        │      DiscDrive       │                  │
│  │  - caps_lock_led     │        │  - activity_led      │                  │
│  │  - shift_lock_led    │        │                      │                  │
│  └──────────┬───────────┘        └──────────┬───────────┘                  │
│             │                               │                               │
│             │   set(id, value)              │   set(id, value)             │
│             └───────────────┬───────────────┘                               │
│                             ▼                                               │
│                  ┌─────────────────────┐                                   │
│                  │  Lock-Free Queue    │  (non-blocking)                   │
│                  │  (4096 events)      │                                   │
│                  └──────────┬──────────┘                                   │
└─────────────────────────────┼───────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Consumer Thread (~50Hz)                            │
│                                                                             │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │                      Indicator Registry                               │  │
│  │  ┌───────────────────┐  ┌───────────────────┐  ┌──────────────────┐  │  │
│  │  │ caps-lock-led     │  │ shift-lock-led    │  │ floppy-0-act...  │  │  │
│  │  │ DutyCycleFilter   │  │ DutyCycleFilter   │  │ PassthroughFilter│  │  │
│  │  │ value: 127        │  │ value: 0          │  │ value: 255       │  │  │
│  │  └───────────────────┘  └───────────────────┘  └──────────────────┘  │  │
│  └──────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
│                         sequence_++  (on any change)                        │
└─────────────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           gRPC Clients                                       │
│                                                                             │
│  GetIndicators(if_changed_since)  ──►  map<name, value>, sequence           │
│  ListIndicators()                 ──►  [name, metadata]                      │
│  Subscribe()                      ──►  stream of updates                     │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Design Rationale

| Design Choice | Rationale |
|---------------|-----------|
| Lock-free queue | Emulation thread must never block; audio/video timing is critical |
| 50Hz consumer | Matches typical display refresh; ~20ms between samples is sufficient for LEDs |
| Component-owned registration | Each hardware component (SystemViaPeripheral, DiscDrive) registers its own indicators, simplifying hardware struct constructors |
| String names | Frontends identify indicators by name (e.g., "caps-lock-led"), not numeric ID |
| Numeric IDs | Hot path uses 16-bit IDs for efficiency; string lookup only at registration |
| Configurable filters | Different indicators need different filtering (debounce vs duty-cycle) |
| Sequence counter | Enables efficient polling via conditional fetch |

## Components

### IndicatorFilter (Abstract Interface)

**Header:** `src/core/include/beebium/indicators/IndicatorFilter.hpp`

Abstract interface for filtering raw indicator updates into smoothed output values.

```cpp
class IndicatorFilter {
public:
    using time_point = std::chrono::steady_clock::time_point;

    // Accept raw update from emulation (must be fast, non-blocking)
    virtual void update(uint8_t value, time_point timestamp) = 0;

    // Compute filtered output value (called at ~50Hz)
    virtual uint8_t sample(time_point now) = 0;
};
```

### Filter Policies

| Class | Use Case | Behavior |
|-------|----------|----------|
| `PassthroughFilter` | Motor state, stable signals | Returns most recent value unchanged |
| `DebounceFilter` | Spurious transients | Requires stable input for N ms before changing |
| `DutyCycleFilter` | PWM-modulated LEDs | Computes on-time fraction over sliding window |

#### PassthroughFilter

No filtering; immediately reflects the most recent value. Suitable for indicators that don't flicker or are already debounced by hardware.

```cpp
class PassthroughFilter : public IndicatorFilter {
    std::atomic<uint8_t> value_{0};
public:
    void update(uint8_t value, time_point) override {
        value_.store(value, std::memory_order_relaxed);
    }
    uint8_t sample(time_point) override {
        return value_.load(std::memory_order_relaxed);
    }
};
```

#### DebounceFilter

Output only changes after input remains stable for a minimum duration. Use for indicators that may have brief spurious transitions.

```cpp
// Only change output after 50ms of stable input
auto filter = std::make_unique<DebounceFilter>(50ms);
```

#### DutyCycleFilter

Computes the "on-time" as a fraction of a sliding time window, outputting a brightness value 0-255. Essential for the BBC Micro's Caps Lock and Shift Lock LEDs, which are PWM-modulated by the MOS.

```cpp
// 10ms window for LED duty cycle averaging
auto filter = std::make_unique<DutyCycleFilter>(10ms);
```

The filter tracks ON/OFF transitions and calculates:
```
output = (time_on_in_window / window_duration) * 255
```

### Indicators (Registry and Event Processor)

**Header:** `src/core/include/beebium/indicators/Indicators.hpp`

Central registry that manages indicator registration, event queuing, and background processing.

```cpp
class Indicators {
public:
    // Registration (call before start())
    uint16_t register_indicator(
        const std::string& name,
        std::unique_ptr<IndicatorFilter> filter,
        std::unordered_map<std::string, std::string> metadata = {});

    // Hot path: update by ID (non-blocking)
    bool set(uint16_t id, uint8_t value);

    // Convenience: update by name (has string lookup overhead)
    bool set(const std::string& name, uint8_t value);

    // Read published values
    uint8_t get(const std::string& name) const;
    std::unordered_map<std::string, uint8_t> values() const;

    // Metadata and discovery
    std::vector<std::string> names() const;
    std::unordered_map<std::string, std::string> metadata(const std::string& name) const;

    // Change tracking
    uint64_t sequence() const;

    // Lifecycle
    void start();  // Start consumer thread
    void stop();   // Stop consumer thread
};
```

## Hardware Integration

Hardware components receive an `Indicators&` reference in their constructor and register their own indicators during construction. This approach:
- Keeps indicator metadata (name, filter policy, label) with the component that owns it
- Simplifies hardware struct constructors
- Allows components to expose read-only ID accessors for testing

### SystemViaPeripheral

**Header:** `src/core/include/beebium/SystemViaPeripheral.hpp`

Registers the Caps Lock and Shift Lock LEDs, which are controlled via the addressable latch (IC32).

```cpp
class SystemViaPeripheral : public ViaPeripheral {
public:
    // Constructor with indicators (preferred)
    SystemViaPeripheral(AddressableLatch& latch, Indicators& indicators)
        : latch_(latch), indicators_(&indicators)
    {
        register_indicators();
    }

    // Accessor for indicator IDs
    uint16_t caps_lock_led_id() const { return caps_lock_led_id_; }
    uint16_t shift_lock_led_id() const { return shift_lock_led_id_; }

private:
    void register_indicators() {
        using namespace std::chrono_literals;

        caps_lock_led_id_ = indicators_->register_indicator(
            "caps-lock-led",
            std::make_unique<DutyCycleFilter>(10ms),
            {{"label", "CAPS LOCK"}, {"color", "470nm"}, {"shape", "domed"}}
        );

        shift_lock_led_id_ = indicators_->register_indicator(
            "shift-lock-led",
            std::make_unique<DutyCycleFilter>(10ms),
            {{"label", "SHIFT LOCK"}, {"color", "470nm"}, {"shape", "domed"}}
        );
    }

    // Push updates when addressable latch changes LED state
    uint8_t update_port_b(uint8_t output, uint8_t ddr) override {
        uint8_t old_value = latch_.value;
        latch_.write(/* ... */);
        uint8_t new_value = latch_.value;

        if ((old_value ^ new_value) & AddressableLatch::CAPS_LOCK_LED) {
            indicators_->set(caps_lock_led_id_,
                (new_value & AddressableLatch::CAPS_LOCK_LED) ? 255 : 0);
        }
        // ... similar for SHIFT_LOCK_LED
    }
};
```

### DiscDrive

**Header:** `src/core/include/beebium/disc/DiscDrive.hpp`

Registers its activity LED indicator on construction.

```cpp
class DiscDrive {
public:
    // Constructor with indicators
    DiscDrive(Indicators& indicators, const std::string& indicator_name,
              const std::string& label)
        : indicators_(&indicators)
    {
        activity_led_id_ = indicators_->register_indicator(
            indicator_name,
            std::make_unique<PassthroughFilter>(),
            {{"label", label}, {"color", "590nm"}, {"shape", "rectangular"}}
        );
    }

    uint16_t activity_led_id() const { return activity_led_id_; }

    void set_motor(bool on) {
        motor_on_ = on;
        if (indicators_) {
            indicators_->set(activity_led_id_, on ? 255 : 0);
        }
    }
};
```

### Cassette motor LED

**Header:** `src/core/include/beebium/cassette/CassetteMotorLed.hpp`

The keyboard's cassette motor LED is lit with the motor relay, which the Serial ULA's control latch drives: the value is 255 while latch bit 7 (&FE10) is set and 0 while it is clear, whether or not a tape is in the deck. A `CassetteMotorLed` registers `cassette-motor-led` when it is constructed, and its `attach()` connects it to the ULA's motor line. Every hardware policy declares it as a member after the System VIA peripheral (which registers the lock LEDs) and before the disc drives, and calls `attach()` in its constructor, since the serial socket is declared later. Clients list indicators in registration order, so the status bar shows it between SHIFT LOCK and FDD 0, as the storage sidebar puts the cassette above the floppies.

```cpp
class CassetteMotorLed {
public:
    explicit CassetteMotorLed(Indicators& indicators) : indicators_(indicators) {
        std::unordered_map<std::string, std::string> metadata;
        metadata.emplace("label", "CASSETTE MOTOR");
        metadata.emplace("color", "625nm");
        metadata.emplace("shape", "domed");
        id_ = indicators.register_indicator(
            "cassette-motor-led", std::make_unique<PassthroughFilter>(), std::move(metadata));
    }
    void attach(SerialUla& ula);  // light the LED with the motor relay
    // ...
};
```

The filter is a `PassthroughFilter`, so the MOS's 1 ms motor blips pass through unfiltered; a client applies the same hold-off it gives the drive activity LEDs. See [cassette.md](cassette.md).

### Hardware Struct Integration

**Model B Hardware:**
```cpp
struct ModelBHardware {
    // Indicators declared before components that use them
    Indicators indicators;

    AddressableLatch addressable_latch;
    SystemViaPeripheral system_via_peripheral{addressable_latch, indicators};

    ModelBHardware() {
        // ... other initialization ...
        indicators.start();
    }
};
```

**Model B+ Hardware:**
```cpp
struct ModelBPlusHardware {
    Indicators indicators;

    AddressableLatch addressable_latch;
    SystemViaPeripheral system_via_peripheral{addressable_latch, indicators};

    DiscDrive disc_drive_0{indicators, "floppy-0-activity-led", "Drive 0"};
    DiscDrive disc_drive_1{indicators, "floppy-1-activity-led", "Drive 1"};

    ModelBPlusHardware() {
        // ... other initialization ...
        indicators.start();
    }
};
```

## gRPC API

**Proto:** `src/service/proto/indicator.proto`
**Service:** `src/service/include/beebium/service/IndicatorService.hpp`

### Service Definition

```protobuf
service IndicatorService {
    // Get current values of all indicators
    rpc GetIndicators(GetIndicatorsRequest) returns (GetIndicatorsResponse);

    // List all registered indicators with metadata
    rpc ListIndicators(ListIndicatorsRequest) returns (ListIndicatorsResponse);

    // Stream indicator value changes in real-time
    rpc Subscribe(SubscribeIndicatorsRequest) returns (stream IndicatorUpdate);
}
```

### GetIndicators

Batch fetch of all indicator values with optional conditional fetch.

```protobuf
message GetIndicatorsRequest {
    uint64 if_changed_since = 1;  // Optional: skip if sequence unchanged
}

message GetIndicatorsResponse {
    map<string, uint32> values = 1;  // name -> value (0-255)
    uint64 sequence = 2;
    bool changed = 3;                 // false if sequence matched
}
```

**Usage pattern:**
```cpp
// First fetch
auto response = stub->GetIndicators({});
auto seq = response.sequence();

// Subsequent fetches (only get data if changed)
request.set_if_changed_since(seq);
response = stub->GetIndicators(request);
if (response.changed()) {
    // Process new values
    seq = response.sequence();
}
```

### ListIndicators

Returns all registered indicators with their metadata. Call once at startup to discover available indicators and their rendering hints.

```protobuf
message ListIndicatorsResponse {
    repeated IndicatorInfo indicators = 1;
}

message IndicatorInfo {
    string name = 1;
    map<string, string> metadata = 2;  // e.g., {"label": "CAPS LOCK", "color": "470nm"}
}
```

### Subscribe

Server-streaming RPC for real-time updates. Only sends changes, not full state.

```protobuf
message SubscribeIndicatorsRequest {
    repeated string names = 1;    // Filter to specific indicators (empty = all)
    uint32 min_interval_ms = 2;   // Minimum interval between updates (default: 20)
}

message IndicatorUpdate {
    map<string, uint32> values = 1;  // Changed values only
    uint64 sequence = 2;
}
```

## Registered Indicators

### Every model

Every hardware policy registers these five, in this order, which is the order ListIndicators gives and clients show:

| Name | Filter | Label | Color | Shape |
|------|--------|-------|-------|-------|
| `caps-lock-led` | DutyCycle(10ms) | CAPS LOCK | 470nm (blue) | domed |
| `shift-lock-led` | DutyCycle(10ms) | SHIFT LOCK | 470nm (blue) | domed |
| `cassette-motor-led` | Passthrough | CASSETTE MOTOR | 625nm (red) | domed |
| `floppy-0-activity-led` | Passthrough | FDD 0 | 568nm (green) | rectangular |
| `floppy-1-activity-led` | Passthrough | FDD 1 | 568nm (green) | rectangular |

Extensions register theirs after these.

**Note:** The 470nm, 568nm and 625nm wavelengths indicate the approximate LED color (blue, green and red respectively). Frontends can use this metadata for accurate visual rendering.

## Test Coverage

**Test file:** `tests/test_indicators.cpp`, `tests/test_indicator_filter.cpp`

| Category | Tests | Coverage |
|----------|-------|----------|
| PassthroughFilter | 3 | Initial value, update, immediate propagation |
| DebounceFilter | 6 | Stable/unstable input, timing edge cases |
| DutyCycleFilter | 12 | Duty cycle calculation, window sliding, edge cases |
| Indicators registry | 6 | Registration, ID lookup, name lookup, metadata |
| Indicators consumer | 8 | Event processing, sequence increment, start/stop |
| Indicators batch | 3 | Multiple updates, batch API |
| Integration | 4 | SystemViaPeripheral LED updates, DiscDrive motor |

Total: 42 tests

## Implementation Notes

### Thread Safety

- **Event queue:** Lock-free SPSC queue (moodycamel::ReaderWriterQueue)
- **Registry access:** `std::shared_mutex` for read-heavy workload after initialization
- **Published values:** `std::atomic<uint8_t>` for lock-free reads by gRPC clients

### Event Queue Overflow

The event queue holds 4096 events. If the emulation pushes events faster than the consumer processes them (unlikely at 50Hz), old events are dropped. The `set()` method returns `false` if the queue is full.

### PWM and DutyCycleFilter

The BBC Micro's MOS toggles the Caps Lock LED rapidly during certain operations. Without filtering, a 50Hz sample rate would show random on/off flicker. The DutyCycleFilter maintains a sliding window of ON/OFF transitions and computes the actual brightness the user would perceive.

Window size tradeoffs:
- **Too short (1ms):** Still appears flickery
- **Too long (100ms):** Sluggish response to actual state changes
- **10ms:** Good balance for LED-like response

### Legacy Constructors

Both `SystemViaPeripheral` and `DiscDrive` retain constructors that don't take `Indicators&` for backward compatibility with tests that don't need indicator support. In these cases, `set()` calls become no-ops.
