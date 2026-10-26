EESchema Schematic File Version 4
LIBS:music5xxx-cache
EELAYER 29 0
EELAYER END
$Descr A3 16535 11693
encoding utf-8
Sheet 3 3
Title ""
Date "2019-07-20"
Rev ""
Comp ""
Comment1 ""
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
$Comp
L power:+5V #PWR041
U 1 1 5DDDA341
P 1050 1000
F 0 "#PWR041" H 1050 850 50  0001 C CNN
F 1 "+5V" H 1065 1173 50  0000 C CNN
F 2 "" H 1050 1000 50  0001 C CNN
F 3 "" H 1050 1000 50  0001 C CNN
	1    1050 1000
	1    0    0    -1  
$EndComp
$Comp
L Device:Jumper_NO_Small JP1
U 1 1 5DDDA726
P 1050 1150
F 0 "JP1" V 1000 1450 50  0000 R CNN
F 1 "Jumper" V 1100 1500 50  0000 R CNN
F 2 "Pin_Headers:Pin_Header_Straight_1x02_Pitch2.54mm" H 1050 1150 50  0001 C CNN
F 3 "~" H 1050 1150 50  0001 C CNN
	1    1050 1150
	0    -1   1    0   
$EndComp
Text Notes 1000 1350 2    50   ~ 0
left open
$Comp
L Device:R R6
U 1 1 5DDDB410
P 1050 1700
F 0 "R6" H 1120 1746 50  0000 L CNN
F 1 "1K" H 1120 1655 50  0000 L CNN
F 2 "musix5xxx-custom:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal" V 980 1700 50  0001 C CNN
F 3 "~" H 1050 1700 50  0001 C CNN
	1    1050 1700
	1    0    0    -1  
$EndComp
$Comp
L power:GND #PWR042
U 1 1 5DDDB8AE
P 1050 2350
F 0 "#PWR042" H 1050 2100 50  0001 C CNN
F 1 "GND" H 1055 2177 50  0000 C CNN
F 2 "" H 1050 2350 50  0001 C CNN
F 3 "" H 1050 2350 50  0001 C CNN
	1    1050 2350
	1    0    0    -1  
$EndComp
Wire Wire Line
	1050 2350 1050 2250
Wire Wire Line
	1050 1550 1050 1300
Wire Wire Line
	1050 1050 1050 1000
$Comp
L 74xx:74LS393 IC9
U 2 1 5DDDCCC3
P 1950 1500
F 0 "IC9" H 1950 1033 50  0000 C CNN
F 1 "74HCT393" H 1950 1124 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 1950 1500 50  0001 C CNN
F 3 "74xx\\74LS393.pdf" H 1950 1500 50  0001 C CNN
	2    1950 1500
	1    0    0    1   
$EndComp
$Comp
L 74xx:74LS393 IC9
U 1 1 5DDDD509
P 1950 2450
F 0 "IC9" H 1950 1983 50  0000 C CNN
F 1 "74HCT393" H 1950 2074 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 1950 2450 50  0001 C CNN
F 3 "74xx\\74LS393.pdf" H 1950 2450 50  0001 C CNN
	1    1950 2450
	1    0    0    1   
$EndComp
Wire Wire Line
	2450 2250 2600 2250
Wire Wire Line
	2600 2250 2600 1900
Wire Wire Line
	1450 1600 1400 1600
Wire Wire Line
	1400 1600 1400 1900
Wire Wire Line
	1450 1300 1050 1300
Connection ~ 1050 1300
Wire Wire Line
	1050 1300 1050 1250
Wire Wire Line
	1450 2250 1050 2250
Connection ~ 1050 2250
Wire Wire Line
	1050 2250 1050 1850
$Comp
L 74xx:74LS04 IC?
U 6 1 5DDE5BBE
P 3000 2850
AR Path="/5C8C3E7F/5DDE5BBE" Ref="IC?"  Part="2" 
AR Path="/5DDDA239/5DDE5BBE" Ref="IC5"  Part="6" 
F 0 "IC5" H 3000 3167 50  0000 C CNN
F 1 "74F04" H 3000 3076 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 3000 2850 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS04" H 3000 2850 50  0001 C CNN
	6    3000 2850
	1    0    0    -1  
$EndComp
Wire Wire Line
	2450 2550 2600 2550
Wire Wire Line
	3300 2850 3400 2850
Wire Wire Line
	3400 2850 3400 3100
Wire Wire Line
	3400 3100 3550 3100
Text GLabel 3550 3100 2    50   Output ~ 0
PHI1
Wire Wire Line
	3400 2850 3400 2550
Connection ~ 3400 2850
Wire Wire Line
	2600 2550 2600 2850
Wire Wire Line
	2600 2850 2700 2850
$Comp
L Device:R R7
U 1 1 5DDE7CAC
P 1050 3400
F 0 "R7" V 843 3400 50  0000 C CNN
F 1 "1K" V 934 3400 50  0000 C CNN
F 2 "musix5xxx-custom:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal" V 980 3400 50  0001 C CNN
F 3 "~" H 1050 3400 50  0001 C CNN
	1    1050 3400
	0    1    1    0   
$EndComp
$Comp
L Device:R R8
U 1 1 5DDE85D8
P 2250 3350
F 0 "R8" V 2043 3350 50  0000 C CNN
F 1 "1K" V 2134 3350 50  0000 C CNN
F 2 "musix5xxx-custom:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal" V 2180 3350 50  0001 C CNN
F 3 "~" H 2250 3350 50  0001 C CNN
	1    2250 3350
	0    1    1    0   
$EndComp
$Comp
L Device:C C8
U 1 1 5DDE8CFA
P 1650 3050
F 0 "C8" V 1398 3050 50  0000 C CNN
F 1 "22nF" V 1489 3050 50  0000 C CNN
F 2 "musix5xxx-custom:C_Rect_L7.0mm_W2.5mm_P5.00mm" H 1688 2900 50  0001 C CNN
F 3 "~" H 1650 3050 50  0001 C CNN
	1    1650 3050
	0    1    1    0   
$EndComp
$Comp
L 74xx:74LS04 IC?
U 1 1 5DDE9662
P 1050 3800
AR Path="/5C8C3E7F/5DDE9662" Ref="IC?"  Part="2" 
AR Path="/5DDDA239/5DDE9662" Ref="IC5"  Part="1" 
F 0 "IC5" H 1050 4117 50  0000 C CNN
F 1 "74F04" H 1050 4026 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 1050 3800 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS04" H 1050 3800 50  0001 C CNN
	1    1050 3800
	1    0    0    1   
$EndComp
$Comp
L 74xx:74LS04 IC?
U 3 1 5DDEA9CA
P 2250 3800
AR Path="/5C8C3E7F/5DDEA9CA" Ref="IC?"  Part="2" 
AR Path="/5DDDA239/5DDEA9CA" Ref="IC5"  Part="3" 
F 0 "IC5" H 2250 4117 50  0000 C CNN
F 1 "74F04" H 2250 4026 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 2250 3800 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS04" H 2250 3800 50  0001 C CNN
	3    2250 3800
	1    0    0    1   
$EndComp
$Comp
L Device:Crystal X1
U 1 1 5DDEB763
P 1650 3800
F 0 "X1" H 1650 3450 50  0000 C CNN
F 1 "12 Mhz" H 1650 3600 50  0000 C CNN
F 2 "musix5xxx-custom:Crystal_HC49-U_Vertical" H 1650 3800 50  0001 C CNN
F 3 "~" H 1650 3800 50  0001 C CNN
	1    1650 3800
	1    0    0    -1  
$EndComp
Wire Wire Line
	1800 3800 1900 3800
Wire Wire Line
	1350 3800 1400 3800
Wire Wire Line
	750  3800 700  3800
Wire Wire Line
	700  3800 700  3400
Wire Wire Line
	700  3050 1500 3050
Wire Wire Line
	1800 3050 2600 3050
Wire Wire Line
	2600 3050 2600 3350
Wire Wire Line
	2600 3800 2550 3800
Wire Wire Line
	2600 3800 2600 3950
Connection ~ 2600 3800
$Comp
L Device:C C9
U 1 1 5DDF0ABF
P 2600 4100
F 0 "C9" H 2485 4054 50  0000 R CNN
F 1 "33pF" H 2485 4145 50  0000 R CNN
F 2 "musix5xxx-custom:C_Disc_D3.4mm_W2.1mm_P2.50mm" H 2638 3950 50  0001 C CNN
F 3 "~" H 2600 4100 50  0001 C CNN
	1    2600 4100
	-1   0    0    1   
$EndComp
$Comp
L power:GND #PWR045
U 1 1 5DDF1B6A
P 2600 4350
F 0 "#PWR045" H 2600 4100 50  0001 C CNN
F 1 "GND" H 2605 4177 50  0000 C CNN
F 2 "" H 2600 4350 50  0001 C CNN
F 3 "" H 2600 4350 50  0001 C CNN
	1    2600 4350
	1    0    0    -1  
$EndComp
Wire Wire Line
	2600 4350 2600 4250
Text Notes 3200 4350 2    50   ~ 0
C9 Not fitted\nas standard
Wire Wire Line
	1400 3800 1400 3400
Wire Wire Line
	1400 3400 1200 3400
Connection ~ 1400 3800
Wire Wire Line
	1400 3800 1500 3800
Wire Wire Line
	900  3400 700  3400
Connection ~ 700  3400
Wire Wire Line
	700  3400 700  3050
Wire Wire Line
	1900 3800 1900 3350
Wire Wire Line
	1900 3350 2100 3350
Connection ~ 1900 3800
Wire Wire Line
	1900 3800 1950 3800
Wire Wire Line
	2400 3350 2600 3350
Connection ~ 2600 3350
Wire Wire Line
	2600 3350 2600 3800
Wire Wire Line
	1400 3400 1400 2550
