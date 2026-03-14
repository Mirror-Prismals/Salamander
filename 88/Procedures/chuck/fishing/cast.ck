Noise n => BPF b => Gain g => dac.chan(0);
0.28 => g.gain;
950.0 => b.freq;
1.4 => b.Q;

for (0 => int i; i < 18; i++) {
    (1200.0 - i * 45.0) => b.freq;
    (0.30 - i * 0.014) => g.gain;
    4::ms => now;
}
