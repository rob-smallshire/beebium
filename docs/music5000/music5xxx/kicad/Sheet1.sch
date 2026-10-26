EESchema Schematic File Version 4
LIBS:music5xxx-cache
EELAYER 29 0
EELAYER END
$Descr A3 16535 11693
encoding utf-8
Sheet 2 3
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
L Connector_Generic:Conn_01x34 J11
U 1 1 5C8C3F3C
P 650 2450
F 0 "J11" H 600 4300 50  0000 C CNN
F 1 "1MHzBus1" H 550 4200 50  0000 C CNN
F 2 "musix5xxx-custom:IDC-Header_2x17_Pitch2.54mm_Straight" H 650 2450 50  0001 C CNN
F 3 "~" H 650 2450 50  0001 C CNN
	1    650  2450
	-1   0    0    -1  
$EndComp
$Comp
L Connector_Generic:Conn_01x34 J12
U 1 1 5C8C4004
P 1450 2450
F 0 "J12" H 1400 4300 50  0000 L CNN
F 1 "1MHzBus2" H 1250 4200 50  0000 L CNN
F 2 "musix5xxx-custom:IDC-Header_2x17_Pitch2.54mm_Angled" H 1450 2450 50  0001 C CNN
F 3 "~" H 1450 2450 50  0001 C CNN
	1    1450 2450
	1    0    0    -1  
$EndComp
Wire Wire Line
	850  850  1250 850 
Wire Wire Line
	850  950  1250 950 
Wire Wire Line
	850  1050 1250 1050
Wire Wire Line
	850  1150 1250 1150
Wire Wire Line
	850  1250 1250 1250
Wire Wire Line
	850  1350 1250 1350
Wire Wire Line
	850  1450 1250 1450
Wire Wire Line
	850  1550 1250 1550
Wire Wire Line
	850  1650 1250 1650
Wire Wire Line
	850  1750 1250 1750
Wire Wire Line
	850  1850 1250 1850
Wire Wire Line
	850  1950 1250 1950
Wire Wire Line
	850  2050 1250 2050
Wire Wire Line
	850  2150 1250 2150
Wire Wire Line
	850  2250 1250 2250
Wire Wire Line
	850  2350 1250 2350
Wire Wire Line
	850  2450 1250 2450
Wire Wire Line
	850  2550 1250 2550
Wire Wire Line
	850  2650 1250 2650
Wire Wire Line
	850  2750 1250 2750
Wire Wire Line
	850  2850 1250 2850
Wire Wire Line
	850  2950 1250 2950
Wire Wire Line
	850  3050 1250 3050
Wire Wire Line
	850  3150 1250 3150
Wire Wire Line
	850  3250 1250 3250
Wire Wire Line
	850  3350 1250 3350
Wire Wire Line
	850  3450 1250 3450
Wire Wire Line
	850  3550 1250 3550
Wire Wire Line
	850  3650 1250 3650
Wire Wire Line
	850  3750 1250 3750
Wire Wire Line
	850  3850 1250 3850
Wire Wire Line
	850  3950 1250 3950
Wire Wire Line
	850  4050 1250 4050
Wire Wire Line
	850  4150 1250 4150
Text Label 950  4150 0    50   ~ 0
a7
Text Label 950  4050 0    50   ~ 0
a6
Text Label 950  3950 0    50   ~ 0
a5
Text Label 950  3850 0    50   ~ 0
a4
Text Label 950  3750 0    50   ~ 0
a3
Text Label 950  3650 0    50   ~ 0
a2
Text Label 950  3550 0    50   ~ 0
a1
Text Label 950  3450 0    50   ~ 0
a0
Text Label 950  3250 0    50   ~ 0
d7
Text Label 950  3150 0    50   ~ 0
d6
Text Label 950  3050 0    50   ~ 0
d5
Text Label 950  2950 0    50   ~ 0
d4
Text Label 950  2850 0    50   ~ 0
d3
Text Label 950  2750 0    50   ~ 0
d2
Text Label 950  2650 0    50   ~ 0
d1
Text Label 950  2550 0    50   ~ 0
d0
Text Label 950  1750 0    50   ~ 0
~PGFC
Text Label 950  2150 0    50   ~ 0
~RST
Text Label 950  1150 0    50   ~ 0
1MHZE
Text Label 950  1950 0    50   ~ 0
~PGFD
Text Label 950  950  0    50   ~ 0
R~W
$Comp
L power:+5V #PWR027
U 1 1 5C8C6CBC
P 3600 1600
F 0 "#PWR027" H 3600 1450 50  0001 C CNN
F 1 "+5V" H 3615 1773 50  0000 C CNN
F 2 "" H 3600 1600 50  0001 C CNN
F 3 "" H 3600 1600 50  0001 C CNN
	1    3600 1600
	1    0    0    -1  
$EndComp
Wire Wire Line
	3600 1600 3600 1650
Wire Wire Line
	3600 1650 3950 1650
$Comp
L 74xx:74LS74 IC34
U 1 1 5C8C76AC
P 4950 2150
F 0 "IC34" H 5200 2550 50  0000 C CNN
F 1 "74HCT74" H 5200 2450 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 4950 2150 50  0001 C CNN
F 3 "74xx/74hc_hct74.pdf" H 4950 2150 50  0001 C CNN
	1    4950 2150
	1    0    0    -1  
$EndComp
Wire Wire Line
	4250 2050 4450 2050
Wire Wire Line
	4450 2050 4450 2150
Wire Wire Line
	4450 2150 4650 2150
Wire Wire Line
	4650 2050 4600 2050
Wire Wire Line
	4600 2050 4600 1650
Wire Wire Line
	3950 2450 3950 2600
Wire Wire Line
	3950 2600 4200 2600
$Comp
L power:+5V #PWR028
U 1 1 5C8CA14A
P 4200 2600
F 0 "#PWR028" H 4200 2450 50  0001 C CNN
F 1 "+5V" H 4215 2773 50  0000 C CNN
F 2 "" H 4200 2600 50  0001 C CNN
F 3 "" H 4200 2600 50  0001 C CNN
	1    4200 2600
	1    0    0    -1  
$EndComp
Wire Wire Line
	4250 2250 4400 2250
Text Label 2600 2450 0    50   ~ 0
1MHZE
Wire Wire Line
	3400 2450 3550 2450
Text GLabel 3550 2450 2    50   Output ~ 0
E
Wire Wire Line
	3950 1850 3950 1650
Connection ~ 3950 1650
Wire Wire Line
	3950 1650 4600 1650
Wire Wire Line
	4600 1650 4950 1650
Wire Wire Line
	4950 1650 4950 1850
Connection ~ 4600 1650
NoConn ~ 5250 2050
$Comp
L 74xx:74LS32 IC35
U 1 1 5C8CF69F
P 5900 2150
F 0 "IC35" H 5900 1833 50  0000 C CNN
F 1 "74HCT32" H 5900 1924 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 5900 2150 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 5900 2150 50  0001 C CNN
	1    5900 2150
	1    0    0    1   
$EndComp
$Comp
L 74xx:74LS32 IC35
U 4 1 5C8D0379
P 3100 2050
F 0 "IC35" H 3100 1733 50  0000 C CNN
F 1 "74HCT32" H 3100 1824 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 3100 2050 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 3100 2050 50  0001 C CNN
	4    3100 2050
	1    0    0    1   
$EndComp
Wire Wire Line
	3400 2050 3650 2050
Text Label 2600 1950 0    50   ~ 0
R~W
Wire Wire Line
	2600 1950 2800 1950
Text Label 2600 2150 0    50   ~ 0
~PGFD
Wire Wire Line
	2600 2150 2800 2150
Wire Wire Line
	4400 3050 4550 3050
Wire Wire Line
	4400 2250 4400 2900
Text GLabel 4550 3050 2    50   Output ~ 0
SEL
Wire Wire Line
	4400 2900 5250 2900
Connection ~ 4400 2900
Wire Wire Line
	4400 2900 4400 3050
Text GLabel 4800 2600 0    50   Input ~ 0
~PROG
Wire Wire Line
	4800 2600 4950 2600
Wire Wire Line
	4950 2600 4950 2450
Text GLabel 5550 2050 0    50   Input ~ 0
~S0
Wire Wire Line
	5600 2250 5250 2250
Wire Wire Line
	5550 2050 5600 2050
Text GLabel 6300 2150 2    50   Output ~ 0
~REQ
Wire Wire Line
	6300 2150 6200 2150
$Comp
L 74xx:74LS32 IC30
U 1 1 5C8DBFA1
P 5900 2900
F 0 "IC30" H 5900 3225 50  0000 C CNN
F 1 "74HCT32" H 5900 3134 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 5900 2900 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 5900 2900 50  0001 C CNN
	1    5900 2900
	-1   0    0    -1  
$EndComp
Wire Wire Line
	5600 2900 5350 2900
Wire Wire Line
	6200 2800 6300 2800
Wire Wire Line
	6200 3000 6300 3000
Text GLabel 6300 2800 2    50   Input ~ 0
PHI1
Text GLabel 6300 3000 2    50   Input ~ 0
~WR
Text Label 2600 2650 0    50   ~ 0
~RST
Wire Wire Line
	2600 2650 3400 2650
Wire Wire Line
	3400 2650 3400 2700
$Comp
L Device:R R1
U 1 1 5C8E5CB8
P 3400 2850
F 0 "R1" H 3470 2896 50  0000 L CNN
F 1 "1K" H 3470 2805 50  0000 L CNN
F 2 "musix5xxx-custom:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal" V 3330 2850 50  0001 C CNN
F 3 "~" H 3400 2850 50  0001 C CNN
	1    3400 2850
	1    0    0    -1  
$EndComp
$Comp
L Device:C C7
U 1 1 5C8E5D57
P 3400 3300
F 0 "C7" H 3515 3346 50  0000 L CNN
F 1 "22nF" H 3515 3255 50  0000 L CNN
F 2 "musix5xxx-custom:C_Rect_L7.0mm_W2.5mm_P5.00mm" H 3438 3150 50  0001 C CNN
F 3 "~" H 3400 3300 50  0001 C CNN
	1    3400 3300
	1    0    0    -1  