Wire Wire Line
	1400 2550 1450 2550
Connection ~ 1400 3400
$Comp
L 74xx:74LS04 IC?
U 4 1 5DDFA952
P 5500 3100
AR Path="/5C8C3E7F/5DDFA952" Ref="IC?"  Part="2" 
AR Path="/5DDDA239/5DDFA952" Ref="IC5"  Part="4" 
F 0 "IC5" H 5500 3417 50  0000 C CNN
F 1 "74F04" H 5500 3326 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 5500 3100 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS04" H 5500 3100 50  0001 C CNN
	4    5500 3100
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS374 IC13
U 1 1 5DDFD766
P 3450 1850
F 0 "IC13" H 3450 1150 50  0000 C CNN
F 1 "74HCT374" H 3450 1250 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 3450 1850 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS374" H 3450 1850 50  0001 C CNN
	1    3450 1850
	1    0    0    1   
$EndComp
Text GLabel 6000 3100 2    50   Output ~ 0
~ADDR0
Wire Wire Line
	5800 3100 6000 3100
Wire Wire Line
	5000 3100 5200 3100
$Comp
L music5xxx-custom:74LS138 IC10
U 1 1 5DE0BD7A
P 5750 1750
F 0 "IC10" H 5300 2350 50  0000 C CNN
F 1 "74HCT138" H 5350 2250 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 5750 1750 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS138" H 5750 1750 50  0001 C CNN
	1    5750 1750
	1    0    0    -1  
$EndComp
$Comp
L power:GND #PWR049
U 1 1 5DE103E5
P 5750 2550
F 0 "#PWR049" H 5750 2300 50  0001 C CNN
F 1 "GND" H 5755 2377 50  0000 C CNN
F 2 "" H 5750 2550 50  0001 C CNN
F 3 "" H 5750 2550 50  0001 C CNN
	1    5750 2550
	1    0    0    -1  
$EndComp
Wire Wire Line
	5650 2450 5650 2500
Wire Wire Line
	5650 2500 5750 2500
Wire Wire Line
	5750 2500 5750 2550
Wire Wire Line
	5750 2500 5850 2500
Wire Wire Line
	5850 2500 5850 2450
Connection ~ 5750 2500
$Comp
L power:+5V #PWR048
U 1 1 5DE12470
P 5650 1100
F 0 "#PWR048" H 5650 950 50  0001 C CNN
F 1 "+5V" H 5665 1273 50  0000 C CNN
F 2 "" H 5650 1100 50  0001 C CNN
F 3 "" H 5650 1100 50  0001 C CNN
	1    5650 1100
	1    0    0    -1  
$EndComp
Wire Wire Line
	5650 1100 5650 1150
Wire Wire Line
	5250 2050 5000 2050
Wire Wire Line
	5000 2050 5000 3100
Text Label 5000 2050 0    50   ~ 0
addr0
Text Label 5000 1950 0    50   ~ 0
addr1
Text Label 5000 1850 0    50   ~ 0
addr2
Wire Wire Line
	5000 1850 5250 1850
Wire Wire Line
	5000 1950 5250 1950
Wire Wire Line
	6250 2150 6300 2150
Text GLabel 6300 2150 2    50   Output ~ 0
~S0
Wire Wire Line
	6250 2050 6500 2050
Text GLabel 6500 2050 2    50   Output ~ 0
~S1
Wire Wire Line
	6250 1550 6300 1550
Text GLabel 6300 1550 2    50   Output ~ 0
~S6
Wire Wire Line
	6250 1450 6850 1450
Text GLabel 7000 1450 2    50   Output ~ 0
~S7
Wire Wire Line
	6250 1750 6500 1750
Text GLabel 6500 1750 2    50   Output ~ 0
~S4
Wire Wire Line
	2950 1900 2600 1900
Connection ~ 2600 1900
Wire Wire Line
	2450 2450 2600 2450
Wire Wire Line
	2450 2350 2600 2350
Text Label 2850 1900 0    50   ~ 0
i3
Wire Wire Line
	2950 2000 2850 2000
Text Label 2850 2000 0    50   ~ 0
i2
Wire Wire Line
	2950 2100 2850 2100
Text Label 2850 2100 0    50   ~ 0
i1
Wire Wire Line
	2950 2200 2850 2200
Text Label 2850 2200 0    50   ~ 0
i0
Wire Wire Line
	2950 1600 2850 1600
Text Label 2850 1600 0    50   ~ 0
i6
Wire Wire Line
	2950 1700 2850 1700
Text Label 2850 1700 0    50   ~ 0
i5
Wire Wire Line
	2950 1800 2850 1800
Text Label 2850 1800 0    50   ~ 0
i4
Wire Wire Line
	2450 1400 2550 1400
Wire Wire Line
	2450 1300 2550 1300
Wire Wire Line
	2450 1600 2550 1600
Wire Wire Line
	2450 1500 2550 1500
Text Label 2550 1300 2    50   ~ 0
i1
Text Label 2550 1400 2    50   ~ 0
i6
Text Label 2550 1500 2    50   ~ 0
i2
Text Label 2550 1600 2    50   ~ 0
i4
Text Label 2600 2250 2    50   ~ 0
i3
Text Label 2600 2350 2    50   ~ 0
i0
Text Label 2600 2450 2    50   ~ 0
i5
NoConn ~ 6250 1650
NoConn ~ 6250 1850
NoConn ~ 6250 1950
Wire Wire Line
	1400 1900 2600 1900
Text Label 2700 1500 0    50   ~ 0
addr1
Wire Wire Line
	2700 1500 2950 1500
$Comp
L 74xx:74LS86 IC15
U 2 1 5DEE5EF9
P 1250 6550
F 0 "IC15" H 1250 6875 50  0000 C CNN
F 1 "74HCT86" H 1250 6784 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 1250 6550 50  0001 C CNN
F 3 "74xx/74ls86.pdf" H 1250 6550 50  0001 C CNN
	2    1250 6550
	-1   0    0    -1  
$EndComp
$Comp
L 74xx:74LS86 IC15
U 1 1 5DEE73AD
P 1900 6650
F 0 "IC15" H 1900 6975 50  0000 C CNN
F 1 "74HCT86" H 1900 6884 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 1900 6650 50  0001 C CNN
F 3 "74xx/74ls86.pdf" H 1900 6650 50  0001 C CNN
	1    1900 6650
	-1   0    0    1   
$EndComp
Wire Wire Line
	1600 6650 1550 6650
$Comp
L power:+5V #PWR043
U 1 1 5DEEF34E
P 1600 6300
F 0 "#PWR043" H 1600 6150 50  0001 C CNN
F 1 "+5V" H 1615 6473 50  0000 C CNN
F 2 "" H 1600 6300 50  0001 C CNN
F 3 "" H 1600 6300 50  0001 C CNN
	1    1600 6300
	1    0    0    -1  
$EndComp
Wire Wire Line
	1600 6300 1600 6450
Wire Wire Line
	1600 6450 1550 6450
Wire Wire Line
	950  6550 900  6550
Text GLabel 900  6550 0    50   Output ~ 0
~GATE
Text GLabel 1700 7300 0    50   Output ~ 0
CY4
Text GLabel 1700 8000 0    50   Output ~ 0
SIGN
Text GLabel 1950 9400 0    50   Input ~ 0
PHI1
Text GLabel 1950 10200 0    50   Input ~ 0
data[0..7]
Wire Wire Line
	1700 8000 2250 8000
Wire Wire Line
	2250 6750 2200 6750
$Comp
L music5xxx-custom:74LS283 IC11
U 1 1 5DF05A11
P 3100 7300
F 0 "IC11" V 3600 7900 50  0000 C CNN
F 1 "74HCT283" V 3500 7900 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 3100 7300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS283" H 3100 7300 50  0001 C CNN
	1    3100 7300
	0    -1   -1   0   
$EndComp
Wire Wire Line
	3900 7300 3950 7300
Wire Wire Line
	2850 6550 2850 6800
Wire Wire Line
	2200 6550 2850 6550
Connection ~ 2250 8000
Connection ~ 2850 6550
Text Label 2850 6150 3    50   ~ 0
sum7
Wire Wire Line
	2550 8000 2550 7800
Wire Wire Line
	2250 8000 2550 8000
Wire Wire Line
	2250 6750 2250 8000
Wire Wire Line
	1700 7300 2300 7300
Text Label 3000 6150 3    50   ~ 0
sum6
Text Label 3200 6150 3    50   ~ 0
sum5
Text Label 3350 6150 3    50   ~ 0
sum4
Text Label 4500 6150 3    50   ~ 0
sum3
Text Label 4650 6150 3    50   ~ 0
sum2
Text Label 4850 6150 3    50   ~ 0
sum1
Text Label 5000 6150 3    50   ~ 0
sum0
Wire Wire Line
	3350 6800 3350 6050
Wire Wire Line
	3200 6800 3200 6050
Wire Wire Line
	3000 6800 3000 6050
Wire Wire Line
	2850 6550 2850 6050
Entry Wire Line
	4900 5950 5000 6050
Entry Wire Line
	4750 5950 4850 6050
Entry Wire Line
	4550 5950 4650 6050
Entry Wire Line
	3250 5950 3350 6050
Entry Wire Line
	3100 5950 3200 6050
Entry Wire Line
	2900 5950 3000 6050
Entry Wire Line
	2750 5950 2850 6050
Text GLabel 1050 5950 0    50   Output ~ 0
sum[0..7]
Entry Wire Line
	4400 5950 4500 6050
$Comp
L 74xx:74LS08 IC2
U 4 1 5DF98380
P 5950 7300
F 0 "IC2" H 5950 7625 50  0000 C CNN
F 1 "74HCT08" H 5950 7534 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 5950 7300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 5950 7300 50  0001 C CNN
	4    5950 7300
	-1   0    0    -1  
