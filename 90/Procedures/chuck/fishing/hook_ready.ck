TriOsc t => Gain g => dac.chan(0);
0.20 => g.gain;
760.0 => t.freq;
30::ms => now;
1050.0 => t.freq;
35::ms => now;
0.0 => g.gain;
