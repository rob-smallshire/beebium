// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "AddressableLatch.hpp"
#include "KeyboardMatrix.hpp"
#include "TypeAheadQueue.hpp"
#include "Via6522.hpp"
#include "devices/Sn76489.hpp"
#include "devices/Upd7002.hpp"
#include "extension/AnaloguePortDevice.hpp"
#include "indicators/IndicatorFilter.hpp"
#include "indicators/Indicators.hpp"
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <memory>

// Keyboard scan diagnostics are temporary measurement scaffolding, compiled in
// only when BEEBIUM_KEYBOARD_SCAN_DIAGNOSTICS is defined (the pacing test target
// sets it). Shipping builds carry no observer member and no per-access branch.
#ifdef BEEBIUM_KEYBOARD_SCAN_DIAGNOSTICS
#include <functional>
#endif

namespace beebium {

// System VIA Peripheral - connects keyboard matrix and addressable latch
//
// The System VIA in a BBC Micro connects to:
// - Port A: Keyboard column data (active low when key pressed)
// - Port B bits 0-2: Addressable latch address
// - Port B bit 3: Addressable latch data
// - Port B bits 4-5: Joystick fire buttons (active low)
// - Port B bits 6-7: Speech/Econet ready (active low)
// - CA2: Keyboard interrupt (directly connected to keyboard matrix)
// - CB1: ADC end of conversion
// - CB2: Light pen strobe
//
// Keyboard scanning modes:
// 1. Auto-scan (KB_WRITE=1): Hardware cycles through columns, CA2 interrupts on keypress
// 2. Manual scan (KB_WRITE=0): MOS writes key number to Port A to check specific key
//
class SystemViaPeripheral : public ViaPeripheral {
public:
    // Full constructor with indicators and external keyboard
    SystemViaPeripheral(AddressableLatch& latch, Indicators& indicators, KeyboardMatrix& keyboard)
        : latch_(latch), keyboard_(keyboard), indicators_(&indicators),
          type_ahead_(std::make_unique<TypeAheadQueue>(keyboard_))
    {
        register_indicators();
    }

    // Constructor with indicators, using internal keyboard
    SystemViaPeripheral(AddressableLatch& latch, Indicators& indicators)
        : latch_(latch), keyboard_(internal_keyboard_), indicators_(&indicators),
          type_ahead_(std::make_unique<TypeAheadQueue>(keyboard_))
    {
        register_indicators();
    }

    // Legacy constructor for backward compatibility (no indicators, uses internal keyboard)
    explicit SystemViaPeripheral(AddressableLatch& latch)
        : latch_(latch), keyboard_(internal_keyboard_),
          type_ahead_(std::make_unique<TypeAheadQueue>(keyboard_)) {}

    uint8_t update_port_a(uint8_t output, uint8_t ddr) override {
        (void)ddr;

        // Store full 8-bit Port A output for sound chip strobe
        port_a_full_output_ = output;

        // Sound chip write (gated by addressable latch bit 0, active low)
        //
        // Two write patterns are supported:
        // 1. Write while SOUND_WRITE is already enabled (handled here)
        // 2. Strobe: Write Port A, then toggle SOUND_WRITE (handled in update_port_b)
        //
        // After reset, SOUND_WRITE starts LOW (enabled), so the MOS can write
        // to the sound chip just by writing to Port A without toggling the latch.
        if (sound_chip_ && latch_.sound_write_enabled()) {
            sound_chip_->write(output);
        }

        // Port A reads keyboard matrix data
        // The MOS writes a key number to Port A (bits 0-6) where:
        //   - Bits 0-3: column (0-15, but only 0-9 used)
        //   - Bits 4-6: row (0-7, rows 8-9 use columns 10+)
        // Then reads back bit 7 to check if that key is pressed.
        //
        // IMPORTANT: The bit 7 convention is:
        //   - Bit 7 = 0: Key/link NOT pressed (open/broken)
        //   - Bit 7 = 1: Key/link IS pressed (closed/made)
        //
        // This is the software convention used by all emulators (BeebEm, B2),
        // even though the physical hardware is active-low.

        uint8_t key_number = output & 0x7F;
        port_a_output_ = key_number;  // Store for CA2 calculation in manual scan mode

        // Extract row and column from key number
        // Standard BBC keyboard scanning: column = bits 0-3, row = bits 4-6
        uint8_t column = key_number & 0x0F;
        uint8_t row = (key_number >> 4) & 0x07;

        bool pressed = false;

        if (row == 0 && column >= 2 && column <= 9) {
            // Row 0, columns 2-9 are startup links (DIP switches)
            // Read from the startup options byte instead of key matrix
            pressed = keyboard_.is_link_made(column);
        } else {
            // Regular key matrix lookup
            pressed = keyboard_.is_key_pressed(row, column);
        }

#ifdef BEEBIUM_KEYBOARD_SCAN_DIAGNOSTICS
        // Observe the MOS keyboard scan: every Port A access that probes a key
        // (the MOS writes a key number, then reads back bit 7) passes through
        // here. Reveals the scan cadence without injecting anything. Present
        // only in diagnostic builds.
        if (keyboard_probe_observer_) {
            keyboard_probe_observer_(row, column, pressed);
        }
#endif

        // Return the key number with bit 7 set if pressed/made
        if (pressed) {
            return key_number | 0x80;
        } else {
            return key_number & 0x7F;
        }
    }

