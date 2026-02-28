SawOsc s => LPF lp => Gain g => dac.chan(0);
0.20 => g.gain;
1500.0 => lp.freq;

[700.0, 540.0, 390.0] @=> float notes[];
for (0 => int i; i < 3; i++) {
    notes[i] => s.freq;
    (1500.0 - i * 260.0) => lp.freq;
    62::ms => now;
}
0.0 => g.gain;
