10 REM The four pots through ADVAL(1) to ADVAL(4), and the fire lines
20 REM through ADVAL(0) once the columns are driven low, as the manual
30 REM tells a user to: *FX151,98,112 then *FX151,96,0.
40 *FX16,4
50 *FX151,98,112
60 *FX151,96,0
70 T%=TIME:REPEAT UNTIL TIME>T%+30
80 PRINT "AD14 ";~ADVAL(1);" ";~ADVAL(2);" ";~ADVAL(3);" ";~ADVAL(4);" ";ADVAL(0) AND 3;" END14"
