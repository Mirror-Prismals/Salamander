// Blockforge Day/Night Suite
// Generative overworld music for a voxel-style adventure game.
// Run with: chuck blockforge_daynight.ck

// ---------- Master Chain ----------
Gain musicBus => LPF worldTone => NRev worldRev => dac;
0.9 => musicBus.gain;
2800 => worldTone.freq;
1.0 => worldTone.Q;
0.22 => worldRev.mix;

Gain echoSend => Delay worldEcho => worldRev;
worldEcho => Gain echoFeedback => worldEcho;
0.38 => echoFeedback.gain;
3::second => worldEcho.max => worldEcho.delay;
0.35 => echoSend.gain;

// ---------- Shared Clock ----------
92.0 => float BPM;
(60.0 / BPM)::second => dur beat;
4 * beat => dur bar;

// ---------- World State ----------
0 => int isNight;
0.15 => float tension; // 0.0 calm -> 1.0 intense

[48, 55, 53, 50] @=> int roots[]; // C3, G3, F3, D3
[0, 2, 4, 7, 9] @=> int dayScale[];          // major pentatonic
[0, 2, 3, 5, 7, 9, 10] @=> int nightScale[]; // dorian-ish

fun int pickScale(int root, int octaveSpan) {
    int pick;
    if(isNight == 0) {
        dayScale[Math.random2(0, dayScale.size() - 1)] => pick;
    } else {
        nightScale[Math.random2(0, nightScale.size() - 1)] => pick;
    }
    return root + pick + (12 * Math.random2(0, octaveSpan));
}

fun float clamp(float x, float lo, float hi) {
    if(x < lo) return lo;
    if(x > hi) return hi;
    return x;
}

// ---------- One-shot Voices ----------
fun void playPadVoice(float freq, float gain, float pan, dur hold) {
    TriOsc oscA => LPF lp => ADSR env => Pan2 p => musicBus;
    TriOsc oscB => lp;
    SinOsc shimmer => lp;

    freq => oscA.freq;
    freq * 1.005 => oscB.freq;
    freq * 2.0 => shimmer.freq;

    gain => oscA.gain;
    (gain * 0.6) => oscB.gain;
    (gain * 0.10) => shimmer.gain;

    p.pan(pan);
    lp.freq(1200 + (isNight == 0 ? 1300 : 300));
    lp.Q(1.1);

    env.set(2::second, 2::second, 0.75, 4::second);
    1 => env.keyOn;

    now + hold => time stop;
    while(now < stop) {
        // slow organic motion
        p.pan(clamp(p.pan() + Math.random2f(-0.01, 0.01), -1.0, 1.0));
        lp.freq(clamp(lp.freq() + Math.random2f(-12, 12), 450, 3600));
        100::ms => now;
    }

    1 => env.keyOff;
    4::second => now;
}

fun void playPluck(float freq, float g, float pan, dur len) {
    SawOsc osc => LPF lp => ADSR env => Pan2 p => musicBus;
    osc => Gain sendTap => echoSend;

    freq => osc.freq;
    g => osc.gain;
    1800 => lp.freq;
    1.3 => lp.Q;
    p.pan(pan);

    env.set(8::ms, 45::ms, 0.2, 180::ms);
    1 => env.keyOn;

    now + len => time stop;
    while(now < stop) {
        // tiny brightness wobble keeps repeats alive in echo
        lp.freq(clamp(lp.freq() + Math.random2f(-40, 40), 900, 3200));
        25::ms => now;
    }

    1 => env.keyOff;
    220::ms => now;
}

fun void playKick(float amp) {
    SinOsc body => LPF lp => ADSR env => musicBus;
    0.7 => lp.Q;
    220 => lp.freq;
    amp => body.gain;
    130 => body.freq;
    env.set(1::ms, 45::ms, 0.0, 80::ms);

    1 => env.keyOn;
    for(0 => int i; i < 16; i++) {
        body.freq() * 0.90 => body.freq;
        4::ms => now;
    }
    1 => env.keyOff;
    90::ms => now;
}

