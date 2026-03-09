// Clear short cue for full reel-in completion.
TriOsc t => LPF lp => Gain g => dac.chan(0);
0.18 => g.gain;
0.42 => t.gain;
2200.0 => lp.freq;

[860.0, 1140.0, 920.0] @=> float notes[];
for (0 => int i; i < notes.size(); i++) {
    notes[i] => t.freq;
    (2200.0 - i * 280.0) => lp.freq;
    45::ms => now;
}
0.0 => g.gain;
