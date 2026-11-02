// waveOut playback of the mate's voice (see mate_wave.h).

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "mate_wave.h"

static HWAVEOUT g_wave = NULL;
static WAVEHDR g_header;
static std::vector<char> g_samples;
static bool g_prepared = false;

void MateWave_Stop(void)
{
	if (g_wave)
	{
		waveOutReset(g_wave);
		if (g_prepared)
		{
			waveOutUnprepareHeader(g_wave, &g_header, sizeof(g_header));
			g_prepared = false;
		}
		waveOutClose(g_wave);
		g_wave = NULL;
	}
	g_samples.clear();
}

bool MateWave_Play(const char *path)
{
	FILE *file;
	long size;
	WAVEFORMATEX format;
	const char *data = NULL;
	DWORD data_size = 0;
	long offset;
	std::vector<char> bytes;

	MateWave_Stop();

	file = fopen(path, "rb");
	if (!file)
		return false;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (size <= 44)
	{
		fclose(file);
		return false;
	}
	bytes.resize((size_t)size);
	if (fread(bytes.data(), 1, bytes.size(), file) != bytes.size())
	{
		fclose(file);
		return false;
	}
	fclose(file);

	if (memcmp(bytes.data(), "RIFF", 4) != 0 || memcmp(bytes.data() + 8, "WAVE", 4) != 0)
		return false;

	memset(&format, 0, sizeof(format));
	for (offset = 12; offset + 8 <= size;)
	{
		const char *id = bytes.data() + offset;
		DWORD chunk = *(const DWORD *)(bytes.data() + offset + 4);

		if (memcmp(id, "fmt ", 4) == 0 && chunk >= 16)
			memcpy(&format, bytes.data() + offset + 8, 16);
		else if (memcmp(id, "data", 4) == 0)
		{
			data = bytes.data() + offset + 8;
			data_size = chunk;
			break;
		}
		offset += 8 + (long)chunk + (long)(chunk & 1);
	}
	if (format.nSamplesPerSec == 0 || !data || data_size == 0)
		return false;

	if (waveOutOpen(&g_wave, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
	{
		g_wave = NULL;
		return false;
	}

	g_samples.assign(data, data + data_size);
	memset(&g_header, 0, sizeof(g_header));
	g_header.lpData = g_samples.data();
	g_header.dwBufferLength = (DWORD)g_samples.size();
	if (waveOutPrepareHeader(g_wave, &g_header, sizeof(g_header)) != MMSYSERR_NOERROR)
	{
		MateWave_Stop();
		return false;
	}
	g_prepared = true;
	if (waveOutWrite(g_wave, &g_header, sizeof(g_header)) != MMSYSERR_NOERROR)
	{
		MateWave_Stop();
		return false;
	}
	return true;
}
