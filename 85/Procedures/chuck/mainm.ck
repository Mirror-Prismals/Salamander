// plains_ambient_loop_v2_deluxe.ck
// Deluxe outdoor/plains ambient loop.
// Mood: emerald grass, blue sky, gentle tree motion, warm-cool spring/summer blend.
// Runs forever; stop with Ctrl-C.

// ---------- Tweak Knobs ----------
0.36 => float MASTER_GAIN;
0.070 => float WIND_BASE_MAX;
0.052 => float GUST_MAX;
0.082 => float LEAF_MAX;
0.075 => float BIRD_MAX;
0.015 => float INSECT_MAX;
0.010 => float SKY_HUSH_MAX;

Gain master => dac;
MASTER_GAIN => master.gain;

// Slow macro-state shared across layers.
0.50 => float gWarmth;   // 0 cool breeze -> 1 warm sun
0.50 => float gBreeze;   // 0 calm -> 1 active wind
0.50 => float gLife;     // wildlife activity bias

fun float randf(float lo, float hi)
{
    return Math.random2f(lo, hi);
}

fun float clamp(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

fun float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

fun void macroCycle()
{
    SinOsc dayLfo => blackhole;
    SinOsc weather => blackhole;
    SinOsc life => blackhole;

    // Multi-minute motion so the loop does not feel static.
    (1.0 / 210.0) => dayLfo.freq;
    (1.0 / 77.0) => weather.freq;
    (1.0 / 131.0) => life.freq;

    0.55 => float drift;

    while (true)
    {
        clamp(drift + randf(-0.045, 0.045), 0.14, 0.94) => drift;

        (dayLfo.last() * 0.5 + 0.5) => float d;
        (weather.last() * 0.5 + 0.5) => float w;
        (life.last() * 0.5 + 0.5) => float l;

        clamp(0.30 + 0.70 * d, 0.0, 1.0) => gWarmth;
        clamp(0.15 + 0.55 * w + 0.30 * drift, 0.0, 1.0) => gBreeze;
        clamp(0.18 + 0.52 * d + 0.22 * l + 0.08 * w, 0.0, 1.0) => gLife;

        260::ms => now;
    }
}

fun void windBed()
{
    Noise low => LPF lowLP => HPF lowHP => Gain lowG => Pan2 lowP => master;
    Noise airy => HPF airyHP => LPF airyLP => Gain airyG => Pan2 airyP => master;

    420.0 => lowLP.freq;
    120.0 => lowHP.freq;
    2400.0 => airyHP.freq;
    7300.0 => airyLP.freq;

    0.040 => lowG.gain;
    0.016 => airyG.gain;

    while (true)
    {
        gWarmth => float warm;
        gBreeze => float breeze;

        randf(260.0 + breeze * 120.0, 690.0 + warm * 370.0) => float targetLowCut;
        randf(0.030, WIND_BASE_MAX) * (0.58 + breeze * 0.56) => float targetLowGain;
        randf(2200.0 + warm * 1200.0, 7000.0 + breeze * 2600.0) => float targetAirCut;
        randf(0.0045, 0.020) * (0.40 + warm * 0.90) => float targetAirGain;

        randf(-0.60, 0.60) => float targetPanA;
        randf(-0.70, 0.70) => float targetPanB;
        randf(3800.0, 9000.0)::ms => dur glide;

        96 => int steps;
        (targetLowCut - lowLP.freq()) / steps => float dLowCut;
        (targetLowGain - lowG.gain()) / steps => float dLowGain;
        (targetAirCut - airyLP.freq()) / steps => float dAirCut;
        (targetAirGain - airyG.gain()) / steps => float dAirGain;
        (targetPanA - lowP.pan()) / steps => float dPanA;
        (targetPanB - airyP.pan()) / steps => float dPanB;

        for (0 => int i; i < steps; i++)
        {
            lowLP.freq() + dLowCut => lowLP.freq;
            lowG.gain() + dLowGain => lowG.gain;
            airyLP.freq() + dAirCut => airyLP.freq;
            airyG.gain() + dAirGain => airyG.gain;
            lowP.pan() + dPanA => lowP.pan;
            airyP.pan() + dPanB => airyP.pan;
            glide / steps => now;
        }
    }
}

fun void gustRibbon(float panBias, float energy)
{
    Noise n => BPF bp => Gain g => Pan2 p => master;

    clamp(panBias + randf(-0.20, 0.20), -1.0, 1.0) => p.pan;
    randf(460.0, 1250.0 + energy * 1200.0) => bp.freq;
    randf(1.2, 3.8 + energy * 2.4) => bp.Q;
    0.0 => g.gain;

    clamp(randf(0.25, 1.0) * (0.55 + energy * 0.8) * GUST_MAX, 0.003, 0.090) => float peak;

    randf(45.0, 180.0)::ms => dur attack;
    randf(90.0, 360.0)::ms => dur hold;
    randf(850.0, 2800.0)::ms => dur release;

    18 => int aSteps;
    for (0 => int i; i < aSteps; i++)
    {
        peak * (i $ float / (aSteps - 1)) => g.gain;
        attack / aSteps => now;
    }

    hold => now;

    38 => int rSteps;
    for (0 => int i; i < rSteps; i++)
    {
        peak * (1.0 - (i $ float / (rSteps - 1))) => g.gain;
        release / rSteps => now;
    }
}

fun void gustLayer()
{
    while (true)
    {
        gBreeze => float breeze;

        randf(1100.0, 3400.0 - 1200.0 * breeze)::ms => now;

        if (randf(0.0, 1.0) < (0.22 + 0.58 * breeze))
        {
            1 => int cluster;
            if (breeze > 0.58) 2 => cluster;
            if (breeze > 0.83) 3 => cluster;

            for (0 => int i; i < cluster; i++)
            {
                spork ~ gustRibbon(randf(-0.9, 0.9), clamp(breeze + randf(-0.12, 0.20), 0.0, 1.0));
                randf(130.0, 420.0)::ms => now;
            }
        }
    }
}

fun void leafRustle(float side, float intensity)
{
    Noise n => BPF bp => HPF hp => Gain g => Pan2 p => master;

    randf(900.0 + intensity * 620.0, 2600.0 + intensity * 1700.0) => bp.freq;
    randf(2.6, 9.4) => bp.Q;
    170.0 => hp.freq;
    clamp(side + randf(-0.22, 0.22), -1.0, 1.0) => p.pan;

    0.0 => g.gain;
    clamp(randf(0.25, 1.0) * LEAF_MAX * (0.38 + intensity * 0.95), 0.002, 0.11) => float peak;

    14 => int aSteps;
    randf(14.0, 55.0)::ms => dur attack;
    for (0 => int i; i < aSteps; i++)
    {
        peak * (i $ float / (aSteps - 1)) => g.gain;
        attack / aSteps => now;
    }

    randf(20.0, 120.0)::ms => now;

    24 => int rSteps;
    randf(160.0, 980.0)::ms => dur release;
    for (0 => int i; i < rSteps; i++)
    {
        peak * (1.0 - (i $ float / (rSteps - 1))) => g.gain;
        release / rSteps => now;
    }
}

fun void leavesLayer()
{
    while (true)
    {
        gBreeze => float breeze;

        1 => int cluster;
        if (breeze > 0.52) 2 => cluster;
        if (breeze > 0.79) 3 => cluster;

        if (randf(0.0, 1.0) < (0.55 + 0.35 * breeze))
        {
            for (0 => int i; i < cluster; i++)
            {
                spork ~ leafRustle(randf(-0.95, 0.95), clamp(breeze + randf(-0.20, 0.16), 0.0, 1.0));
                randf(30.0, 190.0)::ms => now;
            }
        }

        randf(260.0, 1500.0 - 720.0 * breeze)::ms => now;
    }
}

fun void birdNote(float pan, float fStart, float fEnd, dur d, float amp, float color)
{
    SinOsc s => Gain g => Pan2 p => master;
    TriOsc t => g;

    0.88 => s.gain;
    0.18 + 0.20 * color => t.gain;
    clamp(pan, -1.0, 1.0) => p.pan;
    0.0 => g.gain;

    56 => int steps;
    for (0 => int i; i < steps; i++)
    {
        i $ float / (steps - 1) => float x;
        lerp(fStart, fEnd, x) => float f;
        f => s.freq;
        f * (1.95 + color * 0.22) => t.freq;

        float env;
        if (x < 0.20) x / 0.20 => env;
        else Math.pow(1.0 - ((x - 0.20) / 0.80), 1.7) => env;
        if (env < 0.0) 0.0 => env;

        amp * env => g.gain;
        d / steps => now;
    }
}

fun void birdPhrase(float panCenter, float life)
{
    randf(1450.0, 2600.0) * (0.86 + 0.24 * life) => float base;
    2 + Math.random2(0, 2) => int notes;

    for (0 => int i; i < notes; i++)
    {
        clamp(panCenter + randf(-0.18, 0.18), -1.0, 1.0) => float pan;
        base * randf(0.84, 1.06) => float f0;
        base * randf(1.12, 1.68) => float f1;
        randf(85.0, 230.0)::ms => dur d;
        clamp(BIRD_MAX * randf(0.25, 0.92) * (0.40 + 0.75 * life), 0.002, 0.10) => float a;

        spork ~ birdNote(pan, f0, f1, d, a, randf(0.2, 1.0));
        randf(55.0, 210.0)::ms => now;
    }
}

fun void distantCall(float pan, float life)
{
    SinOsc s => LPF lp => Gain g => Pan2 p => master;

    clamp(pan, -1.0, 1.0) => p.pan;
    randf(900.0, 1400.0) => float f0;
    randf(650.0, 980.0) => float f1;
    1500.0 => lp.freq;
    0.0 => g.gain;

    randf(450.0, 1100.0)::ms => dur d;
    clamp(BIRD_MAX * randf(0.10, 0.30) * (0.35 + 0.7 * life), 0.001, 0.05) => float peak;

    48 => int steps;
    for (0 => int i; i < steps; i++)
    {
        i $ float / (steps - 1) => float x;
        lerp(f0, f1, x) => s.freq;

        float env;
        if (x < 0.35) x / 0.35 => env;
        else Math.pow(1.0 - ((x - 0.35) / 0.65), 1.5) => env;
        if (env < 0.0) 0.0 => env;

        peak * env => g.gain;
        d / steps => now;
    }
}

fun void birdsLayer()
{
    while (true)
    {
        gLife => float life;

        randf(4200.0, 13000.0 - 3500.0 * life)::ms => now;

        if (randf(0.0, 1.0) < (0.18 + 0.62 * life))
        {
            spork ~ birdPhrase(randf(-0.9, 0.9), life);
        }

        if (randf(0.0, 1.0) < (0.05 + 0.16 * life))
        {
            randf(350.0, 1700.0)::ms => now;
            spork ~ distantCall(randf(-1.0, 1.0), life);
        }
    }
}

fun void insectVeil()
{
    Noise n => HPF hp => LPF lp => Gain g => Pan2 p => master;
    SinOsc flutter => blackhole;
    SinOsc sway => blackhole;

    (1.0 / 8.0) => flutter.freq;
    (1.0 / 29.0) => sway.freq;
    0.0 => g.gain;
    0.0 => p.pan;

    while (true)
    {
        gWarmth => float warm;
        gLife => float life;

        (3300.0 + warm * 1800.0) => hp.freq;
        (7100.0 + warm * 2500.0) => lp.freq;

        (flutter.last() * 0.5 + 0.5) => float mod;
        INSECT_MAX * (0.09 + 0.91 * mod) * (0.30 + 0.70 * life) => g.gain;
        clamp(sway.last() * 0.34 + randf(-0.10, 0.10), -1.0, 1.0) => p.pan;

        110::ms => now;
    }
}

fun void skyHush()
{
    Noise n => HPF hp => LPF lp => Gain g => Pan2 p => master;

    4200.0 => hp.freq;
    10800.0 => lp.freq;
    0.004 => g.gain;
    0.0 => p.pan;

    while (true)
    {
        gWarmth => float warm;

        randf(9500.0, 12800.0 + warm * 900.0) => float targetLp;
        randf(3600.0, 6000.0 + warm * 1200.0) => float targetHp;
        SKY_HUSH_MAX * randf(0.35, 1.0) * (0.35 + warm * 0.80) => float targetGain;
        randf(-0.25, 0.25) => float targetPan;
        randf(5600.0, 12800.0)::ms => dur glide;

        88 => int steps;
        (targetLp - lp.freq()) / steps => float dLp;
        (targetHp - hp.freq()) / steps => float dHp;
        (targetGain - g.gain()) / steps => float dG;
        (targetPan - p.pan()) / steps => float dPan;

        for (0 => int i; i < steps; i++)
        {
            lp.freq() + dLp => lp.freq;
            hp.freq() + dHp => hp.freq;
            g.gain() + dG => g.gain;
            p.pan() + dPan => p.pan;
            glide / steps => now;
        }
    }
}

spork ~ macroCycle();
spork ~ windBed();
spork ~ gustLayer();
spork ~ leavesLayer();
spork ~ birdsLayer();
spork ~ insectVeil();
spork ~ skyHush();

while (true)
{
    1::second => now;
}
