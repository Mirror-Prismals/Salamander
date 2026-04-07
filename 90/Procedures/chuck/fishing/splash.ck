Noise n => LPF lp => Gain g => dac.chan(0);
0.36 => g.gain;
1250.0 => lp.freq;

for (0 => int i; i < 26; i++) {
    (1250.0 - i * 32.0) => lp.freq;
    (0.36 - i * 0.012) => g.gain;
    6::ms => now;
}
