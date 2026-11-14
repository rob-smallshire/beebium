10 REM The manual's six-strobe scan, side socket then rear: the
20 REM first key found, with its socket, or NONE -1.
30 *FX151,98,240
40 DATA &E0,&D0,&B0,&60,&50,&30
50 H$="NONE":K%=-1
60 FOR I%=0 TO 5
70 READ S%
80 A%=&97:X%=&60:Y%=S%:CALL &FFF4
90 A%=&96:X%=&60:R%=((USR(&FFF4) AND &FF0000) DIV &10000) AND 15
100 C%=I% MOD 3:W%=-1
110 IF R%=7 THEN W%=0
120 IF R%=11 THEN W%=1
130 IF R%=13 THEN W%=2
140 IF R%=14 THEN W%=3
150 IF W%>=0 AND K%<0 THEN K%=3*W%+C%:H$=MID$("SIDEREAR",1+4*(I% DIV 3),4)
160 NEXT
170 PRINT "SC13 ";H$;" ";K%;" END13"
