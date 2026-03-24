// Plains biome ambient loop for ChucK.
// Layers: wind, leaves, birds, and subtle warm-air shimmer.

0.85 => float MASTER;

fun float clamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

fun void windBed(float master) {
    Noise n => LPF lp => Gain g => Pan2 p => dac;

    0.15 => n.gain;
    900.0 => lp.freq;
    0.6 => lp.Q;
    0.03 * master => g.gain;
    0.05 => p.pan;

    SinOsc swell => blackhole;
    0.021 => swell.freq;

    900.0 => float center;
    900.0 => float target;

    while (true) {
        if (Math.random2(0, 17) == 0) {
            target + Math.random2f(-180.0, 220.0) => target;
            clamp(target, 500.0, 1500.0) => target;
        }

        center + (target - center) * 0.02 => center;
        center + (180.0 * swell.last()) => lp.freq;

        (0.02 + (0.02 * ((swell.last() * 0.5) + 0.5))) * master => g.gain;
        20::ms => now;
    }
}

fun void leafRustles(float master) {
    Noise n => BPF bp => ADSR env => Gain g => Pan2 p => dac;

    0.35 => n.gain;
    2200.0 => bp.freq;
    5.0 => bp.Q;
    0.0 => g.gain;
    0.0 => p.pan;

    env.set(8::ms, 70::ms, 0.35, 180::ms);

    while (true) {
        Math.random2f(1300.0, 4200.0) => bp.freq;
        Math.random2f(3.0, 9.0) => bp.Q;
        Math.random2f(-0.9, 0.9) => p.pan;
        Math.random2f(0.015, 0.05) * master => g.gain;

        env.keyOn();
        (60 + Math.random2(0, 160))::ms => now;
        env.keyOff();
        (120 + Math.random2(50, 420))::ms => now;

        (420 + Math.random2(0, 2200))::ms => now;
    }
}

fun void birdCalls(float master) {
    SinOsc bird => ADSR env => Gain g => Pan2 p => dac;

    0.0 => g.gain;
    env.set(4::ms, 30::ms, 0.0, 70::ms);

    while (true) {
        Math.random2f(-0.8, 0.8) => p.pan;
        Math.random2f(0.01, 0.03) * master => g.gain;

        Math.random2(2, 5) => int chirps;

        for (0 => int i; i < chirps; i++) {
            Math.random2f(1300.0, 2500.0) => float startF;
            Math.random2f(2500.0, 4200.0) => float peakF;
            Math.random2f(900.0, 1800.0) => float endF;
            (55 + Math.random2(0, 80)) => int dMs;

            env.keyOn();

            for (0 => int t; t < dMs; t++) {
                (t $ float) / dMs => float x;
                if (x < 0.35) {
                    startF + (peakF - startF) * (x / 0.35) => bird.freq;
                } else {
                    peakF + (endF - peakF) * ((x - 0.35) / 0.65) => bird.freq;
                }
                1::ms => now;
            }

            env.keyOff();
            (15 + Math.random2(0, 80))::ms => now;
        }

        (2400 + Math.random2(0, 10000))::ms => now;
    }
}

fun void heatShimmer(float master) {
    TriOsc t => LPF lp => Gain g => Pan2 p => dac;
    SinOsc ampLfo => blackhole;
    SinOsc freqLfo => blackhole;
    SinOsc panLfo => blackhole;

    0.9 => t.gain;
    1800.0 => lp.freq;
    1.2 => lp.Q;

    0.11 => ampLfo.freq;
    0.018 => freqLfo.freq;
    0.009 => panLfo.freq;

    while (true) {
        (170.0 + (40.0 * freqLfo.last())) => t.freq;
        (1500.0 + (400.0 * (freqLfo.last() * 0.5 + 0.5))) => lp.freq;
        (0.003 + (0.004 * (ampLfo.last() * 0.5 + 0.5))) * master => g.gain;
        0.25 * panLfo.last() => p.pan;
        15::ms => now;
    }
}

spork ~ windBed(MASTER);
spork ~ leafRustles(MASTER);
spork ~ birdCalls(MASTER);
spork ~ heatShimmer(MASTER);

while (true) {
    1::second => now;
}
