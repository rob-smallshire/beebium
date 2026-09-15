10 REM > StartFS
20 REM Auto-start Acorn Level 3 File Server v1.26.
30 REM The RTC dongle answers the date; a soft key answers
40 REM the "Number of drives" and station-count prompts, then
50 REM the server runs and reaches "Starting - Ready" with
60 REM no operator keystrokes.
70 IF PAGE<>&800 PRINT "Needs Tube":END
80 *KEY0 1|MS10|M
90 *FX138,0,128
100 */FS3v126
