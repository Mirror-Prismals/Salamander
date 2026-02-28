// Low woody crack + rumble for tree felling.
Noise n => LPF low => Gain gLow => dac.chan(0);
Noise n2 => BPF crack => Gain gCrack => dac.chan(0);

180.0 => low.freq;
0.20 => gLow.gain;
980.0 => crack.freq;
2.2 => crack.Q;
0.16 => gCrack.gain;

for (0 => int i; i < 48; i++) {
    (180.0 - i * 1.6) => low.freq;
    (980.0 - i * 9.5) => crack.freq;
    (0.20 - i * 0.0035) => gLow.gain;
    (0.16 - i * 0.0030) => gCrack.gain;
    8::ms => now;
}
0.0 => gLow.gain;
0.0 => gCrack.gain;