$EndComp
Wire Wire Line
	3400 3150 3400 3050
Wire Wire Line
	3400 3050 3650 3050
Text GLabel 3650 3050 2    50   Output ~ 0
~CRST
$Comp
L power:GND #PWR026
U 1 1 5C8E8BA0
P 3400 3500
F 0 "#PWR026" H 3400 3250 50  0001 C CNN
F 1 "GND" H 3405 3327 50  0000 C CNN
F 2 "" H 3400 3500 50  0001 C CNN
F 3 "" H 3400 3500 50  0001 C CNN
	1    3400 3500
	1    0    0    -1  
$EndComp
Wire Wire Line
	3400 3450 3400 3500
Wire Wire Line
	3400 2150 3650 2150
Wire Wire Line
	3400 2150 3400 2450
Connection ~ 3400 2450
Wire Wire Line
	2600 2450 2950 2450
$Comp
L 74xx:74LS74 IC34
U 2 1 5C8C7719
P 3950 2150
F 0 "IC34" H 4200 2550 50  0000 C CNN
F 1 "74HCT74" H 4200 2450 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 3950 2150 50  0001 C CNN
F 3 "74xx/74hc_hct74.pdf" H 3950 2150 50  0001 C CNN
	2    3950 2150
	1    0    0    -1  
$EndComp
Wire Wire Line
	2000 3550 2150 3550
Wire Wire Line
	2000 3650 2150 3650
Wire Wire Line
	2000 3750 2150 3750
Wire Wire Line
	2000 3850 2150 3850
Wire Wire Line
	2000 3950 2150 3950
Wire Wire Line
	2000 4050 2150 4050
Wire Wire Line
	2000 4150 2150 4150
Wire Bus Line
	2250 4250 2450 4250
Wire Wire Line
	2000 3450 2150 3450
Entry Wire Line
	2150 3450 2250 3550
Entry Wire Line
	2150 3550 2250 3650
Entry Wire Line
	2150 3650 2250 3750
Entry Wire Line
	2150 3750 2250 3850
Entry Wire Line
	2150 3850 2250 3950
Entry Wire Line
	2150 3950 2250 4050
Entry Wire Line
	2150 4050 2250 4150
Entry Wire Line
	2150 4150 2250 4250
Wire Wire Line
	2550 7100 2700 7100
Wire Wire Line
	2550 7200 2700 7200
Wire Wire Line
	2550 7300 2700 7300
Wire Wire Line
	2550 7400 2700 7400
Wire Wire Line
	2550 7500 2700 7500
Wire Wire Line
	2550 7600 2700 7600
Wire Wire Line
	2550 7700 2700 7700
Wire Wire Line
	2550 7000 2700 7000
Entry Wire Line
	2700 7000 2800 7100
Entry Wire Line
	2700 7100 2800 7200
Entry Wire Line
	2700 7200 2800 7300
Entry Wire Line
	2700 7300 2800 7400
Entry Wire Line
	2700 7400 2800 7500
Entry Wire Line
	2700 7500 2800 7600
Entry Wire Line
	2700 7600 2800 7700
Entry Wire Line
	2700 7700 2800 7800
$Comp
L 74xx:74LS30 IC33
U 1 1 5C90766C
P 3650 7050
F 0 "IC33" H 3650 6472 50  0000 C CNN
F 1 "74HCT30" H 3650 6563 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 3650 7050 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS30" H 3650 7050 50  0001 C CNN
	1    3650 7050
	1    0    0    1   
$EndComp
Wire Wire Line
	3250 7250 3350 7250
Wire Wire Line
	3250 6650 3350 6650
Wire Wire Line
	3250 7150 3350 7150
Wire Wire Line
	3250 6750 3350 6750
Wire Wire Line
	3250 7050 3350 7050
Wire Wire Line
	3250 6950 3350 6950
Wire Wire Line
	3250 6850 3350 6850
Text Label 3250 6850 0    50   ~ 0
a7
Text Label 3250 6950 0    50   ~ 0
a6
Text Label 3250 7050 0    50   ~ 0
a5
Text Label 3250 6750 0    50   ~ 0
a4
Text Label 3250 6650 0    50   ~ 0
a2
Wire Wire Line
	3250 7350 3350 7350
Entry Wire Line
	3150 6750 3250 6650
Entry Wire Line
	3150 6850 3250 6750
Entry Wire Line
	3150 6950 3250 6850
Entry Wire Line
	3150 7050 3250 6950
Entry Wire Line
	3150 7150 3250 7050
Entry Wire Line
	3150 7250 3250 7150
Entry Wire Line
	3150 7350 3250 7250
Entry Wire Line
	3150 7450 3250 7350
Wire Bus Line
	2800 7800 3150 7800
$Comp
L 74xx:74LS32 IC35
U 3 1 5C94A21D
P 3600 4600
F 0 "IC35" H 3600 4900 50  0000 C CNN
F 1 "74HCT32" H 3600 4800 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 3600 4600 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 3600 4600 50  0001 C CNN
	3    3600 4600
	1    0    0    -1  
$EndComp
Text Label 2000 4150 0    50   ~ 0
d0
Text Label 2000 4050 0    50   ~ 0
d1
Text Label 2000 3950 0    50   ~ 0
d2
Text Label 2000 3850 0    50   ~ 0
d3
Text Label 2000 3750 0    50   ~ 0
d4
Text Label 2000 3650 0    50   ~ 0
d5
Text Label 2000 3550 0    50   ~ 0
d6
Text Label 2000 3450 0    50   ~ 0
d7
Text Label 2550 7000 0    50   ~ 0
a7
Text Label 2550 7100 0    50   ~ 0
a6
Text Label 2550 7200 0    50   ~ 0
a5
Text Label 2550 7300 0    50   ~ 0
a4
Text Label 2550 7400 0    50   ~ 0
a3
Text Label 2550 7500 0    50   ~ 0
a2
Text Label 2550 7600 0    50   ~ 0
a1
Text Label 2550 7700 0    50   ~ 0
a0
Text Label 3250 7150 0    50   ~ 0
a3
Text Label 3250 7250 0    50   ~ 0
a1
Text Label 3250 7350 0    50   ~ 0
a0
$Comp
L 74xx:74LS32 IC35
U 2 1 5C9618D4
P 4300 5000
F 0 "IC35" H 4300 4683 50  0000 C CNN
F 1 "74HCT32" H 4300 4774 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 4300 5000 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 4300 5000 50  0001 C CNN
	2    4300 5000
	1    0    0    1   
$EndComp
Wire Wire Line
	3900 4600 3950 4600
Wire Wire Line
	3950 4600 3950 4900
Wire Wire Line
	3950 4900 4000 4900
$Comp
L 74xx:74LS00 IC14
U 1 1 5C9684E2
P 3600 5150
F 0 "IC14" H 3850 5000 50  0000 C CNN
F 1 "74HCT00" H 3800 4900 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 3600 5150 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 3600 5150 50  0001 C CNN
	1    3600 5150
	1    0    0    -1  
$EndComp
Entry Wire Line
	2550 4500 2450 4600
Entry Wire Line
	2450 4650 2350 4550
Entry Wire Line
	2350 5400 2450 5500
Entry Wire Line
	2550 5250 2450 5350
Wire Wire Line
	3950 5100 4000 5100
Text Label 3200 4500 0    50   ~ 0
d7
Text Label 2250 4550 0    50   ~ 0
d6
Text Label 2250 5200 0    50   ~ 0
d5
Text Label 3200 5250 0    50   ~ 0
d4
Entry Wire Line
	2550 5500 2450 5600
Entry Wire Line
	2550 5600 2450 5700
Entry Wire Line
	2550 5700 2450 5800
Text Label 2600 5500 0    50   ~ 0
d3
Text Label 2600 5600 0    50   ~ 0
d2
Text Label 2600 5700 0    50   ~ 0
d1
$Comp
L music5xxx-custom:74LS173 IC31
U 1 1 5C9C4823
P 5250 5550
F 0 "IC31" H 5700 5950 50  0000 C CNN
F 1 "74HCT173" H 5800 5850 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 5250 5550 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS173" H 5250 5550 50  0001 C CNN
	1    5250 5550
	1    0    0    -1  
$EndComp
Wire Wire Line
	4600 5000 4650 5000
Wire Wire Line
	4650 5000 4650 5400
Wire Wire Line
	4650 5400 4700 5400
Wire Wire Line
	2550 5500 4700 5500
Wire Wire Line
	2550 5600 4700 5600
Wire Wire Line
	2550 5700 4700 5700
Wire Wire Line
	5350 6100 5350 6150
Wire Wire Line
	5450 6150 5450 6100
Connection ~ 5350 6150
Wire Wire Line
	5350 6150 5450 6150
$Comp
L power:GND #PWR029
U 1 1 5CA06A1F
P 5600 4850
F 0 "#PWR029" H 5600 4600 50  0001 C CNN
F 1 "GND" H 5605 4677 50  0000 C CNN
F 2 "" H 5600 4850 50  0001 C CNN
F 3 "" H 5600 4850 50  0001 C CNN
	1    5600 4850
	1    0    0    -1  
$EndComp
Wire Wire Line
	5350 5000 5350 4800
Wire Wire Line
	5350 4800 5600 4800
Wire Wire Line
	5600 4800 5600 4850
Wire Wire Line
	5150 5000 5150 4800
Wire Wire Line
	5150 4800 5050 4800
Text GLabel 5050 4800 0    50   Input ~ 0
E
Wire Wire Line
	5050 6100 5050 6150
Wire Wire Line
	4500 6150 5050 6150
Text Label 4500 6150 0    50   ~ 0
~PGFC
Wire Wire Line
	3950 7050 4100 7050
Wire Wire Line
	4100 7050 4100 6300
Wire Wire Line
	4100 6300 5150 6300
Wire Wire Line
	5150 6300 5150 6100
$Comp
L music5xxx-custom:74LS374 IC29
U 1 1 5CA3ADD2
P 5300 7250
F 0 "IC29" H 5300 6650 50  0000 C CNN
F 1 "74HCT374" H 5300 6550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 5300 7250 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS374" H 5300 7250 50  0001 C CNN
	1    5300 7250
	1    0    0    -1  
