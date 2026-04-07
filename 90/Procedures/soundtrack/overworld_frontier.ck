// Overworld Frontier
// A second generative soundtrack piece for exploration-heavy gameplay.
// Designed to feel expansive, calm, and quietly adventurous.

// --- Master Chain ---
Gain master => LPF tone => Delay echo => NRev reverb => dac;
echo => Gain feedback => echo;

0.82 => master.gain;
5600 => tone.freq;
0.7 => tone.Q;
0.27 => feedback.gain;
1.2::second => echo.max => echo.delay;
0.24 => reverb.mix;

// --- Musical Grid ---
74.0 => float BPM;
(60.0 / BPM)::second => dur beat;

// A minor/Dorian-leaning palette for "familiar but new" terrain energy.
[0, 2, 3, 5, 7, 9, 10] @=> int SCALE[];
[40, 45, 38, 43, 35, 47] @=> int ROOTS[];

40 => int currentRoot;
0 => int chordColor;
0.0 => float worldLight; // 0 = night, 1 = day

fun float clamp(float v, float lo, float hi) {
    if(v < lo) return lo;
    if(v > hi) return hi;
    return v;
}

fun int pickInt(int values[]) {
    return values[Math.random2(0, values.size() - 1)];
}

fun int degreeToMidi(int root, int degree, int octave) {
    degree % SCALE.size() => int idx;
    if(idx < 0) idx + SCALE.size() => idx;
    return root + SCALE[idx] + (12 * octave);
}

class SkyPad {
    SawOsc body => Gain mix => LPF filt => ADSR env => Pan2 pan => master;
    TriOsc air => mix;
    SinOsc shimmer => mix;

    public void init(float freq, float pos) {
        freq => body.freq;
        freq * 0.502 => air.freq;
        freq * 2.01 => shimmer.freq;

        0.045 => body.gain;
        0.03 => air.gain;
        0.008 => shimmer.gain;

        env.set(4::second, 2::second, 0.62, 8::second);
        1500 => filt.freq;
        0.8 => filt.Q;
        pos => pan.pan;
    }

    public void bloom(dur hold) {
        1 => env.keyOn;
        now + hold => time end;

        while(now < end) {
            (900 + (worldLight * 2800) + Math.random2f(-110, 110)) => filt.freq;
            clamp(pan.pan() + Math.random2f(-0.012, 0.012), -1.0, 1.0) => pan.pan;
            120::ms => now;
        }

        1 => env.keyOff;
        9::second => now;
    }
}

fun void playSpark(int midi, float amp, float p, dur hold) {
    TriOsc osc => LPF filt => ADSR env => Pan2 pan => master;

    Std.mtof(midi) => osc.freq;
    amp => osc.gain;
    clamp(p, -1.0, 1.0) => pan.pan;

    (1700 + (worldLight * 2400) + Math.random2f(-150, 150)) => filt.freq;
    1.9 => filt.Q;

    env.set(8::ms, 45::ms, 0.28, 170::ms);
    1 => env.keyOn;
    hold => now;
    1 => env.keyOff;
    180::ms => now;
}

fun void playBeacon(int midi, float amp, float p, dur hold) {
    SinOsc core => Gain mix => ADSR env => Pan2 pan => master;
    TriOsc halo => mix;

    Std.mtof(midi) => core.freq;
    Std.mtof(midi) * 1.5 => halo.freq;

    amp => core.gain;
    amp * 0.33 => halo.gain;
    clamp(p, -1.0, 1.0) => pan.pan;

    env.set(180::ms, 600::ms, 0.45, 2200::ms);
    1 => env.keyOn;
    hold => now;
    1 => env.keyOff;
    2600::ms => now;
}

fun void dayNightCycle() {
    0 => int step;
    256 => int span;

    while(true) {
        ((Math.sin((((step $ float) / (span $ float)) * 2.0 * Math.PI) - (Math.PI / 2.0)) + 1.0) * 0.5) => worldLight;
        beat / 2 => now;
        (step + 1) % span => step;
    }
}

fun void harmonyDriver() {
    while(true) {
        pickInt(ROOTS) => currentRoot;
        Math.random2(0, 2) => chordColor;

        // Hold each harmonic center for 8-16 beats.
        Math.random2(8, 16) => int holdBeats;
        for(0 => int i; i < holdBeats; i++) {
            beat => now;
        }
    }
}

fun void skyLayer() {
    [0, 2, 4, 6] @=> int shape[];

    while(true) {
        for(0 => int i; i < shape.size(); i++) {
            degreeToMidi(currentRoot, shape[i] + chordColor, Math.random2(0, 1)) => int note;

            SkyPad v;
            v.init(Std.mtof(note), Math.random2f(-0.85, 0.85));
            spork ~ v.bloom((8 + Math.random2(0, 6)) * beat);

            beat + (beat / 2) => now;
        }

        8 * beat => now;
    }
}

fun void crystalArp() {
    [0, 2, 4, 7, 9, 7, 4, 2] @=> int dayPattern[];
    [0, 2, 3, 5, 7, 5, 3, 2] @=> int nightPattern[];
    0 => int step;

    while(true) {
        0 => int degree;
        if(worldLight > 0.45) {
            dayPattern[step % dayPattern.size()] => degree;
        } else {
            nightPattern[step % nightPattern.size()] => degree;
        }

        degreeToMidi(currentRoot, degree + chordColor, 1 + Math.random2(0, 1)) => int note;
        0.045 + (worldLight * 0.03) => float amp;

        spork ~ playSpark(note, amp, Math.random2f(-0.65, 0.65), beat / 3);

        if(step % 2 == 0) {
            beat / 2 => now;
        } else {
            (beat / 2) + 25::ms => now;
        }

        step + 1 => step;
    }
}

fun void rootBass() {
    SqrOsc osc => LPF filt => ADSR env => master;

    0.08 => osc.gain;
    env.set(20::ms, 110::ms, 0.45, 220::ms);

    0 => int step;
    while(true) {
        if(step % 8 == 0 || step % 8 == 4) {
            int degree;
            if(step % 8 == 0) 0 => degree;
            else 4 => degree;

            degreeToMidi(currentRoot, degree + (chordColor % 2), -1) => int note;
            Std.mtof(note) => osc.freq;

            (420 + (worldLight * 700)) => filt.freq;
            1.2 => filt.Q;

            1 => env.keyOn;
            beat / 2 => now;
            1 => env.keyOff;
            beat / 2 => now;
        } else {
            beat => now;
        }

        step + 1 => step;
    }
}

fun void beaconLayer() {
    [11, 9, 7, 4, 2] @=> int beaconDegrees[];

    while(true) {
        // Long breaths between landmarks.
        (12 + Math.random2(0, 12)) * beat => now;

        Math.random2(3, 5) => int notes;
        for(0 => int i; i < notes; i++) {
            degreeToMidi(currentRoot, beaconDegrees[Math.random2(0, beaconDegrees.size() - 1)], 2) => int note;

            0.02 + ((1.0 - worldLight) * 0.02) => float amp;
            spork ~ playBeacon(note, amp, Math.random2f(-0.9, 0.9), beat + (beat / 2));

            beat + (beat / 2) => now;
        }
    }
}

spork ~ dayNightCycle();
spork ~ harmonyDriver();
spork ~ skyLayer();
spork ~ crystalArp();
// spork ~ rootBass();
spork ~ beaconLayer();

while(true) {
    1::second => now;
}
