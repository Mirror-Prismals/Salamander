// Voxel Frontier: Infinite Buildscape
// Generative soundtrack piece for an open world crafting adventure.

92 => float BPM;
(60.0 / BPM)::second => dur beat;
beat / 2 => dur eighth;
4 * beat => dur bar;

Gain master => Delay echo => NRev hall => dac;
0.8 => master.gain;
2.75::second => echo.max => echo.delay;
0.28 => hall.mix;

echo => Gain feedback => echo;
0.32 => feedback.gain;

Gain padBus => master;
Gain bassBus => master;
Gain leadBus => master;
Gain drumBus => master;
Gain airBus => master;

0.55 => padBus.gain;
0.9 => bassBus.gain;
0.35 => leadBus.gain;
0.85 => drumBus.gain;
0.95 => airBus.gain;

[0, 2, 3, 5, 7, 9, 10] @=> int mode[]; // Dorian-ish
[38, 45, 43, 50, 40, 47] @=> int progression[];

now => time launchTime;

fun int scaleToMidi(int root, int degree, int octaveOffset)
{
    return root + mode[degree % mode.size()] + (12 * octaveOffset);
}

fun int currentRoot()
{
    ((now - launchTime) / (2 * bar)) $ int => int idx;
    return progression[idx % progression.size()];
}

fun void padVoice(float freq, dur hold, float panPos)
{
    SawOsc body => LPF tone => ADSR env => Pan2 pan => padBus;
    SinOsc shimmer => tone;

    freq => body.freq;
    (freq * 2.01) => shimmer.freq;

    0.07 => body.gain;
    0.02 => shimmer.gain;

    1250 => tone.freq;
    1.8 => tone.Q;
    panPos => pan.pan;

    env.set(4::second, 1.5::second, 0.55, 5::second);
    1 => env.keyOn;

    now + hold => time end;
    while(now < end)
    {
        tone.freq() + Math.random2f(-40.0, 40.0) => float f;
        if(f < 450.0) { 450.0 => f; }
        if(f > 2600.0) { 2600.0 => f; }
        f => tone.freq;
        200::ms => now;
    }

    1 => env.keyOff;
    5::second => now;
}

fun void skylinePads()
{
    [0, 2, 4, 6] @=> int chordDegrees[];

    while(true)
    {
        currentRoot() => int root;

        for(0 => int i; i < chordDegrees.size(); i++)
        {
            chordDegrees[i] => int degree;
            scaleToMidi(root, degree, 1 + Math.random2(0, 1)) => int note;
            6::second + Math.random2f(0.0, 2.5)::second => dur hold;
            Math.random2f(-0.85, 0.85) => float panPos;
            spork ~ padVoice(Std.mtof(note), hold, panPos);
            120::ms => now;
        }

        2 * bar => now;
    }
}

fun void bassHit(float freq, float accent)
{
    TriOsc growl => LPF low => ADSR env => bassBus;
    SinOsc sub => low;

    freq => growl.freq;
    freq => sub.freq;

    (0.12 * accent) => growl.gain;
    (0.09 * accent) => sub.gain;

    220 => low.freq;
    1.2 => low.Q;

    env.set(8::ms, 110::ms, 0.2, 70::ms);
    1 => env.keyOn;

    150::ms => now;
    1 => env.keyOff;
    120::ms => now;
}

fun void bassline()
{
    [0, 0, 3, 0, 4, 3, 2, 0] @=> int pattern[];
    0 => int step;

    while(true)
    {
        currentRoot() => int root;
        pattern[step % pattern.size()] => int degree;
        scaleToMidi(root, degree, 0) => int note;

        1.0 => float accent;
        if(step % 4 == 0) { 1.35 => accent; }

        spork ~ bassHit(Std.mtof(note), accent);

        eighth => now;
        step++;
    }
}

fun void leadTone(float freq, dur length, float panPos)
{
    SqrOsc lead => LPF lp => ADSR env => Pan2 pan => leadBus;
    SinOsc vib => blackhole;

    freq => lead.freq;
    5.2 => vib.freq;
    6.0 => vib.gain;

    2200 => lp.freq;
    1.0 => lp.Q;

    0.08 => lead.gain;
    panPos => pan.pan;

    env.set(10::ms, 120::ms, 0.35, 110::ms);
    1 => env.keyOn;

    now + length => time end;
    while(now < end)
    {
        freq + vib.last() => lead.freq;
        10::ms => now;
    }

    1 => env.keyOff;
    120::ms => now;
}

