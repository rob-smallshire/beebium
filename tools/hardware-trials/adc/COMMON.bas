9000 REM Shared procedures, appended to every program by build_disc.py
9010 REM Results are built as key=value lines (CR-terminated) in rbuf%
9020 REM and written with *SAVE, so no file is open while timing code runs.
9030 REM Zero page &8E/&8F tell an automated harness what is awaited:
9040 REM ?&8F=1 SPACE or any key, 2 a typed line (?&8E = prompt id),
9050 REM 3 a menu choice, 4 finished; ?&8C counts the waits.
9055 REM All of this is harmless on real hardware.
9100 DEF PROCstart(n%)
9110 rptr%=rbuf%
9120 IF F%=1 THEN PROCw("selftest","1")
9130 PROCw("trial",STR$n%)
9140 ENDPROC
9200 DEF PROCw(k$,v$)
9210 $rptr%=k$+"="+v$
9220 rptr%=rptr%+LEN(k$)+LEN(v$)+2
9230 ENDPROC
9300 DEF PROCsave(f$)
9310 OSCLI "SAVE "+f$+" "+STR$~rbuf%+" "+STR$~rptr%
9320 ENDPROC
9400 DEF FNh(x%)=RIGHT$("0"+STR$~x%,2)
9410 DEF FNh4(x%)=RIGHT$("000"+STR$~x%,4)
9500 DEF PROCkey(p$)
9510 IF p$<>"" THEN PRINT p$
9520 *FX15,1
9530 ?&8C=?&8C+1:?&8F=1
9540 REPEAT UNTIL GET=32
9550 ?&8F=0
9560 ENDPROC
9600 DEF PROCany(p$)
9610 IF p$<>"" THEN PRINT p$
9620 *FX15,1
9630 ?&8C=?&8C+1:?&8F=1
9640 G%=GET
9650 ?&8F=0
9660 ENDPROC
9700 DEF FNin(p$,id%)
9710 LOCAL a$
9720 PRINT p$
9730 *FX15,1
9740 ?&8E=id%:?&8C=?&8C+1:?&8F=2
9750 INPUT LINE "> " a$
9760 ?&8F=0
9770 =a$
9800 DEF PROCwait(cs%)
9810 LOCAL t%
9820 t%=TIME
9830 REPEAT UNTIL TIME>=t%+cs%
9840 ENDPROC
9900 DEF PROCend
9910 PRINT '"Results saved. Back to the menu."
9920 Y%=&4D454E55
9930 CHAIN "MENU"
9950 DEF PROCerr
9960 ON ERROR OFF
9970 CLOSE#0
9980 PRINT:REPORT:PRINT " at line ";ERL
9985 M%=0
9990 PROCkey("Press SPACE for the menu.")
9995 Y%=&4D454E55:CHAIN "MENU"
