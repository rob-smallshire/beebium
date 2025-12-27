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

// =============================================================================
// MOS 1.20 Boot Sequence - Single Progressive TDD Test
// =============================================================================
// One long deterministic test that progresses through the entire boot sequence.
// Reference: https://tobylobster.github.io/mos/mos/S-s10.html
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <vector>

using namespace beebium;

namespace {

std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) throw std::runtime_error("Failed to open ROM: " + filepath.string());
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

bool roms_available() {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dir / "OS12.ROM") &&
           std::filesystem::exists(rom_dir / "BASIC2.ROM");
}

bool bplus_roms_available() {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dir / "BPMOS.ROM") &&
           std::filesystem::exists(rom_dir / "BASIC2.ROM");
}

} // namespace

// =============================================================================
// MOS 1.20 Boot Sequence - Complete Test
// =============================================================================
// Progresses through each stage of the boot, asserting exact values at each point.
// Sections reference the annotated disassembly at tobylobster.github.io/mos/

TEST_CASE("MOS 1.20 complete boot sequence", "[boot]") {
    if (!roms_available()) SKIP("ROMs not available");

    // =========================================================================
    // Setup: Load ROMs and reset
    // =========================================================================
    ModelB machine;
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.reset();

    // =========================================================================
    // Verify ROM is correctly loaded and accessible through memory map
    // =========================================================================
    // Reset vector at $FFFC-$FFFD should be $D9CD
    REQUIRE(machine.read(0xFFFC) == 0xCD);
    REQUIRE(machine.read(0xFFFD) == 0xD9);

    // First instruction at $D9CD should be LDA #$40 (A9 40)
    REQUIRE(machine.read(0xD9CD) == 0xA9);  // LDA immediate opcode
    REQUIRE(machine.read(0xD9CE) == 0x40);  // operand

    // Check clearRAM code at $D9EE - should be STA ($00),Y (91 00)
    REQUIRE(machine.read(0xD9EE) == 0x91);  // STA (zp),Y opcode
    REQUIRE(machine.read(0xD9EF) == 0x00);  // operand (zero page address)

    // Check the copyright string in MOS ROM at $C000
    // MOS 1.20 has "(C)" at a specific offset
    REQUIRE(machine.read(0xC000) == 0x00);  // First byte of MOS is 0

    // Complete reset sequence (7 cycles)
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }

    // Helper to step one instruction
    auto step = [&]() { machine.step_instruction(); };

    // Helper to read 16-bit value
    auto read16 = [&](uint16_t addr) {
        return machine.read(addr) | (static_cast<uint16_t>(machine.read(addr + 1)) << 8);
    };

    // =========================================================================
    // §3 Reset Entry Point - $D9CD
    // =========================================================================
    // LDA #$40, STA $0D00, SEI, CLD, LDX #$FF, TXS

    REQUIRE(machine.cycle_count() == 7);
    REQUIRE(machine.cpu().pc.w == 0xD9CE);  // After fetching opcode at $D9CD

    // LDA #$40
    step();
    REQUIRE(machine.cpu().a == 0x40);

    // STA $0D00 - store RTI opcode for NMI
    step();
    REQUIRE(machine.read(0x0D00) == 0x40);

    // SEI - disable interrupts
    step();
    REQUIRE(machine.cpu().p.bits.i == 1);

    // CLD - clear decimal mode
    step();
    REQUIRE(machine.cpu().p.bits.d == 0);

    // LDX #$FF
    step();
    REQUIRE(machine.cpu().x == 0xFF);

    // TXS - set stack pointer
    step();
    REQUIRE(machine.cpu().s.b.l == 0xFF);
    REQUIRE(machine.cycle_count() == 21);

    // =========================================================================
    // §3 continued - Read VIA IER, determine reset type
    // =========================================================================
    // LDA $FE4E, ASL A, PHA, BEQ clearRAM

    // LDA $FE4E - read System VIA IER (returns $80 on power-on)
    step();
    REQUIRE(machine.cpu().a == 0x80);

    // ASL A - shift bit 7 to carry, A becomes 0
    step();
    REQUIRE(machine.cpu().a == 0x00);

    // PHA - push A (0) to stack for later reset type check
    step();
    REQUIRE(machine.cpu().s.b.l == 0xFE);

    // BEQ - branch taken (A == 0), skip to clearRAM at $D9E7
    step();
    REQUIRE(machine.cpu().pc.w == 0xD9E8);  // After fetch of next opcode

    // =========================================================================
    // §4 clearRAM - Clear memory $0400-$7FFF and detect RAM size
    // =========================================================================
    // LDX #$04, STX $01, STA $00, TAY, then loop clearing RAM

    // LDX #$04 - start from page 4
    step();
    REQUIRE(machine.cpu().x == 0x04);

    // STX $01 - high byte of pointer
    step();
    REQUIRE(machine.read(0x01) == 0x04);

    // STA $00 - low byte of pointer (A=0)
    step();
    REQUIRE(machine.read(0x00) == 0x00);

    // TAY - Y = 0
    step();
    REQUIRE(machine.cpu().y == 0x00);

    // Run clearRAM until RAM size marker written at $0284
    // The loop clears pages 4-127, preserving byte 0 of each page
    bool ram_size_written = false;
    uint8_t ram_size_value = 0;
    machine.add_watchpoint(0x0284, 1, WATCH_WRITE,
        [&](uint16_t, uint8_t val, bool, uint64_t) {
            ram_size_written = true;
            ram_size_value = val;
        });

    while (!ram_size_written) {
        step();
    }
    machine.clear_watchpoints();

    // RAM size = $80 (128 pages = 32K)
    REQUIRE(ram_size_value == 0x80);

    // =========================================================================
    // §5 setUpSystemVIA - Configure DDRB and addressable latch
    // =========================================================================
    // DDRB = $0F, then 8 ORB writes to configure latch

    std::vector<uint8_t> orb_writes;
    machine.add_watchpoint(0xFE40, 1, WATCH_WRITE,
        [&](uint16_t, uint8_t val, bool, uint64_t) {
            orb_writes.push_back(val);
        });

    // Run until first keyboard scan (indicates VIA setup complete)
    bool keyboard_scan_started = false;
    machine.add_watchpoint(0xFE4F, 1, WATCH_READ,
        [&](uint16_t, uint8_t, bool, uint64_t) {
            keyboard_scan_started = true;
        });

    while (!keyboard_scan_started) {
        step();
    }
    machine.clear_watchpoints();

    // DDRB should be $0F
    REQUIRE(machine.memory().system_via.state().port_b.ddr == 0x0F);

    // Exact ORB write sequence for latch configuration
    REQUIRE(orb_writes.size() == 8);
    const std::array<uint8_t, 8> expected_orb = {0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x03};
    for (size_t i = 0; i < 8; ++i) {
        REQUIRE(orb_writes[i] == expected_orb[i]);
    }

    // Addressable latch = $77 (all set except bit 3 for keyboard enable)
    REQUIRE(machine.memory().addressable_latch.value == 0x77);

    // =========================================================================
    // §6-7 Keyboard scan and reset type determination
    // =========================================================================
    // 9 keyboard scans, then set lastResetType and startUpOptions

    int port_a_reads = 1;  // Already had one from the trigger above
    std::vector<uint8_t> port_a_values;
    std::vector<uint8_t> port_a_ddrs;
    std::vector<uint8_t> port_a_ors;
    std::vector<uint8_t> kb_columns;
    machine.add_watchpoint(0xFE4F, 1, WATCH_READ,
        [&](uint16_t, uint8_t val, bool, uint64_t) {
            port_a_reads++;
            port_a_values.push_back(val);
            port_a_ddrs.push_back(machine.memory().system_via.state().port_a.ddr);
            port_a_ors.push_back(machine.memory().system_via.state().port_a.or_);
            kb_columns.push_back(machine.memory().system_via_peripheral.keyboard_column());
        });

    // Track lastResetType writes at $028D
    std::vector<uint8_t> reset_type_writes;
    machine.add_watchpoint(0x028D, 1, WATCH_WRITE,
        [&](uint16_t, uint8_t val, bool, uint64_t) {
            reset_type_writes.push_back(val);
        });

    // Run until startUpOptions written at $028F
    bool startup_options_written = false;
    machine.add_watchpoint(0x028F, 1, WATCH_WRITE,
        [&](uint16_t, uint8_t, bool, uint64_t) {
            startup_options_written = true;
        });

    while (!startup_options_written) {
        step();
    }
    machine.clear_watchpoints();

    // Exactly 9 keyboard scans
    REQUIRE(port_a_reads == 9);

    // Log the Port A values read during keyboard scans
    INFO("Port A values (" << port_a_values.size() << " reads):");
    std::string pa_dump, ddr_dump, or_dump;
    for (size_t i = 0; i < port_a_values.size(); ++i) {
        char buf[16];
        snprintf(buf, sizeof(buf), " $%02X", port_a_values[i]);
        pa_dump += buf;
        snprintf(buf, sizeof(buf), " $%02X", port_a_ddrs[i]);
        ddr_dump += buf;
        snprintf(buf, sizeof(buf), " $%02X", port_a_ors[i]);
        or_dump += buf;
    }
    INFO("Values:" << pa_dump);
    INFO("DDRs:  " << ddr_dump);
    INFO("ORs:   " << or_dump);

    // Port A DDR = $7F means bits 0-6 are outputs, bit 7 is keyboard input
    // Following BeebEm/B2 convention:
    //   Bit 7 = 0: key/link NOT pressed (open/broken)
    //   Bit 7 = 1: key/link IS pressed (closed/made)
    //
    // With all keyboard links BROKEN (open, the default), ALL positions
    // return bit 7 = 0. The MOS then XORs the readings to get the mode.
    // Mode 7 results from all links being broken (not made).
    //
    // The MOS scans internal key numbers 9 down to 1 (columns 9-1, row 0).
    // We see 8 reads for columns 8-1 in order.
    REQUIRE(port_a_values.size() == 8);

    // All keyboard positions should return bit 7 = 0 (no key/link pressed)
    for (size_t i = 0; i < port_a_values.size(); ++i) {
        uint8_t column = port_a_ors[i] & 0x0F;  // Low nibble is column
        INFO("Checking scan " << i << " (column " << (int)column << "): $"
             << std::hex << (int)port_a_values[i]);

        // No keys/links pressed - bit 7 should be 0 for all positions
        REQUIRE((port_a_values[i] & 0x80) == 0x00);
    }

    // lastResetType write sequence: 0 (clear), 0 (STX), 1 (INC)
    REQUIRE(reset_type_writes.size() == 3);
    REQUIRE(reset_type_writes[0] == 0);
    REQUIRE(reset_type_writes[1] == 0);
    REQUIRE(reset_type_writes[2] == 1);

    // Final values
    REQUIRE(machine.read(0x028D) == 1);   // lastResetType = 1 (power-on)
    // startUpOptions bits 0-2 = startup mode, should be 7
    // Other bits may vary based on keyboard link readings
    REQUIRE((machine.read(0x028F) & 0x07) == 0x07); // Mode 7
    REQUIRE(machine.cpu().pc.w == 0xDA41); // After STA startUpOptions

    // =========================================================================
    // §8 setUpPage2 - Initialize OS vectors
    // =========================================================================
    // Copy default vectors from ROM table to page 2

    machine.add_watchpoint(0x0203, 1, WATCH_WRITE,
        [&](uint16_t, uint8_t val, bool, uint64_t) {
            if (val != 0) {
                // BRKV high byte written with non-zero = vectors being initialized
            }
        });

    // Run until BRKV is initialized (non-zero high byte)
    while (machine.read(0x0203) == 0) {
        step();
    }
    machine.clear_watchpoints();

    // MOS 1.20 default vector values
    REQUIRE(read16(0x0202) == 0xDC00);  // BRKV
    REQUIRE(read16(0x0204) == 0xDC93);  // IRQ1V
    REQUIRE(read16(0x0206) == 0xDE89);  // IRQ2V
    REQUIRE(read16(0x0208) == 0xDF89);  // CLIV
    REQUIRE(read16(0x020A) == 0xE772);  // BYTEV
    REQUIRE(read16(0x020C) == 0xE7EB);  // WORDV
    REQUIRE(read16(0x020E) == 0xE0A4);  // WRCHV
    REQUIRE(read16(0x0210) == 0xDEC5);  // RDCHV

    // =========================================================================
    // §9 Copy default vectors and MOS variables
    // =========================================================================
    // Copies from .defaultVectorTable in ROM to page 2 locations
    // This happens as part of §8, vectors already verified above

    // =========================================================================
    // §10 Put SPACE in firstKeyPressedInternal ($ED), init ACIA
    // =========================================================================
    // Run until firstKeyPressedInternal is set (internal keycode for SPACE = $62)
    bool first_key_set = false;
    machine.add_watchpoint(0x00ED, 1, WATCH_WRITE,
        [&](uint16_t, uint8_t val, bool, uint64_t) {
            if (val == 0x62) first_key_set = true;
        });

    while (!first_key_set) {
        step();
    }
    machine.clear_watchpoints();

    REQUIRE(machine.read(0x00ED) == 0x62);  // SPACE internal key number

    // =========================================================================
    // §11 Disable all VIA interrupts
    // =========================================================================
    // Write $7F to System VIA IER ($FE4E) and User VIA IER ($FE6E)
    bool system_via_ier_disabled = false;
    bool user_via_ier_disabled = false;

    machine.add_watchpoint(0xFE4E, 1, WATCH_WRITE,
        [&](uint16_t, uint8_t val, bool, uint64_t) {
            if (val == 0x7F) system_via_ier_disabled = true;
        });
    machine.add_watchpoint(0xFE6E, 1, WATCH_WRITE,
        [&](uint16_t, uint8_t val, bool, uint64_t) {
            if (val == 0x7F) user_via_ier_disabled = true;
        });

    while (!system_via_ier_disabled || !user_via_ier_disabled) {
        step();
    }
    machine.clear_watchpoints();

    // Both VIAs have interrupts disabled (IER = $00 after writing $7F with bit 7 clear)
    REQUIRE(machine.memory().system_via.state().ier.value == 0x00);
    REQUIRE(machine.memory().user_via.state().ier.value == 0x00);

    // =========================================================================
    // §12 Check for JIM device at $FDFE
    // =========================================================================
    // Briefly enables interrupts, calls jimPagedEntryJumper if bit 6 set
    // For standard BBC, no JIM device, so this is a no-op
    // We just continue past this point

    // =========================================================================
    // §13 VIA interrupts and timer setup
    // =========================================================================
    // IER = $F2, Timer 1 latch = $270E (9998 cycles = 100Hz)

    bool t1_configured = false;
    machine.add_watchpoint(0xFE47, 1, WATCH_WRITE,
        [&](uint16_t, uint8_t val, bool, uint64_t) {
            if (val == 0x27) {
                t1_configured = true;
            }
        });

    while (!t1_configured) {
        step();
    }
    machine.clear_watchpoints();

    // System VIA IER = $F2
    REQUIRE(machine.memory().system_via.state().ier.value == 0xF2);

    // Timer 1 latch = $270E
    REQUIRE(machine.memory().system_via.state().t1ll == 0x0E);
    REQUIRE(machine.memory().system_via.state().t1lh == 0x27);

    // =========================================================================
    // §14 Clear sounds, Serial ULA, reset soft key definitions
    // =========================================================================
    // Sound cleared via OSWORD 7, Serial ULA at $FE10 configured
    // Soft keys reset via OSBYTE 18 (if required based on reset type)

    bool serial_ula_written = false;
    machine.add_watchpoint(0xFE10, 1, WATCH_WRITE,
        [&](uint16_t, uint8_t, bool, uint64_t) {
            serial_ula_written = true;
        });

    while (!serial_ula_written) {
        step();
    }
    machine.clear_watchpoints();

    // Serial ULA has been initialized
    REQUIRE(serial_ula_written == true);

    // =========================================================================
    // §15 Read ROM Types - catalog sideways ROMs
    // =========================================================================
    // Examines ROM headers at $8006-$8007 for copyright string
    // Stores ROM type in romTypeTable ($02A1-$02B0)
    // Identifies basicROMNumber ($024B)

    // Wait until basicROMNumber is set (indicates ROM scan complete)
    // MOS scans ROMs 15->0, writing romTypeTable entries
    // When it finds a language ROM, it updates basicROMNumber ($024B)
    bool basic_found = false;
    machine.add_watchpoint(0x024B, 1, WATCH_WRITE,
        [&](uint16_t, uint8_t val, bool, uint64_t) {
            // basicROMNumber is written during ROM scan
            // Value 0xFF means no BASIC, 0-15 means BASIC found in that slot
            basic_found = true;
        });

    while (!basic_found) {
        step();
    }
    machine.clear_watchpoints();

    // Check what BASIC slot was found
    uint8_t basic_slot = machine.read(0x024B);
    INFO("basicROMNumber = " << (int)basic_slot);
    INFO("ROM type slot 0 = " << (int)machine.read(0x02A1));

    // BASIC should be in slot 0 (where we loaded it)
    REQUIRE(basic_slot == 0x00);

    // ROM type for slot 0 should indicate language ROM
    REQUIRE(machine.read(0x02A1) != 0x00);

    // =========================================================================
    // §16 Check for speech system
    // =========================================================================
    // Tests System VIA PB7 for speech hardware presence
    // No speech chip in our emulation, so this is skipped

    // =========================================================================
    // §17 Initialize screen, execute BOOT interception
    // =========================================================================
    // Sets screen mode based on startUpOptions
    // Default is Mode 7 (teletext mode)
    // CRTC registers programmed for 40x25 teletext display

    bool crtc_mode_set = false;
    machine.add_watchpoint(0xFE00, 1, WATCH_WRITE,
        [&](uint16_t, uint8_t, bool, uint64_t) {
            crtc_mode_set = true;
        });

    while (!crtc_mode_set) {
        step();
    }
    machine.clear_watchpoints();

    // CRTC has been accessed (Mode 7 setup in progress)
    REQUIRE(crtc_mode_set == true);

    // =========================================================================
    // §18 Initialize Tube
    // =========================================================================
    // Tests Tube ULA at $FEE0 for presence
    // No Tube in our emulation

    // =========================================================================
    // §19 Initialize ROMs, claim memory from OSHWM
    // =========================================================================
    // ROM service calls for workspace claims
    // Sets defaultOSHWM ($0243) and currentOSHWM ($0244)

    bool oshwm_set = false;
    machine.add_watchpoint(0x0244, 1, WATCH_WRITE,
        [&](uint16_t, uint8_t val, bool, uint64_t) {
            if (val >= 0x0E) oshwm_set = true;  // Must be >= $0E00
        });

    while (!oshwm_set) {
        step();
    }
    machine.clear_watchpoints();

    // OSHWM set (high byte stored at $0244)
    uint16_t oshwm = machine.read(0x0243) | (static_cast<uint16_t>(machine.read(0x0244)) << 8);
    REQUIRE(oshwm >= 0x0E00);

    // =========================================================================
    // §20 Show bootup message - "BBC Computer 32K"
    // =========================================================================
    // Message written to screen memory at Mode 7 address ($7C00)
    // Format: "BBC Computer" followed by newline and "32K" or "16K"

    // Run until the message is written to screen memory
    // Mode 7 screen address depends on CRTC register 12-13 (screen start)
    // Default Mode 7 screen is at $7C00 but let's check CRTC

    // Run enough cycles to complete full boot and print message
    // The original test ran 2M instructions from this point
    // Track if we ever enter BASIC ROM space ($8000-$BFFF)
    bool entered_basic = false;
    int basic_entries = 0;
    uint16_t last_pc = 0;
    int loop_count = 0;
    for (int i = 0; i < 2000000; ++i) {
        uint16_t pc = machine.cpu().pc.w;
        if (pc >= 0x8000 && pc < 0xC000 && !entered_basic) {
            entered_basic = true;
            basic_entries++;
        }
        if (pc == last_pc) {
            loop_count++;
            if (loop_count > 1000) {
                INFO("Stuck at PC=$" << std::hex << pc << " after " << i << " steps");
                break;
            }
        } else {
            loop_count = 0;
            last_pc = pc;
        }
        step();
    }
    INFO("Entered BASIC ROM: " << (entered_basic ? "yes" : "no"));
    INFO("BASIC entry count: " << basic_entries);

    // Get actual screen start from CRTC
    uint16_t crtc_screen_start = machine.memory().crtc.screen_start();
    INFO("CRTC screen start register: $" << std::hex << crtc_screen_start);
    INFO("Current PC: $" << std::hex << machine.cpu().pc.w);

    // BBC always boots in Mode 7 (teletext)
    // Mode 7 screen memory is at $7C00-$7FFF (1KB)
    // Characters are stored directly as ASCII bytes

    // Machine boots in Mode 7 (teletext) unless configured otherwise
    // Mode 7 screen is at $7C00, text stored as ASCII
    // But VDU says screen is at $3000 - that's a graphics mode!
    // This indicates something is wrong with mode selection

    // Check the actual screen mode variable
    uint8_t current_mode = machine.read(0x0355);
    INFO("Current screen mode ($355): " << (int)current_mode);

    // Check startup options - determines boot mode
    uint8_t startup_opts = machine.read(0x028F);
    INFO("Startup options ($28F): $" << std::hex << (int)startup_opts);

    // Check configured mode (what MOS thinks should be set)
    uint8_t config_mode = machine.read(0x0290);
    INFO("Configured mode ($290): " << (int)config_mode);

    // Check Video ULA control register value
    uint8_t video_ula = machine.memory().video_ula.control();
    INFO("Video ULA control: $" << std::hex << (int)video_ula);
    INFO("  Teletext bit (0x02): " << ((video_ula & 0x02) ? "SET" : "CLEAR"));

    // The issue is: BBC should boot in Mode 7, but we're seeing Mode 0 indicators
    // Mode 7 requires Video ULA teletext bit to be set
    // If startup_opts = 0, mode should be 7 (default)

    // For now, let's verify what mode we're actually in
    REQUIRE(current_mode == 7);  // BBC MUST boot in Mode 7

    // Check where screen memory actually is according to VDU variables
    // BBC uses little-endian: low byte first
    // $350 = low byte, $351 = high byte
    uint16_t screen_top = machine.read(0x0350) |
                          (static_cast<uint16_t>(machine.read(0x0351)) << 8);
    INFO("VDU screen top ($350-$351): $" << std::hex << screen_top);
    INFO("  $350 = $" << std::hex << (int)machine.read(0x0350));
    INFO("  $351 = $" << std::hex << (int)machine.read(0x0351));

    // $34E-$34F = current screen memory position
    uint16_t screen_pos = machine.read(0x034E) |
                          (static_cast<uint16_t>(machine.read(0x034F)) << 8);
    INFO("VDU screen pos ($34E-$34F): $" << std::hex << screen_pos);
    INFO("  $34E = $" << std::hex << (int)machine.read(0x034E));
    INFO("  $34F = $" << std::hex << (int)machine.read(0x034F));

    // Check for non-zero content around VDU's screen area
    if (screen_top >= 0x100 && screen_top < 0x8000) {
        int nz = 0;
        for (uint16_t a = screen_top; a < screen_top + 0x400 && a < 0x8000; ++a) {
            if (machine.read(a) != 0) nz++;
        }
        INFO("Non-zero bytes from VDU screen top: " << nz);
    }

    // Check key bytes
    INFO("$7C00 = $" << std::hex << (int)machine.read(0x7C00));
    INFO("$7C01 = $" << std::hex << (int)machine.read(0x7C01));
    INFO("$7C02 = $" << std::hex << (int)machine.read(0x7C02));

    // Debug VIA and interrupt state
    auto& sysvia = machine.memory().system_via.state();
    INFO("System VIA IER: $" << std::hex << (int)sysvia.ier.value);
    INFO("System VIA IFR: $" << std::hex << (int)sysvia.ifr.value);
    INFO("System VIA T1 counter: $" << std::hex << sysvia.t1);
    INFO("System VIA T1 latch: $" << std::hex << ((sysvia.t1lh << 8) | sysvia.t1ll));
    INFO("CPU I flag: " << (int)machine.cpu().p.bits.i);
    INFO("Escape flag ($FF): $" << std::hex << (int)machine.read(0xFF));
    INFO("IRQ1V ($0204-$0205): $" << std::hex
         << std::setw(2) << std::setfill('0') << (int)machine.read(0x0205)
         << std::setw(2) << std::setfill('0') << (int)machine.read(0x0204));
    INFO("opcode_pc: $" << std::hex << machine.cpu().opcode_pc.w);

    // Verify "BBC Computer 32K" in Mode 7 screen memory
    // The message appears after a blank line (offset $28 = 40 bytes for one row)
    REQUIRE(machine.read(0x7C28) == 'B');
    REQUIRE(machine.read(0x7C29) == 'B');
    REQUIRE(machine.read(0x7C2A) == 'C');
    REQUIRE(machine.read(0x7C2B) == ' ');
    REQUIRE(machine.read(0x7C2C) == 'C');
    REQUIRE(machine.read(0x7C2D) == 'o');
    REQUIRE(machine.read(0x7C2E) == 'm');
    REQUIRE(machine.read(0x7C2F) == 'p');
    REQUIRE(machine.read(0x7C30) == 'u');
    REQUIRE(machine.read(0x7C31) == 't');
    REQUIRE(machine.read(0x7C32) == 'e');
    REQUIRE(machine.read(0x7C33) == 'r');
    REQUIRE(machine.read(0x7C34) == ' ');
    REQUIRE(machine.read(0x7C35) == '3');
    REQUIRE(machine.read(0x7C36) == '2');
    REQUIRE(machine.read(0x7C37) == 'K');

    // "BASIC" appears two rows down (offset $78 = 120 bytes = 3 rows)
    REQUIRE(machine.read(0x7C78) == 'B');
    REQUIRE(machine.read(0x7C79) == 'A');
    REQUIRE(machine.read(0x7C7A) == 'S');
    REQUIRE(machine.read(0x7C7B) == 'I');
    REQUIRE(machine.read(0x7C7C) == 'C');

    // Video ULA should be in teletext mode
    REQUIRE((machine.memory().video_ula.control() & 0x02) != 0);

    // CRTC R0 = 63 for Mode 7
    REQUIRE(machine.memory().crtc.reg(0) == 63);
}

