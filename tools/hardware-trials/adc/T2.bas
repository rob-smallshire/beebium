10 REM Trial 2. What the registers read, before and after completion
20 ON ERROR PROCerr
30 *FX16,0
40 ?&24C=0
50 MODE 7
60 DIM rbuf% 1500
70 PROCstart(2)
80 PRINT "Trial 2. What the registers read"'
90 PROCw("joystick",STR$J%)
100 pm%=0
110 PROCr("run1",&08)
120 PROCr("run2",&0E)
130 IF J%=1 THEN PROCpair
140 PROCsave("R.T2")
150 PROCend
200 DEF PROCpair
210 PROCkey("Hold the stick hard LEFT and keep it there. Press SPACE.")
220 PROCr("run3_left",&08)
230 PROCkey("Now move it hard RIGHT, then press SPACE straight away.")
240 PROCr("run4_right",&08)
250 ENDPROC
300 DEF PROCr(n$,k%)
310 IF F%=1 THEN PROCsynth ELSE PROCreal
320 PRINT n$;", control ";FNh(k%)
330 PRINT " status just after write ";FNh(S1%)
340 PRINT " status when complete    ";FNh(S2%)
350 PRINT " low ";FNh(L%);", status after low read ";FNh(S3%)
360 PRINT " fourth address reads ";FNh(R3%)
370 PRINT " high ";FNh(H%);", status after high read ";FNh(S4%)
380 PRINT " mirrors ";FNh(M1%);" ";FNh(M2%);" ";FNh(M3%);" ";FNh(M4%);" low ";FNh(M5%)
390 PROCw(n$+"_control","&"+FNh(k%))
400 PROCw(n$+"_status_after_write","&"+FNh(S1%))
410 PROCw(n$+"_status_when_complete","&"+FNh(S2%))
420 PROCw(n$+"_low","&"+FNh(L%))
430 PROCw(n$+"_status_after_low_read","&"+FNh(S3%))
440 PROCw(n$+"_fourth_address","&"+FNh(R3%))
450 PROCw(n$+"_high","&"+FNh(H%))
460 PROCw(n$+"_status_after_high_read","&"+FNh(S4%))
470 PROCw(n$+"_mirror_FEC4","&"+FNh(M1%))
480 PROCw(n$+"_mirror_FEC8","&"+FNh(M2%))
490 PROCw(n$+"_mirror_FED0","&"+FNh(M3%))
500 PROCw(n$+"_mirror_FEDC","&"+FNh(M4%))
510 PROCw(n$+"_mirror_FEC6_low","&"+FNh(M5%))
520 ENDPROC
600 REM The trial sheet's reads, in the same order.
610 DEF PROCreal
620 ?&FEC0=k%
630 S1%=?&FEC0
640 REPEAT UNTIL (?&FEC0 AND &80)=0
650 S2%=?&FEC0
660 L%=?&FEC2:S3%=?&FEC0
670 R3%=?&FEC3
680 H%=?&FEC1:S4%=?&FEC0
690 M1%=?&FEC4:M2%=?&FEC8:M3%=?&FED0:M4%=?&FEDC:M5%=?&FEC6
700 ENDPROC
800 REM Self-test only: what the datasheet predicts, made up.
810 DEF PROCsynth
820 IF n$="run3_left" THEN H%=&FF ELSE IF n$="run4_right" THEN H%=&00 ELSE H%=&CA
830 S1%=&80+k%+pm%
840 pm%=(H% DIV 64)*16
850 S2%=&40+k%+pm%
860 L%=&D0:S3%=S2%:R3%=L%
870 S4%=&C0+k%+pm%
880 M1%=S4%:M2%=S4%:M3%=S4%:M4%=S4%:M5%=L%
890 ENDPROC
