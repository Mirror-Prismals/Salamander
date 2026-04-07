SinOsc s => Gain g => dac.chan(0);
0.18 => g.gain;
980.0 => s.freq;
40::ms => now;
0.0 => g.gain;
20::ms => now;
0.14 => g.gain;
1120.0 => s.freq;
45::ms => now;