$EndComp
Wire Wire Line
	5650 7300 5550 7300
Wire Wire Line
	6250 7400 6550 7400
Text Label 6550 7400 2    50   ~ 0
addr2
Text GLabel 6350 7200 2    50   Input ~ 0
C4D
Wire Wire Line
	6350 7200 6250 7200
$Comp
L music5xxx-custom:74LS374 IC16
U 1 1 5DFAD9F3
P 3050 9350
F 0 "IC16" V 3096 8606 50  0000 R CNN
F 1 "74HCT374" V 3005 8606 50  0000 R CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 3050 9350 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS374" H 3050 9350 50  0001 C CNN
	1    3050 9350
	0    -1   -1   0   
$EndComp
Wire Wire Line
	4200 7800 4200 8500
Wire Wire Line
	4350 7800 4350 8500
Wire Wire Line
	4500 7800 4500 8500
Wire Wire Line
	4650 7800 4650 8500
Wire Wire Line
	2700 8850 2700 8700
Wire Wire Line
	3400 8850 3400 8700
Wire Wire Line
	2800 8850 2800 8700
Wire Wire Line
	3300 8850 3300 8700
Entry Wire Line
	4200 8500 4100 8600
Entry Wire Line
	4350 8500 4250 8600
Entry Wire Line
	4500 8500 4400 8600
Entry Wire Line
	4650 8500 4550 8600
Entry Wire Line
	3500 8600 3400 8700
Entry Wire Line
	3400 8600 3300 8700
Entry Wire Line
	2900 8600 2800 8700
Entry Wire Line
	2800 8600 2700 8700
Wire Wire Line
	2700 8200 2700 7800
Wire Wire Line
	3100 8850 3100 8400
Wire Wire Line
	2850 8200 2850 7800
Text Label 4200 7950 1    50   ~ 0
sm3
Text Label 3300 8850 1    50   ~ 0
sm3
Text Label 4350 7950 1    50   ~ 0
sm2
Text Label 4500 7950 1    50   ~ 0
sm1
Text Label 4650 7950 1    50   ~ 0
sm0
Text Label 2800 8850 1    50   ~ 0
sm2
Text Label 3400 8850 1    50   ~ 0
sm1
Text Label 2700 8850 1    50   ~ 0
sm0
Wire Wire Line
	2900 8700 2950 8700
Wire Wire Line
	3200 8850 3200 8400
Wire Wire Line
	2550 8200 2550 8000
Connection ~ 2550 8000
Text Label 3650 7950 1    50   ~ 0
sn4
Text Label 3500 7950 1    50   ~ 0
sn5
Text Label 3350 7950 1    50   ~ 0
sn6
Text Label 3200 7950 1    50   ~ 0
sn7
$Comp
L power:GND #PWR044
U 1 1 5E044F47
P 2100 9150
F 0 "#PWR044" H 2100 8900 50  0001 C CNN
F 1 "GND" H 2105 8977 50  0000 C CNN
F 2 "" H 2100 9150 50  0001 C CNN
F 3 "" H 2100 9150 50  0001 C CNN
	1    2100 9150
	1    0    0    -1  
$EndComp
Wire Wire Line
	2350 9300 2350 9100
Wire Wire Line
	2350 9100 2100 9100
Wire Wire Line
	2100 9100 2100 9150
Wire Wire Line
	1950 9400 2350 9400
Entry Wire Line
	2700 10100 2600 10200
Wire Wire Line
	2700 10100 2700 9850
Entry Wire Line
	2800 10100 2700 10200
Wire Wire Line
	2800 10100 2800 9850
Entry Wire Line
	2900 10100 2800 10200
Wire Wire Line
	2900 10100 2900 9850
Entry Wire Line
	3000 10100 2900 10200
Wire Wire Line
	3000 10100 3000 9850
Entry Wire Line
	3100 10100 3000 10200
Wire Wire Line
	3100 10100 3100 9850
Entry Wire Line
	3200 10100 3100 10200
Wire Wire Line
	3200 10100 3200 9850
Entry Wire Line
	3300 10100 3200 10200
Wire Wire Line
	3300 10100 3300 9850
Entry Wire Line
	3400 10100 3300 10200
Wire Wire Line
	3400 10100 3400 9850
Text Label 2700 10050 1    50   ~ 0
data0
Text Label 3400 10050 1    50   ~ 0
data1
Text Label 2800 10050 1    50   ~ 0
data2
Text Label 3000 10050 1    50   ~ 0
data4
Text Label 3100 10050 1    50   ~ 0
data5
Text Label 2900 10050 1    50   ~ 0
data6
Text Label 3200 10050 1    50   ~ 0
data7
NoConn ~ 4150 10500
Wire Wire Line
	3400 10500 3550 10500
Text Label 3400 10500 1    50   ~ 0
data0
Wire Wire Line
	3850 11000 3850 10900
Wire Wire Line
	3250 10600 3550 10600
Wire Wire Line
	3850 10250 3850 10300
$Comp
L power:+5V #PWR047
U 1 1 5E0B6AB7
P 3850 10250
F 0 "#PWR047" H 3850 10100 50  0001 C CNN
F 1 "+5V" H 3865 10423 50  0000 C CNN
F 2 "" H 3850 10250 50  0001 C CNN
F 3 "" H 3850 10250 50  0001 C CNN
	1    3850 10250
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS74 IC3
U 2 1 5E0A800C
P 3850 10600
F 0 "IC3" H 4050 11100 50  0000 C CNN
F 1 "74HCT74" H 4050 10950 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 3850 10600 50  0001 C CNN
F 3 "74xx/74hc_hct74.pdf" H 3850 10600 50  0001 C CNN
	2    3850 10600
	1    0    0    -1  
$EndComp
Wire Wire Line
	5000 10700 4150 10700
Wire Wire Line
	5000 6800 5000 6050
Wire Wire Line
	4850 6800 4850 6050
Wire Wire Line
	4650 6800 4650 6050
Wire Wire Line
	4500 6800 4500 6050
Text Label 4850 7950 1    50   ~ 0
sn3
Text Label 5000 7950 1    50   ~ 0
sn2
Text Label 5150 7950 1    50   ~ 0
sn1
Text Label 5300 7950 1    50   ~ 0
sn0
$Comp
L music5xxx-custom:74LS283 IC6
U 1 1 5DF11897
P 4750 7300
F 0 "IC6" V 5250 7900 50  0000 C CNN
F 1 "74HCT283" V 5150 7900 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 4750 7300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS283" H 4750 7300 50  0001 C CNN
	1    4750 7300
	0    -1   -1   0   
$EndComp
Text GLabel 8700 8450 2    50   Input ~ 0
PHI1
Wire Wire Line
	8700 8450 8500 8450
Wire Wire Line
	8500 8550 8700 8550
$Comp
L music5xxx-custom:74LS244 IC17
U 1 1 5E4BB63D
P 7800 6800
F 0 "IC17" V 7750 6100 50  0000 R CNN
F 1 "74HCT244" V 7850 6150 50  0000 R CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 7800 6800 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS244" H 7800 6800 50  0001 C CNN
	1    7800 6800
	0    1    1    0   
$EndComp
Wire Wire Line
	8550 6700 8750 6700
Wire Wire Line
	8750 6700 8750 6800
Wire Wire Line
	8750 6900 8550 6900
Wire Wire Line
	8750 6800 9100 6800
Connection ~ 8750 6800
Wire Wire Line
	8750 6800 8750 6900
Entry Wire Line
	7650 5950 7750 6050
Entry Wire Line
	7550 5950 7650 6050
Entry Wire Line
	7450 5950 7550 6050
Entry Wire Line
	7350 5950 7450 6050
Wire Wire Line
	5000 9600 5000 10700
$Comp
L power:GND #PWR050
U 1 1 5E489E0D
P 8700 8600
F 0 "#PWR050" H 8700 8350 50  0001 C CNN
F 1 "GND" H 8705 8427 50  0000 C CNN
F 2 "" H 8700 8600 50  0001 C CNN
F 3 "" H 8700 8600 50  0001 C CNN
	1    8700 8600
	1    0    0    -1  
$EndComp
Wire Wire Line
	8700 8550 8700 8600
Wire Wire Line
	7900 9500 7550 9500
Connection ~ 7900 9500
Wire Wire Line
	7550 9500 7200 9500
Connection ~ 7550 9500
Wire Wire Line
	7200 9500 6850 9500
Connection ~ 7200 9500
Wire Wire Line
	6850 9500 6500 9500
Connection ~ 6850 9500
Connection ~ 6500 9500
Connection ~ 8250 9500
Wire Wire Line
	8250 9500 7900 9500
Wire Wire Line
	6500 9500 6500 9600
Connection ~ 8600 9500
Wire Wire Line
	8600 9500 8250 9500
Wire Wire Line
	8950 9500 8600 9500
Wire Wire Line
	9150 9000 9150 9600
Wire Wire Line
	8150 9000 9150 9000
Wire Wire Line
	8800 9150 8800 9600
Wire Wire Line
	8050 9150 8050 9000
Wire Wire Line
	8800 9150 8050 9150
Wire Wire Line
	7950 9250 7950 9000
Wire Wire Line
	8450 9250 7950 9250
Wire Wire Line
	7850 9350 7850 9000
Wire Wire Line
	8100 9350 7850 9350
Wire Wire Line
	7750 9000 7750 9600
Wire Wire Line
	7650 9250 7650 9000
Wire Wire Line
	7400 9250 7650 9250
Wire Wire Line
	7050 9150 7050 9600
Wire Wire Line
	7550 9150 7550 9000
Wire Wire Line
	7050 9150 7550 9150
Wire Wire Line
	7450 9050 7450 9000
Wire Wire Line
	6700 9050 7450 9050
