Noise n => BPF b => Gain g => dac.chan(0);
0.33 => g.gain;
820.0 => b.freq;
2.6 => b.Q;

for (0 => int i; i < 22; i++) {
    (820.0 + i * 21.0) => b.freq;
    (0.33 - i * 0.011) => g.gain;
    5::ms => now;
}
