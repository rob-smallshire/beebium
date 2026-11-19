# Routing another program's audio into the cassette deck

The cassette deck's live input takes any audio input the host offers (client specification 9.0,
"Live input"). The obvious source is a real cassette player on a USB audio interface, but the same
path loads a tape recording that another program is playing, which is how the live path is tested
without a player, and how a WAV in a format Beebium does not read can still be loaded. Operating
systems do not route one program's output to another's input on their own; a virtual audio device
does it. This page records the procedure that works, and the traps found on the way, as the
basis for the user documentation to come.

## macOS: BlackHole and a Multi-Output Device

[BlackHole](https://github.com/ExistentialAudio/BlackHole) is a free, open-source virtual audio
driver: whatever is played to its output appears at its input. `BlackHole 2ch` is enough.

1. **Install BlackHole 2ch** (Homebrew: `brew install blackhole-2ch`, or the project's installer).
   It appears in Audio MIDI Setup as a device with two input and two output channels.
2. **Set its volume and rate.** In Audio MIDI Setup select BlackHole 2ch. On the Output tab drag
   the Master volume to the right (1.0); do the same on the Input tab. BlackHole ships with
   these at half scale, which is about 30 dB of attenuation: the meter in Beebium then reads a
   few pixels for a tape that is perfectly loadable. On the same tab set the Format to 48,000 Hz
   (or the rate of the files to be played). A device left at 8,000 Hz puts Nyquist at 4 kHz, on
   top of the front end's 4.13 kHz filter, and the 2400 Hz tone loses its edges.
3. **Hear it too (optional).** Click + at the bottom left of Audio MIDI Setup, Create
   Multi-Output Device, and tick BlackHole 2ch and the speakers or headphones. Set its sample
   rate to the same 48,000 Hz; if it will not change, set each sub-device first and recreate it.
   A Multi-Output Device has no volume control of its own; the level is the file's.
4. **Point the player at it.** System Settings, Sound, Output: choose the Multi-Output Device
   (or BlackHole 2ch directly, to play silently). Any program that plays to the system output now
   feeds BlackHole; Audacity, VLC and the like can also pick BlackHole as their own output
   without touching the system setting.
5. **In Beebium**, with the deck empty: the folder menu, Input Device, BlackHole 2ch. The first
   time, macOS asks whether Beebium may use the microphone; the dialog's text explains that the
   input is played as a live cassette tape. The live tape appears in the deck; press Play.
6. **On the Beeb**: `*TAPE` then `CHAIN ""` (or `*RUN`, or `CHAIN "NAME"` to search for a file).
7. **Start the recording** in the player, for example from a shell:

   ```
   afplay recording.wav
   ```

   The deck's counter runs while samples arrive. The level meter's held peak should sit near
   its tick with the red clip mark absent; set the player's or the file's level accordingly.
   Loading is at real speed: a game's title screen comes up when the tape would have got there.

### Reminders

- **Pick the device again after any change.** The stream is opened at the device's rate and
  channels at the moment it is picked. Changing BlackHole's rate or channel count, or the
  Multi-Output Device, while a live tape is in the deck ends the stream: the client cancels it,
  the server ejects the live tape, and the deck says "The input device changed while the live
  tape was playing; choose it again from Input Device." There is nothing to eject; choose the
  device again. Unplugging the device gives "The input device was disconnected while the live
  tape was playing."
- **Feed it at real speed.** A live load has no resampling or clock recovery. If the deck says
  the input is not keeping up with the Beeb, or arriving faster than the Beeb can take it, for
  three seconds running, the host is starving or flooding the stream: a player running slow or
  fast, or a machine too loaded to run the emulation at real speed.
- **Audio-routing utilities** that insert themselves as the system output (per-application
  volume tools and the like) sit between the player and BlackHole and apply their own gain.
  Either quit them or route the player through them to BlackHole explicitly.
- **The corpus recordings carry their signal on the left channel**, which is the channel the
  macOS app always takes from a stereo device, and meters. A stereo file whose signal is on the
  right needs a player that can swap channels, or a mono mix; the Linux app offers either
  channel from its menu instead.
- **The server may be on another machine.** The live input is captured on the client's host
  and streamed, so it is offered whether or not the server shares the client's filesystem.
- **Measuring what BlackHole receives**, when the meter and the ear disagree, from a shell with
  ffmpeg installed:

  ```
  ffmpeg -f avfoundation -i ":BlackHole 2ch" -t 5 capture.wav
  ffmpeg -i capture.wav -af astats=measure_perchannel=Peak_level -f null -
  ```

  A file that peaks at -2 dBFS should arrive within a decibel or two of that. The first
  capture makes macOS ask whether the terminal may use the microphone, for the same reason
  Beebium is asked.

## Linux: a PipeWire or PulseAudio monitor

On a PipeWire or PulseAudio desktop every output device has a *monitor* source that carries
what is being played to it, so no extra driver is needed: in the client's Input Device menu
(which the Linux app labels Live Input in 0.20.1) choose "Monitor of <the output device>", then
play the recording with any player to that
output. `pavucontrol` (or the desktop's sound settings) shows the monitor sources and lets the
player be moved between outputs. A null sink (`pactl load-module module-null-sink
sink_name=tape`) gives a silent route: play to `tape`, capture `Monitor of tape`. This is the
Linux client's route; not yet exercised end to end.

## Windows

No client yet. For the record: the Windows equivalent is a virtual cable such as VB-Audio
Virtual Cable, or the sound device's "Stereo Mix" recording source where the driver offers it.
