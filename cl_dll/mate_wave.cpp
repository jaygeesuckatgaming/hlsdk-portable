// waveOut playback of the mate's voice (see mate_wave.h).

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "mate_wave.h"

static HWAVEOUT g_wave = NULL;
static WAVEHDR g_header;
static std::vector<char> g_samples;
static bool g_prepared = false;
static char g_device_file[260] = "";

void MateWave_SetDeviceFile(const char *path)
{
	if (path && path[0])
		snprintf(g_device_file, sizeof(g_device_file), "%s", path);
	else
		g_device_file[0] = 0;
}

static int MateWave_Lower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int MateWave_Contains(const char *haystack, const char *needle)
{
	size_t length = strlen(needle);
	size_t i;

	if (!length)
		return 1;
	for (; *haystack; haystack++)
	{
		for (i = 0; i < length; i++)
		{
			if (!haystack[i] || MateWave_Lower(haystack[i]) != MateWave_Lower(needle[i]))
				break;
		}
		if (i == length)
			return 1;
	}
	return 0;
}

/* the device named in the file (a number, or part of its name), else the system default */
static UINT MateWave_Device(void)
{
	char name[128] = "";
	FILE *file;
	UINT count;
	UINT index;

	if (g_device_file[0])
	{
		file = fopen(g_device_file, "r");
		if (file)
		{
			if (fgets(name, sizeof(name), file))
			{
				size_t length = strlen(name);

				while (length > 0 && (name[length - 1] == '\n' || name[length - 1] == '\r' ||
					name[length - 1] == ' ' || name[length - 1] == '\t'))
					name[--length] = 0;
			}
			fclose(file);
		}
	}
	if (!name[0] || MateWave_Contains(name, "default"))
		return WAVE_MAPPER;
	if (name[0] >= '0' && name[0] <= '9')
		return (UINT)atoi(name);
	count = waveOutGetNumDevs();
	for (index = 0; index < count; index++)
	{
		WAVEOUTCAPSA caps;

		if (waveOutGetDevCapsA(index, &caps, sizeof(caps)) == MMSYSERR_NOERROR &&
			MateWave_Contains(caps.szPname, name))
			return index;
	}
	return WAVE_MAPPER;
}

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

	if (waveOutOpen(&g_wave, MateWave_Device(), &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
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
