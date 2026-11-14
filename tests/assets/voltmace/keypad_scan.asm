\ keypad_scan.asm
\
\ The Voltmace Delta 14B/1 keypad scan in the shape of Voltmace's own KEYPAD
\ driver: DDRB = &F0, then for each of the six strobes &60, &50, &30 (PB7 = 0,
\ the rear handset) and &E0, &D0, &B0 (PB7 = 1, the side handset) write the
\ strobe to the user VIA's ORB and read IRB back immediately, with no
\ instruction between. The six reads are stored at &72-&77 for the C++ test
\ to peek, then the sentinel at &70 is set to &FF.
\
\   beebasm -i keypad_scan.asm -do keypad.ssd -boot PROG

USER_ORB  = &FE60
USER_DDRB = &FE62
SENTINEL  = &70
READS     = &72

ORG &2000
.start
    LDA #&F0
    STA USER_DDRB
    LDX #0
.loop
    LDA strobes,X
    STA USER_ORB
    LDA USER_ORB
    STA READS,X
    INX
    CPX #6
    BNE loop
    LDA #&FF
    STA SENTINEL
.spin
    JMP spin
.strobes
    EQUB &60, &50, &30, &E0, &D0, &B0
.end

SAVE "PROG", start, end