$EndComp
Wire Bus Line
	3150 7800 4600 7800
Connection ~ 3150 7800
Wire Wire Line
	4700 7500 4800 7500
Wire Wire Line
	4700 6900 4800 6900
Wire Wire Line
	4700 7400 4800 7400
Wire Wire Line
	4700 7000 4800 7000
Wire Wire Line
	4700 7300 4800 7300
Wire Wire Line
	4700 7200 4800 7200
Wire Wire Line
	4700 7100 4800 7100
Text Label 4700 7100 0    50   ~ 0
a5
Text Label 4700 7200 0    50   ~ 0
a7
Text Label 4700 7300 0    50   ~ 0
a6
Text Label 4700 7000 0    50   ~ 0
a3
Text Label 4700 6900 0    50   ~ 0
a1
Wire Wire Line
	4700 7600 4800 7600
Entry Wire Line
	4600 7000 4700 6900
Entry Wire Line
	4600 7100 4700 7000
Entry Wire Line
	4600 7200 4700 7100
Entry Wire Line
	4600 7300 4700 7200
Entry Wire Line
	4600 7400 4700 7300
Entry Wire Line
	4600 7500 4700 7400
Entry Wire Line
	4600 7600 4700 7500
Entry Wire Line
	4600 7700 4700 7600
Text Label 4700 7400 0    50   ~ 0
a4
Text Label 4700 7500 0    50   ~ 0
a2
Text Label 4700 7600 0    50   ~ 0
a0
Wire Wire Line
	5350 6150 5350 6500
Wire Wire Line
	5350 6500 6150 6500
Connection ~ 5350 6500
Wire Wire Line
	5350 6500 5350 6550
$Comp
L 74xx:74LS32 IC30
U 3 1 5CA58476
P 6450 6500
F 0 "IC30" H 6450 6825 50  0000 C CNN
F 1 "74HCT32" H 6450 6734 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 6450 6500 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 6450 6500 50  0001 C CNN
	3    6450 6500
	-1   0    0    -1  
$EndComp
Wire Wire Line
	6750 6400 6800 6400
Wire Wire Line
	6750 6600 6800 6600
Text GLabel 6800 6600 2    50   Input ~ 0
PHI1
Text GLabel 6800 6400 2    50   Input ~ 0
~PROG
Entry Wire Line
	6100 6900 6000 7000
Wire Wire Line
	5800 7000 6000 7000
Entry Wire Line
	6100 6800 6000 6900
Wire Wire Line
	5800 6900 6000 6900
Entry Wire Line
	6100 7100 6000 7200
Wire Wire Line
	5800 7200 6000 7200
Entry Wire Line
	6100 7000 6000 7100
Wire Wire Line
	5800 7100 6000 7100
Entry Wire Line
	6100 7300 6000 7400
Wire Wire Line
	5800 7400 6000 7400
Entry Wire Line
	6100 7200 6000 7300
Wire Wire Line
	5800 7300 6000 7300
Entry Wire Line
	6100 7500 6000 7600
Wire Wire Line
	5800 7600 6000 7600
Entry Wire Line
	6100 7400 6000 7500
Wire Wire Line
	5800 7500 6000 7500
Entry Wire Line
	6000 5600 6100 5700
Wire Wire Line
	5800 5600 6000 5600
Entry Wire Line
	6000 5500 6100 5600
Wire Wire Line
	5800 5500 6000 5500
Entry Wire Line
	6000 5700 6100 5800
Wire Wire Line
	5800 5700 5950 5700
Wire Wire Line
	5800 5400 6250 5400
Text GLabel 6250 5400 2    50   Input ~ 0
~WRG
Wire Wire Line
	5250 6550 5250 6500
Wire Wire Line
	5250 6500 4950 6500
Text GLabel 4950 6500 0    50   Input ~ 0
SEL
Text Label 5800 7600 0    50   ~ 0
b0
Text Label 5800 6900 0    50   ~ 0
b1
Text Label 5800 7500 0    50   ~ 0
b2
Text Label 5800 7000 0    50   ~ 0
b3
Text Label 5800 7400 0    50   ~ 0
b4
Text Label 5800 7100 0    50   ~ 0
b5
Text Label 5800 7300 0    50   ~ 0
b6
Text Label 5800 7200 0    50   ~ 0
b7
Text Label 5800 5700 0    50   ~ 0
b8
Text Label 5800 5600 0    50   ~ 0
b9
Text Label 5800 5500 0    50   ~ 0
b10
$Comp
L music5xxx-custom:74LS374 IC32
U 1 1 5CB46D93
P 5300 3800
F 0 "IC32" H 5300 3200 50  0000 C CNN
F 1 "74HCT374" H 5300 3100 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 5300 3800 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS374" H 5300 3800 50  0001 C CNN
	1    5300 3800
	1    0    0    -1  
$EndComp
Entry Wire Line
	4650 3550 4550 3650
Text Label 4650 3450 0    50   ~ 0
d1
Wire Wire Line
	4650 3550 4800 3550
Wire Wire Line
	4650 3650 4800 3650
Wire Wire Line
	4650 3750 4800 3750
Wire Wire Line
	4650 3850 4800 3850
Wire Wire Line
	4650 3950 4800 3950
Wire Wire Line
	4650 4050 4800 4050
Wire Wire Line
	4650 4150 4800 4150
Text Label 4650 4150 0    50   ~ 0
d0
Text Label 4650 4050 0    50   ~ 0
d2
Text Label 4650 3950 0    50   ~ 0
d4
Text Label 4650 3850 0    50   ~ 0
d6
Text Label 4650 3750 0    50   ~ 0
d7
Text Label 4650 3650 0    50   ~ 0
d5
Text Label 4650 3550 0    50   ~ 0
d3
Wire Wire Line
	4650 3450 4800 3450
Entry Wire Line
	4650 3450 4550 3550
Entry Wire Line
	4650 3750 4550 3850
Entry Wire Line
	4650 3650 4550 3750
Entry Wire Line
	4650 3950 4550 4050
Entry Wire Line
	4650 3850 4550 3950
Entry Wire Line
	4650 4150 4550 4250
Entry Wire Line
	4650 4050 4550 4150
Wire Bus Line
	2450 4250 4550 4250
Connection ~ 2450 4250
Wire Wire Line
	5350 2900 5350 3100
Wire Wire Line
	5250 2900 5250 3100
Entry Wire Line
	6200 3450 6100 3550
Wire Wire Line
	5800 3550 6100 3550
Entry Wire Line
	6200 3350 6100 3450
Wire Wire Line
	5800 3450 6100 3450
Entry Wire Line
	6200 3650 6100 3750
Wire Wire Line
	5800 3750 6100 3750
Entry Wire Line
	6200 3550 6100 3650
Wire Wire Line
	5800 3650 6100 3650
Entry Wire Line
	6200 3850 6100 3950
Wire Wire Line
	5800 3950 6100 3950
Entry Wire Line
	6200 3750 6100 3850
Wire Wire Line
	5800 3850 6100 3850
Entry Wire Line
	6200 4050 6100 4150
Wire Wire Line
	5800 4150 6100 4150
Entry Wire Line
	6200 3950 6100 4050
Wire Wire Line
	5800 4050 6100 4050
$Comp
L Device:R_Network08 RP1
U 1 1 5CBFEC62
P 6700 1700
F 0 "RP1" H 7300 1650 50  0000 R CNN
F 1 "1Kx8" H 7300 1750 50  0000 R CNN
F 2 "Resistors_THT:R_Array_SIP9" V 7175 1700 50  0001 C CNN
F 3 "http://www.vishay.com/docs/31509/csc.pdf" H 6700 1700 50  0001 C CNN
	1    6700 1700
	-1   0    0    1   
$EndComp
Connection ~ 3400 3050
Wire Wire Line
	3400 3050 3400 3000
Entry Wire Line
	7100 1100 7000 1200
Wire Wire Line
	7000 1500 7000 1200
Entry Wire Line
	7200 1100 7100 1200
Wire Wire Line
	7100 1500 7100 1200
Entry Wire Line
	6900 1100 6800 1200
Wire Wire Line
	6800 1500 6800 1200
Entry Wire Line
	7000 1100 6900 1200
Wire Wire Line
	6900 1500 6900 1200
Entry Wire Line
	6700 1100 6600 1200
Wire Wire Line
	6600 1500 6600 1200
Entry Wire Line
	6800 1100 6700 1200
Wire Wire Line
	6700 1500 6700 1200
Entry Wire Line
	6500 1100 6400 1200
Entry Wire Line
	6600 1100 6500 1200
Wire Wire Line
	6500 1500 6500 1200
Text Label 6400 1450 1    50   ~ 0
data0
Wire Wire Line
	6400 1500 6400 1200
Text GLabel 7500 1100 2    50   Output ~ 0
data[0..7]
Entry Wire Line
	7300 2950 7400 2850
Wire Wire Line
	7700 2850 7400 2850
Entry Wire Line
	7300 3050 7400 2950
Wire Wire Line
	7700 2950 7400 2950
Entry Wire Line
	7300 2750 7400 2650
Wire Wire Line
	7700 2650 7400 2650
Entry Wire Line
	7300 2850 7400 2750
Wire Wire Line
	7700 2750 7400 2750
Entry Wire Line
	7300 2550 7400 2450
Wire Wire Line
	7700 2450 7400 2450
Entry Wire Line
	7300 2650 7400 2550
Wire Wire Line
	7700 2550 7400 2550
Entry Wire Line
	7300 3150 7400 3050
Wire Wire Line
	7700 3050 7400 3050
Entry Wire Line
	7300 2450 7400 2350
Wire Wire Line
	7700 2350 7400 2350
Wire Bus Line
	6200 3350 7300 3350
Connection ~ 7300 1100
Wire Bus Line
	7300 1100 7500 1100
$Comp
L 74xx:74LS08 IC2
U 2 1 5CD4AF33
P 6900 4150
F 0 "IC2" H 6900 4475 50  0000 C CNN
F 1 "74HCT08" H 6900 4384 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 6900 4150 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS08" H 6900 4150 50  0001 C CNN
	2    6900 4150
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS32 IC30
U 2 1 5CD4B0B3
P 7800 4250
F 0 "IC30" H 7750 3950 50  0000 C CNN
F 1 "74HCT32" H 7800 4050 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 7800 4250 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 7800 4250 50  0001 C CNN
	2    7800 4250
	1    0    0    -1  