$Comp
L music5xxx-custom:74LS374 IC12
U 1 1 5E2E4131
P 7800 8500
F 0 "IC12" V 7750 7900 50  0000 R CNN
F 1 "74HCT374" V 7850 7900 50  0000 R CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 7800 8500 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS374" H 7800 8500 50  0001 C CNN
	1    7800 8500
	0    1    1    0   
$EndComp
Entry Wire Line
	6600 10350 6500 10450
Entry Wire Line
	6950 10350 6850 10450
Entry Wire Line
	8700 10350 8600 10450
Entry Wire Line
	9050 10350 8950 10450
Entry Wire Line
	8000 10350 7900 10450
Entry Wire Line
	8350 10350 8250 10450
Entry Wire Line
	7300 10350 7200 10450
Entry Wire Line
	7650 10350 7550 10450
Wire Wire Line
	6600 10200 6600 10350
Wire Wire Line
	6950 10200 6950 10350
Wire Wire Line
	8700 10200 8700 10350
Wire Wire Line
	9050 10200 9050 10350
Wire Wire Line
	8000 10200 8000 10350
Wire Wire Line
	8350 10200 8350 10350
Wire Wire Line
	7300 10200 7300 10350
Wire Wire Line
	7650 10200 7650 10350
Text Label 7650 10350 1    50   ~ 0
sn7
Text Label 7300 10350 1    50   ~ 0
sn6
Text Label 8350 10350 1    50   ~ 0
sn5
Text Label 8000 10350 1    50   ~ 0
sn4
Text Label 6600 10350 1    50   ~ 0
sn0
Text Label 6950 10350 1    50   ~ 0
sn1
Text Label 8700 10350 1    50   ~ 0
sn2
Text Label 9050 10350 1    50   ~ 0
sn3
Wire Wire Line
	6700 9600 6700 9050
Wire Wire Line
	8100 9600 8100 9350
Wire Wire Line
	8450 9600 8450 9250
Wire Wire Line
	7400 9250 7400 9600
Wire Wire Line
	7550 9600 7550 9500
Wire Wire Line
	7200 9600 7200 9500
Wire Wire Line
	8250 9600 8250 9500
Wire Wire Line
	7900 9500 7900 9600
Wire Wire Line
	8950 9600 8950 9500
Wire Wire Line
	8600 9600 8600 9500
Wire Wire Line
	6850 9600 6850 9500
Wire Wire Line
	6400 9500 6500 9500
$Comp
L 74xx:74LS08 IC7
U 1 1 5E181B55
P 6600 9900
F 0 "IC7" V 6550 9800 50  0000 L CNN
F 1 "74HCT08" V 6650 9750 50  0000 L CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 6600 9900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 6600 9900 50  0001 C CNN
	1    6600 9900
	0    1    1    0   
$EndComp
$Comp
L 74xx:74LS08 IC7
U 2 1 5E181B4F
P 6950 9900
F 0 "IC7" V 6900 9800 50  0000 L CNN
F 1 "74HCT08" V 7000 9750 50  0000 L CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 6950 9900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 6950 9900 50  0001 C CNN
	2    6950 9900
	0    1    1    0   
$EndComp
$Comp
L 74xx:74LS08 IC7
U 4 1 5E181B49
P 9050 9900
F 0 "IC7" V 9000 9800 50  0000 L CNN
F 1 "74HCT08" V 9100 9750 50  0000 L CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 9050 9900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 9050 9900 50  0001 C CNN
	4    9050 9900
	0    1    1    0   
$EndComp
$Comp
L 74xx:74LS08 IC7
U 3 1 5E181B43
P 8700 9900
F 0 "IC7" V 8650 9850 50  0000 L CNN
F 1 "74HCT08" V 8750 9750 50  0000 L CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 8700 9900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 8700 9900 50  0001 C CNN
	3    8700 9900
	0    1    1    0   
$EndComp
$Comp
L 74xx:74LS08 IC8
U 2 1 5E16F413
P 8000 9900
F 0 "IC8" V 7950 9800 50  0000 L CNN
F 1 "74HCT08" V 8050 9750 50  0000 L CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 8000 9900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 8000 9900 50  0001 C CNN
	2    8000 9900
	0    1    1    0   
$EndComp
$Comp
L 74xx:74LS08 IC8
U 1 1 5E16C24F
P 8350 9900
F 0 "IC8" V 8300 9800 50  0000 L CNN
F 1 "74HCT08" V 8400 9750 50  0000 L CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 8350 9900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 8350 9900 50  0001 C CNN
	1    8350 9900
	0    1    1    0   
$EndComp
$Comp
L 74xx:74LS08 IC8
U 3 1 5E15D612
P 7650 9900
F 0 "IC8" V 7600 9800 50  0000 L CNN
F 1 "74HCT08" V 7700 9750 50  0000 L CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 7650 9900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 7650 9900 50  0001 C CNN
	3    7650 9900
	0    1    1    0   
$EndComp
$Comp
L 74xx:74LS08 IC8
U 4 1 5E1408CC
P 7300 9900
F 0 "IC8" V 7250 9850 50  0000 L CNN
F 1 "74HCT08" V 7350 9750 50  0000 L CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 7300 9900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 7300 9900 50  0001 C CNN
	4    7300 9900
	0    1    1    0   
$EndComp
Wire Wire Line
	5800 9600 5000 9600
Wire Wire Line
	5750 9400 5800 9400
Text GLabel 5750 9400 0    50   Input ~ 0
~ADDR0
$Comp
L 74xx:74LS08 IC2
U 3 1 5E11E8C6
P 6100 9500
F 0 "IC2" H 6100 9183 50  0000 C CNN
F 1 "74HCT08" H 6100 9274 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 6100 9500 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 6100 9500 50  0001 C CNN
	3    6100 9500
	1    0    0    1   
$EndComp
Entry Wire Line
	8050 5950 8150 6050
Entry Wire Line
	7950 5950 8050 6050
Entry Wire Line
	7850 5950 7950 6050
Entry Wire Line
	7750 5950 7850 6050
Wire Wire Line
	8150 6050 8150 6300
Wire Wire Line
	8050 6050 8050 6300
Wire Wire Line
	7950 6050 7950 6300
Wire Wire Line
	7850 6050 7850 6300
Wire Wire Line
	7750 6300 7750 6050
Wire Wire Line
	7650 6300 7650 6050
Wire Wire Line
	7550 6300 7550 6050
Wire Wire Line
	7450 6300 7450 6050
Text Label 7450 6100 3    50   ~ 0
sum0
Text Label 8050 6100 3    50   ~ 0
sum1
Text Label 7550 6100 3    50   ~ 0
sum2
Text Label 8150 6100 3    50   ~ 0
sum3
Text Label 7750 6100 3    50   ~ 0
sum4
Text Label 7950 6100 3    50   ~ 0
sum5
Text Label 7650 6100 3    50   ~ 0
sum6
Text Label 7850 6100 3    50   ~ 0
sum7
Entry Wire Line
	7750 7500 7850 7600
Entry Wire Line
	7650 7500 7750 7600
Entry Wire Line
	7550 7500 7650 7600
Entry Wire Line
	7450 7500 7550 7600
Entry Wire Line
	8150 7500 8250 7600
Entry Wire Line
	8050 7500 8150 7600
Entry Wire Line
	7950 7500 8050 7600
Entry Wire Line
	7850 7500 7950 7600
Entry Wire Line
	7750 7800 7850 7700
Entry Wire Line
	7650 7800 7750 7700
Entry Wire Line
	7550 7800 7650 7700
Entry Wire Line
	7450 7800 7550 7700
Entry Wire Line
	8150 7800 8250 7700
Entry Wire Line
	8050 7800 8150 7700
Entry Wire Line
	7950 7800 8050 7700
Entry Wire Line
	7850 7800 7950 7700
Wire Bus Line
	8350 7600 8350 7650
Wire Wire Line
	7450 7500 7450 7300
Wire Wire Line
	7550 7500 7550 7300
Wire Wire Line
	7650 7500 7650 7300
Wire Wire Line
	7750 7500 7750 7300
Wire Wire Line
	7850 7500 7850 7300
Wire Wire Line
	7950 7500 7950 7300
Wire Wire Line
	8050 7500 8050 7300
Wire Wire Line
	8150 7500 8150 7300
Wire Wire Line
	7450 8000 7450 7800
Wire Wire Line
	7550 8000 7550 7800
Wire Wire Line
	7650 8000 7650 7800
Wire Wire Line
	7750 8000 7750 7800
Wire Wire Line
	7850 8000 7850 7800
Wire Wire Line
	7950 8000 7950 7800
Wire Wire Line
	8050 8000 8050 7800
Wire Wire Line
	8150 8000 8150 7800
Connection ~ 8350 7650
Wire Bus Line
	8350 7650 8350 7700
Text Label 7450 7300 3    50   ~ 0
mm0
Text Label 8050 7300 3    50   ~ 0
mm1
Text Label 7550 7300 3    50   ~ 0
mm2
Text Label 8150 7300 3    50   ~ 0
mm3
Text Label 7750 7300 3    50   ~ 0
mm4
Text Label 7950 7300 3    50   ~ 0
mm5
Text Label 7650 7300 3    50   ~ 0
mm6
Text Label 7850 7300 3    50   ~ 0
mm7
Text Label 7450 8000 1    50   ~ 0
mm0
Text Label 7550 8000 1    50   ~ 0
mm1
Text Label 8050 8000 1    50   ~ 0
mm2
Text Label 8150 8000 1    50   ~ 0
mm3
Text Label 7850 8000 1    50   ~ 0
mm4
Text Label 7950 8000 1    50   ~ 0
mm5
Text Label 7650 8000 1    50   ~ 0
mm6
Text Label 7750 8000 1    50   ~ 0
mm7
$Comp
L music5xxx-custom:AM9128-10 IC?
U 1 1 5E6E5B87
P 10400 6600
AR Path="/5C8C3E7F/5E6E5B87" Ref="IC?"  Part="1" 
AR Path="/5DDDA239/5E6E5B87" Ref="IC22"  Part="1" 
F 0 "IC22" H 10400 7450 50  0000 C CNN
F 1 "AM9128-10" H 10400 7350 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-24_W15.24mm_LongPads" H 10400 6600 50  0001 C CNN
F 3 "" H 10400 6600 50  0001 C CNN
	1    10400 6600
	1    0    0    -1  
