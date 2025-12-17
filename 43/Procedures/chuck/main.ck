// Aurora debug loop - ChucK 1.5.4.4 (chai)
// Adds startup prints and keeps main alive so you can confirm shreds are running.
// Paste and spork this file; look for the <<< messages in the console.

// Master bus + reverb
Gain master => JCRev rev => dac;
0.30 => master.gain;    // slightly up for debugging
0.18 => rev.mix;

// simple console marker that main started
<<< "aurora: main shred started" >>>;

// ---- Low sub-rumble (slow evolving) ----
SinOsc lowOsc => LPF lowLP => Gain lowG => master;
40 => lowOsc.freq;
600 => lowLP.freq;
0.025 => lowG.gain;     // a touch louder so you can hear it

fun void lowMorph()
{
    <<< "lowMorph: started" >>>;
    while( true )
    {
        Math.random2f(30.0, 70.0) => float f;
        f => lowOsc.freq;
        Math.random2f(2.0, 6.0) => float waitSec;
        waitSec::second => now;
    }
}
spork ~ lowMorph();

// ---- Airy breath / pad (filtered noise + slow cutoff morph) ----
Noise padNoise => LPF padLP => Gain padG => Pan2 padPan => master;
0.01 => padG.gain;       // slightly higher for debug
1800 => padLP.freq;
0.0 => padPan.pan;

fun void padMorph()
{
    <<< "padMorph: started" >>>;
    while(true)
    {
        Math.random2f(1000.0, 3500.0) => float target;
        Math.random2f(6.0, 20.0) => float durSec;
        padLP.freq() => float start;
        0.0 => float elapsed;
        while( elapsed < durSec )
        {
            ( (1.0 - (elapsed / durSec)) * start + (elapsed / durSec) * target ) => float nextf;
            nextf => padLP.freq;
            50::ms => now;
            elapsed + 0.05 => elapsed;
        }
        target => padLP.freq;
        Math.random2f(2.0, 8.0) :: second => now;
    }
}
spork ~ padMorph();

// ---- Sparse, bell-like distant swells ----
fun void swells()
{
    <<< "swells: started" >>>;
    while( true )
    {
        Math.random2f(6.0, 18.0) :: second => now;
        SinOsc s => BPF bp => Gain g => Pan2 p => master;
        Math.random2f(300.0, 1200.0) => bp.freq;
        4.0 => bp.Q;
        Math.random2f(-0.8, 0.8) => p.pan;
        0.015 => g.gain;    // small but audible
        0.015 => float gg;
        gg => g.gain;
        for(0 => int i; i < 12; i++)
        {
            gg * 0.85 => gg;
            gg => g.gain;
            60::ms => now;
        }
        // let locals drop out and GC do its job
    }
}
spork ~ swells();

// ---- Crackle / static layer: short filtered noise bursts ----
fun void crackle()
{
    <<< "crackle: started" >>>;
    while( true )
    {
        Math.random2f(0.05, 0.6) => float waitSec;
        waitSec::second => now;

        Noise n => HPF hpf => Gain g => Pan2 p => master;
        Math.random2f(800.0, 5000.0) => hpf.freq;
        Math.random2f(-0.9, 0.9) => p.pan;

        0.002 => float gg;         // slightly louder
        gg => g.gain;
        10::ms => now;

        Math.random2(1, 4) => int bursts;
        for(0 => int i; i < bursts; i++)
        {
            Math.random2f(0.6, 0.95) * gg => gg;
            gg => g.gain;
            Math.random2f(8.0, 60.0) => float msWait;
            msWait::ms => now;
        }
        0.0 => g.gain;
        // connections go out of scope
    }
}
spork ~ crackle();

// ---- Micro-pop occasionals (very sparse) ----
fun void micropop()
{
    <<< "micropop: started" >>>;
    while(true)
    {
        Math.random2f(12.0, 45.0) :: second => now;
        SinOsc b => HPF h => Gain g => Pan2 p => master;
        Math.random2f(500.0, 2000.0) => b.freq;
        Math.random2f(800.0, 2500.0) => h.freq;
        Math.random2f(-0.6, 0.6) => p.pan;
        0.03 => g.gain;      // small but audible
        15::ms => now;
        0.012 => g.gain;
        20::ms => now;
        0.006 => g.gain;
        20::ms => now;
        0.0 => g.gain;
    }
}
spork ~ micropop();

// ---- gentle stereo wander for pad ----
0.0 => float t;
fun void stereoDrift()
{
    <<< "stereoDrift: started" >>>;
    while(true)
    {
        Math.sin(t) * 0.35 => float pp;
        pp => padPan.pan;
        300::ms => now;
        (t + 0.018) => t;
    }
}
spork ~ stereoDrift();

// keep main alive so you can observe console output and hear the loop
while( true ) { 1::second => now; }
