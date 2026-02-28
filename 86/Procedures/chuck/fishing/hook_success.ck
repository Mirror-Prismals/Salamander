SinOsc s => Gain g => dac.chan(0);
0.22 => g.gain;

[520.0, 780.0, 1040.0] @=> float notes[];
for (0 => int i; i < 3; i++) {
    notes[i] => s.freq;
    56::ms => now;
}
0.0 => g.gain;
