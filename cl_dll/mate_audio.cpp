// The mate's voice in the client.
//
// The server writes each line to a WAV in the mod's sound folder and sends its full path in a
// MateSay user message; MateStop stops it. The playback itself is in mate_wave.cpp.

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"

#include "mate_audio.h"
#include "mate_wave.h"

int __MsgFunc_MateSay(const char *pszName, int iSize, void *pbuf)
{
	const char *path;

	BEGIN_READ(pbuf, iSize);
	path = READ_STRING();
	if (path && path[0])
	{
		bool played = MateWave_Play(path);

		gEngfuncs.Con_Printf("[mate] voice %s: %s\n", path, played ? "playing" : "failed");
	}
	else
		MateWave_Stop();
	return 1;
}

int __MsgFunc_MateStop(const char *pszName, int iSize, void *pbuf)
{
	MateWave_Stop();
	return 1;
}

void MateAudio_Init(void)
{
	MateWave_Stop();
	HOOK_MESSAGE(MateSay);
	HOOK_MESSAGE(MateStop);
}
