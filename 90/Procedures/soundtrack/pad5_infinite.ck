// Level 5: The Infinite Mirror (Masterpiece) - Fixed
// A generative, object-oriented celestial soundscape.
// Features "Harmonic Reflections" and autonomous voice evolution.

// --- Global Master Chain (EffectChain) ---
NRev reverb => dac;
0.3 => reverb.mix;

Delay echo => reverb;
echo => Gain feedback => echo;
0.5 => feedback.gain; // Decay of the echoes
1.5::second => echo.max => echo.delay;

// --- Class for a Single Pad Voice ---
class PadVoice {
    SinOsc mod => TriOsc car => ADSR env => Pan2 p => echo;
    2 => car.sync; // FM Sync
    
    public void init(float rootFreq, float harmonicRatio) {
        rootFreq => car.freq;
        rootFreq * harmonicRatio => mod.freq;
        Math.random2f(50, 150) => mod.gain; // Mirror "Luster"
        
        // Very long swells (8s Attack, 12s Release)
        env.set(8::second, 2::second, 0.7, 12::second);
        Math.random2f(-1.0, 1.0) => p.pan;
        0.05 => car.gain;
    }

    public void play(dur hold) {
        1 => env.keyOn;
        
        // Slow autonomous evolution
        now => time start;
        start + hold => time end;
        while( now < end ) {
            // Stereo drift
            p.pan() + Math.random2f(-0.01, 0.01) => p.pan;
            // Harmonic shimmer drift
            mod.gain() + Math.random2f(-0.2, 0.2) => mod.gain;
            100::ms => now;
        }
        
        1 => env.keyOff;
        15::second => now; // Wait for release tail before shred exits
    }
}

// --- Class for the Generative Mirror System ---
class MirrorSystem {
    // Scales (MIDI offsets) - Lydian Ethereal Mode
    [0, 2, 4, 6, 7, 9, 11] @=> int scale[]; 
    
    public void run() {
        while(true) {
            // Pick a root note (C, G, D, A)
            [36, 43, 38, 45] @=> int roots[];
            roots[Math.random2(0, roots.size()-1)] => int root;
            
            // Build a chord voicing
            for(0 => int i; i < 5; i++) {
                // Pick a note from the scale
                root + scale[Math.random2(0, scale.size()-1)] + (12 * Math.random2(0, 2)) => int note;
                
                // Spork a voice
                spork ~ spawnVoice(Std.mtof(note), Math.random2f(1.5, 3.0));
                
                // Stagger voice starts
                Math.random2f(1, 3)::second => now;
            }
            
            // Wait for next chord wave
            12::second => now;
        }
    }

    private void spawnVoice(float freq, float ratio) {
        PadVoice v;
        v.init(freq, ratio);
        
        // Harmonic Reflection: Occasionally spawn a perfect 5th or Octave
        if(Math.randomf() > 0.6) {
            spork ~ spawnReflection(freq * 1.5, ratio * 0.5); // 5th reflection
        }
        
        v.play(10::second);
    }

    private void spawnReflection(float freq, float ratio) {
        PadVoice v;
        v.init(freq, ratio);
        0.02 => v.car.gain; // Reflections are quieter
        v.play(8::second);
    }
}

// --- Start the Celestial Soundscape ---
MirrorSystem mirror;
mirror.run();

// Keep main thread alive
while(true) 1::second => now;
