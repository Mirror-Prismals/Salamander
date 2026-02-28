// Soft placement thud.
Noise n => LPF lp => Gain g => dac.chan(0);
460.0 => lp.freq;
0.25 => g.gain;

for (0 => int i; i < 16; i++) {
    (460.0 - i * 14.0) => lp.freq;
    (0.25 - i * 0.014) => g.gain;
    5::ms => now;
}
0.0 => g.gain;