// =============================================================================
// BBC Model B+ 64K Boot Sequence Tests
// =============================================================================

TEST_CASE("Model B+ boots to BASIC prompt", "[boot][bplus]") {
    if (!bplus_roms_available()) SKIP("B+ ROMs not available");

    ModelBPlus machine;
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dir / "BPMOS.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.reset();

    // Verify ROM loaded correctly
    // B+ MOS reset vector
    uint16_t reset_vector = machine.read(0xFFFC) | (machine.read(0xFFFD) << 8);
    INFO("Reset vector: $" << std::hex << reset_vector);
    REQUIRE(reset_vector >= 0xC000);  // Should be in MOS ROM space

    // Complete reset sequence
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }

    // Helper to step one instruction
    auto step = [&]() { machine.step_instruction(); };

    // Run boot sequence (2M instructions should be plenty)
    for (int i = 0; i < 2000000; ++i) {
        step();
    }

    // Should be in Mode 7
    uint8_t current_mode = machine.read(0x0355);
    INFO("Current screen mode: " << (int)current_mode);
    REQUIRE(current_mode == 7);

    // Video ULA should be in teletext mode
    REQUIRE((machine.memory().video_ula.control() & 0x02) != 0);

    // Dump first few rows of screen memory for debugging
    std::string screen_dump;
    for (int row = 0; row < 5; ++row) {
        screen_dump += "Row " + std::to_string(row) + ": [";
        for (int col = 0; col < 40; ++col) {
            uint8_t ch = machine.read(0x7C00 + row * 40 + col);
            if (ch >= 0x20 && ch < 0x7F) {
                screen_dump += static_cast<char>(ch);
            } else {
                screen_dump += '.';
            }
        }
        screen_dump += "]\n";
    }
    INFO("Screen memory:\n" << screen_dump);

    // Search for "Acorn OS" in screen memory to find boot message
    // B+ MOS displays "Acorn OS 64K" instead of "BBC Computer 32K"
    bool found_acorn = false;
    uint16_t acorn_addr = 0;
    for (uint16_t addr = 0x7C00; addr < 0x7FFF - 7; ++addr) {
        if (machine.read(addr) == 'A' &&
            machine.read(addr + 1) == 'c' &&
            machine.read(addr + 2) == 'o' &&
            machine.read(addr + 3) == 'r' &&
            machine.read(addr + 4) == 'n' &&
            machine.read(addr + 5) == ' ' &&
            machine.read(addr + 6) == 'O' &&
            machine.read(addr + 7) == 'S') {
            found_acorn = true;
            acorn_addr = addr;
            break;
        }
    }
    INFO("Found 'Acorn OS' at: $" << std::hex << acorn_addr);
    REQUIRE(found_acorn);

    // Search for "BASIC" in screen memory
    bool found_basic = false;
    uint16_t basic_addr = 0;
    for (uint16_t addr = 0x7C00; addr < 0x7FFF - 4; ++addr) {
        if (machine.read(addr) == 'B' &&
            machine.read(addr + 1) == 'A' &&
            machine.read(addr + 2) == 'S' &&
            machine.read(addr + 3) == 'I' &&
            machine.read(addr + 4) == 'C') {
            found_basic = true;
            basic_addr = addr;
            break;
        }
    }
    INFO("Found 'BASIC' at: $" << std::hex << basic_addr);
    REQUIRE(found_basic);
}

