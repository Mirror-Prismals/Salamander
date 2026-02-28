// Wet Splash Sound Effect (compiles)

Gain master => JCRev rev => dac;
0.3 => rev.mix;

// main splash body
Noise n => BPF bpf => ADSR env => master;
800 => bpf.freq;
4 => bpf.Q;
env.set(10::ms, 120::ms, 0.0, 200::ms);

// low thump
SinOsc low => LPF lpf => ADSR lowEnv => master;
60 => low.freq;
200 => lpf.freq;
lowEnv.set(5::ms, 80::ms, 0.0, 150::ms);

// droplets
fun void droplet(float baseFreq)
{
    SinOsc s => ADSR e => master;
    baseFreq => s.freq;
    0.0 => s.gain;
    e.set(2::ms, 40::ms, 0.0, 80::ms);

    e.keyOn();
    60::ms => now;
    e.keyOff();
}

// gains
0.8 => n.gain;
0.5 => low.gain;

// trigger impact
env.keyOn();
lowEnv.keyOn();

50::ms => now;

// downward filter sweep (use while loop)
float f;
800.0 => f;
while( f > 200.0 )
{
    f => bpf.freq;
    2::ms => now;
    (f - 20.0) => f;
}

env.keyOff();
lowEnv.keyOff();

// droplets
for( 0 => int i; i < 8; i++ )
{
    Math.random2f(400, 2000) => float df;
    spork ~ droplet(df);
    Math.random2(20, 60)::ms => now;
}

5000::ms => now;