    uint8_t update_port_b(uint8_t output, uint8_t ddr) override {
        (void)ddr;

        // Port B outputs control the addressable latch:
        // Bits 0-2: Latch address (selects which of 8 latch bits)
        // Bit 3: Latch data value
        uint8_t latch_addr = output & 0x07;
        bool latch_data = (output & 0x08) != 0;

        // Save old latch value to detect sound write and LED changes
        uint8_t old_value = latch_.value;
        latch_.write(latch_addr, latch_data);
        uint8_t new_value = latch_.value;

        // Sound chip write strobe detection
        //
        // The BBC Micro's sound write sequence is:
        // 1. MOS writes data byte to VIA Port A
        // 2. MOS pulls SOUND_WRITE LOW (via addressable latch bit 0)
        // 3. MOS waits >= 8µs
        // 4. MOS pulls SOUND_WRITE HIGH again
        //
        // The sound chip latches data on the falling edge (HIGH→LOW) of
        // SOUND_WRITE. We detect this transition and write Port A to chip.
        //
        // Note: SOUND_WRITE is active LOW, so bit 0 = 0 means ENABLED
        bool old_sound_write = (old_value & AddressableLatch::SOUND_WRITE) != 0;
        bool new_sound_write = (new_value & AddressableLatch::SOUND_WRITE) != 0;

        if (sound_chip_ && old_sound_write && !new_sound_write) {
            // Falling edge: SOUND_WRITE went from HIGH (disabled) to LOW (enabled)
            // Write current Port A output value to sound chip
            sound_chip_->write(port_a_full_output_);
        }

        // Push indicator updates if LEDs changed
        // Note: LEDs are active-low (bit clear = LED on, bit set = LED off)
        if (indicators_ && ((old_value ^ new_value) & 0xC0)) {
            if ((old_value ^ new_value) & AddressableLatch::CAPS_LOCK_LED) {
                indicators_->set(caps_lock_led_id_,
                    (new_value & AddressableLatch::CAPS_LOCK_LED) ? 0 : 255);
            }
            if ((old_value ^ new_value) & AddressableLatch::SHIFT_LOCK_LED) {
                indicators_->set(shift_lock_led_id_,
                    (new_value & AddressableLatch::SHIFT_LOCK_LED) ? 0 : 255);
            }
        }

        // Port B input bits:
        // Bits 4, 5: the analogue port's button lines (active low; the
        //            pull-ups read 1 with nothing pressed or no device)
        // Bit 6: Speech ready (active low) - return 1 = ready
        // Bit 7: Speech interrupt (active low) - return 1 = no interrupt
        const uint8_t buttons =
            analogue_port_device_ ? (analogue_port_device_->button_lines() & 0x30) : 0x30;
        return 0xC0 | buttons;
    }

