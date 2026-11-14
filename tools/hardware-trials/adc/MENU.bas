10 REM uPD7002 analogue port trials: menu
20 REM Resident variables: F% self-test, M% run-all, O% next trial,
30 REM J% joystick available, Y% set by a trial returning here.
40 ON ERROR PROCerr
50 MODE 7
60 IF Y%<>&4D454E55 THEN F%=0:M%=0:O%=1
70 Y%=0:?&8F=0:?&8E=0
80 DIM rbuf% 600:rptr%=rbuf%
90 IF NOT FNexists("M.INFO") THEN PROCnotes
100 J%=FNjoy
110 IF M%=1 THEN PROCnext
120 REPEAT
130 PROCshow
140 *FX15,1
150 ?&8C=?&8C+1:?&8F=3:K$=GET$:?&8F=0
160 IF K$>="a" AND K$<="z" THEN K$=CHR$(ASC K$-32)
170 IF K$>="A" AND K$<="G" THEN F%=0:M%=0:CHAIN "T"+STR$(ASC K$-64)
180 IF K$="R" THEN F%=0:M%=1:O%=1:PROCnext
190 IF K$="Z" THEN PROCselftest
200 IF K$="N" THEN PROCnotes:J%=FNjoy
210 UNTIL K$="Q"
220 *FX16,4
230 CLS:PRINT "Joystick sampling restored (*FX16,4)."
240 PRINT "Press Shift+Break to return to the menu."
250 ?&8C=?&8C+1:?&8F=4
260 END
1000 DEF PROCshow
1010 LOCAL i%,s%,n$
1020 CLS
1030 PRINT "uPD7002 analogue port trials"
1040 PRINT "----------------------------"
1050 RESTORE 1200
1060 FOR i%=1 TO 7
1070 READ n$
1080 s%=FNstat(i%)
1090 PRINT " ";CHR$(64+i%);"  ";i%;". ";n$;TAB(30);
1100 IF s%=1 THEN PRINT "done" ELSE IF s%=2 THEN PRINT "self-test" ELSE PRINT
1110 NEXT
1120 PRINT
1130 PRINT " R  Run all remaining trials"
1140 PRINT " N  Enter the machine notes again"
1150 PRINT " Z  SELF-TEST with made-up data, for"
1160 PRINT "    emulators only; never on hardware"
1170 PRINT " Q  Quit"
1180 IF J%=0 THEN PRINT '"No joystick: trials 5 and 7 skip."
1190 PRINT '"Choose a letter."
1195 ENDPROC
1200 DATA Conversion time,Registers,End-of-conversion edge
1210 DATA Empty port via ADVAL,Joystick via ADVAL,VREF (meter)
1220 DATA Sweep: time against result
1300 DEF PROCnext
1310 LOCAL n%
1320 n%=0
1330 REPEAT
1340 IF O%<=7 THEN IF F%=1 OR FNstat(O%)<>1 THEN n%=O%
1350 O%=O%+1
1360 UNTIL n%>0 OR O%>7
1370 IF n%>0 THEN CHAIN "T"+STR$n%
1380 M%=0
1390 IF F%=1 THEN ?&8D=&5A:F%=0:PRINT "Self-test run complete." ELSE PRINT "Every trial has a result."
1400 PROCwait(100)
1410 ENDPROC
1500 DEF PROCselftest
1510 LOCAL i%,r%
1520 r%=0
1530 FOR i%=1 TO 7
1540 IF FNstat(i%)=1 THEN r%=1
1550 NEXT
1560 IF r%=1 THEN PRINT '"This disc holds real results. The"'"self-test would overwrite them, so it"'"will not run.":PROCkey("Press SPACE.") ELSE F%=1:M%=1:O%=1:PROCnext
1570 ENDPROC
1600 DEF PROCnotes
1610 LOCAL a$,b$,c$,d$,j$
1620 CLS
1630 PRINT "Machine notes (asked once; N to redo)."'
1640 a$=FNin("Board issue number, if known:",1)
1650 b$=FNin("Start-up banner, e.g. BBC Computer 32K:",2)
1660 c$=FNin("Minutes since switched on:",3)
1670 d$=FNin("Is a joystick plugged in or to hand, Y or N?",4)
1680 j$="0":IF LEFT$(d$,1)="Y" OR LEFT$(d$,1)="y" THEN j$="1"
1690 rptr%=rbuf%
1700 PROCw("board_issue",a$):PROCw("banner",b$)
1710 PROCw("minutes_on",c$):PROCw("joystick",j$)
1720 PROCsave("M.INFO")
1730 ENDPROC
1800 DEF FNexists(f$)
1810 LOCAL h%
1820 h%=OPENIN f$
1830 IF h%=0 THEN =FALSE
1840 CLOSE#h%
1850 =TRUE
1900 DEF FNline(h%)
1910 LOCAL s$,c%
1920 s$=""
1930 IF EOF#h% THEN =s$
1940 REPEAT
1950 c%=BGET#h%
1960 IF c%<>13 THEN s$=s$+CHR$c%
1970 UNTIL c%=13 OR EOF#h% OR LEN s$>200
1980 =s$
2000 DEF FNstat(n%)
2010 LOCAL f$,h%,s$
2020 f$="R.T"+STR$n%
2030 h%=OPENIN f$
2040 IF h%=0 THEN =0
2050 s$=FNline(h%)
2060 CLOSE#h%
2070 IF s$="selftest=1" THEN =2
2080 =1
2100 DEF FNjoy
2110 LOCAL h%,j%
2120 j%=0
2130 h%=OPENIN "M.INFO"
2140 IF h%=0 THEN =0
2150 REPEAT
2160 IF FNline(h%)="joystick=1" THEN j%=1
2170 UNTIL EOF#h%
2180 CLOSE#h%
2190 =j%
