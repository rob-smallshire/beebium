10 REM Trial 5. With a joystick, read through ADVAL
20 ON ERROR PROCerr
30 *FX16,0
40 ?&24C=0
50 MODE 7
60 DIM rbuf% 1500
70 PROCstart(5)
80 PRINT "Trial 5. Joystick, read through ADVAL"'
90 PROCw("joystick",STR$J%)
100 IF J%=0 THEN PRINT "No joystick: trial 5 skipped.":PROCsave("R.T5"):PROCend
110 PROCkey("Plug in the joystick (or the pair). Press SPACE when ready.")
120 IF F%=0 THEN OSCLI "FX16,4"
130 PROCwait(100)
140 PROCp("centre","Leave the stick centred, no buttons pressed. Press SPACE.")
150 PROCp("left","Hold the stick hard LEFT. Press SPACE and keep holding until the next message.")
160 PROCp("right","Hold the stick hard RIGHT. Press SPACE and keep holding until the next message.")
170 PROCp("up","Hold the stick hard UP. Press SPACE and keep holding until the next message.")
180 PROCp("down","Hold the stick hard DOWN. Press SPACE and keep holding until the next message.")
190 PROCp("fireA","Hold down one fire button (call it A). Press SPACE and keep holding until the next message.")
200 PROCp("fireB","Hold down the other fire button (B). Press SPACE and keep holding until the next message.")
210 PROCsave("R.T5")
220 PROCend
300 DEF PROCp(n$,p$)
310 PROCkey(p$)
320 PROCwait(25)
330 IF F%=1 THEN PROCsynth ELSE a1%=ADVAL(1):a2%=ADVAL(2):a3%=ADVAL(3):a4%=ADVAL(4):z%=ADVAL(0):f%=?&FE40 AND &30
340 PRINT n$;": ";FNh4(a1%);" ";FNh4(a2%);" ";FNh4(a3%);" ";FNh4(a4%);" ADVAL(0) ";FNh4(z%);" fire ";FNh(f%)'
350 PROCw(n$+"_adval1","&"+FNh4(a1%))
360 PROCw(n$+"_adval2","&"+FNh4(a2%))
370 PROCw(n$+"_adval3","&"+FNh4(a3%))
380 PROCw(n$+"_adval4","&"+FNh4(a4%))
390 PROCw(n$+"_adval0","&"+FNh4(z%))
400 PROCw(n$+"_fire_lines","&"+FNh(f%))
410 ENDPROC
800 REM Self-test only: made-up readings for one Acorn stick on 1 and 2.
810 DEF PROCsynth
820 a1%=&7F00:a2%=&8000:a3%=&C900:a4%=&CA00:z%=&0400:f%=&30
830 IF n$="left" THEN a1%=&FFE0
840 IF n$="right" THEN a1%=&0030
850 IF n$="up" THEN a2%=&FFD0
860 IF n$="down" THEN a2%=&0040
870 IF n$="fireA" THEN z%=&0401:f%=&20
880 IF n$="fireB" THEN z%=&0402:f%=&10
890 ENDPROC