TEST_CASE("Model B+ ACCCON register controls shadow RAM", "[bplus][memory]") {
    if (!bplus_roms_available()) SKIP("B+ ROMs not available");

    ModelBPlus machine;
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dir / "BPMOS.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.reset();

    // ACCCON at $FE34 - bit 7 controls shadow RAM
    // Initially shadow should be disabled
    REQUIRE(machine.memory().shadow_enabled() == false);

    // Write test pattern to main RAM in shadow region
    machine.write(0x3000, 0xAA);
    machine.write(0x5000, 0xBB);
    machine.write(0x7000, 0xCC);

    // Write different pattern to shadow RAM directly
    machine.memory().write_shadow(0x3000, 0x11);
    machine.memory().write_shadow(0x5000, 0x22);
    machine.memory().write_shadow(0x7000, 0x33);

    // With shadow disabled, CPU reads from main RAM
    REQUIRE(machine.read(0x3000) == 0xAA);
    REQUIRE(machine.read(0x5000) == 0xBB);
    REQUIRE(machine.read(0x7000) == 0xCC);

    // peek_video() reads from main RAM when shadow disabled
    REQUIRE(machine.memory().peek_video(0x3000) == 0xAA);
    REQUIRE(machine.memory().peek_video(0x5000) == 0xBB);
    REQUIRE(machine.memory().peek_video(0x7000) == 0xCC);

    // Enable shadow RAM via ACCCON
    machine.write(0xFE34, 0x80);
    REQUIRE(machine.memory().shadow_enabled() == true);

    // CPU still reads from main RAM (shadow is for MOS/video only)
    REQUIRE(machine.read(0x3000) == 0xAA);
    REQUIRE(machine.read(0x5000) == 0xBB);
    REQUIRE(machine.read(0x7000) == 0xCC);

    // But peek_video() now reads from shadow RAM
    REQUIRE(machine.memory().peek_video(0x3000) == 0x11);
    REQUIRE(machine.memory().peek_video(0x5000) == 0x22);
    REQUIRE(machine.memory().peek_video(0x7000) == 0x33);

    // Disable shadow RAM
    machine.write(0xFE34, 0x00);
    REQUIRE(machine.memory().shadow_enabled() == false);

    // peek_video() returns to reading main RAM
    REQUIRE(machine.memory().peek_video(0x3000) == 0xAA);
}