    void update_control_lines(uint8_t& ca1, uint8_t& ca2,
                              uint8_t& cb1, uint8_t& cb2) override {

        // CB1: the uPD7002's EOC pin, a level. The VIA does the edge
        // detection under its own PCR; nothing here looks for edges.
        cb1 = analogue_converter_ ? analogue_converter_->eoc_level() : 1;

        // CA1: VSYNC from CRTC
        // jsbeeb convention: CA1 high when vsync active, low when inactive.
        // With PCR bit 0 = 0 (negative edge detection), interrupt fires when
        // CA1 goes high→low, which occurs when vsync ENDS.
        // This matches jsbeeb's setVBlankInt(inVSync) behavior.
        ca1 = vsync_ ? 1 : 0;

        // CA2: Keyboard matrix interrupt
        // The MOS configures CA2 for POSITIVE edge detection (PCR bits 1-3 = 0b010).
        // This means the interrupt fires on a LOW→HIGH transition.
        //
        // BBC Keyboard scanning has two modes:
        // 1. Auto-scan (KB_WRITE=1): Hardware cycles through columns automatically
        // 2. Manual scan (KB_WRITE=0): CPU writes key number to Port A to check
        //
        // In auto-scan mode (matching B2 emulator):
        // - Hardware cycles through columns 0-15
        // - CA2 = 0 when NO key pressed in current column
        // - CA2 = 1 when a key IS pressed in current column (triggers interrupt)
        //
        if (latch_.keyboard_enabled()) {
            // Manual scan mode (KB_WRITE=0) - CA2 based on current scan result
            uint8_t column = port_a_output_ & 0x0F;
            bool key_pressed = keyboard_.any_key_in_column(column, true);  // Exclude row 0
            if (!key_pressed) {
                ca2 = 0;
            }
        } else {
            // Auto-scan mode (KB_WRITE=1) - cycle through columns
            // Check if any key (rows 1-9) pressed in current auto-scan column
            bool key_pressed = keyboard_.any_key_in_column(auto_scan_column_, true);

            if (!key_pressed) {
                ca2 = 0;
            }
            // If key IS pressed, leave ca2 = 1 (VIA's default), triggering positive edge

            // Advance auto-scan column for next update
            auto_scan_column_ = (auto_scan_column_ + 1) & 0x0F;
        }

        // CB2: the analogue port's light pen strobe. With no device the line
        // is left as the VIA has it.
        if (analogue_port_device_) {
            cb2 = analogue_port_device_->light_pen_strobe();
        }
    }

    // Set VSYNC state (called by video hardware)
    void set_vsync(bool active) {
        if (active && !vsync_) ++vsync_rising_edges_;
        vsync_ = active;
    }

    bool vsync() const { return vsync_; }

    // Keyboard input control - delegates to KeyboardMatrix
    void key_down(uint8_t row, uint8_t column) {
        keyboard_.key_down(row, column);
    }

    void key_up(uint8_t row, uint8_t column) {
        keyboard_.key_up(row, column);
    }

    bool is_key_pressed(uint8_t row, uint8_t column) const {
        return keyboard_.is_key_pressed(row, column);
    }

    uint16_t get_row_state(uint8_t row) const {
        return keyboard_.get_row_state(row);
    }

    void clear_all_keys() {
        keyboard_.clear();
    }

    // Access to keyboard matrix (for direct manipulation if needed)
    KeyboardMatrix& keyboard() { return keyboard_; }
    const KeyboardMatrix& keyboard() const { return keyboard_; }

    // Accessor for testing/debugging
    AddressableLatch& latch() { return latch_; }
    const AddressableLatch& latch() const { return latch_; }
    uint8_t keyboard_column() const { return auto_scan_column_; }

    // Indicator ID accessors (valid only if constructed with Indicators)
    uint16_t caps_lock_led_id() const { return caps_lock_led_id_; }
    uint16_t shift_lock_led_id() const { return shift_lock_led_id_; }

    // Type-ahead queue access (for gRPC service)
    TypeAheadQueue& type_ahead() { return *type_ahead_; }
    const TypeAheadQueue& type_ahead() const { return *type_ahead_; }