$EndComp
Entry Wire Line
	9650 6550 9550 6650
Entry Wire Line
	9650 6450 9550 6550
Entry Wire Line
	9650 6350 9550 6450
Entry Wire Line
	9650 6250 9550 6350
Entry Wire Line
	9650 6950 9550 7050
Entry Wire Line
	9650 6850 9550 6950
Entry Wire Line
	9650 6750 9550 6850
Entry Wire Line
	9650 6650 9550 6750
Wire Wire Line
	9650 6250 9850 6250
Wire Wire Line
	9650 6350 9850 6350
Wire Wire Line
	9650 6450 9850 6450
Wire Wire Line
	9650 6550 9850 6550
Wire Wire Line
	9650 6650 9850 6650
Wire Wire Line
	9650 6750 9850 6750
Wire Wire Line
	9650 6850 9850 6850
Wire Wire Line
	9650 6950 9850 6950
Text Label 9850 6750 2    50   ~ 0
mm0
Text Label 9850 6850 2    50   ~ 0
mm1
Text Label 9850 6950 2    50   ~ 0
mm2
Text Label 9850 6650 2    50   ~ 0
mm3
Text Label 9850 6550 2    50   ~ 0
mm4
Text Label 9850 6250 2    50   ~ 0
mm5
Text Label 9850 6350 2    50   ~ 0
mm6
Text Label 9850 6450 2    50   ~ 0
mm7
Wire Bus Line
	8350 7650 9550 7650
$Comp
L power:GND #PWR054
U 1 1 5E72E668
P 10400 7700
F 0 "#PWR054" H 10400 7450 50  0001 C CNN
F 1 "GND" H 10405 7527 50  0000 C CNN
F 2 "" H 10400 7700 50  0001 C CNN
F 3 "" H 10400 7700 50  0001 C CNN
	1    10400 7700
	1    0    0    -1  
$EndComp
Wire Wire Line
	10400 7700 10400 7450
$Comp
L 74xx:74LS00 IC?
U 4 1 5E741158
P 9700 8400
AR Path="/5C8C3E7F/5E741158" Ref="IC?"  Part="3" 
AR Path="/5DDDA239/5E741158" Ref="IC14"  Part="4" 
F 0 "IC14" H 9650 8650 50  0000 C CNN
F 1 "74HCT00" H 9650 8750 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 9700 8400 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 9700 8400 50  0001 C CNN
	4    9700 8400
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS00 IC?
U 2 1 5E74F3C9
P 10450 8500
AR Path="/5C8C3E7F/5E74F3C9" Ref="IC?"  Part="3" 
AR Path="/5DDDA239/5E74F3C9" Ref="IC14"  Part="2" 
F 0 "IC14" H 10400 8750 50  0000 C CNN
F 1 "74HCT00" H 10400 8850 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 10450 8500 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 10450 8500 50  0001 C CNN
	2    10450 8500
	1    0    0    -1  
$EndComp
Wire Wire Line
	10000 8400 10050 8400
Wire Wire Line
	10050 8400 10050 7700
Wire Wire Line
	10050 7700 10250 7700
Wire Wire Line
	10250 7700 10250 7450
Connection ~ 10050 8400
Wire Wire Line
	10050 8400 10150 8400
Wire Wire Line
	10550 7450 10550 7550
Wire Wire Line
	10550 7550 9100 7550
Wire Wire Line
	9100 7550 9100 6800
Text GLabel 10050 8600 0    50   Input ~ 0
~S0
Wire Wire Line
	10050 8600 10150 8600
Text GLabel 9300 8300 0    50   Input ~ 0
~SX
Wire Wire Line
	9300 8300 9400 8300
Text Label 9150 8500 0    50   ~ 0
addr0
$Comp
L 74xx:74LS32 IC4
U 2 1 5E7EE5B2
P 10950 8100
F 0 "IC4" V 10996 7920 50  0000 R CNN
F 1 "74HCT32" V 10905 7920 50  0000 R CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 10950 8100 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 10950 8100 50  0001 C CNN
	2    10950 8100
	0    -1   -1   0   
$EndComp
Wire Wire Line
	10750 8500 10850 8500
Text GLabel 11150 8500 2    50   Input ~ 0
PHI1
Wire Wire Line
	11150 8500 11050 8500
Wire Wire Line
	10950 7550 10550 7550
Connection ~ 10550 7550
Wire Wire Line
	10850 8400 10850 8500
Wire Wire Line
	11050 8500 11050 8400
Wire Wire Line
	10950 7550 10950 7800
Text GLabel 11600 6950 2    50   Input ~ 0
addr[0..6]
Text GLabel 11400 6450 2    50   Input ~ 0
PA1
Wire Wire Line
	10950 6450 11400 6450
Text GLabel 11400 6050 2    50   Input ~ 0
PA2
Wire Wire Line
	10950 6050 11400 6050
Wire Wire Line
	10950 6150 11000 6150
Wire Wire Line
	11000 6150 11000 5950
Wire Wire Line
	11000 5950 10950 5950
Wire Wire Line
	11000 5950 11000 5800
Connection ~ 11000 5950
$Comp
L power:+5V #PWR055
U 1 1 5E8D9F42
P 11000 5800
F 0 "#PWR055" H 11000 5650 50  0001 C CNN
F 1 "+5V" H 11015 5973 50  0000 C CNN
F 2 "" H 11000 5800 50  0001 C CNN
F 3 "" H 11000 5800 50  0001 C CNN
	1    11000 5800
	1    0    0    -1  
$EndComp
Wire Wire Line
	10950 6950 11000 6950
Wire Wire Line
	11000 6950 11000 6350
Wire Wire Line
	11000 6250 10950 6250
Wire Wire Line
	10950 6350 11000 6350
Connection ~ 11000 6350
Wire Wire Line
	11000 6350 11000 6250
Wire Wire Line
	11000 6950 11000 7050
Connection ~ 11000 6950
$Comp
L power:GND #PWR056
U 1 1 5E90F079
P 11000 7050
F 0 "#PWR056" H 11000 6800 50  0001 C CNN
F 1 "GND" H 11005 6877 50  0000 C CNN
F 2 "" H 11000 7050 50  0001 C CNN
F 3 "" H 11000 7050 50  0001 C CNN
	1    11000 7050
	1    0    0    -1  
$EndComp
Entry Wire Line
	11400 6850 11500 6950
Entry Wire Line
	11400 6750 11500 6850
Entry Wire Line
	11400 6650 11500 6750
Entry Wire Line
	11400 6550 11500 6650
Wire Bus Line
	11500 6950 11600 6950
Wire Wire Line
	11400 6850 10950 6850
Wire Wire Line
	11400 6750 10950 6750
Wire Wire Line
	11400 6650 10950 6650
Wire Wire Line
	11400 6550 10950 6550
Text Label 11100 6850 0    50   ~ 0
addr3
Text Label 11100 6750 0    50   ~ 0
addr4
Text Label 11100 6650 0    50   ~ 0
addr5
Text Label 11100 6550 0    50   ~ 0
addr6
Wire Wire Line
	9600 700  9800 700 
Wire Wire Line
	10150 700  10150 800 
Connection ~ 9800 700 
Wire Wire Line
	9800 700  10150 700 
$Comp
L power:GND #PWR052
U 1 1 5C94A30F
P 10150 800
F 0 "#PWR052" H 10150 550 50  0001 C CNN
F 1 "GND" H 10155 627 50  0000 C CNN
F 2 "" H 10150 800 50  0001 C CNN
F 3 "" H 10150 800 50  0001 C CNN
	1    10150 800 
	1    0    0    -1  
$EndComp
Wire Wire Line
	9450 700  9350 700 
Text GLabel 9350 700  0    50   Input ~ 0
PHI1
Text GLabel 8700 850  0    50   Input ~ 0
sum[0..7]
Entry Wire Line
	8800 1350 8900 1450
Entry Wire Line
	8800 1150 8900 1250
Entry Wire Line
	8800 1450 8900 1550
Entry Wire Line
	8800 1250 8900 1350
Text Label 8950 1250 0    50   ~ 0
sum3
Text Label 8950 1350 0    50   ~ 0
sum4
Text Label 8950 1450 0    50   ~ 0
sum5
Text Label 8950 1550 0    50   ~ 0
sum6
Wire Bus Line
	8700 850  8800 850 
NoConn ~ 9950 2300
Wire Wire Line
	9600 800  9600 700 
Wire Wire Line
	9150 1250 8900 1250
Wire Wire Line
	8900 1350 9150 1350
Wire Wire Line
	9150 1450 8900 1450
Wire Wire Line
	8900 1550 9150 1550
Wire Wire Line
	9450 800  9450 700 
NoConn ~ 9950 800 
Wire Wire Line
	9800 800  9800 700 
Text Label 8950 2750 0    50   ~ 0
sum2
Text Label 8950 2850 0    50   ~ 0
sum1
Text Label 8950 2950 0    50   ~ 0
sum0
Wire Wire Line
	9150 2950 8900 2950
Wire Wire Line
	9150 2750 8900 2750
Wire Wire Line
	8900 2850 9150 2850
Entry Wire Line
	8800 2750 8900 2850
Entry Wire Line
	8800 2850 8900 2950
Entry Wire Line
	8800 2650 8900 2750
Wire Wire Line
	8950 3050 9150 3050
Text GLabel 8950 3050 0    50   Input ~ 0
SIGN
Wire Wire Line
	8250 2050 9550 2050
