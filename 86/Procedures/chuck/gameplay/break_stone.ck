// Short pickaxe-on-stone style tick.
Noise n => BPF body => Gain g => dac.chan(0);
Noise n2 => HPF click => Gain g2 => dac.chan(0);

1300.0 => body.freq;
3.2 => body.Q;
0.30 => g.gain;
5200.0 => click.freq;
0.14 => g2.gain;

for (0 => int i; i < 18; i++) {
    (1300.0 - i * 36.0) => body.freq;
    (0.30 - i * 0.015) => g.gain;
    (0.14 - i * 0.007) => g2.gain;
    4::ms => now;
}
0.0 => g.gain;
0.0 => g2.gain;