fun void playSnare(float amp) {
    Noise n => BPF bp => ADSR env => musicBus;
    1800 => bp.freq;
    4 => bp.Q;
    amp => n.gain;
    env.set(1::ms, 65::ms, 0.0, 70::ms);

    1 => env.keyOn;
    70::ms => now;
    1 => env.keyOff;
    70::ms => now;
}

fun void playHat(float amp) {
    Noise n => HPF hp => ADSR env => musicBus;
    6000 => hp.freq;
    amp => n.gain;
    env.set(1::ms, 15::ms, 0.0, 20::ms);

    1 => env.keyOn;
    15::ms => now;
    1 => env.keyOff;
    25::ms => now;
}

// ---------- Engines ----------
fun void padEngine() {
    while(true) {
        roots[Math.random2(0, roots.size() - 1)] => int root;

        // stacked chord tones spread over two octaves
        for(0 => int i; i < 4; i++) {
            pickScale(root, 2) => int midi;
            spork ~ playPadVoice(
                Std.mtof(midi),
                Math.random2f(0.03, 0.06),
                Math.random2f(-0.9, 0.9),
                8::second
            );
            400::ms => now;
        }

        (isNight == 0 ? 2::second : 1::second) => now;
    }
}

fun void bassEngine() {
    SawOsc bass => LPF lp => ADSR env => musicBus;
    0.10 => bass.gain;
    220 => lp.freq;
    1.1 => lp.Q;
    env.set(8::ms, 80::ms, 0.35, 120::ms);

    0 => int step;
    while(true) {
        roots[step % roots.size()] => int root;
        (root - 12 + (isNight == 1 && Math.randomf() > 0.6 ? -5 : 0)) => int midi;

        Std.mtof(midi) => bass.freq;
        lp.freq(180 + (isNight == 0 ? 120 : 40) + (tension * 180));

        1 => env.keyOn;
        (beat * 0.45) => now;
        1 => env.keyOff;
        (beat * 0.55) => now;

        step++;
    }
}

fun void leadEngine() {
    while(true) {
        roots[Math.random2(0, roots.size() - 1)] => int root;

        if(Math.randomf() > (isNight == 0 ? 0.35 : 0.15)) {
            pickScale(root + 12, 1) => int midi;
            spork ~ playPluck(
                Std.mtof(midi),
                Math.random2f(0.05, 0.09),
                Math.random2f(-0.8, 0.8),
                beat / 2
            );
        }

        // faster melodic activity at night/tension peaks
        if(isNight == 1 && Math.randomf() > 0.55) {
            pickScale(root + 12, 2) => int midi2;
            spork ~ playPluck(Std.mtof(midi2), 0.045, Math.random2f(-0.8, 0.8), beat / 4);
            (beat / 4) => now;
        }

        (beat / 2) => now;
    }
}

fun void drumEngine() {
    0 => int count;
    while(true) {
        if(count % 4 == 0 || (count % 8 == 6 && isNight == 1)) {
            spork ~ playKick(0.35 + (tension * 0.2));
        }

        if(count % 8 == 4 && Math.randomf() > 0.2) {
            spork ~ playSnare(0.20 + (tension * 0.15));
        }

        if(Math.randomf() > (isNight == 0 ? 0.50 : 0.25)) {
            spork ~ playHat(0.06 + (tension * 0.05));
        }

        (beat / 2) => now;
        count++;
    }
}

fun void worldCycle() {
    while(true) {
        // 16 bars of day
        0 => isNight;
        for(0 => int i; i < 16; i++) {
            tension + 0.015 => tension;
            tension => worldRev.mix;
            worldTone.freq(2500 + 600 * (1.0 - tension));
            bar => now;
        }

        // 16 bars of night
        1 => isNight;
        for(0 => int j; j < 16; j++) {
            tension + 0.025 => tension;
            clamp(tension, 0.2, 0.65) => tension;
            worldRev.mix(clamp(0.20 + tension * 0.35, 0.20, 0.50));
            worldTone.freq(1200 + 400 * (1.0 - tension));
            bar => now;
        }

        // dawn reset
        0.15 => tension;
    }
}

// ---------- Launch ----------
spork ~ padEngine();
spork ~ bassEngine();
//spork ~ leadEngine();
spork ~ drumEngine();
spork ~ worldCycle();

while(true) {
    1::second => now;
}