fun void crystalLead()
{
    [0, 2, 4, 5, 4, 2, 6, 4, 2, 0] @=> int motif[];
    0 => int step;

    while(true)
    {
        currentRoot() => int root;

        if(Math.randomf() > 0.3)
        {
            motif[step % motif.size()] => int degree;
            scaleToMidi(root, degree, 2) => int note;
            Math.random2f(-0.9, 0.9) => float panPos;
            spork ~ leadTone(Std.mtof(note), beat, panPos);
        }

        beat => now;
        step++;
    }
}

fun void kick(float amp)
{
    SinOsc thump => ADSR env => drumBus;
    175 => thump.freq;
    (0.9 * amp) => thump.gain;

    env.set(2::ms, 65::ms, 0.0, 50::ms);
    1 => env.keyOn;

    for(0 => int i; i < 18; i++)
    {
        175 - (i * 6) => thump.freq;
        4::ms => now;
    }

    1 => env.keyOff;
    60::ms => now;
}

fun void snare(float amp)
{
    Noise n => BPF bp => ADSR env => drumBus;
    1900 => bp.freq;
    2.5 => bp.Q;
    (0.35 * amp) => n.gain;

    env.set(1::ms, 70::ms, 0.0, 50::ms);
    1 => env.keyOn;
    50::ms => now;
    1 => env.keyOff;
    70::ms => now;
}

fun void hat(float amp)
{
    Noise n => HPF hp => ADSR env => drumBus;
    7000 => hp.freq;
    0.6 => hp.Q;
    amp => n.gain;

    env.set(1::ms, 20::ms, 0.0, 20::ms);
    1 => env.keyOn;
    18::ms => now;
    1 => env.keyOff;
    25::ms => now;
}

fun void drums()
{
    0 => int step;

    while(true)
    {
        step % 16 => int slot;

        if(slot == 0 || slot == 6 || slot == 8 || slot == 14)
        {
            spork ~ kick(1.0);
        }

        if(slot == 4 || slot == 12)
        {
            spork ~ snare(1.0);
        }

        if(Math.randomf() > 0.12)
        {
            0.06 => float amp;
            if(slot % 4 == 2) { 0.1 => amp; }
            spork ~ hat(amp);
        }

        if(slot == 15 && Math.randomf() > 0.55)
        {
            spork ~ hat(0.14);
        }

        eighth => now;
        step++;
    }
}

fun void windBed()
{
    Noise wind => LPF lp => ADSR env => Pan2 pan => airBus;
    0.02 => wind.gain;
    env.set(3::second, 2::second, 0.35, 4::second);

    while(true)
    {
        Math.random2f(300.0, 900.0) => lp.freq;
        Math.random2f(-1.0, 1.0) => pan.pan;
        1 => env.keyOn;
        9::second => now;
        1 => env.keyOff;
        5::second => now;
    }
}

fun void sparkle(float freq)
{
    SinOsc s => ADSR env => Pan2 pan => airBus;
    freq => s.freq;
    0.035 => s.gain;
    Math.random2f(-1.0, 1.0) => pan.pan;

    env.set(10::ms, 250::ms, 0.0, 300::ms);
    1 => env.keyOn;
    220::ms => now;
    1 => env.keyOff;
    300::ms => now;
}

fun void skySparkles()
{
    while(true)
    {
        currentRoot() => int root;
        scaleToMidi(root, Math.random2(0, mode.size() - 1), 3) => int note;
        spork ~ sparkle(Std.mtof(note));
        Math.random2f(1.2, 3.8)::second => now;
    }
}

fun void worldCycle()
{
    while(true)
    {
        // Day: brighter pads + stronger drums
        for(0 => int i; i < 64; i++)
        {
            (i / 63.0) => float t;
            0.45 + (0.22 * t) => padBus.gain;
            0.80 + (0.14 * t) => drumBus.gain;
            0.28 + (0.12 * t) => leadBus.gain;
            0.24 + (0.10 * t) => hall.mix;
            eighth => now;
        }

        // Night: softer rhythm, wider atmosphere
        for(0 => int i; i < 64; i++)
        {
            (i / 63.0) => float t;
            0.67 - (0.20 * t) => padBus.gain;
            0.94 - (0.24 * t) => drumBus.gain;
            0.40 - (0.16 * t) => leadBus.gain;
            0.34 + (0.08 * t) => hall.mix;
            eighth => now;
        }
    }
}

spork ~ skylinePads();
//spork ~ bassline();
spork ~ crystalLead();
//spork ~ drums();
spork ~ windBed();
spork ~ skySparkles();
spork ~ worldCycle();

while(true)
{
    1::second => now;
}
