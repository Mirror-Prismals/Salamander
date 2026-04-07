Drop soundtrack `.wav` and/or `.ck` files in this folder.

The `SoundtrackSystem` picks a random track, plays it for a timed window
(default 180-300 seconds), fades it out, then picks the next random track.

For `.ck` files:
- They are treated as looping ambience scripts.
- Output routes are rewritten to a dedicated soundtrack channel (`dac.chan(3)` by default)
  so they can be faded and mixed like WAV soundtrack tracks.
