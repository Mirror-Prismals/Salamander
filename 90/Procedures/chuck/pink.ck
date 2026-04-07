// Pink noise placeholder for noise bus; routed to channel 0
Noise n => OnePole p => dac.chan(0);
0.05 => p.pole; // low-pass to soften
0.1 => n.gain;
while( true ) { 10::ms => now; }
