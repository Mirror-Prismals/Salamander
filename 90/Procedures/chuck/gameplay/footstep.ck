// Quick voxel-style footstep.
Std.rand2f(0.0, 1.0) => float r;
Noise n => BPF b => Gain g => dac.chan(0);
(620.0 + r * 240.0) => b.freq;
1.6 => b.Q;
(0.12 + r * 0.06) => g.gain;

for (0 => int i; i < 14; i++) {
    (b.freq() - 10.0) => b.freq;
    (g.gain() * 0.86) => g.gain;
    5::ms => now;
}
0.0 => g.gain;
