10 REM Trial 3. The end-of-conversion line into the System VIA
20 ON ERROR PROCerr
30 *FX16,0
40 ?&24C=0
50 MODE 7
60 DIM rbuf% 1500
70 PROCstart(3)
80 PRINT "Trial 3. End of conversion into CB1"'
90 PRINT "No joystick needed. Takes a moment."'
100 DIM C% 200
110 FOR I%=0 TO 2 STEP 2
120 P%=C%
130 [OPT I%
140 SEI
150 LDA #&10:STA &FE4D
160 LDA ctl:STA &FEC0
170 LDX #40:.d1 DEX:BNE d1
180 LDA &FE4D:AND #&10:STA r
190 LDA #&10:STA &FE4D
200 .wait
210 BIT &FEC0:BMI wait
220 LDA &FE4D:AND #&10:STA r+1
230 LDA #&10:STA &FE4D
240 LDA &FEC1
250 LDA &FE4D:AND #&10:STA r+2
260 LDA &FEC0:STA r+3
270 LDA #&10:STA &FE4D
280 LDA ctl:STA &FEC0
290 LDX #40:.d2 DEX:BNE d2
300 LDA &FE4D:AND #&10:STA r+4
310 LDA #&10:STA &FE4D
320 .wait2
330 BIT &FEC0:BMI wait2
340 LDA &FE4D:AND #&10:STA r+5
350 LDA #&10:STA &FE4D
360 CLI:RTS
370 .ctl EQUB 0
380 .r EQUS "......"
390 ]
400 NEXT
410 ?ctl=&08
420 FOR E%=0 TO 1
430 IF E%=0 THEN e$="neg" ELSE e$="pos"
440 IF F%=1 THEN PROCsynth ELSE PROCreal
450 PRINT "flag after write       ";~r?0
460 PRINT "flag at completion     ";~r?1
470 PRINT "flag after high read   ";~r?2
480 PRINT "status after high read ";~r?3
490 PRINT "flag after new write   ";~r?4
500 PRINT "flag at 2nd completion ";~r?5
510 PROCw(e$+"_flag_after_write","&"+FNh(r?0))
520 PROCw(e$+"_flag_at_completion","&"+FNh(r?1))
530 PROCw(e$+"_flag_after_high_read","&"+FNh(r?2))
540 PROCw(e$+"_status_after_high_read","&"+FNh(r?3))
550 PROCw(e$+"_flag_after_new_write","&"+FNh(r?4))
560 PROCw(e$+"_flag_at_2nd_completion","&"+FNh(r?5))
570 NEXT
580 IF F%=0 THEN ?&FE4C=?&FE4C AND &EF
590 PROCsave("R.T3")
600 PROCend
700 DEF PROCreal
710 IF E%=0 THEN ?&FE4C=?&FE4C AND &EF:PRINT "CB1 set to negative edge" ELSE ?&FE4C=(?&FE4C AND &EF) OR &10:PRINT "CB1 set to positive edge"
720 CALL C%
730 ENDPROC
800 REM Self-test only: the datasheet's outcome, made up.
810 DEF PROCsynth
820 IF E%=0 THEN PRINT "CB1 negative edge (self-test)" ELSE PRINT "CB1 positive edge (self-test)"
830 IF E%=0 THEN ?r=0:r?1=&10:r?2=0:r?3=&C8:r?4=0:r?5=&10
840 IF E%=1 THEN ?r=&10:r?1=0:r?2=&10:r?3=&C8:r?4=0:r?5=0
850 ENDPROC
