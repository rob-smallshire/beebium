10 REM Trial 4. What an empty port reads through the operating system
20 ON ERROR PROCerr
30 *FX16,0
40 ?&24C=0
50 MODE 7
60 DIM rbuf% 1500
70 PROCstart(4)
80 PRINT "Trial 4. Empty port, read through ADVAL"'
90 PROCkey("Unplug the joystick. Press SPACE when ready.")
100 PRINT "Restoring normal sampling (*FX16,4)"'"and waiting two seconds..."
110 IF F%=0 THEN OSCLI "FX16,4"
120 PROCwait(200)
130 FOR R%=1 TO 4
140 IF F%=1 THEN PROCsynth ELSE a1%=ADVAL(1):a2%=ADVAL(2):a3%=ADVAL(3):a4%=ADVAL(4)
150 PRINT "read ";R%;": ";FNh4(a1%);" ";FNh4(a2%);" ";FNh4(a3%);" ";FNh4(a4%)
160 p$="read"+STR$R%
170 PROCw(p$+"_adval1","&"+FNh4(a1%))
180 PROCw(p$+"_adval2","&"+FNh4(a2%))
190 PROCw(p$+"_adval3","&"+FNh4(a3%))
200 PROCw(p$+"_adval4","&"+FNh4(a4%))
210 IF R%<4 THEN PROCwait(300)
220 NEXT
230 IF F%=1 THEN z%=&0400:f%=&30 ELSE z%=ADVAL(0):f%=?&FE40 AND &30
240 PRINT "ADVAL(0) ";FNh4(z%);", fire lines ";FNh(f%)
250 PROCw("adval0","&"+FNh4(z%))
260 PROCw("fire_lines","&"+FNh(f%))
270 PROCsave("R.T4")
280 PROCend
800 REM Self-test only: a wandering unplugged value, made up.
810 DEF PROCsynth
820 a1%=&CA00+16*RND(32):a2%=&C800+16*RND(32)
830 a3%=&CB00+16*RND(32):a4%=&C900+16*RND(32)
840 ENDPROC