$EndComp
Text GLabel 7400 4350 0    50   Input ~ 0
PHI1
Wire Wire Line
	7400 4350 7500 4350
Wire Wire Line
	7200 4150 7250 4150
Wire Wire Line
	7250 4150 7250 3850
Connection ~ 7250 4150
Wire Wire Line
	7250 4150 7500 4150
Wire Wire Line
	7900 3850 8100 3850
Wire Wire Line
	8100 4250 8400 4250
Wire Wire Line
	8400 4250 8400 3550
Text GLabel 6500 4050 0    50   Input ~ 0
~WR
Text GLabel 6500 4250 0    50   Input ~ 0
~CRST
Wire Wire Line
	6500 4250 6600 4250
Wire Wire Line
	6500 4050 6600 4050
$Comp
L music5xxx-custom:74LS374 IC27
U 1 1 5CDF0BCC
P 12750 2300
F 0 "IC27" H 12750 1700 50  0000 C CNN
F 1 "74HCT374" H 12750 1600 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 12750 2300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS374" H 12750 2300 50  0001 C CNN
	1    12750 2300
	-1   0    0    1   
$EndComp
$Comp
L power:GND #PWR038
U 1 1 5CE0930C
P 12700 3050
F 0 "#PWR038" H 12700 2800 50  0001 C CNN
F 1 "GND" H 12705 2877 50  0000 C CNN
F 2 "" H 12700 3050 50  0001 C CNN
F 3 "" H 12700 3050 50  0001 C CNN
	1    12700 3050
	1    0    0    -1  
$EndComp
Wire Wire Line
	12700 3050 12700 3000
Text GLabel 13000 3050 2    50   Input ~ 0
PHI1
Wire Wire Line
	13000 3050 12800 3050
Wire Wire Line
	12800 3050 12800 3000
Text GLabel 13450 2650 2    50   Input ~ 0
CY4
Wire Wire Line
	13450 2650 13250 2650
Wire Wire Line
	13250 2550 13350 2550
Wire Wire Line
	13350 2550 13350 3400
Wire Wire Line
	13350 3400 12650 3400
Wire Wire Line
	12200 3400 12200 2650
Wire Wire Line
	12200 2650 12250 2650
Wire Wire Line
	12250 2550 12200 2550
Text GLabel 12200 2550 0    50   Output ~ 0
C4D
Wire Wire Line
	12250 2350 12200 2350
Text GLabel 12200 2350 0    50   Output ~ 0
PA2
Text GLabel 12000 2450 0    50   Output ~ 0
~PROG
Wire Wire Line
	12000 2450 12250 2450
Text GLabel 12200 1950 0    50   Output ~ 0
~WR
Wire Wire Line
	12200 1950 12250 1950
$Comp
L 74xx:74LS04 IC5
U 2 1 5CECF1F4
P 12350 3850
F 0 "IC5" H 12350 4167 50  0000 C CNN
F 1 "74F04" H 12350 4076 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 12350 3850 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS04" H 12350 3850 50  0001 C CNN
	2    12350 3850
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS00 IC14
U 3 1 5CECF3AA
P 13000 3750
F 0 "IC14" H 12950 4000 50  0000 C CNN
F 1 "74HCT00" H 12950 4100 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 13000 3750 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 13000 3750 50  0001 C CNN
	3    13000 3750
	1    0    0    1   
$EndComp
Wire Wire Line
	12650 3850 12700 3850
Wire Wire Line
	12700 3650 12650 3650
Wire Wire Line
	12650 3650 12650 3400
Connection ~ 12650 3400
Wire Wire Line
	12650 3400 12200 3400
Wire Wire Line
	13300 3750 13450 3750
Text GLabel 13450 3750 2    50   Output ~ 0
~SX
Text GLabel 12000 3850 0    50   Input ~ 0
~S7
Wire Wire Line
	12000 3850 12050 3850
$Comp
L Device:R R3
U 1 1 5CF6C913
P 13850 1450
F 0 "R3" H 13920 1496 50  0000 L CNN
F 1 "1K" H 13920 1405 50  0000 L CNN
F 2 "musix5xxx-custom:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal" V 13780 1450 50  0001 C CNN
F 3 "~" H 13850 1450 50  0001 C CNN
	1    13850 1450
	1    0    0    -1  
$EndComp
$Comp
L Device:R R4
U 1 1 5CF6C9B9
P 14100 1450
F 0 "R4" H 14170 1496 50  0000 L CNN
F 1 "1K" H 14170 1405 50  0000 L CNN
F 2 "musix5xxx-custom:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal" V 14030 1450 50  0001 C CNN
F 3 "~" H 14100 1450 50  0001 C CNN
	1    14100 1450
	1    0    0    -1  
$EndComp
$Comp
L Device:R R5
U 1 1 5CF6CA09
P 14350 1450
F 0 "R5" H 14420 1496 50  0000 L CNN
F 1 "1K" H 14420 1405 50  0000 L CNN
F 2 "musix5xxx-custom:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal" V 14280 1450 50  0001 C CNN
F 3 "~" H 14350 1450 50  0001 C CNN
	1    14350 1450
	1    0    0    -1  
$EndComp
$Comp
L Device:R R2
U 1 1 5CF6CA5D
P 13450 1450
F 0 "R2" H 13520 1496 50  0000 L CNN
F 1 "1K" H 13520 1405 50  0000 L CNN
F 2 "musix5xxx-custom:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal" V 13380 1450 50  0001 C CNN
F 3 "~" H 13450 1450 50  0001 C CNN
	1    13450 1450
	1    0    0    -1  
$EndComp
$Comp
L power:+5V #PWR039
U 1 1 5CF7B5F2
P 13450 1200
F 0 "#PWR039" H 13450 1050 50  0001 C CNN
F 1 "+5V" H 13465 1373 50  0000 C CNN
F 2 "" H 13450 1200 50  0001 C CNN
F 3 "" H 13450 1200 50  0001 C CNN
	1    13450 1200
	1    0    0    -1  
$EndComp
Wire Wire Line
	13450 1200 13450 1250
Wire Wire Line
	14350 1250 14350 1300
Wire Wire Line
	14100 1300 14100 1250
Connection ~ 14100 1250
Wire Wire Line
	14100 1250 14350 1250
Wire Wire Line
	13850 1300 13850 1250
Connection ~ 13850 1250
Wire Wire Line
	13850 1250 14100 1250
Wire Wire Line
	13450 1300 13450 1250
Connection ~ 13450 1250
Wire Wire Line
	13450 1250 13850 1250
Wire Wire Line
	13250 2050 14350 2050
Wire Wire Line
	14350 2050 14350 1600
Wire Wire Line
	13250 1950 13450 1950
Wire Wire Line
	13450 1950 13450 1600
Wire Wire Line
	13450 1950 13500 1950
Connection ~ 13450 1950
Text GLabel 13500 1950 2    50   Input ~ 0
~WRG
Wire Wire Line
	14100 2150 14100 1600
Wire Wire Line
	13250 2150 14100 2150
Wire Wire Line
	13250 2250 13850 2250
Wire Wire Line
	13850 2250 13850 1600
Entry Wire Line
	14500 2150 14600 2250
Entry Wire Line
	14500 2050 14600 2150
Wire Wire Line
	14350 2050 14500 2050
Entry Wire Line
	14500 2250 14600 2350
Text Label 14350 2250 0    50   ~ 0
b8
Text Label 14350 2150 0    50   ~ 0
b9
Text Label 14350 2050 0    50   ~ 0
b10
Connection ~ 14350 2050
Wire Wire Line
	14100 2150 14500 2150
Connection ~ 14100 2150
Wire Wire Line
	13850 2250 14500 2250
Connection ~ 13850 2250
Wire Wire Line
	13750 2350 13250 2350
Text GLabel 13450 2450 2    50   Input ~ 0
~REQ
Wire Wire Line
	13450 2450 13250 2450
$Comp
L music5xxx-custom:74LS374 IC26
U 1 1 5D1CA2DC
P 9800 3150
F 0 "IC26" H 9800 2550 50  0000 C CNN
F 1 "74HCT374" H 9800 2450 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 9800 3150 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS374" H 9800 3150 50  0001 C CNN
	1    9800 3150
	-1   0    0    1   
$EndComp
Text GLabel 9900 3950 2    50   Input ~ 0
PHI1
Wire Wire Line
	9900 3950 9850 3950
Wire Wire Line
	9850 3950 9850 3850
$Comp
L power:GND #PWR032
U 1 1 5D1DB3EF
P 9750 3950
F 0 "#PWR032" H 9750 3700 50  0001 C CNN
F 1 "GND" H 9755 3777 50  0000 C CNN
F 2 "" H 9750 3950 50  0001 C CNN
F 3 "" H 9750 3950 50  0001 C CNN
	1    9750 3950
	1    0    0    -1  
$EndComp
Wire Wire Line
	9750 3950 9750 3850
Wire Bus Line
	6100 4800 7350 4800
Wire Bus Line
	10550 4800 9700 4800
Connection ~ 10550 4800
Entry Wire Line
	10550 3500 10450 3400
Wire Wire Line
	10300 3400 10450 3400
Entry Wire Line
	10550 3600 10450 3500
Wire Wire Line
	10300 3500 10450 3500
Entry Wire Line
	10550 3300 10450 3200
Wire Wire Line
	10300 3200 10450 3200
Entry Wire Line
	10550 3400 10450 3300
Wire Wire Line
	10300 3300 10450 3300
Entry Wire Line
	10550 3100 10450 3000
Wire Wire Line
	10300 3000 10450 3000
Entry Wire Line
	10550 3200 10450 3100
Wire Wire Line
	10300 3100 10450 3100
Entry Wire Line
	10550 2900 10450 2800
Wire Wire Line
	10300 2800 10450 2800
Entry Wire Line
	10550 3000 10450 2900
