\ serial_break.asm
\
\ A tiny BBC Micro program that drives or detects a serial BREAK at the
\ hardware level (MC6850 ACIA + Serial ULA / SERPROC), assembled by beebasm
\ and run from an auto-booting DFS disc image. It is the BBC-application end
\ of an application-to-application break test against a host peer (pySerial /
\ a recording serial device).
\
\ Assemble with exactly one mode selected:
\   beebasm -i serial_break.asm -D MODE=0 -do tx.ssd -boot PROG   ; transmit
\   beebasm -i serial_break.asm -D MODE=1 -do rx.ssd -boot PROG   ; detect
\
\ The program owns the serial hardware by writing the chip registers directly
\ rather than going through the MOS RS423 driver. That is deliberate: it keeps
\ the test deterministic (no interrupt-driven MOS buffering racing the poll)
\ and shows the bare-metal sequence. The MOS-level equivalents are noted in
\ comments.

ACIA_CONTROL = &FE08    \ write: 6850 control register; read: status register
ACIA_DATA    = &FE09    \ write: TDR; read: RDR
SERPROC      = &FE10    \ Serial ULA latch (SERPROC), write-only

\ Zero-page locations the C++ test harness peeks (the &70-&8F block is reserved
\ for the user, so the MOS will not touch them).
READY    = &71          \ set once RS423 is selected (carrier up) and we are polling
SENTINEL = &70          \ set when the program has finished its job

\ 6850 control-register values (8 data bits, no parity, 1 stop, divide-by-64,
\ which is what MOS 1.20 uses on RS423; the Serial ULA supplies 64 clocks a bit):
\   bits 0-1 = 10  divide-by-64 clock
\   bits 2-4 = 101 word select 8N1
\   bits 5-6 = transmitter control, bit 7 = receive interrupt enable
CTRL_RESET = &03        \ master reset the ACIA (counter-divide = 11)
CTRL_8N1   = &16        \ 8N1, /64, RX IRQ off, /RTS low, no break (TX control = 00)
CTRL_BREAK = &76        \ as CTRL_8N1 but TX control = 11 -> hold TX in BREAK
                        \ (the MOS-level equivalent is OSBYTE &9C / *FX156,96,159)

\ Serial ULA latch: bit 6 selects RS423 (which drives the ACIA /DCD low, i.e.
\ carrier present); the low bits pick the transmit/receive baud (index 0 = 19200).
ULA_RS423_19200 = &40

ORG &1900

.prog
    \ --- Take direct control of the serial hardware ---
    LDA #CTRL_RESET : STA ACIA_CONTROL    \ master reset
    LDA #CTRL_8N1   : STA ACIA_CONTROL    \ 8N1, RX interrupt OFF
    LDA #ULA_RS423_19200 : STA SERPROC    \ select RS423: carrier present, 19200

IF MODE = 0
    \ ============================================================
    \ Transmit a BREAK: hold the TX line in the space state, then
    \ release it. The Serial ULA forwards the on/off edges to the
    \ attached device out of band.
    \ ============================================================
    LDA #CTRL_BREAK : STA ACIA_CONTROL    \ assert BREAK (TX control = 11)

    \ Hold the break long enough for the ULA to sample it many times.
    LDX #&30
.hold_outer
    LDY #&00
.hold_inner
    DEY : BNE hold_inner
    DEX : BNE hold_outer

    LDA #CTRL_8N1 : STA ACIA_CONTROL      \ clear BREAK (TX control back to 00)
ELSE
    \ ============================================================
    \ Detect a received BREAK. The 6850 has no dedicated break bit:
    \ a received break arrives as a Framing Error (status bit 4) with
    \ data 0x00. With the receive interrupt disabled above, the MOS
    \ RS423 handler never consumes it, so this foreground poll catches
    \ the latched Framing Error + Receive-Data-Register-Full.
    \ ============================================================
    LDA #&AA : STA READY                  \ tell the harness RS423 is live; send now
.poll
    LDA ACIA_CONTROL                      \ read the status register
    AND #&11                              \ RDRF (bit 0) + FE (bit 4)
    CMP #&11
    BNE poll
ENDIF

    LDA #&FF : STA SENTINEL               \ signal completion to the host harness
.spin
    JMP spin

SAVE "PROG", prog, P%
