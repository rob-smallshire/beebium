10 REM Trial 6. The reference voltage, if a meter is to hand
20 ON ERROR PROCerr
30 *FX16,0
40 ?&24C=0
50 MODE 7
60 DIM rbuf% 600
70 PROCstart(6)
80 PRINT "Trial 6. VREF, optional, needs a meter"'
90 PRINT "With the machine on, measure the voltage"
100 PRINT "between pin 11 (or 14, VREF) and pin 5"
110 PRINT "(or 8, analogue ground) of the analogue"
120 PRINT "port. Type it in volts, e.g. 1.81, and"
130 PRINT "press RETURN. With no meter, just press"
140 PRINT "RETURN to skip."'
150 v$=FNin("Voltage:",6)
160 IF v$="" THEN PROCw("skipped","1") ELSE PROCw("vref_volts",v$)
170 PROCsave("R.T6")
180 PROCend
