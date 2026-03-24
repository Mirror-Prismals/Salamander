SinOsc tone => LPF lp => Gain g => dac.chan(0);
0.32 => g.gain;
0.45 => tone.gain;
700.0 => lp.freq;

for (0 => int i; i < 28; i++) {
    (160.0 + (i * 13.5)) => tone.freq;
    (900.0 + (i * 70.0)) => lp.freq;
    (0.22 + (i $ float) / 120.0) => g.gain;
    5::ms => now;
}
