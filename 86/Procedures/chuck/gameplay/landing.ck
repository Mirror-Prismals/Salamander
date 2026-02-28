// Ground-landing thud for jump/fall impacts.
Noise body => LPF low => Gain gLow => dac.chan(0);
SinOsc tone => LPF toneLp => Gain gTone => dac.chan(0);

190.0 => low.freq;
0.24 => gLow.gain;
82.0 => tone.freq;
220.0 => toneLp.freq;
0.12 => gTone.gain;

for (0 => int i; i < 26; i++) {
    (190.0 - i * 3.5) => low.freq;
    (220.0 - i * 2.8) => toneLp.freq;
    (0.24 - i * 0.0082) => gLow.gain;
    (0.12 - i * 0.0042) => gTone.gain;
    6::ms => now;
}

0.0 => gLow.gain;
0.0 => gTone.gain;
