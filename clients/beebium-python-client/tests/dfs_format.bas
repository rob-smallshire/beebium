10 REM Format the disc in drive 0 for Acorn DFS: 80 tracks of ten
20 REM 256-byte sectors, through OSWORD &7F with the 8271 Format
30 REM Track command (&63), then write an empty catalogue with the
40 REM 8271 Write Sectors command (&4B). The 8271 DFS issues these to
50 REM the chip; the 1770 DFS emulates them in software (issue 249).
60 B%=&2F00:D%=&3000:A%=&7F:X%=B% MOD 256:Y%=B% DIV 256
70 FOR T%=0 TO 79
80 FOR S%=0 TO 9:D%!(S%*4)=T%+S%*&10000+&1000000:NEXT
90 ?B%=0:B%!1=D% OR &FFFF0000:B%?5=5:B%?6=&63:B%?7=T%
100 B%?8=&15:B%?9=&2A:B%?10=0:B%?11=&10:CALL &FFF1
110 IF B%?12 THEN PRINT "Format failed: track ";T%;", result &";~B%?12:END
120 NEXT
130 FOR I%=0 TO 508 STEP 4:D%!I%=0:NEXT
140 D%?&106=3:D%?&107=&20:REM 800 sectors
150 ?B%=0:B%!1=D% OR &FFFF0000:B%?5=3:B%?6=&4B:B%?7=0:B%?8=0:B%?9=&22
160 CALL &FFF1
170 IF B%?10 THEN PRINT "Catalogue failed: result &";~B%?10:END
180 PRINT "Format";"ted"
