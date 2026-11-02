// waveOut playback, kept in a translation unit of its own: mmsystem.h defines HSPRITE, and
// the client SDK has its own HSPRITE, so the two cannot be in the same file.
#pragma once

bool MateWave_Play(const char *path);
void MateWave_Stop(void);
