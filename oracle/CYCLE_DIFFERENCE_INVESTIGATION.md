# Investigation: Origin of Cycle Differences Between Beebium and jsbeeb

## Summary

This document records the findings from investigating why beebium and jsbeeb have cycle count differences during emulation.

## Key Findings

### 1. Reset Sequence Handling (7 cycles initial offset)

**jsbeeb** (`6502.js:418-423`):
```javascript
reset(hard) {
    this.pc = this.readmem(0xfffc) | (this.readmem(0xfffd) << 8);
    this.p.i = true;
    if (hard) { this.currentCycles = 0; }
}
```
- Instantaneous reset - reads vector directly, sets PC immediately
- No cycles counted for reset sequence

**beebium** (`Machine.hpp:145-153`, `DebuggerService.hpp:316-337`):
- Full 7-cycle 6502 reset sequence implemented in CPU
- After `reset()`, the gRPC service calls `machine_.run(7)` to complete the reset sequence
- This puts both emulators at the same PC ($D9CD) but with different cycle counts

**Impact**: After reset, beebium shows 7 cycles, jsbeeb shows 0. The gRPC Reset handler compensates for this.

### 2. 1MHz Bus Stretching (1+ cycle differences)

At step 7 from reset, the instruction `LDA $FE4E` (System VIA IER read) shows:
- jsbeeb: 5 cycles
- beebium: 6 cycles

Accesses to the SHEILA range ($FE00-$FEFF) trigger 1MHz bus stretching. The exact timing depends on the phase relationship between the 2MHz and 1MHz clocks.

**Impact**: Each slow bus access can differ by 1 cycle. Over many accesses, this accumulates.

### 3. VSync Width Interpretation with R3=0

**jsbeeb** (`video.js:91`):
```javascript
this.vpulseWidth = (val & 0xf0) >>> 4;  // Raw value: 0 stays 0
```

**beebium** (`Crtc6845.hpp:260-263`):
```cpp
uint8_t vsync_width() const {
    uint8_t w = (registers_[R3_SYNC_WIDTH] >> 4) & 0x0F;
    return (w == 0) ? 16 : w;  // Per 6845 datasheet: 0 means 16 scanlines
}
```

At reset, all CRTC registers are 0. However, **both emulators effectively produce 16-scanline vsync**:

- **jsbeeb**: `vpulseWidth=0` but uses equality check (`vpulseCounter === vpulseWidth`). Counter wraps from 15→0 after 16 increments before matching.
- **beebium**: Explicitly maps 0→16 per Hitachi 6845 datasheet, uses `>=` check.

**Result**: Both should produce 16-scanline vsync duration with R3=0. The R3 handling is functionally equivalent.

### 4. Degenerate CRTC Behavior at Reset

With all CRTC registers at 0:
- R0 (horizontal total) = 0 → line length = 1 character
- R4 (vertical total) = 0 → frame height = 1 row
- R7 (vsync position) = 0 → vsync triggers at row 0
- R9 (max scanline) = 0 → row height = 1 scanline

Both emulators have undefined behavior in this state. jsbeeb and beebium implement different interpretations, leading to cycle differences during the initialization period.

## Test Observations

### Cycle Difference Pattern

Running convergence tests showed:
- After 100 instructions: beebium ahead by 2 cycles
- Fluctuates between small positive and negative values
- Eventually diverges significantly (2M+ cycles) around 600K instructions

This indicates the differences are NOT a simple constant offset but accumulate based on execution patterns.

### First VSync Detection

Test checking for CA1 interrupt (vsync end):
- jsbeeb: First CA1 interrupt at cycle 19 (instruction 6)
- beebium: No CA1 interrupt detected in 25000 instructions