Wire Wire Line
	10300 2900 10450 2900
Text Label 10300 2800 0    50   ~ 0
b0
Text Label 10300 3500 0    50   ~ 0
b1
Text Label 10300 2900 0    50   ~ 0
b2
Text Label 10300 3400 0    50   ~ 0
b3
Text Label 10300 3000 0    50   ~ 0
b4
Text Label 10300 3300 0    50   ~ 0
b5
Text Label 10300 3100 0    50   ~ 0
b6
Text Label 10300 3200 0    50   ~ 0
b7
Text Label 8900 3050 2    50   ~ 0
b0'
Wire Wire Line
	9150 2800 9300 2800
Text Label 8900 2950 2    50   ~ 0
b1'
Text Label 8900 2850 2    50   ~ 0
b2'
Text Label 8900 2750 2    50   ~ 0
b3'
Text Label 8900 2650 2    50   ~ 0
b4'
Text Label 8900 2550 2    50   ~ 0
b5'
Text Label 8900 2450 2    50   ~ 0
b6'
Text Label 8900 2350 2    50   ~ 0
b7'
Wire Wire Line
	9150 2900 9300 2900
Wire Wire Line
	9150 3000 9300 3000
Wire Wire Line
	9150 3100 9300 3100
Wire Wire Line
	9150 3300 9300 3300
Wire Wire Line
	9150 3400 9300 3400
Text Label 9150 2800 0    50   ~ 0
b0'
Text Label 9150 3500 0    50   ~ 0
b1'
Text Label 9150 3400 0    50   ~ 0
b3'
Text Label 9150 2900 0    50   ~ 0
b2'
Text Label 9150 3000 0    50   ~ 0
b4'
Text Label 9150 3300 0    50   ~ 0
b5'
Text Label 9150 3100 0    50   ~ 0
b6'
Text Label 9150 3200 0    50   ~ 0
b7'
Wire Wire Line
	8950 2950 8950 3500
Wire Wire Line
	8950 3500 9300 3500
Wire Wire Line
	9050 3200 9050 2350
Wire Wire Line
	9050 3200 9300 3200
$Comp
L 74xx:74LS86 IC15
U 4 1 5D458A57
P 12750 5500
F 0 "IC15" H 12750 5183 50  0000 C CNN
F 1 "74HCT86" H 12750 5274 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 12750 5500 50  0001 C CNN
F 3 "74xx/74ls86.pdf" H 12750 5500 50  0001 C CNN
	4    12750 5500
	1    0    0    1   
$EndComp
Text GLabel 12250 5600 0    50   Input ~ 0
~PROG
Wire Wire Line
	12250 5600 12450 5600
Text GLabel 13250 5500 2    50   Output ~ 0
PROG
Wire Wire Line
	13250 5500 13050 5500
Wire Wire Line
	12450 5400 12250 5400
Wire Wire Line
	12250 5400 12250 5300
$Comp
L power:+5V #PWR035
U 1 1 5D4B01F0
P 12250 5300
F 0 "#PWR035" H 12250 5150 50  0001 C CNN
F 1 "+5V" H 12265 5473 50  0000 C CNN
F 2 "" H 12250 5300 50  0001 C CNN
F 3 "" H 12250 5300 50  0001 C CNN
	1    12250 5300
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS257 IC23
U 1 1 5C91E755
P 8250 6400
F 0 "IC23" H 8250 7300 50  0000 C CNN
F 1 "74HCT257" H 8250 7150 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 8250 6400 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS257" H 8250 6400 50  0001 C CNN
	1    8250 6400
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS257 IC18
U 1 1 5C94F292
P 8250 8450
F 0 "IC18" H 8250 9350 50  0000 C CNN
F 1 "74HCT257" H 8250 9200 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 8250 8450 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS257" H 8250 8450 50  0001 C CNN
	1    8250 8450
	1    0    0    -1  
$EndComp
Text GLabel 9050 5950 2    50   Input ~ 0
~S6
Wire Wire Line
	9050 5950 8750 5950
Text GLabel 8800 5850 2    50   Input ~ 0
PROG
Wire Wire Line
	8800 5850 8750 5850
Wire Wire Line
	8750 6950 10000 6950
Wire Wire Line
	7750 8850 7300 8850
Wire Wire Line
	7750 8700 7300 8700
Wire Wire Line
	7750 9000 7300 9000
Wire Wire Line
	7750 6800 7300 6800
Wire Wire Line
	7750 6500 7300 6500
Wire Wire Line
	7750 6650 7300 6650
Entry Wire Line
	7300 9000 7200 8900
Entry Wire Line
	7300 8850 7200 8750
Entry Wire Line
	7300 8700 7200 8600
Entry Wire Line
	7300 8550 7200 8450
Entry Wire Line
	7300 6800 7200 6700
Entry Wire Line
	7300 6500 7200 6400
Entry Wire Line
	7300 6650 7200 6550
Text Label 7700 8850 2    50   ~ 0
addr6
Text Label 7700 8700 2    50   ~ 0
addr4
Text Label 7700 9000 2    50   ~ 0
addr3
Text Label 7750 6800 2    50   ~ 0
addr2
Text Label 7750 6500 2    50   ~ 0
addr1
Text Label 7750 6650 2    50   ~ 0
addr0
Text GLabel 8850 8000 2    50   Input ~ 0
~S6
Wire Wire Line
	8850 8000 8750 8000
Text GLabel 8750 7800 2    50   Input ~ 0
PROG
Wire Wire Line
	8750 7800 8750 7900
Wire Wire Line
	7700 6950 7750 6950
Text GLabel 7700 6950 0    50   Input ~ 0
INDEX
Text Label 7700 8550 2    50   ~ 0
addr5
Wire Wire Line
	7750 8550 7300 8550
Wire Bus Line
	7200 8900 7100 8900
Text GLabel 7100 8900 0    50   Input ~ 0
addr[0..6]
Wire Wire Line
	7750 6300 7450 6300
Wire Wire Line
	7750 6150 7450 6150
Wire Wire Line
	7750 6000 7450 6000
Wire Wire Line
	7750 5850 7450 5850
Wire Wire Line
	7750 7900 7450 7900
Wire Wire Line
	7750 8050 7450 8050
Wire Wire Line
	7450 8200 7750 8200
Wire Wire Line
	7450 8350 7750 8350
Text Label 7650 8050 0    50   ~ 0
b0
Text Label 7650 7900 0    50   ~ 0
b1
Text Label 7650 8200 0    50   ~ 0
b2
Text Label 7650 8350 0    50   ~ 0
b3
Text Label 7650 5850 0    50   ~ 0
b4
Text Label 7650 6150 0    50   ~ 0
b5
Text Label 7650 6000 0    50   ~ 0
b6
Text Label 7650 6300 0    50   ~ 0
b7
Entry Wire Line
	7350 5750 7450 5850
Entry Wire Line
	7350 5900 7450 6000
Entry Wire Line
	7350 6050 7450 6150
Entry Wire Line
	7350 6200 7450 6300
Entry Wire Line
	7350 7800 7450 7900
Entry Wire Line
	7350 7950 7450 8050
Entry Wire Line
	7350 8100 7450 8200
Entry Wire Line
	7350 8250 7450 8350
Connection ~ 7350 4800
Wire Bus Line
	7350 4800 9700 4800
Text GLabel 9550 8900 2    50   Input ~ 0
sum[0..7]
Wire Wire Line
	8750 8550 8950 8550
Wire Wire Line
	8750 8700 8950 8700
Wire Wire Line
	8750 8850 8950 8850
Wire Wire Line
	8750 9000 8950 9000
Text Label 8950 9000 2    50   ~ 0
sum4
Text Label 8950 8850 2    50   ~ 0
sum3
Text Label 8950 8700 2    50   ~ 0
sum1
Text Label 8950 8550 2    50   ~ 0
sum2
Wire Wire Line
	8750 6500 8950 6500
Wire Wire Line
	8750 6650 8950 6650
Wire Wire Line
	8750 6800 8950 6800
Text Label 8950 6800 2    50   ~ 0
sum6
Text Label 8950 6650 2    50   ~ 0
sum7
Text Label 8950 6500 2    50   ~ 0
sum5
Entry Wire Line
	9050 8450 8950 8550
Entry Wire Line
	9050 8600 8950 8700
Entry Wire Line
	9050 8750 8950 8850
Entry Wire Line
	9050 8900 8950 9000
Entry Wire Line
	9050 6400 8950 6500
Entry Wire Line
	9050 6550 8950 6650
Entry Wire Line
	9050 6700 8950 6800
Wire Bus Line
	9050 8900 9550 8900
$Comp
L music5xxx-custom:74LS173 IC25
U 1 1 5CF67D53
P 10550 6800
F 0 "IC25" H 11000 7200 50  0000 C CNN
F 1 "74HCT173" H 11100 7100 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 10550 6800 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS173" H 10550 6800 50  0001 C CNN
	1    10550 6800
	-1   0    0    -1  
$EndComp
Wire Wire Line
	10650 6250 10650 6050
Wire Wire Line
	10650 6050 10750 6050
Text GLabel 10750 6050 2    50   Input ~ 0
PHI1
$Comp
L power:GND #PWR033
U 1 1 5D0360A7
P 10300 6100
F 0 "#PWR033" H 10300 5850 50  0001 C CNN
F 1 "GND" H 10305 5927 50  0000 C CNN
F 2 "" H 10300 6100 50  0001 C CNN
F 3 "" H 10300 6100 50  0001 C CNN
	1    10300 6100
	1    0    0    -1  
$EndComp
Wire Wire Line
	10300 6100 10300 6050
Wire Wire Line
	10300 6050 10450 6050
Wire Wire Line
	10450 6050 10450 6250
$Comp
L power:GND #PWR034
U 1 1 5D054FDA
P 10650 7600
F 0 "#PWR034" H 10650 7350 50  0001 C CNN
F 1 "GND" H 10655 7427 50  0000 C CNN
F 2 "" H 10650 7600 50  0001 C CNN
F 3 "" H 10650 7600 50  0001 C CNN
	1    10650 7600
	1    0    0    -1  
$EndComp
Wire Wire Line
	10650 7600 10650 7350
