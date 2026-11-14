10 REM Trial 1. Conversion time against input
20 ON ERROR PROCerr
30 *FX16,0
40 ?&24C=0
50 MODE 7
60 DIM rbuf% 1500
70 PROCstart(1)
80 PRINT "Trial 1. Conversion time against input"'
90 DIM C% 100
100 FOR I%=0 TO 2 STEP 2
110 P%=C%
120 [OPT I%
130 LDY #100
140 .outer
150 LDA ctl:STA &FEC0
160 .rise
170 BIT &FEC0:BPL rise
180 .wait
190 BIT &FEC0:BMI wait
200 DEY:BNE outer
210 RTS
220 .ctl EQUB 0
230 ]
240 NEXT
250 PROCw("joystick",STR$J%)
260 PROCkey("Unplug the joystick. Press SPACE when ready.")
270 PROCm("unplugged",&08):PROCm("unplugged",&00)
280 IF J%=1 THEN PROCstick
290 PROCsave("R.T1")
300 PROCend
400 DEF PROCstick
410 PROCkey("Plug in the joystick and leave it centred. Press SPACE.")
420 PROCm("centre",&08):PROCm("centre",&00)
430 PROCkey("Hold the stick hard LEFT and keep it there. Press SPACE.")
440 PROCm("left",&08):PROCm("left",&00)
450 PROCkey("Hold the stick hard RIGHT and keep it there. Press SPACE.")
460 PROCm("right",&08):PROCm("right",&00)
470 PROCkey("Hold the stick hard UP and keep it there. Press SPACE.")
480 PROCm("up",&09)
490 PROCkey("Hold the stick hard DOWN and keep it there. Press SPACE.")
500 PROCm("down",&09)
510 ENDPROC
600 REM One measurement: the mean of 100 conversions, timed by TIME,
610 REM with the CB1 interrupt disabled so the MOS does not run on
620 REM each completion (as the trial sheet does).
630 DEF PROCm(c$,k%)
640 LOCAL ms,cs%,h%,l%,s%,T%,p$
650 ?ctl=k%
660 IF F%=1 THEN PROCsynth ELSE ?&FE4E=&10:T%=TIME:CALL C%:?&FE4E=&90:cs%=TIME-T%:h%=?&FEC1:l%=?&FEC2:s%=?&FEC0
665 ms=cs%/10
670 PRINT c$;" ";FNh(k%);": ";ms;" ms, high ";FNh(h%);" low ";FNh(l%);" status ";FNh(s%)
680 p$=c$+"_"+FNh(k%)
690 PROCw(p$+"_mean_ms",STR$ms)
695 PROCw(p$+"_total_cs_for_100",STR$cs%)
700 PROCw(p$+"_high","&"+FNh(h%))
710 PROCw(p$+"_low","&"+FNh(l%))
720 PROCw(p$+"_status_after_read","&"+FNh(s%))
730 ENDPROC
800 REM Self-test only: made-up values in place of the chip.
810 DEF PROCsynth
820 LOCAL n$,a,b
830 RESTORE 900
840 REPEAT
850 READ n$,a,b,h%,l%
860 UNTIL n$=c$
870 IF k%=0 THEN cs%=INT(b*10+0.5) ELSE cs%=INT(a*10+0.5)
880 IF k%=0 THEN l%=l% AND &C0
890 s%=&C0+k%+(h% DIV 64)*16
895 ENDPROC
900 DATA unplugged,10.45,3.90,202,208
910 DATA centre,10.07,3.96,127,32
920 DATA left,11.12,4.97,255,224
930 DATA right,9.08,2.95,0,48
940 DATA up,11.10,4.95,255,192
950 DATA down,9.10,2.96,0,64
