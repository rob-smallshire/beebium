# BBC Micro Model B, uPD7002 analogue port: trials on real hardware

Part of the analogue port work (issue #98). These trials settle the points on which the emulators we have studied disagree and the NEC uPD7002 datasheet is silent or unverified. The results feed the specification of Beebium's ADC model; the model is built from the datasheet and these measurements, never from the behaviour of a particular MOS.

Any Model B with MOS 1.20 will do. A pair of Acorn analogue joysticks (or any BBC analogue joystick) is needed for trials 1 (in part), 2 (in part), 5 and 7, and useful nowhere else. Nothing here needs a disc or any wiring.

Type each program in as listed, `RUN` it, and send back the screen output, by photograph or by hand. Where a program asks for a control byte, it means the value written to &FEC0: the low two bits are the channel (0 to 3), bit 3 set means 12-bit mode, bit 2 is a "flag" bit the chip echoes in its status. The useful values are `08` (channel 0, 12-bit), `00` (channel 0, 8-bit), `09` (channel 1, 12-bit) and `0E` (what the operating system writes at reset).

Please note the machine: issue number of the board if known, the start-up banner, and roughly how long it has been switched on.

Every program begins with `*FX16,0` and `?&24C=0`. Together these stop the operating system from starting any further conversions of its own once the one in flight, if any, has finished, so the programs then have the chip to themselves. `*FX16,4` restores normal joystick operation at the end of a session.

**The machine-code loops in trials 1, 3 and 7 cannot be stopped with Escape.** If one of them hangs, press Break, then type the programs' first two lines again as commands before re-running.

## Trial 1. Conversion time against input

Measures the mean time of 100 conversions. The datasheet says the time depends on the input voltage, and Stardot measurements found about 9.1 ms with the stick hard right, 10.1 ms at centre and 11.1 ms hard left in 12-bit mode, and trial 7 explains why that is only part of the story. Emulators all use exactly 10 ms. Nobody has measured 8-bit mode. We want both modes, with nothing plugged in, and with a stick at centre, hard left and hard right if one is available.

```
   10 *FX16,0
   15 ?&24C=0
   20 DIM C% 100
   30 FOR I%=0 TO 2 STEP 2
   40 P%=C%
   50 [OPT I%
   60 LDY #100
   70 .outer
   80 LDA ctl:STA &FEC0
   90 .rise
  100 BIT &FEC0:BPL rise
  110 .wait
  120 BIT &FEC0:BMI wait
  130 DEY:BNE outer
  140 RTS
  150 .ctl EQUB 0
  160 ]
  170 NEXT
  180 REPEAT
  190 INPUT "Control byte (hex) "A$
  200 ?ctl=EVAL("&"+A$)
  210 ?&FE4E=&10
  220 T%=TIME:CALL C%
  230 ?&FE4E=&90
  240 PRINT "Mean of 100 conversions: ";(TIME-T%)/10;" ms"
  250 PRINT "Last result: high ";~?&FEC1;" low ";~?&FEC2
  260 PRINT "Status after reading it: ";~?&FEC0
  270 UNTIL FALSE
```

Run with `08` and `00` for each input condition: nothing plugged in; stick at centre; hard left; hard right; and, if you have time, `09` with the stick hard up and hard down for channel 1. Note the millisecond figure and the result bytes each time. Escape ends it at the input prompt.

## Trial 2. What the registers read, before and after completion

Shows the status byte during a conversion and after it, the low byte and whether its bottom four bits are clear, what the fourth address (&FEC3) returns, whether reading the high byte clears the "conversion complete" bit, and whether the registers repeat every 4 bytes across &FEC0 to &FEDF.

```
   10 *FX16,0
   15 ?&24C=0
   20 REPEAT
   30 INPUT "Control byte (hex) "A$
   40 ?&FEC0=EVAL("&"+A$)
   50 S1%=?&FEC0
   60 REPEAT UNTIL (?&FEC0 AND &80)=0
   70 S2%=?&FEC0
   80 L%=?&FEC2:S3%=?&FEC0
   90 R3%=?&FEC3
  100 H%=?&FEC1:S4%=?&FEC0
  110 PRINT "status just after write ";~S1%
  120 PRINT "status when complete    ";~S2%
  130 PRINT "low byte ";~L%;" status after low read ";~S3%
  140 PRINT "fourth address reads ";~R3%
  150 PRINT "high byte ";~H%;" status after high read ";~S4%
  160 PRINT "mirrors ";~?&FEC4;" ";~?&FEC8;" ";~?&FED0;" ";~?&FEDC;" low ";~?&FEC6
  170 UNTIL FALSE
```

Run it with `08`, then `0E`. Then, with a stick plugged in, hold it **hard left** and enter `08`; keep the result; then move it **hard right** and enter `08` again straight away. The "status just after write" of that second run is the one we want: &30 in bits 4 and 5 means the chip shows the previous result's top bits while the new conversion runs; 00 means it clears them.

## Trial 3. The end-of-conversion line into the System VIA

The chip's end-of-conversion pin goes to the System VIA's CB1. This checks which edge the VIA sees at completion and whether reading the high byte produces the opposite edge, with interrupts off so the operating system cannot clear the flag first. The flag is bit 4 of the VIA interrupt flag register, which this program reports as 10 (set) or 0 (clear).

```
   10 *FX16,0
   15 ?&24C=0
   20 DIM C% 200
   30 FOR I%=0 TO 2 STEP 2
   40 P%=C%
   50 [OPT I%
   60 SEI
   70 LDA #&10:STA &FE4D
   80 LDA ctl:STA &FEC0
   85 LDX #40:.d1 DEX:BNE d1
   90 LDA &FE4D:AND #&10:STA r
   95 LDA #&10:STA &FE4D
  100 .wait
  110 BIT &FEC0:BMI wait
  120 LDA &FE4D:AND #&10:STA r+1
  130 LDA #&10:STA &FE4D
  140 LDA &FEC1
  150 LDA &FE4D:AND #&10:STA r+2
  160 LDA &FEC0:STA r+3
  170 LDA #&10:STA &FE4D
  180 LDA ctl:STA &FEC0
  185 LDX #40:.d2 DEX:BNE d2
  190 LDA &FE4D:AND #&10:STA r+4
  195 LDA #&10:STA &FE4D
  200 .wait2
  210 BIT &FEC0:BMI wait2
  220 LDA &FE4D:AND #&10:STA r+5
  230 LDA #&10:STA &FE4D
  240 CLI:RTS
  250 .ctl EQUB 0
  260 .r EQUS "......"
  270 ]
  280 NEXT
  290 ?ctl=&08
  300 FOR E%=0 TO 1
  310 IF E%=0 THEN ?&FE4C=?&FE4C AND &EF:PRINT "CB1 set to negative edge" ELSE ?&FE4C=(?&FE4C AND &EF) OR &10:PRINT "CB1 set to positive edge"
  320 CALL C%
  330 PRINT "flag after write       ";~r?0
  340 PRINT "flag at completion     ";~r?1
  350 PRINT "flag after high read   ";~r?2
  360 PRINT "status after high read ";~r?3
  370 PRINT "flag after new write   ";~r?4
  380 PRINT "flag at 2nd completion ";~r?5
  390 NEXT
  400 ?&FE4C=?&FE4C AND &EF
```

Reading. With the negative edge selected we expect: after write 0, at completion 10, after high read 0, after new write 0, at second completion 10, and the status after the high read with bit 7 set. With the positive edge selected there are two possible outcomes, and that is the point of the trial:

- **flag after high read = 10, flag after new write = 0**: reading the high byte releases the end-of-conversion line, as the datasheet says and only one emulator implements;
- **flag after high read = 0, flag after new write = 10**: only the next write releases it, as the other emulators assume.

"Flag after write" on the positive-edge run will probably read 10, because the line was still low from the previous run; that is expected.

## Trial 4. What an empty port reads through the operating system

Nothing plugged in. Press Break, type `*FX16,4`, wait a couple of seconds, then type:

```
PRINT ~ADVAL(1),~ADVAL(2),~ADVAL(3),~ADVAL(4)
PRINT ~ADVAL(0),~(?&FE40 AND &30)
```

Repeat the first line three or four times a few seconds apart. Stardot measurements found an unplugged port reads a machine-dependent value (about &C800 on one board, about &9600 on another) that wanders by tens; emulators return 0, &FFF0 or the centre. The second line reads the two fire-button lines; we expect 30 with nothing pressed.

## Trial 5. With a joystick

Plug in a stick (or the pair) and type the same two lines with the stick at centre, hard left, hard right, hard up and hard down, and with each fire button held. Note which ADVAL channel moves with which axis, whether left or right gives the low value, and which bit of the second figure drops when each fire button is pressed.

## Trial 6, optional, if a meter is to hand

Measure the voltage on the analogue port between pin 11 or 14 (VREF) and pin 5 or 8 (analogue ground) with the machine on. The User Guide gives two calibration figures that do not agree, and this single reading settles the scale.

## Trial 7. Conversion time across the whole range: the sweep

Hardware measurements by others show that in 12-bit mode the conversion time is a sawtooth against the result: about seven teeth across the range, each rising about 2 µs per step of the 12-bit result and then dropping back by about 1 ms. In 8-bit mode the time is a single straight line. This trial records about two thousand conversions while you move the stick slowly from one side to the other, then plots time against result, so we can see the teeth on another chip, where they fall, and how far one tooth overlaps the next. It needs a joystick.

```
   10 *FX16,0
   20 ?&24C=0
   30 MODE 4
   40 N%=2000
   50 DIM D% N%*4,C% 200,Z%(255)
   60 ?&FE6E=&20:?&FE6B=?&FE6B AND &DF
   70 FOR I%=0 TO 2 STEP 2
   80 P%=C%
   90 [OPT I%
  100 SEI
  110 LDA #D% AND &FF:STA &70:LDA #D% DIV 256:STA &71
  120 LDA #N% AND &FF:STA &72:LDA #N% DIV 256:STA &73
  130 LDA #0:STA &74:LDA #&58:STA &75
  140 .conv LDA #&FF:STA &FE68:LDX ctl
  150 STA &FE69:STX &FEC0
  160 .rise BIT &FEC0:BPL rise
  170 .wait BIT &FEC0:BMI wait
  180 .rd LDA &FE69:STA &76:LDA &FE68:LDX &FE69:CPX &76:BNE rd
  190 LDY #0:STA (&70),Y:INY:TXA:STA (&70),Y
  200 INY:LDA &FEC2:STA (&70),Y:INY:LDA &FEC1:STA (&70),Y
  210 CLC:LDA &70:ADC #4:STA &70:BCC nc:INC &71
  220 .nc LDA &72:AND #&3F:BNE np
  230 LDA #&FF:LDY #0:STA (&74),Y
  240 CLC:LDA &74:ADC #8:STA &74:BCC np:INC &75
  250 .np LDA &72:BNE dl:DEC &73
  260 .dl DEC &72:LDA &72:ORA &73:BNE conv
  270 CLI:RTS
  280 .ctl EQUB 0
  290 ]
  300 NEXT
  310 REPEAT
  320 CLS:INPUT "Control byte, 08 or 00 "A$
  330 ?ctl=EVAL("&"+A$)
  340 PRINT '"Hold the stick hard right, press SPACE,"'"then move it slowly and steadily to"'"hard left. 08 takes about 20 seconds,"'"00 about 8. The bar at the top grows"'"while it records."
  350 REPEAT UNTIL GET=32
  360 CLS:CALL C%
  370 L%=99999:H%=0:U%=9999:W%=-1
  380 FOR K%=0 TO 255:Z%(K%)=99999:NEXT
  390 FOR K%=0 TO N%-1
  400 A%=D%+4*K%:T%=&FFFF-(?A%+256*A%?1):V%=(A%?2+256*A%?3) DIV 64
  410 IF T%<L% THEN L%=T%
  420 IF T%>H% THEN H%=T%
  430 IF V%<U% THEN U%=V%:TU%=T%
  440 IF V%>W% THEN W%=V%:TW%=T%
  450 IF T%<Z%(V% DIV 4) THEN Z%(V% DIV 4)=T%
  460 NEXT
  470 CLS:PRINT "time ";L%;" to ";H%;" us"
  480 PRINT "result ";U%;" (";TU%;" us) to ";W%;" (";TW%;" us)"
  490 FOR K%=0 TO N%-1
  500 A%=D%+4*K%:T%=&FFFF-(?A%+256*A%?1):V%=(A%?2+256*A%?3) DIV 64
  510 PLOT 69,V%*5 DIV 4,96+(T%-L%)*800 DIV (H%-L%+1)
  520 NEXT
  530 G%=GET:CLS
  540 PRINT "Drops of more than 300 us:"
  550 Q%=0
  560 FOR K%=0 TO 255
  570 IF Z%(K%)<99999 AND Q%-Z%(K%)>300 THEN PRINT "at result ";K%*4;": ";Q%;" to ";Z%(K%);" us"
  580 IF Z%(K%)<99999 THEN Q%=Z%(K%)
  590 NEXT
  600 PRINT '"To keep the data: *SAVE SWEEP ";~D%;" +";~N%*4
  610 PRINT "Any key for another run":G%=GET
  620 UNTIL FALSE
```

Run it once with `08` and once with `00`. While it records, the machine does nothing else: the keyboard is not read, so Escape does nothing, and the clock stops. The bar at the top of the screen reaches about three quarters of the way across when it has finished. Move the stick slowly and evenly. It does not matter if you go a little past either end, or move back and forth, because the plot is by result, not by order. If the stick is still moving when the bar stops, the last part of the range is simply missing; run it again more slowly.

Each run ends with three screens. Please photograph each one:

1. **The plot**: result (left to right, 0 to 1023 in the 10-bit units the operating system's ADVAL uses, divided by 64) against time (bottom to top). The two lines of text above it give the shortest and longest times, and the time at the lowest and highest result. Press a key to go on.
2. **The drops**: the points where the time falls back by more than 300 µs from one result to the next, with the times before and after. We expect about six with `08` and none with `00`.
3. **The save command**: if a disc or tape is to hand, type it exactly as shown before pressing a key, and send us the file. That gives us every point rather than a photograph.

Stop with Break when you have finished, then type `*FX16,4`.

Times include about 10 µs of loop delay, the same for every point. They are measured with the User VIA's second timer at one count per microsecond, read in a way that avoids the ±256 µs error a straight low-then-high read can give.

Optional, if there is time: a third `08` run moving the stick very slowly across the middle only (about a quarter of the travel either side of centre). The plot then shows how wide the band is where two neighbouring teeth both occur.

## How we will read the results

| Observation | What it decides |
|---|---|
| Trial 1 times differ between hard left and hard right, and between 8 and 12-bit | We model conversion time as a function of input and mode, not a constant. Expected 12-bit: about 9.1 right, 10.1 centre, 11.1 left; 8-bit unknown, datasheet says 2.4 to 5 ms |
| Trial 2 low byte has its bottom digit 0 | Low nibble reads 0, as the datasheet says |
| Trial 2 fourth address equals the low byte | &FEC3 mirrors the low byte, as the datasheet says |
| Trial 2 status after high read has bit 7 set, after low read clear | Reading the high byte, and only the high byte, clears conversion complete |
| Trial 2 with `0E`, status just after write has bit 2 set | The chip echoes the flag bit the manual calls "not used" |
| Trial 2 left-then-right run, status just after write shows 30 in bits 4 and 5 | Status holds the old top bits during a conversion |
| Trial 2 mirrors all equal the status after high read, and &FEC6 equals the low byte | Registers repeat every 4 bytes across the 32 |
| Trial 3 positive-edge run: which of "after high read" and "after new write" shows 10 | Whether the high-byte read or the next write releases the end-of-conversion line |
| Trial 4 values | What a bare machine's ADVAL returns, and whether it wanders |
| Trial 6 voltage | The full-scale reference, so our joystick values map to the right numbers |
| Trial 7 `08` plot: teeth, each rising about 2 µs per 12-bit step and dropping about 1 ms, with drops near 12-bit results 1020, 1452, 2052, 2628, 3204, 3796 (10-bit 255, 363, 513, 657, 801, 949) | Whether the sawtooth and its spacing are a property of the uPD7002 or of one chip. The positions are expected to move by tens of codes between chips |
| Trial 7 `08` plot: places where two teeth overlap (points on both lines at the same result) | The width of the band in which a conversion may take either tooth's time |
| Trial 7 `00` plot: one straight line, about 2.95 ms at the right to 5.0 ms at the left | The 8-bit time model; the start time and slope tell us the 8-bit run-up length |

Footnote: reading &FE40 in trial 4 also clears two VIA flags the operating system uses, so it may miss one end-of-conversion interrupt. Its own 10 ms poll catches up, so this is harmless.

## Results

To be filled in when the trials have been run.