Wire Wire Line
	10750 7350 10750 7650
Wire Wire Line
	10750 7650 10950 7650
Text GLabel 10950 7650 2    50   Input ~ 0
~S4
Text GLabel 10250 7650 0    50   Input ~ 0
~S6
Wire Wire Line
	10250 7650 10450 7650
Wire Wire Line
	10450 7650 10450 7350
Text GLabel 10250 7450 0    50   Input ~ 0
PROG
Wire Wire Line
	10250 7450 10350 7450
Wire Wire Line
	10350 7450 10350 7350
Connection ~ 9700 4800
Wire Wire Line
	10000 6850 9800 6850
Text Label 9800 6850 0    50   ~ 0
b8
Entry Wire Line
	9700 6750 9800 6850
Wire Wire Line
	10000 6750 9800 6750
Text Label 9800 6750 0    50   ~ 0
b9
Entry Wire Line
	9700 6650 9800 6750
Wire Wire Line
	10000 6650 9800 6650
Text Label 9800 6650 0    50   ~ 0
b10
Entry Wire Line
	9700 6550 9800 6650
Text GLabel 11300 5700 0    50   Input ~ 0
data[0..7]
Wire Wire Line
	11100 6750 11350 6750
Wire Wire Line
	11100 6850 11350 6850
Wire Wire Line
	11100 6950 11350 6950
Wire Wire Line
	11100 6650 11350 6650
Entry Wire Line
	11450 6550 11350 6650
Entry Wire Line
	11450 6650 11350 6750
Entry Wire Line
	11450 6750 11350 6850
Entry Wire Line
	11450 6850 11350 6950
Text Label 11150 6950 0    50   ~ 0
data4
Text Label 11150 6850 0    50   ~ 0
data5
Text Label 11150 6750 0    50   ~ 0
data6
Text Label 11150 6650 0    50   ~ 0
data7
Wire Wire Line
	11750 8300 11550 8300
Wire Wire Line
	11750 8400 11550 8400
Wire Wire Line
	11750 8500 11550 8500
Wire Wire Line
	11750 8200 11000 8200
Entry Wire Line
	11450 8400 11550 8300
Entry Wire Line
	11450 8500 11550 8400
Entry Wire Line
	11450 8600 11550 8500
Wire Bus Line
	11300 5700 11450 5700
$Comp
L music5xxx-custom:74LS163 IC21
U 1 1 5D3F93C6
P 12300 8350
F 0 "IC21" H 12850 9050 50  0000 C CNN
F 1 "74HCT163" H 12900 8900 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 12300 8350 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS163" H 12300 8350 50  0001 C CNN
	1    12300 8350
	1    0    0    -1  
$EndComp
NoConn ~ 12550 8950
$Comp
L 74xx:74LS32 IC30
U 4 1 5D4A40F7
P 10700 8200
F 0 "IC30" H 10700 7950 50  0000 C CNN
F 1 "74HCT32" H 10700 7850 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 10700 8200 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 10700 8200 50  0001 C CNN
	4    10700 8200
	1    0    0    -1  
$EndComp
Text GLabel 10250 8100 0    50   Input ~ 0
C4D
Wire Wire Line
	10250 8100 10400 8100
Text GLabel 10250 8300 0    50   Input ~ 0
SIGN
Wire Wire Line
	10250 8300 10400 8300
Text GLabel 11800 9150 0    50   Input ~ 0
PHI1
Wire Wire Line
	11800 9150 12050 9150
Wire Wire Line
	12050 9150 12050 8950
Wire Wire Line
	12200 8950 12200 9150
Wire Wire Line
	12200 9150 12300 9150
Wire Wire Line
	12300 9150 12300 9250
Wire Wire Line
	12300 9150 12400 9150
Wire Wire Line
	12400 9150 12400 8950
Connection ~ 12300 9150
$Comp
L power:GND #PWR036
U 1 1 5D5563B5
P 12300 9250
F 0 "#PWR036" H 12300 9000 50  0001 C CNN
F 1 "GND" H 12305 9077 50  0000 C CNN
F 2 "" H 12300 9250 50  0001 C CNN
F 3 "" H 12300 9250 50  0001 C CNN
	1    12300 9250
	1    0    0    -1  
$EndComp
NoConn ~ 12850 8300
Text GLabel 13000 8500 2    50   Output ~ 0
INVERT
Wire Wire Line
	13000 8500 12850 8500
$Comp
L 74xx:74LS08 IC2
U 1 1 5D59D701
P 13550 8300
F 0 "IC2" H 13550 8625 50  0000 C CNN
F 1 "74HCT08" H 13550 8534 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 13550 8300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS08" H 13550 8300 50  0001 C CNN
	1    13550 8300
	1    0    0    -1  
$EndComp
Wire Wire Line
	13250 8200 12850 8200
Wire Wire Line
	12850 8400 13250 8400
Text GLabel 11850 7650 0    50   Input ~ 0
~S0
Wire Wire Line
	11850 7650 12150 7650
Wire Wire Line
	12150 7650 12150 7750
Wire Wire Line
	12450 7750 12450 7600
$Comp
L power:+5V #PWR037
U 1 1 5D703417
P 12450 7600
F 0 "#PWR037" H 12450 7450 50  0001 C CNN
F 1 "+5V" H 12465 7773 50  0000 C CNN
F 2 "" H 12450 7600 50  0001 C CNN
F 3 "" H 12450 7600 50  0001 C CNN
	1    12450 7600
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS32 IC4
U 1 1 5D704493
P 13550 9150
F 0 "IC4" H 13550 8900 50  0000 C CNN
F 1 "74HCT32" H 13550 8800 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 13550 9150 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 13550 9150 50  0001 C CNN
	1    13550 9150
	1    0    0    -1  
$EndComp
Wire Bus Line
	10550 4800 14600 4800
Text GLabel 13050 9050 0    50   Input ~ 0
~S7
Wire Wire Line
	13050 9050 13250 9050
Text GLabel 13000 9250 0    50   Input ~ 0
PHI1
Wire Wire Line
	13000 9250 13250 9250
Text Label 11550 8500 0    50   ~ 0
data4
Text Label 11550 8400 0    50   ~ 0
data5
Text Label 11550 8300 0    50   ~ 0
data7
Wire Wire Line
	8900 3050 8800 3050
Wire Wire Line
	8800 2950 8950 2950
Wire Wire Line
	8900 2850 8800 2850
Wire Wire Line
	8900 2750 8800 2750
Wire Wire Line
	8900 2650 8800 2650
Wire Wire Line
	8900 2550 8800 2550
Wire Wire Line
	8800 2250 12250 2250
Wire Wire Line
	12250 2150 8800 2150
Wire Wire Line
	8900 2450 8800 2450
Wire Wire Line
	8800 2050 12250 2050
Wire Wire Line
	8800 2350 9050 2350
$Comp
L music5xxx-custom:AM9128-10 IC28
U 1 1 5CCBCEEA
P 8250 2700
F 0 "IC28" H 8250 3550 50  0000 C CNN
F 1 "AM9128-10" H 8250 3450 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-24_W15.24mm_LongPads" H 8250 2700 50  0001 C CNN
F 3 "" H 8250 2700 50  0001 C CNN
	1    8250 2700
	1    0    0    -1  
$EndComp
Wire Wire Line
	7250 3850 7300 3850
Wire Wire Line
	8250 3650 8250 3550
$Comp
L power:GND #PWR031
U 1 1 5CDD77E9
P 8250 3650
F 0 "#PWR031" H 8250 3400 50  0001 C CNN
F 1 "GND" H 8255 3477 50  0000 C CNN
F 2 "" H 8250 3650 50  0001 C CNN
F 3 "" H 8250 3650 50  0001 C CNN
	1    8250 3650
	1    0    0    -1  
$EndComp
Wire Wire Line
	8100 3850 8100 3550
$Comp
L 74xx:74LS04 IC5
U 5 1 5CD4ABDB
P 7600 3850
F 0 "IC5" H 7600 4167 50  0000 C CNN
F 1 "74F04" H 7600 4076 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 7600 3850 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS04" H 7600 3850 50  0001 C CNN
	5    7600 3850
	1    0    0    -1  
$EndComp
Text GLabel 14400 9150 0    50   Input ~ 0
~CRST
$Comp
L 74xx:74LS74 IC3
U 1 1 5DC690B3
P 14550 8400
F 0 "IC3" H 14800 8850 50  0000 C CNN
F 1 "74HCT74" H 14850 8750 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 14550 8400 50  0001 C CNN
F 3 "74xx/74hc_hct74.pdf" H 14550 8400 50  0001 C CNN
	1    14550 8400
	1    0    0    -1  
$EndComp
Wire Wire Line
	14550 8100 14550 7950
$Comp
L power:+5V #PWR040
U 1 1 5DC6B00E
P 14550 7950
F 0 "#PWR040" H 14550 7800 50  0001 C CNN
F 1 "+5V" H 14565 8123 50  0000 C CNN
F 2 "" H 14550 7950 50  0001 C CNN
F 3 "" H 14550 7950 50  0001 C CNN
	1    14550 7950
	1    0    0    -1  
$EndComp
Wire Wire Line
	13850 8300 14250 8300
Wire Wire Line
	13850 9150 14000 9150
Wire Wire Line
	14000 9150 14000 8400
Wire Wire Line
	14000 8400 14250 8400
Wire Wire Line
	14400 9150 14550 9150
Wire Wire Line
	14550 9150 14550 8700
NoConn ~ 14850 8500
Wire Wire Line
	14850 8300 15050 8300
Text GLabel 15050 8300 2    50   Output ~ 0
INDEX
$Comp
L 74xx:74LS32 IC30
U 5 1 5E0BC021
P 8050 10300
F 0 "IC30" H 8050 9650 50  0000 C CNN
F 1 "74HCT32" H 8050 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 8050 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 8050 10300 50  0001 C CNN
	5    8050 10300
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS32 IC35
U 5 1 5E10D9B4
P 10300 10300
F 0 "IC35" H 10300 9650 50  0000 C CNN
F 1 "74HCT32" H 10300 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 10300 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 10300 10300 50  0001 C CNN
	5    10300 10300
	-1   0    0    -1  
