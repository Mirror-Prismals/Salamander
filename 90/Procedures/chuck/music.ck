// Simple music playback for ray-traced testing.
SndBuf buf => Gain g => dac.chan(0);
0.25 => g.gain;
me.dir() + "../assets/music.wav" => buf.read;
0 => buf.pos;
1 => buf.loop;
1.0 => buf.rate;
while (true) { 100::ms => now; }
