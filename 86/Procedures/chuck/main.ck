// Player-head ambient source.
// Distinct from main.ck so the head-following ray source has its own feed.
TriOsc tone => LPF lp => Gain master => dac.chan(2);
0.08 => master.gain;
0.35 => tone.gain;
1200 => lp.freq;
2.0 => lp.Q;

SinOsc slow => blackhole;
0.07 => slow.freq;

while (true) {
    // Slow drift to keep motion in the source without harsh transients.
    (260.0 + (slow.last() * 90.0)) => tone.freq;
    (1200.0 + (slow.last() * 450.0)) => lp.freq;
    10::ms => now;
}