$EndComp
$Comp
L 74xx:74LS00 IC14
U 5 1 5E15C4D4
P 3550 10300
F 0 "IC14" H 3550 9650 50  0000 C CNN
F 1 "74HCT00" H 3550 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 3550 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74ls00" H 3550 10300 50  0001 C CNN
	5    3550 10300
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS08 IC2
U 5 1 5E15EA92
P 1750 10300
F 0 "IC2" H 1750 9650 50  0000 C CNN
F 1 "74HCT08" H 1750 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 1750 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS08" H 1750 10300 50  0001 C CNN
	5    1750 10300
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS74 IC34
U 3 1 5E162DBE
P 9850 10300
F 0 "IC34" H 9850 9650 50  0000 C CNN
F 1 "74HCT74" H 9850 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 9850 10300 50  0001 C CNN
F 3 "74xx/74hc_hct74.pdf" H 9850 10300 50  0001 C CNN
	3    9850 10300
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS32 IC4
U 5 1 5E16C2BE
P 2650 10300
F 0 "IC4" H 2650 9650 50  0000 C CNN
F 1 "74HCT32" H 2650 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 2650 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS32" H 2650 10300 50  0001 C CNN
	5    2650 10300
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS04 IC5
U 7 1 5E17266C
P 3100 10300
F 0 "IC5" H 3100 9650 50  0000 C CNN
F 1 "74F04" H 3100 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 3100 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS04" H 3100 10300 50  0001 C CNN
	7    3100 10300
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS86 IC15
U 5 1 5E19E614
P 4000 10300
F 0 "IC15" H 4000 9650 50  0000 C CNN
F 1 "74HCT86" H 4000 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 4000 10300 50  0001 C CNN
F 3 "74xx/74ls86.pdf" H 4000 10300 50  0001 C CNN
	5    4000 10300
	-1   0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS374 IC29
U 2 1 5E268F22
P 7600 10300
F 0 "IC29" H 7600 9650 50  0000 C CNN
F 1 "74HCT374" H 7600 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 7600 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS374" H 7600 10300 50  0001 C CNN
	2    7600 10300
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS374 IC26
U 2 1 5E337260
P 6250 10300
F 0 "IC26" H 6250 9650 50  0000 C CNN
F 1 "74HCT374" H 6250 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 6250 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS374" H 6250 10300 50  0001 C CNN
	2    6250 10300
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS374 IC32
U 2 1 5E30C442
P 8950 10300
F 0 "IC32" H 8950 9650 50  0000 C CNN
F 1 "74HCT374" H 8950 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 8950 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS374" H 8950 10300 50  0001 C CNN
	2    8950 10300
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS30 IC33
U 2 1 5E0BFE9E
P 9400 10300
F 0 "IC33" H 9400 9650 50  0000 C CNN
F 1 "74HCT30" H 9400 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 9400 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS30" H 9400 10300 50  0001 C CNN
	2    9400 10300
	-1   0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS374 IC27
U 2 1 5E3D7351
P 6700 10300
F 0 "IC27" H 6700 9650 50  0000 C CNN
F 1 "74HCT374" H 6700 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-20_W7.62mm_LongPads" H 6700 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS374" H 6700 10300 50  0001 C CNN
	2    6700 10300
	-1   0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS163 IC21
U 2 1 5E4536A6
P 4900 10300
F 0 "IC21" H 4900 9650 50  0000 C CNN
F 1 "74HCT163" H 4900 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 4900 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS163" H 4900 10300 50  0001 C CNN
	2    4900 10300
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS173 IC25
U 2 1 5E4F7B65
P 5800 10300
F 0 "IC25" H 5800 9650 50  0000 C CNN
F 1 "74HCT173" H 5800 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 5800 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS173" H 5800 10300 50  0001 C CNN
	2    5800 10300
	-1   0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS257 IC23
U 2 1 5E58103A
P 5350 10300
F 0 "IC23" H 5350 9650 50  0000 C CNN
F 1 "74HCT257" H 5350 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 5350 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS257" H 5350 10300 50  0001 C CNN
	2    5350 10300
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS257 IC18
U 2 1 5E583E6E
P 4450 10300
F 0 "IC18" H 4450 9650 50  0000 C CNN
F 1 "74HCT257" H 4450 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 4450 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS257" H 4450 10300 50  0001 C CNN
	2    4450 10300
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:74LS173 IC31
U 2 1 5E5DE2DB
P 8500 10300
F 0 "IC31" H 8500 9650 50  0000 C CNN
F 1 "74HCT173" H 8500 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-16_W7.62mm_LongPads" H 8500 10300 50  0001 C CNN
F 3 "http://www.ti.com/lit/gpn/sn74LS173" H 8500 10300 50  0001 C CNN
	2    8500 10300
	1    0    0    -1  
$EndComp
$Comp
L music5xxx-custom:AM9128-10 IC28
U 2 1 5E67CA71
P 7150 10300
F 0 "IC28" H 7150 9650 50  0000 C CNN
F 1 "AM9128-10" H 7150 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-24_W15.24mm_LongPads" H 7150 10300 50  0001 C CNN
F 3 "" H 7150 10300 50  0001 C CNN
	2    7150 10300
	1    0    0    -1  
$EndComp
$Comp
L 74xx:74LS74 IC3
U 3 1 5E7477A8
P 2200 10300
F 0 "IC3" H 2200 9650 50  0000 C CNN
F 1 "74HCT74" H 2200 9550 50  0000 C CNN
F 2 "musix5xxx-custom:DIP-14_W7.62mm_LongPads" H 2200 10300 50  0001 C CNN
F 3 "74xx/74hc_hct74.pdf" H 2200 10300 50  0001 C CNN
	3    2200 10300
	1    0    0    -1  
$EndComp
Wire Wire Line
	10300 9750 10300 9800
Wire Wire Line
	10300 10800 10300 10850
Wire Wire Line
	10300 10850 9850 10850
Wire Wire Line
	1400 10850 1400 10900
Wire Wire Line
	1750 10850 1750 10800
Wire Wire Line
	1750 9800 1750 9750
Connection ~ 1750 10850
Wire Wire Line
	1750 10850 1400 10850
Connection ~ 1750 9750
Wire Wire Line
	1750 9750 2200 9750
Wire Wire Line
	2200 9750 2200 9900
Wire Wire Line
	2200 10700 2200 10850
Connection ~ 2200 9750
Wire Wire Line
	2200 9750 2650 9750
Connection ~ 2200 10850
Wire Wire Line
	2200 10850 1750 10850
Wire Wire Line
	2650 9750 2650 9800
Wire Wire Line
	2650 10800 2650 10850
Connection ~ 2650 9750
Wire Wire Line
	2650 9750 3100 9750
Connection ~ 2650 10850
Wire Wire Line
	2650 10850 2200 10850
Wire Wire Line
	3100 9750 3100 9800
Wire Wire Line
	3100 10800 3100 10850
Connection ~ 3100 9750
Wire Wire Line
	3100 9750 3550 9750
Connection ~ 3100 10850
Wire Wire Line
	3100 10850 2650 10850
Wire Wire Line
	3550 9750 3550 9800
Wire Wire Line
	3550 10800 3550 10850
Connection ~ 3550 9750
Wire Wire Line
	3550 9750 4000 9750
Connection ~ 3550 10850
Wire Wire Line
	3550 10850 3100 10850
Wire Wire Line
	4000 9750 4000 9800
Wire Wire Line
	4000 10800 4000 10850
Connection ~ 4000 9750
Wire Wire Line
	4000 9750 4450 9750
Connection ~ 4000 10850
Wire Wire Line
	4000 10850 3550 10850
Wire Wire Line
	4450 9750 4450 9800
Wire Wire Line
	4450 10800 4450 10850
Connection ~ 4450 9750
Wire Wire Line
	4450 9750 4900 9750
Connection ~ 4450 10850
Wire Wire Line
	4450 10850 4000 10850
Wire Wire Line
	4900 9750 4900 9800
Wire Wire Line
	4900 10800 4900 10850
Connection ~ 4900 9750
Wire Wire Line
	4900 9750 5350 9750
Connection ~ 4900 10850
Wire Wire Line
	4900 10850 4450 10850
Wire Wire Line
	5350 9750 5350 9800
Wire Wire Line
	5350 10800 5350 10850
Connection ~ 5350 9750
Wire Wire Line
	5350 9750 5800 9750
Connection ~ 5350 10850
Wire Wire Line
	5350 10850 4900 10850
Wire Wire Line
	5800 9750 5800 9800
Wire Wire Line
	5800 10800 5800 10850
Connection ~ 5800 9750
Wire Wire Line
	5800 9750 6250 9750
Connection ~ 5800 10850
Wire Wire Line
	5800 10850 5350 10850
Wire Wire Line
	6250 9750 6250 9800
Wire Wire Line
	6250 10800 6250 10850
Connection ~ 6250 9750
Wire Wire Line
	6250 9750 6700 9750
Connection ~ 6250 10850
Wire Wire Line
	6250 10850 5800 10850
Wire Wire Line
	6700 9750 6700 9800
Wire Wire Line
	6700 10800 6700 10850
Connection ~ 6700 9750
Wire Wire Line
	6700 9750 7150 9750
Connection ~ 6700 10850
Wire Wire Line
	6700 10850 6250 10850
Wire Wire Line
	7150 9750 7150 9800
Wire Wire Line
	7150 10800 7150 10850
Connection ~ 7150 9750
Wire Wire Line
	7150 9750 7600 9750
Connection ~ 7150 10850
Wire Wire Line
	7150 10850 6700 10850
Wire Wire Line
	7600 9750 7600 9800
Wire Wire Line
	7600 10800 7600 10850
Connection ~ 7600 9750
Wire Wire Line
	7600 9750 8050 9750
Connection ~ 7600 10850
Wire Wire Line
	7600 10850 7150 10850
Wire Wire Line
	8050 9750 8050 9800
Wire Wire Line
	8050 10800 8050 10850
Connection ~ 8050 9750
Wire Wire Line
	8050 9750 8500 9750
Connection ~ 8050 10850
Wire Wire Line
	8050 10850 7600 10850
