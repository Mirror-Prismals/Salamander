// Deluxe v2 plains ambient loop for ChucK.
// Adds distant river, gust swells, and a slow day/night morph.

0.82 => float MASTER;

// Shared control signals across layers.
1.0 => float DAY_LEVEL;
0.0 => float NIGHT_LEVEL;
0.0 => float GUST_LEVEL;

fun float clamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

fun void dayNightMorph(float cycleSeconds) {
    SinOsc phase => blackhole;
    (1.0 / cycleSeconds) => phase.freq;

    while (true) {
        // Convert [-1, 1] to [0, 1], then smooth for gentler transitions.
        (phase.last() * 0.5 + 0.5) => float x;
        x * x * (3.0 - 2.0 * x) => float eased;

        eased => DAY_LEVEL;
        1.0 - eased => NIGHT_LEVEL;

        40::ms => now;
    }
}

fun void gustDirector() {
    while (true) {
        (5 + Math.random2(0, 10))::second => now;

        Math.random2f(0.25, 0.95) => float peak;
        (22 + Math.random2(0, 48)) => int riseSteps;
        (35 + Math.random2(0, 75)) => int fallSteps;

        for (0 => int i; i < riseSteps; i++) {
            ((i $ float) / riseSteps) * peak => GUST_LEVEL;
            35::ms => now;
        }

        for (0 => int i; i < fallSteps; i++) {
            peak * (1.0 - (i $ float) / fallSteps) => GUST_LEVEL;
            45::ms => now;
        }

        0.0 => GUST_LEVEL;
    }
}

fun void windBed(float master) {
    Noise n => LPF lp => HPF hp => Gain g => Pan2 p => dac;
    SinOsc colorLfo => blackhole;
    SinOsc panLfo => blackhole;

    0.16 => n.gain;
    1000.0 => lp.freq;
    0.8 => lp.Q;
    70.0 => hp.freq;
    0.0 => g.gain;

    0.018 => colorLfo.freq;
    0.006 => panLfo.freq;

    980.0 => float center;
    980.0 => float target;

    while (true) {
        if (Math.random2(0, 22) == 0) {
            target + Math.random2f(-260.0, 320.0) => target;
            clamp(target, 500.0, 1900.0) => target;
        }

        center + (target - center) * 0.02 => center;
        center + 160.0 * colorLfo.last() + 260.0 * GUST_LEVEL => lp.freq;
        60.0 + 60.0 * (colorLfo.last() * 0.5 + 0.5) + 110.0 * GUST_LEVEL => hp.freq;

        (0.014 + 0.016 * (colorLfo.last() * 0.5 + 0.5) + 0.032 * GUST_LEVEL)
            * (0.78 + 0.22 * DAY_LEVEL) * master => g.gain;

        0.2 * panLfo.last() => p.pan;
        20::ms => now;
    }
}

fun void riverBed(float master) {
    Noise n => LPF lp => HPF hp => Gain g => Pan2 p => dac;
    SinOsc drift => blackhole;
    SinOsc eddy => blackhole;
    SinOsc panLfo => blackhole;

    0.24 => n.gain;
    760.0 => lp.freq;
    1.1 => lp.Q;
    90.0 => hp.freq;
    0.0 => g.gain;
    -0.4 => p.pan;

    0.012 => drift.freq;
    0.046 => eddy.freq;
    0.004 => panLfo.freq;

    while (true) {
        700.0 + 230.0 * drift.last() + 150.0 * eddy.last() => lp.freq;
        85.0 + 35.0 * (drift.last() * 0.5 + 0.5) => hp.freq;

        (0.010 + 0.006 * (eddy.last() * 0.5 + 0.5) + 0.004 * GUST_LEVEL)
            * (0.86 + 0.14 * NIGHT_LEVEL) * master => g.gain;

        -0.45 + 0.12 * panLfo.last() => p.pan;
        20::ms => now;
    }
}

fun void leafRustles(float master) {
    Noise n => BPF bp => ADSR env => Gain g => Pan2 p => dac;

    0.38 => n.gain;
    2300.0 => bp.freq;
    5.8 => bp.Q;
    0.0 => g.gain;
    env.set(5::ms, 55::ms, 0.32, 160::ms);

    while (true) {
        Math.random2f(1400.0, 4700.0) => bp.freq;
        Math.random2f(3.0, 10.0) => bp.Q;
        Math.random2f(-0.95, 0.95) => p.pan;

        (0.011 + Math.random2f(0.0, 0.03) + 0.028 * GUST_LEVEL)
            * (0.7 + 0.3 * DAY_LEVEL) * master => g.gain;

        env.keyOn();
        (40 + Math.random2(0, 120))::ms => now;
        env.keyOff();
        (100 + Math.random2(0, 260))::ms => now;

        (320 + Math.random2(0, 1850) - ((GUST_LEVEL * 220.0) $ int)) => int waitMs;
        if (waitMs < 75) 75 => waitMs;
        waitMs::ms => now;
    }
}

