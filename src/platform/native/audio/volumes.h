#pragma once

// The player's own volumes, on top of everything the game sets (native/AUDIO.md, "The player's volumes"): music, sound effects
// and voices (the dialogue), each 0 to 1 (1 to start with), and muting everything (while the window isn't in front). What a
// sound processor's voice plays decides its kind, from what the game asked MultiStream for on it: a stream of a file of
// Crash6\Music is music, a stream of a language's bank (Crash6\English and the others: the dialogue, which the game plays as
// music through its music players) is a voice, a sound of a sound bank (PlaySound) is a voice when it's one of the language's
// voices' table (the game's resource table of voices, which SoundById looks in after the sounds'), else an effect. Movies' sound
// counts as music.
// Safe to call from any thread
void NativeAudioSetVolumes(float music, float effects, float voice);
void NativeAudioSetMuted(bool muted);
// The master volume (THE LIST 81): the gain of the whole output, 0 to 1, as it goes to the device (SDL's stream: every sound and
// the movies' together, after the volumes above); kept for a stream opened later
void NativeAudioSetMasterVolume(float gain);