Wire Wire Line
	8500 9750 8500 9800
Wire Wire Line
	8500 10800 8500 10850
Connection ~ 8500 9750
Wire Wire Line
	8500 9750 8950 9750
Connection ~ 8500 10850
Wire Wire Line
	8500 10850 8050 10850
Wire Wire Line
	8950 10850 8950 10800
Wire Wire Line
	8950 9800 8950 9750
Connection ~ 8950 10850
Wire Wire Line
	8950 10850 8500 10850
Connection ~ 8950 9750
Wire Wire Line
	8950 9750 9400 9750
Wire Wire Line
	9400 9750 9400 9800
Wire Wire Line
	9400 10800 9400 10850
Connection ~ 9400 9750
Wire Wire Line
	9400 9750 9850 9750
Connection ~ 9400 10850
Wire Wire Line
	9400 10850 8950 10850
Wire Wire Line
	9850 9750 9850 9900
Wire Wire Line
	9850 10700 9850 10850
Connection ~ 9850 9750
Wire Wire Line
	9850 9750 10300 9750
Connection ~ 9850 10850
Wire Wire Line
	9850 10850 9400 10850
$Comp
L power:GND #PWR025
U 1 1 5ECB61D0
P 1400 10900
F 0 "#PWR025" H 1400 10650 50  0001 C CNN
F 1 "GND" H 1405 10727 50  0000 C CNN
F 2 "" H 1400 10900 50  0001 C CNN
F 3 "" H 1400 10900 50  0001 C CNN
	1    1400 10900
	1    0    0    -1  
$EndComp
$Comp
L power:+5V #PWR024
U 1 1 5ECEE3A2
P 1400 9700
F 0 "#PWR024" H 1400 9550 50  0001 C CNN
F 1 "+5V" H 1415 9873 50  0000 C CNN
F 2 "" H 1400 9700 50  0001 C CNN
F 3 "" H 1400 9700 50  0001 C CNN
	1    1400 9700
	1    0    0    -1  
$EndComp
Wire Wire Line
	1400 9700 1400 9750
Wire Wire Line
	1400 9750 1750 9750
Text Label 9100 8900 0    39   ~ 0
sum[0..7]
Text Label 6500 1450 1    50   ~ 0
data1
Text Label 6600 1450 1    50   ~ 0
data2
Text Label 6700 1450 1    50   ~ 0
data3
$Comp
L power:GND #PWR0101
U 1 1 5CEACADC
P 7100 1900
F 0 "#PWR0101" H 7100 1650 50  0001 C CNN
F 1 "GND" H 7105 1727 50  0000 C CNN
F 2 "" H 7100 1900 50  0001 C CNN
F 3 "" H 7100 1900 50  0001 C CNN
	1    7100 1900
	1    0    0    -1  
$EndComp
Text Label 1850 850  0    50   ~ 0
0V
Wire Wire Line
	1850 850  1950 850 
Wire Wire Line
	1950 850  1950 900 
$Comp
L power:GND #PWR0103
U 1 1 5CAF7A91
P 1950 900
F 0 "#PWR0103" H 1950 650 50  0001 C CNN
F 1 "GND" H 1955 727 50  0000 C CNN
F 2 "" H 1950 900 50  0001 C CNN
F 3 "" H 1950 900 50  0001 C CNN
	1    1950 900 
	1    0    0    -1  
$EndComp
Text Label 950  850  0    50   ~ 0
0V
Text Label 950  1050 0    50   ~ 0
0V
Text Label 950  1250 0    50   ~ 0
0V
Text Label 950  1350 0    50   ~ 0
~NMI
Text Label 950  1450 0    50   ~ 0
0V
Text Label 950  1550 0    50   ~ 0
~IRQ
Text Label 950  1650 0    50   ~ 0
0V
Text Label 950  1850 0    50   ~ 0
0V
Text Label 950  2050 0    50   ~ 0
0V
Text Label 950  2250 0    50   ~ 0
0V
Text Label 950  2350 0    50   ~ 0
AUDIO
Text Label 950  2450 0    50   ~ 0
0V
Text Label 950  3350 0    50   ~ 0
0V
Text Label 6800 1450 1    50   ~ 0
data4
Text Label 6900 1450 1    50   ~ 0
data5
Text Label 7000 1450 1    50   ~ 0
data6
Text Label 7100 1450 1    50   ~ 0
data7
Text Label 7450 2650 0    50   ~ 0
data7
Text Label 5850 3750 0    50   ~ 0
data7
Text Label 5850 3850 0    50   ~ 0
data6
Text Label 5850 3950 0    50   ~ 0
data4
Text Label 5850 4050 0    50   ~ 0
data2
Text Label 5850 4150 0    50   ~ 0
data0
Text Label 5850 3650 0    50   ~ 0
data5
Text Label 5850 3550 0    50   ~ 0
data3
Text Label 5850 3450 0    50   ~ 0
data1
Text Label 7450 2750 0    50   ~ 0
data5
Text Label 7450 2850 0    50   ~ 0
data3
Text Label 7450 2950 0    50   ~ 0
data1
Text Label 7450 3050 0    50   ~ 0
data0
Text Label 7450 2550 0    50   ~ 0
data6
Text Label 7450 2450 0    50   ~ 0
data4
Text Label 7450 2350 0    50   ~ 0
data2
Text Label 13750 2350 2    50   ~ 0
addr2
$Comp
L Device:C C44
U 1 1 5D8463E7
P 5950 6000
F 0 "C44" H 6250 6050 50  0000 L CNN
F 1 "220nF" H 6250 5950 50  0000 L CNN
F 2 "musix5xxx-custom:C_Rect_L7.0mm_W2.5mm_P5.00mm" H 5988 5850 50  0001 C CNN
F 3 "~" H 5950 6000 50  0001 C CNN
	1    5950 6000
	1    0    0    -1  
$EndComp
$Comp
L power:GND #PWR013
U 1 1 5D84683B
P 5950 6150
F 0 "#PWR013" H 5950 5900 50  0001 C CNN
F 1 "GND" H 5955 5977 50  0000 C CNN
F 2 "" H 5950 6150 50  0001 C CNN
F 3 "" H 5950 6150 50  0001 C CNN
	1    5950 6150
	1    0    0    -1  
$EndComp
Wire Wire Line
	5950 5850 5950 5700
Connection ~ 5950 5700
Wire Wire Line
	5950 5700 6000 5700
Wire Wire Line
	2550 4500 3300 4500
$Comp
L Jumper:Jumper_3_Bridged12 JP6
U 1 1 5DD99786
P 1950 4550
F 0 "JP6" H 1950 4754 50  0000 C CNN
F 1 "opt1" H 1950 4663 50  0000 C CNN
F 2 "musix5xxx-custom:Pin_Header_Straight_1x03_Pitch2.54mm" H 1950 4550 50  0001 C CNN
F 3 "~" H 1950 4550 50  0001 C CNN
	1    1950 4550
	-1   0    0    -1  
$EndComp
Wire Wire Line
	2200 4550 2350 4550
Entry Wire Line
	2350 4850 2450 4950
Wire Wire Line
	2350 4850 1650 4850
Wire Wire Line
	1650 4550 1700 4550
$Comp
L Jumper:Jumper_3_Bridged12 JP7
U 1 1 5E1C68C4
P 1950 5200
F 0 "JP7" H 1950 4950 50  0000 C CNN
F 1 "opt2" H 1950 5300 50  0000 C CNN
F 2 "musix5xxx-custom:Pin_Header_Straight_1x03_Pitch2.54mm" H 1950 5200 50  0001 C CNN
F 3 "~" H 1950 5200 50  0001 C CNN
	1    1950 5200
	-1   0    0    1   
$EndComp
Wire Wire Line
	1650 4850 1650 4550
Wire Wire Line
	3950 5100 3950 5150
Wire Wire Line
	3950 5150 3900 5150
Wire Wire Line
	1950 5050 3300 5050
Wire Wire Line
	1950 4700 3300 4700
Wire Wire Line
	2350 5200 2200 5200
Wire Wire Line
	2550 5250 3300 5250
Wire Wire Line
	2350 5400 1650 5400
Wire Wire Line
	1650 5400 1650 5200
Wire Wire Line
	1650 5200 1700 5200
Entry Wire Line
	2350 5200 2450 5300
$Comp
L Device:R R37
U 1 1 5E6497AC
P 3100 2450
F 0 "R37" V 3200 2500 50  0000 L CNN
F 1 "4K7*" V 3200 2300 50  0000 L CNN
F 2 "Resistors_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal" V 3030 2450 50  0001 C CNN
F 3 "~" H 3100 2450 50  0001 C CNN
	1    3100 2450
	0    1    1    0   
$EndComp
Wire Wire Line
	3250 2450 3400 2450
Text Notes 2350 3100 0    50   ~ 0
* R37 - \nAlternative wire link if \ninstalled on a BBC\nModel B\n
Text Notes 1250 5950 0    50   ~ 0
JP6 & JP7\nSet jumpers to select\naddress for M5000 or\nM3000 mode.\nLink 1-2 for M5000\nLink 2-3 for M3000
Text Label 2250 5400 0    50   ~ 0
d6
Text Label 2250 4850 0    50   ~ 0
d5
Wire Bus Line
	14600 2150 14600 4800
Wire Bus Line
	9700 4800 9700 6750
Wire Bus Line
	2250 3550 2250 4250
Wire Bus Line
	2800 7100 2800 7800
Wire Bus Line
	4550 3550 4550 4250
Wire Bus Line
	6200 3350 6200 4050
Wire Bus Line
	7200 6400 7200 8900
Wire Bus Line
	9050 6400 9050 8900
Wire Bus Line
	11450 5700 11450 8600
Wire Bus Line
	2450 4250 2450 5800
Wire Bus Line
	6500 1100 7300 1100
Wire Bus Line
	7300 1100 7300 3350
Wire Bus Line
	10550 2900 10550 4800
Wire Bus Line
	7350 4800 7350 8250
Wire Bus Line
	6100 4800 6100 7500
Wire Bus Line
	4600 7000 4600 7800
Wire Bus Line
	3150 6750 3150 7800
$EndSCHEMATC