This major difference is due to the vsync width interpretation (#3 above).

### Video ULA Clock Rate Diagnostic Results

Running `oracle/tests/video-ula-clock.test.ts` with varying clock configurations:

| Configuration | First CA1 Instruction | First CA1 Cycle | Clock Mode |
|--------------|----------------------|-----------------|------------|
| jsbeeb (default) | 7 | 19 | 2MHz (halfClock=false) |
| jsbeeb (aligned) | 81 | 254 | 1MHz (halfClock=true) |
| beebium (default) | Not detected | - | 1MHz (control=0) |

**Key observations**:

1. **jsbeeb's halfClock variable is separate from ulactrl**: Despite ulactrl being 0 at reset, jsbeeb runs at 2MHz because `halfClock = false` is set independently in the constructor.

2. **Aligning jsbeeb to 1MHz delays CA1 significantly**: From 19 cycles to 254 cycles, demonstrating the impact of clock rate on vsync timing.

3. **Beebium still doesn't detect CA1**: Even with clock rates aligned, beebium doesn't trigger CA1 within 25000 instructions. Since R3=0 handling is functionally equivalent (both produce 16-scanline vsync), the root cause requires further investigation - likely in the CRTC tick rate or VIA edge detection timing.

### 5. Video ULA Clock Rate at Reset (2x timing difference)

**Critical Finding**: The CRTC clock rate at reset differs between emulators, causing a 2x timing difference for all CRTC operations during initialization.

| Emulator | Initial State | Bit 4 | Clock Rate | Source |
|----------|--------------|-------|------------|--------|
| **jsbeeb** | `halfClock = false` | N/A | **2MHz** | `video.js:194` constructor |
| **beebium** | `control_ = 0` | 0 | **1MHz** | `VideoUla.hpp:210` reset() |
| **b2** | `control = {}` | 0 | **1MHz** | `VideoULA.h:57` zero-init |
| **B-Em** | `ula_ctrl = 0x00` | 0 | **1MHz** | `video.c:126` zero-init |
| **BeebEm-mac** | `0x9c` | 1 | **2MHz** | `Video.cpp:74` explicit init |

**Pattern**: b2 and B-Em match beebium (1MHz). BeebEm-mac and jsbeeb match each other (2MHz).

BeebEm-mac's `0x9c` value is notable: it's a Mode 0/3-like default (80 chars/line, 2MHz clock, master cursor) rather than undefined state.

**Impact on VSync Detection**:

With degenerate CRTC state (all registers = 0) and R0=0 (1-character scanlines):
- **jsbeeb at 2MHz**: VSync events complete in ~17 CRTC ticks ≈ ~17 CPU cycles
- **beebium at 1MHz**: VSync events complete in ~17 CRTC ticks ≈ ~34 CPU cycles

This explains the cycle 19 CA1 interrupt detection in jsbeeb versus no detection in beebium within the same instruction count - the 2MHz clock allows vsync to complete faster.

**Notes on jsbeeb's halfClock variable**:
- `halfClock = false` means 2MHz (full speed)
- `halfClock = true` means 1MHz (half speed)
- **Important**: `halfClock` is set independently from `ulactrl` in the Video constructor
- At reset, `ulactrl = 0` but `halfClock = false`, so jsbeeb runs at 2MHz despite the register being 0
- When the MOS writes to the Video ULA control register, both `ulactrl` and `halfClock` are updated in sync
- jsbeeb's reset() method doesn't reset video state - it only sets cpu/via references
- Diagnostic test confirmed: setting `halfClock = true` after reset changes first CA1 from cycle 19 to cycle 254

### 6. CA1 Detection Issue - VSync Never Falling (INVESTIGATED)

**Root Cause Identified**: With degenerate CRTC state (R0=R4=R7=0), vsync ends and immediately restarts on the same tick:

1. Vsync starts at row 0 (R7=0)
2. After 16 scanlines (vsync_width with R3=0), vsync ends: `vsync_counter_ = -1`
3. Same tick: vsync start condition is met (row_ = 0 = R7, vsync_counter_ < 0, etc.)
4. Vsync restarts: `vsync_counter_ = 0`
5. Result: vsync signal is ALWAYS HIGH, CA1 never sees a falling edge

**Partial Fix Applied** (Option A from b2):
- `had_vsync_this_row_` is NO LONGER cleared in `end_of_frame()` - only in `end_of_row()`
- This matches b2's implementation (`crtc.cpp:317`)
- However, with degenerate R4=0, `end_of_row()` is called every tick, so `had_vsync_this_row_` is still cleared before vsync ends

**Full Fix Not Yet Applied** (Option B from b2):
- B2 also uses a `!vsync_ending` check to prevent same-tick restart: `if (vsync_starting && !vsync_ending)`
- **Problem**: Adding this check breaks interlace mode (Mode 7) video rendering
- Investigation found that with the `!vsync_ending` check, all frames have 0 brightness in Mode 7
- The exact cause is under investigation - possibly related to how vsync transitions affect frame timing in interlace mode

**Current Status**:
- Option A applied: `had_vsync_this_row_` not cleared in `end_of_frame()` (matches b2)
- Option B not applied: `!vsync_ending` check omitted (breaks interlace video)
- CA1 detection in degenerate case: STILL BROKEN (vsync always high)
- Normal operation: WORKS (cursor blink, boot, convergence all pass)

**Analysis of Why Option B Breaks Interlace Video**:

The FrameRenderer uses vsync transitions for frame timing:
```cpp
// Handle VSYNC rising edge - finalize frame and swap buffers
if (vsync && !in_vsync_) {
    // ...
    finish_frame();
}
in_vsync_ = vsync;
```

In interlace Mode 7, frames only swap when `latched_odd_field_` is true. The odd_field flag toggles when v_display_ goes false at R6 hit.

When the `!vsync_ending` check is added, something about the vsync timing changes in a way that causes:
1. All frames to have 0 brightness (completely black)
2. Boot still works (text appears in screen memory)
3. Frame version still increments (frame swapping happens)

This suggests the `display` flag in PixelBatch is never set to true when `!vsync_ending` is present, even though the machine boots correctly. The exact mechanism requires tracing through the Mode 7 vsync/display timing.

**Potential Alternative Approaches**:

1. **Delay same-tick restart by one cycle**: Instead of checking `!vsync_ending` in the start condition, set a flag when vsync ends and clear it on the next tick. This would prevent same-tick restart while allowing normal operation.

2. **Use a different degenerate case detection**: Only apply the `!vsync_ending` restriction when R4=0 or similar degenerate conditions, not in normal operation.

3. **Fix at the VIA level**: Instead of preventing same-tick restart in CRTC, have the VIA require at least one cycle of vsync=0 before detecting a falling edge on CA1.

4. **Accept degenerate case divergence**: Document that beebium differs from b2/jsbeeb in degenerate CRTC states, since these states don't occur during normal BBC Micro operation (MOS initializes CRTC registers early in boot).

## Root Causes

1. **Slow bus timing**: Different implementations of 1MHz bus cycle stretching
2. **Video ULA clock rate**: jsbeeb starts at 2MHz (halfClock=false), beebium starts at 1MHz (control=0)
3. **Degenerate CRTC state**: Undefined behavior when all registers are 0
4. **CA1 detection issue**: In degenerate state, vsync never falls because it restarts on same tick it ends (partially addressed)

## Recommendations

### For Oracle Testing

1. Accept small cycle differences (1-10 cycles per instruction) as normal
2. Focus on CPU state and memory comparison rather than cycle-exact matching
3. Test after MOS has initialized the CRTC (e.g., at BASIC entry point)

### For Beebium

1. **Document**: The vsync_width=0→16 mapping follows the 6845 datasheet
2. **Consider**: Matching jsbeeb's raw interpretation for compatibility testing
3. **Test**: Cycle accuracy against real hardware, not just jsbeeb

### For Video ULA Clock Rate Alignment

Several options exist for aligning the clock rate between emulators:

**Option A: Align jsbeeb to 1MHz in oracle wrapper**

Modify `JsbeebOracle.reset()` to set jsbeeb to 1MHz mode after reset:
```typescript
this.processor.video.halfClock = true;
this.processor.video.pixelsPerChar = 16;
```
Pros: Matches beebium/b2/B-Em, minimal change to oracle only
Cons: Diagnostic tests show this still doesn't align CA1 timing - beebium doesn't detect CA1 at all, suggesting the vsync width (R3=0) handling is a larger factor

**Note**: jsbeeb's `halfClock` is set independently from `ulactrl` in the constructor. The ULA control register value (ulactrl) may be 0, but `halfClock = false` gives 2MHz behavior. Setting `halfClock = true` after reset successfully puts jsbeeb in 1MHz mode.

**Option B: Change beebium to 2MHz at reset**

Modify `VideoUla::reset()` to initialize at 2MHz:
```cpp
control_ = CTRL_FAST_CLOCK;  // 0x10 - Start in 2MHz mode
```
Pros: Matches jsbeeb/BeebEm-mac which have better game compatibility
Cons: Diverges from b2/B-Em, changes beebium core behavior

**Option C: Change beebium to 0x9c (BeebEm-mac style)**

Initialize Video ULA to a Mode 0-like default:
```cpp
control_ = 0x9c;  // Mode 0-like: 80 chars, 2MHz, master cursor
```
Pros: Most complete initialization
Cons: Arbitrary default, may mask bugs in MOS initialization

**Option D: Test both configurations**

Run diagnostic tests with both configurations to measure actual impact before committing to a direction.

## Files Referenced

- `/Users/rjs/Code/jsbeeb/src/6502.js` - CPU reset
- `/Users/rjs/Code/jsbeeb/src/video.js` - Video timing, vsync, halfClock initialization (line 194)
- `/Users/rjs/Code/jsbeeb/src/via.js` - VIA interrupt handling
- `/Users/rjs/Code/beebium/src/core/include/beebium/Machine.hpp` - Machine step/reset
- `/Users/rjs/Code/beebium/src/core/include/beebium/devices/Crtc6845.hpp` - CRTC implementation
- `/Users/rjs/Code/beebium/src/core/include/beebium/devices/VideoUla.hpp` - Video ULA implementation, control_ initialization (line 210)
- `/Users/rjs/Code/beebium/src/core/src/Via6522.cpp` - VIA edge detection
- `/Users/rjs/Code/beebium/src/core/include/beebium/VideoBinding.hpp` - Video/VIA connection
- `/Users/rjs/Code/b2/src/beeb/include/beeb/VideoULA.h` - b2 Video ULA (reference)
- `/Users/rjs/Code/b2/src/beeb/src/crtc.cpp` - b2 CRTC (reference for vsync handling)
- `/Users/rjs/Code/b-em/src/video.c` - B-Em video (reference)
- `/Users/rjs/Code/beebem-mac/Src/Video.cpp` - BeebEm-mac video (reference)

## Current Implementation Status (2025-01-19)

### Changes Applied (B2-Style Latching Mechanism)

The fundamental issue was that beebium's direct row/raster checks allowed rapid cycling in degenerate CRTC state (all registers = 0). B2 uses a latching mechanism that requires column to advance past 0 before end_of_frame can be triggered.

**Key Implementation Details**:

1. **B2-style latching state variables** (`Crtc6845.hpp`):
   - `end_of_main_latched_`: Set at column==1 when row==R4 && raster matches R9
   - `end_of_vadj_latched_`: Set when vadj_counter == R5
   - `end_of_frame_latched_`: For interlace dummy raster handling
   - `check_vadj_`: One-tick delay for vadj check (b2-style timing)

2. **Latching check at column 1** (required for degenerate state):
   - With R0=0, column never reaches 1, so end_of_main_latched_ is never set
   - This prevents rapid row cycling in degenerate state
   - Normal Mode 7 (R0=63) works correctly because column reaches 1

3. **Interlace raster comparison fix**:
   - The end_of_main latch check uses halved raster comparison in interlace mode
   - Without this, even fields (raster=1,3,5...19) never match R9 (e.g., 18)
   - Fix: `(raster_ >> 1) == ((R9 & 0x1F) >> 1)` instead of exact equality

4. **Fixed CRTC interlace test** (`test_crtc6845.cpp`):
   - The test calculation for tick count was wrong (missing row 25 completion)
   - Changed from 6 rows (3968 ticks) to 7 rows (4607 ticks)

### CA1 Detection Results

After implementing the B2-style latching:

| Emulator | First CA1 Instruction | First CA1 Cycle | Notes |
|----------|----------------------|-----------------|-------|
| jsbeeb (2MHz) | 7 | 19 | Default clock rate at reset |
| jsbeeb (1MHz) | 81 | 254 | Aligned to beebium clock rate |
| beebium (1MHz) | 11 | 35 | **NOW WORKING** |

**Key Observation**: Beebium now detects CA1 at cycle 35, compared to jsbeeb's cycle 254 in 1MHz mode. The difference (219 cycles) is due to different vsync timing implementations, but the fundamental bug is **FIXED** - vsync now ends properly.

### Test Results

- **CRTC tests**: 33/33 pass (all CRTC unit tests working)
- **VideoUla tests**: 11/11 pass
- **FrameRenderer unit tests**: 3/3 pass
- **CA1 detection**: Working (vsync ends correctly in degenerate state)

### Remaining Issue: Framebuffer Rendering

The cursor blink integration tests fail with 0 bright pixels in the framebuffer:
- "Mode 7 cursor blinks": 0 min/max bright pixels
- "Simple cursor blink detection": All 150 frames show 0 brightness

**Diagnosis**:
- Video batches ARE being generated correctly (test #432 shows 1677 batches with bright pixels)
- FrameRenderer unit tests pass (interlace field handling works in isolation)
- Frame version counter IS incrementing (finish_frame() is being called)
- The issue is in the integration between CRTC/VideoUla and FrameRenderer

**Possible Causes**:
1. Display flag timing issue causing early returns in FrameRenderer
2. Interlace field detection mismatch between CRTC and FrameRenderer
3. Y-position calculation issues in interlace mode

This is a **separate issue** from the CA1 detection bug and requires further investigation.

### Attempted Fixes History (2025-01-18)

This section documents approaches tried before the successful B2-style latching implementation.

#### Early Attempts (all failed)

1. **B2-Style Vsync Counter with == comparison**: Failed because beebium's tick structure differs.
2. **FrameRenderer X-Position Field Detection**: Did not resolve the 0-brightness issue.
3. **!vsync_ending check alone**: Caused 0-brightness in Mode 7 without the latching mechanism.
4. **VIA-Level Fix**: Rejected as it would deviate from documented 6522 hardware behavior.

#### Successful Approach: B2-Style Latching

The key insight came from studying b2's `crtc.cpp` more carefully:

**B2's end_of_frame latching mechanism**:
- `end_of_main_latched` is set at column==1 (not column==0) when row==R4 and raster==R9
- With R0=0 (degenerate), column never reaches 1, so frames never end
- This naturally prevents rapid cycling without needing the `!vsync_ending` check

**Beebium's implementation**:
- Added same latching state variables as b2
- Check for end_of_main at column==1 with proper interlace raster comparison
- Vadj timing matches b2's one-tick delay pattern

This approach fixes CA1 detection while maintaining correct interlace video timing.

### Key Forensic Finding: B2's Latching Mechanism

The breakthrough came from understanding how B2 handles degenerate state:

```cpp
// B2's crtc.cpp at column == 1 check (line 198):
if (m_st.column == 1) {
    if (m_st.row == m_registers.values[4]) {
        if (m_st.raster == m_registers.values[9]) {
            m_st.end_of_main_latched = true;  // Only set when column == 1!
        }
    }
}
```

**With degenerate R0=0:**
- In B2: column is ALWAYS 0 (never reaches 1), so `end_of_main_latched` is never set
- The frame never ends, row keeps incrementing: 0 → 1 → 2 → ... → 16+
- By the time vsync ends (16 scanlines later), row has advanced past R7(0)
- So vsync_starting is FALSE, and vsync stays off naturally

**Implication:**
B2's degenerate behavior is a side effect of its latching mechanism.
The `!vsync_ending` check in B2 is for OTHER edge cases, not the degenerate R0=0 case.
Implementing the same latching in beebium achieves correct behavior without needing `!vsync_ending`.

## Display Jumping Regression (2025-01-19)

### Problem Statement

After implementing the B2-style latching mechanism for CA1 detection:
- **MODE 7**: Display steady, cursor works (CORRECT)
- **MODE 0-6**: Display jumps up/down by one scanline (REGRESSION)
- **`*TV 0,1`** (interlace sync ON): Display becomes steady
- **`*TV 0,0`** (interlace sync OFF): Display jumps again
- **Master branch**: No issues, all modes display correctly

### Investigation Summary

#### Candidates Tested (All Failed to Fix)

| Test | Change | Result |
|------|--------|--------|
| A | Remove `h_display_ = false` from `end_of_frame()` | Still jumps |
| B | Remove `do_even_frame_logic_ = false` reset from `end_of_frame()` | Still jumps |
| C | Remove `end_of_row()` call before `end_of_frame()` | Still jumps |
| D | Remove `!first_scanline_` from R6 hit condition | Still jumps |

#### Other Attempted Fixes

| Fix | Result |
|-----|--------|
| Only toggle `odd_field_` in interlace mode | Still jumps |
| Master + only CA1 fix (remove `had_vsync_this_row_` from `end_of_frame()`) | Display stable BUT CA1 not detected |
| Master + `!vsync_ending` check | CA1 detected at cycle 35, BUT no image displayed at all |

### Key Insight: The Fixes Are Mutually Exclusive

The latching mechanism is **required** for CA1 detection in degenerate state because:
1. With R0=0, column never reaches 1, so frames never end
2. Row increments past R7 before vsync ends
3. Vsync stays off naturally after ending

But the latching mechanism causes display jumping in non-interlace modes.

Without the latching mechanism:
- Master's direct `row_ == R4+1` comparison works for normal display
- But in degenerate state, frames end every scanline, vsync restarts immediately
- CA1 never sees a falling edge

### The `!vsync_ending` Check Issue

Adding `!vsync_ending` to the vsync start condition:
- Successfully prevents same-tick restart
- CA1 is detected at cycle 35
- BUT: No image is rendered (not even in the active pixel area)

This is too aggressive - it breaks normal vsync operation entirely, not just degenerate state.

### B2 vs Beebium Field Handling Difference

**B2's Approach** (`TVOutput.cpp:132-142`):
- B2 **derives** odd/even field from horizontal position at vsync start
- In non-interlace mode, vsync always starts at column ~R0 (end of line)
- So every field is classified as "odd" - no alternation

**Beebium's Approach**:
- Beebium **explicitly tracks** `odd_field_` and toggles it at R6 hit
- This alternation happens even in non-interlace modes
- May cause display position to alternate between frames

However, making `odd_field_` toggle conditional on interlace mode did NOT fix the jumping.

### Current Status

The `option-b` branch has:
- CA1 detection working (cycle 35)
- MODE 7 display stable
- MODE 0-6 display jumping (regression from master)

### Possible Next Steps

1. **Deeper investigation of frame boundary timing**: The latching mechanism shifts frame end detection to column 1. This timing shift may affect display sync in non-interlace modes.

2. **Frontend investigation**: The jumping might be in how the macOS client processes `odd_field` or vsync timing, not in CRTC logic itself.

3. **Compare tick-by-tick vsync timing**: Detailed comparison between master and option-b to find exactly where the timing diverges.

4. **Hybrid approach**: Use latching only during degenerate state (R0=0 or R4=0), revert to direct comparison for normal operation.

---

## Verdict: Divergence, Not Defect (2026-07-17)

Reviewed on picking the harness up again. The question this document left open
-- *which* emulator is right about the 6845 clock rate at reset -- is answered
by the hardware documentation, and the answer is **neither, because the hardware
does not define it**.

### The ULA control register has no reset state

Advanced User Guide §19.1 (`docs/manuals_text/Advanced_User_Guide/19_19._The_video_ULA.md`):
SHEILA &20 is **WRITE ONLY**, bit 4 is `6845 CLOCK RATE SELECT`, and the chapter
specifies **no reset or power-on value**. Neither does the Master AUG's video
hardware chapter, nor any other transcribed manual. A write-only register with
no documented reset value is *indeterminate at power-on on real hardware*.

That reframes the survey in §5. This:

| 1MHz at reset | 2MHz at reset |
|---|---|
| Beebium, b2, B-Em | jsbeeb, BeebEm-mac |

is not a 2-2 split over a fact. It is four emulators each picking a defined
value for something the hardware leaves undefined, and splitting evenly because
there is nothing to be right about. **jsbeeb is not ground truth here**, and no
amount of differential testing against it will settle the question -- which is
why the "For Oracle Testing" recommendation (do not assert on cycle counts) is
the correct one and should stand.

MOS writes the register (via the `*FX154` path) before any of this is
observable, so the divergence is unreachable by real software.

### Nothing here is a Beebium bug

Assessed against real hardware rather than against jsbeeb:

| Finding | Verdict |
|---|---|
| §1 Reset sequence (7 cycles) | **Beebium more correct.** jsbeeb sets PC in 0 cycles; the real 6502 takes 7. |
| §2 1MHz bus stretching | Divergence; Beebium models the stretch, jsbeeb does not. |
| §3 VSync width, R3=0 | Not a defect -- already concluded functionally equivalent. Beebium follows the Hitachi datasheet (0 -> 16). |
| §4 Degenerate CRTC at reset | Undefined; all registers zero cannot occur once MOS has initialised. |
| §5 ULA clock rate at reset | **Undefined on hardware** (above). No defect on either side. |
| §6 CA1 never falling | Only reachable in the degenerate state of §4, so not reachable by real software. |

So no GitHub issue is warranted. What §6 *does* expose is a **latent modelling
gap** worth recording: Beebium cannot presently satisfy CA1-in-degenerate-state
and correct MODE 0-6 display at the same time ("The Fixes Are Mutually
Exclusive", above). Both behaviours are individually reachable; the fact that
they are mutually exclusive says the frame-end/vsync model diverges from the
real 6845 somewhere that a faithful model would not. It is invisible to real
software and carries no user-facing symptom, which is why "accept degenerate
case divergence" remains the right call -- but if the CRTC is ever reworked,
this is the constraint to test against, and satisfying both would be evidence
the rework is more faithful than what it replaced.

### What the harness did find

For balance -- the tool earned its keep on defects that were real and are fixed:

- **`bus_stretch_cancel` data loss** on Tube R1/R3/R4 host writes (`47955495`),
  exposed here and by a C++ test, since fixed and verified: *"All deltas zero.
  Every byte written was read"* (`docs/discussion/chuckie-egg-2023-tube-hang.md`).
- A genuine defect in **b2**, found while cross-checking the CE2023 hang and
  sent upstream as tom-seddon/b2 PR #569.

The pattern is worth noting: the harness was most valuable where the two
emulators disagreed about something the hardware *does* define (data delivered
through a Tube register), and least valuable where they disagreed about
something it does not (state of a write-only register at power-on).

### Settled: the ULA has no reset input (2026-07-17)

The Model B circuit diagram closes the question the section above left as
"undefined by the documentation". It is stronger than that: **the Video ULA has
no reset pin at all.** All 28 of IC6's pins are accounted for --

| Pin(s) | Function |
|---|---|
| 1, 15, 16 | GND, V2, V1 |
| 2 | A0 |
| 3 | /CS (`/VIDPROC`) |
| 4, 5, 6, 7 | 1MHz, 2MHz, 4MHz, 8MHz out |
| 8 | 16MHz in |
| 9-14 | RGB in, RGB out |
| 17-24 | D0-D7 |
| 25-28 | CURSOR, DISEN, INVERT, CRTC CLK |

-- pins 4-8 from the service manual's §9.3 ("8MHz, 4MHz, 2MHz, and 1MHz are
available from pins 7, 6, 5, and 4 respectively... 16MHz is available at pin 8"),
the rest read off the schematic. Power, one address line, chip select, the data
bus, the clock divider, RGB, four control signals. **There is nowhere for a
reset to arrive**, which is why the service manual routes notRS only to the CPU
(pin 40, IC42) and the 1MHz bus and TUBE connectors.

So the register's power-on value is whatever its latches settle to, and no
manual states one because there is nothing to state. This retires the survey in
§5 as a way of deciding the question: the two-two split is five emulators
inventing a defined state for a device that has none. The 1MHz "majority"
(Beebium, b2, B-Em) is three C++ zero-initialisers agreeing with each other, not
three claims about hardware.

It also means **&FE20 survives BREAK on real hardware** -- BREAK asserts the same
notRS as power-on (§5.3: one 555, both events; the separate RC network on the
system VIA exists purely so software can tell them apart), and notRS never
reaches the ULA. Beebium's `VideoUla::reset()` zeroes `control_`, so we clear a
register the hardware would hold. Unobservable, since the MOS rewrites it during
reset, but a real divergence and now a documented one -- see the comment in
`VideoUla::reset()`.

What remains open is only whether the power-up state is *stable* per chip or
genuinely random, which the pinout cannot answer. Tracked as issue #58.

### CPLD/FPGA reimplementations corroborate 1MHz (2026-07-17)

Ken Lowe's Video ULA CPLD project (Stardot t=33291) links three reimplementations.
The two in VHDL both power up at 1MHz:

- **mikestir/fpga-bbc** `vidproc.vhd`: `if nRESET = '0' then ... r0_crtc_2mhz <= '0'`
- **jonsole/bbc-vidproc-cpld** `ula.vhd`: `signal r0_crtc_2mhz : std_logic := '0'`

jonsole's is the telling one. It is a drop-in replacement whose entity ports are
exactly the real pinout -- CLK_16M in, 8/4/2/1M out, CLK_CRTC, nCS, A, D[7:0],
nINVERT/DISEN/CURSOR -- and it has **no reset port**, independently confirming
that the real ULA has none. It sets the power-on state through the CPLD's
configuration-time initializer.

This upgrades the 1MHz side of the survey from "three emulators zero-initialised
a struct" to "the people who reimplemented the part pin-for-pin also chose 1MHz",
which is real weight, and Beebium already matches it. It does **not** close the
open question: a CPLD always powers up deterministically, so `:= '0'` is an
informed choice, not a measurement of the original uncommitted-logic-array's
power-on behaviour. Whether the real Ferranti die is deterministic at all remains
for #58. (gertk64/BBC_Video_ULA is the third reimplementation but is PALASM split
across several PALs with no clean power-on statement.)
