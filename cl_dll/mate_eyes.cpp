// The client half of m8's eyes (see mate_eyes.h). The actual grab is in mate_grab.cpp.

#include <stdio.h>

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"

#include "mate_eyes.h"
#include "mate_grab.h"

static int g_need_view = 0;

int __MsgFunc_MateView(const char *pszName, int iSize, void *pbuf)
{
	g_need_view = 1;
	return 1;
}

void MateEyes_Init(void)
{
	g_need_view = 0;
	HOOK_MESSAGE(MateView);
}

void MateEyes_Frame(void)
{
	char raw[256];
	char tmp[256];
	const char *game;

	if (!g_need_view)
		return;
	g_need_view = 0;
	game = gEngfuncs.pfnGetGameDirectory();
	snprintf(raw, sizeof(raw), "%s/mate_view.raw", game && game[0] ? game : "hlm8");
	snprintf(tmp, sizeof(tmp), "%s/mate_view.tmp", game && game[0] ? game : "hlm8");
	MateGrab_Write(raw, tmp, ScreenWidth, ScreenHeight);
}
