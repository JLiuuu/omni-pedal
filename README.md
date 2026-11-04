# Omni Pedal

Software for the fall proposal: dry bypass, overdrive, distortion, delay, chorus, reverb, and Bass Split. Effect changes crossfade for about 10 ms. The output stays muted for 3 seconds after power-up.

Knob and footswitch reads are written. They run on a Raspberry Pi with an ADS7830 and a switch. On a PC, listen to the wav files in `out/`.

## On this computer

After Cygwin g++ and make are installed, double-click `run.bat`, or from the project directory:

```bash
make test
make demo
```

`make test` prints `all passed` when the checks succeed.

`make demo` writes listening files in `out/`:

- `input.wav`, `output.wav`, `output_no_mute.wav`: dry signal and the 3 second mute
- `overdrive.wav`, `distortion.wav`, `delay.wav`, `chorus.wav`, `reverb.wav`
- `bass_low.wav`: 110 Hz with the octave below. `bass_high.wav`: 330 Hz, left dry
- `switch.wav`: one effect change per second

Process a recording:

```bash
./stage1 --effect overdrive --drive 0.6 my_guitar.wav out/processed.wav
./stage1 --effect bass --time 0.6 --mix 0.8 my_guitar.wav out/bass.wav
```

For Bass Split, `--time` is the split frequency: 0 is about 80 Hz and 1 is about 250 Hz.

## On the Raspberry Pi

Use Codec Zero AUX IN and AUX OUT. Set the codec for line input and line output. The onboard microphone jack is the one with mic bias.

```bash
sudo apt install g++ make libasound2-dev
make
sudo chrt -f 60 ./stage1 --live hw:CodecZero --gpio 17 --i2c 1
```

Without the knobs attached, type `effect overdrive` or `next` in that terminal. The footswitch uses GPIO 17, leaving the Codec Zero pins 2, 3, 18, 19, 20, 21, 23, 24, and 27 free. Five knobs go to ADS7830 channels 0 through 4 at address `0x48`, in the order drive, tone, mix, time, level.

If the device name is different, run `arecord -l` and pass that name, for example `./stage1 --live hw:0`.

The program prints the period it actually opened. If the software buffer upper bound reaches 5 ms or more, the terminal prints a warning.
