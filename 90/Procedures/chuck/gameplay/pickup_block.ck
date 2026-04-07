// Bright pickup chirp.
SinOsc s => Gain g => dac.chan(0);
0.20 => g.gain;

[720.0, 940.0, 1180.0] @=> float notes[];
for (0 => int i; i < notes.size(); i++) {
    notes[i] => s.freq;
    28::ms => now;
}
0.0 => g.gain;