fun void birdCalls(float master) {
    SinOsc b1 => Gain tone => ADSR env => Gain g => Pan2 p => dac;
    TriOsc b2 => tone;

    0.68 => b1.gain;
    0.32 => b2.gain;
    0.0 => g.gain;
    env.set(3::ms, 22::ms, 0.0, 60::ms);

    while (true) {
        if (Math.random2f(0.0, 1.0) > (0.12 + 0.78 * DAY_LEVEL)) {
            (1000 + Math.random2(0, 7000))::ms => now;
            continue;
        }

        Math.random2f(-0.85, 0.85) => p.pan;
        Math.random2f(0.008, 0.028) * (0.2 + 0.8 * DAY_LEVEL) * master => g.gain;

        Math.random2(2, 5) => int chirps;
        for (0 => int i; i < chirps; i++) {
            Math.random2f(1400.0, 2800.0) => float startF;
            Math.random2f(2600.0, 4700.0) => float peakF;
            Math.random2f(1000.0, 2100.0) => float endF;
            (45 + Math.random2(0, 85)) => int dMs;

            env.keyOn();
            for (0 => int t; t < dMs; t++) {
                (t $ float) / dMs => float x;
                if (x < 0.33) {
                    startF + (peakF - startF) * (x / 0.33) => b1.freq;
                } else {
                    peakF + (endF - peakF) * ((x - 0.33) / 0.67) => b1.freq;
                }
                b1.freq() * 1.98 => b2.freq;
                1::ms => now;
            }
            env.keyOff();
            (15 + Math.random2(0, 80))::ms => now;
        }

        (1600 + Math.random2(0, 6200))::ms => now;
    }
}

fun void nightInsects(float master) {
    SinOsc c1 => Gain tone => ADSR env => Gain g => Pan2 p => dac;
    SawOsc c2 => tone;

    0.6 => c1.gain;
    0.18 => c2.gain;
    env.set(2::ms, 10::ms, 0.0, 20::ms);
    0.0 => g.gain;

    while (true) {
        if (NIGHT_LEVEL < 0.12) {
            250::ms => now;
            continue;
        }

        Math.random2f(-0.9, 0.9) => p.pan;
        Math.random2f(2200.0, 4200.0) => c1.freq;
        c1.freq() * 2.02 => c2.freq;
        (0.0015 + 0.011 * NIGHT_LEVEL) * master => g.gain;

        Math.random2(3, 8) => int ticks;
        for (0 => int i; i < ticks; i++) {
            env.keyOn();
            (12 + Math.random2(0, 20))::ms => now;
            env.keyOff();
            (35 + Math.random2(0, 95))::ms => now;
        }

        (280 + Math.random2(0, 1800) - ((NIGHT_LEVEL * 180.0) $ int)) => int waitMs;
        if (waitMs < 120) 120 => waitMs;
        waitMs::ms => now;
    }
}

fun void heatShimmer(float master) {
    TriOsc t => LPF lp => Gain g => Pan2 p => dac;
    SinOsc ampLfo => blackhole;
    SinOsc freqLfo => blackhole;
    SinOsc panLfo => blackhole;

    0.85 => t.gain;
    1700.0 => lp.freq;
    1.25 => lp.Q;
    0.0 => g.gain;

    0.09 => ampLfo.freq;
    0.018 => freqLfo.freq;
    0.01 => panLfo.freq;

    while (true) {
        160.0 + 45.0 * freqLfo.last() => t.freq;
        1450.0 + 420.0 * (freqLfo.last() * 0.5 + 0.5) => lp.freq;
        (0.0008 + 0.0058 * DAY_LEVEL * (ampLfo.last() * 0.5 + 0.5)) * master => g.gain;
        0.25 * panLfo.last() => p.pan;
        15::ms => now;
    }
}

// Around 4.5 minutes for a full day->night->day cycle.
spork ~ dayNightMorph(270.0);
spork ~ gustDirector();
spork ~ windBed(MASTER);
spork ~ riverBed(MASTER);
spork ~ leafRustles(MASTER);
spork ~ birdCalls(MASTER);
spork ~ nightInsects(MASTER);
spork ~ heatShimmer(MASTER);

while (true) {
    1::second => now;
}