Connection ~ 8250 2050
Wire Wire Line
	8250 3550 9550 3550
Wire Wire Line
	8250 2050 8250 3550
Wire Wire Line
	9550 3550 9550 3500
Wire Wire Line
	9850 3550 9850 3500
Wire Wire Line
	9950 3550 9850 3550
Text GLabel 9950 3550 2    50   Input ~ 0
~S6
Text GLabel 9350 2200 0    50   Input ~ 0
PHI1
Wire Wire Line
	9450 2200 9350 2200
Wire Wire Line
	9450 2300 9450 2200
$Comp
L power:GND #PWR053
U 1 1 5C9D748E
P 10150 2300
F 0 "#PWR053" H 10150 2050 50  0001 C CNN
F 1 "GND" H 10155 2127 50  0000 C CNN
F 2 "" H 10150 2300 50  0001 C CNN
F 3 "" H 10150 2300 50  0001 C CNN
	1    10150 2300
	1    0    0    -1  
$EndComp
Wire Wire Line
	9800 2200 10150 2200
Connection ~ 9800 2200
Wire Wire Line
	9800 2300 9800 2200
Wire Wire Line
	10150 2200 10150 2300
Wire Wire Line
	9600 2200 9800 2200
Wire Wire Line
	9600 2300 9600 2200
$Comp
L music5xxx-custom:74LS163 IC20
U 1 1 5C9D7482
P 9700 2900
F 0 "IC20" H 10250 3500 50  0000 C CNN
F 1 "74HCT163" H 10250 3350 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 9700 2900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS163" H 9700 2900 50  0001 C CNN
	1    9700 2900
	1    0    0    1   
$EndComp
Wire Wire Line
	9550 2050 9550 2000
Wire Wire Line
	8200 2050 8250 2050
Wire Wire Line
	9850 2050 9850 2000
Wire Wire Line
	9950 2050 9850 2050
Text GLabel 9950 2050 2    50   Input ~ 0
~S6
$Comp
L music5xxx-custom:74LS163 IC19
U 1 1 5C912D19
P 9700 1400
F 0 "IC19" H 10250 2000 50  0000 C CNN
F 1 "74HCT163" H 10250 1850 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 9700 1400 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS163" H 9700 1400 50  0001 C CNN
	1    9700 1400
	1    0    0    1   
$EndComp
Wire Wire Line
	7550 1950 7600 1950
Text GLabel 7550 1950 0    50   Input ~ 0
~GATE
Wire Wire Line
	7550 2150 7600 2150
Text GLabel 7550 2150 0    50   Input ~ 0
~S0
$Comp
L 74xx:74LS32 IC4
U 3 1 5C8EC070
P 7900 2050
F 0 "IC4" H 7900 2375 50  0000 C CNN
F 1 "74HCT32" H 7900 2284 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 7900 2050 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 7900 2050 50  0001 C CNN
	3    7900 2050
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS86 IC15
U 3 1 5CCCED49
P 11000 3700
F 0 "IC15" H 11000 4025 50  0000 C CNN
F 1 "74HCT86" H 11000 3934 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 11000 3700 50  0001 C CNN
F 3 "74xx/74ls86.pdf" H 11000 3700 50  0001 C CNN
	3    11000 3700
	1    0    0    -1  
$EndComp
Wire Wire Line
	10250 3050 10550 3050
Wire Wire Line
	10550 3050 10550 3600
Wire Wire Line
	10550 3600 10700 3600
Wire Wire Line
	10700 3800 10300 3800
Text GLabel 10300 3800 0    50   Input ~ 0
INVERT
NoConn ~ 9950 4100
Text Label 8900 4550 0    50   ~ 0
data3
Text Label 8900 4650 0    50   ~ 0
data2
Text Label 8900 4750 0    50   ~ 0
data1
Wire Wire Line
	9150 4750 8900 4750
Wire Wire Line
	9150 4550 8900 4550
Wire Wire Line
	8900 4650 9150 4650
Entry Wire Line
	8800 4550 8900 4650
Entry Wire Line
	8800 4650 8900 4750
Entry Wire Line
	8800 4450 8900 4550
Wire Wire Line
	8900 4850 9150 4850
Wire Wire Line
	8250 5350 9550 5350
Wire Wire Line
	9550 5350 9550 5300
Wire Wire Line
	9850 5350 9850 5300
Wire Wire Line
	9950 5350 9850 5350
Text GLabel 9950 5350 2    50   Input ~ 0
~S6
Text GLabel 9350 4000 0    50   Input ~ 0
PHI1
Wire Wire Line
	9450 4000 9350 4000
Wire Wire Line
	9450 4100 9450 4000
Wire Wire Line
	9800 4000 10300 4000
Wire Wire Line
	9800 4100 9800 4000
Wire Wire Line
	9600 4100 9600 4000
$Comp
L music5xxx-custom:74LS163 IC24
U 1 1 5CD0112A
P 9700 4700
F 0 "IC24" H 10250 5300 50  0000 C CNN
F 1 "74HCT163" H 10250 5150 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 9700 4700 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS163" H 9700 4700 50  0001 C CNN
	1    9700 4700
	1    0    0    1   
$EndComp
$Comp
L power:+5V #PWR051
U 1 1 5CD19C56
P 9600 4000
F 0 "#PWR051" H 9600 3850 50  0001 C CNN
F 1 "+5V" H 9615 4173 50  0000 C CNN
F 2 "" H 9600 4000 50  0001 C CNN
F 3 "" H 9600 4000 50  0001 C CNN
	1    9600 4000
	1    0    0    -1  
$EndComp
Wire Wire Line
	10250 4550 10300 4550
Wire Wire Line
	10300 4000 10300 4550
Wire Wire Line
	8250 3550 8250 5350
Connection ~ 8250 3550
Entry Wire Line
	8800 4750 8900 4850
Text Label 8900 4850 0    50   ~ 0
data0
Wire Bus Line
	8800 4300 8750 4300
Text GLabel 8750 4300 0    50   Input ~ 0
data[0..7]
$Comp
L Device:R R9
U 1 1 5CDCF24C
P 13600 3700
F 0 "R9" H 13500 3750 50  0000 C CNN
F 1 "1K" H 13500 3650 50  0000 C CNN
F 2 "musix5xxx-custom:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal" V 13530 3700 50  0001 C CNN
F 3 "~" H 13600 3700 50  0001 C CNN
	1    13600 3700
	-1   0    0    1   
$EndComp
$Comp
L Device:R R11
U 1 1 5CDCFB6A
P 13100 3700
F 0 "R11" H 12950 3750 50  0000 C CNN
F 1 "15K" H 12950 3650 50  0000 C CNN
F 2 "musix5xxx-custom:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal" V 13030 3700 50  0001 C CNN
F 3 "~" H 13100 3700 50  0001 C CNN
	1    13100 3700
	-1   0    0    1   
$EndComp
$Comp
L Device:R R12
U 1 1 5CDD00AC
P 12700 3700
F 0 "R12" H 12600 3850 50  0000 C CNN
F 1 "15K" H 12600 3950 50  0000 C CNN
F 2 "musix5xxx-custom:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal" V 12630 3700 50  0001 C CNN
F 3 "~" H 12700 3700 50  0001 C CNN
	1    12700 3700
	-1   0    0    1   
$EndComp
$Comp
L Device:R R10
U 1 1 5CDD0544
P 13350 3950
F 0 "R10" V 13150 3950 50  0000 C CNN
F 1 "15K" V 13250 3950 50  0000 C CNN
F 2 "musix5xxx-custom:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal" V 13280 3950 50  0001 C CNN
F 3 "~" H 13350 3950 50  0001 C CNN
	1    13350 3950
	0    -1   -1   0   
$EndComp
$Comp
L Device:C C12
U 1 1 5CDD1462
P 13100 4250
F 0 "C12" H 12900 4350 50  0000 C CNN
F 1 "22nF" H 12900 4250 50  0000 C CNN
F 2 "musix5xxx-custom:C_Rect_L7.0mm_W2.5mm_P5.00mm" H 13138 4100 50  0001 C CNN
F 3 "~" H 13100 4250 50  0001 C CNN
	1    13100 4250
	-1   0    0    1   
$EndComp
Wire Wire Line
	13100 4100 13100 3950
Wire Wire Line
	13100 3950 13200 3950
Connection ~ 13100 3950
Wire Wire Line
	13100 3950 13100 3850
Wire Wire Line
	13500 3950 13600 3950
Wire Wire Line
	13600 3950 13600 3850
Wire Wire Line
	12700 3850 12700 4450
Wire Wire Line
	12700 4450 12900 4450
Wire Wire Line
	13100 4450 13100 4400
$Comp
L Device:D_Zener_ALT ZD1
U 1 1 5CE3E9FD
P 13600 4200
F 0 "ZD1" V 13554 4279 50  0000 L CNN
F 1 "12V" V 13645 4279 50  0000 L CNN
F 2 "musix5xxx-custom:D_DO-35_SOD27_P10.16mm_Horizontal" H 13600 4200 50  0001 C CNN
F 3 "~" H 13600 4200 50  0001 C CNN
	1    13600 4200
	0    1    1    0   
$EndComp
Wire Wire Line
	13600 4050 13600 3950
Connection ~ 13600 3950
Wire Wire Line
	13600 4450 13100 4450
Wire Wire Line
	13600 4350 13600 4450
Connection ~ 13100 4450
Wire Wire Line
	13600 3500 13600 3550
$Comp
L power:GND #PWR062
U 1 1 5CE91D08
P 12700 4550
F 0 "#PWR062" H 12700 4300 50  0001 C CNN
F 1 "GND" H 12705 4377 50  0000 C CNN
F 2 "" H 12700 4550 50  0001 C CNN
F 3 "" H 12700 4550 50  0001 C CNN
	1    12700 4550
	1    0    0    -1  
