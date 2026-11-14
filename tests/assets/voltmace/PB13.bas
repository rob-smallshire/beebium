10 REM The manual's PROCPB, rear socket: DDRB = &F0, then the
20 REM strobes &60, &50, &30 through OSBYTE &97 and reads through
30 REM OSBYTE &96. 0 when no key is held, as the manual's routine.
40 *FX151,98,240
50 DATA &60,&50,&30
60 K%=0
70 FOR C%=0 TO 2
80 READ S%
90 A%=&97:X%=&60:Y%=S%:CALL &FFF4
100 A%=&96:X%=&60:R%=((USR(&FFF4) AND &FF0000) DIV &10000) AND 15
110 IF R%=14 THEN K%=9+C%
120 IF R%=13 THEN K%=6+C%
130 IF R%=11 THEN K%=3+C%
140 IF R%=7 THEN K%=C%
150 NEXT
160 PRINT "PB13 ";K%;" END13"
