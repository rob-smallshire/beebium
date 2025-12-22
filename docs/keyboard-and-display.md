# BBC Micro Keyboard and Display Emulation

This document describes the keyboard matrix emulation and Mode 7 screen memory handling in Beebium, based on analysis of the MOS 1.20 ROM and reference emulators (BeebEm, B2).

## Keyboard Matrix

### Hardware Overview

The BBC Micro keyboard is organized as a 10×8 matrix with 73 keys and 8 keyboard links. The System VIA (6522 at $FE40-$FE4F) interfaces with the keyboard:

- **Port A (bits 0-6)**: Key number output (column in low nibble, row in bits 4-6)
- **Port A (bit 7)**: Key state input (directly from keyboard matrix)
- **Port B (bits 0-2)**: Addressable latch address
- **Port B (bit 3)**: Addressable latch data
- **Addressable latch bit 3**: KB_WRITE (active low, enables keyboard scanning)

### Key Number Encoding

The MOS encodes key positions as a 7-bit "key number":
```
Bits 0-3: Column (0-15, though only 0-9 used for keys)
Bits 4-6: Row (0-7)
```

For example:
- SHIFT is at row 0, column 0 → key number $00
- CTRL is at row 0, column 1 → key number $01
- 'A' is at row 4, column 1 → key number $41

### Bit 7 Convention

**Critical**: The software convention used by emulators (BeebEm, B2) is:

| Bit 7 Value | Meaning |
|-------------|---------|
| 0 | Key/link NOT pressed (open circuit) |
| 1 | Key/link IS pressed (closed circuit) |

This is the logical convention, not the physical hardware level. When emulating:
- Return `output & 0x7F` (bit 7 = 0) when no key is pressed
- Return `output | 0x80` (bit 7 = 1) when the key IS pressed

### Row 0 Special Handling

Row 0 contains special keys and links that don't generate interrupts:
- Column 0: SHIFT
- Column 1: CTRL
- Columns 2-9: Keyboard links (active only during startup)

The MOS scans SHIFT and CTRL separately when needed (e.g., in the `$CAE0` handleScrollingInPagedMode routine). During regular keyboard scanning, row 0 keys don't trigger the CA2 keyboard interrupt.

## Keyboard Links and Startup Mode

### Link Positions

Eight keyboard links at row 0 determine startup options:

| Link | Column | Purpose |
|------|--------|---------|
| Link 1 | Column 2 | *CAPS LOCK (active low) |
| Link 2 | Column 3 | *No boot (active low) |
| Link 3 | Column 4 | Disc timing |
| Link 4 | Column 5 | Shift-Break action bit 0 |
| Link 5 | Column 6 | Shift-Break action bit 1 |
| Link 6 | Column 7 | **Mode bit 2** |
| Link 7 | Column 8 | **Mode bit 1** |
| Link 8 | Column 9 | **Mode bit 0** |

### Mode Detection

During startup, the MOS reads columns 7, 8, 9 (row 0) to determine the startup display mode:

1. MOS writes key numbers $09, $08, $07 to Port A (columns 9, 8, 7)
2. MOS reads back bit 7 for each position
3. The three bits form a mode number (0-7)

**Key insight**: Mode 7 is selected when all links are **BROKEN** (open/unmade):

| All Links | Bit 7 Readings | Mode |
|-----------|----------------|------|
| BROKEN (open) | 0, 0, 0 | MOS XORs to get 7 |
| MADE (closed) | 1, 1, 1 | Mode 0 |

The MOS applies EOR (XOR) operations to invert the readings, so:
- All broken links (bit 7 = 0) → Mode 7 (default)
- All made links (bit 7 = 1) → Mode 0

### Emulation Implementation

For default startup (Mode 7, all links broken):
```cpp
uint8_t update_port_a(uint8_t output, uint8_t ddr) override {
    // Return key number with bit 7 = 0 (no key/link pressed)
    return output & 0x7F;
}
```

This returns bit 7 = 0 for all positions, indicating no keys pressed and all links broken, which results in Mode 7 being selected.

## Screen Memory

### Mode 7 (Teletext)

Mode 7 is a 40×25 character teletext display mode:

| Property | Value |
|----------|-------|
| Screen start | $7C00 |
| Screen end | $7FFF |
| Size | 1000 bytes (1024 allocated) |
| Characters per row | 40 |
| Rows | 25 |
| Bytes per character | 1 |

### Memory Layout

```
$7C00: Row 0, columns 0-39
$7C28: Row 1, columns 0-39
$7C50: Row 2, columns 0-39
...
$7FC8: Row 24, columns 0-39
```

Row offset = row_number × 40 (decimal) = row_number × $28 (hex)

### VDU Variables

Key MOS variables for screen handling:

| Address | Name | Description |
|---------|------|-------------|
| $0350-$0351 | vduScreenTopAddress | Start of screen memory |
| $034E-$034F | vduCurrentTextCell | Current cursor position (character offset) |
| $0355 | screenMode | Current display mode (0-7) |

### BASIC Startup Display

When BASIC starts, it outputs:
```
[blank line]
BBC Computer 32K
[blank line]
BASIC
```

In screen memory at $7C00:
- $7C00-$7C27: Spaces (blank first row)
- $7C28-$7C37: "BBC Computer 32K"
- $7C38-$7C4F: Spaces (rest of row 1)
- $7C50-$7C77: Spaces (blank row 2)
- $7C78-$7C7C: "BASIC"

### Character Output Path

When OSWRCH ($FFEE) is called:
1. JMP through WRCHV ($020E) → default handler at $E0A4
2. VDU handler processes character
3. For printable characters in Mode 7:
   - Calculate screen address from cursor position
   - Write character byte directly to screen memory
   - Increment cursor position
4. The actual write happens at $CFE6 in the MOS

## Video ULA

The Video ULA control register ($FE20) bit 1 indicates teletext mode:

```cpp
// Check if teletext mode is active
bool is_teletext = (video_ula.control() & 0x02) != 0;
```

In Mode 7, this bit is SET to enable teletext character rendering by the SAA5050 chip.

## References

- BeebEm source: `Src/SysVia.cpp` - keyboard matrix and link handling
- B2 source: `src/beeb/src/BBCMicro_Update.inl` - keyboard state management
- MOS 1.20 disassembly: https://tobylobster.github.io/mos/mos/
- BBC Micro Advanced User Guide - keyboard hardware description