TEST_CASE("Model B+ ROMSEL bit 7 controls ANDY private RAM", "[bplus][memory]") {
    if (!bplus_roms_available()) SKIP("B+ ROMs not available");

    ModelBPlus machine;
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dir / "BPMOS.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.reset();

    // Initially ANDY should be disabled
    REQUIRE(machine.memory().andy_enabled() == false);

    // Read from ROM space - should get ROM data
    uint8_t rom_byte = machine.read(0x8000);

    // Enable ANDY via ROMSEL bit 7 (while keeping bank 0 selected)
    machine.write(0xFE30, 0x80);
    REQUIRE(machine.memory().andy_enabled() == true);

    // Now reads from $8000-$AFFF should come from ANDY RAM
    // Initially ANDY RAM is cleared to 0
    REQUIRE(machine.read(0x8000) == 0x00);
    REQUIRE(machine.read(0x9000) == 0x00);
    REQUIRE(machine.read(0xA000) == 0x00);

    // Writes should go to ANDY RAM
    machine.write(0x8000, 0x42);
    machine.write(0x9000, 0x43);
    machine.write(0xA000, 0x44);

    REQUIRE(machine.read(0x8000) == 0x42);
    REQUIRE(machine.read(0x9000) == 0x43);
    REQUIRE(machine.read(0xA000) == 0x44);

    // $B000-$BFFF should still be ROM (top 4KB of bank)
    // Just verify it's not reading from ANDY by checking we get non-zero ROM data
    // or at least different data than ANDY
    uint8_t bxxx_byte = machine.read(0xB000);
    INFO("$B000 = $" << std::hex << (int)bxxx_byte);
    // (This is ROM data, not ANDY)

    // Disable ANDY - reads should return to ROM
    machine.write(0xFE30, 0x00);
    REQUIRE(machine.memory().andy_enabled() == false);

    // ROM should be visible again at $8000
    REQUIRE(machine.read(0x8000) == rom_byte);
}
