// The mate's voice in the client.
//
// The server writes each line to a WAV in the mod's sound folder and sends its name in a
// MateSay user message; MateStop stops it. waveOut is used rather than the engine's mixer so
// stop_audio can stop the mate without stopping the game's gunfire.
#pragma once

void MateAudio_Init(void);