$EndComp
$Comp
L power:+15V #PWR064
U 1 1 5CECBC1B
P 13600 3500
F 0 "#PWR064" H 13600 3350 50  0001 C CNN
F 1 "+15V" H 13615 3673 50  0000 C CNN
F 2 "" H 13600 3500 50  0001 C CNN
F 3 "" H 13600 3500 50  0001 C CNN
	1    13600 3500
	1    0    0    -1  
$EndComp
$Comp
L power:-15V #PWR063
U 1 1 5CECCE72
P 13100 1700
F 0 "#PWR063" H 13100 1800 50  0001 C CNN
F 1 "-15V" H 13115 1873 50  0000 C CNN
F 2 "" H 13100 1700 50  0001 C CNN
F 3 "" H 13100 1700 50  0001 C CNN
	1    13100 1700
	-1   0    0    1   
$EndComp
Wire Wire Line
	12900 4450 12900 3450
Connection ~ 12900 4450
Wire Wire Line
	12900 4450 13100 4450
Text GLabel 13550 2350 2    50   Output ~ 0
L-
Text GLabel 13550 2500 2    50   Output ~ 0
L+
Text GLabel 13550 2800 2    50   Output ~ 0
R-
Text GLabel 13550 2950 2    50   Output ~ 0
R+
$Comp
L music5xxx-custom:AM6070D IC36
U 1 1 5CF09E26
P 12700 2650
F 0 "IC36" H 12650 2850 50  0000 C CNN
F 1 "AM6070D" H 12650 2650 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-18_W7.62mm_LongPads" H 12700 2650 50  0001 C CNN
F 3 "" H 12700 2650 50  0001 C CNN
	1    12700 2650
	1    0    0    -1  
$EndComp
Wire Wire Line
	13550 2350 13400 2350
Wire Wire Line
	13400 2500 13550 2500
Wire Wire Line
	13550 2950 13400 2950
Wire Wire Line
	13400 2800 13550 2800
Wire Wire Line
	13100 1700 13100 1650
Wire Wire Line
	13100 1650 12900 1650
Wire Wire Line
	12900 1650 12900 1850
$Comp
L power:+15V #PWR057
U 1 1 5C9FDD5E
P 12500 1750
F 0 "#PWR057" H 12500 1600 50  0001 C CNN
F 1 "+15V" H 12515 1923 50  0000 C CNN
F 2 "" H 12500 1750 50  0001 C CNN
F 3 "" H 12500 1750 50  0001 C CNN
	1    12500 1750
	1    0    0    -1  
$EndComp
Wire Wire Line
	12500 1750 12500 1850
Wire Wire Line
	11300 3700 12300 3700
Wire Wire Line
	12300 3700 12300 3450
Wire Wire Line
	10300 4000 12500 4000
Wire Wire Line
	12500 4000 12500 3450
Connection ~ 10300 4000
Wire Wire Line
	12700 3550 12700 3450
Wire Wire Line
	13100 3550 13100 3450
Wire Wire Line
	12700 4450 12700 4550
Connection ~ 12700 4450
NoConn ~ 10250 4850
NoConn ~ 10250 4750
NoConn ~ 10250 4650
Wire Wire Line
	10250 2950 12000 2950
Wire Wire Line
	10250 2850 12000 2850
Wire Wire Line
	10250 2750 12000 2750
Wire Wire Line
	10250 1550 10850 1550
Wire Wire Line
	10850 1550 10850 2350
Wire Wire Line
	10250 1450 11050 1450
Wire Wire Line
	11050 1450 11050 2450
Wire Wire Line
	10250 1350 11250 1350
Wire Wire Line
	10250 1250 11450 1250
Wire Wire Line
	11450 2650 12000 2650
Wire Wire Line
	11450 1250 11450 2650
Wire Wire Line
	12000 2550 11250 2550
Wire Wire Line
	11250 1350 11250 2550
Wire Wire Line
	12000 2450 11050 2450
Wire Wire Line
	12000 2350 10850 2350
Wire Wire Line
	4850 7950 4850 7800
Wire Wire Line
	5000 7950 5000 7800
Wire Wire Line
	5150 7950 5150 7800
Wire Wire Line
	5300 7950 5300 7800
Wire Wire Line
	3650 7950 3650 7800
Wire Wire Line
	3500 7950 3500 7800
Wire Wire Line
	3200 7950 3200 7800
Wire Bus Line
	5400 10450 5400 8050
Entry Wire Line
	4850 7950 4950 8050
Entry Wire Line
	5000 7950 5100 8050
Entry Wire Line
	5150 7950 5250 8050
Entry Wire Line
	5300 7950 5400 8050
Entry Wire Line
	3650 7950 3750 8050
Entry Wire Line
	3500 7950 3600 8050
Entry Wire Line
	3200 7950 3300 8050
Wire Wire Line
	3350 7950 3350 7800
Entry Wire Line
	3350 7950 3450 8050
Wire Wire Line
	2450 11000 3850 11000
$Comp
L 74xx:74LS32 IC4
U 4 1 5E0A9395
P 2950 10600
F 0 "IC4" H 2950 10925 50  0000 C CNN
F 1 "74HCT32" H 2950 10834 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 2950 10600 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 2950 10600 50  0001 C CNN
	4    2950 10600
	1    0    0    -1  
$EndComp
Text GLabel 2450 10500 0    50   Input ~ 0
~S1
Wire Wire Line
	2450 10500 2650 10500
Text GLabel 2450 10700 0    50   Input ~ 0
PHI1
Wire Wire Line
	2450 10700 2650 10700
Text GLabel 2450 11000 0    50   Input ~ 0
~S7
Wire Wire Line
	3400 10500 3400 10300
Entry Wire Line
	3400 10300 3300 10200
Wire Wire Line
	2900 8700 2900 8850
Text Label 3000 7950 1    50   ~ 0
sp0
Text Label 2850 7950 1    50   ~ 0
sp1
Text Label 2700 7950 1    50   ~ 0
sp2
Text Label 2550 7950 1    50   ~ 0
sp3
Text Label 3200 8850 1    50   ~ 0
sp3
Text Label 2900 8850 1    50   ~ 0
sp2
Text Label 3100 8850 1    50   ~ 0
sp1
Text Label 3000 8850 1    50   ~ 0
sp0
Wire Wire Line
	2950 8700 2950 8500
Wire Wire Line
	2950 8500 2850 8500
Entry Wire Line
	3000 8200 3100 8300
Entry Wire Line
	2850 8200 2950 8300
Entry Wire Line
	2700 8200 2800 8300
Entry Wire Line
	2550 8200 2650 8300
Wire Wire Line
	3000 8850 3000 8400
Entry Wire Line
	3100 8300 3200 8400
Entry Wire Line
	3000 8300 3100 8400
Entry Wire Line
	2900 8300 3000 8400
Entry Wire Line
	2750 8300 2850 8400
Wire Wire Line
	2850 8500 2850 8400
Wire Wire Line
	3000 8200 3000 7800
$Comp
L 74xx:74LS393 IC9
U 3 1 5ED2F9C9
P 14300 7350
F 0 "IC9" H 14300 6700 50  0000 C CNN
F 1 "74HCT393" H 14300 6600 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 14300 7350 50  0001 C CNN
F 3 "74xx\\74LS393.pdf" H 14300 7350 50  0001 C CNN
	3    14300 7350
	-1   0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS374 IC16
U 2 1 5ED73272
P 13400 8900
F 0 "IC16" H 13500 8250 50  0000 R CNN
F 1 "74HCT374" H 13550 8150 50  0000 R CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 13400 8900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS374" H 13400 8900 50  0001 C CNN
	2    13400 8900
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS08 IC8
U 5 1 5ED955F0
P 13850 7350
F 0 "IC8" H 13750 6700 50  0000 L CNN
F 1 "74HCT08" H 13700 6600 50  0000 L CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 13850 7350 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 13850 7350 50  0001 C CNN
	5    13850 7350
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS08 IC7
U 5 1 5EDFCD37
P 13400 7350
F 0 "IC7" H 13300 6700 50  0000 L CNN
F 1 "74HCT08" H 13250 6600 50  0000 L CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 13400 7350 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 13400 7350 50  0001 C CNN
	5    13400 7350
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS283 IC11
U 2 1 5EE22D00
P 15200 7350
F 0 "IC11" H 15200 6700 50  0000 C CNN
F 1 "74HCT283" H 15200 6600 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 15200 7350 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS283" H 15200 7350 50  0001 C CNN
	2    15200 7350
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS244 IC17
U 2 1 5EE6B5E5
P 13850 8900
F 0 "IC17" H 13950 8250 50  0000 R CNN
F 1 "74HCT244" H 14000 8150 50  0000 R CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 13850 8900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS244" H 13850 8900 50  0001 C CNN
	2    13850 8900
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS374 IC12
U 2 1 5EED1734
P 15650 7350
F 0 "IC12" H 15750 6700 50  0000 R CNN
F 1 "74HCT374" H 15800 6600 50  0000 R CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 15650 7350 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS374" H 15650 7350 50  0001 C CNN
	2    15650 7350
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:AM9128-10 IC?
U 2 1 5EF59022
P 15200 8900
AR Path="/5C8C3E7F/5EF59022" Ref="IC?"  Part="1" 
AR Path="/5DDDA239/5EF59022" Ref="IC22"  Part="2" 
F 0 "IC22" H 15200 8250 50  0000 C CNN
F 1 "AM9128-10" H 15200 8150 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-24_W15.24mm_LongPads" H 15200 8900 50  0001 C CNN
F 3 "" H 15200 8900 50  0001 C CNN
	2    15200 8900
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS163 IC19
U 2 1 5EF5BD29
P 14300 8900
F 0 "IC19" H 14300 8250 50  0000 C CNN
F 1 "74HCT163" H 14300 8150 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 14300 8900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS163" H 14300 8900 50  0001 C CNN
	2    14300 8900
	-1   0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS163 IC20
