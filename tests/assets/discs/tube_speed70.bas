10 REM Issue #70 effective-MHz probe for the second processor
20 REM Two loops of identical structure and cycle count C: one all LDA zp
30 REM (every cycle a read), one all STA zp (one write cycle in three). Timed
40 REM with the host TIME (centiseconds). On a real 6502 Second Processor the
50 REM DRAM refresh cycle thief and the write-cycle stretch make LDA read about
60 REM 2.93 MHz and STA about 2.70 MHz; Beebium today runs both at 3.00 MHz.
70 zp=&70
80 K=200
90 XO=200
100 op=&A5:base=&2000:PROCasm
110 op=&85:base=&3000:PROCasm
120 YB=768*K+1792
130 C=2+(XO-1)*(YB+7)+YB+11
140 T=TIME:CALL &2000:LD=TIME-T
150 T=TIME:CALL &3000:ST=TIME-T
160 PRINT "C=";C
170 PRINT "LD=";LD
180 PRINT "ST=";ST
190 PRINT "LDAMHZ=";C/(LD/100)/1000000
200 PRINT "STAMHZ=";C/(ST/100)/1000000
210 PRINT "DONE70"
220 END
230 DEF PROCasm
240 FOR pass=0 TO 2 STEP 2
250 P%=base
260 [OPT pass
270 LDX #XO
280 .xlp LDY #0
290 .ylp
300 ]
310 FOR i=1 TO K
320 ?P%=op:P%=P%+1:?P%=zp:P%=P%+1
330 NEXT
340 [OPT pass
350 DEY:BEQ yend:JMP ylp
360 .yend DEX:BEQ xend:JMP xlp
370 .xend RTS
380 ]
390 NEXT
400 ENDPROC
