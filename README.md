# pdsynth

Casio CZ phase distortion, in software.

Phase distortion is not FM and it is not subtractive, though it was sold in
1984 against machines that were both. A CZ reads one sine table and bends the
phase on the way in. The phase of a plain oscillator climbs from 0 to 1 at a
constant rate; bend that climb so it rushes through part of the cycle and
crawls through the rest, and the sine that comes out has harmonics. How far it
is bent is a single number, and on the hardware that number comes from an eight
step envelope, which is why a CZ sweeps the way it does while owning no filter
at all.

## Listen first

```
cmake -B build && cmake --build build
./build/pd_render demo.wav
```

Six demonstrations: the sweep the machine is known for, the resonant waveforms
whose formant climbs while the pitch stays put, two detuned lines, an eight
step envelope doing a double attack that no ADSR can express, ring modulation,
and noise modulation.

## What is here

**Eight waveforms**, the set a CZ-101 lists on its panel.

| | |
|---|---|
| saw, square, pulse, double sine, saw pulse | a bent phase, one sine table |
| reso saw, reso triangle, reso trapezoid | a sine at a multiple of the note, under a falling window |

Every one collapses to a pure sine when nothing is bent, which is what the
hardware does with its DCW envelope at zero, and every one brightens as it
bends. The sawtooth carries a falling harmonic series, the square carries odd
harmonics and no even ones, and the resonant three sweep a formant from the
third harmonic to the thirteenth without moving the pitch.

**Eight step envelopes**, three per line, on the pitch, the waveform and the
level. Each step is a rate and a level, with one step marked as the sustain and
one as the end. This is not an ADSR with extra stages: it can do an ADSR, and
it can also do a double attack, a swell that pauses, or a decay that stops
halfway down and starts again, none of which an ADSR can express. The envelope
on the waveform is why no filter is needed.

**Two lines per voice**, each an oscillator with its own three envelopes,
detunable against each other, which is most of what the machine is remembered
for.

**Ring and noise modulation.** The originals had both. The 2026 hardware
reissue has neither. Noise here shakes how far the phase is bent rather than
how loud the line is: shaking the amplitude hollows the note out, and at full
depth removes most of what you were playing.

## Testing

```
ctest --test-dir build
```

The oscillator is judged by its spectrum, because that is the only thing about
an oscillator a listener can hear. The envelope is judged by where it is at a
given moment, because an envelope that reaches the right levels at the wrong
time is a different instrument. The voice is judged on pitch, detuning,
velocity, and on whether the waveform envelope does the work a filter would do
elsewhere.

## Still to come

CV and gate, so it can sit in a modular rig. A SysEx import that knows what a
given piece of hardware supports and turns off what it does not, and an export
that declines to send parameters the target cannot receive. Aftertouch. A
multimode filter, glide, and more than two lines stacked, all of which are
things the hardware could not do and software has no reason not to.

## License

GPL-2.0-or-later.