U 2 1 5EF5FF20
P 14750 8900
F 0 "IC20" H 14750 8250 50  0000 C CNN
F 1 "74HCT163" H 14750 8150 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 14750 8900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS163" H 14750 8900 50  0001 C CNN
	2    14750 8900
	-1   0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS163 IC24
U 2 1 5EFE623F
P 15650 8900
F 0 "IC24" H 15650 8250 50  0000 C CNN
F 1 "74HCT163" H 15650 8150 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 15650 8900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS163" H 15650 8900 50  0001 C CNN
	2    15650 8900
	-1   0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS138 IC10
U 2 1 5F050384
P 14750 7350
F 0 "IC10" H 14750 6700 50  0000 C CNN
F 1 "74HCT138" H 14750 6600 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 14750 7350 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS138" H 14750 7350 50  0001 C CNN
	2    14750 7350
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS374 IC13
U 2 1 5F098976
P 12950 8900
F 0 "IC13" H 12950 8250 50  0000 C CNN
F 1 "74HCT374" H 12950 8150 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 12950 8900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS374" H 12950 8900 50  0001 C CNN
	2    12950 8900
	-1   0    0    -1  
$EndComp
$Comp
L power:+5V #PWR058
U 1 1 5F126D68
P 12550 6850
F 0 "#PWR058" H 12550 6700 50  0001 C CNN
F 1 "+5V" H 12565 7023 50  0000 C CNN
F 2 "" H 12550 6850 50  0001 C CNN
F 3 "" H 12550 6850 50  0001 C CNN
	1    12550 6850
	1    0    0    -1  
$EndComp
Wire Wire Line
	12550 6850 12950 6850
Connection ~ 12950 6850
Wire Wire Line
	12950 6850 13400 6850
Connection ~ 13400 6850
Wire Wire Line
	13400 6850 13850 6850
Connection ~ 13850 6850
Wire Wire Line
	13850 6850 14300 6850
Connection ~ 14300 6850
Wire Wire Line
	14300 6850 14750 6850
Connection ~ 14750 6850
Wire Wire Line
	14750 6850 15200 6850
Connection ~ 15200 6850
Wire Wire Line
	15200 6850 15650 6850
Wire Wire Line
	12550 8400 12950 8400
Connection ~ 12950 8400
Wire Wire Line
	12950 8400 13400 8400
Connection ~ 13400 8400
Wire Wire Line
	13400 8400 13850 8400
Connection ~ 13850 8400
Wire Wire Line
	13850 8400 14300 8400
Connection ~ 14300 8400
Wire Wire Line
	14300 8400 14750 8400
Connection ~ 14750 8400
Wire Wire Line
	14750 8400 15200 8400
Connection ~ 15200 8400
Wire Wire Line
	15200 8400 15650 8400
Wire Wire Line
	15650 9400 15200 9400
Wire Wire Line
	12550 7850 12550 7900
Connection ~ 12950 7850
Wire Wire Line
	12950 7850 12550 7850
Connection ~ 13400 7850
Wire Wire Line
	13400 7850 12950 7850
Connection ~ 13850 7850
Wire Wire Line
	13850 7850 13400 7850
Connection ~ 14300 7850
Wire Wire Line
	14300 7850 13850 7850
Wire Wire Line
	14750 7850 14300 7850
Connection ~ 12950 9400
Wire Wire Line
	12950 9400 12550 9400
Connection ~ 13400 9400
Wire Wire Line
	13400 9400 12950 9400
Connection ~ 13850 9400
Wire Wire Line
	13850 9400 13400 9400
Connection ~ 14300 9400
Wire Wire Line
	14300 9400 13850 9400
Connection ~ 14750 9400
Wire Wire Line
	14750 9400 14300 9400
Connection ~ 15200 9400
Wire Wire Line
	15200 9400 14750 9400
$Comp
L power:GND #PWR059
U 1 1 5F1979DA
P 12550 7900
F 0 "#PWR059" H 12550 7650 50  0001 C CNN
F 1 "GND" H 12555 7727 50  0000 C CNN
F 2 "" H 12550 7900 50  0001 C CNN
F 3 "" H 12550 7900 50  0001 C CNN
	1    12550 7900
	1    0    0    -1  
$EndComp
$Comp
L power:+5V #PWR060
U 1 1 5F382A09
P 12550 8400
F 0 "#PWR060" H 12550 8250 50  0001 C CNN
F 1 "+5V" H 12565 8573 50  0000 C CNN
F 2 "" H 12550 8400 50  0001 C CNN
F 3 "" H 12550 8400 50  0001 C CNN
	1    12550 8400
	1    0    0    -1  
$EndComp
$Comp
L power:GND #PWR061
U 1 1 5F3A7F56
P 12550 9400
F 0 "#PWR061" H 12550 9150 50  0001 C CNN
F 1 "GND" H 12555 9227 50  0000 C CNN
F 2 "" H 12550 9400 50  0001 C CNN
F 3 "" H 12550 9400 50  0001 C CNN
	1    12550 9400
	1    0    0    -1  
$EndComp
Wire Wire Line
	9150 8500 9400 8500
Text GLabel 4400 2400 2    50   Output ~ 0
addr[0..6]
Wire Bus Line
	4350 2400 4400 2400
Wire Wire Line
	3950 1700 4250 1700
Wire Wire Line
	3950 1800 4250 1800
Wire Wire Line
	3950 1900 4250 1900
Wire Wire Line
	3950 2000 4250 2000
Wire Wire Line
	3950 2100 4250 2100
Wire Wire Line
	3950 2200 4250 2200
Entry Wire Line
	4250 2200 4350 2300
Entry Wire Line
	4250 2100 4350 2200
Entry Wire Line
	4250 2000 4350 2100
Entry Wire Line
	4250 1900 4350 2000
Entry Wire Line
	4250 1800 4350 1900
Entry Wire Line
	4250 1700 4350 1800
Text Label 4000 2200 0    50   ~ 0
addr1
Text Label 4000 1900 0    50   ~ 0
addr2
Text Label 4000 1800 0    50   ~ 0
addr3
Text Label 4000 2000 0    50   ~ 0
addr4
Text Label 4000 1600 0    50   ~ 0
addr5
Text Label 4000 2100 0    50   ~ 0
addr6
Wire Wire Line
	3950 1600 4250 1600
Text Label 4000 1700 0    50   ~ 0
addr0
Entry Wire Line
	4250 1600 4350 1700
Wire Wire Line
	4450 1500 3950 1500
Text GLabel 4450 1500 2    50   Output ~ 0
PA1
Text Label 3300 10050 1    50   ~ 0
data3
Wire Wire Line
	14750 7850 15200 7850
Connection ~ 14750 7850
Wire Wire Line
	15200 7850 15650 7850
Connection ~ 15200 7850
$Comp
L power:GND #PWR0102
U 1 1 5CEAEE67
P 3500 2550
F 0 "#PWR0102" H 3500 2300 50  0001 C CNN
F 1 "GND" H 3505 2377 50  0000 C CNN
F 2 "" H 3500 2550 50  0001 C CNN
F 3 "" H 3500 2550 50  0001 C CNN
	1    3500 2550
	1    0    0    -1  
$EndComp
$Comp
L Device:C C?
U 1 1 5DB3BED0
P 6850 1650
AR Path="/5C8C3E7F/5DB3BED0" Ref="C?"  Part="1" 
AR Path="/5DDDA239/5DB3BED0" Ref="C45"  Part="1" 
F 0 "C45" H 7150 1700 50  0000 L CNN
F 1 "220nF" H 7150 1600 50  0000 L CNN
F 2 "musix5xxx-custom:C_Rect_L7.0mm_W2.5mm_P5.00mm" H 6888 1500 50  0001 C CNN
F 3 "~" H 6850 1650 50  0001 C CNN
	1    6850 1650
	1    0    0    -1  
$EndComp
$Comp
L power:GND #PWR?
U 1 1 5DB3BED6
P 6850 1800
AR Path="/5C8C3E7F/5DB3BED6" Ref="#PWR?"  Part="1" 
AR Path="/5DDDA239/5DB3BED6" Ref="#PWR072"  Part="1" 
F 0 "#PWR072" H 6850 1550 50  0001 C CNN
F 1 "GND" H 6855 1627 50  0000 C CNN
F 2 "" H 6850 1800 50  0001 C CNN
F 3 "" H 6850 1800 50  0001 C CNN
	1    6850 1800
	1    0    0    -1  
$EndComp
Wire Wire Line
	6850 1500 6850 1450
Connection ~ 6850 1450
Wire Wire Line
	6850 1450 7000 1450
Wire Bus Line
	11500 6650 11500 6950
Wire Bus Line
	8800 4300 8800 4750
Wire Bus Line
	2800 8600 4550 8600
Wire Bus Line
	8800 850  8800 2850
Wire Bus Line
	3300 8050 5400 8050
Wire Bus Line
	2650 8300 3100 8300
Wire Bus Line
	4350 1700 4350 2400
Wire Bus Line
	9550 6350 9550 7650
Wire Bus Line
	7550 7700 8350 7700
Wire Bus Line
	7550 7600 8350 7600
Wire Bus Line
	5400 10450 8950 10450
Wire Bus Line
	1950 10200 3300 10200
Wire Bus Line
	1050 5950 8050 5950
$Comp
L music5xxx-custom:74LS283 IC6
U 2 1 5EE47B41
P 12950 7350
F 0 "IC6" H 12950 6700 50  0000 C CNN
F 1 "74HCT283" H 12950 6600 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 12950 7350 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS283" H 12950 7350 50  0001 C CNN
	2    12950 7350
	1    0    0    -1  
$EndComp
$EndSCHEMATC
