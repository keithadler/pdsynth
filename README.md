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

## Where it is

The oscillator: all eight waveforms of a CZ-101, checked by their spectra
rather than by eye.

| | |
|---|---|
| saw, square, pulse, double sine, saw pulse | a bent phase, one sine table |
| reso saw, reso triangle, reso trapezoid | a sine at a multiple of the note, under a falling window |

Every waveform collapses to a pure sine when nothing is bent, which is what the
hardware does when its DCW envelope sits at zero, and every one brightens as it
bends. The sawtooth carries a proper harmonic series, the square carries odd
harmonics and no even ones, and the resonant three sweep a formant from the
third harmonic to the thirteenth while the pitch stays where it was.

## Building

```
cmake -B build && cmake --build build && ctest --test-dir build
```