    // Tick the type-ahead queue (call from emulator main loop)
    void tick_type_ahead() { type_ahead_->tick(); }

#ifdef BEEBIUM_KEYBOARD_SCAN_DIAGNOSTICS
    // Diagnostic: observe every MOS Port A key probe (row, column, pressed).
    // Pass nullptr to disable. Intended for measuring keyboard scan cadence.
    using KeyboardProbeObserver = std::function<void(uint8_t row, uint8_t column, bool pressed)>;
    void set_keyboard_probe_observer(KeyboardProbeObserver observer) {
        keyboard_probe_observer_ = std::move(observer);
    }
#endif

    // Sound chip integration (optional)
    void set_sound_chip(Sn76489* chip) { sound_chip_ = chip; }
    void set_analogue_converter(const Upd7002* adc) { analogue_converter_ = adc; }

    // The analogue port's device: its button lines become PB4 and PB5, its
    // light pen strobe CB2. Throws std::runtime_error if a device is already
    // attached (the port is one connector).
    void attach_analogue_port_device(AnaloguePortDevice& device) {
        if (analogue_port_device_) {
            throw std::runtime_error(
                "AnaloguePort: a device is already attached (only one analogue port device is supported)");
        }
        analogue_port_device_ = &device;
    }

    bool has_analogue_port_device() const { return analogue_port_device_ != nullptr; }

    // The light pen strobe level on the analogue port, for the CRTC's light
    // pen input: 0 with no device.
    uint8_t light_pen_strobe() {
        return analogue_port_device_ ? analogue_port_device_->light_pen_strobe() : 0;
    }
    Sn76489* sound_chip() const { return sound_chip_; }

private:
    void register_indicators() {
        using namespace std::chrono_literals;

        // Build metadata maps explicitly using emplace to avoid any potential
        // issues with initializer-list construction on different architectures
        std::unordered_map<std::string, std::string> caps_metadata;
        caps_metadata.emplace("label", "CAPS LOCK");
        caps_metadata.emplace("color", "625nm");
        caps_metadata.emplace("shape", "domed");
        caps_metadata.emplace("related_key", "Caps Lock");

        caps_lock_led_id_ = indicators_->register_indicator(
            "caps-lock-led",
            std::make_unique<QuantizedDutyCycleFilter<2>>(100ms),
            std::move(caps_metadata)
        );

        std::unordered_map<std::string, std::string> shift_metadata;
        shift_metadata.emplace("label", "SHIFT LOCK");
        shift_metadata.emplace("color", "625nm");
        shift_metadata.emplace("shape", "domed");
        shift_metadata.emplace("related_key", "Shift Lock");

        shift_lock_led_id_ = indicators_->register_indicator(
            "shift-lock-led",
            std::make_unique<QuantizedDutyCycleFilter<2>>(100ms),
            std::move(shift_metadata)
        );
    }

    AddressableLatch& latch_;
    KeyboardMatrix& keyboard_;
    KeyboardMatrix internal_keyboard_;  // Used when no external keyboard provided

    uint8_t port_a_output_ = 0;      // Last Port A output (key number for manual scan)
    uint8_t port_a_full_output_ = 0; // Full 8-bit Port A output (for sound chip strobe)
    uint8_t auto_scan_column_ = 0;   // Hardware auto-scan column counter (0-15)
    bool vsync_ = false;
    uint32_t vsync_rising_edges_ = 0;
public:
    /// Read and reset VSYNC edge counter (for diagnostics).
    uint32_t consume_vsync_rising_edges() {
        uint32_t count = vsync_rising_edges_;
        vsync_rising_edges_ = 0;
        return count;
    }
private:

    // Indicator integration (optional)
    Indicators* indicators_ = nullptr;
    uint16_t caps_lock_led_id_ = 0;
    uint16_t shift_lock_led_id_ = 0;

    // Type-ahead queue for TypeQuickly functionality
    std::unique_ptr<TypeAheadQueue> type_ahead_;

#ifdef BEEBIUM_KEYBOARD_SCAN_DIAGNOSTICS
    // Diagnostic observer for MOS keyboard scan probes (diagnostic builds only)
    KeyboardProbeObserver keyboard_probe_observer_;
#endif

    // Sound chip integration (optional)
    Sn76489* sound_chip_ = nullptr;
    const Upd7002* analogue_converter_ = nullptr;
    AnaloguePortDevice* analogue_port_device_ = nullptr;
};

} // namespace beebium
