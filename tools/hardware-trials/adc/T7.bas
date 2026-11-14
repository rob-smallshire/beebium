10 REM Trial 7. Conversion time across the whole range: the sweep
20 ON ERROR PROCerr
30 *FX16,0
40 ?&24C=0
50 MODE 4
60 REM Records go below the MODE 4 screen. Leave room for the rest.
70 N%=(HIMEM-TOP-3600) DIV 4
80 IF N%>2000 THEN N%=2000
90 DIM rbuf% 700
100 DIM D% N%*4,C% 200,Z%(255),b%(6)
110 PROCstart(7)
120 PROCw("joystick",STR$J%)
130 IF J%=0 THEN PRINT "No joystick: trial 7 skipped.":PROCsave("R.T7"):PROCend
140 IF F%=0 THEN ?&FE6E=&20:?&FE6B=?&FE6B AND &DF
150 FOR I%=0 TO 2 STEP 2
160 P%=C%
170 [OPT I%
180 SEI
190 LDA #D% AND &FF:STA &70:LDA #D% DIV 256:STA &71
200 LDA #N% AND &FF:STA &72:LDA #N% DIV 256:STA &73
210 LDA #0:STA &74:LDA #&58:STA &75
220 .conv LDA #&FF:STA &FE68:LDX ctl
230 STA &FE69:STX &FEC0
240 .rise BIT &FEC0:BPL rise
250 .wait BIT &FEC0:BMI wait
260 .rd LDA &FE69:STA &76:LDA &FE68:LDX &FE69:CPX &76:BNE rd
270 LDY #0:STA (&70),Y:INY:TXA:STA (&70),Y
280 INY:LDA &FEC2:STA (&70),Y:INY:LDA &FEC1:STA (&70),Y
290 CLC:LDA &70:ADC #4:STA &70:BCC nc:INC &71
300 .nc LDA &72:AND #&3F:BNE np
310 LDA #&FF:LDY #0:STA (&74),Y
320 CLC:LDA &74:ADC #8:STA &74:BCC np:INC &75
330 .np LDA &72:BNE dl:DEC &73
340 .dl DEC &72:LDA &72:ORA &73:BNE conv
350 CLI:RTS
360 .ctl EQUB 0
370 ]
380 NEXT
390 r$=""
400 PROCrun("A",&08,"Hold the stick hard RIGHT. Press SPACE, then at once move it slowly and steadily to hard LEFT, taking about 20 seconds. The bar at the top grows while it records.")
410 PROCrun("B",&00,"Hold the stick hard RIGHT. Press SPACE, then at once move it slowly and steadily to hard LEFT, taking about 8 seconds. The bar at the top grows while it records.")
420 CLS:PRINT "Optional: a very slow sweep across the middle only. Press SPACE to do it, or S to skip."
430 *FX15,1
440 ?&8C=?&8C+1:?&8F=1:G%=GET:?&8F=0
450 IF G%<>83 AND G%<>115 THEN PROCrun("C",&08,"Hold the stick a quarter of the way RIGHT of centre. Press SPACE, then move it very slowly to a quarter of the way LEFT of centre, taking about 20 seconds.")
460 PROCstart(7)
470 PROCw("joystick",STR$J%)
480 PROCw("runs",r$)
490 PROCw("records_per_run",STR$N%)
500 CLS:PROCsave("R.T7")
510 PROCend
600 DEF PROCrun(id$,k%,p$)
610 LOCAL K%,A%,T%,V%,L%,H%,U%,W%,TU%,TW%,Q%,n%
620 PROCstart(7)
630 PROCw("run",id$)
640 PROCw("control","&"+FNh(k%))
650 PROCw("records",STR$N%)
660 ?ctl=k%
670 CLS:PROCkey(p$)
680 CLS
690 IF F%=1 THEN PROCsynth ELSE CALL C%
700 L%=99999:H%=0:U%=9999:W%=-1
710 FOR K%=0 TO 255:Z%(K%)=99999:NEXT
720 FOR K%=0 TO N%-1
730 A%=D%+4*K%:T%=&FFFF-(?A%+256*A%?1):V%=(A%?2+256*A%?3) DIV 64
740 IF T%<L% THEN L%=T%
750 IF T%>H% THEN H%=T%
760 IF V%<U% THEN U%=V%:TU%=T%
770 IF V%>W% THEN W%=V%:TW%=T%
780 IF T%<Z%(V% DIV 4) THEN Z%(V% DIV 4)=T%
790 NEXT
800 CLS:PRINT "Run ";id$;": time ";L%;" to ";H%;" us"
810 PRINT "result ";U%;" (";TU%;" us) to ";W%;" (";TW%;" us)"
820 FOR K%=0 TO N%-1
830 A%=D%+4*K%:T%=&FFFF-(?A%+256*A%?1):V%=(A%?2+256*A%?3) DIV 64
840 PLOT 69,V%*5 DIV 4,96+(T%-L%)*800 DIV (H%-L%+1)
850 NEXT
860 PROCw("time_min_us",STR$L%):PROCw("time_max_us",STR$H%)
870 PROCw("result_min",STR$U%):PROCw("time_at_result_min_us",STR$TU%)
880 PROCw("result_max",STR$W%):PROCw("time_at_result_max_us",STR$TW%)
890 PRINT TAB(0,31);"Photograph the plot; any key goes on.";
900 PROCany("")
910 CLS
920 PRINT "Run ";id$;": drops of more than 300 us:"
930 Q%=0:n%=0
940 FOR K%=0 TO 255
950 IF Z%(K%)<99999 AND Q%-Z%(K%)>300 THEN n%=n%+1:PRINT "at result ";K%*4;": ";Q%;" to ";Z%(K%);" us":PROCw("drop"+STR$n%,STR$(K%*4)+","+STR$Q%+","+STR$Z%(K%))
960 IF Z%(K%)<99999 THEN Q%=Z%(K%)
970 NEXT
980 PROCw("drops",STR$n%)
990 PRINT "Saving the records and results..."
1000 OSCLI "SAVE D.T7"+id$+" "+STR$~D%+" "+STR$~(D%+N%*4)
1010 PROCsave("R.T7"+id$)
1020 r$=r$+id$
1030 ENDPROC
1100 REM Self-test only: a made-up sawtooth (08) or line (00).
1110 DEF PROCsynth
1120 LOCAL c%,t%,v%,w%,i%,n%
1130 PRINT "Self-test: making up ";N%;" records..."
1135 RESTORE 1400:FOR i%=1 TO 6:READ b%(i%):NEXT:b%(0)=-999
1140 FOR K%=0 TO N%-1
1150 IF id$="C" THEN c%=1024+K%*2048 DIV N% ELSE c%=K%*4096 DIV N%
1160 c%=c%+RND(5)-3
1170 IF c%<0 THEN c%=0
1180 IF c%>4095 THEN c%=4095
1190 IF k%=0 THEN t%=2950+c% DIV 2 ELSE PROCtooth:t%=9100+2*c%-1000*n%
1200 t%=t%+RND(9)-5
1210 A%=D%+4*K%:v%=&FFFF-t%:w%=c%*16
1220 ?A%=v% AND &FF:A%?1=v% DIV 256:A%?2=w% AND &FF:A%?3=w% DIV 256
1230 NEXT
1240 ENDPROC
1300 REM Teeth below c%; within 100 codes of a boundary, now and then
1310 REM the earlier tooth, as on the hardware.
1320 DEF PROCtooth
1330 n%=0
1340 REPEAT n%=n%+1:UNTIL n%=7 OR c%<b%(n% MOD 7)
1350 n%=n%-1
1360 IF c%-b%(n%)<100 AND n%>0 THEN IF RND(3)=1 THEN n%=n%-1
1370 ENDPROC
1400 DATA 1020,1452,2052,2628,3204,3796
