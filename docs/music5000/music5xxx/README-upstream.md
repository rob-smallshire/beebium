# music5xxx
## Hybrid Music 5000/3000 BBC Microcomputer synthesiser clone PCB 

![music5xxx PCB render](https://github.com/jlbeebprojects/music5xxx/raw/master/images/m5xxx-box.jpg)

This is a hardware clone of the Hybrid Music 5000 add-on for the BBC Microcomputer model B and Master 128. It was designed by Chris Jordan in 1984 as the Acorn Music500, and became part of quite an advanced (for its time) music recording and programming system. It was available for sale until at least 1992. Second hand models appear from time-to-time on eBay, but appear quite rare.

The unit connects to the BBC Microcomputer's 1Mhz bus, and implements an 8 voice, 16 channel, 8 bit stereo wavetable-style synthesizer. It uses a companding DAC to increase the fidelity of using 8 bit waveforms. The PCB can be configured as an expander, allowing two to be connected to the computer, doubling the available voices and channels.

## The clone

![music5xxx PCB render](https://github.com/jlbeebprojects/music5xxx/raw/master/images/music5xxx.png)

It is designed in Kicad, using the original schematic and photographs of the original unit. PCBs have been made, and tested using a BBC Microcomputer Model B and BBC Master 128.

Currently all the 74xx series logic ICs are available new. Two 9128-10 RAM ICs, and the AM6070DC Companding DAC are no longer manufactured, but are available as NOS from Utsource https://www.utsource.net/ and perhaps other places.

Some alterations were made compared to the original unit:

* The power supply circuit has been changed for safety reasons, as the original unit used an on-board mains transformer. This is replaced by a 15V AC-AC plug-top adapter, and the +15V/-15V and 5V DC rails are generated from the 15V AC input. A LM7805 compatible switching DC-DC converter is used for the 5V rail, and saves having to use a heatsink if a linear adapter was used.

* Originally, the Music5000 used 74LS series logic ICs - experimentation has shown that 74HCT and 74HC series logic ICs can be used and work well with the circuit, with the advantage that they are more power efficient and run much cooler.

* The audio output DIN socket has been replaced with a 3.5mm stereo socket for convenience.

* The PCB can be configured using jumpers to work both as a Music5000 and Music3000 expander, allowing two PCBs to be stacked together sharing the same power supply.

* The low-pass filter in the output stage has been altered to not be so aggressive, possibly at the expense of unmasking some digital hash when using some instruments.

## Resources

* A comprehensive archive about the Hybrid Music System: https://www.retro-kit.co.uk/page.cfm/content/Hybrid-Music-5000-Synthesiser/

* The Stardot 8bit hardware forum often has posts about the Hybrid Music system: https://stardot.org.uk/forums/viewforum.php?f=3

* Here's the Stardot thread about this project: https://stardot.org.uk/forums/viewtopic.php?f=3&t=16895

## Other options

Instead of building one of these, you could use the emulated version that's included with Beebem: https://github.com/stardot/beebem-windows

Or if you want to use a real BBC Microcomputer you could make one of these:

* Hoglet's 1Mhz Bus->FPGA adapter which implements the Music5000/3000 in an FPGA module: https://stardot.org.uk/forums/viewtopic.php?f=3&t=16643

* dp11's 1Mhz Bus->Raspberry Pi adapter which emulates the Music5000 on a Raspberry Pi: https://stardot.org.uk/forums/viewtopic.php?f=3&t=15848

## Licence

Unless otherwise marked, work in this repository is licenced under [Creative Commons Attribution-ShareAlike 4.0 International License](http://creativecommons.org/licenses/by-sa/4.0/)  ![Creative Commons Licence](https://i.creativecommons.org/l/by-sa/4.0/88x31.png)

