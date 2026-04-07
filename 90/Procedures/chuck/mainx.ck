// plains_ambient_loop.ck
// Soothing outdoor/plains ambient loop for a voxel world.
// Runs forever; stop with Ctrl-C.

// ---------- Tweak Knobs ----------
0.34 => float MASTER_GAIN;
0.060 => float WIND_BASE_GAIN;
0.010 => float INSECT_MAX_GAIN;
0.075 => float LEAF_MAX_GAIN;
0.070 => float BIRD_MAX_GAIN;

Gain master => dac;
MASTER_GAIN => master.gain;

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

fun void windBed()
{
    Noise n => LPF lp => HPF hp => Gain g => Pan2 p => master;
    420.0 => lp.freq;
    100.0 => hp.freq;
    WIND_BASE_GAIN => g.gain;
    0.0 => p.pan;

    while (true)
    {
        randf(220.0, 760.0) => float targetCutoff;
        randf(-0.55, 0.55) => float targetPan;
        randf(0.040, 0.090) => float targetGain;
        randf(2800.0, 7600.0)::ms => dur glide;

        90 => int steps;
        (targetCutoff - lp.freq()) / steps => float df;
        (targetPan - p.pan()) / steps => float dp;
        (targetGain - g.gain()) / steps => float dg;

        for (0 => int i; i < steps; i++)
        {
            lp.freq() + df => lp.freq;
            p.pan() + dp => p.pan;
            g.gain() + dg => g.gain;
            glide / steps => now;
        }
    }
}

fun void leafRustle(float panBias)
{
    Noise n => BPF bp => Gain g => Pan2 p => master;
    randf(1000.0, 2800.0) => bp.freq;
    randf(2.0, 8.5) => bp.Q;
    clamp(randf(-0.9, 0.9) + panBias, -1.0, 1.0) => p.pan;

    0.0 => g.gain;
    randf(0.028, LEAF_MAX_GAIN) => float peak;

    16 => int aSteps;
    randf(18.0, 45.0)::ms => dur attack;
    for (0 => int i; i < aSteps; i++)
    {
        (peak * (i $ float / (aSteps - 1))) => g.gain;
        attack / aSteps => now;
    }

    randf(40.0, 120.0)::ms => now;

    24 => int rSteps;
    randf(180.0, 750.0)::ms => dur release;
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
        spork ~ leafRustle(randf(-0.25, 0.25));
        randf(280.0, 1700.0)::ms => now;
    }
}

fun void birdChirp(float pan, float fStart, float fEnd, dur d, float amp)
{
    SinOsc s1 => Gain g => Pan2 p => master;
    SinOsc s2 => g;
    1.0 => s1.gain;
    0.18 => s2.gain;
    clamp(pan, -1.0, 1.0) => p.pan;
    0.0 => g.gain;

    44 => int steps;
    for (0 => int i; i < steps; i++)
    {
        i $ float / (steps - 1) => float t;
        (fStart + (fEnd - fStart) * t) => float f;
        f => s1.freq;
        f * 1.98 => s2.freq;

        float env;
        if (t < 0.20)
        {
            t / 0.20 => env;
        }
        else
        {
            Math.pow(1.0 - ((t - 0.20) / 0.80), 1.7) => env;
        }
        if (env < 0.0) 0.0 => env;

        amp * env => g.gain;
        d / steps => now;
    }
}

fun void birdsLayer()
{
    while (true)
    {
        randf(5000.0, 14000.0)::ms => now;

        // Leave occasional quiet gaps for a natural loop feel.
        if (Math.random2(0, 100) < 75)
        {
            randf(-0.8, 0.8) => float basePan;
            randf(1450.0, 2550.0) => float baseFreq;
            1 + Math.random2(0, 2) => int chirps;

            for (0 => int i; i < chirps; i++)
            {
                spork ~ birdChirp(
                    clamp(basePan + randf(-0.18, 0.18), -1.0, 1.0),
                    baseFreq * randf(0.86, 1.08),
                    baseFreq * randf(1.10, 1.70),
                    randf(90.0, 240.0)::ms,
                    randf(0.030, BIRD_MAX_GAIN)
                );
                randf(70.0, 220.0)::ms => now;
            }
        }
    }
}

fun void insectShimmer()
{
    Noise n => HPF hp => LPF lp => Gain g => Pan2 p => master;
    3600.0 => hp.freq;
    8200.0 => lp.freq;
    0.0 => p.pan;
    0.0 => g.gain;

    SinOsc lfo => blackhole;
    0.07 => lfo.freq;

    while (true)
    {
        (lfo.last() * 0.5 + 0.5) => float x;
        (0.0015 + x * INSECT_MAX_GAIN) => g.gain;
        clamp(randf(-0.35, 0.35), -1.0, 1.0) => p.pan;
        120::ms => now;
    }
}

spork ~ windBed();
spork ~ leavesLayer();
spork ~ birdsLayer();
spork ~ insectShimmer();

while (true)
{
    1::second => now;
}
